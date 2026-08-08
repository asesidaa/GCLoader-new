// SPDX-License-Identifier: CC0-1.0

#include "Audio/Asio/AsioCapabilityProbe.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <expected>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

int Expect(bool condition, std::string_view name) {
    if (condition) {
        return 0;
    }
    std::cerr << "Expectation failed: " << name << '\n';
    return 1;
}

struct ProbeState {
    std::vector<std::string> calls;
    int factory_calls{};
    long created_channels{};
    long created_frames{};
    ASIOCallbacks* callbacks{};
    ASIOBufferInfo buffers[2]{};
};

class ProbeRegistry final : public gc::audio::IAsioRegistrySource {
public:
    explicit ProbeRegistry(ProbeState& state) : state_(state) {}

    std::expected<
        std::vector<gc::audio::AsioRegistryValue>,
        gc::audio::AsioFailure>
    Read32BitRegistrations() noexcept override {
        state_.calls.push_back("resolve");
        return std::vector<gc::audio::AsioRegistryValue>{
            {
                .subkey_name = L"XONAR SOUND CARD",
                .clsid_text =
                    L"{12345678-1234-5678-9ABC-DEF012345678}",
            },
        };
    }

private:
    ProbeState& state_;
};

class ProbeDriver final : public gc::audio::IAsioDriver {
public:
    explicit ProbeDriver(ProbeState& state) : state_(state) {}
    ~ProbeDriver() override {
        state_.calls.push_back("release");
    }

    ASIOBool Init(HWND) noexcept override {
        state_.calls.push_back("init");
        return ASIOTrue;
    }
    void GetDriverName(char (&name)[32]) noexcept override {
        state_.calls.push_back("getDriverName");
        constexpr std::string_view text{"Xonar AE ASIO"};
        std::memcpy(name, text.data(), text.size());
    }
    long GetDriverVersion() noexcept override {
        state_.calls.push_back("getDriverVersion");
        return 1;
    }
    void GetErrorMessage(char (&message)[124]) noexcept override {
        state_.calls.push_back("getErrorMessage");
        constexpr std::string_view text{"probe error"};
        std::memcpy(message, text.data(), text.size());
    }
    ASIOError Start() noexcept override {
        state_.calls.push_back("start");
        return ASE_OK;
    }
    ASIOError Stop() noexcept override {
        state_.calls.push_back("stop");
        return ASE_OK;
    }
    ASIOError GetChannels(long* inputs, long* outputs) noexcept override {
        state_.calls.push_back("getChannels");
        *inputs = 0;
        *outputs = 8;
        return ASE_OK;
    }
    ASIOError GetLatencies(long* input, long* output) noexcept override {
        state_.calls.push_back("getLatencies");
        *input = 0;
        *output = 384;
        return ASE_OK;
    }
    ASIOError GetBufferSize(
        long* minimum,
        long* maximum,
        long* preferred,
        long* granularity) noexcept override {
        state_.calls.push_back("getBufferSize");
        *minimum = 192;
        *maximum = 2400;
        *preferred = 192;
        *granularity = 1;
        return ASE_OK;
    }
    ASIOError CanSampleRate(ASIOSampleRate rate) noexcept override {
        state_.calls.push_back("canSampleRate");
        return rate == 48'000.0 ? ASE_OK : ASE_NoClock;
    }
    ASIOError GetSampleRate(ASIOSampleRate* rate) noexcept override {
        state_.calls.push_back("getSampleRate");
        *rate = 48'000.0;
        return ASE_OK;
    }
    ASIOError SetSampleRate(ASIOSampleRate) noexcept override {
        state_.calls.push_back("setSampleRate");
        return ASE_OK;
    }
    ASIOError GetSamplePosition(
        ASIOSamples*,
        ASIOTimeStamp*) noexcept override {
        state_.calls.push_back("getSamplePosition");
        return ASE_OK;
    }
    ASIOError GetChannelInfo(ASIOChannelInfo* info) noexcept override {
        state_.calls.push_back(
            "getChannelInfo:" + std::to_string(info->channel));
        info->type = ASIOSTInt24LSB;
        const std::string name = "Xonar Out " +
            std::to_string(info->channel);
        std::memcpy(
            info->name,
            name.data(),
            std::min(name.size(), sizeof(info->name)));
        return ASE_OK;
    }
    ASIOError CreateBuffers(
        ASIOBufferInfo* buffers,
        long channels,
        long frames,
        ASIOCallbacks* callbacks) noexcept override {
        state_.calls.push_back("createBuffers");
        state_.created_channels = channels;
        state_.created_frames = frames;
        state_.callbacks = callbacks;
        if (channels == 2) {
            state_.buffers[0] = buffers[0];
            state_.buffers[1] = buffers[1];
        }
        return ASE_OK;
    }
    ASIOError DisposeBuffers() noexcept override {
        state_.calls.push_back("disposeBuffers");
        return ASE_OK;
    }
    ASIOError Future(long selector, void*) noexcept override {
        state_.calls.push_back("future");
        return selector == kAsioCanReportOverload
            ? ASE_SUCCESS
            : ASE_NotPresent;
    }
    ASIOError OutputReady() noexcept override {
        state_.calls.push_back("outputReady");
        return ASE_OK;
    }

private:
    ProbeState& state_;
};

class ProbeFactory final : public gc::audio::IAsioDriverFactory {
public:
    explicit ProbeFactory(ProbeState& state) : state_(state) {}

    std::expected<
        std::unique_ptr<gc::audio::IAsioDriver>,
        gc::audio::AsioFailure>
    Create(const CLSID&) noexcept override {
        ++state_.factory_calls;
        state_.calls.push_back("CoCreateInstance");
        std::unique_ptr<gc::audio::IAsioDriver> driver =
            std::make_unique<ProbeDriver>(state_);
        return driver;
    }

private:
    ProbeState& state_;
};

std::expected<gc::audio::AsioCapabilityReport, gc::audio::AsioFailure>
RunProbe(
    ProbeState& state,
    const gc::audio::AsioStreamRequest& request,
    gc::audio::AsioProbeMode mode) {
    ProbeRegistry registry{state};
    ProbeFactory factory{state};
    return gc::audio::ProbeAsioCapability(
        registry,
        factory,
        request,
        reinterpret_cast<HWND>(0x1234),
        mode);
}

} // namespace

int main() {
    int failures = 0;

    ProbeState xonar_state;
    const auto xonar = RunProbe(
        xonar_state,
        {
            .driver_name = "XONAR SOUND CARD",
            .buffer_frames = 192,
            .output_base_channel = 0,
        },
        gc::audio::AsioProbeMode::validate);
    failures += Expect(
        xonar &&
            xonar->registration.registry_name == "XONAR SOUND CARD" &&
            xonar->reported_driver_name == "Xonar AE ASIO" &&
            xonar->input_channels == 0 &&
            xonar->output_channels.size() == 8 &&
            xonar->sample_rate == 48'000.0 &&
            xonar->buffer_limits.minimum == 192 &&
            xonar->buffer_limits.maximum == 2400 &&
            xonar->buffer_limits.preferred == 192 &&
            xonar->buffer_limits.granularity == 1 &&
            xonar->effective_buffer_frames == 192 &&
            xonar->output_latency_frames == 384 &&
            xonar->output_ready_supported &&
            xonar->overload_reporting_supported,
        "observed Xonar capabilities validate without vendor branches");
    failures += Expect(
        xonar_state.created_channels == 2 &&
            xonar_state.created_frames == 192 &&
            xonar_state.callbacks != nullptr &&
            xonar_state.buffers[0].isInput == ASIOFalse &&
            xonar_state.buffers[0].channelNum == 0 &&
            xonar_state.buffers[1].isInput == ASIOFalse &&
            xonar_state.buffers[1].channelNum == 1,
        "probe creates only the exact selected stereo pair");
    const std::vector<std::string> expected_order{
        "resolve",
        "CoCreateInstance",
        "init",
        "getDriverName",
        "getDriverVersion",
        "future",
        "getChannels",
        "getSampleRate",
        "canSampleRate",
        "getSampleRate",
        "getBufferSize",
        "getChannelInfo:0",
        "getChannelInfo:1",
        "getChannelInfo:2",
        "getChannelInfo:3",
        "getChannelInfo:4",
        "getChannelInfo:5",
        "getChannelInfo:6",
        "getChannelInfo:7",
        "outputReady",
        "createBuffers",
        "getLatencies",
        "disposeBuffers",
        "release",
    };
    failures += Expect(
        xonar_state.calls == expected_order &&
            std::find(
                xonar_state.calls.begin(),
                xonar_state.calls.end(),
                "start") == xonar_state.calls.end(),
        "probe follows lifecycle order and never starts audio");

    ProbeState inspect_state;
    const auto inspected = RunProbe(
        inspect_state,
        {
            .driver_name = "XONAR SOUND CARD",
            .buffer_frames = 0,
            .output_base_channel = 0,
        },
        gc::audio::AsioProbeMode::inspect);
    failures += Expect(
        inspected && inspected->effective_buffer_frames == 192 &&
            inspect_state.created_frames == 192,
        "inspection uses the reported preferred size");

    ProbeState zero_validate_state;
    const auto zero_validate = RunProbe(
        zero_validate_state,
        {
            .driver_name = "XONAR SOUND CARD",
            .buffer_frames = 0,
            .output_base_channel = 0,
        },
        gc::audio::AsioProbeMode::validate);
    failures += Expect(
        !zero_validate &&
            zero_validate.error().stage ==
                gc::audio::AsioFailureStage::buffer_metadata &&
            std::find(
                zero_validate_state.calls.begin(),
                zero_validate_state.calls.end(),
                "createBuffers") == zero_validate_state.calls.end(),
        "validation rejects zero without substituting preferred frames");

    ProbeState missing_state;
    const auto missing = RunProbe(
        missing_state,
        {
            .driver_name = "Arbitrary Missing ASIO",
            .buffer_frames = 192,
            .output_base_channel = 0,
        },
        gc::audio::AsioProbeMode::validate);
    failures += Expect(
        !missing &&
            missing.error().stage ==
                gc::audio::AsioFailureStage::registry &&
            missing_state.factory_calls == 0 &&
            missing_state.calls == std::vector<std::string>{"resolve"},
        "missing arbitrary name fails before loading vendor code");

    return failures == 0 ? 0 : 1;
}

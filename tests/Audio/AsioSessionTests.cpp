// SPDX-License-Identifier: CC0-1.0

#include "Audio/Asio/AsioSession.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iostream>
#include <limits>
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

struct FakeState {
    std::vector<std::string> calls;
    ASIOBool init_result{ASIOTrue};
    std::string driver_name{"Fake ASIO"};
    std::string error_message{"fake driver error"};
    long driver_version{42};
    ASIOError future_result{ASE_SUCCESS};
    ASIOError channels_result{ASE_OK};
    long input_channels{};
    long output_channels{3};
    ASIOError get_sample_rate_result{ASE_OK};
    int fail_get_sample_rate_call{};
    ASIOError failed_get_sample_rate_result{ASE_HWMalfunction};
    int get_sample_rate_calls{};
    double current_sample_rate{48'000.0};
    ASIOError can_sample_rate_result{ASE_OK};
    ASIOError set_sample_rate_result{ASE_OK};
    int fail_set_sample_rate_call{};
    ASIOError failed_set_sample_rate_result{ASE_HWMalfunction};
    int set_sample_rate_calls{};
    bool apply_sample_rate{true};
    ASIOError buffer_size_result{ASE_OK};
    gc::audio::AsioBufferLimits buffer_limits{192, 2400, 192, 1};
    std::vector<ASIOSampleType> output_types{
        ASIOSTInt24LSB,
        ASIOSTInt24LSB,
        ASIOSTInt24LSB,
    };
    ASIOError channel_info_result{ASE_OK};
    long fail_channel{-1};
    ASIOError output_ready_result{ASE_OK};
    ASIOError create_buffers_result{ASE_OK};
    ASIOError latencies_result{ASE_OK};
    long input_latency{};
    long output_latency{384};
    ASIOError start_result{ASE_OK};
    ASIOError stop_result{ASE_OK};
    ASIOError dispose_result{ASE_OK};
    long created_channel_count{};
    long created_buffer_frames{};
    ASIOCallbacks* created_callbacks{};
    std::array<ASIOBufferInfo, 2> created_buffers{};
};

class FakeDriver final : public gc::audio::IAsioDriver {
public:
    explicit FakeDriver(FakeState& state) : state_(state) {}
    ~FakeDriver() override {
        state_.calls.push_back("release");
    }

    ASIOBool Init(HWND) noexcept override {
        state_.calls.push_back("init");
        return state_.init_result;
    }

    void GetDriverName(char (&name)[32]) noexcept override {
        state_.calls.push_back("getDriverName");
        CopyText(state_.driver_name, name, sizeof(name));
    }

    long GetDriverVersion() noexcept override {
        state_.calls.push_back("getDriverVersion");
        return state_.driver_version;
    }

    void GetErrorMessage(char (&message)[124]) noexcept override {
        state_.calls.push_back("getErrorMessage");
        CopyText(state_.error_message, message, sizeof(message));
    }

    ASIOError Start() noexcept override {
        state_.calls.push_back("start");
        return state_.start_result;
    }

    ASIOError Stop() noexcept override {
        state_.calls.push_back("stop");
        return state_.stop_result;
    }

    ASIOError GetChannels(long* inputs, long* outputs) noexcept override {
        state_.calls.push_back("getChannels");
        *inputs = state_.input_channels;
        *outputs = state_.output_channels;
        return state_.channels_result;
    }

    ASIOError GetLatencies(long* input, long* output) noexcept override {
        state_.calls.push_back("getLatencies");
        *input = state_.input_latency;
        *output = state_.output_latency;
        return state_.latencies_result;
    }

    ASIOError GetBufferSize(
        long* minimum,
        long* maximum,
        long* preferred,
        long* granularity) noexcept override {
        state_.calls.push_back("getBufferSize");
        *minimum = state_.buffer_limits.minimum;
        *maximum = state_.buffer_limits.maximum;
        *preferred = state_.buffer_limits.preferred;
        *granularity = state_.buffer_limits.granularity;
        return state_.buffer_size_result;
    }

    ASIOError CanSampleRate(ASIOSampleRate) noexcept override {
        state_.calls.push_back("canSampleRate");
        return state_.can_sample_rate_result;
    }

    ASIOError GetSampleRate(ASIOSampleRate* rate) noexcept override {
        state_.calls.push_back("getSampleRate");
        ++state_.get_sample_rate_calls;
        *rate = state_.current_sample_rate;
        if (state_.fail_get_sample_rate_call ==
            state_.get_sample_rate_calls) {
            return state_.failed_get_sample_rate_result;
        }
        return state_.get_sample_rate_result;
    }

    ASIOError SetSampleRate(ASIOSampleRate rate) noexcept override {
        state_.calls.push_back("setSampleRate");
        ++state_.set_sample_rate_calls;
        if (state_.fail_set_sample_rate_call ==
            state_.set_sample_rate_calls) {
            return state_.failed_set_sample_rate_result;
        }
        if (state_.set_sample_rate_result == ASE_OK &&
            state_.apply_sample_rate) {
            state_.current_sample_rate = rate;
        }
        return state_.set_sample_rate_result;
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
        if (info->channel == state_.fail_channel) {
            return state_.channel_info_result;
        }
        const auto index = static_cast<std::size_t>(info->channel);
        info->type = index < state_.output_types.size()
            ? state_.output_types[index]
            : ASIOSTInt24LSB;
        const std::string name = "Output " + std::to_string(info->channel);
        CopyText(name, info->name, sizeof(info->name));
        return ASE_OK;
    }

    ASIOError CreateBuffers(
        ASIOBufferInfo* buffers,
        long channel_count,
        long buffer_frames,
        ASIOCallbacks* callbacks) noexcept override {
        state_.calls.push_back("createBuffers");
        state_.created_channel_count = channel_count;
        state_.created_buffer_frames = buffer_frames;
        state_.created_callbacks = callbacks;
        if (channel_count == 2) {
            state_.created_buffers[0] = buffers[0];
            state_.created_buffers[1] = buffers[1];
        }
        return state_.create_buffers_result;
    }

    ASIOError DisposeBuffers() noexcept override {
        state_.calls.push_back("disposeBuffers");
        return state_.dispose_result;
    }

    ASIOError ControlPanel() noexcept override {
        state_.calls.push_back("controlPanel");
        return ASE_NotPresent;
    }

    ASIOError Future(long, void*) noexcept override {
        state_.calls.push_back("future");
        return state_.future_result;
    }

    ASIOError OutputReady() noexcept override {
        state_.calls.push_back("outputReady");
        return state_.output_ready_result;
    }

private:
    static void CopyText(
        std::string_view source,
        char* destination,
        std::size_t capacity) noexcept {
        const std::size_t count = std::min(source.size(), capacity);
        std::memcpy(destination, source.data(), count);
    }

    FakeState& state_;
};

gc::audio::AsioDriverRegistration Registration() {
    return {
        .registry_name = "Fake Registration",
        .clsid = {},
    };
}

gc::audio::AsioStreamRequest Request(
    std::uint32_t frames = 192,
    std::uint32_t base = 0) {
    return {
        .driver_name = "Fake Registration",
        .buffer_frames = frames,
        .output_base_channel = base,
    };
}

template <typename Configure>
int ExpectPrepareFailure(
    Configure configure,
    gc::audio::AsioFailureStage stage,
    std::string_view name,
    gc::audio::AsioStreamRequest request = Request(),
    gc::audio::AsioProbeMode mode =
        gc::audio::AsioProbeMode::validate) {
    FakeState state;
    configure(state);
    auto result = gc::audio::AsioSession::Prepare(
        Registration(),
        std::make_unique<FakeDriver>(state),
        request,
        reinterpret_cast<HWND>(0x1234),
        mode,
        true);
    const bool matches = !result && result.error().stage == stage;
    if (result) {
        (void)(*result)->Close();
    }
    return Expect(matches, name);
}

ASIOCallbacks InertCallbacks() {
    return {};
}

} // namespace

int main() {
    using gc::audio::AsioFailureStage;
    using gc::audio::AsioProbeMode;

    int failures = 0;

    FakeState success;
    success.current_sample_rate = 44'100.0;
    success.output_channels = 4;
    success.output_types = {
        ASIOSTInt16MSB,
        ASIOSTInt24LSB,
        ASIOSTFloat32LSB,
        ASIOSTDSDInt8LSB1,
    };
    auto session_result = gc::audio::AsioSession::Prepare(
        Registration(),
        std::make_unique<FakeDriver>(success),
        Request(192, 1),
        reinterpret_cast<HWND>(0x1234),
        AsioProbeMode::validate,
        true);
    failures += Expect(
        session_result.has_value(),
        "valid mixed selected formats prepare successfully");
    if (!session_result) {
        return 1;
    }
    auto session = std::move(*session_result);
    const auto& initial_report = session->report();
    failures += Expect(
        initial_report.registration.registry_name == "Fake Registration" &&
            initial_report.reported_driver_name == "Fake ASIO" &&
            initial_report.driver_version == 42 &&
            initial_report.original_sample_rate == 44'100.0 &&
            initial_report.sample_rate == 48'000.0 &&
            initial_report.input_channels == 0 &&
            initial_report.output_channels.size() == 4 &&
            initial_report.output_channels[0].sample_type ==
                ASIOSTInt16MSB &&
            initial_report.selected_base_channel == 1 &&
            initial_report.effective_buffer_frames == 192 &&
            initial_report.output_ready_supported &&
            initial_report.overload_reporting_supported,
        "preparation report retains identity and all output formats");

    ASIOCallbacks callbacks = InertCallbacks();
    failures += Expect(
        session->CreateOutputBuffers(&callbacks).has_value(),
        "selected pair creates exact buffers");
    failures += Expect(
        success.created_channel_count == 2 &&
            success.created_buffer_frames == 192 &&
            success.created_callbacks == &callbacks &&
            success.created_buffers[0].isInput == ASIOFalse &&
            success.created_buffers[0].channelNum == 1 &&
            success.created_buffers[1].isInput == ASIOFalse &&
            success.created_buffers[1].channelNum == 2 &&
            session->buffers().size() == 2 &&
            session->report().output_latency_frames == 384,
        "createBuffers receives adjacent output channels and exact frames");
    failures += Expect(
        !session->CreateOutputBuffers(&callbacks) &&
            success.created_channel_count == 2,
        "output buffers may be created exactly once");
    failures += Expect(
        session->Start().has_value(),
        "prepared buffers start");
    failures += Expect(
        session->Stop().has_value(),
        "started session stops");
    failures += Expect(
        session->Close().has_value(),
        "session disposes, restores, and releases");

    const std::vector<std::string> expected_order{
        "init",
        "getDriverName",
        "getDriverVersion",
        "future",
        "getChannels",
        "getSampleRate",
        "canSampleRate",
        "setSampleRate",
        "getSampleRate",
        "getBufferSize",
        "getChannelInfo:0",
        "getChannelInfo:1",
        "getChannelInfo:2",
        "getChannelInfo:3",
        "outputReady",
        "createBuffers",
        "getLatencies",
        "start",
        "stop",
        "disposeBuffers",
        "setSampleRate",
        "release",
    };
    failures += Expect(
        success.calls == expected_order &&
            success.current_sample_rate == 44'100.0,
        "lifecycle follows SDK order and restores rate in reverse order");

    failures += ExpectPrepareFailure(
        [](FakeState& state) { state.init_result = ASIOFalse; },
        AsioFailureStage::init,
        "init failure is typed");
    failures += ExpectPrepareFailure(
        [](FakeState& state) { state.future_result = ASE_HWMalfunction; },
        AsioFailureStage::identity,
        "overload capability failure is typed");
    failures += ExpectPrepareFailure(
        [](FakeState& state) { state.channels_result = ASE_HWMalfunction; },
        AsioFailureStage::channels,
        "channel query failure is typed");
    failures += ExpectPrepareFailure(
        [](FakeState& state) { state.input_channels = -1; },
        AsioFailureStage::channels,
        "negative channel count is rejected");
    failures += ExpectPrepareFailure(
        [](FakeState& state) {
            state.output_channels =
                gc::audio::kMaxAsioReportedChannels + 1;
        },
        AsioFailureStage::channels,
        "channel count above the protocol bound is rejected");
    failures += ExpectPrepareFailure(
        [](FakeState& state) {
            state.fail_get_sample_rate_call = 1;
        },
        AsioFailureStage::sample_rate,
        "initial sample-rate query failure is typed");
    failures += ExpectPrepareFailure(
        [](FakeState& state) {
            state.current_sample_rate =
                std::numeric_limits<double>::quiet_NaN();
        },
        AsioFailureStage::sample_rate,
        "non-finite current sample rate is rejected");
    failures += ExpectPrepareFailure(
        [](FakeState& state) {
            state.can_sample_rate_result = ASE_NoClock;
        },
        AsioFailureStage::sample_rate,
        "unsupported 48 kHz is rejected");
    failures += ExpectPrepareFailure(
        [](FakeState& state) {
            state.current_sample_rate = 44'100.0;
            state.set_sample_rate_result = ASE_HWMalfunction;
        },
        AsioFailureStage::sample_rate,
        "sample-rate change failure is typed");
    failures += ExpectPrepareFailure(
        [](FakeState& state) {
            state.current_sample_rate = 44'100.0;
            state.apply_sample_rate = false;
        },
        AsioFailureStage::sample_rate,
        "driver must report exact 48 kHz after setting");
    failures += ExpectPrepareFailure(
        [](FakeState& state) {
            state.buffer_size_result = ASE_HWMalfunction;
        },
        AsioFailureStage::buffer_metadata,
        "buffer metadata driver failure is typed");
    failures += ExpectPrepareFailure(
        [](FakeState& state) {
            state.buffer_limits = {64, 1024, 256, 0};
        },
        AsioFailureStage::buffer_metadata,
        "inconsistent buffer metadata is rejected");
    failures += ExpectPrepareFailure(
        [](FakeState&) {},
        AsioFailureStage::buffer_metadata,
        "unsupported exact frames are rejected",
        Request(191));
    failures += ExpectPrepareFailure(
        [](FakeState&) {},
        AsioFailureStage::buffer_metadata,
        "validation mode rejects zero frames",
        Request(0),
        AsioProbeMode::validate);
    failures += ExpectPrepareFailure(
        [](FakeState& state) {
            state.fail_channel = 1;
            state.channel_info_result = ASE_HWMalfunction;
        },
        AsioFailureStage::channel_info,
        "selected channel query failure is typed");
    failures += ExpectPrepareFailure(
        [](FakeState& state) { state.output_channels = 1; },
        AsioFailureStage::channel_info,
        "fewer than two adjacent outputs are rejected");
    failures += ExpectPrepareFailure(
        [](FakeState& state) {
            state.output_types[0] = ASIOSTInt16MSB;
        },
        AsioFailureStage::channel_info,
        "unsupported selected format is rejected");
    failures += ExpectPrepareFailure(
        [](FakeState& state) {
            state.output_ready_result = ASE_HWMalfunction;
        },
        AsioFailureStage::output_ready_probe,
        "unexpected output-ready result is typed");

    FakeState inspect;
    auto inspection = gc::audio::AsioSession::Prepare(
        Registration(),
        std::make_unique<FakeDriver>(inspect),
        Request(0),
        reinterpret_cast<HWND>(0x1234),
        AsioProbeMode::inspect,
        true);
    failures += Expect(
        inspection &&
            (*inspection)->report().effective_buffer_frames == 192,
        "inspection mode uses preferred frames only for zero request");
    if (inspection) {
        (void)(*inspection)->Close();
    }

    FakeState unsupported_capabilities;
    unsupported_capabilities.future_result = ASE_OK;
    unsupported_capabilities.output_ready_result = ASE_NotPresent;
    auto unsupported_capability_session =
        gc::audio::AsioSession::Prepare(
            Registration(),
            std::make_unique<FakeDriver>(unsupported_capabilities),
            Request(),
            reinterpret_cast<HWND>(0x1234),
            AsioProbeMode::validate,
            true);
    failures += Expect(
        unsupported_capability_session &&
            !(*unsupported_capability_session)
                 ->report().overload_reporting_supported &&
            !(*unsupported_capability_session)
                 ->report().output_ready_supported,
        "ASE_OK future and ASE_NotPresent outputReady mean unsupported");
    if (unsupported_capability_session) {
        (void)(*unsupported_capability_session)->Close();
    }

    auto ExpectPostPrepareFailure = [&](auto configure,
                                        AsioFailureStage stage,
                                        std::string_view name) {
        FakeState state;
        configure(state);
        auto prepared = gc::audio::AsioSession::Prepare(
            Registration(),
            std::make_unique<FakeDriver>(state),
            Request(),
            reinterpret_cast<HWND>(0x1234),
            AsioProbeMode::validate,
            true);
        if (!prepared) {
            return Expect(false, name);
        }
        ASIOCallbacks inert{};
        const auto result = (*prepared)->CreateOutputBuffers(&inert);
        const bool matches = !result && result.error().stage == stage;
        return Expect(matches, name);
    };
    failures += ExpectPostPrepareFailure(
        [](FakeState& state) {
            state.create_buffers_result = ASE_HWMalfunction;
        },
        AsioFailureStage::create_buffers,
        "createBuffers failure is typed");
    failures += ExpectPostPrepareFailure(
        [](FakeState& state) {
            state.latencies_result = ASE_HWMalfunction;
        },
        AsioFailureStage::latency,
        "latency query failure is typed");
    failures += ExpectPostPrepareFailure(
        [](FakeState& state) { state.output_latency = -1; },
        AsioFailureStage::latency,
        "negative output latency is rejected");

    FakeState null_callbacks_state;
    auto null_callbacks_session = gc::audio::AsioSession::Prepare(
        Registration(),
        std::make_unique<FakeDriver>(null_callbacks_state),
        Request(),
        reinterpret_cast<HWND>(0x1234),
        AsioProbeMode::validate,
        true);
    if (null_callbacks_session) {
        const auto created =
            (*null_callbacks_session)->CreateOutputBuffers(nullptr);
        failures += Expect(
            !created && created.error().stage ==
                AsioFailureStage::callback_prepare &&
                std::find(
                    null_callbacks_state.calls.begin(),
                    null_callbacks_state.calls.end(),
                    "createBuffers") == null_callbacks_state.calls.end(),
            "null callbacks fail before createBuffers");
        (void)(*null_callbacks_session)->Close();
    } else {
        failures += Expect(false, "null-callback fixture prepares");
    }

    FakeState start_failure;
    start_failure.start_result = ASE_HWMalfunction;
    auto start_session = gc::audio::AsioSession::Prepare(
        Registration(),
        std::make_unique<FakeDriver>(start_failure),
        Request(),
        reinterpret_cast<HWND>(0x1234),
        AsioProbeMode::validate,
        true);
    if (start_session) {
        ASIOCallbacks inert{};
        (void)(*start_session)->CreateOutputBuffers(&inert);
        const auto started = (*start_session)->Start();
        failures += Expect(
            !started && started.error().stage == AsioFailureStage::start &&
                started.error().domain ==
                    gc::audio::AsioResultDomain::asio &&
                started.error().result == ASE_HWMalfunction &&
                started.error().driver_message == "fake driver error",
            "start failure retains ASIO domain, value, and driver text");
        (void)(*start_session)->Close();
    } else {
        failures += Expect(false, "start failure fixture prepares");
    }

    FakeState stop_failure;
    auto stop_session = gc::audio::AsioSession::Prepare(
        Registration(),
        std::make_unique<FakeDriver>(stop_failure),
        Request(),
        reinterpret_cast<HWND>(0x1234),
        AsioProbeMode::validate,
        true);
    if (stop_session) {
        ASIOCallbacks inert{};
        (void)(*stop_session)->CreateOutputBuffers(&inert);
        (void)(*stop_session)->Start();
        stop_failure.stop_result = ASE_HWMalfunction;
        const auto closed = (*stop_session)->Close();
        failures += Expect(
            !closed && closed.error().stage == AsioFailureStage::stop &&
                std::find(
                    stop_failure.calls.begin(),
                    stop_failure.calls.end(),
                    "disposeBuffers") == stop_failure.calls.end(),
            "failed stop blocks buffer disposal");
        stop_failure.stop_result = ASE_OK;
        failures += Expect(
            (*stop_session)->Close().has_value(),
            "stop failure can be retried before safe cleanup");
    } else {
        failures += Expect(false, "stop failure fixture prepares");
    }

    FakeState dispose_failure;
    auto dispose_session = gc::audio::AsioSession::Prepare(
        Registration(),
        std::make_unique<FakeDriver>(dispose_failure),
        Request(),
        reinterpret_cast<HWND>(0x1234),
        AsioProbeMode::validate,
        true);
    if (dispose_session) {
        ASIOCallbacks inert{};
        (void)(*dispose_session)->CreateOutputBuffers(&inert);
        dispose_failure.dispose_result = ASE_HWMalfunction;
        const auto closed = (*dispose_session)->Close();
        failures += Expect(
            !closed && closed.error().stage ==
                AsioFailureStage::dispose,
            "disposeBuffers failure is typed");
        dispose_failure.dispose_result = ASE_OK;
        failures += Expect(
            (*dispose_session)->Close().has_value(),
            "dispose failure can be retried before release");
    } else {
        failures += Expect(false, "dispose failure fixture prepares");
    }

    FakeState restore_failure;
    restore_failure.current_sample_rate = 44'100.0;
    restore_failure.fail_set_sample_rate_call = 2;
    auto restore_session = gc::audio::AsioSession::Prepare(
        Registration(),
        std::make_unique<FakeDriver>(restore_failure),
        Request(),
        reinterpret_cast<HWND>(0x1234),
        AsioProbeMode::validate,
        true);
    if (restore_session) {
        const auto closed = (*restore_session)->Close();
        failures += Expect(
            !closed && closed.error().stage ==
                AsioFailureStage::restore_sample_rate,
            "explicit close reports sample-rate restoration failure");
    } else {
        failures += Expect(false, "restore failure fixture prepares");
    }

    FakeState no_restore;
    no_restore.current_sample_rate = 44'100.0;
    auto no_restore_session = gc::audio::AsioSession::Prepare(
        Registration(),
        std::make_unique<FakeDriver>(no_restore),
        Request(),
        reinterpret_cast<HWND>(0x1234),
        AsioProbeMode::validate,
        false);
    if (no_restore_session) {
        failures += Expect(
            (*no_restore_session)->Close().has_value() &&
                no_restore.set_sample_rate_calls == 1 &&
                no_restore.current_sample_rate == 48'000.0,
            "runtime session may deliberately retain the committed rate");
    } else {
        failures += Expect(false, "non-restoring fixture prepares");
    }

    return failures == 0 ? 0 : 1;
}

// SPDX-License-Identifier: CC0-1.0

#include "Audio/Asio/AsioControlPanel.h"

#include <Windows.h>

#include <algorithm>
#include <cstring>
#include <expected>
#include <iostream>
#include <memory>
#include <optional>
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

struct PanelState {
    std::vector<std::string> calls;
    int factory_calls{};
    int create_buffers{};
    int starts{};
    int stops{};
    int set_sample_rate{};
    HWND initialized_with{};
    HWND waited_with{};
    ASIOBool init_result{ASIOTrue};
    ASIOError panel_result{ASE_OK};
    std::string driver_message{"driver panel failed"};
};

class PanelRegistry final : public gc::audio::IAsioRegistrySource {
public:
    explicit PanelRegistry(PanelState& state) : state_(state) {}

    std::expected<
        std::vector<gc::audio::AsioRegistryValue>,
        gc::audio::AsioFailure>
    Read32BitRegistrations() noexcept override {
        state_.calls.push_back("resolve");
        return values;
    }

    std::vector<gc::audio::AsioRegistryValue> values{
        {
            .subkey_name = L"XONAR SOUND CARD",
            .clsid_text = L"{12345678-1234-5678-9ABC-DEF012345678}",
        },
    };

private:
    PanelState& state_;
};

class PanelDriver final : public gc::audio::IAsioDriver {
public:
    explicit PanelDriver(PanelState& state) : state_(state) {}

    ~PanelDriver() override {
        state_.calls.push_back("release");
    }

    ASIOBool Init(HWND owner) noexcept override {
        state_.calls.push_back("init");
        state_.initialized_with = owner;
        return state_.init_result;
    }

    void GetDriverName(char (&)[32]) noexcept override {}
    long GetDriverVersion() noexcept override { return 1; }

    void GetErrorMessage(char (&message)[124]) noexcept override {
        state_.calls.push_back("getErrorMessage");
        const auto count = std::min(
            state_.driver_message.size(),
            sizeof(message) - 1);
        std::memcpy(message, state_.driver_message.data(), count);
    }

    ASIOError Start() noexcept override {
        ++state_.starts;
        return ASE_OK;
    }

    ASIOError Stop() noexcept override {
        ++state_.stops;
        return ASE_OK;
    }

    ASIOError GetChannels(long*, long*) noexcept override {
        return ASE_OK;
    }

    ASIOError GetLatencies(long*, long*) noexcept override {
        return ASE_OK;
    }

    ASIOError GetBufferSize(long*, long*, long*, long*) noexcept override {
        return ASE_OK;
    }

    ASIOError CanSampleRate(ASIOSampleRate) noexcept override {
        return ASE_OK;
    }

    ASIOError GetSampleRate(ASIOSampleRate*) noexcept override {
        return ASE_OK;
    }

    ASIOError SetSampleRate(ASIOSampleRate) noexcept override {
        ++state_.set_sample_rate;
        return ASE_OK;
    }

    ASIOError GetSamplePosition(ASIOSamples*, ASIOTimeStamp*) noexcept override {
        return ASE_OK;
    }

    ASIOError GetChannelInfo(ASIOChannelInfo*) noexcept override {
        return ASE_OK;
    }

    ASIOError CreateBuffers(
        ASIOBufferInfo*, long, long, ASIOCallbacks*) noexcept override {
        ++state_.create_buffers;
        return ASE_OK;
    }

    ASIOError DisposeBuffers() noexcept override { return ASE_OK; }

    ASIOError ControlPanel() noexcept override {
        state_.calls.push_back("controlPanel");
        return state_.panel_result;
    }

    ASIOError Future(long, void*) noexcept override { return ASE_OK; }
    ASIOError OutputReady() noexcept override { return ASE_OK; }

private:
    PanelState& state_;
};

class PanelFactory final : public gc::audio::IAsioDriverFactory {
public:
    explicit PanelFactory(PanelState& state) : state_(state) {}

    std::expected<
        std::unique_ptr<gc::audio::IAsioDriver>,
        gc::audio::AsioFailure>
    Create(const CLSID&) noexcept override {
        ++state_.factory_calls;
        state_.calls.push_back("create");
        if (failure) {
            return std::unexpected(*failure);
        }
        std::unique_ptr<gc::audio::IAsioDriver> driver =
            std::make_unique<PanelDriver>(state_);
        return driver;
    }

    std::optional<gc::audio::AsioFailure> failure;

private:
    PanelState& state_;
};

void WaitForPanel(void* context, HWND owner) noexcept {
    auto& state = *static_cast<PanelState*>(context);
    state.calls.push_back("wait");
    state.waited_with = owner;
}

gc::audio::AsioControlPanelActions Actions(PanelState& state) {
    return {
        .context = &state,
        .wait_for_visible_windows = &WaitForPanel,
    };
}

std::expected<void, gc::audio::AsioFailure> Run(
    PanelState& state,
    PanelRegistry& registry,
    PanelFactory& factory,
    HWND owner = reinterpret_cast<HWND>(0x1234)) {
    return gc::audio::OpenAsioControlPanel(
        registry,
        factory,
        {.driver_name = "XONAR SOUND CARD"},
        owner,
        Actions(state));
}

int CheckSuccess(ASIOError result, std::string_view name) {
    PanelState state{.panel_result = result};
    PanelRegistry registry{state};
    PanelFactory factory{state};
    const HWND owner = reinterpret_cast<HWND>(0x1234);
    const auto opened = Run(state, registry, factory, owner);
    const std::vector<std::string> expected{
        "resolve", "create", "init", "controlPanel", "wait", "release"};
    return Expect(
        opened && state.calls == expected &&
            state.initialized_with == owner &&
            state.waited_with == owner &&
            state.create_buffers == 0 && state.starts == 0 &&
            state.stops == 0 && state.set_sample_rate == 0,
        name);
}

} // namespace

int main() {
    int failures = 0;

    failures += CheckSuccess(ASE_OK, "ASE_OK panel lifecycle has exact order");
    failures += CheckSuccess(
        ASE_SUCCESS,
        "ASE_SUCCESS panel lifecycle has exact order");

    PanelState missing_state;
    PanelRegistry missing_registry{missing_state};
    missing_registry.values.clear();
    PanelFactory missing_factory{missing_state};
    const auto missing = Run(
        missing_state,
        missing_registry,
        missing_factory);
    failures += Expect(
        !missing &&
            missing.error().stage == gc::audio::AsioFailureStage::registry &&
            missing_state.factory_calls == 0 &&
            missing_state.calls == std::vector<std::string>{"resolve"},
        "missing registration fails before driver creation");

    PanelState factory_state;
    PanelRegistry factory_registry{factory_state};
    PanelFactory failed_factory{factory_state};
    failed_factory.failure = gc::audio::AsioFailure{
        .stage = gc::audio::AsioFailureStage::com,
        .domain = gc::audio::AsioResultDomain::hresult,
        .result = E_NOINTERFACE,
        .detail = "factory failed",
    };
    const auto factory_failure = Run(
        factory_state,
        factory_registry,
        failed_factory);
    failures += Expect(
        !factory_failure &&
            factory_failure.error().stage ==
                gc::audio::AsioFailureStage::com &&
            factory_failure.error().result == E_NOINTERFACE &&
            factory_state.calls ==
                std::vector<std::string>{"resolve", "create"},
        "factory failure is preserved without a driver lifetime");

    PanelState init_state{.init_result = ASIOFalse};
    PanelRegistry init_registry{init_state};
    PanelFactory init_factory{init_state};
    const auto init_failure = Run(init_state, init_registry, init_factory);
    failures += Expect(
        !init_failure &&
            init_failure.error().stage == gc::audio::AsioFailureStage::init &&
            init_failure.error().domain == gc::audio::AsioResultDomain::asio &&
            init_failure.error().result == ASIOFalse &&
            init_failure.error().driver_message == "driver panel failed" &&
            init_state.calls == std::vector<std::string>{
                "resolve", "create", "init", "getErrorMessage", "release"},
        "init rejection carries bounded driver text and releases the driver");

    for (const ASIOError result : {ASE_NotPresent, ASE_InvalidMode}) {
        PanelState panel_state{.panel_result = result};
        PanelRegistry panel_registry{panel_state};
        PanelFactory panel_factory{panel_state};
        const auto panel_failure = Run(
            panel_state,
            panel_registry,
            panel_factory);
        failures += Expect(
            !panel_failure &&
                panel_failure.error().stage ==
                    gc::audio::AsioFailureStage::control_panel &&
                panel_failure.error().domain ==
                    gc::audio::AsioResultDomain::asio &&
                panel_failure.error().result == result &&
                panel_failure.error().driver_message ==
                    "driver panel failed" &&
                panel_state.calls == std::vector<std::string>{
                    "resolve", "create", "init", "controlPanel",
                    "getErrorMessage", "release"} &&
                panel_state.create_buffers == 0 && panel_state.starts == 0 &&
                panel_state.stops == 0 && panel_state.set_sample_rate == 0,
            result == ASE_NotPresent
                ? "ASE_NotPresent panel failure is typed"
                : "other ASIO panel failure is typed");
    }

    PanelState invalid_state;
    PanelRegistry invalid_registry{invalid_state};
    PanelFactory invalid_factory{invalid_state};
    const auto empty_name = gc::audio::OpenAsioControlPanel(
        invalid_registry,
        invalid_factory,
        {.driver_name = {}},
        reinterpret_cast<HWND>(0x1234),
        Actions(invalid_state));
    const auto null_owner = gc::audio::OpenAsioControlPanel(
        invalid_registry,
        invalid_factory,
        {.driver_name = "XONAR SOUND CARD"},
        nullptr,
        Actions(invalid_state));
    const auto null_wait = gc::audio::OpenAsioControlPanel(
        invalid_registry,
        invalid_factory,
        {.driver_name = "XONAR SOUND CARD"},
        reinterpret_cast<HWND>(0x1234),
        {});
    failures += Expect(
        !empty_name && !null_owner && !null_wait &&
            invalid_state.calls.empty() && invalid_state.factory_calls == 0,
        "invalid panel inputs fail before registry or driver access");

    return failures == 0 ? 0 : 1;
}

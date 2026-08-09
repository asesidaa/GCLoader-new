// SPDX-License-Identifier: CC0-1.0

#include "AudioOperationWorker.h"

#include "Audio/Asio/AsioControlPanelClient.h"
#include "Audio/Asio/AsioProbeClient.h"
#include "Config/ConfigDocument.h"
#include "Config/config.h"

#include <Windows.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <expected>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace {

int Expect(bool condition, std::string_view name) {
    if (condition) {
        return 0;
    }
    std::cerr << "Expectation failed: " << name << '\n';
    return 1;
}

gc::audio::AsioCapabilityReport Report() {
    gc::audio::AsioCapabilityReport report;
    report.registration.registry_name = "XONAR SOUND CARD";
    report.sample_rate = 48'000.0;
    report.effective_buffer_frames = 192;
    report.output_channels = {
        {0, "Left", ASIOSTInt24LSB},
        {1, "Right", ASIOSTInt24LSB},
    };
    return report;
}

class FakeProbe final : public gc::audio::IAsioProbeClient {
public:
    std::expected<
        gc::audio::AsioCapabilityReport,
        gc::audio::AsioFailure>
    Run(
        const gc::audio::AsioProbeRequest& request,
        std::chrono::milliseconds timeout) noexcept override {
        ++calls;
        last_request = request;
        last_timeout = timeout;
        return result;
    }

    std::atomic<int> calls{};
    gc::audio::AsioProbeRequest last_request;
    std::chrono::milliseconds last_timeout{};
    std::expected<
        gc::audio::AsioCapabilityReport,
        gc::audio::AsioFailure> result{Report()};
};

struct PanelState {
    std::atomic<int> calls{};
    std::atomic<bool> entered{};
    std::atomic<bool> cancellation_seen{};
    bool block_until_cancel{};
    gc::audio::AsioControlPanelRequest request;
    HANDLE cancellation_event{};
    std::expected<
        gc::audio::AsioControlPanelCompletion,
        gc::audio::AsioFailure> result{
            gc::audio::AsioControlPanelCompletion::closed};
};

class FakePanel final : public gc::audio::IAsioControlPanelClient {
public:
    explicit FakePanel(PanelState& state) : state_(state) {}

    std::expected<
        gc::audio::AsioControlPanelCompletion,
        gc::audio::AsioFailure>
    Run(
        const gc::audio::AsioControlPanelRequest& request,
        HANDLE cancellation_event) noexcept override {
        ++state_.calls;
        state_.request = request;
        state_.cancellation_event = cancellation_event;
        state_.entered.store(true, std::memory_order_release);
        if (state_.block_until_cancel) {
            const DWORD wait = WaitForSingleObject(
                cancellation_event,
                2'000);
            state_.cancellation_seen.store(
                wait == WAIT_OBJECT_0,
                std::memory_order_release);
            return gc::audio::AsioControlPanelCompletion::cancelled;
        }
        return state_.result;
    }

private:
    PanelState& state_;
};

template <typename Take>
auto WaitForResult(Take&& take) {
    using Result = decltype(take());
    for (int attempt = 0; attempt < 1'000; ++attempt) {
        if (auto result = take()) {
            return Result{std::move(result)};
        }
        Sleep(1);
    }
    return Result{};
}

InputConfig Config() {
    InputConfig config;
    config.experimental().audio_backend = gc::config::AudioBackend::asio;
    config.experimental().asio_driver_name = "XONAR SOUND CARD";
    config.experimental().asio_buffer_frames = 192;
    config.experimental().asio_output_base_channel = 0;
    return config;
}

int TestMutualExclusionAndResultsTakenOnce() {
    auto probe = std::make_unique<FakeProbe>();
    auto* observed_probe = probe.get();
    PanelState panel_state;
    auto panel = std::make_unique<FakePanel>(panel_state);
    AudioOperationWorker worker{
        std::move(probe),
        std::move(panel),
        gc::config::ProductionAtomicConfigWriteActions()};

    const gc::audio::AsioProbeRequest inspection_request{
        gc::audio::AsioProbeMode::inspect,
        "XONAR SOUND CARD",
        192,
        0,
    };
    const auto started = worker.StartInspection(inspection_request);
    const auto rejected_panel = worker.StartControlPanel(
        {.driver_name = "XONAR SOUND CARD"});
    const auto rejected_save = worker.StartSave(
        L"C:\\Arcade\\config.toml",
        Config());
    auto inspection = WaitForResult([&] {
        return worker.TakeInspection();
    });

    int failures = Expect(
        started && !rejected_panel && !rejected_save && inspection &&
            inspection->has_value() && observed_probe->calls == 1 &&
            observed_probe->last_request.driver_name ==
                inspection_request.driver_name &&
            worker.operation() == AudioOperationWorker::Operation::idle &&
            !worker.TakeInspection(),
        "inspection excludes other work and its result is taken once");

    const auto panel_started = worker.StartControlPanel({
        .driver_name = "任意 Unicode ASIO 名称",
    });
    auto panel_result = WaitForResult([&] {
        return worker.TakeControlPanel();
    });
    failures += Expect(
        panel_started && panel_result && panel_result->has_value() &&
            **panel_result ==
                gc::audio::AsioControlPanelCompletion::closed &&
            panel_state.calls == 1 &&
            panel_state.request.driver_name ==
                "任意 Unicode ASIO 名称" &&
            panel_state.cancellation_event != nullptr &&
            worker.operation() == AudioOperationWorker::Operation::idle &&
            !worker.TakeControlPanel(),
        "panel request completes normally and its result is taken once");

    panel_state.result = std::unexpected(gc::audio::AsioFailure{
        .stage = gc::audio::AsioFailureStage::control_panel,
        .domain = gc::audio::AsioResultDomain::asio,
        .result = ASE_NotPresent,
        .detail = "no panel",
    });
    failures += Expect(
        worker.StartControlPanel({.driver_name = "FlexASIO"}).has_value(),
        "second panel operation starts after returning to idle");
    auto failed_panel = WaitForResult([&] {
        return worker.TakeControlPanel();
    });
    failures += Expect(
        failed_panel && !failed_panel->has_value() &&
            failed_panel->error().stage ==
                gc::audio::AsioFailureStage::control_panel,
        "typed panel failure crosses the worker unchanged");
    return failures;
}

int TestShutdownSignalsCancellationBeforeJoin() {
    auto probe = std::make_unique<FakeProbe>();
    PanelState panel_state{.block_until_cancel = true};
    auto panel = std::make_unique<FakePanel>(panel_state);
    AudioOperationWorker worker{
        std::move(probe),
        std::move(panel),
        gc::config::ProductionAtomicConfigWriteActions()};
    const auto started = worker.StartControlPanel({
        .driver_name = "XONAR SOUND CARD",
    });
    for (int attempt = 0;
         attempt < 1'000 &&
             !panel_state.entered.load(std::memory_order_acquire);
         ++attempt) {
        Sleep(1);
    }
    worker.Shutdown();
    worker.Shutdown();
    return Expect(
        started && panel_state.entered.load(std::memory_order_acquire) &&
            panel_state.cancellation_seen.load(std::memory_order_acquire) &&
            worker.operation() == AudioOperationWorker::Operation::idle,
        "shutdown signals panel cancellation before joining and is idempotent");
}

int TestMissingClientsFailWithoutStartingThreads() {
    AudioOperationWorker worker{
        nullptr,
        nullptr,
        gc::config::ProductionAtomicConfigWriteActions()};
    return Expect(
        !worker.StartInspection({
            gc::audio::AsioProbeMode::inspect,
            "FlexASIO",
            0,
            0,
        }) &&
            !worker.StartControlPanel({.driver_name = "FlexASIO"}) &&
            worker.operation() == AudioOperationWorker::Operation::idle,
        "missing clients are contained without starting worker threads");
}

} // namespace

int main() {
    int failures{};
    failures += TestMutualExclusionAndResultsTakenOnce();
    failures += TestShutdownSignalsCancellationBeforeJoin();
    failures += TestMissingClientsFailWithoutStartingThreads();
    return failures == 0 ? 0 : 1;
}

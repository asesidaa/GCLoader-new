// SPDX-License-Identifier: CC0-1.0

#include "Audio/AudioBackendController.h"

#include <Windows.h>
#include <dsound.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace {

using namespace std::chrono_literals;
using gc::audio::ActiveAudioBackend;
using gc::audio::AsioFailure;
using gc::audio::AsioFailureStage;
using gc::audio::AsioStreamRequest;
using gc::audio::AudioBackendController;
using gc::audio::AudioBackendControllerConfig;
using gc::audio::AudioBackendStartupFailure;
using gc::audio::AudioStartupFailure;
using gc::audio::IAudioBackendControllerReporter;
using gc::audio::IAudioEngineServices;
using gc::audio::IAsioOutputBackendFactory;
using gc::audio::IWasapiOutputBackendFactory;
using gc::audio::MixerVoice;
using gc::audio::NormalizedSourceFormat;
using gc::audio::VoiceUsage;

const HWND kGameWindow = reinterpret_cast<HWND>(0x1234);
const HWND kOtherWindow = reinterpret_cast<HWND>(0x5678);
constexpr REFERENCE_TIME kWasapiDuration = 70'000;

int Expect(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        return 1;
    }
    return 0;
}

struct ServiceState
{
    std::atomic_int create_voice_calls{};
    std::atomic_int current_frame_calls{};
    std::atomic_int pending_calls{};
    std::atomic_int unmapped_calls{};
    std::uint32_t buffer_frames{192};
    std::uint32_t sample_rate{48'000};
    std::optional<std::uint64_t> current_frame{384};
};

class FakeServices final : public IAudioEngineServices
{
public:
    explicit FakeServices(std::shared_ptr<ServiceState> state)
        : state_(std::move(state))
    {
    }

    std::unique_ptr<MixerVoice> CreateVoice(
        const NormalizedSourceFormat&,
        std::shared_ptr<gc::audio::AudioSnapshot>,
        std::shared_ptr<gc::audio::AudioCursorTimeline>,
        VoiceUsage,
        ma_result* result) noexcept override
    {
        state_->create_voice_calls.fetch_add(1, std::memory_order_relaxed);
        if (result != nullptr)
        {
            *result = MA_INVALID_OPERATION;
        }
        return nullptr;
    }

    std::optional<std::uint64_t> CurrentOutputFrame() noexcept override
    {
        state_->current_frame_calls.fetch_add(1, std::memory_order_relaxed);
        return state_->current_frame;
    }

    std::uint32_t endpoint_buffer_frames() const noexcept override
    {
        return state_->buffer_frames;
    }

    std::uint32_t output_sample_rate() const noexcept override
    {
        return state_->sample_rate;
    }

    void CountPendingCursorQuery() noexcept override
    {
        state_->pending_calls.fetch_add(1, std::memory_order_relaxed);
    }

    void CountUnmappedCursorFailure() noexcept override
    {
        state_->unmapped_calls.fetch_add(1, std::memory_order_relaxed);
    }

private:
    std::shared_ptr<ServiceState> state_;
};

struct WasapiFactoryState
{
    std::atomic_int calls{};
    REFERENCE_TIME duration{};
    bool succeed{true};
    AudioStartupFailure failure{};
    std::shared_ptr<ServiceState> services = std::make_shared<ServiceState>();
};

class FakeWasapiFactory final : public IWasapiOutputBackendFactory
{
public:
    explicit FakeWasapiFactory(WasapiFactoryState& state) : state_(state) {}

    std::unique_ptr<IAudioEngineServices> Start(
        REFERENCE_TIME duration,
        AudioStartupFailure* failure) noexcept override
    {
        state_.calls.fetch_add(1, std::memory_order_relaxed);
        state_.duration = duration;
        if (!state_.succeed)
        {
            if (failure != nullptr)
            {
                *failure = state_.failure;
            }
            return nullptr;
        }
        return std::make_unique<FakeServices>(state_.services);
    }

private:
    WasapiFactoryState& state_;
};

struct AsioFactoryState
{
    std::mutex mutex;
    std::condition_variable condition;
    std::atomic_int calls{};
    HWND window{};
    AsioStreamRequest request;
    bool succeed{true};
    bool block{};
    bool entered{};
    bool allow_return{};
    bool committed_fault_before_return{};
    AsioFailure failure{};
    std::shared_ptr<ServiceState> services = std::make_shared<ServiceState>();
};

class FakeAsioFactory final : public IAsioOutputBackendFactory
{
public:
    explicit FakeAsioFactory(AsioFactoryState& state) : state_(state) {}

    std::unique_ptr<IAudioEngineServices> Start(
        HWND window,
        const AsioStreamRequest& request,
        AsioFailure* failure) noexcept override
    {
        state_.calls.fetch_add(1, std::memory_order_relaxed);
        {
            std::unique_lock lock(state_.mutex);
            state_.window = window;
            state_.request = request;
            state_.entered = true;
            state_.condition.notify_all();
            if (state_.block)
            {
                state_.condition.wait(lock, [&]
                {
                    return state_.allow_return;
                });
            }
        }
        if (!state_.succeed)
        {
            if (failure != nullptr)
            {
                *failure = state_.failure;
            }
            return nullptr;
        }
        if (state_.committed_fault_before_return)
        {
            state_.services->current_frame = std::nullopt;
        }
        return std::make_unique<FakeServices>(state_.services);
    }

private:
    AsioFactoryState& state_;
};

struct ReporterState
{
    std::mutex mutex;
    int fallback_reports{};
    int fatal_reports{};
    std::optional<AsioFailure> fallback_failure;
    std::optional<AudioBackendStartupFailure> fatal_failure;
};

class FakeReporter final : public IAudioBackendControllerReporter
{
public:
    explicit FakeReporter(ReporterState& state) : state_(state) {}

    void AsioFallback(const AsioFailure& failure) noexcept override
    {
        std::lock_guard lock(state_.mutex);
        state_.fallback_failure = failure;
        ++state_.fallback_reports;
    }

    void FatalStartupFailure(
        const AudioBackendStartupFailure& failure) noexcept override
    {
        std::lock_guard lock(state_.mutex);
        state_.fatal_failure = failure;
        ++state_.fatal_reports;
    }

    void FatalControllerAllocationFailure() noexcept override
    {
    }

private:
    ReporterState& state_;
};

AudioBackendControllerConfig Config(gc::config::AudioBackend backend)
{
    return {
        .requested_backend = backend,
        .wasapi_configured_duration = kWasapiDuration,
        .asio_request = {
            .driver_name = "XONAR SOUND CARD(64)",
            .buffer_frames = 192,
            .output_base_channel = 2,
        },
    };
}

int TestWasapiStartsOnceAndDelegates()
{
    WasapiFactoryState wasapi_state;
    AsioFactoryState asio_state;
    ReporterState reporter_state;
    FakeWasapiFactory wasapi{wasapi_state};
    FakeAsioFactory asio{asio_state};
    FakeReporter reporter{reporter_state};
    AudioBackendController controller{
        Config(gc::config::AudioBackend::wasapi_exclusive),
        wasapi,
        asio,
        reporter};

    ma_result voice_result = MA_SUCCESS;
    int failures = Expect(
        controller.active_backend() == ActiveAudioBackend::none &&
            controller.CurrentOutputFrame() == std::nullopt &&
            controller.endpoint_buffer_frames() == 0 &&
            controller.output_sample_rate() == 0 &&
            controller.CreateVoice({}, {}, {}, VoiceUsage::General,
                                   &voice_result) == nullptr &&
            voice_result == MA_INVALID_OPERATION,
        "service calls fail safely before backend startup");
    controller.CountPendingCursorQuery();
    controller.CountUnmappedCursorFailure();

    failures += Expect(
        controller.StartForWindow(kGameWindow) == DS_OK &&
            controller.StartForWindow(kOtherWindow) == DS_OK &&
            wasapi_state.calls.load() == 1 &&
            wasapi_state.duration == kWasapiDuration &&
            asio_state.calls.load() == 0 &&
            controller.active_backend() ==
                ActiveAudioBackend::wasapi_exclusive,
        "WASAPI starts once with the persisted duration");
    controller.CountPendingCursorQuery();
    controller.CountUnmappedCursorFailure();
    failures += Expect(
        controller.CurrentOutputFrame() == 384 &&
            controller.endpoint_buffer_frames() == 192 &&
            controller.output_sample_rate() == 48'000 &&
            wasapi_state.services->current_frame_calls.load() == 1 &&
            wasapi_state.services->pending_calls.load() == 1 &&
            wasapi_state.services->unmapped_calls.load() == 1,
        "service calls delegate only after successful startup");
    return failures;
}

int TestAsioExactRequestCommitsWithoutWasapi()
{
    WasapiFactoryState wasapi_state;
    AsioFactoryState asio_state;
    asio_state.committed_fault_before_return = true;
    ReporterState reporter_state;
    FakeWasapiFactory wasapi{wasapi_state};
    FakeAsioFactory asio{asio_state};
    FakeReporter reporter{reporter_state};
    const auto config = Config(gc::config::AudioBackend::asio);
    AudioBackendController controller{config, wasapi, asio, reporter};

    const auto result = controller.StartForWindow(kGameWindow);
    return Expect(
        result == DS_OK && asio_state.calls.load() == 1 &&
            asio_state.window == kGameWindow &&
            asio_state.request.driver_name == config.asio_request.driver_name &&
            asio_state.request.buffer_frames == 192 &&
            asio_state.request.output_base_channel == 2 &&
            wasapi_state.calls.load() == 0 &&
            reporter_state.fallback_reports == 0 &&
            controller.requested_backend() == gc::config::AudioBackend::asio &&
            controller.active_backend() == ActiveAudioBackend::asio &&
            controller.CurrentOutputFrame() == std::nullopt,
        "committed ASIO remains active even if it faults before Start returns");
}

int TestAsioPrecommitFailureFallsBackWithoutChangingRequest()
{
    WasapiFactoryState wasapi_state;
    AsioFactoryState asio_state;
    asio_state.succeed = false;
    asio_state.failure = {
        .stage = AsioFailureStage::startup_clock,
        .result = WAIT_TIMEOUT,
        .detail = "third callback timeout",
    };
    ReporterState reporter_state;
    FakeWasapiFactory wasapi{wasapi_state};
    FakeAsioFactory asio{asio_state};
    FakeReporter reporter{reporter_state};
    AudioBackendController controller{
        Config(gc::config::AudioBackend::asio), wasapi, asio, reporter};

    int failures = Expect(
        controller.StartForWindow(kGameWindow) == DS_OK &&
            asio_state.calls.load() == 1 && wasapi_state.calls.load() == 1 &&
            reporter_state.fallback_reports == 1 &&
            reporter_state.fallback_failure.has_value() &&
            reporter_state.fallback_failure->stage ==
                AsioFailureStage::startup_clock &&
            reporter_state.fatal_reports == 0,
        "precommit ASIO failure is reported and falls back once");
    failures += Expect(
        controller.requested_backend() == gc::config::AudioBackend::asio &&
            controller.active_backend() ==
                ActiveAudioBackend::wasapi_exclusive,
        "runtime fallback never rewrites the requested backend");
    return failures;
}

int TestDualFailureIsTerminalAndNested()
{
    WasapiFactoryState wasapi_state;
    wasapi_state.succeed = false;
    wasapi_state.failure.failure = {
        gc::audio::AudioFailureStage::InitializeExclusive,
        E_FAIL,
    };
    AsioFactoryState asio_state;
    asio_state.succeed = false;
    asio_state.failure = {
        .stage = AsioFailureStage::init,
        .detail = "driver rejected HWND",
    };
    ReporterState reporter_state;
    FakeWasapiFactory wasapi{wasapi_state};
    FakeAsioFactory asio{asio_state};
    FakeReporter reporter{reporter_state};
    AudioBackendController controller{
        Config(gc::config::AudioBackend::asio), wasapi, asio, reporter};

    int failures = Expect(
        controller.StartForWindow(kGameWindow) == DSERR_NODRIVER &&
            controller.StartForWindow(kOtherWindow) == DSERR_NODRIVER &&
            asio_state.calls.load() == 1 && wasapi_state.calls.load() == 1 &&
            reporter_state.fallback_reports == 1 &&
            reporter_state.fatal_reports == 1 &&
            reporter_state.fatal_failure.has_value() &&
            reporter_state.fatal_failure->asio_failure.has_value() &&
            reporter_state.fatal_failure->asio_failure->stage ==
                AsioFailureStage::init &&
            reporter_state.fatal_failure->wasapi_failure.failure.stage ==
                gc::audio::AudioFailureStage::InitializeExclusive,
        "dual backend failure is nested, fatal, and never retried");
    failures += Expect(
        controller.active_backend() == ActiveAudioBackend::failed &&
            controller.CurrentOutputFrame() == std::nullopt,
        "terminal failure leaves service calls inert");
    return failures;
}

int TestConcurrentCallersShareOneInitialization()
{
    WasapiFactoryState wasapi_state;
    AsioFactoryState asio_state;
    asio_state.block = true;
    ReporterState reporter_state;
    FakeWasapiFactory wasapi{wasapi_state};
    FakeAsioFactory asio{asio_state};
    FakeReporter reporter{reporter_state};
    AudioBackendController controller{
        Config(gc::config::AudioBackend::asio), wasapi, asio, reporter};

    auto first = std::async(std::launch::async, [&]
    {
        return controller.StartForWindow(kGameWindow);
    });
    {
        std::unique_lock lock(asio_state.mutex);
        asio_state.condition.wait_for(lock, 2s, [&]
        {
            return asio_state.entered;
        });
    }
    auto second = std::async(std::launch::async, [&]
    {
        return controller.StartForWindow(kOtherWindow);
    });
    int failures = Expect(
        second.wait_for(20ms) == std::future_status::timeout &&
            asio_state.calls.load() == 1,
        "concurrent caller waits without starting a second backend");
    {
        std::lock_guard lock(asio_state.mutex);
        asio_state.allow_return = true;
    }
    asio_state.condition.notify_all();
    failures += Expect(
        first.get() == DS_OK && second.get() == DS_OK &&
            asio_state.calls.load() == 1 && wasapi_state.calls.load() == 0 &&
            controller.active_backend() == ActiveAudioBackend::asio,
        "concurrent callers observe the same stable result");
    return failures;
}

} // namespace

int main()
{
    int failures{};
    failures += TestWasapiStartsOnceAndDelegates();
    failures += TestAsioExactRequestCommitsWithoutWasapi();
    failures += TestAsioPrecommitFailureFallsBackWithoutChangingRequest();
    failures += TestDualFailureIsTerminalAndNested();
    failures += TestConcurrentCallersShareOneInitialization();
    return failures == 0 ? 0 : 1;
}

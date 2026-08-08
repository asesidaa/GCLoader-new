// SPDX-License-Identifier: CC0-1.0

#include "Audio/Asio/AsioOutputBackend.h"
#include "Audio/Asio/AsioOutputBackendInternal.h"

#include "Audio/Asio/AsioCallbackRuntime.h"
#include "Audio/Asio/AsioClock.h"
#include "Audio/Asio/AsioSampleConverter.h"
#include "Audio/Asio/AsioSession.h"
#include "Audio/Mixer/AudioRenderCore.h"

#include <Windows.h>
#include <mmsystem.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <utility>

namespace gc::audio {
namespace detail {
namespace {

HRESULT ProductionInitializeCom(void*, DWORD coinit_flags) noexcept
{
    return CoInitializeEx(nullptr, coinit_flags);
}

void ProductionUninitializeCom(void*) noexcept
{
    CoUninitialize();
}

HANDLE ProductionCreateManualEvent(void*) noexcept
{
    return CreateEventW(nullptr, TRUE, FALSE, nullptr);
}

bool ProductionSignalEvent(void*, HANDLE event) noexcept
{
    return event != nullptr && SetEvent(event) != FALSE;
}

DWORD ProductionWaitForEvent(
    void*,
    HANDLE event,
    DWORD timeout_ms) noexcept
{
    return WaitForSingleObject(event, timeout_ms);
}

void ProductionCloseHandle(void*, HANDLE handle) noexcept
{
    if (handle != nullptr)
    {
        CloseHandle(handle);
    }
}

DWORD ProductionMessageWait(
    void*,
    std::span<const HANDLE> handles,
    DWORD timeout_ms) noexcept
{
    return MsgWaitForMultipleObjectsEx(
        static_cast<DWORD>(handles.size()),
        handles.data(),
        timeout_ms,
        QS_ALLINPUT,
        MWMO_INPUTAVAILABLE);
}

void ProductionDrainMessages(void*) noexcept
{
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
    {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

std::uint64_t ProductionTickCountMs(void*) noexcept
{
    return GetTickCount64();
}

std::uint32_t ProductionTimeGetTimeMs(void*) noexcept
{
    return timeGetTime();
}

AsioFailure Failure(
    AsioFailureStage stage,
    std::string detail,
    AsioResultDomain domain = AsioResultDomain::none,
    std::int64_t result = 0)
{
    return {
        .stage = stage,
        .domain = domain,
        .result = result,
        .detail = std::move(detail),
    };
}

bool ActionsComplete(const AsioOutputBackendActions& actions) noexcept
{
    return actions.initialize_com != nullptr &&
        actions.uninitialize_com != nullptr &&
        actions.create_manual_event != nullptr &&
        actions.signal_event != nullptr &&
        actions.wait_for_event != nullptr &&
        actions.close_handle != nullptr &&
        actions.message_wait != nullptr &&
        actions.drain_messages != nullptr &&
        actions.tick_count_ms != nullptr &&
        actions.time_get_time_ms != nullptr;
}

DWORD BoundedDeadline(DWORD requested) noexcept
{
    return (std::min)(requested, kAsioStartupClockDeadlineMs);
}

DWORD RemainingTimeout(
    std::uint64_t start_ms,
    std::uint64_t now_ms,
    DWORD timeout_ms) noexcept
{
    const auto elapsed = now_ms >= start_ms ? now_ms - start_ms : 0;
    return elapsed >= timeout_ms
        ? 0
        : static_cast<DWORD>(timeout_ms - elapsed);
}

const char* RuntimeFailureDetail(AsioFailureStage stage) noexcept
{
    switch (stage)
    {
    case AsioFailureStage::runtime_clock:
        return "ASIO presentation clock became invalid; restart required";
    case AsioFailureStage::conversion:
        return "ASIO sample conversion failed; restart required";
    case AsioFailureStage::output_ready:
        return "ASIO outputReady failed; restart required";
    case AsioFailureStage::callback:
        return "ASIO callback contract failed; restart required";
    case AsioFailureStage::stop:
        return "ASIO stop failed during teardown";
    case AsioFailureStage::dispose:
        return "ASIO buffer disposal failed during teardown";
    case AsioFailureStage::restore_sample_rate:
        return "ASIO sample-rate restoration failed during teardown";
    default:
        return "ASIO runtime failed; restart required";
    }
}

} // namespace

AsioOutputBackendActions ProductionAsioOutputBackendActions() noexcept
{
    return {
        .initialize_com = &ProductionInitializeCom,
        .uninitialize_com = &ProductionUninitializeCom,
        .create_manual_event = &ProductionCreateManualEvent,
        .signal_event = &ProductionSignalEvent,
        .wait_for_event = &ProductionWaitForEvent,
        .close_handle = &ProductionCloseHandle,
        .message_wait = &ProductionMessageWait,
        .drain_messages = &ProductionDrainMessages,
        .tick_count_ms = &ProductionTickCountMs,
        .time_get_time_ms = &ProductionTimeGetTimeMs,
        .callback_runtime_actions =
            ProductionAsioCallbackRuntimeActions(),
        .summary_interval_ms = kAsioRuntimeSummaryIntervalMs,
    };
}

class AsioOutputBackendState final : public IAsioBlockRenderer
{
public:
    AsioOutputBackendState(
        HWND game_window,
        AsioStreamRequest request,
        std::unique_ptr<IAsioRegistrySource> registry,
        std::unique_ptr<IAsioDriverFactory> factory,
        std::shared_ptr<IAsioOutputObserver> observer,
        std::shared_ptr<const ma_allocation_callbacks> allocations,
        DWORD startup_clock_timeout_ms,
        AsioOutputBackendActions actions)
        : game_window_(game_window),
          request_(std::move(request)),
          pending_registry_(std::move(registry)),
          pending_factory_(std::move(factory)),
          observer_(std::move(observer)),
          mixer_allocations_(std::move(allocations)),
          startup_clock_timeout_ms_(
              BoundedDeadline(startup_clock_timeout_ms)),
          actions_(actions)
    {
    }

    ~AsioOutputBackendState() override
    {
        ShutdownAndJoin();
        CloseEvents();
    }

    std::expected<void, AsioFailure> StartControlThread() noexcept
    {
        if (const auto events = CreateEvents(); !events)
        {
            return events;
        }
        try
        {
            control_thread_ = std::thread([this]
            {
                ControlThreadMain();
            });
            return {};
        }
        catch (const std::exception& error)
        {
            return std::unexpected(Failure(
                AsioFailureStage::callback_prepare,
                "Could not create ASIO control thread: " +
                    std::string{error.what()}));
        }
        catch (...)
        {
            return std::unexpected(Failure(
                AsioFailureStage::callback_prepare,
                "Could not create ASIO control thread"));
        }
    }

    std::expected<void, AsioFailure> WaitForStartup() noexcept
    {
        const DWORD wait = actions_.wait_for_event(
            actions_.context,
            startup_event_,
            INFINITE);
        if (wait != WAIT_OBJECT_0)
        {
            return std::unexpected(Failure(
                AsioFailureStage::startup_clock,
                "Waiting for the ASIO control thread failed",
                AsioResultDomain::win32,
                wait == WAIT_FAILED ? GetLastError() : wait));
        }
        if (!startup_succeeded_.load(std::memory_order_acquire))
        {
            return std::unexpected(startup_failure_);
        }
        return {};
    }

    void ShutdownAndJoin() noexcept
    {
        if (shutdown_event_ != nullptr)
        {
            actions_.signal_event(actions_.context, shutdown_event_);
        }
        if (control_thread_.joinable())
        {
            control_thread_.join();
        }
    }

    std::unique_ptr<MixerVoice> CreateVoice(
        const NormalizedSourceFormat& format,
        std::shared_ptr<AudioSnapshot> snapshot,
        std::shared_ptr<AudioCursorTimeline> timeline,
        VoiceUsage usage,
        ma_result* result) noexcept
    {
        if (render_core_ == nullptr)
        {
            if (result != nullptr)
            {
                *result = MA_INVALID_OPERATION;
            }
            return nullptr;
        }
        return render_core_->CreateVoice(
            format,
            std::move(snapshot),
            std::move(timeline),
            usage,
            result);
    }

    std::optional<std::uint64_t> CurrentOutputFrame() noexcept
    {
        return render_core_ != nullptr
            ? render_core_->CurrentOutputFrame()
            : std::nullopt;
    }

    std::uint32_t endpoint_buffer_frames() const noexcept
    {
        return endpoint_buffer_frames_.load(std::memory_order_acquire);
    }

    std::uint32_t output_sample_rate() const noexcept
    {
        return output_sample_rate_.load(std::memory_order_acquire);
    }

    void CountPendingCursorQuery() noexcept
    {
        pending_cursor_queries_.fetch_add(1, std::memory_order_relaxed);
    }

    void CountUnmappedCursorFailure() noexcept
    {
        unmapped_cursor_failures_.fetch_add(1, std::memory_order_relaxed);
    }

private:
    std::expected<void, AsioFailure> CreateEvents() noexcept
    {
        startup_event_ = actions_.create_manual_event(actions_.context);
        stable_render_event_ =
            actions_.create_manual_event(actions_.context);
        fault_event_ = actions_.create_manual_event(actions_.context);
        shutdown_event_ = actions_.create_manual_event(actions_.context);
        if (startup_event_ != nullptr && stable_render_event_ != nullptr &&
            fault_event_ != nullptr && shutdown_event_ != nullptr)
        {
            return {};
        }
        return std::unexpected(Failure(
            AsioFailureStage::callback_prepare,
            "Could not create ASIO control events",
            AsioResultDomain::win32,
            GetLastError()));
    }

    void CloseEvents() noexcept
    {
        const HANDLE events[]{
            startup_event_,
            stable_render_event_,
            fault_event_,
            shutdown_event_,
        };
        for (const HANDLE event : events)
        {
            if (event != nullptr)
            {
                actions_.close_handle(actions_.context, event);
            }
        }
        startup_event_ = nullptr;
        stable_render_event_ = nullptr;
        fault_event_ = nullptr;
        shutdown_event_ = nullptr;
    }

    void ControlThreadMain() noexcept
    {
        const HRESULT com_result = actions_.initialize_com(
            actions_.context,
            COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        if (FAILED(com_result))
        {
            CompleteStartupFailure(Failure(
                AsioFailureStage::com,
                "CoInitializeEx(STA) failed on the ASIO control thread",
                AsioResultDomain::hresult,
                com_result));
            return;
        }

        auto initialized = InitializeBackend();
        if (!initialized)
        {
            TeardownOnControlThread();
            actions_.uninitialize_com(actions_.context);
            CompleteStartupFailure(std::move(initialized.error()));
            return;
        }

        auto stable = WaitForStableRender();
        if (!stable)
        {
            TeardownOnControlThread();
            actions_.uninitialize_com(actions_.context);
            CompleteStartupFailure(std::move(stable.error()));
            return;
        }

        committed_.store(true, std::memory_order_release);
        observer_->StartupSucceeded(session_->report());
        startup_succeeded_.store(true, std::memory_order_release);
        actions_.signal_event(actions_.context, startup_event_);

        auto runtime_failure = MonitorCommittedRuntime();
        TeardownOnControlThread();
        if (!runtime_failure &&
            first_fault_claimed_.load(std::memory_order_acquire))
        {
            runtime_failure = BuildLatchedFailure();
        }
        const auto counters = SnapshotCounters();
        if (runtime_failure)
        {
            observer_->RuntimeFailed(*runtime_failure, counters);
        }
        else
        {
            observer_->RuntimeSummary(counters);
        }
        actions_.uninitialize_com(actions_.context);
    }

    std::expected<void, AsioFailure> InitializeBackend() noexcept
    {
        try
        {
            if (game_window_ == nullptr || pending_registry_ == nullptr ||
                pending_factory_ == nullptr || observer_ == nullptr)
            {
                return std::unexpected(Failure(
                    AsioFailureStage::init,
                    "ASIO runtime dependencies and game HWND are required"));
            }

            auto registration = ResolveAsioDriver(
                *pending_registry_, request_.driver_name);
            pending_registry_.reset();
            if (!registration)
            {
                return std::unexpected(std::move(registration.error()));
            }
            auto driver = pending_factory_->Create(registration->clsid);
            pending_factory_.reset();
            if (!driver)
            {
                return std::unexpected(std::move(driver.error()));
            }

            auto prepared = AsioSession::Prepare(
                std::move(*registration),
                std::move(*driver),
                request_,
                game_window_,
                AsioProbeMode::validate,
                true);
            if (!prepared)
            {
                return std::unexpected(std::move(prepared.error()));
            }
            session_ = std::move(*prepared);

            AsioLegacyPositionActions legacy{
                this,
                &AsioOutputBackendState::ReadLegacyPosition,
            };
            auto callbacks = AsioCallbackRuntime::Prepare(
                *this,
                legacy,
                actions_.callback_runtime_actions);
            if (!callbacks)
            {
                return std::unexpected(std::move(callbacks.error()));
            }
            callback_runtime_ = std::move(*callbacks);
            if (auto installed = callback_runtime_->Install(); !installed)
            {
                return installed;
            }
            if (auto buffers = session_->CreateOutputBuffers(
                    AsioCallbackRuntime::Callbacks());
                !buffers)
            {
                return buffers;
            }
            if (auto views = ConfigureDriverBuffers(); !views)
            {
                return views;
            }

            auto clock = std::make_unique<AsioPresentedClockPublication>(
                AsioClockNowActions{
                    actions_.context,
                    actions_.time_get_time_ms,
                });
            presented_clock_ = clock.get();
            ma_result mixer_result = MA_ERROR;
            render_core_ = AudioRenderCore::Create(
                request_.buffer_frames,
                48'000,
                std::move(mixer_allocations_),
                std::move(clock),
                &mixer_result);
            if (render_core_ == nullptr)
            {
                presented_clock_ = nullptr;
                return std::unexpected(Failure(
                    AsioFailureStage::render_core,
                    "Could not create the preallocated ASIO render core",
                    AsioResultDomain::none,
                    mixer_result));
            }

            clock_tracker_.Reset(
                request_.buffer_frames,
                session_->report().output_latency_frames);
            endpoint_buffer_frames_.store(
                request_.buffer_frames,
                std::memory_order_release);
            output_sample_rate_.store(48'000, std::memory_order_release);
            render_ready_.store(true, std::memory_order_release);
            if (auto started = session_->Start(); !started)
            {
                return started;
            }
            return {};
        }
        catch (const std::bad_alloc&)
        {
            return std::unexpected(Failure(
                AsioFailureStage::render_core,
                "ASIO runtime allocation failed"));
        }
        catch (const std::exception& error)
        {
            return std::unexpected(Failure(
                AsioFailureStage::protocol,
                "ASIO runtime initialization failed: " +
                    std::string{error.what()}));
        }
        catch (...)
        {
            return std::unexpected(Failure(
                AsioFailureStage::protocol,
                "ASIO runtime initialization failed unexpectedly"));
        }
    }

    std::expected<void, AsioFailure> ConfigureDriverBuffers() noexcept
    {
        const auto bytes_per_sample = AsioBytesPerSample(
            session_->report().output_channels[
                request_.output_base_channel].sample_type);
        const auto right_bytes_per_sample = AsioBytesPerSample(
            session_->report().output_channels[
                request_.output_base_channel + 1U].sample_type);
        if (!bytes_per_sample || !right_bytes_per_sample ||
            request_.buffer_frames >
                (std::numeric_limits<std::size_t>::max)() /
                    (std::max)(*bytes_per_sample, *right_bytes_per_sample))
        {
            return std::unexpected(Failure(
                AsioFailureStage::conversion,
                "Selected ASIO buffer byte size is invalid"));
        }

        const auto infos = session_->buffers();
        for (std::size_t channel = 0; channel < 2; ++channel)
        {
            channel_types_[channel] = session_->report().output_channels[
                request_.output_base_channel + channel].sample_type;
            const auto sample_bytes =
                channel == 0 ? *bytes_per_sample : *right_bytes_per_sample;
            const auto byte_count =
                static_cast<std::size_t>(request_.buffer_frames) *
                sample_bytes;
            for (std::size_t index = 0; index < 2; ++index)
            {
                if (infos[channel].buffers[index] == nullptr)
                {
                    return std::unexpected(Failure(
                        AsioFailureStage::create_buffers,
                        "ASIO driver returned a null output buffer"));
                }
                driver_buffers_[channel][index] = std::span<std::byte>{
                    static_cast<std::byte*>(infos[channel].buffers[index]),
                    byte_count,
                };
            }
        }
        return {};
    }

    std::expected<void, AsioFailure> WaitForStableRender() noexcept
    {
        const std::uint64_t started_ms =
            actions_.tick_count_ms(actions_.context);
        const std::array<HANDLE, 3> handles{
            stable_render_event_,
            fault_event_,
            shutdown_event_,
        };
        for (;;)
        {
            const DWORD remaining = RemainingTimeout(
                started_ms,
                actions_.tick_count_ms(actions_.context),
                startup_clock_timeout_ms_);
            const DWORD wait = actions_.message_wait(
                actions_.context,
                handles,
                remaining);
            if (wait == WAIT_OBJECT_0)
            {
                if (first_fault_claimed_.load(std::memory_order_acquire))
                {
                    return std::unexpected(BuildLatchedFailure());
                }
                return {};
            }
            if (wait == WAIT_OBJECT_0 + 1)
            {
                return std::unexpected(BuildLatchedFailure());
            }
            if (wait == WAIT_OBJECT_0 + 2)
            {
                return std::unexpected(Failure(
                    AsioFailureStage::startup_clock,
                    "ASIO startup was cancelled before clock stability"));
            }
            if (wait == WAIT_OBJECT_0 + handles.size())
            {
                actions_.drain_messages(actions_.context);
                continue;
            }
            if (wait == WAIT_TIMEOUT)
            {
                return std::unexpected(Failure(
                    AsioFailureStage::startup_clock,
                    "ASIO did not produce a stable third callback before "
                    "the startup deadline",
                    AsioResultDomain::win32,
                    WAIT_TIMEOUT));
            }
            return std::unexpected(Failure(
                AsioFailureStage::startup_clock,
                "ASIO startup message wait failed",
                AsioResultDomain::win32,
                wait == WAIT_FAILED ? GetLastError() : wait));
        }
    }

    std::optional<AsioFailure> MonitorCommittedRuntime() noexcept
    {
        const std::array<HANDLE, 2> handles{fault_event_, shutdown_event_};
        std::uint64_t summary_started =
            actions_.tick_count_ms(actions_.context);
        for (;;)
        {
            const DWORD remaining = RemainingTimeout(
                summary_started,
                actions_.tick_count_ms(actions_.context),
                actions_.summary_interval_ms);
            const DWORD wait = actions_.message_wait(
                actions_.context,
                handles,
                remaining);
            if (wait == WAIT_OBJECT_0)
            {
                return BuildLatchedFailure();
            }
            if (wait == WAIT_OBJECT_0 + 1)
            {
                return std::nullopt;
            }
            if (wait == WAIT_OBJECT_0 + handles.size())
            {
                actions_.drain_messages(actions_.context);
                continue;
            }
            if (wait == WAIT_TIMEOUT)
            {
                observer_->RuntimeSummary(SnapshotCounters());
                summary_started = actions_.tick_count_ms(actions_.context);
                continue;
            }
            LatchRuntimeFault(
                AsioFailureStage::callback,
                AsioResultDomain::win32,
                wait == WAIT_FAILED ? GetLastError() : wait);
            return BuildLatchedFailure();
        }
    }

    void TeardownOnControlThread() noexcept
    {
        stopping_.store(true, std::memory_order_release);
        render_ready_.store(false, std::memory_order_release);
        if (render_core_ != nullptr)
        {
            render_core_->InvalidatePresentationClock();
        }
        if (callback_runtime_ != nullptr)
        {
            callback_runtime_->BeginStopping();
        }
        if (session_ != nullptr)
        {
            if (auto stopped = session_->Stop(); !stopped)
            {
                LatchRuntimeFault(
                    stopped.error().stage,
                    stopped.error().domain,
                    stopped.error().result);
            }
        }
        if (callback_runtime_ != nullptr)
        {
            callback_runtime_->JoinWorker();
            callback_runtime_->Uninstall();
            final_callback_snapshot_ = callback_runtime_->Snapshot();
            has_final_callback_snapshot_ = true;
        }
        if (session_ != nullptr)
        {
            if (auto closed = session_->Close(); !closed)
            {
                LatchRuntimeFault(
                    closed.error().stage,
                    closed.error().domain,
                    closed.error().result);
            }
            session_.reset();
        }
        callback_runtime_.reset();
        presented_clock_ = nullptr;
        for (auto& channel : driver_buffers_)
        {
            channel = {};
        }
    }

    void CompleteStartupFailure(AsioFailure failure) noexcept
    {
        startup_failure_ = std::move(failure);
        startup_succeeded_.store(false, std::memory_order_release);
        actions_.signal_event(actions_.context, startup_event_);
    }

    static ASIOError ReadLegacyPosition(
        void* context,
        ASIOSamples* samples,
        ASIOTimeStamp* timestamp) noexcept
    {
        auto& self = *static_cast<AsioOutputBackendState*>(context);
        return self.session_ != nullptr
            ? self.session_->driver().GetSamplePosition(samples, timestamp)
            : ASE_NotPresent;
    }

    void RenderAsioBlock(const AsioRenderRequest& request) noexcept override
    {
        if (!render_ready_.load(std::memory_order_acquire) ||
            stopping_.load(std::memory_order_acquire) ||
            first_fault_claimed_.load(std::memory_order_acquire))
        {
            ClearAsioBlock(request.buffer_index);
            return;
        }
        if (request.buffer_index < 0 || request.buffer_index > 1)
        {
            LatchRuntimeFault(AsioFailureStage::callback);
            return;
        }

        RecordSamplePosition(request.sample_position);
        const auto decision = clock_tracker_.Observe(
            request.sample_position,
            request.system_time_ns);
        if (decision.kind == AsioClockDecisionKind::invalid)
        {
            ClearAsioBlock(request.buffer_index);
            LatchRuntimeFault(AsioFailureStage::runtime_clock);
            return;
        }
        if (decision.kind == AsioClockDecisionKind::priming)
        {
            ClearAsioBlock(request.buffer_index);
            CallOutputReady();
            return;
        }

        const auto block = render_core_->Render(MixerRenderTimeline{
            decision.render_output_frame_begin,
            0,
        });
        if (block.silence_substituted)
        {
            silence_substitutions_.fetch_add(1, std::memory_order_relaxed);
        }
        const auto index = static_cast<std::size_t>(request.buffer_index);
        const bool left_converted = ConvertFloatStereoChannelToAsio(
            block.interleaved_stereo,
            0,
            channel_types_[0],
            driver_buffers_[0][index]);
        const bool right_converted = left_converted &&
            ConvertFloatStereoChannelToAsio(
                block.interleaved_stereo,
                1,
                channel_types_[1],
                driver_buffers_[1][index]);
        if (!left_converted || !right_converted)
        {
            ClearAsioBlock(request.buffer_index);
            LatchRuntimeFault(AsioFailureStage::conversion);
            return;
        }
        if (decision.render_output_frame_begin >
            (std::numeric_limits<std::uint64_t>::max)() -
                request_.buffer_frames)
        {
            ClearAsioBlock(request.buffer_index);
            LatchRuntimeFault(AsioFailureStage::runtime_clock);
            return;
        }
        presented_clock_->Publish(
            decision,
            decision.render_output_frame_begin + request_.buffer_frames);
        if (!CallOutputReady())
        {
            return;
        }
        actions_.signal_event(actions_.context, stable_render_event_);
    }

    void ClearAsioBlock(long buffer_index) noexcept override
    {
        if (buffer_index < 0 || buffer_index > 1)
        {
            return;
        }
        const auto index = static_cast<std::size_t>(buffer_index);
        const bool left = ClearAsioChannel(
            channel_types_[0],
            driver_buffers_[0][index],
            request_.buffer_frames);
        const bool right = ClearAsioChannel(
            channel_types_[1],
            driver_buffers_[1][index],
            request_.buffer_frames);
        if (render_ready_.load(std::memory_order_acquire) &&
            (!left || !right))
        {
            LatchRuntimeFault(AsioFailureStage::conversion);
        }
    }

    void OnAsioRuntimeFault(AsioFailureStage stage) noexcept override
    {
        LatchRuntimeFault(stage);
    }

    bool CallOutputReady() noexcept
    {
        if (session_ == nullptr ||
            !session_->report().output_ready_supported)
        {
            return true;
        }
        const ASIOError result = session_->driver().OutputReady();
        if (result == ASE_OK)
        {
            return true;
        }
        LatchRuntimeFault(
            AsioFailureStage::output_ready,
            AsioResultDomain::asio,
            result);
        return false;
    }

    void RecordSamplePosition(std::uint64_t sample_position) noexcept
    {
        if (has_previous_sample_position_)
        {
            const auto previous = previous_sample_position_;
            if (previous >
                    (std::numeric_limits<std::uint64_t>::max)() -
                        request_.buffer_frames ||
                sample_position != previous + request_.buffer_frames)
            {
                sample_position_discontinuities_.fetch_add(
                    1, std::memory_order_relaxed);
                if (previous <=
                        (std::numeric_limits<std::uint64_t>::max)() -
                            request_.buffer_frames &&
                    sample_position > previous + request_.buffer_frames)
                {
                    render_gap_frames_.fetch_add(
                        sample_position -
                            (previous + request_.buffer_frames),
                        std::memory_order_relaxed);
                }
            }
        }
        previous_sample_position_ = sample_position;
        has_previous_sample_position_ = true;
    }

    void LatchRuntimeFault(
        AsioFailureStage stage,
        AsioResultDomain domain = AsioResultDomain::none,
        std::int64_t result = 0) noexcept
    {
        if (stage == AsioFailureStage::none)
        {
            return;
        }
        bool expected{};
        if (!first_fault_claimed_.compare_exchange_strong(
                expected,
                true,
                std::memory_order_acq_rel,
                std::memory_order_acquire))
        {
            return;
        }
        first_fault_domain_.store(
            static_cast<std::uint8_t>(domain),
            std::memory_order_relaxed);
        first_fault_result_.store(result, std::memory_order_relaxed);
        first_fault_stage_.store(
            static_cast<std::uint8_t>(stage),
            std::memory_order_release);
        actions_.signal_event(actions_.context, fault_event_);
    }

    AsioFailure BuildLatchedFailure() const
    {
        auto stage = AsioFailureStage::none;
        while (first_fault_claimed_.load(std::memory_order_acquire) &&
               stage == AsioFailureStage::none)
        {
            stage = static_cast<AsioFailureStage>(
                first_fault_stage_.load(std::memory_order_acquire));
            if (stage == AsioFailureStage::none)
            {
                std::this_thread::yield();
            }
        }
        return Failure(
            stage == AsioFailureStage::none
                ? AsioFailureStage::callback
                : stage,
            RuntimeFailureDetail(stage),
            static_cast<AsioResultDomain>(
                first_fault_domain_.load(std::memory_order_relaxed)),
            first_fault_result_.load(std::memory_order_relaxed));
    }

    AsioRuntimeCountersSnapshot SnapshotCounters() const noexcept
    {
        const AsioCallbackRuntimeSnapshot callback = callback_runtime_ != nullptr
            ? callback_runtime_->Snapshot()
            : has_final_callback_snapshot_
                ? final_callback_snapshot_
                : AsioCallbackRuntimeSnapshot{};
        return {
            .callbacks = callback.callbacks,
            .time_info_callbacks = callback.time_info_callbacks,
            .legacy_callbacks = callback.legacy_callbacks,
            .deferred_callbacks = callback.deferred_callbacks,
            .deadline_misses = callback.deadline_misses,
            .silence_substitutions =
                silence_substitutions_.load(std::memory_order_relaxed),
            .overload_messages = callback.overload_messages,
            .reset_requests = callback.reset_requests,
            .resync_requests = callback.resync_requests,
            .latency_change_requests = callback.latency_change_requests,
            .buffer_size_change_requests =
                callback.buffer_size_change_requests,
            .sample_rate_change_requests =
                callback.sample_rate_change_requests,
            .sample_position_discontinuities =
                sample_position_discontinuities_.load(
                    std::memory_order_relaxed),
            .render_gap_frames =
                render_gap_frames_.load(std::memory_order_relaxed),
            .maximum_callback_ticks = callback.maximum_callback_ticks,
            .maximum_render_ticks = callback.maximum_render_ticks,
            .pending_cursor_queries =
                pending_cursor_queries_.load(std::memory_order_relaxed),
            .unmapped_cursor_failures =
                unmapped_cursor_failures_.load(std::memory_order_relaxed),
            .mixer = render_core_ != nullptr
                ? render_core_->diagnostics()
                : MixerDiagnosticsSnapshot{},
        };
    }

    HWND game_window_{};
    AsioStreamRequest request_;
    std::unique_ptr<IAsioRegistrySource> pending_registry_;
    std::unique_ptr<IAsioDriverFactory> pending_factory_;
    std::shared_ptr<IAsioOutputObserver> observer_;
    std::shared_ptr<const ma_allocation_callbacks> mixer_allocations_;
    DWORD startup_clock_timeout_ms_{};
    AsioOutputBackendActions actions_{};

    std::thread control_thread_;
    HANDLE startup_event_{};
    HANDLE stable_render_event_{};
    HANDLE fault_event_{};
    HANDLE shutdown_event_{};
    AsioFailure startup_failure_{};
    std::atomic_bool startup_succeeded_{};
    std::atomic_bool committed_{};
    std::atomic_bool stopping_{};
    std::atomic_bool render_ready_{};

    std::unique_ptr<AsioSession> session_;
    std::unique_ptr<AsioCallbackRuntime> callback_runtime_;
    std::unique_ptr<AudioRenderCore> render_core_;
    AsioPresentedClockPublication* presented_clock_{};
    AsioClockTracker clock_tracker_;
    std::array<ASIOSampleType, 2> channel_types_{};
    std::array<std::array<std::span<std::byte>, 2>, 2> driver_buffers_{};
    AsioCallbackRuntimeSnapshot final_callback_snapshot_{};
    bool has_final_callback_snapshot_{};

    std::atomic_uint32_t endpoint_buffer_frames_{};
    std::atomic_uint32_t output_sample_rate_{};
    std::atomic_uint64_t silence_substitutions_{};
    std::atomic_uint64_t sample_position_discontinuities_{};
    std::atomic_uint64_t render_gap_frames_{};
    std::atomic_uint64_t pending_cursor_queries_{};
    std::atomic_uint64_t unmapped_cursor_failures_{};
    std::atomic_bool first_fault_claimed_{};
    std::atomic<std::uint8_t> first_fault_stage_{};
    std::atomic<std::uint8_t> first_fault_domain_{};
    std::atomic<std::int64_t> first_fault_result_{};
    std::uint64_t previous_sample_position_{};
    bool has_previous_sample_position_{};
};

std::unique_ptr<AsioOutputBackend> StartAsioOutputBackendAndWait(
    HWND game_window,
    const AsioStreamRequest& request,
    std::unique_ptr<IAsioRegistrySource> registry,
    std::unique_ptr<IAsioDriverFactory> factory,
    std::shared_ptr<IAsioOutputObserver> observer,
    std::shared_ptr<const ma_allocation_callbacks> allocations,
    DWORD startup_clock_timeout_ms,
    const AsioOutputBackendActions& actions,
    AsioFailure* failure) noexcept
{
    if (failure != nullptr)
    {
        *failure = {};
    }
    if (!ActionsComplete(actions))
    {
        if (failure != nullptr)
        {
            *failure = Failure(
                AsioFailureStage::callback_prepare,
                "ASIO backend actions are incomplete");
        }
        return nullptr;
    }
    try
    {
        auto state = std::make_unique<AsioOutputBackendState>(
            game_window,
            request,
            std::move(registry),
            std::move(factory),
            std::move(observer),
            std::move(allocations),
            startup_clock_timeout_ms,
            actions);
        auto backend = std::unique_ptr<AsioOutputBackend>(
            new AsioOutputBackend(std::move(state)));
        auto started = backend->state_->StartControlThread();
        if (!started)
        {
            if (failure != nullptr)
            {
                *failure = std::move(started.error());
            }
            return nullptr;
        }
        auto startup = backend->state_->WaitForStartup();
        if (!startup)
        {
            if (failure != nullptr)
            {
                *failure = std::move(startup.error());
            }
            return nullptr;
        }
        return backend;
    }
    catch (const std::bad_alloc&)
    {
        if (failure != nullptr)
        {
            *failure = Failure(
                AsioFailureStage::render_core,
                "ASIO backend allocation failed");
        }
        return nullptr;
    }
    catch (const std::exception& error)
    {
        if (failure != nullptr)
        {
            *failure = Failure(
                AsioFailureStage::protocol,
                "ASIO backend startup failed: " +
                    std::string{error.what()});
        }
        return nullptr;
    }
    catch (...)
    {
        if (failure != nullptr)
        {
            *failure = Failure(
                AsioFailureStage::protocol,
                "ASIO backend startup failed unexpectedly");
        }
        return nullptr;
    }
}

} // namespace detail

AsioOutputBackend::AsioOutputBackend(
    std::unique_ptr<detail::AsioOutputBackendState> state) noexcept
    : state_(std::move(state))
{
}

AsioOutputBackend::~AsioOutputBackend() = default;

std::unique_ptr<AsioOutputBackend> AsioOutputBackend::StartAndWait(
    HWND game_window,
    const AsioStreamRequest& request,
    std::unique_ptr<IAsioRegistrySource> registry,
    std::unique_ptr<IAsioDriverFactory> factory,
    std::shared_ptr<IAsioOutputObserver> observer,
    std::shared_ptr<const ma_allocation_callbacks> allocations,
    DWORD startup_clock_timeout_ms,
    AsioFailure* failure) noexcept
{
    return detail::StartAsioOutputBackendAndWait(
        game_window,
        request,
        std::move(registry),
        std::move(factory),
        std::move(observer),
        std::move(allocations),
        startup_clock_timeout_ms,
        detail::ProductionAsioOutputBackendActions(),
        failure);
}

std::unique_ptr<MixerVoice> AsioOutputBackend::CreateVoice(
    const NormalizedSourceFormat& format,
    std::shared_ptr<AudioSnapshot> snapshot,
    std::shared_ptr<AudioCursorTimeline> timeline,
    VoiceUsage usage,
    ma_result* result) noexcept
{
    return state_ != nullptr
        ? state_->CreateVoice(
              format,
              std::move(snapshot),
              std::move(timeline),
              usage,
              result)
        : nullptr;
}

std::optional<std::uint64_t>
AsioOutputBackend::CurrentOutputFrame() noexcept
{
    return state_ != nullptr ? state_->CurrentOutputFrame() : std::nullopt;
}

std::uint32_t AsioOutputBackend::endpoint_buffer_frames() const noexcept
{
    return state_ != nullptr ? state_->endpoint_buffer_frames() : 0;
}

std::uint32_t AsioOutputBackend::output_sample_rate() const noexcept
{
    return state_ != nullptr ? state_->output_sample_rate() : 0;
}

void AsioOutputBackend::CountPendingCursorQuery() noexcept
{
    if (state_ != nullptr)
    {
        state_->CountPendingCursorQuery();
    }
}

void AsioOutputBackend::CountUnmappedCursorFailure() noexcept
{
    if (state_ != nullptr)
    {
        state_->CountUnmappedCursorFailure();
    }
}

} // namespace gc::audio

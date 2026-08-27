// SPDX-License-Identifier: CC0-1.0

#include "Audio/Asio/AsioOutputBackend.h"
#include "Audio/Asio/AsioOutputBackendInternal.h"

#include "Audio/Asio/AsioCallbackRuntime.h"
#include "Audio/Asio/AsioClock.h"
#include "Audio/Asio/AsioForegroundMonitor.h"
#include "Audio/Asio/AsioLogicalRenderSequencer.h"
#include "Audio/Asio/ExactAsioClock.h"
#include "Audio/Asio/AsioSampleConverter.h"
#include "Audio/Asio/AsioSession.h"
#include "Audio/ExactOutputClock.h"
#include "Audio/Mixer/AudioRenderCore.h"

#include <Windows.h>
#include <mmsystem.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
// ReSharper disable once CppUnusedIncludeDirective
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <initializer_list>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <utility>

namespace gc::audio
{
    namespace detail
    {
        namespace
        {
            static_assert(std::atomic_uint64_t::is_always_lock_free);
            static_assert(std::atomic_uint32_t::is_always_lock_free);
            static_assert(std::atomic_uint8_t::is_always_lock_free);
            static_assert(std::atomic_int32_t::is_always_lock_free);
            static_assert(std::atomic_bool::is_always_lock_free);
            static_assert(std::numeric_limits<float>::is_iec559);

            constexpr DWORD kRecoveryRetryMs = 250;
            constexpr std::uint64_t kOutputFramesPerMillisecond = 48;

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

            bool ProductionResetEvent(void*, HANDLE event) noexcept
            {
                return event != nullptr && ResetEvent(event) != FALSE;
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

            MMRESULT ProductionBeginTimerPeriod(void*, UINT period_ms) noexcept
            {
                return timeBeginPeriod(period_ms);
            }

            MMRESULT ProductionEndTimerPeriod(void*, UINT period_ms) noexcept
            {
                return timeEndPeriod(period_ms);
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

            void AppendSecondaryFailure(
                AsioFailure& primary,
                const AsioFailure& secondary) noexcept
            {
                try
                {
                    std::array<char, 320> suffix{};
                    const auto formatted = std::format_to_n(
                        suffix.data(),
                        suffix.size() - 1,
                        " secondary_stage={} secondary_domain={} secondary_result={}"
                        " secondary_detail={}",
                        static_cast<unsigned>(secondary.stage),
                        static_cast<unsigned>(secondary.domain),
                        secondary.result,
                        secondary.detail);
                    const auto size = (std::min)(
                        static_cast<std::size_t>(formatted.size),
                        suffix.size() - 1);
                    primary.detail.append(suffix.data(), size);
                }
                catch (...)
                {
                }
            }

            bool ActionsComplete(
                const AsioOutputBackendActions& actions,
                const bool enable_absolute_time_judgement) noexcept
            {
                const bool base_complete = actions.initialize_com != nullptr &&
                    actions.uninitialize_com != nullptr &&
                    actions.create_manual_event != nullptr &&
                    actions.signal_event != nullptr &&
                    actions.wait_for_event != nullptr &&
                    actions.close_handle != nullptr &&
                    actions.message_wait != nullptr &&
                    actions.drain_messages != nullptr &&
                    actions.tick_count_ms != nullptr &&
                    actions.time_get_time_ms != nullptr;
                return base_complete &&
                (!enable_absolute_time_judgement ||
                    (actions.begin_timer_period != nullptr &&
                        actions.end_timer_period != nullptr));
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

            const char* RuntimeFailureDetail(
                const AsioFailureStage stage) noexcept
            {
                switch (stage)
                {
                case AsioFailureStage::runtime_clock:
                    return "ASIO runtime clock became invalid; restart required";
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
                case AsioFailureStage::multimedia_timer:
                    return "timeEndPeriod(1) failed during ASIO absolute-clock teardown";
                default:
                    return "ASIO runtime failed; restart required";
                }
            }

            void SaturatingAddCounter(
                std::atomic_uint64_t& destination,
                std::uint64_t value) noexcept
            {
                auto observed = destination.load(std::memory_order_relaxed);
                for (;;)
                {
                    const auto remaining =
                        (std::numeric_limits<std::uint64_t>::max)() - observed;
                    const auto desired = value > remaining
                                             ? (std::numeric_limits<std::uint64_t>::max)()
                                             : observed + value;
                    if (destination.compare_exchange_weak(
                        observed,
                        desired,
                        std::memory_order_relaxed,
                        std::memory_order_relaxed))
                    {
                        return;
                    }
                }
            }

            void SaturatingIncrementCounter(
                std::atomic_uint64_t& destination) noexcept
            {
                SaturatingAddCounter(destination, 1);
            }

            std::uint64_t SaturatingSum(
                std::initializer_list<std::uint64_t> values) noexcept
            {
                std::uint64_t sum{};
                for (const auto value : values)
                {
                    const auto remaining =
                        (std::numeric_limits<std::uint64_t>::max)() - sum;
                    sum = value > remaining
                              ? (std::numeric_limits<std::uint64_t>::max)()
                              : sum + value;
                }
                return sum;
            }

            void MergeCallbackSnapshot(
                AsioCallbackRuntimeSnapshot& total,
                const AsioCallbackRuntimeSnapshot& session) noexcept
            {
                const auto add = [](std::uint64_t& destination,
                                    const std::uint64_t value) noexcept
                {
                    const auto remaining =
                        (std::numeric_limits<std::uint64_t>::max)() - destination;
                    destination = value > remaining
                                      ? (std::numeric_limits<std::uint64_t>::max)()
                                      : destination + value;
                };
                const auto maximum = [](std::uint64_t& destination,
                                        const std::uint64_t value) noexcept
                {
                    destination = (std::max)(destination, value);
                };

                add(total.callbacks, session.callbacks);
                add(total.time_info_callbacks, session.time_info_callbacks);
                add(total.legacy_callbacks, session.legacy_callbacks);
                add(total.deferred_callbacks, session.deferred_callbacks);
                add(total.deadline_misses, session.deadline_misses);
                add(total.overload_messages, session.overload_messages);
                add(total.reset_requests, session.reset_requests);
                add(total.resync_requests, session.resync_requests);
                add(total.latency_change_requests, session.latency_change_requests);
                add(total.buffer_size_change_requests,
                    session.buffer_size_change_requests);
                add(total.sample_rate_change_requests,
                    session.sample_rate_change_requests);
                add(total.buffer_alternation_violations,
                    session.buffer_alternation_violations);
                add(total.callback_interval_samples,
                    session.callback_interval_samples);
                add(total.total_callback_interval_ticks,
                    session.total_callback_interval_ticks);
                maximum(total.maximum_callback_interval_ticks,
                        session.maximum_callback_interval_ticks);
                add(total.early_callback_intervals, session.early_callback_intervals);
                add(total.late_callback_intervals, session.late_callback_intervals);
                add(total.severe_callback_intervals, session.severe_callback_intervals);
                add(total.timed_callback_work_samples,
                    session.timed_callback_work_samples);
                add(total.total_callback_ticks, session.total_callback_ticks);
                maximum(total.maximum_callback_ticks, session.maximum_callback_ticks);
                add(total.timed_render_work_samples,
                    session.timed_render_work_samples);
                add(total.total_render_ticks, session.total_render_ticks);
                maximum(total.maximum_render_ticks, session.maximum_render_ticks);
                add(total.driver_interval_samples, session.driver_interval_samples);
                maximum(total.maximum_driver_period_error_ns,
                        session.maximum_driver_period_error_ns);
                maximum(total.maximum_host_driver_interval_skew_ns,
                        session.maximum_host_driver_interval_skew_ns);
                if (total.expected_period_ns == 0)
                {
                    total.expected_period_ns = session.expected_period_ns;
                }
                if (total.qpc_frequency == 0)
                {
                    total.qpc_frequency = session.qpc_frequency;
                }
                if (session.last_reported_sample_rate != 0.0)
                {
                    total.last_reported_sample_rate =
                        session.last_reported_sample_rate;
                }
                if (total.first_fault == AsioFailureStage::none)
                {
                    total.first_fault = session.first_fault;
                }
            }

            void UpdateMaximumFloatBits(
                std::atomic_uint32_t& destination,
                float value) noexcept
            {
                if (!std::isfinite(value) || value < 0.0F)
                {
                    return;
                }
                const auto bits = std::bit_cast<std::uint32_t>(value);
                auto observed = destination.load(std::memory_order_relaxed);
                while (observed < bits &&
                    !destination.compare_exchange_weak(
                        observed,
                        bits,
                        std::memory_order_relaxed,
                        std::memory_order_relaxed))
                {
                }
            }
        } // namespace

        void AsioRenderDiagnostics::RecordRender(
            const AudioRenderBlock& block) noexcept
        {
            switch (block.silence_reason)
            {
            case AudioRenderSilenceReason::none:
                break;
            case AudioRenderSilenceReason::no_active_voice:
                SaturatingIncrementCounter(no_active_voice_silence_blocks_);
                SaturatingAddCounter(
                    short_read_missing_frames_,
                    block.missing_frames);
                break;
            case AudioRenderSilenceReason::active_short_read:
                SaturatingIncrementCounter(active_short_read_blocks_);
                SaturatingAddCounter(
                    short_read_missing_frames_,
                    block.missing_frames);
                break;
            case AudioRenderSilenceReason::mixer_error:
                {
                    SaturatingIncrementCounter(mixer_error_blocks_);
                    if (block.mixer_result != MA_SUCCESS)
                    {
                        std::int32_t expected = MA_SUCCESS;
                        first_mixer_error_.compare_exchange_strong(
                            expected,
                            static_cast<std::int32_t>(block.mixer_result),
                            std::memory_order_relaxed,
                            std::memory_order_relaxed);
                    }
                    break;
                }
            case AudioRenderSilenceReason::render_contract_error:
                SaturatingIncrementCounter(render_contract_error_blocks_);
                break;
            }
        }

        void AsioRenderDiagnostics::RecordConversion(
            const AudioRenderBlock& block,
            const AsioStereoConversionResult& conversion) noexcept
        {
            if (conversion.stats.non_finite)
            {
                SaturatingIncrementCounter(non_finite_output_blocks_);
                return;
            }
            if (!conversion.converted || block.silence_substituted)
            {
                return;
            }
            if (conversion.stats.clipped_samples != 0)
            {
                SaturatingIncrementCounter(clipped_output_blocks_);
                SaturatingAddCounter(
                    clipped_output_samples_,
                    conversion.stats.clipped_samples);
            }
            UpdateMaximumFloatBits(
                maximum_absolute_output_sample_bits_,
                conversion.stats.maximum_absolute_sample);
            if (conversion.stats.all_zero)
            {
                auto& counter = block.active_voices != 0
                                    ? zero_output_blocks_with_active_voice_
                                    : zero_output_blocks_without_active_voice_;
                SaturatingIncrementCounter(counter);
            }
        }

        AsioRenderDiagnosticsSnapshot
        AsioRenderDiagnostics::Snapshot() const noexcept
        {
            return {
                .no_active_voice_silence_blocks =
                no_active_voice_silence_blocks_.load(
                    std::memory_order_relaxed),
                .active_short_read_blocks =
                active_short_read_blocks_.load(std::memory_order_relaxed),
                .mixer_error_blocks =
                mixer_error_blocks_.load(std::memory_order_relaxed),
                .render_contract_error_blocks =
                render_contract_error_blocks_.load(std::memory_order_relaxed),
                .short_read_missing_frames =
                short_read_missing_frames_.load(std::memory_order_relaxed),
                .first_mixer_error = static_cast<ma_result>(
                    first_mixer_error_.load(std::memory_order_relaxed)),
                .clipped_output_blocks =
                clipped_output_blocks_.load(std::memory_order_relaxed),
                .clipped_output_samples =
                clipped_output_samples_.load(std::memory_order_relaxed),
                .zero_output_blocks_with_active_voice =
                zero_output_blocks_with_active_voice_.load(
                    std::memory_order_relaxed),
                .zero_output_blocks_without_active_voice =
                zero_output_blocks_without_active_voice_.load(
                    std::memory_order_relaxed),
                .non_finite_output_blocks =
                non_finite_output_blocks_.load(std::memory_order_relaxed),
                .maximum_absolute_output_sample = std::bit_cast<float>(
                    maximum_absolute_output_sample_bits_.load(
                        std::memory_order_relaxed)),
            };
        }

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
                .begin_timer_period = &ProductionBeginTimerPeriod,
                .end_timer_period = &ProductionEndTimerPeriod,
                .callback_runtime_actions =
                ProductionAsioCallbackRuntimeActions(),
                .summary_interval_ms = kAsioRuntimeSummaryIntervalMs,
                .reset_event = &ProductionResetEvent,
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
                bool enable_absolute_time_judgement,
                const AsioOutputBackendActions& actions)
                : game_window_(game_window),
                  request_(std::move(request)),
                  registry_(std::move(registry)),
                  factory_(std::move(factory)),
                  observer_(std::move(observer)),
                  mixer_allocations_(std::move(allocations)),
                  startup_clock_timeout_ms_(
                      BoundedDeadline(startup_clock_timeout_ms)),
                  enable_absolute_time_judgement_(
                      enable_absolute_time_judgement),
                  actions_(actions),
                  logical_render_sequencer_(request_.buffer_frames, 48'000)
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
                if (enable_absolute_time_judgement_ &&
                    usage == VoiceUsage::GameplayNativeCandidate)
                {
                    const auto buffer_instance_id = timeline != nullptr
                                                        ? timeline->exact_buffer_instance_id()
                                                        : 0;
                    const auto endpoint_generation = exact_clock_ != nullptr
                                                         ? exact_clock_->info().endpoint_generation
                                                         : 0;
                    if (exact_clock_ == nullptr || timeline == nullptr ||
                        buffer_instance_id == 0 || endpoint_generation == 0 ||
                        !timeline->ConfigureExactPlaybackHistory(
                            buffer_instance_id, endpoint_generation))
                    {
                        if (result != nullptr)
                        {
                            *result = MA_INVALID_OPERATION;
                        }
                        LatchRuntimeFault(AsioFailureStage::runtime_clock);
                        return nullptr;
                    }
                }
                return render_core_->CreateVoice(
                    format,
                    std::move(snapshot),
                    std::move(timeline),
                    usage,
                    result);
            }

            // ReSharper disable once CppMemberFunctionMayBeConst
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
            enum class StableRenderOutcome : std::uint8_t
            {
                stable,
                shutdown,
            };

            enum class PhysicalPreparationFailureKind : std::uint8_t
            {
                retryable_before_start,
                fatal,
            };

            struct PhysicalPreparationFailure final
            {
                PhysicalPreparationFailureKind kind{
                    PhysicalPreparationFailureKind::fatal
                };
                AsioFailure failure;
            };

            static PhysicalPreparationFailure RetryablePreparationFailure(
                AsioFailure failure) noexcept
            {
                return {
                    .kind = PhysicalPreparationFailureKind::retryable_before_start,
                    .failure = std::move(failure),
                };
            }

            static PhysicalPreparationFailure FatalPreparationFailure(
                AsioFailure failure) noexcept
            {
                return {
                    .kind = PhysicalPreparationFailureKind::fatal,
                    .failure = std::move(failure),
                };
            }

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
                for (HANDLE event : events)
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
                    auto failure = std::move(initialized.error());
                    if (const auto teardown_failure = TeardownOnControlThread())
                    {
                        AppendSecondaryFailure(failure, *teardown_failure);
                    }
                    actions_.uninitialize_com(actions_.context);
                    CompleteStartupFailure(std::move(failure));
                    return;
                }

                auto stable = WaitForStableRender();
                if (!stable || *stable == StableRenderOutcome::shutdown)
                {
                    auto failure = !stable
                                       ? std::move(stable.error())
                                       : Failure(
                                           AsioFailureStage::startup_clock,
                                           "ASIO startup was cancelled before clock stability");
                    if (const auto teardown_failure = TeardownOnControlThread())
                    {
                        AppendSecondaryFailure(failure, *teardown_failure);
                    }
                    actions_.uninitialize_com(actions_.context);
                    CompleteStartupFailure(std::move(failure));
                    return;
                }

                committed_.store(true, std::memory_order_release);
                observer_->StartupSucceeded(session_->report());
                startup_succeeded_.store(true, std::memory_order_release);
                actions_.signal_event(actions_.context, startup_event_);

                auto runtime_failure = MonitorCommittedRuntime(true, true);
                const auto teardown_failure = TeardownOnControlThread();
                if (!runtime_failure && HasPublishedFault())
                {
                    runtime_failure = BuildLatchedFailure();
                }
                if (teardown_failure)
                {
                    if (!runtime_failure)
                    {
                        runtime_failure = *teardown_failure;
                    }
                    else if (runtime_failure->stage != teardown_failure->stage ||
                        runtime_failure->domain != teardown_failure->domain ||
                        runtime_failure->result != teardown_failure->result)
                    {
                        AppendSecondaryFailure(
                            *runtime_failure, *teardown_failure);
                    }
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

            std::expected<void, AsioFailure>
            InitializeBackend() noexcept
            {
                try
                {
                    if (game_window_ == nullptr || registry_ == nullptr ||
                        factory_ == nullptr || observer_ == nullptr)
                    {
                        return std::unexpected(Failure(
                            AsioFailureStage::init,
                            "ASIO runtime dependencies and game HWND are required"));
                    }

                    AsioFailure monitor_failure{};
                    foreground_monitor_ = AsioForegroundMonitor::Start(
                        game_window_, &monitor_failure);
                    if (foreground_monitor_ == nullptr)
                    {
                        return std::unexpected(std::move(monitor_failure));
                    }

                    if (enable_absolute_time_judgement_)
                    {
                        const auto timer_result = actions_.begin_timer_period(
                            actions_.context, 1);
                        if (timer_result != TIMERR_NOERROR)
                        {
                            return std::unexpected(Failure(
                                AsioFailureStage::multimedia_timer,
                                "timeBeginPeriod(1) failed before ASIO startup",
                                AsioResultDomain::winmm,
                                timer_result));
                        }
                        timer_period_active_ = true;
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

                    auto prepared = PreparePhysicalSession();
                    if (!prepared)
                    {
                        return std::unexpected(
                            std::move(prepared.error().failure));
                    }
                    if (auto started = StartPreparedPhysicalSession(); !started)
                    {
                        return started;
                    }
                    endpoint_buffer_frames_.store(
                        request_.buffer_frames,
                        std::memory_order_release);
                    output_sample_rate_.store(48'000, std::memory_order_release);
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

            std::expected<void, PhysicalPreparationFailure>
            PreparePhysicalSession() noexcept
            {
                try
                {
                    if (session_ != nullptr || callback_runtime_ != nullptr)
                    {
                        return std::unexpected(FatalPreparationFailure(Failure(
                            AsioFailureStage::protocol,
                            "ASIO physical session must be closed before acquisition")));
                    }
                    ClearSessionFault();

                    auto registration = ResolveAsioDriver(
                        *registry_, request_.driver_name);
                    if (!registration)
                    {
                        return std::unexpected(RetryablePreparationFailure(
                            std::move(registration.error())));
                    }
                    auto driver = factory_->Create(registration->clsid);
                    if (!driver)
                    {
                        return std::unexpected(RetryablePreparationFailure(
                            std::move(driver.error())));
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
                        return std::unexpected(RetryablePreparationFailure(
                            std::move(prepared.error())));
                    }
                    session_ = std::move(*prepared);

                    AsioLegacyPositionActions legacy{
                        this,
                        &AsioOutputBackendState::ReadLegacyPosition,
                    };
                    auto callbacks = AsioCallbackRuntime::Prepare(
                        *this,
                        legacy,
                        {request_.buffer_frames, 48'000},
                        actions_.callback_runtime_actions);
                    if (!callbacks)
                    {
                        return std::unexpected(RetryablePreparationFailure(
                            std::move(callbacks.error())));
                    }
                    callback_runtime_ = std::move(*callbacks);
                    if (auto installed = callback_runtime_->Install(); !installed)
                    {
                        return std::unexpected(RetryablePreparationFailure(
                            std::move(installed.error())));
                    }
                    if (auto buffers = session_->CreateOutputBuffers(
                            AsioCallbackRuntime::Callbacks());
                        !buffers)
                    {
                        return std::unexpected(RetryablePreparationFailure(
                            std::move(buffers.error())));
                    }
                    if (auto views = ConfigureDriverBuffers(); !views)
                    {
                        return std::unexpected(RetryablePreparationFailure(
                            std::move(views.error())));
                    }
                    if (auto contract = EstablishOrValidatePhysicalContract();
                        !contract)
                    {
                        return std::unexpected(FatalPreparationFailure(
                            std::move(contract.error())));
                    }
                    return {};
                }
                catch (const std::bad_alloc&)
                {
                    return std::unexpected(FatalPreparationFailure(Failure(
                        AsioFailureStage::render_core,
                        "ASIO physical-session allocation failed")));
                }
                catch (const std::exception& error)
                {
                    return std::unexpected(FatalPreparationFailure(Failure(
                        AsioFailureStage::protocol,
                        "ASIO physical-session acquisition failed: " +
                        std::string{error.what()})));
                }
                catch (...)
                {
                    return std::unexpected(FatalPreparationFailure(Failure(
                        AsioFailureStage::protocol,
                        "ASIO physical-session acquisition failed unexpectedly")));
                }
            }

            std::expected<void, AsioFailure>
            StartPreparedPhysicalSession() noexcept
            {
                try
                {
                    if (session_ == nullptr || callback_runtime_ == nullptr)
                    {
                        return std::unexpected(Failure(
                            AsioFailureStage::protocol,
                            "ASIO Start requires a prepared physical session"));
                    }

                    const auto physical_generation =
                        logical_render_sequencer_.BeginPhysicalSession();
                    if (!physical_generation)
                    {
                        return std::unexpected(Failure(
                            AsioFailureStage::runtime_clock,
                            std::format(
                                "Could not begin ASIO physical-session generation: {}",
                                static_cast<unsigned>(physical_generation.error()))));
                    }
                    active_physical_session_generation_.store(
                        *physical_generation, std::memory_order_release);
                    clock_tracker_.Reset(
                        request_.buffer_frames,
                        session_->report().output_latency_frames);
                    has_previous_sample_position_ = false;
                    render_ready_.store(true, std::memory_order_release);
                    if (auto started = session_->Start(); !started)
                    {
                        render_ready_.store(false, std::memory_order_release);
                        return std::unexpected(std::move(started.error()));
                    }
                    return {};
                }
                catch (const std::bad_alloc&)
                {
                    return std::unexpected(Failure(
                        AsioFailureStage::start,
                        "ASIO Start diagnostics allocation failed"));
                }
                catch (...)
                {
                    render_ready_.store(false, std::memory_order_release);
                    return std::unexpected(Failure(
                        AsioFailureStage::start,
                        "ASIO Start boundary failed unexpectedly"));
                }
            }

            std::expected<void, AsioFailure>
            EstablishOrValidatePhysicalContract() noexcept
            {
                const auto& report = session_->report();
                if (physical_contract_established_)
                {
                    if (report.sample_rate != 48'000.0 ||
                        report.effective_buffer_frames != request_.buffer_frames ||
                        report.output_latency_frames != logical_output_latency_frames_ ||
                        channel_types_ != logical_channel_types_)
                    {
                        return std::unexpected(Failure(
                            AsioFailureStage::protocol,
                            "Reacquired ASIO session changed the logical endpoint contract"));
                    }
                    return {};
                }

                logical_output_latency_frames_ = report.output_latency_frames;
                logical_channel_types_ = channel_types_;
                if (enable_absolute_time_judgement_)
                {
                    const auto callback = callback_runtime_->Snapshot();
                    const auto endpoint_generation =
                        detail::NextExactOutputClockGeneration();
                    if (endpoint_generation == 0 || callback.qpc_frequency == 0 ||
                        callback.qpc_frequency > static_cast<std::uint64_t>(
                            (std::numeric_limits<std::int64_t>::max)()))
                    {
                        return std::unexpected(Failure(
                            AsioFailureStage::startup_clock,
                            "ASIO exact clock generation or QPC frequency is invalid"));
                    }
                    exact_clock_ = ExactAsioClock::Create(
                        endpoint_generation,
                        48'000,
                        static_cast<std::int64_t>(callback.qpc_frequency),
                        request_.buffer_frames,
                        logical_output_latency_frames_);
                    if (exact_clock_ == nullptr)
                    {
                        return std::unexpected(Failure(
                            AsioFailureStage::startup_clock,
                            "Could not allocate the ASIO exact clock history"));
                    }
                    exact_endpoint_generation_ = endpoint_generation;
                    if (!detail::RegisterExactOutputClock(exact_clock_))
                    {
                        return std::unexpected(Failure(
                            AsioFailureStage::startup_clock,
                            "Could not register the ASIO exact clock provider"));
                    }
                    exact_clock_registered_ = true;
                }
                physical_contract_established_ = true;
                return {};
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

            // ReSharper disable once CppMemberFunctionMayBeConst
            std::expected<StableRenderOutcome, AsioFailure>
            WaitForStableRender() noexcept
            {
                const std::uint64_t started_ms =
                    actions_.tick_count_ms(actions_.context);
                const std::array<HANDLE, 4> handles{
                    stable_render_event_,
                    fault_event_,
                    shutdown_event_,
                    foreground_monitor_->change_event(),
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
                        if (HasPublishedFault())
                        {
                            return std::unexpected(BuildLatchedFailure());
                        }
                        return StableRenderOutcome::stable;
                    }
                    if (wait == WAIT_OBJECT_0 + 1)
                    {
                        return std::unexpected(BuildLatchedFailure());
                    }
                    if (wait == WAIT_OBJECT_0 + 2)
                    {
                        return StableRenderOutcome::shutdown;
                    }
                    if (wait == WAIT_OBJECT_0 + 3)
                    {
                        if (!foreground_monitor_->healthy())
                        {
                            return std::unexpected(Failure(
                                AsioFailureStage::foreground_monitor,
                                "ASIO foreground monitor stopped unexpectedly",
                                AsioResultDomain::win32,
                                foreground_monitor_->failure_code()));
                        }
                        continue;
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

            std::optional<AsioFailure> MonitorCommittedRuntime(
                bool session_active,
                bool foreground_reported) noexcept
            {
                const std::array<HANDLE, 3> handles{
                    fault_event_,
                    shutdown_event_,
                    foreground_monitor_->change_event(),
                };
                std::uint64_t recovery_attempt{};
                std::uint64_t next_recovery_ms{};
                std::uint64_t summary_started =
                    actions_.tick_count_ms(actions_.context);
                for (;;)
                {
                    // Elapsed time schedules summaries, silent advancement, and retry
                    // attempts. Foreground ownership is established only by the
                    // monitor's direct window/process query below.
                    const auto now_ms = actions_.tick_count_ms(actions_.context);
                    const DWORD summary_remaining = RemainingTimeout(
                        summary_started,
                        now_ms,
                        actions_.summary_interval_ms);
                    const DWORD remaining = session_active
                                                ? summary_remaining
                                                : (std::min)(summary_remaining, SilentPollIntervalMs());
                    const DWORD wait = actions_.message_wait(
                        actions_.context,
                        handles,
                        remaining);
                    if (wait == WAIT_OBJECT_0 + 1)
                    {
                        return std::nullopt;
                    }
                    if (wait == WAIT_OBJECT_0 + handles.size())
                    {
                        actions_.drain_messages(actions_.context);
                    }
                    else if (wait != WAIT_OBJECT_0 &&
                        wait != WAIT_OBJECT_0 + 2 &&
                        wait != WAIT_TIMEOUT)
                    {
                        return Failure(
                            AsioFailureStage::callback,
                            "ASIO runtime message wait failed",
                            AsioResultDomain::win32,
                            wait == WAIT_FAILED ? GetLastError() : wait);
                    }

                    if (!foreground_monitor_->healthy())
                    {
                        return Failure(
                            AsioFailureStage::foreground_monitor,
                            "ASIO foreground monitor stopped unexpectedly",
                            AsioResultDomain::win32,
                            foreground_monitor_->failure_code());
                    }

                    if (HasPublishedFault())
                    {
                        return BuildLatchedFailure();
                    }

                    const bool foreground =
                        foreground_monitor_->snapshot().is_foreground;
                    if (!foreground)
                    {
                        if (foreground_reported)
                        {
                            foreground_reported = false;
                            SaturatingIncrementCounter(foreground_losses_);
                            observer_->SessionLifecycleChanged(
                                AsioSessionLifecycleEvent::foreground_lost,
                                recovery_attempt,
                                nullptr);
                        }
                        if (session_active)
                        {
                            auto release_failure = ClosePhysicalSession();
                            session_active = false;
                            if (HasPublishedFault())
                            {
                                auto runtime_failure = BuildLatchedFailure();
                                if (release_failure)
                                {
                                    AppendSecondaryFailure(
                                        runtime_failure, *release_failure);
                                }
                                release_failure = std::move(runtime_failure);
                            }
                            SaturatingIncrementCounter(session_releases_);
                            observer_->SessionLifecycleChanged(
                                AsioSessionLifecycleEvent::session_released,
                                recovery_attempt,
                                release_failure ? &*release_failure : nullptr);
                            if (release_failure)
                            {
                                return release_failure;
                            }
                            ClearSessionFault();
                        }
                        if (const auto advance_failure = AdvanceSilentRendering())
                        {
                            return advance_failure;
                        }
                    }
                    else if (!session_active)
                    {
                        if (!foreground_reported)
                        {
                            foreground_reported = true;
                            observer_->SessionLifecycleChanged(
                                AsioSessionLifecycleEvent::foreground_regained,
                                recovery_attempt,
                                nullptr);
                        }
                        if (const auto advance_failure = AdvanceSilentRendering())
                        {
                            return advance_failure;
                        }

                        const auto recovery_now =
                            actions_.tick_count_ms(actions_.context);
                        if (next_recovery_ms == 0 ||
                            recovery_now >= next_recovery_ms)
                        {
                            ++recovery_attempt;
                            SaturatingIncrementCounter(recovery_attempts_);
                            auto prepared = PreparePhysicalSession();
                            if (!prepared)
                            {
                                auto preparation_failure =
                                    std::move(prepared.error());
                                const bool acquired_resources =
                                    session_ != nullptr || callback_runtime_ != nullptr;
                                const auto close_failure = ClosePhysicalSession();
                                if (close_failure)
                                {
                                    AppendSecondaryFailure(
                                        preparation_failure.failure,
                                        *close_failure);
                                }
                                if (acquired_resources)
                                {
                                    SaturatingIncrementCounter(session_releases_);
                                    observer_->SessionLifecycleChanged(
                                        AsioSessionLifecycleEvent::session_released,
                                        recovery_attempt,
                                        &preparation_failure.failure);
                                }
                                if (close_failure)
                                {
                                    return std::move(preparation_failure.failure);
                                }
                                ClearSessionFault();
                                if (preparation_failure.kind ==
                                    PhysicalPreparationFailureKind::fatal)
                                {
                                    return std::move(preparation_failure.failure);
                                }
                                if (!foreground_monitor_->snapshot().is_foreground)
                                {
                                    if (foreground_reported)
                                    {
                                        foreground_reported = false;
                                        SaturatingIncrementCounter(foreground_losses_);
                                        observer_->SessionLifecycleChanged(
                                            AsioSessionLifecycleEvent::foreground_lost,
                                            recovery_attempt,
                                            nullptr);
                                    }
                                    next_recovery_ms = 0;
                                }
                                else
                                {
                                    SaturatingIncrementCounter(recovery_failures_);
                                    observer_->SessionLifecycleChanged(
                                        AsioSessionLifecycleEvent::recovery_attempt_failed,
                                        recovery_attempt,
                                        &preparation_failure.failure);
                                    next_recovery_ms =
                                        recovery_now + kRecoveryRetryMs;
                                }
                            }
                            else
                            {
                                if (!foreground_monitor_->snapshot().is_foreground)
                                {
                                    const auto close_failure =
                                        ClosePhysicalSession();
                                    SaturatingIncrementCounter(session_releases_);
                                    observer_->SessionLifecycleChanged(
                                        AsioSessionLifecycleEvent::session_released,
                                        recovery_attempt,
                                        close_failure ? &*close_failure : nullptr);
                                    if (close_failure)
                                    {
                                        return close_failure;
                                    }
                                    ClearSessionFault();
                                    if (foreground_reported)
                                    {
                                        foreground_reported = false;
                                        SaturatingIncrementCounter(
                                            foreground_losses_);
                                        observer_->SessionLifecycleChanged(
                                            AsioSessionLifecycleEvent::foreground_lost,
                                            recovery_attempt,
                                            nullptr);
                                    }
                                    next_recovery_ms = 0;
                                    continue;
                                }

                                auto started = StartPreparedPhysicalSession();
                                if (!started)
                                {
                                    auto failure = std::move(started.error());
                                    if (const auto close_failure =
                                        ClosePhysicalSession())
                                    {
                                        AppendSecondaryFailure(
                                            failure, *close_failure);
                                    }
                                    SaturatingIncrementCounter(session_releases_);
                                    observer_->SessionLifecycleChanged(
                                        AsioSessionLifecycleEvent::session_released,
                                        recovery_attempt,
                                        &failure);
                                    return failure;
                                }

                                auto stable = WaitForStableRender();
                                if (stable &&
                                    *stable == StableRenderOutcome::shutdown)
                                {
                                    return std::nullopt;
                                }
                                if (!stable)
                                {
                                    auto failure = std::move(stable.error());
                                    if (const auto close_failure =
                                        ClosePhysicalSession())
                                    {
                                        AppendSecondaryFailure(
                                            failure, *close_failure);
                                    }
                                    SaturatingIncrementCounter(session_releases_);
                                    observer_->SessionLifecycleChanged(
                                        AsioSessionLifecycleEvent::session_released,
                                        recovery_attempt,
                                        &failure);
                                    return failure;
                                }

                                session_active = true;
                                next_recovery_ms = 0;
                                SaturatingIncrementCounter(session_recoveries_);
                                observer_->SessionLifecycleChanged(
                                    AsioSessionLifecycleEvent::session_recovered,
                                    recovery_attempt,
                                    nullptr);
                            }
                        }
                    }

                    const auto summary_now =
                        actions_.tick_count_ms(actions_.context);
                    if (summary_now - summary_started >=
                        actions_.summary_interval_ms)
                    {
                        observer_->RuntimeSummary(SnapshotCounters());
                        summary_started = summary_now;
                    }
                }
            }

            std::optional<AsioFailure> ReleaseTimerPeriod() noexcept
            {
                if (!timer_period_active_)
                {
                    return std::nullopt;
                }
                timer_period_active_ = false;
                const auto result = actions_.end_timer_period(actions_.context, 1);
                if (result == TIMERR_NOERROR)
                {
                    return std::nullopt;
                }
                return Failure(
                    AsioFailureStage::multimedia_timer,
                    "timeEndPeriod(1) failed during ASIO absolute-clock teardown",
                    AsioResultDomain::winmm,
                    result);
            }

            std::optional<AsioFailure> ClosePhysicalSession() noexcept
            {
                // Focus release owns only IASIO, its buffers, and callback runtime.
                // The mixer and both logical clocks must survive this operation so
                // existing voices and judgement-clock bindings remain valid.
                render_ready_.store(false, std::memory_order_release);
                std::optional<AsioFailure> failure;
                const auto record_failure = [&failure](
                    AsioFailure candidate) noexcept
                {
                    if (failure)
                    {
                        AppendSecondaryFailure(*failure, candidate);
                    }
                    else
                    {
                        failure = std::move(candidate);
                    }
                };
                if (callback_runtime_ != nullptr)
                {
                    callback_runtime_->BeginStopping();
                }
                if (session_ != nullptr)
                {
                    if (auto stopped = session_->Stop(); !stopped)
                    {
                        record_failure(std::move(stopped.error()));
                    }
                }
                if (callback_runtime_ != nullptr)
                {
                    callback_runtime_->JoinWorker();
                    callback_runtime_->Uninstall();
                    MergeCallbackSnapshot(
                        completed_callback_snapshot_,
                        callback_runtime_->Snapshot());
                }
                const auto physical_generation =
                    active_physical_session_generation_.exchange(
                        0, std::memory_order_acq_rel);
                if (physical_generation != 0 &&
                    !logical_render_sequencer_.EndPhysicalSession(
                        physical_generation))
                {
                    record_failure(Failure(
                        AsioFailureStage::runtime_clock,
                        "Could not end the ASIO physical-session generation after callback quiescence"));
                }
                if (session_ != nullptr)
                {
                    if (auto closed = session_->Close(); !closed)
                    {
                        record_failure(std::move(closed.error()));
                    }
                    session_.reset();
                }
                callback_runtime_.reset();
                for (auto& channel : driver_buffers_)
                {
                    channel = {};
                }
                has_previous_sample_position_ = false;
                return failure;
            }

            std::optional<AsioFailure> TeardownOnControlThread() noexcept
            {
                final_stopping_.store(true, std::memory_order_release);
                auto close_failure = ClosePhysicalSession();
                std::optional<AsioFailure> failure;
                if (close_failure)
                {
                    failure = std::move(*close_failure);
                }
                if (render_core_ != nullptr)
                {
                    render_core_->InvalidatePresentationClock();
                }
                if (exact_clock_ != nullptr)
                {
                    final_exact_clock_counters_ = exact_clock_->counters();
                    has_final_exact_clock_counters_ = true;
                    exact_clock_->Invalidate();
                    if (exact_clock_registered_)
                    {
                        detail::UnregisterExactOutputClock(
                            exact_endpoint_generation_);
                    }
                    exact_clock_registered_ = false;
                    exact_endpoint_generation_ = 0;
                    exact_clock_.reset();
                }
                presented_clock_ = nullptr;
                foreground_monitor_.reset();
                if (auto timer_failure = ReleaseTimerPeriod(); timer_failure)
                {
                    if (failure)
                    {
                        AppendSecondaryFailure(*failure, *timer_failure);
                    }
                    else
                    {
                        failure = std::move(timer_failure);
                    }
                }
                return failure;
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

            struct LogicalRenderResult
            {
                AudioRenderBlock block;
                std::uint64_t submitted_output_tail{};
            };

            std::optional<LogicalRenderResult> RenderLogicalBlock(
                const AsioLogicalRenderPlan& plan) noexcept
            {
                if (render_core_ == nullptr)
                {
                    logical_render_sequencer_.Abandon(plan);
                    return std::nullopt;
                }
                const auto block = render_core_->Render(plan.timeline);
                render_diagnostics_.RecordRender(block);
                return LogicalRenderResult{block, plan.submitted_output_tail};
            }

            bool PublishExactAnchor(
                const AsioLogicalRenderPlan& plan,
                const std::uint64_t presented_output_frame,
                const bool detached) noexcept
            {
                if (!enable_absolute_time_judgement_)
                {
                    return true;
                }
                if (exact_clock_ == nullptr ||
                    exact_endpoint_generation_ == 0 ||
                    exact_anchor_sequence_ ==
                    (std::numeric_limits<std::uint64_t>::max)())
                {
                    return false;
                }

                const auto next_sequence = exact_anchor_sequence_ + 1;
                if (!exact_clock_->Publish({
                    .sequence = next_sequence,
                    .endpoint_generation = exact_endpoint_generation_,
                    .presented_output_frame = presented_output_frame,
                    .system_time_ns = plan.system_time_ns,
                    .submitted_output_tail = plan.submitted_output_tail,
                }))
                {
                    return false;
                }
                exact_anchor_sequence_ = next_sequence;
                if (detached)
                {
                    SaturatingIncrementCounter(
                        detached_exact_anchor_publications_);
                }
                return true;
            }

            DWORD SilentPollIntervalMs() const noexcept
            {
                const auto milliseconds =
                    (static_cast<std::uint64_t>(request_.buffer_frames) +
                        kOutputFramesPerMillisecond - 1) /
                    kOutputFramesPerMillisecond;
                return static_cast<DWORD>((std::max)(
                    std::uint64_t{1},
                    (std::min)(milliseconds,
                               static_cast<std::uint64_t>((std::numeric_limits<DWORD>::max)()))));
            }

            std::optional<AsioFailure> AdvanceSilentRendering() noexcept
            {
                // This clock advances an already-confirmed background interval; it is
                // never used to infer whether the game owns the foreground.
                const auto plan = logical_render_sequencer_.TryPlanDetached(
                    actions_.time_get_time_ms(actions_.context));
                if (!plan)
                {
                    if (plan.error() == AsioLogicalRenderPlanFailure::Busy ||
                        plan.error() == AsioLogicalRenderPlanFailure::NotDue)
                    {
                        return std::nullopt;
                    }
                    return Failure(
                        AsioFailureStage::runtime_clock,
                        std::format(
                            "ASIO detached logical render planning failed: {}",
                            static_cast<unsigned>(plan.error())));
                }
                const auto rendered = RenderLogicalBlock(*plan);
                if (!rendered)
                {
                    return Failure(
                        AsioFailureStage::runtime_clock,
                        "ASIO silent continuity render contract failed");
                }
                const auto presented =
                    plan->timeline.output_frame_begin >=
                    logical_output_latency_frames_
                        ? plan->timeline.output_frame_begin -
                        logical_output_latency_frames_
                        : 0;
                if (!PublishExactAnchor(*plan, presented, true))
                {
                    logical_render_sequencer_.Abandon(*plan);
                    return Failure(
                        AsioFailureStage::runtime_clock,
                        "ASIO exact clock rejected a detached continuity anchor");
                }
                presented_clock_->PublishContinuityAnchor(
                    presented,
                    rendered->submitted_output_tail,
                    plan->system_time_ns);
                if (!logical_render_sequencer_.Commit(*plan))
                {
                    logical_render_sequencer_.Abandon(*plan);
                    return Failure(
                        AsioFailureStage::runtime_clock,
                        "ASIO detached logical render commit failed");
                }
                SaturatingAddCounter(
                    silent_advance_frames_,
                    plan->timeline.discontinuity_frames +
                    request_.buffer_frames);
                return std::nullopt;
            }

            void ClearSessionFault() noexcept
            {
                if (actions_.reset_event != nullptr)
                {
                    actions_.reset_event(actions_.context, stable_render_event_);
                    actions_.reset_event(actions_.context, fault_event_);
                }
                else
                {
                    ResetEvent(stable_render_event_);
                    ResetEvent(fault_event_);
                }
                first_fault_stage_.store(
                    static_cast<std::uint8_t>(AsioFailureStage::none),
                    std::memory_order_relaxed);
                first_fault_domain_.store(
                    static_cast<std::uint8_t>(AsioResultDomain::none),
                    std::memory_order_relaxed);
                first_fault_result_.store(0, std::memory_order_relaxed);
                first_fault_claimed_.store(false, std::memory_order_release);
            }

            bool HasPublishedFault() const noexcept
            {
                return static_cast<AsioFailureStage>(
                        first_fault_stage_.load(std::memory_order_acquire)) !=
                    AsioFailureStage::none;
            }

            // ReSharper disable once CppOverrideWithDifferentVisibility
            void RenderAsioBlock(const AsioRenderRequest& request) noexcept override
            {
                if (!render_ready_.load(std::memory_order_acquire) ||
                    final_stopping_.load(std::memory_order_acquire) ||
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
                const auto logical_plan =
                    logical_render_sequencer_.TryPlanPhysical(
                        active_physical_session_generation_.load(
                            std::memory_order_acquire),
                        decision);
                if (!logical_plan)
                {
                    ClearAsioBlock(request.buffer_index);
                    if (logical_plan.error() ==
                        AsioLogicalRenderPlanFailure::Busy)
                    {
                        return;
                    }
                    LatchRuntimeFault(AsioFailureStage::runtime_clock);
                    return;
                }
                const auto rendered = RenderLogicalBlock(*logical_plan);
                if (!rendered)
                {
                    ClearAsioBlock(request.buffer_index);
                    LatchRuntimeFault(AsioFailureStage::runtime_clock);
                    return;
                }
                const AsioClockDecision logical{
                    .kind = decision.kind,
                    .presented_output_frame =
                    logical_plan->presented_output_frame,
                    .render_output_frame_begin =
                    logical_plan->timeline.output_frame_begin,
                    .system_time_ns = logical_plan->system_time_ns,
                };
                if (logical.kind == AsioClockDecisionKind::priming)
                {
                    ClearAsioBlock(request.buffer_index);
                    presented_clock_->PublishContinuityAnchor(
                        logical.presented_output_frame,
                        rendered->submitted_output_tail,
                        logical.system_time_ns);
                    if (!logical_render_sequencer_.Commit(*logical_plan))
                    {
                        logical_render_sequencer_.Abandon(*logical_plan);
                        LatchRuntimeFault(AsioFailureStage::runtime_clock);
                        return;
                    }
                    SaturatingAddCounter(
                        render_gap_frames_,
                        logical_plan->timeline.discontinuity_frames);
                    SaturatingAddCounter(
                        silent_advance_frames_, request_.buffer_frames);
                    CallOutputReady();
                    return;
                }

                const auto index = static_cast<std::size_t>(request.buffer_index);
                const auto conversion = ConvertFloatStereoToAsio(
                    rendered->block.interleaved_stereo,
                    channel_types_,
                    {
                        driver_buffers_[0][index],
                        driver_buffers_[1][index],
                    });
                render_diagnostics_.RecordConversion(rendered->block, conversion);
                if (!conversion.converted)
                {
                    logical_render_sequencer_.Abandon(*logical_plan);
                    ClearAsioBlock(request.buffer_index);
                    LatchRuntimeFault(AsioFailureStage::conversion);
                    return;
                }
                if (!PublishExactAnchor(
                    *logical_plan,
                    logical.presented_output_frame,
                    false))
                {
                    logical_render_sequencer_.Abandon(*logical_plan);
                    ClearAsioBlock(request.buffer_index);
                    LatchRuntimeFault(AsioFailureStage::runtime_clock);
                    return;
                }
                presented_clock_->Publish(
                    logical,
                    rendered->submitted_output_tail);
                if (!logical_render_sequencer_.Commit(*logical_plan))
                {
                    logical_render_sequencer_.Abandon(*logical_plan);
                    ClearAsioBlock(request.buffer_index);
                    LatchRuntimeFault(AsioFailureStage::runtime_clock);
                    return;
                }
                SaturatingAddCounter(
                    render_gap_frames_,
                    logical_plan->timeline.discontinuity_frames);
                if (!CallOutputReady())
                {
                    return;
                }
                actions_.signal_event(actions_.context, stable_render_event_);
            }

            // ReSharper disable once CppOverrideWithDifferentVisibility
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

            // ReSharper disable once CppOverrideWithDifferentVisibility
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
                const auto stage = static_cast<AsioFailureStage>(
                    first_fault_stage_.load(std::memory_order_acquire));
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
                auto callback = completed_callback_snapshot_;
                if (callback_runtime_ != nullptr)
                {
                    MergeCallbackSnapshot(callback, callback_runtime_->Snapshot());
                }
                const auto exact = exact_clock_ != nullptr
                                       ? exact_clock_->counters()
                                       : has_final_exact_clock_counters_
                                       ? final_exact_clock_counters_
                                       : ExactOutputClockCounters{};
                const auto render = render_diagnostics_.Snapshot();
                return {
                    .callbacks = callback.callbacks,
                    .time_info_callbacks = callback.time_info_callbacks,
                    .legacy_callbacks = callback.legacy_callbacks,
                    .deferred_callbacks = callback.deferred_callbacks,
                    .deadline_misses = callback.deadline_misses,
                    .silence_substitutions = SaturatingSum({
                        render.no_active_voice_silence_blocks,
                        render.active_short_read_blocks,
                        render.mixer_error_blocks,
                        render.render_contract_error_blocks,
                    }),
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
                    .foreground_losses =
                    foreground_losses_.load(std::memory_order_relaxed),
                    .physical_session_losses =
                    physical_session_losses_.load(
                        std::memory_order_relaxed),
                    .physical_session_generation =
                    logical_render_sequencer_.physical_session_generation(),
                    .session_releases =
                    session_releases_.load(std::memory_order_relaxed),
                    .recovery_attempts =
                    recovery_attempts_.load(std::memory_order_relaxed),
                    .recovery_failures =
                    recovery_failures_.load(std::memory_order_relaxed),
                    .session_recoveries =
                    session_recoveries_.load(std::memory_order_relaxed),
                    .silent_advance_frames =
                    silent_advance_frames_.load(std::memory_order_relaxed),
                    .expected_period_ns = callback.expected_period_ns,
                    .callback_interval_samples =
                    callback.callback_interval_samples,
                    .total_callback_interval_ticks =
                    callback.total_callback_interval_ticks,
                    .maximum_callback_interval_ticks =
                    callback.maximum_callback_interval_ticks,
                    .early_callback_intervals =
                    callback.early_callback_intervals,
                    .late_callback_intervals = callback.late_callback_intervals,
                    .severe_callback_intervals =
                    callback.severe_callback_intervals,
                    .timed_callback_work_samples =
                    callback.timed_callback_work_samples,
                    .total_callback_ticks = callback.total_callback_ticks,
                    .maximum_callback_ticks = callback.maximum_callback_ticks,
                    .timed_render_work_samples =
                    callback.timed_render_work_samples,
                    .total_render_ticks = callback.total_render_ticks,
                    .maximum_render_ticks = callback.maximum_render_ticks,
                    .driver_interval_samples = callback.driver_interval_samples,
                    .maximum_driver_period_error_ns =
                    callback.maximum_driver_period_error_ns,
                    .maximum_host_driver_interval_skew_ns =
                    callback.maximum_host_driver_interval_skew_ns,
                    .buffer_alternation_violations =
                    callback.buffer_alternation_violations,
                    .no_active_voice_silence_blocks =
                    render.no_active_voice_silence_blocks,
                    .active_short_read_blocks = render.active_short_read_blocks,
                    .mixer_error_blocks = render.mixer_error_blocks,
                    .render_contract_error_blocks =
                    render.render_contract_error_blocks,
                    .short_read_missing_frames =
                    render.short_read_missing_frames,
                    .first_mixer_error = render.first_mixer_error,
                    .clipped_output_blocks = render.clipped_output_blocks,
                    .clipped_output_samples = render.clipped_output_samples,
                    .zero_output_blocks_with_active_voice =
                    render.zero_output_blocks_with_active_voice,
                    .zero_output_blocks_without_active_voice =
                    render.zero_output_blocks_without_active_voice,
                    .non_finite_output_blocks =
                    render.non_finite_output_blocks,
                    .maximum_absolute_output_sample =
                    render.maximum_absolute_output_sample,
                    .qpc_frequency = callback.qpc_frequency,
                    .exact_anchor_publications = exact.publication_count,
                    .detached_exact_anchor_publications =
                    detached_exact_anchor_publications_.load(
                        std::memory_order_relaxed),
                    .exact_resolved_queries = exact.resolved_queries,
                    .exact_pending_queries = exact.pending_queries,
                    .exact_temporarily_unavailable_queries =
                    exact.temporarily_unavailable_queries,
                    .exact_history_lost_queries = exact.history_lost_queries,
                    .exact_discontinuous_queries = exact.discontinuous_queries,
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
            std::unique_ptr<IAsioRegistrySource> registry_;
            std::unique_ptr<IAsioDriverFactory> factory_;
            std::shared_ptr<IAsioOutputObserver> observer_;
            std::shared_ptr<const ma_allocation_callbacks> mixer_allocations_;
            DWORD startup_clock_timeout_ms_{};
            bool enable_absolute_time_judgement_{};
            AsioOutputBackendActions actions_{};
            bool timer_period_active_{};

            std::thread control_thread_;
            HANDLE startup_event_{};
            HANDLE stable_render_event_{};
            HANDLE fault_event_{};
            HANDLE shutdown_event_{};
            AsioFailure startup_failure_{};
            std::atomic_bool startup_succeeded_{};
            std::atomic_bool committed_{};
            std::atomic_bool final_stopping_{};
            std::atomic_bool render_ready_{};

            std::unique_ptr<AsioForegroundMonitor> foreground_monitor_;
            std::unique_ptr<AsioSession> session_;
            std::unique_ptr<AsioCallbackRuntime> callback_runtime_;
            std::unique_ptr<AudioRenderCore> render_core_;
            std::shared_ptr<ExactAsioClock> exact_clock_;
            ExactOutputClockCounters final_exact_clock_counters_{};
            bool has_final_exact_clock_counters_{};
            bool exact_clock_registered_{};
            std::uint64_t exact_endpoint_generation_{};
            std::uint64_t exact_anchor_sequence_{};
            AsioPresentedClockPublication* presented_clock_{};
            AsioClockTracker clock_tracker_;
            AsioLogicalRenderSequencer logical_render_sequencer_;
            std::array<ASIOSampleType, 2> channel_types_{};
            std::array<ASIOSampleType, 2> logical_channel_types_{};
            std::array<std::array<std::span<std::byte>, 2>, 2> driver_buffers_{};
            AsioCallbackRuntimeSnapshot completed_callback_snapshot_{};

            std::atomic_uint32_t endpoint_buffer_frames_{};
            std::atomic_uint32_t output_sample_rate_{};
            AsioRenderDiagnostics render_diagnostics_;
            std::atomic_uint64_t sample_position_discontinuities_{};
            std::atomic_uint64_t render_gap_frames_{};
            std::atomic_uint64_t foreground_losses_{};
            std::atomic_uint64_t physical_session_losses_{};
            std::atomic_uint64_t session_releases_{};
            std::atomic_uint64_t recovery_attempts_{};
            std::atomic_uint64_t recovery_failures_{};
            std::atomic_uint64_t session_recoveries_{};
            std::atomic_uint64_t silent_advance_frames_{};
            std::atomic_uint64_t detached_exact_anchor_publications_{};
            std::atomic_uint64_t pending_cursor_queries_{};
            std::atomic_uint64_t unmapped_cursor_failures_{};
            std::atomic_bool first_fault_claimed_{};
            std::atomic<std::uint8_t> first_fault_stage_{};
            std::atomic<std::uint8_t> first_fault_domain_{};
            std::atomic<std::int64_t> first_fault_result_{};
            std::atomic_uint64_t active_physical_session_generation_{};
            std::uint32_t logical_output_latency_frames_{};
            bool physical_contract_established_{};
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
            bool enable_absolute_time_judgement,
            const AsioOutputBackendActions& actions,
            AsioFailure* failure) noexcept
        {
            if (failure != nullptr)
            {
                *failure = {};
            }
            if (!ActionsComplete(actions, enable_absolute_time_judgement))
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
                    enable_absolute_time_judgement,
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
        bool enable_absolute_time_judgement,
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
            enable_absolute_time_judgement,
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

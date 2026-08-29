// SPDX-License-Identifier: CC0-1.0

#include "Audio/Asio/AsioOutputBackend.h"
#include "Audio/Asio/AsioOutputBackendInternal.h"

#include "Audio/Asio/AsioCallbackRuntime.h"
#include "Audio/Asio/AsioClock.h"
#include "Audio/Asio/AsioForegroundMonitor.h"
#include "Audio/Asio/AsioLogicalRenderSequencer.h"
#include "Audio/Asio/AsioSubmittedOutputTail.h"
#include "Audio/Asio/AsioSampleConverter.h"
#include "Audio/Asio/AsioSession.h"
#include "Audio/ExactJudgementTimeline.h"
#include "Audio/Logical/LogicalPresentationClock.h"
#include "Audio/Logical/LogicalPresentedOutputClock.h"
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

            constexpr std::array<DWORD, 2> kRecoveryRetryDelaysMs{1'000, 2'000};
            constexpr std::uint64_t kMaximumRecoveryAttempts = 3;

            enum class PhysicalSessionPurpose : std::uint8_t
            {
                InitialStartup,
                FocusRecovery,
            };

            struct AsioLogicalOutputContract final
            {
                AsioDriverRegistration registration;
                std::uint32_t sample_rate{};
                std::uint32_t period_frames{};
                std::uint32_t output_base_channel{};
                std::array<ASIOSampleType, 2> channel_types{};
                std::uint32_t output_latency_frames{};
                bool output_ready_supported{};
            };

            bool SameDriverRegistration(
                const AsioDriverRegistration& left,
                const AsioDriverRegistration& right) noexcept
            {
                return left.registry_name == right.registry_name &&
                    InlineIsEqualGUID(left.clsid, right.clsid) != FALSE;
            }

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

            void MaximumCounter(
                std::atomic_uint64_t& destination,
                const std::uint64_t value) noexcept
            {
                auto observed = destination.load(std::memory_order_relaxed);
                while (observed < value &&
                    !destination.compare_exchange_weak(
                        observed,
                        value,
                        std::memory_order_relaxed,
                        std::memory_order_relaxed))
                {
                }
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
                if (enable_absolute_time_judgement_ &&
                    usage == VoiceUsage::GameplayNativeCandidate)
                {
                    const auto buffer_instance_id = timeline != nullptr
                                                        ? timeline->exact_buffer_instance_id()
                                                        : 0;
                    const auto timeline_generation = logical_clock_ != nullptr
                                                         ? logical_clock_->info().timeline_generation
                                                         : 0;
                    if (logical_clock_ == nullptr || timeline == nullptr ||
                        buffer_instance_id == 0 || timeline_generation == 0 ||
                        !timeline->ConfigureExactPlaybackHistory(
                            buffer_instance_id, timeline_generation))
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

            enum class LifecycleState : std::uint8_t
            {
                starting,
                running,
                suspending,
                suspended,
                recovering,
                fatal,
                stopping,
            };

            enum class RuntimeWake : std::uint8_t
            {
                fault,
                shutdown,
                foreground_change,
                message,
                timeout,
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

            struct PhysicalSessionFacts final
            {
                AsioPhysicalSessionReason reason{};
                double observed_sample_rate{};
                double active_sample_rate{};
                bool frozen_rate_requested{};
                bool sample_rate_changed{};
            };

            struct ClosedPhysicalSessionFacts final
            {
                PhysicalSessionFacts session{};
                bool callback_quiesced{};
                bool buffers_disposed{};
                bool restoration_attempted{};
                bool restoration_succeeded{};
                bool available{};
            };

            static PhysicalPreparationFailure PreStartPreparationFailure(
                const PhysicalSessionPurpose purpose,
                AsioFailure failure,
                const bool cleanup_complete = true) noexcept
            {
                return {
                    .kind = purpose == PhysicalSessionPurpose::FocusRecovery &&
                            cleanup_complete
                                ? PhysicalPreparationFailureKind::
                                retryable_before_start
                                : PhysicalPreparationFailureKind::fatal,
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
                observer_->StartupSucceeded(
                    session_->report(),
                    {
                        .origin_raw_ms = logical_clock_->origin_raw_ms(),
                        .origin_unwrapped_ms = 0,
                        .origin_presented_frame = 0,
                        .timeline_generation = logical_timeline_generation_,
                        .sample_rate = logical_contract_.sample_rate,
                        .period_frames = logical_contract_.period_frames,
                        .output_latency_frames =
                        logical_contract_.output_latency_frames,
                        .alternate_backend_selected = false,
                    });
                const auto startup_focus = foreground_monitor_->snapshot();
                const auto physical_generation =
                    active_physical_session_generation_.load(
                        std::memory_order_acquire);
                ReportLifecycle(
                    AsioSessionLifecycleEvent::physical_session_started,
                    startup_focus,
                    physical_generation,
                    0,
                    0,
                    handoff_logical_render_origin_.load(
                        std::memory_order_relaxed),
                    handoff_physical_render_origin_.load(
                        std::memory_order_relaxed),
                    nullptr,
                    &active_physical_session_facts_);
                startup_succeeded_.store(true, std::memory_order_release);
                actions_.signal_event(actions_.context, startup_event_);

                auto runtime_failure = MonitorCommittedRuntime();
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

                    auto prepared = PreparePhysicalSession(
                        PhysicalSessionPurpose::InitialStartup);
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
                    output_sample_rate_.store(
                        logical_output_sample_rate_,
                        std::memory_order_release);
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
            PreparePhysicalSession(
                const PhysicalSessionPurpose purpose) noexcept
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
                        return std::unexpected(PreStartPreparationFailure(
                            purpose,
                            std::move(registration.error())));
                    }
                    auto driver = factory_->Create(registration->clsid);
                    if (!driver)
                    {
                        return std::unexpected(PreStartPreparationFailure(
                            purpose,
                            std::move(driver.error())));
                    }

                    AsioSampleRatePolicy sample_rate_policy{
                        AsioAdoptCurrentRate{}
                    };
                    if (purpose == PhysicalSessionPurpose::FocusRecovery)
                    {
                        if (logical_output_sample_rate_ == 0)
                        {
                            return std::unexpected(FatalPreparationFailure(
                                Failure(
                                    AsioFailureStage::protocol,
                                    "ASIO recovery requires a frozen logical sample rate")));
                        }
                        sample_rate_policy = AsioRequireFrozenRate{
                            logical_output_sample_rate_
                        };
                    }

                    auto prepared = AsioSession::Prepare(
                        std::move(*registration),
                        std::move(*driver),
                        request_,
                        game_window_,
                        AsioProbeMode::validate,
                        sample_rate_policy);
                    if (!prepared)
                    {
                        auto preparation_failure =
                            std::move(prepared.error());
                        return std::unexpected(PreStartPreparationFailure(
                            purpose,
                            std::move(preparation_failure.failure),
                            preparation_failure.cleanup_complete));
                    }
                    session_ = std::move(*prepared);

                    const double reported_sample_rate =
                        session_->report().sample_rate;
                    if (!std::isfinite(reported_sample_rate) ||
                        reported_sample_rate <= 0.0 ||
                        std::trunc(reported_sample_rate) !=
                        reported_sample_rate ||
                        reported_sample_rate > static_cast<double>(
                            (std::numeric_limits<
                                std::uint32_t>::max)()))
                    {
                        return std::unexpected(PreStartPreparationFailure(
                            purpose,
                            Failure(
                                AsioFailureStage::sample_rate,
                                "Prepared ASIO session has an invalid whole-Hz sample rate")));
                    }
                    const auto session_sample_rate =
                        static_cast<std::uint32_t>(reported_sample_rate);
                    active_physical_session_facts_ = {
                        .reason = purpose == PhysicalSessionPurpose::InitialStartup
                                      ? AsioPhysicalSessionReason::startup
                                      : AsioPhysicalSessionReason::focus_recovery,
                        .observed_sample_rate =
                        session_->report().original_sample_rate,
                        .active_sample_rate = reported_sample_rate,
                        .frozen_rate_requested =
                        purpose == PhysicalSessionPurpose::FocusRecovery,
                        .sample_rate_changed =
                        session_->report().original_sample_rate !=
                        reported_sample_rate,
                    };

                    if (purpose == PhysicalSessionPurpose::InitialStartup)
                    {
                        if (logical_output_sample_rate_ != 0 ||
                            render_core_ != nullptr ||
                            logical_render_sequencer_ != nullptr ||
                            logical_clock_ != nullptr ||
                            submitted_tail_ != nullptr ||
                            logical_timeline_generation_ != 0)
                        {
                            return std::unexpected(FatalPreparationFailure(
                                Failure(
                                    AsioFailureStage::protocol,
                                    "ASIO logical rendering may be initialized only once")));
                        }
                        logical_output_sample_rate_ = session_sample_rate;
                        std::uint64_t qpc_frequency{};
                        const auto& clock_actions =
                            actions_.callback_runtime_actions;
                        if (clock_actions.query_performance_frequency == nullptr ||
                            !clock_actions.query_performance_frequency(
                                clock_actions.context, &qpc_frequency) ||
                            qpc_frequency == 0 ||
                            qpc_frequency > static_cast<std::uint64_t>(
                                (std::numeric_limits<std::int64_t>::max)()))
                        {
                            return std::unexpected(FatalPreparationFailure(
                                Failure(
                                    AsioFailureStage::startup_clock,
                                    "ASIO logical clock QPC frequency is invalid")));
                        }
                        const auto timeline_generation =
                            detail::NextExactJudgementTimelineGeneration();
                        if (timeline_generation == 0)
                        {
                            return std::unexpected(FatalPreparationFailure(
                                Failure(
                                    AsioFailureStage::startup_clock,
                                    "ASIO logical timeline generation is invalid")));
                        }
                        logical_clock_ = LogicalPresentationClock::Create(
                            timeline_generation,
                            actions_.time_get_time_ms(actions_.context),
                            logical_output_sample_rate_,
                            static_cast<std::int64_t>(qpc_frequency));
                        submitted_tail_ =
                            std::make_shared<AsioSubmittedOutputTail>();
                        if (logical_clock_ == nullptr ||
                            submitted_tail_ == nullptr)
                        {
                            return std::unexpected(FatalPreparationFailure(
                                Failure(
                                    AsioFailureStage::render_core,
                                    "Could not allocate the persistent ASIO logical clock")));
                        }
                        logical_timeline_generation_ = timeline_generation;

                        auto clock =
                            std::make_unique<LogicalPresentedOutputClock>(
                                LogicalPresentedOutputClockActions{
                                    actions_.context,
                                    actions_.time_get_time_ms,
                                },
                                logical_clock_);
                        ma_result mixer_result = MA_ERROR;
                        render_core_ = AudioRenderCore::Create(
                            request_.buffer_frames,
                            logical_output_sample_rate_,
                            std::move(mixer_allocations_),
                            std::move(clock),
                            &mixer_result);
                        if (render_core_ == nullptr)
                        {
                            return std::unexpected(FatalPreparationFailure(
                                Failure(
                                    AsioFailureStage::render_core,
                                    "Could not create the preallocated ASIO render core",
                                    AsioResultDomain::none,
                                    mixer_result)));
                        }
                        if (enable_absolute_time_judgement_ &&
                            !detail::RegisterExactJudgementTimeline(
                                logical_clock_))
                        {
                            return std::unexpected(FatalPreparationFailure(
                                Failure(
                                    AsioFailureStage::startup_clock,
                                    "Could not register the ASIO logical judgement timeline")));
                        }
                        exact_clock_registered_ =
                            enable_absolute_time_judgement_;
                    }
                    else if (session_sample_rate !=
                        logical_output_sample_rate_ ||
                        render_core_ == nullptr ||
                        logical_render_sequencer_ == nullptr ||
                        logical_clock_ == nullptr ||
                        submitted_tail_ == nullptr)
                    {
                        return std::unexpected(FatalPreparationFailure(Failure(
                            AsioFailureStage::protocol,
                            "ASIO recovery did not preserve initialized logical rendering")));
                    }

                    AsioLegacyPositionActions legacy{
                        this,
                        &AsioOutputBackendState::ReadLegacyPosition,
                    };
                    auto callbacks = AsioCallbackRuntime::Prepare(
                        *this,
                        legacy,
                        {
                            request_.buffer_frames,
                            logical_output_sample_rate_
                        },
                        actions_.callback_runtime_actions);
                    if (!callbacks)
                    {
                        return std::unexpected(PreStartPreparationFailure(
                            purpose,
                            std::move(callbacks.error())));
                    }
                    callback_runtime_ = std::move(*callbacks);
                    if (auto installed = callback_runtime_->Install(); !installed)
                    {
                        return std::unexpected(PreStartPreparationFailure(
                            purpose,
                            std::move(installed.error())));
                    }
                    if (auto buffers = session_->CreateOutputBuffers(
                            AsioCallbackRuntime::Callbacks());
                        !buffers)
                    {
                        return std::unexpected(PreStartPreparationFailure(
                            purpose,
                            std::move(buffers.error())));
                    }
                    if (auto views = ConfigureDriverBuffers(); !views)
                    {
                        return std::unexpected(PreStartPreparationFailure(
                            purpose,
                            std::move(views.error())));
                    }
                    if (auto contract = EstablishOrValidatePhysicalContract();
                        !contract)
                    {
                        return std::unexpected(PreStartPreparationFailure(
                            purpose,
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
                    if (session_ == nullptr || callback_runtime_ == nullptr ||
                        logical_render_sequencer_ == nullptr)
                    {
                        return std::unexpected(Failure(
                            AsioFailureStage::protocol,
                            "ASIO Start requires a prepared physical session"));
                    }

                    handoff_logical_render_origin_.store(
                        0, std::memory_order_relaxed);
                    handoff_physical_render_origin_.store(
                        0, std::memory_order_relaxed);
                    handoff_raw_sample_origin_.store(
                        0, std::memory_order_relaxed);
                    handoff_attachment_disposition_.store(
                        0, std::memory_order_relaxed);
                    handoff_attachment_interval_frames_.store(
                        0, std::memory_order_relaxed);
                    physical_silent_priming_callbacks_.store(
                        0, std::memory_order_relaxed);
                    handoff_physical_session_generation_.store(
                        0, std::memory_order_release);
                    const auto physical_generation =
                        logical_render_sequencer_->BeginPhysicalSession();
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
                    physical_stability_proof_callbacks_.store(
                        0, std::memory_order_relaxed);
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
                    if (!SameDriverRegistration(
                            report.registration,
                            logical_contract_.registration) ||
                        report.sample_rate != static_cast<double>(
                            logical_contract_.sample_rate) ||
                        report.effective_buffer_frames !=
                        logical_contract_.period_frames ||
                        report.selected_base_channel !=
                        logical_contract_.output_base_channel ||
                        report.output_latency_frames !=
                        logical_contract_.output_latency_frames ||
                        report.output_ready_supported !=
                        logical_contract_.output_ready_supported ||
                        channel_types_ != logical_contract_.channel_types)
                    {
                        return std::unexpected(Failure(
                            AsioFailureStage::protocol,
                            "Reacquired ASIO session changed the logical endpoint contract"));
                    }
                    return {};
                }

                logical_output_latency_frames_ = report.output_latency_frames;
                logical_contract_ = {
                    .registration = report.registration,
                    .sample_rate = logical_output_sample_rate_,
                    .period_frames = report.effective_buffer_frames,
                    .output_base_channel = report.selected_base_channel,
                    .channel_types = channel_types_,
                    .output_latency_frames = report.output_latency_frames,
                    .output_ready_supported = report.output_ready_supported,
                };
                if (logical_render_sequencer_ != nullptr)
                {
                    return std::unexpected(Failure(
                        AsioFailureStage::protocol,
                        "ASIO logical render sequencer may be created only once"));
                }
                logical_render_sequencer_ =
                    std::make_unique<AsioLogicalRenderSequencer>(
                        logical_contract_.period_frames);
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

            std::expected<RuntimeWake, AsioFailure> WaitForRuntimeWake(
                const DWORD timeout_ms) const noexcept
            {
                const std::array<HANDLE, 3> handles{
                    fault_event_,
                    shutdown_event_,
                    foreground_monitor_->change_event(),
                };
                const DWORD wait = actions_.message_wait(
                    actions_.context,
                    handles,
                    timeout_ms);
                if (wait == WAIT_OBJECT_0)
                {
                    return RuntimeWake::fault;
                }
                if (wait == WAIT_OBJECT_0 + 1)
                {
                    return RuntimeWake::shutdown;
                }
                if (wait == WAIT_OBJECT_0 + 2)
                {
                    return RuntimeWake::foreground_change;
                }
                if (wait == WAIT_OBJECT_0 + handles.size())
                {
                    actions_.drain_messages(actions_.context);
                    return RuntimeWake::message;
                }
                if (wait == WAIT_TIMEOUT)
                {
                    return RuntimeWake::timeout;
                }
                return std::unexpected(Failure(
                    AsioFailureStage::callback,
                    "ASIO runtime message wait failed",
                    AsioResultDomain::win32,
                    wait == WAIT_FAILED ? GetLastError() : wait));
            }

            void ReportLifecycle(
                const AsioSessionLifecycleEvent event,
                const AsioForegroundSnapshot& focus,
                const std::uint64_t physical_session_generation,
                const std::uint64_t recovery_attempt,
                const std::uint32_t retry_delay_ms,
                const std::uint64_t logical_render_origin,
                const std::uint64_t physical_render_origin,
                const AsioFailure* failure,
                const PhysicalSessionFacts* session_facts = nullptr,
                const ClosedPhysicalSessionFacts* closed_facts = nullptr) const noexcept
            {
                AsioSessionLifecycleRecord record{
                    .event = event,
                    .foreground = focus.is_foreground,
                    .focus_loss_generation = focus.loss_generation,
                    .physical_session_generation = physical_session_generation,
                    .recovery_attempt = recovery_attempt,
                    .retry_delay_ms = retry_delay_ms,
                    .logical_render_origin = logical_render_origin,
                    .physical_render_origin = physical_render_origin,
                };
                const auto apply_session_facts = [&record](
                    const PhysicalSessionFacts& facts) noexcept
                {
                    record.reason = facts.reason;
                    record.observed_sample_rate = facts.observed_sample_rate;
                    record.active_sample_rate = facts.active_sample_rate;
                    record.frozen_rate_requested =
                        facts.frozen_rate_requested;
                    record.sample_rate_changed = facts.sample_rate_changed;
                };
                if (session_facts != nullptr)
                {
                    apply_session_facts(*session_facts);
                }
                if (closed_facts != nullptr && closed_facts->available)
                {
                    apply_session_facts(closed_facts->session);
                    record.callback_quiesced =
                        closed_facts->callback_quiesced;
                    record.buffers_disposed = closed_facts->buffers_disposed;
                    record.restoration_attempted =
                        closed_facts->restoration_attempted;
                    record.restoration_succeeded =
                        closed_facts->restoration_succeeded;
                }
                if (event == AsioSessionLifecycleEvent::physical_session_started ||
                    event == AsioSessionLifecycleEvent::session_recovered)
                {
                    record.raw_sample_origin = handoff_raw_sample_origin_.load(
                        std::memory_order_relaxed);
                    record.attachment_disposition =
                        static_cast<AsioPhysicalAttachmentDisposition>(
                            handoff_attachment_disposition_.load(
                                std::memory_order_relaxed));
                    record.attachment_interval_frames =
                        handoff_attachment_interval_frames_.load(
                            std::memory_order_relaxed);
                    record.silent_priming_callbacks =
                        physical_silent_priming_callbacks_.load(
                            std::memory_order_relaxed);
                }
                observer_->SessionLifecycleChanged(record, failure);
            }

            static std::optional<AsioFailure> ValidateFocusSnapshot(
                const AsioForegroundSnapshot& focus,
                const std::uint64_t consumed_generation) noexcept
            {
                if (focus.loss_generation >= consumed_generation)
                {
                    return std::nullopt;
                }
                return Failure(
                    AsioFailureStage::foreground_monitor,
                    "ASIO foreground loss generation regressed");
            }

            void ConsumeFocusLossGeneration(
                const AsioForegroundSnapshot& focus,
                std::uint64_t& consumed_generation) noexcept
            {
                if (focus.loss_generation <= consumed_generation)
                {
                    return;
                }
                SaturatingAddCounter(
                    foreground_losses_,
                    focus.loss_generation - consumed_generation);
                consumed_generation = focus.loss_generation;
                consumed_focus_loss_generation_.store(
                    consumed_generation,
                    std::memory_order_release);
            }

            void PublishRuntimeSummaryIfDue(
                std::uint64_t& summary_started_ms) const noexcept
            {
                const auto now_ms = actions_.tick_count_ms(actions_.context);
                if (now_ms - summary_started_ms < actions_.summary_interval_ms)
                {
                    return;
                }
                observer_->RuntimeSummary(SnapshotCounters());
                summary_started_ms = now_ms;
            }

            std::optional<AsioFailure> MonitorCommittedRuntime() noexcept
            {
                LifecycleState state = LifecycleState::starting;
                std::optional<AsioFailure> fatal_failure;
                std::uint64_t consumed_focus_loss_generation{};
                std::uint64_t recovery_attempt{};
                std::uint32_t recovery_retry_delay_ms{};
                std::uint64_t recovery_retry_started_ms{};
                std::uint64_t summary_started_ms =
                    actions_.tick_count_ms(actions_.context);
                AsioForegroundSnapshot suspension_focus{};
                bool suspended_loss_pending{};

                const auto fail = [&](AsioFailure failure) noexcept
                {
                    fatal_failure = std::move(failure);
                    state = LifecycleState::fatal;
                };
                const auto foreground_monitor_failure =
                    [&]() -> std::optional<AsioFailure>
                {
                    if (foreground_monitor_->healthy())
                    {
                        return std::nullopt;
                    }
                    return Failure(
                        AsioFailureStage::foreground_monitor,
                        "ASIO foreground monitor stopped unexpectedly",
                        AsioResultDomain::win32,
                        foreground_monitor_->failure_code());
                };
                const auto close_after_failure =
                    [&](AsioFailure failure) noexcept
                {
                    const auto close_failure = ClosePhysicalSession();
                    if (HasPublishedFault())
                    {
                        const auto latched = BuildLatchedFailure();
                        if (latched.stage != failure.stage ||
                            latched.domain != failure.domain ||
                            latched.result != failure.result)
                        {
                            AppendSecondaryFailure(failure, latched);
                        }
                    }
                    if (close_failure)
                    {
                        AppendSecondaryFailure(failure, *close_failure);
                    }
                    return failure;
                };

                for (;;)
                {
                    // Maintain the persistent logical coordinate independently of
                    // physical-session state. This advances time; it never infers
                    // foreground ownership or recovery state.
                    if (state != LifecycleState::fatal &&
                        state != LifecycleState::stopping)
                    {
                        if (logical_clock_ == nullptr)
                        {
                            fail(Failure(
                                AsioFailureStage::runtime_clock,
                                "ASIO logical timeline is unavailable"));
                        }
                        else if (const auto advanced =
                                logical_clock_->ObserveNow(
                                    actions_.time_get_time_ms(
                                        actions_.context));
                            !advanced)
                        {
                            fail(Failure(
                                AsioFailureStage::runtime_clock,
                                std::format(
                                    "ASIO logical timeline advance failed: {}",
                                    static_cast<unsigned>(advanced.error()))));
                        }
                    }
                    switch (state)
                    {
                    case LifecycleState::starting:
                        {
                            if (const auto failure = foreground_monitor_failure())
                            {
                                fail(*failure);
                                break;
                            }
                            if (HasPublishedFault())
                            {
                                fail(BuildLatchedFailure());
                                break;
                            }
                            const auto focus = foreground_monitor_->snapshot();
                            if (const auto failure = ValidateFocusSnapshot(
                                focus,
                                consumed_focus_loss_generation))
                            {
                                fail(*failure);
                                break;
                            }
                            if (!focus.is_foreground ||
                                focus.loss_generation >
                                consumed_focus_loss_generation)
                            {
                                suspension_focus = focus;
                                state = LifecycleState::suspending;
                            }
                            else
                            {
                                state = LifecycleState::running;
                            }
                            break;
                        }

                    case LifecycleState::running:
                        {
                            const auto now_ms =
                                actions_.tick_count_ms(actions_.context);
                            const auto summary_remaining = RemainingTimeout(
                                summary_started_ms,
                                now_ms,
                                actions_.summary_interval_ms);
                            auto wake = WaitForRuntimeWake(summary_remaining);
                            if (!wake)
                            {
                                fail(std::move(wake.error()));
                                break;
                            }
                            if (*wake == RuntimeWake::shutdown)
                            {
                                state = LifecycleState::stopping;
                                break;
                            }
                            if (*wake == RuntimeWake::fault ||
                                HasPublishedFault())
                            {
                                fail(BuildLatchedFailure());
                                break;
                            }
                            if (const auto failure = foreground_monitor_failure())
                            {
                                fail(*failure);
                                break;
                            }

                            const auto focus = foreground_monitor_->snapshot();
                            if (const auto failure = ValidateFocusSnapshot(
                                focus,
                                consumed_focus_loss_generation))
                            {
                                fail(*failure);
                                break;
                            }
                            if (!focus.is_foreground ||
                                focus.loss_generation >
                                consumed_focus_loss_generation)
                            {
                                suspension_focus = focus;
                                state = LifecycleState::suspending;
                            }
                            break;
                        }

                    case LifecycleState::suspending:
                        {
                            if (const auto failure = foreground_monitor_failure())
                            {
                                fail(*failure);
                                break;
                            }
                            if (const auto failure = ValidateFocusSnapshot(
                                suspension_focus,
                                consumed_focus_loss_generation))
                            {
                                fail(*failure);
                                break;
                            }
                            ConsumeFocusLossGeneration(
                                suspension_focus,
                                consumed_focus_loss_generation);
                            const auto physical_generation =
                                active_physical_session_generation_.load(
                                    std::memory_order_acquire);
                            ReportLifecycle(
                                AsioSessionLifecycleEvent::foreground_lost,
                                suspension_focus,
                                physical_generation,
                                recovery_attempt,
                                0,
                                0,
                                0,
                                nullptr);

                            const auto close_failure = ClosePhysicalSession();
                            if (HasPublishedFault())
                            {
                                auto failure = BuildLatchedFailure();
                                if (close_failure)
                                {
                                    AppendSecondaryFailure(
                                        failure,
                                        *close_failure);
                                }
                                fail(std::move(failure));
                                break;
                            }
                            if (close_failure)
                            {
                                fail(*close_failure);
                                break;
                            }

                            SaturatingIncrementCounter(session_releases_);
                            ReportLifecycle(
                                AsioSessionLifecycleEvent::session_released,
                                suspension_focus,
                                physical_generation,
                                recovery_attempt,
                                0,
                                0,
                                0,
                                nullptr,
                                nullptr,
                                &last_closed_physical_session_facts_);
                            ClearSessionFault();
                            state = LifecycleState::suspended;
                            break;
                        }

                    case LifecycleState::suspended:
                        {
                            if (const auto failure = foreground_monitor_failure())
                            {
                                fail(*failure);
                                break;
                            }
                            if (HasPublishedFault())
                            {
                                fail(BuildLatchedFailure());
                                break;
                            }

                            if (suspended_loss_pending)
                            {
                                if (const auto failure = ValidateFocusSnapshot(
                                    suspension_focus,
                                    consumed_focus_loss_generation))
                                {
                                    fail(*failure);
                                    break;
                                }
                                ConsumeFocusLossGeneration(
                                    suspension_focus,
                                    consumed_focus_loss_generation);
                                ReportLifecycle(
                                    AsioSessionLifecycleEvent::foreground_lost,
                                    suspension_focus,
                                    0,
                                    recovery_attempt,
                                    0,
                                    0,
                                    0,
                                    nullptr);
                                suspended_loss_pending = false;
                            }

                            const auto focus = foreground_monitor_->snapshot();
                            if (const auto failure = ValidateFocusSnapshot(
                                focus,
                                consumed_focus_loss_generation))
                            {
                                fail(*failure);
                                break;
                            }
                            if (focus.loss_generation >
                                consumed_focus_loss_generation)
                            {
                                ConsumeFocusLossGeneration(
                                    focus,
                                    consumed_focus_loss_generation);
                                ReportLifecycle(
                                    AsioSessionLifecycleEvent::foreground_lost,
                                    focus,
                                    0,
                                    recovery_attempt,
                                    0,
                                    0,
                                    0,
                                    nullptr);
                            }
                            if (focus.is_foreground)
                            {
                                recovery_attempt = 0;
                                recovery_retry_delay_ms = 0;
                                ReportLifecycle(
                                    AsioSessionLifecycleEvent::foreground_regained,
                                    focus,
                                    0,
                                    recovery_attempt,
                                    0,
                                    0,
                                    0,
                                    nullptr);
                                state = LifecycleState::recovering;
                                break;
                            }

                            if (const auto failure = AdvanceSilentRendering())
                            {
                                fail(*failure);
                                break;
                            }
                            const auto now_ms =
                                actions_.tick_count_ms(actions_.context);
                            const auto summary_remaining = RemainingTimeout(
                                summary_started_ms,
                                now_ms,
                                actions_.summary_interval_ms);
                            const auto wait_timeout = (std::min)(
                                summary_remaining,
                                SilentPollIntervalMs());
                            auto wake = WaitForRuntimeWake(wait_timeout);
                            if (!wake)
                            {
                                fail(std::move(wake.error()));
                                break;
                            }
                            if (*wake == RuntimeWake::shutdown)
                            {
                                state = LifecycleState::stopping;
                                break;
                            }
                            if (*wake == RuntimeWake::fault ||
                                HasPublishedFault())
                            {
                                fail(BuildLatchedFailure());
                            }
                            break;
                        }

                    case LifecycleState::recovering:
                        {
                            auto immediate_wake = WaitForRuntimeWake(0);
                            if (!immediate_wake)
                            {
                                fail(std::move(immediate_wake.error()));
                                break;
                            }
                            if (*immediate_wake == RuntimeWake::shutdown)
                            {
                                state = LifecycleState::stopping;
                                break;
                            }
                            if (*immediate_wake == RuntimeWake::fault ||
                                HasPublishedFault())
                            {
                                fail(BuildLatchedFailure());
                                break;
                            }
                            if (const auto failure = foreground_monitor_failure())
                            {
                                fail(*failure);
                                break;
                            }

                            auto focus = foreground_monitor_->snapshot();
                            if (const auto failure = ValidateFocusSnapshot(
                                focus,
                                consumed_focus_loss_generation))
                            {
                                fail(*failure);
                                break;
                            }
                            if (!focus.is_foreground ||
                                focus.loss_generation >
                                consumed_focus_loss_generation)
                            {
                                suspension_focus = focus;
                                suspended_loss_pending = true;
                                state = LifecycleState::suspended;
                                break;
                            }

                            if (const auto failure = AdvanceSilentRendering())
                            {
                                fail(*failure);
                                break;
                            }

                            if (recovery_retry_delay_ms != 0)
                            {
                                const auto now_ms =
                                    actions_.tick_count_ms(actions_.context);
                                const auto retry_remaining = RemainingTimeout(
                                    recovery_retry_started_ms,
                                    now_ms,
                                    recovery_retry_delay_ms);
                                if (retry_remaining == 0)
                                {
                                    recovery_retry_delay_ms = 0;
                                    break;
                                }
                                const auto summary_remaining = RemainingTimeout(
                                    summary_started_ms,
                                    now_ms,
                                    actions_.summary_interval_ms);
                                const auto wait_timeout = (std::min)(
                                    retry_remaining,
                                    (std::min)(
                                        summary_remaining,
                                        SilentPollIntervalMs()));
                                auto wake = WaitForRuntimeWake(wait_timeout);
                                if (!wake)
                                {
                                    fail(std::move(wake.error()));
                                    break;
                                }
                                if (*wake == RuntimeWake::shutdown)
                                {
                                    state = LifecycleState::stopping;
                                    break;
                                }
                                if (*wake == RuntimeWake::fault ||
                                    HasPublishedFault())
                                {
                                    fail(BuildLatchedFailure());
                                }
                                break;
                            }

                            focus = foreground_monitor_->snapshot();
                            if (const auto failure = ValidateFocusSnapshot(
                                focus,
                                consumed_focus_loss_generation))
                            {
                                fail(*failure);
                                break;
                            }
                            if (!focus.is_foreground ||
                                focus.loss_generation >
                                consumed_focus_loss_generation)
                            {
                                suspension_focus = focus;
                                suspended_loss_pending = true;
                                state = LifecycleState::suspended;
                                break;
                            }
                            if (recovery_attempt >= kMaximumRecoveryAttempts)
                            {
                                fail(Failure(
                                    AsioFailureStage::protocol,
                                    "ASIO recovery attempt limit was exceeded"));
                                break;
                            }

                            ++recovery_attempt;
                            SaturatingIncrementCounter(recovery_attempts_);
                            ReportLifecycle(
                                AsioSessionLifecycleEvent::recovery_attempt_started,
                                focus,
                                0,
                                recovery_attempt,
                                0,
                                0,
                                0,
                                nullptr);

                            auto prepared = PreparePhysicalSession(
                                PhysicalSessionPurpose::FocusRecovery);
                            if (!prepared)
                            {
                                auto preparation_failure =
                                    std::move(prepared.error());
                                const bool acquired_resources =
                                    session_ != nullptr ||
                                    callback_runtime_ != nullptr;
                                const auto close_failure =
                                    ClosePhysicalSession();
                                if (HasPublishedFault())
                                {
                                    auto failure = BuildLatchedFailure();
                                    AppendSecondaryFailure(
                                        failure,
                                        preparation_failure.failure);
                                    if (close_failure)
                                    {
                                        AppendSecondaryFailure(
                                            failure,
                                            *close_failure);
                                    }
                                    fail(std::move(failure));
                                    break;
                                }
                                if (close_failure)
                                {
                                    AppendSecondaryFailure(
                                        preparation_failure.failure,
                                        *close_failure);
                                    fail(std::move(
                                        preparation_failure.failure));
                                    break;
                                }
                                ClearSessionFault();

                                if (const auto failure =
                                    foreground_monitor_failure())
                                {
                                    auto monitor_failure = *failure;
                                    AppendSecondaryFailure(
                                        monitor_failure,
                                        preparation_failure.failure);
                                    fail(std::move(monitor_failure));
                                    break;
                                }
                                const auto after_failure_focus =
                                    foreground_monitor_->snapshot();
                                if (const auto failure = ValidateFocusSnapshot(
                                    after_failure_focus,
                                    consumed_focus_loss_generation))
                                {
                                    auto focus_failure = *failure;
                                    AppendSecondaryFailure(
                                        focus_failure,
                                        preparation_failure.failure);
                                    fail(std::move(focus_failure));
                                    break;
                                }

                                if (acquired_resources)
                                {
                                    SaturatingIncrementCounter(session_releases_);
                                    ReportLifecycle(
                                        AsioSessionLifecycleEvent::session_released,
                                        after_failure_focus,
                                        0,
                                        recovery_attempt,
                                        0,
                                        0,
                                        0,
                                        nullptr,
                                        nullptr,
                                        &last_closed_physical_session_facts_);
                                }

                                const bool focus_interrupted =
                                    !after_failure_focus.is_foreground ||
                                    after_failure_focus.loss_generation >
                                    consumed_focus_loss_generation;
                                if (preparation_failure.kind !=
                                    PhysicalPreparationFailureKind::fatal &&
                                    focus_interrupted)
                                {
                                    suspension_focus = after_failure_focus;
                                    suspended_loss_pending = true;
                                    state = LifecycleState::suspended;
                                    break;
                                }

                                SaturatingIncrementCounter(recovery_failures_);
                                std::uint32_t next_retry_delay_ms{};
                                if (preparation_failure.kind ==
                                    PhysicalPreparationFailureKind::
                                    retryable_before_start &&
                                    !focus_interrupted &&
                                    recovery_attempt <
                                    kMaximumRecoveryAttempts)
                                {
                                    next_retry_delay_ms =
                                        kRecoveryRetryDelaysMs[
                                            static_cast<std::size_t>(
                                                recovery_attempt - 1)];
                                }
                                ReportLifecycle(
                                    AsioSessionLifecycleEvent::
                                    recovery_attempt_failed,
                                    after_failure_focus,
                                    0,
                                    recovery_attempt,
                                    next_retry_delay_ms,
                                    0,
                                    0,
                                    &preparation_failure.failure);

                                if (preparation_failure.kind ==
                                    PhysicalPreparationFailureKind::fatal)
                                {
                                    fail(std::move(
                                        preparation_failure.failure));
                                    break;
                                }
                                if (recovery_attempt >=
                                    kMaximumRecoveryAttempts)
                                {
                                    fail(std::move(
                                        preparation_failure.failure));
                                    break;
                                }

                                recovery_retry_delay_ms =
                                    next_retry_delay_ms;
                                recovery_retry_started_ms =
                                    actions_.tick_count_ms(actions_.context);
                                break;
                            }

                            auto prepared_wake = WaitForRuntimeWake(0);
                            if (!prepared_wake)
                            {
                                fail(close_after_failure(
                                    std::move(prepared_wake.error())));
                                break;
                            }
                            if (*prepared_wake == RuntimeWake::shutdown)
                            {
                                state = LifecycleState::stopping;
                                break;
                            }
                            if (*prepared_wake == RuntimeWake::fault ||
                                HasPublishedFault())
                            {
                                fail(close_after_failure(
                                    BuildLatchedFailure()));
                                break;
                            }
                            if (const auto failure =
                                foreground_monitor_failure())
                            {
                                fail(close_after_failure(*failure));
                                break;
                            }
                            if (const auto failure = AdvanceSilentRendering())
                            {
                                fail(close_after_failure(*failure));
                                break;
                            }

                            const auto before_start_focus =
                                foreground_monitor_->snapshot();
                            if (const auto failure = ValidateFocusSnapshot(
                                before_start_focus,
                                consumed_focus_loss_generation))
                            {
                                fail(close_after_failure(*failure));
                                break;
                            }
                            if (!before_start_focus.is_foreground ||
                                before_start_focus.loss_generation >
                                consumed_focus_loss_generation)
                            {
                                const auto close_failure =
                                    ClosePhysicalSession();
                                if (HasPublishedFault())
                                {
                                    auto failure = BuildLatchedFailure();
                                    if (close_failure)
                                    {
                                        AppendSecondaryFailure(
                                            failure,
                                            *close_failure);
                                    }
                                    fail(std::move(failure));
                                    break;
                                }
                                if (close_failure)
                                {
                                    fail(*close_failure);
                                    break;
                                }
                                SaturatingIncrementCounter(session_releases_);
                                ReportLifecycle(
                                    AsioSessionLifecycleEvent::session_released,
                                    before_start_focus,
                                    0,
                                    recovery_attempt,
                                    0,
                                    0,
                                    0,
                                    nullptr,
                                    nullptr,
                                    &last_closed_physical_session_facts_);
                                ClearSessionFault();
                                suspension_focus = before_start_focus;
                                suspended_loss_pending = true;
                                state = LifecycleState::suspended;
                                break;
                            }

                            if (auto started = StartPreparedPhysicalSession();
                                !started)
                            {
                                fail(close_after_failure(
                                    std::move(started.error())));
                                break;
                            }

                            auto stable = WaitForStableRender();
                            if (stable &&
                                *stable == StableRenderOutcome::shutdown)
                            {
                                state = LifecycleState::stopping;
                                break;
                            }
                            if (!stable)
                            {
                                fail(close_after_failure(
                                    std::move(stable.error())));
                                break;
                            }

                            auto post_stability_wake =
                                WaitForRuntimeWake(0);
                            if (!post_stability_wake)
                            {
                                fail(close_after_failure(
                                    std::move(post_stability_wake.error())));
                                break;
                            }
                            if (*post_stability_wake ==
                                RuntimeWake::shutdown)
                            {
                                state = LifecycleState::stopping;
                                break;
                            }
                            if (*post_stability_wake == RuntimeWake::fault ||
                                HasPublishedFault())
                            {
                                fail(close_after_failure(
                                    BuildLatchedFailure()));
                                break;
                            }
                            if (const auto failure =
                                foreground_monitor_failure())
                            {
                                fail(close_after_failure(*failure));
                                break;
                            }

                            const auto physical_generation =
                                active_physical_session_generation_.load(
                                    std::memory_order_acquire);
                            const auto handoff_generation =
                                handoff_physical_session_generation_.load(
                                    std::memory_order_acquire);
                            if (physical_generation == 0 ||
                                handoff_generation != physical_generation)
                            {
                                fail(close_after_failure(Failure(
                                    AsioFailureStage::runtime_clock,
                                    "ASIO recovery reached stability without "
                                    "publishing its physical-to-logical handoff")));
                                break;
                            }
                            const auto logical_render_origin =
                                handoff_logical_render_origin_.load(
                                    std::memory_order_relaxed);
                            const auto physical_render_origin =
                                handoff_physical_render_origin_.load(
                                    std::memory_order_relaxed);

                            const auto post_stability_focus =
                                foreground_monitor_->snapshot();
                            if (const auto failure = ValidateFocusSnapshot(
                                post_stability_focus,
                                consumed_focus_loss_generation))
                            {
                                fail(close_after_failure(*failure));
                                break;
                            }

                            SaturatingIncrementCounter(session_recoveries_);
                            ReportLifecycle(
                                AsioSessionLifecycleEvent::session_recovered,
                                post_stability_focus,
                                physical_generation,
                                recovery_attempt,
                                0,
                                logical_render_origin,
                                physical_render_origin,
                                nullptr,
                                &active_physical_session_facts_);
                            recovery_retry_delay_ms = 0;
                            if (!post_stability_focus.is_foreground ||
                                post_stability_focus.loss_generation >
                                consumed_focus_loss_generation)
                            {
                                suspension_focus = post_stability_focus;
                                state = LifecycleState::suspending;
                            }
                            else
                            {
                                state = LifecycleState::running;
                            }
                            break;
                        }

                    case LifecycleState::fatal:
                        if (fatal_failure)
                        {
                            return std::move(*fatal_failure);
                        }
                        return Failure(
                            AsioFailureStage::protocol,
                            "ASIO lifecycle entered Fatal without a failure");

                    case LifecycleState::stopping:
                        return std::nullopt;
                    }

                    if (state != LifecycleState::fatal &&
                        state != LifecycleState::stopping)
                    {
                        PublishRuntimeSummaryIfDue(summary_started_ms);
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
                last_closed_physical_session_facts_ = {};
                const bool had_physical_resources =
                    session_ != nullptr || callback_runtime_ != nullptr;
                const bool had_session = session_ != nullptr;
                const auto closing_session_facts =
                    active_physical_session_facts_;
                bool callback_quiesced = callback_runtime_ == nullptr;
                AsioSessionCleanupReport cleanup_report{};
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
                    callback_quiesced = true;
                    MergeCallbackSnapshot(
                        completed_callback_snapshot_,
                        callback_runtime_->Snapshot());
                }
                const auto physical_generation =
                    active_physical_session_generation_.exchange(
                        0, std::memory_order_acq_rel);
                if (physical_generation != 0 &&
                    !logical_render_sequencer_->EndPhysicalSession(
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
                    cleanup_report = session_->cleanup_report();
                    session_.reset();
                }
                callback_runtime_.reset();
                for (auto& channel : driver_buffers_)
                {
                    channel = {};
                }
                has_previous_sample_position_ = false;
                if (!failure && had_physical_resources)
                {
                    last_closed_physical_session_facts_ = {
                        .session = closing_session_facts,
                        .callback_quiesced = callback_quiesced,
                        .buffers_disposed =
                        !had_session || cleanup_report.buffers_disposed,
                        .restoration_attempted =
                        cleanup_report.sample_rate_restoration_attempted,
                        .restoration_succeeded =
                        cleanup_report.sample_rate_restored,
                        .available = true,
                    };
                }
                active_physical_session_facts_ = {};
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
                if (logical_clock_ != nullptr)
                {
                    final_exact_clock_counters_ = logical_clock_->counters();
                    has_final_exact_clock_counters_ = true;
                    logical_clock_->Invalidate();
                    if (exact_clock_registered_)
                    {
                        detail::UnregisterExactJudgementTimeline(
                            logical_timeline_generation_);
                    }
                    exact_clock_registered_ = false;
                    logical_timeline_generation_ = 0;
                    logical_clock_.reset();
                }
                if (submitted_tail_ != nullptr)
                {
                    final_submitted_tail_snapshot_ = submitted_tail_->Read();
                    has_final_submitted_tail_snapshot_ =
                        final_submitted_tail_snapshot_.stable;
                    submitted_tail_->Invalidate();
                }
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
                if (render_core_ == nullptr ||
                    logical_render_sequencer_ == nullptr)
                {
                    if (logical_render_sequencer_ != nullptr)
                    {
                        logical_render_sequencer_->Abandon(plan);
                    }
                    return std::nullopt;
                }
                const auto block = render_core_->Render(plan.timeline);
                render_diagnostics_.RecordRender(block);
                return LogicalRenderResult{block, plan.submitted_output_tail};
            }

            DWORD SilentPollIntervalMs() const noexcept
            {
                if (logical_output_sample_rate_ == 0)
                {
                    return 1;
                }
                constexpr std::uint64_t milliseconds_per_second = 1'000;
                const auto scaled_frames =
                    static_cast<std::uint64_t>(request_.buffer_frames) *
                    milliseconds_per_second;
                const auto milliseconds =
                    (scaled_frames + logical_output_sample_rate_ - 1) /
                    logical_output_sample_rate_;
                return static_cast<DWORD>((std::max)(
                    std::uint64_t{1},
                    (std::min)(milliseconds,
                               static_cast<std::uint64_t>((std::numeric_limits<DWORD>::max)()))));
            }

            std::optional<AsioFailure> AdvanceSilentRendering() noexcept
            {
                // This clock advances an already-confirmed background interval; it is
                // never used to infer whether the game owns the foreground.
                if (logical_clock_ == nullptr)
                {
                    return Failure(
                        AsioFailureStage::runtime_clock,
                        "ASIO logical timeline is unavailable during detached rendering");
                }
                const auto now_ms =
                    actions_.time_get_time_ms(actions_.context);
                if (const auto advanced = logical_clock_->ObserveNow(now_ms);
                    !advanced)
                {
                    return Failure(
                        AsioFailureStage::runtime_clock,
                        std::format(
                            "ASIO detached timeline advance failed: {}",
                            static_cast<unsigned>(advanced.error())));
                }
                const auto projected = logical_clock_->WholeFrameAt(
                    now_ms);
                if (!projected)
                {
                    if (projected.error() ==
                        LogicalPresentationClockFailure::SnapshotUnavailable)
                    {
                        return std::nullopt;
                    }
                    return Failure(
                        AsioFailureStage::runtime_clock,
                        std::format(
                            "ASIO detached logical projection failed: {}",
                            static_cast<unsigned>(projected.error())));
                }
                if (*projected >
                    (std::numeric_limits<std::uint64_t>::max)() -
                    logical_output_latency_frames_)
                {
                    return Failure(
                        AsioFailureStage::runtime_clock,
                        "ASIO detached logical render target overflowed");
                }
                const auto plan = logical_render_sequencer_->TryPlanDetached(
                    *projected + logical_output_latency_frames_);
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
                if (!logical_render_sequencer_->Commit(*plan))
                {
                    logical_render_sequencer_->Abandon(*plan);
                    return Failure(
                        AsioFailureStage::runtime_clock,
                        "ASIO detached logical render commit failed");
                }
                if (!submitted_tail_->Publish(
                    rendered->submitted_output_tail))
                {
                    return Failure(
                        AsioFailureStage::runtime_clock,
                        "ASIO submitted-output tail rejected a committed detached render");
                }
                SaturatingAddCounter(
                    detached_discarded_frames_,
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
                    request.sample_position);
                if (decision.kind == AsioClockDecisionKind::invalid)
                {
                    ClearAsioBlock(request.buffer_index);
                    LatchRuntimeFault(AsioFailureStage::runtime_clock);
                    return;
                }
                const auto physical_session_generation =
                    active_physical_session_generation_.load(
                        std::memory_order_acquire);
                auto proof_callbacks =
                    physical_stability_proof_callbacks_.load(
                        std::memory_order_relaxed);
                if (handoff_physical_session_generation_.load(
                        std::memory_order_acquire) !=
                    physical_session_generation)
                {
                    if (!request.has_system_time)
                    {
                        ClearAsioBlock(request.buffer_index);
                        (void)CallOutputReady();
                        return;
                    }
                    if (logical_clock_ == nullptr)
                    {
                        ClearAsioBlock(request.buffer_index);
                        LatchRuntimeFault(AsioFailureStage::runtime_clock);
                        return;
                    }

                    const auto projected =
                        logical_clock_->WholeFrameAtSystemTime(
                            request.system_time_ns);
                    if (!projected)
                    {
                        ClearAsioBlock(request.buffer_index);
                        if (projected.error() ==
                            LogicalPresentationClockFailure::SnapshotUnavailable)
                        {
                            (void)CallOutputReady();
                            return;
                        }
                        LatchRuntimeFault(AsioFailureStage::runtime_clock);
                        return;
                    }
                    if (*projected >
                        (std::numeric_limits<std::uint64_t>::max)() -
                        logical_output_latency_frames_)
                    {
                        ClearAsioBlock(request.buffer_index);
                        LatchRuntimeFault(AsioFailureStage::runtime_clock);
                        return;
                    }

                    const auto attachment =
                        logical_render_sequencer_->AttachPhysicalSession(
                            physical_session_generation,
                            *projected + logical_output_latency_frames_,
                            decision.render_output_frame_begin);
                    if (!attachment)
                    {
                        ClearAsioBlock(request.buffer_index);
                        if (attachment.error() ==
                            AsioLogicalRenderPlanFailure::Busy)
                        {
                            (void)CallOutputReady();
                            return;
                        }
                        LatchRuntimeFault(AsioFailureStage::runtime_clock);
                        return;
                    }

                    handoff_logical_render_origin_.store(
                        attachment->logical_render_origin,
                        std::memory_order_relaxed);
                    handoff_physical_render_origin_.store(
                        attachment->physical_render_origin,
                        std::memory_order_relaxed);
                    handoff_raw_sample_origin_.store(
                        request.sample_position,
                        std::memory_order_relaxed);
                    handoff_attachment_disposition_.store(
                        static_cast<std::uint8_t>(attachment->disposition),
                        std::memory_order_relaxed);
                    handoff_attachment_interval_frames_.store(
                        attachment->interval_frames,
                        std::memory_order_relaxed);
                    handoff_physical_session_generation_.store(
                        physical_session_generation,
                        std::memory_order_release);
                    proof_callbacks = 1;
                    physical_stability_proof_callbacks_.store(
                        proof_callbacks,
                        std::memory_order_relaxed);
                }
                else if (proof_callbacks < 3)
                {
                    ++proof_callbacks;
                    physical_stability_proof_callbacks_.store(
                        proof_callbacks,
                        std::memory_order_relaxed);
                }

                const auto logical_plan =
                    logical_render_sequencer_->TryPlanPhysical(
                        physical_session_generation,
                        decision.render_output_frame_begin);
                if (!logical_plan)
                {
                    ClearAsioBlock(request.buffer_index);
                    if (logical_plan.error() ==
                        AsioLogicalRenderPlanFailure::Busy)
                    {
                        (void)CallOutputReady();
                        return;
                    }
                    if (logical_plan.error() ==
                        AsioLogicalRenderPlanFailure::NotDue)
                    {
                        if (proof_callbacks < 3)
                        {
                            SaturatingIncrementCounter(
                                physical_silent_priming_callbacks_);
                        }
                        if (CallOutputReady() && proof_callbacks >= 3)
                        {
                            actions_.signal_event(
                                actions_.context,
                                stable_render_event_);
                        }
                        return;
                    }
                    LatchRuntimeFault(AsioFailureStage::runtime_clock);
                    return;
                }
                RecordDriverTimelineResidual(request, *logical_plan);
                const auto rendered = RenderLogicalBlock(*logical_plan);
                if (!rendered)
                {
                    ClearAsioBlock(request.buffer_index);
                    LatchRuntimeFault(AsioFailureStage::runtime_clock);
                    return;
                }
                if (proof_callbacks < 3)
                {
                    ClearAsioBlock(request.buffer_index);
                    if (!logical_render_sequencer_->Commit(*logical_plan))
                    {
                        logical_render_sequencer_->Abandon(*logical_plan);
                        LatchRuntimeFault(AsioFailureStage::runtime_clock);
                        return;
                    }
                    if (!submitted_tail_->Publish(
                        rendered->submitted_output_tail))
                    {
                        LatchRuntimeFault(AsioFailureStage::runtime_clock);
                        return;
                    }
                    SaturatingAddCounter(
                        render_gap_frames_,
                        logical_plan->timeline.discontinuity_frames);
                    SaturatingAddCounter(
                        priming_discarded_frames_, request_.buffer_frames);
                    SaturatingIncrementCounter(
                        physical_silent_priming_callbacks_);
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
                    logical_render_sequencer_->Abandon(*logical_plan);
                    ClearAsioBlock(request.buffer_index);
                    LatchRuntimeFault(AsioFailureStage::conversion);
                    return;
                }
                if (!logical_render_sequencer_->Commit(*logical_plan))
                {
                    logical_render_sequencer_->Abandon(*logical_plan);
                    ClearAsioBlock(request.buffer_index);
                    LatchRuntimeFault(AsioFailureStage::runtime_clock);
                    return;
                }
                if (!submitted_tail_->Publish(
                    rendered->submitted_output_tail))
                {
                    ClearAsioBlock(request.buffer_index);
                    LatchRuntimeFault(AsioFailureStage::runtime_clock);
                    return;
                }
                SaturatingAddCounter(
                    render_gap_frames_,
                    logical_plan->timeline.discontinuity_frames);
                if (!CallOutputReady())
                {
                    ClearAsioBlock(request.buffer_index);
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

            void RecordDriverTimelineResidual(
                const AsioRenderRequest& request,
                const AsioLogicalRenderPlan& plan) noexcept
            {
                if (!request.has_system_time || logical_clock_ == nullptr ||
                    logical_output_sample_rate_ == 0 ||
                    plan.timeline.output_frame_begin <
                    logical_output_latency_frames_)
                {
                    return;
                }
                const auto mapped_presented_frame =
                    plan.timeline.output_frame_begin -
                    logical_output_latency_frames_;
                if (mapped_presented_frame > static_cast<std::uint64_t>(
                    (std::numeric_limits<std::int64_t>::max)()))
                {
                    return;
                }

                const auto projected =
                    logical_clock_->ProjectSystemTimeNanoseconds(
                        request.system_time_ns);
                if (!projected)
                {
                    return;
                }
                const auto residual_frames = projected->Subtract(
                    gc::timing::CheckedRational::Whole(
                        static_cast<std::int64_t>(mapped_presented_frame)));
                if (!residual_frames)
                {
                    return;
                }
                const auto residual_nanoseconds = residual_frames->Multiply(
                    1'000'000'000,
                    logical_output_sample_rate_);
                if (!residual_nanoseconds)
                {
                    return;
                }

                const auto numerator = residual_nanoseconds->numerator();
                const auto magnitude = numerator >= 0
                                           ? static_cast<std::uint64_t>(numerator)
                                           : std::uint64_t{0} -
                                           static_cast<std::uint64_t>(numerator);
                const auto denominator = residual_nanoseconds->denominator();
                auto absolute_nanoseconds = magnitude / denominator;
                if (magnitude % denominator != 0)
                {
                    ++absolute_nanoseconds;
                }
                SaturatingIncrementCounter(
                    driver_timeline_residual_samples_);
                MaximumCounter(
                    maximum_absolute_driver_timeline_residual_ns_,
                    absolute_nanoseconds);
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
                const auto exact = logical_clock_ != nullptr
                                       ? logical_clock_->counters()
                                       : has_final_exact_clock_counters_
                                       ? final_exact_clock_counters_
                                       : ExactJudgementTimelineCounters{};
                const auto render = render_diagnostics_.Snapshot();
                const auto submitted_tail =
                    has_final_submitted_tail_snapshot_
                        ? final_submitted_tail_snapshot_
                        : submitted_tail_ != nullptr
                        ? submitted_tail_->Read()
                        : AsioSubmittedOutputTailSnapshot{};
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
                    .consumed_focus_loss_generation =
                    consumed_focus_loss_generation_.load(
                        std::memory_order_relaxed),
                    .physical_session_generation =
                    logical_render_sequencer_ != nullptr
                        ? logical_render_sequencer_->physical_session_generation()
                        : 0,
                    .session_releases =
                    session_releases_.load(std::memory_order_relaxed),
                    .recovery_attempts =
                    recovery_attempts_.load(std::memory_order_relaxed),
                    .recovery_failures =
                    recovery_failures_.load(std::memory_order_relaxed),
                    .session_recoveries =
                    session_recoveries_.load(std::memory_order_relaxed),
                    .submitted_tail_publications =
                    submitted_tail.stable
                        ? submitted_tail.publication_sequence
                        : 0,
                    .submitted_output_tail =
                    submitted_tail.stable && submitted_tail.available
                        ? submitted_tail.submitted_output_tail
                        : 0,
                    .total_logically_advanced_frames =
                    submitted_tail.stable && submitted_tail.available
                        ? submitted_tail.submitted_output_tail
                        : 0,
                    .detached_discarded_frames =
                    detached_discarded_frames_.load(
                        std::memory_order_relaxed),
                    .priming_discarded_frames =
                    priming_discarded_frames_.load(
                        std::memory_order_relaxed),
                    .driver_timeline_residual_samples =
                    driver_timeline_residual_samples_.load(
                        std::memory_order_relaxed),
                    .maximum_absolute_driver_timeline_residual_ns =
                    maximum_absolute_driver_timeline_residual_ns_.load(
                        std::memory_order_relaxed),
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
            ExactJudgementTimelineCounters final_exact_clock_counters_{};
            bool has_final_exact_clock_counters_{};
            bool exact_clock_registered_{};
            std::uint64_t logical_timeline_generation_{};
            AsioClockTracker clock_tracker_;
            std::unique_ptr<AsioLogicalRenderSequencer>
            logical_render_sequencer_;
            std::shared_ptr<LogicalPresentationClock> logical_clock_;
            std::shared_ptr<AsioSubmittedOutputTail> submitted_tail_;
            AsioSubmittedOutputTailSnapshot final_submitted_tail_snapshot_{};
            bool has_final_submitted_tail_snapshot_{};
            AsioLogicalOutputContract logical_contract_{};
            PhysicalSessionFacts active_physical_session_facts_{};
            ClosedPhysicalSessionFacts last_closed_physical_session_facts_{};
            std::array<ASIOSampleType, 2> channel_types_{};
            std::array<std::array<std::span<std::byte>, 2>, 2> driver_buffers_{};
            AsioCallbackRuntimeSnapshot completed_callback_snapshot_{};

            std::atomic_uint32_t endpoint_buffer_frames_{};
            std::atomic_uint32_t output_sample_rate_{};
            AsioRenderDiagnostics render_diagnostics_;
            std::atomic_uint64_t sample_position_discontinuities_{};
            std::atomic_uint64_t render_gap_frames_{};
            std::atomic_uint64_t foreground_losses_{};
            std::atomic_uint64_t consumed_focus_loss_generation_{};
            std::atomic_uint64_t session_releases_{};
            std::atomic_uint64_t recovery_attempts_{};
            std::atomic_uint64_t recovery_failures_{};
            std::atomic_uint64_t session_recoveries_{};
            std::atomic_uint64_t detached_discarded_frames_{};
            std::atomic_uint64_t priming_discarded_frames_{};
            std::atomic_uint64_t driver_timeline_residual_samples_{};
            std::atomic_uint64_t maximum_absolute_driver_timeline_residual_ns_{};
            std::atomic_uint64_t pending_cursor_queries_{};
            std::atomic_uint64_t unmapped_cursor_failures_{};
            std::atomic_bool first_fault_claimed_{};
            std::atomic<std::uint8_t> first_fault_stage_{};
            std::atomic<std::uint8_t> first_fault_domain_{};
            std::atomic<std::int64_t> first_fault_result_{};
            std::atomic_uint64_t active_physical_session_generation_{};
            std::atomic_uint64_t handoff_physical_session_generation_{};
            std::atomic_uint64_t handoff_logical_render_origin_{};
            std::atomic_uint64_t handoff_physical_render_origin_{};
            std::atomic_uint64_t handoff_raw_sample_origin_{};
            std::atomic_uint8_t handoff_attachment_disposition_{};
            std::atomic_uint64_t handoff_attachment_interval_frames_{};
            std::atomic_uint64_t physical_silent_priming_callbacks_{};
            std::atomic_uint32_t physical_stability_proof_callbacks_{};
            std::uint32_t logical_output_sample_rate_{};
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

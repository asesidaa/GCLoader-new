// SPDX-License-Identifier: CC0-1.0

#include "Audio/Asio/AsioOutputBackend.h"
#include "Audio/Asio/AsioOutputBackendInternal.h"

#include "Audio/Asio/AsioCallbackRuntime.h"
#include "Audio/Asio/AsioForegroundMonitor.h"
#include "Audio/Asio/AsioPhysicalSessionController.h"
#include "Audio/Asio/AsioPresentationBridge.h"
#include "Audio/Asio/AsioSampleConverter.h"
#include "Audio/Asio/AsioSession.h"
#include "Audio/ExactJudgementTimeline.h"
#include "Audio/Logical/LogicalPresentationClock.h"
#include "Audio/Logical/LogicalPresentedOutputClock.h"
#include "Audio/Mixer/AudioRenderCore.h"
#include "Audio/Mixer/LogicalRenderStream.h"

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
#include <vector>

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
                    std::array < char, 320 > suffix{};
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

            void MergeBridgeSnapshot(
                AsioPresentationBridgeSnapshot& total,
                bool& has_total,
                const AsioPresentationBridgeSnapshot& session) noexcept
            {
                if (!has_total)
                {
                    total = session;
                    has_total = true;
                    return;
                }

                const auto add = [](std::uint64_t& destination,
                                    const std::uint64_t value) noexcept
                {
                    const auto remaining =
                        (std::numeric_limits<std::uint64_t>::max)() -
                        destination;
                    destination = value > remaining
                                      ? (std::numeric_limits<
                                          std::uint64_t>::max)()
                                      : destination + value;
                };
                const auto previous_running_callbacks =
                    total.running_callbacks;

                total.state = session.state;
                if (total.first_fault ==
                    AsioPresentationBridgeFault::None &&
                    session.first_fault !=
                    AsioPresentationBridgeFault::None)
                {
                    total.first_fault = session.first_fault;
                }
                total.physical_session_generation =
                    session.physical_session_generation;
                add(total.callbacks, session.callbacks);
                add(total.priming_callbacks,
                    session.priming_callbacks);
                add(total.running_callbacks,
                    session.running_callbacks);
                if (session.handoff_logical_tail != 0)
                {
                    total.handoff_logical_tail =
                        session.handoff_logical_tail;
                }
                add(total.logical_rendered_frames,
                    session.logical_rendered_frames);
                total.input_high_water_frames = (std::max)(
                    total.input_high_water_frames,
                    session.input_high_water_frames);
                add(total.input_underflows,
                    session.input_underflows);
                add(total.input_overflows,
                    session.input_overflows);
                add(total.conversion_failures,
                    session.conversion_failures);
                add(total.phase_envelope_violations,
                    session.phase_envelope_violations);
                add(total.non_finite_output_blocks,
                    session.non_finite_output_blocks);
                total.resampler_input_latency_frames =
                    session.resampler_input_latency_frames;
                total.resampler_output_latency_frames =
                    session.resampler_output_latency_frames;
                total.phase_envelope_frames =
                    session.phase_envelope_frames;

                if (session.running_callbacks != 0)
                {
                    if (previous_running_callbacks == 0)
                    {
                        total.initial_phase_error_frames =
                            session.initial_phase_error_frames;
                        total.minimum_rate_ratio_ppm =
                            session.minimum_rate_ratio_ppm;
                        total.maximum_rate_ratio_ppm =
                            session.maximum_rate_ratio_ppm;
                    }
                    else
                    {
                        total.minimum_rate_ratio_ppm = (std::min)(
                            total.minimum_rate_ratio_ppm,
                            session.minimum_rate_ratio_ppm);
                        total.maximum_rate_ratio_ppm = (std::max)(
                            total.maximum_rate_ratio_ppm,
                            session.maximum_rate_ratio_ppm);
                    }
                    total.maximum_absolute_phase_error_frames =
                        (std::max)(
                            total.maximum_absolute_phase_error_frames,
                            session.maximum_absolute_phase_error_frames);
                    total.final_phase_error_frames =
                        session.final_phase_error_frames;
                    total.final_rate_ratio_ppm =
                        session.final_rate_ratio_ppm;
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
                if (render_core_ == nullptr ||
                    logical_render_stream_ == nullptr)
                {
                    return std::nullopt;
                }
                const auto current =
                    render_core_->CurrentOutputFrame();
                if (!current)
                {
                    return std::nullopt;
                }
                if (logical_render_stream_->committed_tail() <=
                    *current)
                {
                    LatchRuntimeFault(
                        AsioFailureStage::runtime_clock);
                    return std::nullopt;
                }
                return current;
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
                focus_lost,
                shutdown,
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

            struct ObservedControlDirective final
            {
                AsioForegroundSnapshot focus{};
                AsioControlDirective directive{};
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

                auto stable = DrivePhysicalLifecycle(*initialized);
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

                endpoint_buffer_frames_.store(
                    request_.buffer_frames,
                    std::memory_order_release);
                output_sample_rate_.store(
                    logical_output_sample_rate_,
                    std::memory_order_release);

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
                        .timestamp_quantum_ns =
                        logical_clock_->info().timestamp_quantum_ns,
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
                    presentation_bridge_ != nullptr
                        ? presentation_bridge_->Snapshot().
                                                handoff_logical_tail
                        : 0,
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

            std::expected<AsioControlDirective, AsioFailure>
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

                    const auto focus = foreground_monitor_->snapshot();
                    consumed_focus_loss_generation_.store(
                        focus.loss_generation,
                        std::memory_order_release);
                    return lifecycle_controller_.Start(focus);
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
                            logical_render_stream_ != nullptr ||
                            logical_clock_ != nullptr ||
                            pump_lease_.has_value() ||
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
                        if (logical_clock_ == nullptr)
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
                            mixer_allocations_,
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
                        logical_render_stream_ =
                            LogicalRenderStream::Create(*render_core_);
                        if (logical_render_stream_ == nullptr)
                        {
                            return std::unexpected(FatalPreparationFailure(
                                Failure(
                                    AsioFailureStage::render_core,
                                    "Could not create the persistent ASIO logical render stream")));
                        }
                        const auto pump_lease =
                            logical_render_stream_->AcquireInitial(
                                LogicalRenderOwner::Pump);
                        if (!pump_lease)
                        {
                            return std::unexpected(FatalPreparationFailure(
                                Failure(
                                    AsioFailureStage::render_core,
                                    std::format(
                                        "Could not acquire the initial ASIO logical render lease: {}",
                                        static_cast<unsigned>(pump_lease.error())))));
                        }
                        pump_lease_ = *pump_lease;
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
                        logical_render_stream_ == nullptr ||
                        logical_clock_ == nullptr ||
                        !pump_lease_.has_value())
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
                    if (physical_session_generation_ ==
                        (std::numeric_limits<std::uint64_t>::max)())
                    {
                        return std::unexpected(FatalPreparationFailure(
                            Failure(
                                AsioFailureStage::protocol,
                                "ASIO physical-session generation overflowed")));
                    }
                    const auto physical_generation =
                        ++physical_session_generation_;
                    presentation_bridge_ =
                        AsioPresentationBridge::Create(
                            {
                                .physical_session_generation =
                                physical_generation,
                                .logical_rate =
                                logical_output_sample_rate_,
                                .driver_rate = session_sample_rate,
                                .period_frames =
                                request_.buffer_frames,
                                .driver_output_latency_frames =
                                session_->report().output_latency_frames,
                                .timestamp_quantum_ns =
                                logical_clock_->info().timestamp_quantum_ns,
                            },
                            logical_clock_,
                            *logical_render_stream_,
                            mixer_allocations_);
                    if (presentation_bridge_ == nullptr)
                    {
                        return std::unexpected(PreStartPreparationFailure(
                            purpose,
                            Failure(
                                AsioFailureStage::render_core,
                                "Could not create the preallocated ASIO presentation bridge")));
                    }
                    physical_float_output_.assign(
                        static_cast<std::size_t>(
                            request_.buffer_frames) *
                        2,
                        0.0F);
                    active_physical_session_generation_.store(
                        physical_generation,
                        std::memory_order_release);
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
                        presentation_bridge_ == nullptr)
                    {
                        return std::unexpected(Failure(
                            AsioFailureStage::protocol,
                            "ASIO Start requires a prepared physical session"));
                    }

                    physical_stability_proof_callbacks_.store(
                        0, std::memory_order_relaxed);
                    pending_bridge_lease_.reset();
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

                logical_contract_ = {
                    .registration = report.registration,
                    .sample_rate = logical_output_sample_rate_,
                    .period_frames = report.effective_buffer_frames,
                    .output_base_channel = report.selected_base_channel,
                    .channel_types = channel_types_,
                    .output_latency_frames = report.output_latency_frames,
                    .output_ready_supported = report.output_ready_supported,
                };
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

            std::expected<ObservedControlDirective, AsioFailure>
            ObserveControllerForeground() noexcept
            {
                if (!foreground_monitor_->healthy())
                {
                    return std::unexpected(Failure(
                        AsioFailureStage::foreground_monitor,
                        "ASIO foreground monitor stopped unexpectedly",
                        AsioResultDomain::win32,
                        foreground_monitor_->failure_code()));
                }

                const auto focus = foreground_monitor_->snapshot();
                if (const auto failure = ValidateFocusSnapshot(
                    focus,
                    lifecycle_controller_.
                    consumed_focus_loss_generation()))
                {
                    return std::unexpected(*failure);
                }
                const auto previous_generation =
                    consumed_focus_loss_generation_.load(
                        std::memory_order_relaxed);
                const auto directive =
                    lifecycle_controller_.ObserveForeground(focus);
                const auto consumed_generation =
                    lifecycle_controller_.
                    consumed_focus_loss_generation();
                if (consumed_generation > previous_generation)
                {
                    SaturatingAddCounter(
                        foreground_losses_,
                        consumed_generation - previous_generation);
                    consumed_focus_loss_generation_.store(
                        consumed_generation,
                        std::memory_order_release);
                }
                if (directive.kind ==
                    AsioControlDirectiveKind::FailFatal)
                {
                    return std::unexpected(Failure(
                        AsioFailureStage::foreground_monitor,
                        "ASIO physical-session controller rejected the foreground transition"));
                }
                return ObservedControlDirective{
                    .focus = focus,
                    .directive = directive,
                };
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
                    if (HasPublishedFault())
                    {
                        return std::unexpected(
                            BuildLatchedFailure());
                    }
                    if (presentation_bridge_ == nullptr ||
                        logical_render_stream_ == nullptr)
                    {
                        return std::unexpected(Failure(
                            AsioFailureStage::runtime_clock,
                            "ASIO presentation bridge disappeared while waiting for stability"));
                    }
                    if (presentation_bridge_->state() ==
                        AsioPresentationBridgeState::Faulted)
                    {
                        LatchRuntimeFault(
                            AsioFailureStage::runtime_clock);
                        return std::unexpected(
                            BuildLatchedFailure());
                    }
                    auto observed_focus =
                        ObserveControllerForeground();
                    if (!observed_focus)
                    {
                        return std::unexpected(
                            std::move(observed_focus.error()));
                    }
                    if (observed_focus->directive.kind ==
                        AsioControlDirectiveKind::ReleaseToSuspended)
                    {
                        return StableRenderOutcome::focus_lost;
                    }
                    if (observed_focus->directive.kind !=
                        AsioControlDirectiveKind::ContinuePump)
                    {
                        return std::unexpected(Failure(
                            AsioFailureStage::protocol,
                            "ASIO controller produced an invalid directive while priming"));
                    }
                    if (presentation_bridge_->state() ==
                        AsioPresentationBridgeState::Running)
                    {
                        const auto committed =
                            lifecycle_controller_.
                            ReportRunningCommitted();
                        if (committed.kind !=
                            AsioControlDirectiveKind::CommitRunning)
                        {
                            return std::unexpected(Failure(
                                AsioFailureStage::protocol,
                                "ASIO controller rejected the running bridge commit"));
                        }
                        return StableRenderOutcome::stable;
                    }

                    if (const auto failure =
                        AdvanceSilentRendering())
                    {
                        return std::unexpected(*failure);
                    }
                    if (physical_stability_proof_callbacks_.load(
                            std::memory_order_acquire) >= 3 &&
                        presentation_bridge_->state() ==
                        AsioPresentationBridgeState::Priming)
                    {
                        const auto now_ms =
                            actions_.time_get_time_ms(
                                actions_.context);
                        const auto projected =
                            logical_clock_->WholeFrameAt(now_ms);
                        if (!projected)
                        {
                            if (projected.error() !=
                                LogicalPresentationClockFailure::
                                SnapshotUnavailable)
                            {
                                return std::unexpected(Failure(
                                    AsioFailureStage::runtime_clock,
                                    std::format(
                                        "ASIO bridge handoff projection failed: {}",
                                        static_cast<unsigned>(
                                            projected.error()))));
                            }
                        }
                        else if (*projected <=
                            (std::numeric_limits<
                                std::uint64_t>::max)() -
                            request_.buffer_frames)
                        {
                            const auto required_tail =
                                *projected +
                                request_.buffer_frames;
                            const auto committed_tail =
                                logical_render_stream_->
                                committed_tail();
                            if (committed_tail > required_tail)
                            {
                                if (!pending_bridge_lease_)
                                {
                                    if (!pump_lease_)
                                    {
                                        return std::unexpected(
                                            Failure(
                                                AsioFailureStage::
                                                runtime_clock,
                                                "ASIO bridge handoff has no pump lease"));
                                    }
                                    const auto transferred =
                                        logical_render_stream_->
                                        Transfer(
                                            *pump_lease_,
                                            LogicalRenderOwner::
                                            AsioBridge,
                                            committed_tail);
                                    if (!transferred)
                                    {
                                        if (transferred.error() !=
                                            LogicalRenderFailure::
                                            Busy)
                                        {
                                            return std::unexpected(
                                                Failure(
                                                    AsioFailureStage::
                                                    runtime_clock,
                                                    std::format(
                                                        "ASIO bridge lease transfer failed: {}",
                                                        static_cast<unsigned>(
                                                            transferred.error()))));
                                        }
                                    }
                                    else
                                    {
                                        pending_bridge_lease_ =
                                            *transferred;
                                        pump_lease_.reset();
                                        const auto handoff =
                                            lifecycle_controller_.
                                            ReportRenderLeaseTransferred();
                                        if (handoff.kind !=
                                            AsioControlDirectiveKind::
                                            ContinuePump)
                                        {
                                            return std::unexpected(Failure(
                                                AsioFailureStage::protocol,
                                                "ASIO controller rejected the logical render lease transfer"));
                                        }
                                    }
                                }
                                if (pending_bridge_lease_)
                                {
                                    const auto armed =
                                        presentation_bridge_->Arm(
                                            *pending_bridge_lease_,
                                            committed_tail);
                                    if (!armed)
                                    {
                                        if (armed.error() !=
                                            AsioPresentationBridgeControlFailure::
                                            Busy)
                                        {
                                            return std::unexpected(
                                                Failure(
                                                    AsioFailureStage::
                                                    runtime_clock,
                                                    std::format(
                                                        "ASIO bridge arm failed: {}",
                                                        static_cast<unsigned>(
                                                            armed.error()))));
                                        }
                                    }
                                    else
                                    {
                                        pending_bridge_lease_.reset();
                                        if (actions_.reset_event !=
                                            nullptr)
                                        {
                                            actions_.reset_event(
                                                actions_.context,
                                                stable_render_event_);
                                        }
                                        else
                                        {
                                            ResetEvent(
                                                stable_render_event_);
                                        }
                                    }
                                }
                            }
                        }
                        else
                        {
                            return std::unexpected(Failure(
                                AsioFailureStage::runtime_clock,
                                "ASIO bridge handoff target overflowed"));
                        }
                    }

                    const DWORD remaining = RemainingTimeout(
                        started_ms,
                        actions_.tick_count_ms(actions_.context),
                        startup_clock_timeout_ms_);
                    if (remaining == 0)
                    {
                        return std::unexpected(Failure(
                            AsioFailureStage::startup_clock,
                            "ASIO did not commit an aligned running bridge before the startup deadline",
                            AsioResultDomain::win32,
                            WAIT_TIMEOUT));
                    }
                    const DWORD wait = actions_.message_wait(
                        actions_.context,
                        handles,
                        (std::min)(
                            remaining,
                            SilentPollIntervalMs()));
                    if (wait == WAIT_OBJECT_0)
                    {
                        if (actions_.reset_event != nullptr)
                        {
                            actions_.reset_event(
                                actions_.context,
                                stable_render_event_);
                        }
                        else
                        {
                            ResetEvent(stable_render_event_);
                        }
                        continue;
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
                        continue;
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
                const std::uint64_t handoff_logical_tail,
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
                    .handoff_logical_tail = handoff_logical_tail,
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
                    record.silent_priming_callbacks =
                        presentation_bridge_ != nullptr
                            ? presentation_bridge_->Snapshot().
                                                    priming_callbacks
                            : 0;
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

            std::expected<AsioControlDirective, AsioFailure>
            ReleasePhysicalSessionToSuspended() noexcept
            {
                const auto focus = foreground_monitor_->snapshot();
                const auto physical_generation =
                    active_physical_session_generation_.load(
                        std::memory_order_acquire);
                const auto recovery_attempt =
                    lifecycle_controller_.recovery_attempt();
                const auto commit_phase =
                    lifecycle_controller_.commit_phase();
                const bool bridge_had_running_callback =
                    presentation_bridge_ != nullptr &&
                    presentation_bridge_->Snapshot().
                                          running_callbacks != 0;
                const bool had_physical_resources =
                    session_ != nullptr ||
                    callback_runtime_ != nullptr ||
                    presentation_bridge_ != nullptr;

                ReportLifecycle(
                    AsioSessionLifecycleEvent::foreground_lost,
                    focus,
                    physical_generation,
                    recovery_attempt,
                    0,
                    0,
                    nullptr);

                auto close_failure = ClosePhysicalSession();
                if (HasPublishedFault() &&
                    (commit_phase ==
                        AsioPhysicalCommitPhase::Running ||
                        bridge_had_running_callback))
                {
                    auto failure = BuildLatchedFailure();
                    if (close_failure)
                    {
                        AppendSecondaryFailure(
                            failure,
                            *close_failure);
                    }
                    return std::unexpected(std::move(failure));
                }
                if (close_failure)
                {
                    return std::unexpected(
                        std::move(*close_failure));
                }

                if (had_physical_resources)
                {
                    SaturatingIncrementCounter(session_releases_);
                    ReportLifecycle(
                        AsioSessionLifecycleEvent::session_released,
                        focus,
                        physical_generation,
                        recovery_attempt,
                        0,
                        0,
                        nullptr,
                        nullptr,
                        &last_closed_physical_session_facts_);
                }
                ClearSessionFault();

                const auto directive =
                    lifecycle_controller_.ReportPhysicalReleased();
                if (directive.kind ==
                    AsioControlDirectiveKind::FailFatal)
                {
                    return std::unexpected(Failure(
                        AsioFailureStage::protocol,
                        "ASIO controller rejected completed physical-session release"));
                }
                if (directive.kind ==
                    AsioControlDirectiveKind::BeginPhysicalAttempt)
                {
                    const auto regained_focus =
                        foreground_monitor_->snapshot();
                    ReportLifecycle(
                        AsioSessionLifecycleEvent::foreground_regained,
                        regained_focus,
                        0,
                        directive.recovery_attempt,
                        0,
                        0,
                        nullptr);
                }
                return directive;
            }

            std::expected<AsioControlDirective, AsioFailure>
            HandlePhysicalAttemptFailure(
                const PhysicalSessionPurpose purpose,
                AsioFailure failure,
                AsioPhysicalAttemptFailureKind failure_kind) noexcept
            {
                const auto physical_generation =
                    active_physical_session_generation_.load(
                        std::memory_order_acquire);
                const auto recovery_attempt =
                    lifecycle_controller_.recovery_attempt();
                const bool had_physical_resources =
                    session_ != nullptr ||
                    callback_runtime_ != nullptr ||
                    presentation_bridge_ != nullptr;
                const bool bridge_had_running_callback =
                    presentation_bridge_ != nullptr &&
                    presentation_bridge_->Snapshot().
                                          running_callbacks != 0;
                if (bridge_had_running_callback)
                {
                    failure_kind =
                        AsioPhysicalAttemptFailureKind::Fatal;
                }

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
                    AppendSecondaryFailure(
                        failure,
                        *close_failure);
                }
                const bool cleanup_complete =
                    !close_failure &&
                    session_ == nullptr &&
                    callback_runtime_ == nullptr &&
                    presentation_bridge_ == nullptr &&
                    (logical_render_stream_ == nullptr ||
                        pump_lease_.has_value());

                AsioForegroundSnapshot focus{};
                if (foreground_monitor_ == nullptr ||
                    !foreground_monitor_->healthy())
                {
                    auto monitor_failure = Failure(
                        AsioFailureStage::foreground_monitor,
                        "ASIO foreground monitor stopped during physical-attempt cleanup",
                        AsioResultDomain::win32,
                        foreground_monitor_ != nullptr
                            ? foreground_monitor_->failure_code()
                            : ERROR_INVALID_HANDLE);
                    AppendSecondaryFailure(
                        monitor_failure,
                        failure);
                    return std::unexpected(
                        std::move(monitor_failure));
                }
                focus = foreground_monitor_->snapshot();

                if (had_physical_resources &&
                    cleanup_complete)
                {
                    SaturatingIncrementCounter(session_releases_);
                    ReportLifecycle(
                        AsioSessionLifecycleEvent::session_released,
                        focus,
                        physical_generation,
                        recovery_attempt,
                        0,
                        0,
                        nullptr,
                        nullptr,
                        &last_closed_physical_session_facts_);
                }

                if (purpose ==
                    PhysicalSessionPurpose::FocusRecovery &&
                    cleanup_complete &&
                    failure_kind ==
                    AsioPhysicalAttemptFailureKind::
                    RetryableBeforeRunning)
                {
                    auto observed =
                        ObserveControllerForeground();
                    if (!observed)
                    {
                        auto focus_failure =
                            std::move(observed.error());
                        AppendSecondaryFailure(
                            focus_failure,
                            failure);
                        return std::unexpected(
                            std::move(focus_failure));
                    }
                    focus = observed->focus;
                    if (observed->directive.kind ==
                        AsioControlDirectiveKind::
                        ReleaseToSuspended)
                    {
                        if (cleanup_complete)
                        {
                            ClearSessionFault();
                        }
                        return observed->directive;
                    }
                    if (observed->directive.kind !=
                        AsioControlDirectiveKind::ContinuePump)
                    {
                        auto protocol_failure = Failure(
                            AsioFailureStage::protocol,
                            "ASIO controller produced an invalid directive after recovery failure");
                        AppendSecondaryFailure(
                            protocol_failure,
                            failure);
                        return std::unexpected(
                            std::move(protocol_failure));
                    }
                }

                const auto directive =
                    lifecycle_controller_.ReportAttemptFailed(
                        failure_kind,
                        cleanup_complete);
                if (purpose ==
                    PhysicalSessionPurpose::FocusRecovery)
                {
                    SaturatingIncrementCounter(
                        recovery_failures_);
                    ReportLifecycle(
                        AsioSessionLifecycleEvent::
                        recovery_attempt_failed,
                        focus,
                        0,
                        recovery_attempt,
                        directive.retry_delay_ms,
                        0,
                        &failure);
                }
                if (directive.kind ==
                    AsioControlDirectiveKind::WaitRetry)
                {
                    ClearSessionFault();
                    return directive;
                }
                if (directive.kind ==
                    AsioControlDirectiveKind::FailFatal)
                {
                    return std::unexpected(
                        std::move(failure));
                }

                auto protocol_failure = Failure(
                    AsioFailureStage::protocol,
                    "ASIO controller did not classify a failed physical attempt");
                AppendSecondaryFailure(
                    protocol_failure,
                    failure);
                return std::unexpected(
                    std::move(protocol_failure));
            }

            std::expected<AsioControlDirective, AsioFailure>
            WaitForRecoveryRetry(
                const AsioControlDirective& wait_directive) noexcept
            {
                if (wait_directive.kind !=
                    AsioControlDirectiveKind::WaitRetry ||
                    wait_directive.retry_delay_ms == 0)
                {
                    return std::unexpected(Failure(
                        AsioFailureStage::protocol,
                        "ASIO controller supplied an invalid recovery delay"));
                }

                const auto started_ms =
                    actions_.tick_count_ms(actions_.context);
                for (;;)
                {
                    if (logical_clock_ != nullptr)
                    {
                        if (const auto failure =
                            AdvanceSilentRendering())
                        {
                            return std::unexpected(*failure);
                        }
                    }

                    const auto now_ms =
                        actions_.tick_count_ms(actions_.context);
                    const auto remaining = RemainingTimeout(
                        started_ms,
                        now_ms,
                        wait_directive.retry_delay_ms);
                    const auto wait_timeout =
                        remaining == 0
                            ? 0
                            : (std::min)(
                                remaining,
                                SilentPollIntervalMs());
                    auto wake =
                        WaitForRuntimeWake(wait_timeout);
                    if (!wake)
                    {
                        return std::unexpected(
                            std::move(wake.error()));
                    }
                    if (*wake == RuntimeWake::shutdown)
                    {
                        return lifecycle_controller_.
                            RequestShutdown();
                    }
                    if (*wake == RuntimeWake::fault ||
                        HasPublishedFault())
                    {
                        return std::unexpected(
                            BuildLatchedFailure());
                    }

                    auto observed =
                        ObserveControllerForeground();
                    if (!observed)
                    {
                        return std::unexpected(
                            std::move(observed.error()));
                    }
                    if (observed->directive.kind ==
                        AsioControlDirectiveKind::
                        ReleaseToSuspended)
                    {
                        return observed->directive;
                    }
                    if (observed->directive.kind !=
                        AsioControlDirectiveKind::ContinuePump)
                    {
                        return std::unexpected(Failure(
                            AsioFailureStage::protocol,
                            "ASIO controller produced an invalid directive during recovery delay"));
                    }

                    if (RemainingTimeout(
                        started_ms,
                        actions_.tick_count_ms(
                            actions_.context),
                        wait_directive.retry_delay_ms) == 0)
                    {
                        const auto retry =
                            lifecycle_controller_.
                            ReportRetryDelayElapsed();
                        if (retry.kind !=
                            AsioControlDirectiveKind::
                            BeginPhysicalAttempt)
                        {
                            return std::unexpected(Failure(
                                AsioFailureStage::protocol,
                                "ASIO controller rejected elapsed recovery delay"));
                        }
                        return retry;
                    }
                }
            }

            std::expected<StableRenderOutcome, AsioFailure>
            DrivePhysicalLifecycle(
                AsioControlDirective directive) noexcept
            {
                for (;;)
                {
                    switch (directive.kind)
                    {
                    case AsioControlDirectiveKind::ContinuePump:
                        {
                            if (lifecycle_controller_.state() !=
                                AsioLifecycleState::Suspended)
                            {
                                return std::unexpected(Failure(
                                    AsioFailureStage::protocol,
                                    "ASIO controller requested pumping outside suspension"));
                            }
                            if (logical_clock_ != nullptr)
                            {
                                if (const auto failure =
                                    AdvanceSilentRendering())
                                {
                                    return std::unexpected(
                                        *failure);
                                }
                            }

                            auto wake = WaitForRuntimeWake(
                                SilentPollIntervalMs());
                            if (!wake)
                            {
                                return std::unexpected(
                                    std::move(wake.error()));
                            }
                            if (*wake ==
                                RuntimeWake::shutdown)
                            {
                                directive =
                                    lifecycle_controller_.
                                    RequestShutdown();
                                break;
                            }
                            if (*wake == RuntimeWake::fault ||
                                HasPublishedFault())
                            {
                                return std::unexpected(
                                    BuildLatchedFailure());
                            }

                            auto observed =
                                ObserveControllerForeground();
                            if (!observed)
                            {
                                return std::unexpected(
                                    std::move(
                                        observed.error()));
                            }
                            directive =
                                observed->directive;
                            if (directive.kind ==
                                AsioControlDirectiveKind::
                                BeginPhysicalAttempt)
                            {
                                ReportLifecycle(
                                    AsioSessionLifecycleEvent::
                                    foreground_regained,
                                    observed->focus,
                                    0,
                                    directive.recovery_attempt,
                                    0,
                                    0,
                                    nullptr);
                            }
                            break;
                        }

                    case AsioControlDirectiveKind::
                    BeginPhysicalAttempt:
                        {
                            const auto purpose =
                                logical_output_sample_rate_ == 0
                                    ? PhysicalSessionPurpose::
                                    InitialStartup
                                    : PhysicalSessionPurpose::
                                    FocusRecovery;
                            if (purpose ==
                                PhysicalSessionPurpose::
                                FocusRecovery)
                            {
                                SaturatingIncrementCounter(
                                    recovery_attempts_);
                                ReportLifecycle(
                                    AsioSessionLifecycleEvent::
                                    recovery_attempt_started,
                                    foreground_monitor_->snapshot(),
                                    0,
                                    directive.recovery_attempt,
                                    0,
                                    0,
                                    nullptr);
                            }

                            auto prepared =
                                PreparePhysicalSession(purpose);
                            if (!prepared)
                            {
                                const auto kind =
                                    prepared.error().kind ==
                                    PhysicalPreparationFailureKind::
                                    retryable_before_start
                                        ? AsioPhysicalAttemptFailureKind::
                                        RetryableBeforeRunning
                                        : AsioPhysicalAttemptFailureKind::
                                        Fatal;
                                auto classified =
                                    HandlePhysicalAttemptFailure(
                                        purpose,
                                        std::move(
                                            prepared.error().
                                                     failure),
                                        kind);
                                if (!classified)
                                {
                                    return std::unexpected(
                                        std::move(
                                            classified.error()));
                                }
                                directive = *classified;
                                break;
                            }

                            const auto prepared_directive =
                                lifecycle_controller_.
                                ReportPrepared();
                            if (prepared_directive.kind !=
                                AsioControlDirectiveKind::
                                ContinuePump)
                            {
                                return std::unexpected(Failure(
                                    AsioFailureStage::protocol,
                                    "ASIO controller rejected prepared physical session"));
                            }

                            auto observed =
                                ObserveControllerForeground();
                            if (!observed)
                            {
                                return std::unexpected(
                                    std::move(
                                        observed.error()));
                            }
                            if (observed->directive.kind ==
                                AsioControlDirectiveKind::
                                ReleaseToSuspended)
                            {
                                directive =
                                    observed->directive;
                                break;
                            }
                            if (observed->directive.kind !=
                                AsioControlDirectiveKind::
                                ContinuePump)
                            {
                                return std::unexpected(Failure(
                                    AsioFailureStage::protocol,
                                    "ASIO controller produced an invalid pre-start directive"));
                            }

                            if (auto started =
                                    StartPreparedPhysicalSession();
                                !started)
                            {
                                auto classified =
                                    HandlePhysicalAttemptFailure(
                                        purpose,
                                        std::move(
                                            started.error()),
                                        purpose ==
                                        PhysicalSessionPurpose::
                                        FocusRecovery
                                            ? AsioPhysicalAttemptFailureKind::
                                            RetryableBeforeRunning
                                            : AsioPhysicalAttemptFailureKind::
                                            Fatal);
                                if (!classified)
                                {
                                    return std::unexpected(
                                        std::move(
                                            classified.error()));
                                }
                                directive = *classified;
                                break;
                            }

                            const auto priming_directive =
                                lifecycle_controller_.
                                ReportPrimingStarted();
                            if (priming_directive.kind !=
                                AsioControlDirectiveKind::
                                ContinuePump)
                            {
                                return std::unexpected(Failure(
                                    AsioFailureStage::protocol,
                                    "ASIO controller rejected physical-session priming"));
                            }

                            auto stable =
                                WaitForStableRender();
                            if (!stable)
                            {
                                const bool bridge_committed =
                                    presentation_bridge_ !=
                                    nullptr &&
                                    presentation_bridge_->
                                    Snapshot().
                                    running_callbacks != 0;
                                auto classified =
                                    HandlePhysicalAttemptFailure(
                                        purpose,
                                        std::move(
                                            stable.error()),
                                        bridge_committed ||
                                        purpose ==
                                        PhysicalSessionPurpose::
                                        InitialStartup
                                            ? AsioPhysicalAttemptFailureKind::
                                            Fatal
                                            : AsioPhysicalAttemptFailureKind::
                                            RetryableBeforeRunning);
                                if (!classified)
                                {
                                    return std::unexpected(
                                        std::move(
                                            classified.error()));
                                }
                                directive = *classified;
                                break;
                            }
                            if (*stable ==
                                StableRenderOutcome::shutdown)
                            {
                                directive =
                                    lifecycle_controller_.
                                    RequestShutdown();
                                break;
                            }
                            if (*stable ==
                                StableRenderOutcome::focus_lost)
                            {
                                directive = {
                                    .kind =
                                    AsioControlDirectiveKind::
                                    ReleaseToSuspended,
                                    .recovery_attempt =
                                    lifecycle_controller_.
                                    recovery_attempt(),
                                };
                                break;
                            }
                            return StableRenderOutcome::stable;
                        }

                    case AsioControlDirectiveKind::
                    ReleaseToSuspended:
                        {
                            auto released =
                                ReleasePhysicalSessionToSuspended();
                            if (!released)
                            {
                                return std::unexpected(
                                    std::move(
                                        released.error()));
                            }
                            directive = *released;
                            break;
                        }

                    case AsioControlDirectiveKind::WaitRetry:
                        {
                            auto retry =
                                WaitForRecoveryRetry(directive);
                            if (!retry)
                            {
                                return std::unexpected(
                                    std::move(retry.error()));
                            }
                            directive = *retry;
                            break;
                        }

                    case AsioControlDirectiveKind::CommitRunning:
                        return StableRenderOutcome::stable;

                    case AsioControlDirectiveKind::FailFatal:
                        return std::unexpected(Failure(
                            AsioFailureStage::protocol,
                            "ASIO physical-session controller entered Fatal without a typed failure"));

                    case AsioControlDirectiveKind::Stop:
                        return StableRenderOutcome::shutdown;
                    }
                }
            }

            std::optional<AsioFailure> MonitorCommittedRuntime() noexcept
            {
                std::uint64_t summary_started_ms =
                    actions_.tick_count_ms(actions_.context);
                for (;;)
                {
                    if (logical_clock_ == nullptr)
                    {
                        return Failure(
                            AsioFailureStage::runtime_clock,
                            "ASIO logical timeline is unavailable");
                    }
                    if (const auto advanced =
                            logical_clock_->ObserveNow(
                                actions_.time_get_time_ms(
                                    actions_.context));
                        !advanced)
                    {
                        return Failure(
                            AsioFailureStage::runtime_clock,
                            std::format(
                                "ASIO logical timeline advance failed: {}",
                                static_cast<unsigned>(
                                    advanced.error())));
                    }

                    const auto now_ms =
                        actions_.tick_count_ms(actions_.context);
                    const auto summary_remaining =
                        RemainingTimeout(
                            summary_started_ms,
                            now_ms,
                            actions_.summary_interval_ms);
                    auto wake =
                        WaitForRuntimeWake(summary_remaining);
                    if (!wake)
                    {
                        return std::move(wake.error());
                    }
                    if (*wake == RuntimeWake::shutdown)
                    {
                        const auto stopped =
                            lifecycle_controller_.
                            RequestShutdown();
                        if (stopped.kind !=
                            AsioControlDirectiveKind::Stop)
                        {
                            return Failure(
                                AsioFailureStage::protocol,
                                "ASIO controller rejected shutdown");
                        }
                        return std::nullopt;
                    }
                    if (*wake == RuntimeWake::fault ||
                        HasPublishedFault())
                    {
                        const auto fatal =
                            lifecycle_controller_.
                            ReportRuntimeFault();
                        if (fatal.kind !=
                            AsioControlDirectiveKind::
                            FailFatal)
                        {
                            return Failure(
                                AsioFailureStage::protocol,
                                "ASIO controller did not make a running fault fatal");
                        }
                        return BuildLatchedFailure();
                    }

                    auto observed =
                        ObserveControllerForeground();
                    if (!observed)
                    {
                        return std::move(observed.error());
                    }
                    if (observed->directive.kind ==
                        AsioControlDirectiveKind::
                        ReleaseToSuspended)
                    {
                        auto recovered =
                            DrivePhysicalLifecycle(
                                observed->directive);
                        if (!recovered)
                        {
                            return std::move(
                                recovered.error());
                        }
                        if (*recovered ==
                            StableRenderOutcome::shutdown)
                        {
                            return std::nullopt;
                        }
                        if (session_ == nullptr ||
                            presentation_bridge_ == nullptr ||
                            presentation_bridge_->state() !=
                            AsioPresentationBridgeState::
                            Running)
                        {
                            return Failure(
                                AsioFailureStage::runtime_clock,
                                "ASIO recovery completed without a running physical bridge");
                        }

                        SaturatingIncrementCounter(
                            session_recoveries_);
                        ReportLifecycle(
                            AsioSessionLifecycleEvent::
                            session_recovered,
                            foreground_monitor_->snapshot(),
                            active_physical_session_generation_.
                            load(std::memory_order_acquire),
                            lifecycle_controller_.
                            recovery_attempt(),
                            0,
                            presentation_bridge_->
                            Snapshot().
                            handoff_logical_tail,
                            nullptr,
                            &active_physical_session_facts_);
                    }
                    else if (observed->directive.kind !=
                        AsioControlDirectiveKind::
                        ContinuePump)
                    {
                        return Failure(
                            AsioFailureStage::protocol,
                            "ASIO controller produced an invalid running directive");
                    }

                    PublishRuntimeSummaryIfDue(
                        summary_started_ms);
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
                if (presentation_bridge_ != nullptr)
                {
                    MergeBridgeSnapshot(
                        completed_bridge_snapshot_,
                        has_completed_bridge_snapshot_,
                        presentation_bridge_->Snapshot());
                }
                std::optional<LogicalRenderLease> bridge_lease =
                    pending_bridge_lease_;
                pending_bridge_lease_.reset();
                if (presentation_bridge_ != nullptr)
                {
                    if (!presentation_bridge_->BeginQuiescing())
                    {
                        record_failure(Failure(
                            AsioFailureStage::runtime_clock,
                            "Could not quiesce the ASIO presentation bridge after callbacks stopped"));
                    }
                    if (!bridge_lease)
                    {
                        const auto released =
                            presentation_bridge_->ReleaseLease(
                                logical_render_stream_ != nullptr
                                    ? logical_render_stream_->
                                    committed_tail()
                                    : 0);
                        if (!released)
                        {
                            record_failure(Failure(
                                AsioFailureStage::runtime_clock,
                                std::format(
                                    "Could not release the ASIO presentation bridge lease: {}",
                                    static_cast<unsigned>(
                                        released.error()))));
                        }
                        else
                        {
                            bridge_lease = *released;
                        }
                    }
                }
                if (bridge_lease)
                {
                    if (logical_render_stream_ == nullptr)
                    {
                        record_failure(Failure(
                            AsioFailureStage::runtime_clock,
                            "ASIO bridge lease survived without its logical render stream"));
                    }
                    else
                    {
                        const auto tail =
                            logical_render_stream_->
                            committed_tail();
                        const auto transferred =
                            logical_render_stream_->Transfer(
                                *bridge_lease,
                                LogicalRenderOwner::Pump,
                                tail);
                        if (!transferred)
                        {
                            record_failure(Failure(
                                AsioFailureStage::runtime_clock,
                                std::format(
                                    "Could not return the ASIO logical render lease to the pump: {}",
                                    static_cast<unsigned>(
                                        transferred.error()))));
                        }
                        else
                        {
                            pump_lease_ = *transferred;
                        }
                    }
                }
                if (logical_render_stream_ != nullptr &&
                    !pump_lease_)
                {
                    record_failure(Failure(
                        AsioFailureStage::runtime_clock,
                        "ASIO physical-session close stranded the logical render lease"));
                }
                active_physical_session_generation_.store(
                    0, std::memory_order_release);
                presentation_bridge_.reset();
                physical_float_output_.clear();
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
                    final_logical_timeline_generation_ =
                        logical_clock_->info().timeline_generation;
                    const auto final_frame =
                        logical_clock_->WholeFrameAt(
                            actions_.time_get_time_ms(
                                actions_.context));
                    if (final_frame)
                    {
                        final_logical_current_frame_ =
                            *final_frame;
                    }
                    if (logical_render_stream_ != nullptr)
                    {
                        final_logical_render_tail_ =
                            logical_render_stream_->committed_tail();
                    }
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
                pump_lease_.reset();
                pending_bridge_lease_.reset();
                logical_render_stream_.reset();
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
                // Focus is the only suspension authority. Logical time merely
                // determines how far the single sequential pump must render.
                if (logical_clock_ == nullptr ||
                    logical_render_stream_ == nullptr ||
                    render_core_ == nullptr)
                {
                    return Failure(
                        AsioFailureStage::runtime_clock,
                        "ASIO logical timeline is unavailable during logical pump rendering");
                }
                if (!pump_lease_)
                {
                    return std::nullopt;
                }
                const auto now_ms =
                    actions_.time_get_time_ms(actions_.context);
                if (const auto advanced = logical_clock_->ObserveNow(now_ms);
                    !advanced)
                {
                    return Failure(
                        AsioFailureStage::runtime_clock,
                        std::format(
                            "ASIO logical pump timeline advance failed: {}",
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
                            "ASIO logical pump projection failed: {}",
                            static_cast<unsigned>(projected.error())));
                }
                if (*projected >
                    (std::numeric_limits<std::uint64_t>::max)() -
                    request_.buffer_frames)
                {
                    return Failure(
                        AsioFailureStage::runtime_clock,
                        "ASIO logical pump render target overflowed");
                }
                const auto target =
                    *projected + request_.buffer_frames;
                for (std::uint32_t block_index = 0;
                     block_index <
                     kMaximumPrimingRenderBlocksPerCallback &&
                     logical_render_stream_->committed_tail() <= target;
                     ++block_index)
                {
                    const auto plan =
                        logical_render_stream_->BeginRender(
                            *pump_lease_);
                    if (!plan)
                    {
                        if (plan.error() ==
                            LogicalRenderFailure::Busy)
                        {
                            return std::nullopt;
                        }
                        return Failure(
                            AsioFailureStage::runtime_clock,
                            std::format(
                                "ASIO logical pump planning failed: {}",
                                static_cast<unsigned>(
                                    plan.error())));
                    }
                    if (plan->timeline.discontinuity_frames != 0)
                    {
                        static_cast<void>(
                            logical_render_stream_->Abandon(*plan));
                        return Failure(
                            AsioFailureStage::runtime_clock,
                            "ASIO logical pump planned a discontinuity");
                    }
                    const auto rendered =
                        logical_render_stream_->Render(*plan);
                    render_diagnostics_.RecordRender(rendered);
                    if (!logical_render_stream_->Commit(*plan))
                    {
                        return Failure(
                            AsioFailureStage::runtime_clock,
                            "ASIO logical pump commit failed");
                    }
                    SaturatingAddCounter(
                        sequential_pump_rendered_frames_,
                        request_.buffer_frames);
                }
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
                if (presentation_bridge_ == nullptr ||
                    physical_float_output_.size() !=
                    static_cast<std::size_t>(
                        request_.buffer_frames) *
                    2)
                {
                    ClearAsioBlock(request.buffer_index);
                    LatchRuntimeFault(
                        AsioFailureStage::runtime_clock);
                    return;
                }

                const auto processed =
                    presentation_bridge_->Process(
                        request,
                        physical_float_output_);
                if (processed.first_fault !=
                    AsioPresentationBridgeFault::None ||
                    processed.output_frames !=
                    request_.buffer_frames)
                {
                    ClearAsioBlock(request.buffer_index);
                    LatchRuntimeFault(
                        AsioFailureStage::runtime_clock);
                    return;
                }

                auto proof_callbacks =
                    physical_stability_proof_callbacks_.load(
                        std::memory_order_relaxed);
                if (proof_callbacks < 3)
                {
                    ++proof_callbacks;
                    physical_stability_proof_callbacks_.store(
                        proof_callbacks,
                        std::memory_order_release);
                }
                const auto index = static_cast<std::size_t>(request.buffer_index);
                const auto conversion = ConvertFloatStereoToAsio(
                    physical_float_output_,
                    channel_types_,
                    {
                        driver_buffers_[0][index],
                        driver_buffers_[1][index],
                    });
                const AudioRenderBlock bridge_output{
                    .interleaved_stereo =
                    physical_float_output_,
                    .mixer_result = MA_SUCCESS,
                    .frames_read = request_.buffer_frames,
                    .active_voices = 0,
                    .missing_frames = 0,
                    .silence_reason = processed.audible
                                          ? AudioRenderSilenceReason::none
                                          : AudioRenderSilenceReason::
                                          no_active_voice,
                    .silence_substituted =
                    !processed.audible,
                };
                render_diagnostics_.RecordConversion(
                    bridge_output, conversion);
                if (!conversion.converted)
                {
                    ClearAsioBlock(request.buffer_index);
                    LatchRuntimeFault(AsioFailureStage::conversion);
                    return;
                }
                if (!CallOutputReady())
                {
                    ClearAsioBlock(request.buffer_index);
                    return;
                }
                if (proof_callbacks >= 3 ||
                    processed.state ==
                    AsioPresentationBridgeState::Running)
                {
                    actions_.signal_event(
                        actions_.context,
                        stable_render_event_);
                }
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
                    MergeCallbackSnapshot(
                        callback,
                        callback_runtime_->Snapshot());
                }

                auto bridge = completed_bridge_snapshot_;
                auto has_bridge = has_completed_bridge_snapshot_;
                if (presentation_bridge_ != nullptr)
                {
                    MergeBridgeSnapshot(
                        bridge,
                        has_bridge,
                        presentation_bridge_->Snapshot());
                }

                const auto timeline = logical_clock_ != nullptr
                                          ? logical_clock_->counters()
                                          : has_final_exact_clock_counters_
                                          ? final_exact_clock_counters_
                                          : ExactJudgementTimelineCounters{};
                const auto render = render_diagnostics_.Snapshot();
                auto logical_timeline_generation =
                    final_logical_timeline_generation_;
                auto logical_current_frame =
                    final_logical_current_frame_;
                auto logical_render_tail =
                    final_logical_render_tail_;
                if (logical_clock_ != nullptr)
                {
                    logical_timeline_generation =
                        logical_clock_->info().
                                        timeline_generation;
                    const auto current_frame =
                        logical_clock_->WholeFrameAt(
                            actions_.time_get_time_ms(
                                actions_.context));
                    if (current_frame)
                    {
                        logical_current_frame =
                            *current_frame;
                    }
                }
                if (logical_render_stream_ != nullptr)
                {
                    logical_render_tail =
                        logical_render_stream_->
                        committed_tail();
                }

                const auto phase_frames_to_nanoseconds =
                    [this](const double frames) noexcept
                {
                    return logical_output_sample_rate_ == 0
                               ? 0.0
                               : frames * 1'000'000'000.0 /
                               static_cast<double>(
                                   logical_output_sample_rate_);
                };

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
                    .latency_change_requests =
                    callback.latency_change_requests,
                    .buffer_size_change_requests =
                    callback.buffer_size_change_requests,
                    .sample_rate_change_requests =
                    callback.sample_rate_change_requests,
                    .foreground_losses =
                    foreground_losses_.load(
                        std::memory_order_relaxed),
                    .consumed_focus_loss_generation =
                    consumed_focus_loss_generation_.load(
                        std::memory_order_relaxed),
                    .logical_timeline_generation =
                    logical_timeline_generation,
                    .logical_sample_rate =
                    logical_output_sample_rate_,
                    .logical_current_frame =
                    logical_current_frame,
                    .logical_render_tail =
                    logical_render_tail,
                    .physical_session_generation =
                    physical_session_generation_,
                    .physical_sample_rate =
                    physical_contract_established_
                        ? logical_contract_.sample_rate
                        : 0,
                    .physical_period_frames =
                    physical_contract_established_
                        ? logical_contract_.period_frames
                        : 0,
                    .physical_output_latency_frames =
                    physical_contract_established_
                        ? logical_contract_.
                        output_latency_frames
                        : 0,
                    .lifecycle_state =
                    lifecycle_controller_.state(),
                    .session_releases =
                    session_releases_.load(
                        std::memory_order_relaxed),
                    .recovery_attempts =
                    recovery_attempts_.load(
                        std::memory_order_relaxed),
                    .recovery_failures =
                    recovery_failures_.load(
                        std::memory_order_relaxed),
                    .session_recoveries =
                    session_recoveries_.load(
                        std::memory_order_relaxed),
                    .sequential_pump_rendered_frames =
                    sequential_pump_rendered_frames_.load(
                        std::memory_order_relaxed),
                    .bridge_callbacks =
                    has_bridge ? bridge.callbacks : 0,
                    .bridge_priming_callbacks =
                    has_bridge
                        ? bridge.priming_callbacks
                        : 0,
                    .bridge_running_callbacks =
                    has_bridge
                        ? bridge.running_callbacks
                        : 0,
                    .bridge_handoff_logical_tail =
                    has_bridge
                        ? bridge.handoff_logical_tail
                        : 0,
                    .bridge_logical_rendered_frames =
                    has_bridge
                        ? bridge.logical_rendered_frames
                        : 0,
                    .bridge_initial_phase_error_frames =
                    has_bridge
                        ? bridge.initial_phase_error_frames
                        : 0.0,
                    .bridge_maximum_absolute_phase_error_frames =
                    has_bridge
                        ? bridge.
                        maximum_absolute_phase_error_frames
                        : 0.0,
                    .bridge_final_phase_error_frames =
                    has_bridge
                        ? bridge.final_phase_error_frames
                        : 0.0,
                    .bridge_initial_phase_error_ns =
                    has_bridge
                        ? phase_frames_to_nanoseconds(
                            bridge.initial_phase_error_frames)
                        : 0.0,
                    .bridge_maximum_absolute_phase_error_ns =
                    has_bridge
                        ? phase_frames_to_nanoseconds(
                            bridge.
                            maximum_absolute_phase_error_frames)
                        : 0.0,
                    .bridge_final_phase_error_ns =
                    has_bridge
                        ? phase_frames_to_nanoseconds(
                            bridge.final_phase_error_frames)
                        : 0.0,
                    .bridge_minimum_rate_ratio_ppm =
                    has_bridge
                        ? bridge.minimum_rate_ratio_ppm
                        : 0.0,
                    .bridge_maximum_rate_ratio_ppm =
                    has_bridge
                        ? bridge.maximum_rate_ratio_ppm
                        : 0.0,
                    .bridge_final_rate_ratio_ppm =
                    has_bridge
                        ? bridge.final_rate_ratio_ppm
                        : 0.0,
                    .bridge_input_high_water_frames =
                    has_bridge
                        ? bridge.input_high_water_frames
                        : 0,
                    .bridge_input_underflows =
                    has_bridge
                        ? bridge.input_underflows
                        : 0,
                    .bridge_input_overflows =
                    has_bridge
                        ? bridge.input_overflows
                        : 0,
                    .bridge_conversion_failures =
                    has_bridge
                        ? bridge.conversion_failures
                        : 0,
                    .bridge_phase_envelope_violations =
                    has_bridge
                        ? bridge.phase_envelope_violations
                        : 0,
                    .bridge_non_finite_output_blocks =
                    has_bridge
                        ? bridge.non_finite_output_blocks
                        : 0,
                    .expected_period_ns = callback.expected_period_ns,
                    .callback_interval_samples =
                    callback.callback_interval_samples,
                    .total_callback_interval_ticks =
                    callback.total_callback_interval_ticks,
                    .maximum_callback_interval_ticks =
                    callback.maximum_callback_interval_ticks,
                    .early_callback_intervals =
                    callback.early_callback_intervals,
                    .late_callback_intervals =
                    callback.late_callback_intervals,
                    .severe_callback_intervals =
                    callback.severe_callback_intervals,
                    .timed_callback_work_samples =
                    callback.timed_callback_work_samples,
                    .total_callback_ticks =
                    callback.total_callback_ticks,
                    .maximum_callback_ticks =
                    callback.maximum_callback_ticks,
                    .timed_render_work_samples =
                    callback.timed_render_work_samples,
                    .total_render_ticks =
                    callback.total_render_ticks,
                    .maximum_render_ticks =
                    callback.maximum_render_ticks,
                    .driver_interval_samples =
                    callback.driver_interval_samples,
                    .maximum_driver_period_error_ns =
                    callback.maximum_driver_period_error_ns,
                    .maximum_host_driver_interval_skew_ns =
                    callback.maximum_host_driver_interval_skew_ns,
                    .buffer_alternation_violations =
                    callback.buffer_alternation_violations,
                    .no_active_voice_silence_blocks =
                    render.no_active_voice_silence_blocks,
                    .active_short_read_blocks =
                    render.active_short_read_blocks,
                    .mixer_error_blocks =
                    render.mixer_error_blocks,
                    .render_contract_error_blocks =
                    render.render_contract_error_blocks,
                    .short_read_missing_frames =
                    render.short_read_missing_frames,
                    .first_mixer_error =
                    render.first_mixer_error,
                    .clipped_output_blocks =
                    render.clipped_output_blocks,
                    .clipped_output_samples =
                    render.clipped_output_samples,
                    .zero_output_blocks_with_active_voice =
                    render.zero_output_blocks_with_active_voice,
                    .zero_output_blocks_without_active_voice =
                    render.zero_output_blocks_without_active_voice,
                    .non_finite_output_blocks =
                    render.non_finite_output_blocks,
                    .maximum_absolute_output_sample =
                    render.maximum_absolute_output_sample,
                    .qpc_frequency = callback.qpc_frequency,
                    .judgement_timeline_resolved_queries =
                    timeline.resolved_queries,
                    .judgement_timeline_pending_queries =
                    timeline.pending_queries,
                    .judgement_timeline_temporarily_unavailable_queries =
                    timeline.temporarily_unavailable_queries,
                    .judgement_timeline_history_lost_queries =
                    timeline.history_lost_queries,
                    .judgement_timeline_discontinuous_queries =
                    timeline.discontinuous_queries,
                    .pending_cursor_queries =
                    pending_cursor_queries_.load(
                        std::memory_order_relaxed),
                    .unmapped_cursor_failures =
                    unmapped_cursor_failures_.load(
                        std::memory_order_relaxed),
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
            AsioPhysicalSessionController lifecycle_controller_;
            std::unique_ptr<AsioSession> session_;
            std::unique_ptr<AsioCallbackRuntime> callback_runtime_;
            std::unique_ptr<AsioPresentationBridge>
            presentation_bridge_;
            std::vector<float> physical_float_output_;
            std::unique_ptr<AudioRenderCore> render_core_;
            std::unique_ptr<LogicalRenderStream>
            logical_render_stream_;
            std::optional<LogicalRenderLease> pump_lease_;
            std::optional<LogicalRenderLease>
            pending_bridge_lease_;
            std::shared_ptr<LogicalPresentationClock> logical_clock_;
            ExactJudgementTimelineCounters final_exact_clock_counters_{};
            bool has_final_exact_clock_counters_{};
            std::uint64_t final_logical_timeline_generation_{};
            std::uint64_t final_logical_current_frame_{};
            std::uint64_t final_logical_render_tail_{};
            bool exact_clock_registered_{};
            std::uint64_t logical_timeline_generation_{};
            std::uint64_t physical_session_generation_{};
            AsioLogicalOutputContract logical_contract_{};
            PhysicalSessionFacts active_physical_session_facts_{};
            ClosedPhysicalSessionFacts last_closed_physical_session_facts_{};
            std::array<ASIOSampleType, 2> channel_types_{};
            std::array<std::array<std::span<std::byte>, 2>, 2> driver_buffers_{};
            AsioCallbackRuntimeSnapshot completed_callback_snapshot_{};
            AsioPresentationBridgeSnapshot completed_bridge_snapshot_{};
            bool has_completed_bridge_snapshot_{};

            std::atomic_uint32_t endpoint_buffer_frames_{};
            std::atomic_uint32_t output_sample_rate_{};
            AsioRenderDiagnostics render_diagnostics_;
            std::atomic_uint64_t foreground_losses_{};
            std::atomic_uint64_t consumed_focus_loss_generation_{};
            std::atomic_uint64_t session_releases_{};
            std::atomic_uint64_t recovery_attempts_{};
            std::atomic_uint64_t recovery_failures_{};
            std::atomic_uint64_t session_recoveries_{};
            std::atomic_uint64_t sequential_pump_rendered_frames_{};
            std::atomic_uint64_t pending_cursor_queries_{};
            std::atomic_uint64_t unmapped_cursor_failures_{};
            std::atomic_bool first_fault_claimed_{};
            std::atomic<std::uint8_t> first_fault_stage_{};
            std::atomic<std::uint8_t> first_fault_domain_{};
            std::atomic<std::int64_t> first_fault_result_{};
            std::atomic_uint64_t active_physical_session_generation_{};
            std::atomic_uint32_t physical_stability_proof_callbacks_{};
            std::uint32_t logical_output_sample_rate_{};
            bool physical_contract_established_{};
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

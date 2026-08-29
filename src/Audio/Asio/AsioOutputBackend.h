#pragma once
// SPDX-License-Identifier: CC0-1.0

#include "Audio/Asio/AsioDriver.h"
#include "Audio/Asio/AsioDriverCatalog.h"
#include "Audio/Asio/AsioPhysicalSessionController.h"
#include "Audio/Asio/AsioTypes.h"
#include "Audio/DirectSound/DirectSoundFacade.h"

#include <Windows.h>

#include <cstdint>
#include <memory>
#include <optional>

namespace gc::audio
{
    struct AsioRuntimeCountersSnapshot
    {
        std::uint64_t callbacks{};
        std::uint64_t time_info_callbacks{};
        std::uint64_t legacy_callbacks{};
        std::uint64_t deferred_callbacks{};
        std::uint64_t deadline_misses{};
        std::uint64_t silence_substitutions{};
        std::uint64_t overload_messages{};
        std::uint64_t reset_requests{};
        std::uint64_t resync_requests{};
        std::uint64_t latency_change_requests{};
        std::uint64_t buffer_size_change_requests{};
        std::uint64_t sample_rate_change_requests{};
        std::uint64_t foreground_losses{};
        std::uint64_t consumed_focus_loss_generation{};
        std::uint64_t logical_timeline_generation{};
        std::uint32_t logical_sample_rate{};
        std::uint64_t logical_current_frame{};
        std::uint64_t logical_render_tail{};
        std::uint64_t physical_session_generation{};
        std::uint32_t physical_sample_rate{};
        std::uint32_t physical_period_frames{};
        std::uint32_t physical_output_latency_frames{};
        AsioLifecycleState lifecycle_state{AsioLifecycleState::Starting};
        std::uint64_t session_releases{};
        std::uint64_t recovery_attempts{};
        std::uint64_t recovery_failures{};
        std::uint64_t session_recoveries{};
        std::uint64_t sequential_pump_rendered_frames{};
        std::uint64_t bridge_callbacks{};
        std::uint64_t bridge_priming_callbacks{};
        std::uint64_t bridge_running_callbacks{};
        std::uint64_t bridge_handoff_logical_tail{};
        std::uint64_t bridge_logical_rendered_frames{};
        double bridge_initial_phase_error_frames{};
        double bridge_maximum_absolute_phase_error_frames{};
        double bridge_final_phase_error_frames{};
        double bridge_initial_phase_error_ns{};
        double bridge_maximum_absolute_phase_error_ns{};
        double bridge_final_phase_error_ns{};
        double bridge_minimum_rate_ratio_ppm{};
        double bridge_maximum_rate_ratio_ppm{};
        double bridge_final_rate_ratio_ppm{};
        std::uint64_t bridge_input_high_water_frames{};
        std::uint64_t bridge_input_underflows{};
        std::uint64_t bridge_input_overflows{};
        std::uint64_t bridge_conversion_failures{};
        std::uint64_t bridge_phase_envelope_violations{};
        std::uint64_t bridge_non_finite_output_blocks{};
        std::uint64_t expected_period_ns{};
        std::uint64_t callback_interval_samples{};
        std::uint64_t total_callback_interval_ticks{};
        std::uint64_t maximum_callback_interval_ticks{};
        std::uint64_t early_callback_intervals{};
        std::uint64_t late_callback_intervals{};
        std::uint64_t severe_callback_intervals{};
        std::uint64_t timed_callback_work_samples{};
        std::uint64_t total_callback_ticks{};
        std::uint64_t maximum_callback_ticks{};
        std::uint64_t timed_render_work_samples{};
        std::uint64_t total_render_ticks{};
        std::uint64_t maximum_render_ticks{};
        std::uint64_t driver_interval_samples{};
        std::uint64_t maximum_driver_period_error_ns{};
        std::uint64_t maximum_host_driver_interval_skew_ns{};
        std::uint64_t buffer_alternation_violations{};
        std::uint64_t no_active_voice_silence_blocks{};
        std::uint64_t active_short_read_blocks{};
        std::uint64_t mixer_error_blocks{};
        std::uint64_t render_contract_error_blocks{};
        std::uint64_t short_read_missing_frames{};
        ma_result first_mixer_error{MA_SUCCESS};
        std::uint64_t clipped_output_blocks{};
        std::uint64_t clipped_output_samples{};
        std::uint64_t zero_output_blocks_with_active_voice{};
        std::uint64_t zero_output_blocks_without_active_voice{};
        std::uint64_t non_finite_output_blocks{};
        float maximum_absolute_output_sample{};
        std::uint64_t qpc_frequency{};
        std::uint64_t judgement_timeline_resolved_queries{};
        std::uint64_t judgement_timeline_pending_queries{};
        std::uint64_t judgement_timeline_temporarily_unavailable_queries{};
        std::uint64_t judgement_timeline_history_lost_queries{};
        std::uint64_t judgement_timeline_discontinuous_queries{};
        std::uint64_t pending_cursor_queries{};
        std::uint64_t unmapped_cursor_failures{};
        MixerDiagnosticsSnapshot mixer{};
    };

    enum class AsioPhysicalSessionReason : std::uint8_t
    {
        startup,
        focus_recovery,
    };

    enum class AsioSessionLifecycleEvent : std::uint8_t
    {
        foreground_lost,
        session_released,
        foreground_regained,
        recovery_attempt_started,
        recovery_attempt_failed,
        physical_session_started,
        session_recovered,
    };

    struct AsioLogicalBackendRecord final
    {
        std::uint32_t origin_raw_ms{};
        std::uint64_t origin_unwrapped_ms{};
        std::uint64_t origin_presented_frame{};
        std::uint64_t timeline_generation{};
        std::uint32_t sample_rate{};
        std::uint32_t period_frames{};
        std::uint32_t timestamp_quantum_ns{};
        bool alternate_backend_selected{};
    };

    struct AsioSessionLifecycleRecord final
    {
        AsioSessionLifecycleEvent event{};
        bool foreground{};
        std::uint64_t focus_loss_generation{};
        std::uint64_t physical_session_generation{};
        std::uint64_t recovery_attempt{};
        std::uint32_t retry_delay_ms{};
        std::uint64_t handoff_logical_tail{};
        AsioPhysicalSessionReason reason{};
        double observed_sample_rate{};
        double active_sample_rate{};
        bool frozen_rate_requested{};
        bool sample_rate_changed{};
        bool restoration_attempted{};
        bool restoration_succeeded{};
        std::uint64_t silent_priming_callbacks{};
        bool callback_quiesced{};
        bool buffers_disposed{};
    };

    class IAsioOutputObserver
    {
    public:
        virtual ~IAsioOutputObserver() = default;
        virtual void StartupSucceeded(
            const AsioCapabilityReport&,
            const AsioLogicalBackendRecord&) noexcept = 0;
        virtual void RuntimeSummary(
            const AsioRuntimeCountersSnapshot&) noexcept = 0;
        virtual void RuntimeFailed(
            const AsioFailure&,
            const AsioRuntimeCountersSnapshot&) noexcept = 0;

        virtual void SessionLifecycleChanged(
            const AsioSessionLifecycleRecord&,
            const AsioFailure*) noexcept
        {
        }
    };

    class AsioOutputBackend;

    namespace detail
    {
        class AsioOutputBackendState;
        struct AsioOutputBackendActions;
        [[nodiscard]] std::unique_ptr<AsioOutputBackend>
        StartAsioOutputBackendAndWait(
            HWND,
            const AsioStreamRequest&,
            std::unique_ptr<IAsioRegistrySource>,
            std::unique_ptr<IAsioDriverFactory>,
            std::shared_ptr<IAsioOutputObserver>,
            std::shared_ptr<const ma_allocation_callbacks>,
            DWORD,
            bool,
            const AsioOutputBackendActions&,
            AsioFailure*) noexcept;
    } // namespace detail

    class AsioOutputBackend final : public IAudioEngineServices
    {
    public:
        static std::unique_ptr<AsioOutputBackend> StartAndWait(
            HWND game_window,
            const AsioStreamRequest&,
            std::unique_ptr<IAsioRegistrySource>,
            std::unique_ptr<IAsioDriverFactory>,
            std::shared_ptr<IAsioOutputObserver>,
            std::shared_ptr<const ma_allocation_callbacks>,
            DWORD startup_clock_timeout_ms,
            bool enable_absolute_time_judgement,
            AsioFailure*) noexcept;
        ~AsioOutputBackend() override;

        AsioOutputBackend(const AsioOutputBackend&) = delete;
        AsioOutputBackend& operator=(const AsioOutputBackend&) = delete;

        std::unique_ptr<MixerVoice> CreateVoice(
            const NormalizedSourceFormat&,
            std::shared_ptr<AudioSnapshot>,
            std::shared_ptr<AudioCursorTimeline>,
            VoiceUsage,
            ma_result*) noexcept override;
        std::optional<std::uint64_t>
        CurrentOutputFrame() noexcept override;
        [[nodiscard]] std::uint32_t endpoint_buffer_frames() const noexcept override;
        [[nodiscard]] std::uint32_t output_sample_rate() const noexcept override;
        void CountPendingCursorQuery() noexcept override;
        void CountUnmappedCursorFailure() noexcept override;

    private:
        friend std::unique_ptr<AsioOutputBackend>
        detail::StartAsioOutputBackendAndWait(
            HWND,
            const AsioStreamRequest&,
            std::unique_ptr<IAsioRegistrySource>,
            std::unique_ptr<IAsioDriverFactory>,
            std::shared_ptr<IAsioOutputObserver>,
            std::shared_ptr<const ma_allocation_callbacks>,
            DWORD,
            bool,
            const detail::AsioOutputBackendActions&,
            AsioFailure*) noexcept;

        explicit AsioOutputBackend(
            std::unique_ptr<detail::AsioOutputBackendState>) noexcept;

        std::unique_ptr<detail::AsioOutputBackendState> state_;
    };
} // namespace gc::audio

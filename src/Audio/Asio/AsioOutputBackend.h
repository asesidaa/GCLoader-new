#pragma once
// SPDX-License-Identifier: CC0-1.0

#include "Audio/Asio/AsioDriver.h"
#include "Audio/Asio/AsioDriverCatalog.h"
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
        std::uint64_t sample_position_discontinuities{};
        std::uint64_t render_gap_frames{};
        std::uint64_t foreground_losses{};
        std::uint64_t consumed_focus_loss_generation{};
        std::uint64_t physical_session_generation{};
        std::uint64_t session_releases{};
        std::uint64_t recovery_attempts{};
        std::uint64_t recovery_failures{};
        std::uint64_t session_recoveries{};
        std::uint64_t silent_advance_frames{};
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
        std::uint64_t exact_anchor_publications{};
        std::uint64_t detached_exact_anchor_publications{};
        std::uint64_t exact_resolved_queries{};
        std::uint64_t exact_pending_queries{};
        std::uint64_t exact_temporarily_unavailable_queries{};
        std::uint64_t exact_history_lost_queries{};
        std::uint64_t exact_discontinuous_queries{};
        std::uint64_t pending_cursor_queries{};
        std::uint64_t unmapped_cursor_failures{};
        MixerDiagnosticsSnapshot mixer{};
    };

    enum class AsioSessionLifecycleEvent : std::uint8_t
    {
        foreground_lost,
        session_released,
        foreground_regained,
        recovery_attempt_started,
        recovery_attempt_failed,
        session_recovered,
    };

    struct AsioSessionLifecycleRecord final
    {
        AsioSessionLifecycleEvent event{};
        bool foreground{};
        std::uint64_t focus_loss_generation{};
        std::uint64_t physical_session_generation{};
        std::uint64_t recovery_attempt{};
        std::uint32_t retry_delay_ms{};
        std::uint64_t logical_render_origin{};
        std::uint64_t physical_render_origin{};
    };

    class IAsioOutputObserver
    {
    public:
        virtual ~IAsioOutputObserver() = default;
        virtual void StartupSucceeded(
            const AsioCapabilityReport&) noexcept = 0;
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

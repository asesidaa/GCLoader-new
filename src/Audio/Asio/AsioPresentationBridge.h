#pragma once

#include "Audio/Asio/AsioCallbackRuntime.h"
#include "Audio/Asio/AsioClock.h"
#include "Audio/Asio/AsioPresentationRateMatcher.h"
#include "Audio/Logical/LogicalPresentationClock.h"
#include "Audio/Mixer/LogicalRenderStream.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>

namespace gc::audio
{
    inline constexpr double kMaximumRateCorrectionPpm = 1'000.0;
    inline constexpr double kMaximumRateSlewPpmPerCallback = 25.0;
    inline constexpr std::uint32_t kPhaseFilterCallbacks = 32;
    inline constexpr std::uint32_t
    kPhaseCorrectionHorizonSeconds = 2;
    inline constexpr std::uint32_t kMinimumPhaseEnvelopeMs = 20;
    inline constexpr std::uint32_t
    kMaximumPrimingRenderBlocksPerCallback = 8;

    enum class AsioPresentationBridgeState : std::uint8_t
    {
        Priming,
        Armed,
        Running,
        Quiescing,
        Faulted,
    };

    enum class AsioPresentationBridgeFault : std::uint8_t
    {
        None,
        InvalidConfiguration,
        InvalidOutputBuffer,
        InvalidCallback,
        InvalidClock,
        ArithmeticOverflow,
        ConcurrentAccess,
        RateControlFailure,
        InputStarvation,
        InputOverflow,
        ConversionFailure,
        LostRenderLease,
        RenderDiscontinuity,
        RenderFailure,
        RenderCommitFailure,
        PhaseEnvelopeViolation,
        NonFiniteOutput,
    };

    enum class AsioPresentationBridgeControlFailure : std::uint8_t
    {
        Busy,
        InvalidState,
        InvalidLease,
        TailMismatch,
        MatcherResetFailed,
    };

    struct AsioPresentationBridgeConfig final
    {
        std::uint64_t physical_session_generation{};
        std::uint32_t logical_rate{};
        std::uint32_t driver_rate{};
        std::uint32_t period_frames{};
        std::uint32_t driver_output_latency_frames{};
        std::uint32_t timestamp_quantum_ns{};
    };

    struct AsioPresentationProcessResult final
    {
        AsioPresentationBridgeState state{
            AsioPresentationBridgeState::Faulted
        };
        AsioPresentationBridgeFault first_fault{
            AsioPresentationBridgeFault::InvalidConfiguration
        };
        std::uint32_t output_frames{};
        bool audible{};
    };

    struct AsioPresentationBridgeSnapshot final
    {
        AsioPresentationBridgeState state{
            AsioPresentationBridgeState::Faulted
        };
        AsioPresentationBridgeFault first_fault{
            AsioPresentationBridgeFault::InvalidConfiguration
        };
        std::uint64_t physical_session_generation{};
        std::uint64_t callbacks{};
        std::uint64_t priming_callbacks{};
        std::uint64_t running_callbacks{};
        std::uint64_t handoff_logical_tail{};
        std::uint64_t logical_rendered_frames{};
        std::uint64_t input_high_water_frames{};
        std::uint64_t input_underflows{};
        std::uint64_t input_overflows{};
        std::uint64_t conversion_failures{};
        std::uint64_t phase_envelope_violations{};
        std::uint64_t non_finite_output_blocks{};
        std::uint64_t resampler_input_latency_frames{};
        std::uint64_t resampler_output_latency_frames{};
        std::uint64_t phase_envelope_frames{};
        double initial_phase_error_frames{};
        double maximum_absolute_phase_error_frames{};
        double final_phase_error_frames{};
        double minimum_rate_ratio_ppm{};
        double maximum_rate_ratio_ppm{};
        double final_rate_ratio_ppm{};
    };

    class AsioPresentationBridge final
    {
    public:
        [[nodiscard]] static std::unique_ptr<AsioPresentationBridge>
        Create(
            const AsioPresentationBridgeConfig& config,
            std::shared_ptr<const LogicalPresentationClock>
            logical_clock,
            LogicalRenderStream& logical_render_stream,
            std::shared_ptr<const ma_allocation_callbacks>
            allocation_callbacks = {}) noexcept;

        [[nodiscard]]
        std::expected<void, AsioPresentationBridgeControlFailure>
        Arm(
            const LogicalRenderLease& lease,
            std::uint64_t exact_tail) noexcept;
        [[nodiscard]] bool BeginQuiescing() noexcept;
        [[nodiscard]]
        std::expected<std::optional<LogicalRenderLease>,
                      AsioPresentationBridgeControlFailure>
        ReleaseLease(std::uint64_t exact_tail) noexcept;

        [[nodiscard]] AsioPresentationProcessResult Process(
            const AsioRenderRequest& request,
            std::span<float> interleaved_stereo_output) noexcept;
        [[nodiscard]] AsioPresentationBridgeSnapshot
        Snapshot() const noexcept;
        [[nodiscard]] AsioPresentationBridgeState state() const noexcept;

    private:
        AsioPresentationBridge(
            const AsioPresentationBridgeConfig& config,
            std::shared_ptr<const LogicalPresentationClock>
            logical_clock,
            LogicalRenderStream& logical_render_stream,
            std::unique_ptr<AsioPresentationRateMatcher>
            rate_matcher,
            std::uint64_t phase_envelope_frames) noexcept;

        [[nodiscard]] bool TryAcquireClaim() noexcept;
        void ReleaseClaim() noexcept;
        void LatchFault(AsioPresentationBridgeFault fault) noexcept;
        [[nodiscard]] bool ValidateAndProjectTarget(
            const AsioRenderRequest& request,
            double* target_source_phase) noexcept;
        [[nodiscard]] bool ProcessArmed(
            double target_source_phase,
            std::span<float> output) noexcept;
        [[nodiscard]] bool ProduceRunningPeriod(
            double target_source_phase,
            std::span<float> output,
            bool commit_running) noexcept;
        [[nodiscard]] bool EnsureInputForOutput(
            std::uint64_t output_frames) noexcept;
        [[nodiscard]] bool RenderOneLogicalBlock() noexcept;
        [[nodiscard]] bool ApplyRateControl(
            double phase_error_frames) noexcept;
        void RecordPhaseError(double phase_error_frames) noexcept;
        void RecordRateRatio(double rate_ratio) noexcept;
        void RecordInputHighWater() noexcept;
        [[nodiscard]] AsioPresentationProcessResult
        Result(bool audible) const noexcept;

        const AsioPresentationBridgeConfig config_;
        std::shared_ptr<const LogicalPresentationClock> logical_clock_;
        LogicalRenderStream& logical_render_stream_;
        std::unique_ptr<AsioPresentationRateMatcher> rate_matcher_;
        AsioClockTracker physical_clock_;
        const std::uint64_t phase_envelope_frames_;

        std::atomic_bool process_claim_{};
        std::atomic_uint8_t state_{
            static_cast<std::uint8_t>(
                AsioPresentationBridgeState::Priming)
        };
        std::atomic_uint8_t first_fault_{
            static_cast<std::uint8_t>(
                AsioPresentationBridgeFault::None)
        };

        std::optional<LogicalRenderLease> lease_;
        std::uint64_t previous_system_time_ns_{};
        bool has_previous_system_time_{};
        double source_phase_{};
        double current_rate_ratio_{1.0};
        std::array<double, kPhaseFilterCallbacks>
        phase_filter_{};
        std::uint32_t phase_filter_count_{};
        std::uint32_t phase_filter_index_{};
        double phase_filter_sum_{};
        bool has_phase_error_{};
        double initial_phase_error_{};
        double maximum_absolute_phase_error_{};
        double final_phase_error_{};
        double minimum_rate_ratio_ppm_{};
        double maximum_rate_ratio_ppm_{};
        double final_rate_ratio_ppm_{};

        std::atomic_uint64_t callbacks_{};
        std::atomic_uint64_t priming_callbacks_{};
        std::atomic_uint64_t running_callbacks_{};
        std::atomic_uint64_t handoff_logical_tail_{};
        std::atomic_uint64_t logical_rendered_frames_{};
        std::atomic_uint64_t input_high_water_frames_{};
        std::atomic_uint64_t input_underflows_{};
        std::atomic_uint64_t input_overflows_{};
        std::atomic_uint64_t conversion_failures_{};
        std::atomic_uint64_t phase_envelope_violations_{};
        std::atomic_uint64_t non_finite_output_blocks_{};
        std::atomic_uint64_t initial_phase_error_bits_{};
        std::atomic_uint64_t maximum_absolute_phase_error_bits_{};
        std::atomic_uint64_t final_phase_error_bits_{};
        std::atomic_uint64_t minimum_rate_ratio_ppm_bits_{};
        std::atomic_uint64_t maximum_rate_ratio_ppm_bits_{};
        std::atomic_uint64_t final_rate_ratio_ppm_bits_{};
    };
} // namespace gc::audio

#pragma once

#include "Audio/Asio/AsioClock.h"
#include "Audio/Mixer/MiniaudioMixer.h"

#include <atomic>
#include <cstdint>
#include <expected>

namespace gc::audio
{
    enum class AsioLogicalRenderPlanFailure : std::uint8_t
    {
        Busy,
        NotDue,
        InvalidConfiguration,
        InvalidPhysicalSession,
        InvalidClock,
        CoordinateRegressed,
        ArithmeticOverflow,
        GenerationOverflow,
    };

    struct AsioLogicalRenderPlan final
    {
        MixerRenderTimeline timeline{};
        std::uint64_t system_time_ns{};
        std::uint64_t presented_output_frame{};
        std::uint64_t submitted_output_tail{};
        std::uint64_t physical_session_generation{};

    private:
        friend class AsioLogicalRenderSequencer;

        std::uint64_t claim_token{};
    };

    class AsioLogicalRenderSequencer final
    {
    public:
        AsioLogicalRenderSequencer(
            std::uint32_t period_frames,
            std::uint32_t output_sample_rate) noexcept;

        [[nodiscard]] std::expected<
            std::uint64_t,
            AsioLogicalRenderPlanFailure>
        BeginPhysicalSession() noexcept;
        bool EndPhysicalSession(std::uint64_t generation) noexcept;

        [[nodiscard]] std::expected<
            AsioLogicalRenderPlan,
            AsioLogicalRenderPlanFailure>
        TryPlanPhysical(
            std::uint64_t physical_session_generation,
            const AsioClockDecision& physical) noexcept;
        [[nodiscard]] std::expected<
            AsioLogicalRenderPlan,
            AsioLogicalRenderPlanFailure>
        TryPlanDetached(std::uint32_t now_ms) noexcept;

        bool Commit(const AsioLogicalRenderPlan& plan) noexcept;
        bool Abandon(const AsioLogicalRenderPlan& plan) noexcept;

        [[nodiscard]] std::uint64_t physical_session_generation() const noexcept;

    private:
        [[nodiscard]] bool TryAcquireClaim() noexcept;
        void ReleaseClaim() noexcept;
        [[nodiscard]] std::expected<
            std::uint64_t,
            AsioLogicalRenderPlanFailure>
        BeginPlanClaim() noexcept;
        [[nodiscard]] std::expected<
            std::uint64_t,
            AsioLogicalRenderPlanFailure>
        LogicalFrameAt(std::uint64_t system_time_ns) const noexcept;
        [[nodiscard]] std::expected<
            AsioLogicalRenderPlan,
            AsioLogicalRenderPlanFailure>
        PreparePlan(
            std::uint64_t claim_token,
            std::uint64_t output_frame_begin,
            std::uint64_t system_time_ns,
            std::uint64_t presented_output_frame,
            std::uint64_t physical_session_generation,
            bool install_physical_mapping,
            std::uint64_t physical_render_origin) noexcept;
        void ClearPendingPlan() noexcept;

        std::uint32_t period_frames_{};
        std::uint32_t output_sample_rate_{};
        std::atomic_bool claim_active_{};
        std::uint64_t next_claim_token_{};
        std::uint64_t active_claim_token_{};

        std::uint64_t next_logical_output_frame_{};
        std::uint64_t last_logical_render_begin_{};
        std::uint64_t last_logical_render_system_time_ns_{};
        bool has_logical_render_anchor_{};

        std::atomic_uint64_t physical_session_generation_{};
        std::uint64_t active_physical_session_generation_{};
        std::uint64_t physical_render_origin_{};
        std::uint64_t logical_render_origin_{};
        bool physical_mapping_ready_{};

        std::uint64_t pending_output_frame_begin_{};
        std::uint64_t pending_system_time_ns_{};
        std::uint64_t pending_submitted_output_tail_{};
        std::uint64_t pending_physical_session_generation_{};
        std::uint64_t pending_physical_render_origin_{};
        bool pending_install_physical_mapping_{};
    };
} // namespace gc::audio

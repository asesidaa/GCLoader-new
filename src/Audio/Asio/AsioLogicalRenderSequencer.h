#pragma once

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
        PhysicalSessionAlreadyAttached,
        PhysicalSessionNotAttached,
        CoordinateRegressed,
        ArithmeticOverflow,
        GenerationOverflow,
    };

    enum class AsioPhysicalAttachmentDisposition : std::uint8_t
    {
        Aligned,
        WaitForPhysical,
        CatchUpLogical,
    };

    struct AsioPhysicalAttachment final
    {
        std::uint64_t physical_session_generation{};
        std::uint64_t logical_render_origin{};
        std::uint64_t physical_render_origin{};
        AsioPhysicalAttachmentDisposition disposition{};
        std::uint64_t interval_frames{};
    };

    struct AsioLogicalRenderPlan final
    {
        MixerRenderTimeline timeline{};
        std::uint64_t submitted_output_tail{};
        std::uint64_t physical_session_generation{};

    private:
        friend class AsioLogicalRenderSequencer;

        std::uint64_t claim_token{};
    };

    class AsioLogicalRenderSequencer final
    {
    public:
        explicit AsioLogicalRenderSequencer(
            std::uint32_t period_frames) noexcept;

        [[nodiscard]] std::expected<std::uint64_t,
                                    AsioLogicalRenderPlanFailure>
        BeginPhysicalSession() noexcept;
        [[nodiscard]] std::expected<AsioPhysicalAttachment,
                                    AsioLogicalRenderPlanFailure>
        AttachPhysicalSession(
            std::uint64_t generation,
            std::uint64_t logical_render_origin,
            std::uint64_t physical_render_origin) noexcept;
        [[nodiscard]] std::expected<AsioLogicalRenderPlan,
                                    AsioLogicalRenderPlanFailure>
        TryPlanPhysical(
            std::uint64_t generation,
            std::uint64_t physical_render_begin) noexcept;
        [[nodiscard]] std::expected<AsioLogicalRenderPlan,
                                    AsioLogicalRenderPlanFailure>
        TryPlanDetached(std::uint64_t logical_render_begin) noexcept;
        bool Commit(const AsioLogicalRenderPlan& plan) noexcept;
        bool Abandon(const AsioLogicalRenderPlan& plan) noexcept;
        bool EndPhysicalSession(std::uint64_t generation) noexcept;

        [[nodiscard]] std::uint64_t physical_session_generation() const noexcept;

    private:
        [[nodiscard]] bool TryAcquireClaim() noexcept;
        void ReleaseClaim() noexcept;
        [[nodiscard]] std::expected<std::uint64_t,
                                    AsioLogicalRenderPlanFailure>
        BeginPlanClaim() noexcept;
        [[nodiscard]] std::expected<AsioLogicalRenderPlan,
                                    AsioLogicalRenderPlanFailure>
        PreparePlan(
            std::uint64_t claim_token,
            std::uint64_t logical_render_begin,
            std::uint64_t physical_session_generation) noexcept;
        void ClearPendingPlan() noexcept;

        std::uint32_t period_frames_{};
        std::atomic_bool claim_active_{};
        std::uint64_t next_claim_token_{};
        std::uint64_t active_claim_token_{};

        std::uint64_t next_logical_output_frame_{};

        std::atomic_uint64_t physical_session_generation_{};
        std::uint64_t active_physical_session_generation_{};
        std::uint64_t physical_render_origin_{};
        std::uint64_t logical_render_origin_{};
        bool physical_session_attached_{};

        std::uint64_t pending_submitted_output_tail_{};
        std::uint64_t pending_physical_session_generation_{};
    };
} // namespace gc::audio

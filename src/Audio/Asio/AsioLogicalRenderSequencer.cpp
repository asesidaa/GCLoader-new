#include "Audio/Asio/AsioLogicalRenderSequencer.h"

#include <limits>

namespace gc::audio
{
    static_assert(std::atomic_bool::is_always_lock_free);
    static_assert(std::atomic_uint64_t::is_always_lock_free);

    AsioLogicalRenderSequencer::AsioLogicalRenderSequencer(
        const std::uint32_t period_frames) noexcept
        : period_frames_(period_frames)
    {
    }

    bool AsioLogicalRenderSequencer::TryAcquireClaim() noexcept
    {
        bool expected = false;
        return claim_active_.compare_exchange_strong(
            expected,
            true,
            std::memory_order_acq_rel,
            std::memory_order_acquire);
    }

    void AsioLogicalRenderSequencer::ReleaseClaim() noexcept
    {
        claim_active_.store(false, std::memory_order_release);
    }

    std::expected<std::uint64_t, AsioLogicalRenderPlanFailure>
    AsioLogicalRenderSequencer::BeginPlanClaim() noexcept
    {
        if (!TryAcquireClaim())
        {
            return std::unexpected(AsioLogicalRenderPlanFailure::Busy);
        }
        if (next_claim_token_ == (std::numeric_limits<std::uint64_t>::max)())
        {
            ReleaseClaim();
            return std::unexpected(
                AsioLogicalRenderPlanFailure::GenerationOverflow);
        }
        active_claim_token_ = ++next_claim_token_;
        return active_claim_token_;
    }

    std::expected<std::uint64_t, AsioLogicalRenderPlanFailure>
    AsioLogicalRenderSequencer::BeginPhysicalSession() noexcept
    {
        if (period_frames_ == 0)
        {
            return std::unexpected(
                AsioLogicalRenderPlanFailure::InvalidConfiguration);
        }
        if (!TryAcquireClaim())
        {
            return std::unexpected(AsioLogicalRenderPlanFailure::Busy);
        }

        const auto previous = physical_session_generation_.load(
            std::memory_order_relaxed);
        if (previous == (std::numeric_limits<std::uint64_t>::max)())
        {
            ReleaseClaim();
            return std::unexpected(
                AsioLogicalRenderPlanFailure::GenerationOverflow);
        }

        active_physical_session_generation_ = previous + 1;
        physical_session_generation_.store(
            active_physical_session_generation_,
            std::memory_order_release);
        physical_render_origin_ = 0;
        logical_render_origin_ = 0;
        physical_session_attached_ = false;
        ReleaseClaim();
        return active_physical_session_generation_;
    }

    std::expected<AsioPhysicalAttachment, AsioLogicalRenderPlanFailure>
    AsioLogicalRenderSequencer::AttachPhysicalSession(
        const std::uint64_t generation,
        const std::uint64_t logical_render_origin,
        const std::uint64_t physical_render_origin) noexcept
    {
        if (!TryAcquireClaim())
        {
            return std::unexpected(AsioLogicalRenderPlanFailure::Busy);
        }
        if (period_frames_ == 0)
        {
            ReleaseClaim();
            return std::unexpected(
                AsioLogicalRenderPlanFailure::InvalidConfiguration);
        }
        if (generation == 0 ||
            generation != active_physical_session_generation_)
        {
            ReleaseClaim();
            return std::unexpected(
                AsioLogicalRenderPlanFailure::InvalidPhysicalSession);
        }
        if (physical_session_attached_)
        {
            ReleaseClaim();
            return std::unexpected(
                AsioLogicalRenderPlanFailure::PhysicalSessionAlreadyAttached);
        }

        AsioPhysicalAttachmentDisposition disposition =
            AsioPhysicalAttachmentDisposition::Aligned;
        std::uint64_t interval_frames{};
        if (logical_render_origin < next_logical_output_frame_)
        {
            disposition = AsioPhysicalAttachmentDisposition::WaitForPhysical;
            interval_frames = next_logical_output_frame_ - logical_render_origin;
        }
        else if (logical_render_origin > next_logical_output_frame_)
        {
            disposition = AsioPhysicalAttachmentDisposition::CatchUpLogical;
            interval_frames = logical_render_origin - next_logical_output_frame_;
        }

        physical_render_origin_ = physical_render_origin;
        logical_render_origin_ = logical_render_origin;
        physical_session_attached_ = true;
        ReleaseClaim();
        return AsioPhysicalAttachment{
            .physical_session_generation = generation,
            .logical_render_origin = logical_render_origin,
            .physical_render_origin = physical_render_origin,
            .disposition = disposition,
            .interval_frames = interval_frames,
        };
    }

    std::expected<AsioLogicalRenderPlan, AsioLogicalRenderPlanFailure>
    AsioLogicalRenderSequencer::PreparePlan(
        const std::uint64_t claim_token,
        const std::uint64_t logical_render_begin,
        const std::uint64_t physical_session_generation) noexcept
    {
        if (logical_render_begin < next_logical_output_frame_)
        {
            ReleaseClaim();
            return std::unexpected(AsioLogicalRenderPlanFailure::NotDue);
        }
        if (logical_render_begin >
            (std::numeric_limits<std::uint64_t>::max)() - period_frames_)
        {
            ReleaseClaim();
            return std::unexpected(
                AsioLogicalRenderPlanFailure::ArithmeticOverflow);
        }

        const auto submitted_output_tail = logical_render_begin + period_frames_;
        pending_submitted_output_tail_ = submitted_output_tail;
        pending_physical_session_generation_ = physical_session_generation;

        AsioLogicalRenderPlan plan{};
        plan.timeline = {
            .output_frame_begin = logical_render_begin,
            .discontinuity_frames =
            logical_render_begin - next_logical_output_frame_,
        };
        plan.submitted_output_tail = submitted_output_tail;
        plan.physical_session_generation = physical_session_generation;
        plan.claim_token = claim_token;
        return plan;
    }

    std::expected<AsioLogicalRenderPlan, AsioLogicalRenderPlanFailure>
    AsioLogicalRenderSequencer::TryPlanPhysical(
        const std::uint64_t generation,
        const std::uint64_t physical_render_begin) noexcept
    {
        const auto claim = BeginPlanClaim();
        if (!claim)
        {
            return std::unexpected(claim.error());
        }
        if (period_frames_ == 0)
        {
            ReleaseClaim();
            return std::unexpected(
                AsioLogicalRenderPlanFailure::InvalidConfiguration);
        }
        if (generation == 0 || generation != active_physical_session_generation_)
        {
            ReleaseClaim();
            return std::unexpected(
                AsioLogicalRenderPlanFailure::InvalidPhysicalSession);
        }
        if (!physical_session_attached_)
        {
            ReleaseClaim();
            return std::unexpected(
                AsioLogicalRenderPlanFailure::PhysicalSessionNotAttached);
        }
        if (physical_render_begin < physical_render_origin_)
        {
            ReleaseClaim();
            return std::unexpected(
                AsioLogicalRenderPlanFailure::CoordinateRegressed);
        }

        const auto physical_offset = physical_render_begin - physical_render_origin_;
        if (logical_render_origin_ >
            (std::numeric_limits<std::uint64_t>::max)() - physical_offset)
        {
            ReleaseClaim();
            return std::unexpected(
                AsioLogicalRenderPlanFailure::ArithmeticOverflow);
        }
        return PreparePlan(
            *claim,
            logical_render_origin_ + physical_offset,
            generation);
    }

    std::expected<AsioLogicalRenderPlan, AsioLogicalRenderPlanFailure>
    AsioLogicalRenderSequencer::TryPlanDetached(
        const std::uint64_t logical_render_begin) noexcept
    {
        const auto claim = BeginPlanClaim();
        if (!claim)
        {
            return std::unexpected(claim.error());
        }
        if (period_frames_ == 0)
        {
            ReleaseClaim();
            return std::unexpected(
                AsioLogicalRenderPlanFailure::InvalidConfiguration);
        }
        return PreparePlan(*claim, logical_render_begin, 0);
    }

    void AsioLogicalRenderSequencer::ClearPendingPlan() noexcept
    {
        pending_submitted_output_tail_ = 0;
        pending_physical_session_generation_ = 0;
        active_claim_token_ = 0;
    }

    bool AsioLogicalRenderSequencer::Commit(
        const AsioLogicalRenderPlan& plan) noexcept
    {
        if (!claim_active_.load(std::memory_order_acquire) ||
            plan.claim_token == 0 || plan.claim_token != active_claim_token_)
        {
            return false;
        }
        if (pending_physical_session_generation_ != 0 &&
            (pending_physical_session_generation_ !=
                active_physical_session_generation_ ||
                !physical_session_attached_))
        {
            ClearPendingPlan();
            ReleaseClaim();
            return false;
        }

        next_logical_output_frame_ = pending_submitted_output_tail_;
        ClearPendingPlan();
        ReleaseClaim();
        return true;
    }

    bool AsioLogicalRenderSequencer::Abandon(
        const AsioLogicalRenderPlan& plan) noexcept
    {
        if (!claim_active_.load(std::memory_order_acquire) ||
            plan.claim_token == 0 || plan.claim_token != active_claim_token_)
        {
            return false;
        }
        ClearPendingPlan();
        ReleaseClaim();
        return true;
    }

    bool AsioLogicalRenderSequencer::EndPhysicalSession(
        const std::uint64_t generation) noexcept
    {
        if (!TryAcquireClaim())
        {
            return false;
        }
        if (generation == 0 || generation != active_physical_session_generation_)
        {
            ReleaseClaim();
            return false;
        }
        active_physical_session_generation_ = 0;
        physical_render_origin_ = 0;
        logical_render_origin_ = 0;
        physical_session_attached_ = false;
        ReleaseClaim();
        return true;
    }

    std::uint64_t
    AsioLogicalRenderSequencer::physical_session_generation() const noexcept
    {
        return physical_session_generation_.load(std::memory_order_acquire);
    }
} // namespace gc::audio

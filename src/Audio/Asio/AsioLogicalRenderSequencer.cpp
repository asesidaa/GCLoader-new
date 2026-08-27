#include "Audio/Asio/AsioLogicalRenderSequencer.h"

#include <limits>

namespace gc::audio
{
    namespace
    {
        constexpr std::uint64_t kNanosecondsPerMillisecond = 1'000'000;
        constexpr std::uint64_t kNanosecondsPerSecond = 1'000'000'000;

        [[nodiscard]] bool IsForwardWrappingDelta(const std::uint32_t elapsed_ms) noexcept
        {
            return elapsed_ms <= static_cast<std::uint32_t>(
                (std::numeric_limits<std::int32_t>::max)());
        }

        [[nodiscard]] std::expected<std::uint64_t, AsioLogicalRenderPlanFailure>
        NanosecondsToFrames(
            const std::uint64_t nanoseconds,
            const std::uint32_t sample_rate) noexcept
        {
            const auto whole_seconds = nanoseconds / kNanosecondsPerSecond;
            const auto remaining_nanoseconds = nanoseconds % kNanosecondsPerSecond;
            if (sample_rate == 0 || whole_seconds >
                (std::numeric_limits<std::uint64_t>::max)() / sample_rate)
            {
                return std::unexpected(
                    AsioLogicalRenderPlanFailure::ArithmeticOverflow);
            }
            const auto whole_frames = whole_seconds * sample_rate;
            const auto remaining_frames =
                remaining_nanoseconds * sample_rate / kNanosecondsPerSecond;
            if (whole_frames > (std::numeric_limits<std::uint64_t>::max)() -
                remaining_frames)
            {
                return std::unexpected(
                    AsioLogicalRenderPlanFailure::ArithmeticOverflow);
            }
            return whole_frames + remaining_frames;
        }
    } // namespace

    static_assert(std::atomic_bool::is_always_lock_free);
    static_assert(std::atomic_uint64_t::is_always_lock_free);

    AsioLogicalRenderSequencer::AsioLogicalRenderSequencer(
        const std::uint32_t period_frames,
        const std::uint32_t output_sample_rate) noexcept
        : period_frames_(period_frames),
          output_sample_rate_(output_sample_rate)
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
        if (period_frames_ == 0 || output_sample_rate_ == 0)
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
        physical_mapping_ready_ = false;
        physical_render_origin_ = 0;
        logical_render_origin_ = 0;
        ReleaseClaim();
        return active_physical_session_generation_;
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
        physical_mapping_ready_ = false;
        physical_render_origin_ = 0;
        logical_render_origin_ = 0;
        ReleaseClaim();
        return true;
    }

    std::expected<std::uint64_t, AsioLogicalRenderPlanFailure>
    AsioLogicalRenderSequencer::LogicalFrameAt(
        const std::uint64_t system_time_ns) const noexcept
    {
        if (!has_logical_render_anchor_ || system_time_ns == 0)
        {
            return std::unexpected(AsioLogicalRenderPlanFailure::InvalidClock);
        }

        const auto previous_ms = static_cast<std::uint32_t>(
            last_logical_render_system_time_ns_ / kNanosecondsPerMillisecond);
        const auto current_ms = static_cast<std::uint32_t>(
            system_time_ns / kNanosecondsPerMillisecond);
        const auto elapsed_ms = static_cast<std::uint32_t>(current_ms - previous_ms);
        if (!IsForwardWrappingDelta(elapsed_ms))
        {
            return std::unexpected(AsioLogicalRenderPlanFailure::InvalidClock);
        }

        std::uint64_t elapsed_ns{};
        if (system_time_ns >= last_logical_render_system_time_ns_)
        {
            elapsed_ns = system_time_ns - last_logical_render_system_time_ns_;
            if (elapsed_ns / kNanosecondsPerMillisecond >
                static_cast<std::uint64_t>(
                    (std::numeric_limits<std::int32_t>::max)()))
            {
                return std::unexpected(AsioLogicalRenderPlanFailure::InvalidClock);
            }
        }
        else
        {
            elapsed_ns = static_cast<std::uint64_t>(elapsed_ms) *
                kNanosecondsPerMillisecond;
        }

        const auto elapsed_frames = NanosecondsToFrames(
            elapsed_ns, output_sample_rate_);
        if (!elapsed_frames || last_logical_render_begin_ >
            (std::numeric_limits<std::uint64_t>::max)() - *elapsed_frames)
        {
            return std::unexpected(
                AsioLogicalRenderPlanFailure::ArithmeticOverflow);
        }
        return last_logical_render_begin_ + *elapsed_frames;
    }

    std::expected<AsioLogicalRenderPlan, AsioLogicalRenderPlanFailure>
    AsioLogicalRenderSequencer::PreparePlan(
        const std::uint64_t claim_token,
        const std::uint64_t output_frame_begin,
        const std::uint64_t system_time_ns,
        const std::uint64_t presented_output_frame,
        const std::uint64_t physical_session_generation,
        const bool install_physical_mapping,
        const std::uint64_t physical_render_origin) noexcept
    {
        if (system_time_ns == 0 || output_frame_begin < next_logical_output_frame_)
        {
            ReleaseClaim();
            return std::unexpected(
                output_frame_begin < next_logical_output_frame_
                    ? AsioLogicalRenderPlanFailure::CoordinateRegressed
                    : AsioLogicalRenderPlanFailure::InvalidClock);
        }
        if (output_frame_begin > (std::numeric_limits<std::uint64_t>::max)() -
            period_frames_)
        {
            ReleaseClaim();
            return std::unexpected(
                AsioLogicalRenderPlanFailure::ArithmeticOverflow);
        }

        const auto submitted_output_tail = output_frame_begin + period_frames_;
        pending_output_frame_begin_ = output_frame_begin;
        pending_system_time_ns_ = system_time_ns;
        pending_submitted_output_tail_ = submitted_output_tail;
        pending_physical_session_generation_ = physical_session_generation;
        pending_physical_render_origin_ = physical_render_origin;
        pending_install_physical_mapping_ = install_physical_mapping;

        AsioLogicalRenderPlan plan{};
        plan.timeline = {
            .output_frame_begin = output_frame_begin,
            .discontinuity_frames =
            output_frame_begin - next_logical_output_frame_,
        };
        plan.system_time_ns = system_time_ns;
        plan.presented_output_frame = presented_output_frame;
        plan.submitted_output_tail = submitted_output_tail;
        plan.physical_session_generation = physical_session_generation;
        plan.claim_token = claim_token;
        return plan;
    }

    std::expected<AsioLogicalRenderPlan, AsioLogicalRenderPlanFailure>
    AsioLogicalRenderSequencer::TryPlanPhysical(
        const std::uint64_t physical_session_generation,
        const AsioClockDecision& physical) noexcept
    {
        const auto claim = BeginPlanClaim();
        if (!claim)
        {
            return std::unexpected(claim.error());
        }
        if (period_frames_ == 0 || output_sample_rate_ == 0)
        {
            ReleaseClaim();
            return std::unexpected(
                AsioLogicalRenderPlanFailure::InvalidConfiguration);
        }
        if (physical_session_generation == 0 ||
            physical_session_generation != active_physical_session_generation_)
        {
            ReleaseClaim();
            return std::unexpected(
                AsioLogicalRenderPlanFailure::InvalidPhysicalSession);
        }
        if (physical.kind == AsioClockDecisionKind::invalid ||
            physical.system_time_ns == 0 ||
            physical.render_output_frame_begin < physical.presented_output_frame)
        {
            ReleaseClaim();
            return std::unexpected(AsioLogicalRenderPlanFailure::InvalidClock);
        }

        std::uint64_t logical_render_begin{};
        bool install_mapping{};
        if (!physical_mapping_ready_)
        {
            logical_render_begin = next_logical_output_frame_;
            if (has_logical_render_anchor_)
            {
                const auto timed_frame = LogicalFrameAt(physical.system_time_ns);
                if (!timed_frame)
                {
                    ReleaseClaim();
                    return std::unexpected(timed_frame.error());
                }
                if (*timed_frame > logical_render_begin)
                {
                    logical_render_begin = *timed_frame;
                }
            }
            install_mapping = true;
        }
        else
        {
            if (physical.render_output_frame_begin < physical_render_origin_)
            {
                ReleaseClaim();
                return std::unexpected(
                    AsioLogicalRenderPlanFailure::CoordinateRegressed);
            }
            const auto physical_offset =
                physical.render_output_frame_begin - physical_render_origin_;
            if (logical_render_origin_ >
                (std::numeric_limits<std::uint64_t>::max)() - physical_offset)
            {
                ReleaseClaim();
                return std::unexpected(
                    AsioLogicalRenderPlanFailure::ArithmeticOverflow);
            }
            logical_render_begin = logical_render_origin_ + physical_offset;
        }

        if (logical_render_begin < next_logical_output_frame_)
        {
            ReleaseClaim();
            return std::unexpected(
                AsioLogicalRenderPlanFailure::CoordinateRegressed);
        }
        const auto presentation_lag =
            physical.render_output_frame_begin - physical.presented_output_frame;
        const auto logical_presented = logical_render_begin >= presentation_lag
                                           ? logical_render_begin - presentation_lag
                                           : 0;
        return PreparePlan(
            *claim,
            logical_render_begin,
            physical.system_time_ns,
            logical_presented,
            physical_session_generation,
            install_mapping,
            physical.render_output_frame_begin);
    }

    std::expected<AsioLogicalRenderPlan, AsioLogicalRenderPlanFailure>
    AsioLogicalRenderSequencer::TryPlanDetached(const std::uint32_t now_ms) noexcept
    {
        const auto claim = BeginPlanClaim();
        if (!claim)
        {
            return std::unexpected(claim.error());
        }
        if (period_frames_ == 0 || output_sample_rate_ == 0)
        {
            ReleaseClaim();
            return std::unexpected(
                AsioLogicalRenderPlanFailure::InvalidConfiguration);
        }
        if (!has_logical_render_anchor_)
        {
            ReleaseClaim();
            return std::unexpected(AsioLogicalRenderPlanFailure::NotDue);
        }

        const auto previous_ms = static_cast<std::uint32_t>(
            last_logical_render_system_time_ns_ / kNanosecondsPerMillisecond);
        const auto elapsed_ms = static_cast<std::uint32_t>(now_ms - previous_ms);
        if (!IsForwardWrappingDelta(elapsed_ms))
        {
            ReleaseClaim();
            return std::unexpected(AsioLogicalRenderPlanFailure::InvalidClock);
        }
        const auto elapsed_ns = static_cast<std::uint64_t>(elapsed_ms) *
            kNanosecondsPerMillisecond;
        if (last_logical_render_system_time_ns_ >
            (std::numeric_limits<std::uint64_t>::max)() - elapsed_ns)
        {
            ReleaseClaim();
            return std::unexpected(
                AsioLogicalRenderPlanFailure::ArithmeticOverflow);
        }
        const auto scheduled_time_ns =
            last_logical_render_system_time_ns_ + elapsed_ns;
        const auto target = LogicalFrameAt(scheduled_time_ns);
        if (!target)
        {
            ReleaseClaim();
            return std::unexpected(target.error());
        }
        if (*target < next_logical_output_frame_)
        {
            ReleaseClaim();
            return std::unexpected(AsioLogicalRenderPlanFailure::NotDue);
        }
        return PreparePlan(
            *claim,
            *target,
            scheduled_time_ns,
            0,
            0,
            false,
            0);
    }

    void AsioLogicalRenderSequencer::ClearPendingPlan() noexcept
    {
        pending_output_frame_begin_ = 0;
        pending_system_time_ns_ = 0;
        pending_submitted_output_tail_ = 0;
        pending_physical_session_generation_ = 0;
        pending_physical_render_origin_ = 0;
        pending_install_physical_mapping_ = false;
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
        if (pending_install_physical_mapping_ &&
            (pending_physical_session_generation_ == 0 ||
                pending_physical_session_generation_ !=
                active_physical_session_generation_))
        {
            ClearPendingPlan();
            ReleaseClaim();
            return false;
        }

        next_logical_output_frame_ = pending_submitted_output_tail_;
        last_logical_render_begin_ = pending_output_frame_begin_;
        last_logical_render_system_time_ns_ = pending_system_time_ns_;
        has_logical_render_anchor_ = true;
        if (pending_install_physical_mapping_)
        {
            physical_render_origin_ = pending_physical_render_origin_;
            logical_render_origin_ = pending_output_frame_begin_;
            physical_mapping_ready_ = true;
        }

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

    std::uint64_t
    AsioLogicalRenderSequencer::physical_session_generation() const noexcept
    {
        return physical_session_generation_.load(std::memory_order_acquire);
    }
} // namespace gc::audio

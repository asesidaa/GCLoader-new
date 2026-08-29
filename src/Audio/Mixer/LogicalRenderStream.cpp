#include "Audio/Mixer/LogicalRenderStream.h"

#include <limits>
#include <new>

namespace gc::audio
{
    static_assert(std::atomic_bool::is_always_lock_free);
    static_assert(std::atomic_uint64_t::is_always_lock_free);

    LogicalRenderStream::LogicalRenderStream(
        AudioRenderCore& render_core) noexcept
        : render_core_(render_core),
          period_frames_(render_core.period_frames())
    {
    }

    std::unique_ptr<LogicalRenderStream>
    LogicalRenderStream::Create(
        AudioRenderCore& render_core) noexcept
    {
        if (render_core.period_frames() == 0 ||
            render_core.output_sample_rate() == 0)
        {
            return {};
        }
        return std::unique_ptr<LogicalRenderStream>{
            new(std::nothrow) LogicalRenderStream(render_core)
        };
    }

    bool LogicalRenderStream::TryAcquireClaim() noexcept
    {
        bool expected = false;
        return claim_active_.compare_exchange_strong(
            expected,
            true,
            std::memory_order_acq_rel,
            std::memory_order_acquire);
    }

    void LogicalRenderStream::ReleaseClaim() noexcept
    {
        claim_active_.store(false, std::memory_order_release);
    }

    bool LogicalRenderStream::MatchesActiveLease(
        const LogicalRenderLease& lease) const noexcept
    {
        return lease_active_ &&
            lease.generation != 0 &&
            lease.generation == active_lease_generation_ &&
            lease.owner == active_owner_;
    }

    std::expected<LogicalRenderLease, LogicalRenderFailure>
    LogicalRenderStream::AcquireInitial(
        const LogicalRenderOwner owner) noexcept
    {
        if (period_frames_ == 0)
        {
            return std::unexpected(
                LogicalRenderFailure::InvalidConfiguration);
        }
        if (!TryAcquireClaim())
        {
            return std::unexpected(LogicalRenderFailure::Busy);
        }
        if (lease_active_)
        {
            ReleaseClaim();
            return std::unexpected(
                LogicalRenderFailure::LeaseAlreadyActive);
        }
        if (next_lease_generation_ ==
            (std::numeric_limits<std::uint64_t>::max)())
        {
            ReleaseClaim();
            return std::unexpected(
                LogicalRenderFailure::GenerationOverflow);
        }

        active_owner_ = owner;
        active_lease_generation_ = ++next_lease_generation_;
        lease_active_ = true;
        const auto tail =
            committed_tail_.load(std::memory_order_relaxed);
        const LogicalRenderLease lease{
            .owner = owner,
            .generation = active_lease_generation_,
            .acquired_tail = tail,
        };
        ReleaseClaim();
        return lease;
    }

    std::expected<LogicalRenderLease, LogicalRenderFailure>
    LogicalRenderStream::Transfer(
        const LogicalRenderLease& from,
        const LogicalRenderOwner to,
        const std::uint64_t expected_tail) noexcept
    {
        if (!TryAcquireClaim())
        {
            return std::unexpected(LogicalRenderFailure::Busy);
        }
        if (!lease_active_)
        {
            ReleaseClaim();
            return std::unexpected(
                LogicalRenderFailure::NoActiveLease);
        }
        if (!MatchesActiveLease(from))
        {
            ReleaseClaim();
            return std::unexpected(
                LogicalRenderFailure::InvalidLease);
        }
        if (to == from.owner)
        {
            ReleaseClaim();
            return std::unexpected(LogicalRenderFailure::SameOwner);
        }

        const auto tail =
            committed_tail_.load(std::memory_order_relaxed);
        if (tail != expected_tail)
        {
            ReleaseClaim();
            return std::unexpected(LogicalRenderFailure::TailMismatch);
        }
        if (next_lease_generation_ ==
            (std::numeric_limits<std::uint64_t>::max)())
        {
            ReleaseClaim();
            return std::unexpected(
                LogicalRenderFailure::GenerationOverflow);
        }

        active_owner_ = to;
        active_lease_generation_ = ++next_lease_generation_;
        const LogicalRenderLease lease{
            .owner = to,
            .generation = active_lease_generation_,
            .acquired_tail = tail,
        };
        ReleaseClaim();
        return lease;
    }

    std::expected<LogicalRenderPlan, LogicalRenderFailure>
    LogicalRenderStream::BeginRender(
        const LogicalRenderLease& lease) noexcept
    {
        if (period_frames_ == 0)
        {
            return std::unexpected(
                LogicalRenderFailure::InvalidConfiguration);
        }
        if (!TryAcquireClaim())
        {
            return std::unexpected(LogicalRenderFailure::Busy);
        }
        if (!MatchesActiveLease(lease))
        {
            ReleaseClaim();
            return std::unexpected(
                LogicalRenderFailure::InvalidLease);
        }

        const auto begin =
            committed_tail_.load(std::memory_order_relaxed);
        if (begin >
            (std::numeric_limits<std::uint64_t>::max)() -
                period_frames_)
        {
            ReleaseClaim();
            return std::unexpected(
                LogicalRenderFailure::ArithmeticOverflow);
        }
        if (next_claim_token_ ==
            (std::numeric_limits<std::uint64_t>::max)())
        {
            ReleaseClaim();
            return std::unexpected(
                LogicalRenderFailure::GenerationOverflow);
        }

        active_claim_token_ = ++next_claim_token_;
        pending_begin_ = begin;
        pending_tail_ = begin + period_frames_;
        pending_lease_generation_ = lease.generation;
        pending_rendered_ = false;
        return LogicalRenderPlan{
            .timeline = {
                .output_frame_begin = begin,
                .discontinuity_frames = 0,
            },
            .committed_tail_after = pending_tail_,
            .lease_generation = lease.generation,
            .claim_token = active_claim_token_,
        };
    }

    bool LogicalRenderStream::MatchesPendingPlan(
        const LogicalRenderPlan& plan) const noexcept
    {
        return claim_active_.load(std::memory_order_acquire) &&
            active_claim_token_ != 0 &&
            plan.claim_token == active_claim_token_ &&
            plan.lease_generation == pending_lease_generation_ &&
            plan.lease_generation == active_lease_generation_ &&
            plan.timeline.output_frame_begin == pending_begin_ &&
            plan.timeline.discontinuity_frames == 0 &&
            plan.committed_tail_after == pending_tail_;
    }

    AudioRenderBlock LogicalRenderStream::InvalidRenderBlock() noexcept
    {
        return {
            .interleaved_stereo = {},
            .mixer_result = MA_INVALID_OPERATION,
            .frames_read = 0,
            .active_voices = 0,
            .missing_frames = 0,
            .silence_reason =
                AudioRenderSilenceReason::render_contract_error,
            .silence_substituted = true,
        };
    }

    AudioRenderBlock LogicalRenderStream::Render(
        const LogicalRenderPlan& plan) noexcept
    {
        if (!MatchesPendingPlan(plan) || pending_rendered_)
        {
            return InvalidRenderBlock();
        }

        pending_rendered_ = true;
        return render_core_.Render(plan.timeline);
    }

    void LogicalRenderStream::ClearPendingPlan() noexcept
    {
        active_claim_token_ = 0;
        pending_begin_ = 0;
        pending_tail_ = 0;
        pending_lease_generation_ = 0;
        pending_rendered_ = false;
    }

    bool LogicalRenderStream::Commit(
        const LogicalRenderPlan& plan) noexcept
    {
        if (!MatchesPendingPlan(plan) || !pending_rendered_)
        {
            return false;
        }

        committed_tail_.store(
            pending_tail_, std::memory_order_release);
        ClearPendingPlan();
        ReleaseClaim();
        return true;
    }

    bool LogicalRenderStream::Abandon(
        const LogicalRenderPlan& plan) noexcept
    {
        if (!MatchesPendingPlan(plan) || pending_rendered_)
        {
            return false;
        }

        ClearPendingPlan();
        ReleaseClaim();
        return true;
    }

    std::uint64_t
    LogicalRenderStream::committed_tail() const noexcept
    {
        return committed_tail_.load(std::memory_order_acquire);
    }
} // namespace gc::audio

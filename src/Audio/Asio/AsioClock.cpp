// SPDX-License-Identifier: CC0-1.0

#include "Audio/Asio/AsioClock.h"

#include <algorithm>
#include <atomic>
#include <limits>
#include <utility>

namespace gc::audio
{
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
    static_assert(std::atomic<bool>::is_always_lock_free);

    void AsioClockTracker::Reset(
        std::uint32_t buffer_frames,
        std::uint32_t output_latency_frames) noexcept
    {
        buffer_frames_ = buffer_frames;
        output_latency_frames_ = output_latency_frames;
        previous_sample_position_ = 0;
        has_previous_sample_position_ = false;
        configured_ = buffer_frames != 0;
        faulted_ = !configured_;
    }

    AsioClockDecision AsioClockTracker::Fault() noexcept
    {
        faulted_ = true;
        return {};
    }

    AsioClockDecision AsioClockTracker::Observe(
        const std::uint64_t sample_position) noexcept
    {
        if (!configured_ || faulted_)
        {
            return Fault();
        }

        if (has_previous_sample_position_)
        {
            if (sample_position <= previous_sample_position_ ||
                sample_position - previous_sample_position_ != buffer_frames_)
            {
                return Fault();
            }
        }

        if (sample_position >
            std::numeric_limits<std::uint64_t>::max() -
            output_latency_frames_)
        {
            return Fault();
        }

        previous_sample_position_ = sample_position;
        has_previous_sample_position_ = true;

        const AsioClockDecision decision{
            AsioClockDecisionKind::valid,
            sample_position,
            sample_position + output_latency_frames_,
        };
        return decision;
    }

    AsioPresentedClockPublication::AsioPresentedClockPublication(
        AsioClockNowActions actions,
        std::shared_ptr<const AsioLogicalTimeline> timeline,
        std::shared_ptr<const AsioSubmittedOutputTail> submitted_tail) noexcept
        : actions_(actions),
          timeline_(std::move(timeline)),
          submitted_tail_(std::move(submitted_tail))
    {
    }

    void AsioPresentedClockPublication::Invalidate() noexcept
    {
        invalidated_.store(true, std::memory_order_release);
    }

    std::optional<std::uint64_t>
    AsioPresentedClockPublication::LastReturned() const noexcept
    {
        if (!has_last_returned_.load(std::memory_order_acquire))
        {
            return std::nullopt;
        }
        return last_returned_.load(std::memory_order_acquire);
    }

    std::uint64_t AsioPresentedClockPublication::RememberMonotonic(
        std::uint64_t frame) noexcept
    {
        auto observed = last_returned_.load(std::memory_order_acquire);
        while (observed < frame &&
            !last_returned_.compare_exchange_weak(
                observed,
                frame,
                std::memory_order_acq_rel,
                std::memory_order_acquire))
        {
        }
        has_last_returned_.store(true, std::memory_order_release);
        return std::max(observed, frame);
    }

    std::optional<std::uint64_t>
    AsioPresentedClockPublication::CurrentOutputFrame() noexcept
    {
        if (invalidated_.load(std::memory_order_acquire))
        {
            return std::nullopt;
        }
        if (timeline_ == nullptr || submitted_tail_ == nullptr ||
            actions_.time_get_time_ms == nullptr)
        {
            Invalidate();
            return std::nullopt;
        }

        const auto tail = submitted_tail_->Read();
        if (!tail.valid)
        {
            Invalidate();
            return std::nullopt;
        }
        if (!tail.stable || !tail.available)
        {
            return LastReturned();
        }

        const auto now_ms = actions_.time_get_time_ms(actions_.context);
        const auto projected = timeline_->WholePresentedFrameAt(now_ms);
        if (!projected)
        {
            if (projected.error() ==
                AsioLogicalTimelineFailure::SnapshotUnavailable)
            {
                return LastReturned();
            }
            Invalidate();
            return std::nullopt;
        }

        return RememberMonotonic((std::min)(
            *projected, tail.submitted_output_tail));
    }
} // namespace gc::audio

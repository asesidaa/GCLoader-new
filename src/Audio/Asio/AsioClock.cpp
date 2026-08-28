// SPDX-License-Identifier: CC0-1.0

#include "Audio/Asio/AsioClock.h"

#include <algorithm>
#include <atomic>
#include <limits>
#include <utility>

namespace gc::audio
{
    namespace
    {
        constexpr std::uint64_t kNanosecondsPerMillisecond = 1'000'000;
        constexpr std::uint64_t kMillisecondsPerSecond = 1'000;
        constexpr int kStableReadAttempts = 3;

        [[nodiscard]] bool IsForwardClockDelta(
            const std::uint32_t elapsed_ms) noexcept
        {
            return elapsed_ms <= static_cast<std::uint32_t>(
                (std::numeric_limits<std::int32_t>::max)());
        }
    } // namespace

    static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
    static_assert(std::atomic<std::uint32_t>::is_always_lock_free);
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

    void AsioPresentedClockPublication::StoreAnchor(
        const Anchor& anchor) noexcept
    {
        if (writer_version_ >
            ((std::numeric_limits<std::uint64_t>::max)() - 2) / 2)
        {
            Invalidate();
            return;
        }

        const auto writing = writer_version_++ * 2 + 1;
        anchor_version_.store(writing, std::memory_order_seq_cst);
        anchor_presented_output_frame_.store(
            anchor.presented_output_frame, std::memory_order_seq_cst);
        anchor_submitted_output_tail_.store(
            anchor.submitted_output_tail, std::memory_order_seq_cst);
        anchor_multimedia_time_ms_.store(
            anchor.multimedia_time_ms, std::memory_order_seq_cst);
        anchor_available_.store(true, std::memory_order_seq_cst);
        anchor_version_.store(writing + 1, std::memory_order_seq_cst);
    }

    void AsioPresentedClockPublication::PublishPhysicalAnchor(
        const std::uint64_t presented_output_frame,
        const std::uint64_t submitted_output_tail,
        const std::uint64_t system_time_ns) noexcept
    {
        if (invalidated_.load(std::memory_order_acquire) ||
            system_time_ns == 0 || submitted_output_tail <= presented_output_frame)
        {
            Invalidate();
            return;
        }
        StoreAnchor({
            .presented_output_frame = presented_output_frame,
            .submitted_output_tail = submitted_output_tail,
            .multimedia_time_ms = static_cast<std::uint32_t>(
                system_time_ns / kNanosecondsPerMillisecond),
        });
    }

    void AsioPresentedClockPublication::PublishLogicalAnchor(
        const std::uint64_t presented_output_frame,
        const std::uint64_t submitted_output_tail,
        const std::uint32_t multimedia_time_ms) noexcept
    {
        if (invalidated_.load(std::memory_order_acquire) ||
            submitted_output_tail <= presented_output_frame)
        {
            Invalidate();
            return;
        }
        StoreAnchor({
            .presented_output_frame = presented_output_frame,
            .submitted_output_tail = submitted_output_tail,
            .multimedia_time_ms = multimedia_time_ms,
        });
    }

    std::optional<AsioPresentedClockPublication::Anchor>
    AsioPresentedClockPublication::ReadStable() const noexcept
    {
        for (int attempt = 0; attempt < kStableReadAttempts; ++attempt)
        {
            const auto before = anchor_version_.load(std::memory_order_seq_cst);
            if ((before & 1U) != 0)
            {
                continue;
            }
            const Anchor candidate{
                .presented_output_frame =
                anchor_presented_output_frame_.load(std::memory_order_seq_cst),
                .submitted_output_tail =
                anchor_submitted_output_tail_.load(std::memory_order_seq_cst),
                .multimedia_time_ms =
                anchor_multimedia_time_ms_.load(std::memory_order_seq_cst),
            };
            const bool available =
                anchor_available_.load(std::memory_order_seq_cst);
            const auto after = anchor_version_.load(std::memory_order_seq_cst);
            if (before == after && (after & 1U) == 0)
            {
                return available ? std::optional<Anchor>{candidate} : std::nullopt;
            }
        }
        return std::nullopt;
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

        const auto anchor = ReadStable();
        if (!anchor)
        {
            return LastReturned();
        }
        if (anchor->submitted_output_tail <= anchor->presented_output_frame)
        {
            Invalidate();
            return std::nullopt;
        }

        const auto now_ms = actions_.time_get_time_ms(actions_.context);
        auto elapsed_ms = now_ms - anchor->multimedia_time_ms;
        if (!IsForwardClockDelta(elapsed_ms))
        {
            elapsed_ms = 0;
        }

        const auto available_frames =
            anchor->submitted_output_tail - anchor->presented_output_frame;
        const auto projected_frames = (std::min)(
            static_cast<std::uint64_t>(elapsed_ms) *
            timeline_->output_sample_rate() /
            kMillisecondsPerSecond,
            available_frames - 1);
        return RememberMonotonic(
            anchor->presented_output_frame + projected_frames);
    }
} // namespace gc::audio

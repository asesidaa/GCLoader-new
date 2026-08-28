#include "Audio/Asio/ExactAsioClock.h"

#include <new>
#include <optional>
#include <utility>

namespace gc::audio
{
    namespace
    {
        constexpr std::uint32_t kNanosecondsPerMillisecond = 1'000'000;

        [[nodiscard]] ExactOutputClockResult Result(
            const ExactClockStatus status,
            const std::uint64_t endpoint_generation) noexcept
        {
            return {
                .status = status,
                .endpoint_generation = endpoint_generation,
                .output_frame = std::nullopt,
                .submitted_output_tail = 0,
                .anchor_sequence = 0,
                .anchor_endpoint_position = std::nullopt,
            };
        }
    } // namespace

    ExactAsioClock::ExactAsioClock(
        const std::uint64_t endpoint_generation,
        std::shared_ptr<const AsioLogicalTimeline> timeline,
        const std::int64_t qpc_frequency,
        const std::uint32_t period_frames,
        const std::uint32_t output_latency_frames) noexcept
        : endpoint_generation_(endpoint_generation),
          timeline_(std::move(timeline)),
          qpc_frequency_(qpc_frequency),
          period_frames_(period_frames),
          output_latency_frames_(output_latency_frames)
    {
    }

    ExactAsioClock::~ExactAsioClock() = default;

    std::shared_ptr<ExactAsioClock> ExactAsioClock::Create(
        const std::uint64_t endpoint_generation,
        std::shared_ptr<const AsioLogicalTimeline> timeline,
        const std::int64_t qpc_frequency,
        const std::uint32_t period_frames,
        const std::uint32_t output_latency_frames) noexcept
    {
        if (endpoint_generation == 0 || timeline == nullptr ||
            qpc_frequency <= 0 || period_frames == 0)
        {
            return {};
        }

        std::unique_ptr<ExactAsioClock> clock{
            new(std::nothrow) ExactAsioClock(
                endpoint_generation,
                std::move(timeline),
                qpc_frequency,
                period_frames,
                output_latency_frames)
        };
        if (!clock)
        {
            return {};
        }

        try
        {
            return std::shared_ptr{std::move(clock)};
        }
        catch (...)
        {
            return {};
        }
    }

    ExactOutputClockResult ExactAsioClock::Resolve(
        const timing::AbsoluteHostTime& timestamp,
        const ExactClockResolveIntent intent) const noexcept
    {
        if ((intent != ExactClockResolveIntent::FinalizedTimestamp &&
                intent != ExactClockResolveIntent::ProvisionalHorizon) ||
            invalidated_.load(std::memory_order_acquire))
        {
            return CountResult(Result(
                ExactClockStatus::Discontinuous, endpoint_generation_));
        }

        const auto output_frame = timeline_->ProjectMultimediaMilliseconds(
            timestamp.multimedia_time_ms);
        if (!output_frame)
        {
            const auto status =
                output_frame.error() ==
                AsioLogicalTimelineFailure::SnapshotUnavailable
                    ? ExactClockStatus::TemporarilyUnavailable
                    : ExactClockStatus::Discontinuous;
            return CountResult(Result(status, endpoint_generation_));
        }
        if (output_frame->numerator() < 0)
        {
            return CountResult(Result(
                ExactClockStatus::Discontinuous, endpoint_generation_));
        }

        if (invalidated_.load(std::memory_order_acquire))
        {
            return CountResult(Result(
                ExactClockStatus::Discontinuous, endpoint_generation_));
        }
        return CountResult({
            .status = ExactClockStatus::Resolved,
            .endpoint_generation = endpoint_generation_,
            .output_frame = *output_frame,
            .submitted_output_tail = 0,
            .anchor_sequence = 0,
            .anchor_endpoint_position = std::nullopt,
        });
    }

    ExactOutputClockInfo ExactAsioClock::info() const noexcept
    {
        return {
            .domain = ExactOutputClockDomain::AsioMultimediaMilliseconds,
            .endpoint_generation = endpoint_generation_,
            .qpc_frequency = qpc_frequency_,
            .output_sample_rate = timeline_->output_sample_rate(),
            .period_frames = period_frames_,
            .output_latency_frames = output_latency_frames_,
            .timestamp_quantum_ns = kNanosecondsPerMillisecond,
        };
    }

    ExactOutputClockCounters ExactAsioClock::counters() const noexcept
    {
        return {
            .publication_count = 0,
            .resolved_queries =
            resolved_queries_.load(std::memory_order_relaxed),
            .pending_queries = pending_queries_.load(std::memory_order_relaxed),
            .temporarily_unavailable_queries =
            temporarily_unavailable_queries_.load(std::memory_order_relaxed),
            .history_lost_queries =
            history_lost_queries_.load(std::memory_order_relaxed),
            .discontinuous_queries =
            discontinuous_queries_.load(std::memory_order_relaxed),
        };
    }

    void ExactAsioClock::Invalidate() noexcept
    {
        invalidated_.store(true, std::memory_order_release);
    }

    ExactOutputClockResult ExactAsioClock::CountResult(
        ExactOutputClockResult&& result) const noexcept
    {
        switch (result.status)
        {
        case ExactClockStatus::Resolved:
            resolved_queries_.fetch_add(1, std::memory_order_relaxed);
            break;
        case ExactClockStatus::Pending:
            pending_queries_.fetch_add(1, std::memory_order_relaxed);
            break;
        case ExactClockStatus::TemporarilyUnavailable:
            temporarily_unavailable_queries_.fetch_add(
                1, std::memory_order_relaxed);
            break;
        case ExactClockStatus::HistoryLost:
            history_lost_queries_.fetch_add(1, std::memory_order_relaxed);
            break;
        case ExactClockStatus::Discontinuous:
            discontinuous_queries_.fetch_add(1, std::memory_order_relaxed);
            break;
        case ExactClockStatus::NoPlayback:
        case ExactClockStatus::OutsidePlayback:
            break;
        }
        return result;
    }
} // namespace gc::audio

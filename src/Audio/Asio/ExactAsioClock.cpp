#include "Audio/Asio/ExactAsioClock.h"

#include <new>
#include <optional>
#include <utility>

namespace gc::audio
{
    namespace
    {
        constexpr std::uint32_t kNanosecondsPerMillisecond = 1'000'000;

        [[nodiscard]] ExactJudgementTimelineResult Result(
            const ExactClockStatus status,
            const std::uint64_t timeline_generation) noexcept
        {
            return {
                .status = status,
                .timeline_generation = timeline_generation,
                .logical_output_frame = std::nullopt,
                .available_output_tail = 0,
                .provider_anchor_sequence = 0,
                .provider_position = std::nullopt,
            };
        }
    } // namespace

    ExactAsioClock::ExactAsioClock(
        const std::uint64_t timeline_generation,
        std::shared_ptr<const AsioLogicalTimeline> timeline,
        const std::int64_t qpc_frequency,
        const std::uint32_t period_frames,
        const std::uint32_t output_latency_frames) noexcept
        : timeline_generation_(timeline_generation),
          timeline_(std::move(timeline)),
          qpc_frequency_(qpc_frequency),
          period_frames_(period_frames),
          output_latency_frames_(output_latency_frames)
    {
    }

    ExactAsioClock::~ExactAsioClock() = default;

    std::shared_ptr<ExactAsioClock> ExactAsioClock::Create(
        const std::uint64_t timeline_generation,
        std::shared_ptr<const AsioLogicalTimeline> timeline,
        const std::int64_t qpc_frequency,
        const std::uint32_t period_frames,
        const std::uint32_t output_latency_frames) noexcept
    {
        if (timeline_generation == 0 || timeline == nullptr ||
            qpc_frequency <= 0 || period_frames == 0)
        {
            return {};
        }

        std::unique_ptr<ExactAsioClock> clock{
            new(std::nothrow) ExactAsioClock(
                timeline_generation,
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

    ExactJudgementTimelineResult ExactAsioClock::Resolve(
        const timing::AbsoluteHostTime& timestamp,
        const ExactClockResolveIntent intent) const noexcept
    {
        if ((intent != ExactClockResolveIntent::FinalizedTimestamp &&
                intent != ExactClockResolveIntent::ProvisionalHorizon) ||
            invalidated_.load(std::memory_order_acquire))
        {
            return CountResult(Result(
                ExactClockStatus::Discontinuous, timeline_generation_));
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
            return CountResult(Result(status, timeline_generation_));
        }
        if (output_frame->numerator() < 0)
        {
            return CountResult(Result(
                ExactClockStatus::Discontinuous, timeline_generation_));
        }

        if (invalidated_.load(std::memory_order_acquire))
        {
            return CountResult(Result(
                ExactClockStatus::Discontinuous, timeline_generation_));
        }
        return CountResult({
            .status = ExactClockStatus::Resolved,
            .timeline_generation = timeline_generation_,
            .logical_output_frame = *output_frame,
            .available_output_tail = 0,
            .provider_anchor_sequence = 0,
            .provider_position = std::nullopt,
        });
    }

    ExactJudgementTimelineInfo ExactAsioClock::info() const noexcept
    {
        return {
            .domain = ExactJudgementTimelineDomain::LogicalMultimediaMilliseconds,
            .timeline_generation = timeline_generation_,
            .qpc_frequency = qpc_frequency_,
            .logical_output_rate = timeline_->output_sample_rate(),
            .provider_period_frames = period_frames_,
            .provider_output_latency_frames = output_latency_frames_,
            .timestamp_quantum_ns = kNanosecondsPerMillisecond,
        };
    }

    ExactJudgementTimelineCounters ExactAsioClock::counters() const noexcept
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

    ExactJudgementTimelineResult ExactAsioClock::CountResult(
        ExactJudgementTimelineResult&& result) const noexcept
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

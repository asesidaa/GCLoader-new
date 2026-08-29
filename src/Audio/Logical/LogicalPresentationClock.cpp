#include "Audio/Logical/LogicalPresentationClock.h"

#include <bit>
#include <limits>
#include <new>
#include <optional>
#include <utility>

namespace gc::audio
{
    namespace
    {
        constexpr std::uint64_t kNanosecondsPerMillisecond = 1'000'000;
        constexpr std::uint64_t kNanosecondsPerSecond = 1'000'000'000;
        constexpr std::uint64_t kMillisecondsPerSecond = 1'000;
        constexpr std::uint32_t kAmbiguousWrappedDelta = 0x8000'0000U;
        constexpr int kSnapshotReadAttempts = 3;

        [[nodiscard]] LogicalPresentationClockFailure MapRationalFailure(
            const gc::timing::RationalError) noexcept
        {
            return LogicalPresentationClockFailure::ArithmeticOverflow;
        }

        [[nodiscard]] std::expected<std::uint64_t, LogicalPresentationClockFailure>
        AddSignedDelta(const std::uint64_t base, const std::int32_t delta) noexcept
        {
            if (delta < 0)
            {
                const auto magnitude =
                    std::uint64_t{0} - static_cast<std::uint64_t>(delta);
                if (magnitude > base)
                {
                    return std::unexpected(
                        LogicalPresentationClockFailure::NegativeCoordinate);
                }
                return base - magnitude;
            }

            const auto positive_delta = static_cast<std::uint64_t>(delta);
            if (positive_delta >
                (std::numeric_limits<std::uint64_t>::max)() - base)
            {
                return std::unexpected(
                    LogicalPresentationClockFailure::ArithmeticOverflow);
            }
            return base + positive_delta;
        }

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

    LogicalPresentationClock::LogicalPresentationClock(
        const std::uint64_t timeline_generation,
        const std::uint32_t origin_raw_ms,
        const std::uint32_t logical_output_rate,
        const std::int64_t qpc_frequency) noexcept
        : timeline_generation_(timeline_generation),
          origin_raw_ms_(origin_raw_ms),
          logical_output_rate_(logical_output_rate),
          qpc_frequency_(qpc_frequency),
          writer_raw_ms_(origin_raw_ms),
          observed_raw_ms_(origin_raw_ms)
    {
    }

    std::shared_ptr<LogicalPresentationClock> LogicalPresentationClock::Create(
        const std::uint64_t timeline_generation,
        const std::uint32_t origin_raw_ms,
        const std::uint32_t logical_output_rate,
        const std::int64_t qpc_frequency) noexcept
    {
        if (timeline_generation == 0 || logical_output_rate == 0 ||
            qpc_frequency <= 0)
        {
            return {};
        }

        std::unique_ptr < LogicalPresentationClock > clock{
            new(std::nothrow) LogicalPresentationClock(
                timeline_generation,
                origin_raw_ms,
                logical_output_rate,
                qpc_frequency)
        };
        if (!clock)
        {
            return {};
        }

        try
        {
            return std::shared_ptr < LogicalPresentationClock >
            {
                std::move(clock)
            };
        }
        catch (...)
        {
            return {};
        }
    }

    std::expected<void, LogicalPresentationClockFailure>
    LogicalPresentationClock::ObserveNow(
        const std::uint32_t observed_raw_ms) noexcept
    {
        const std::uint32_t delta = observed_raw_ms - writer_raw_ms_;
        if (delta > static_cast<std::uint32_t>(
            (std::numeric_limits<std::int32_t>::max)()))
        {
            return std::unexpected(
                LogicalPresentationClockFailure::WriterDeltaAmbiguous);
        }
        if (delta >
            (std::numeric_limits<std::uint64_t>::max)() - writer_unwrapped_ms_)
        {
            return std::unexpected(
                LogicalPresentationClockFailure::ArithmeticOverflow);
        }

        const std::uint64_t version =
            snapshot_version_.load(std::memory_order_relaxed);
        if ((version & 1U) != 0 ||
            version > (std::numeric_limits<std::uint64_t>::max)() - 2)
        {
            return std::unexpected(
                LogicalPresentationClockFailure::ArithmeticOverflow);
        }

        writer_raw_ms_ = observed_raw_ms;
        writer_unwrapped_ms_ += delta;

        snapshot_version_.fetch_add(1, std::memory_order_acq_rel);
        observed_raw_ms_.store(writer_raw_ms_, std::memory_order_relaxed);
        observed_unwrapped_ms_.store(
            writer_unwrapped_ms_, std::memory_order_relaxed);
        snapshot_version_.store(version + 2, std::memory_order_release);
        return {};
    }

    std::expected<LogicalPresentationClock::Snapshot,
                  LogicalPresentationClockFailure>
    LogicalPresentationClock::ReadSnapshot() const noexcept
    {
        for (int attempt = 0; attempt < kSnapshotReadAttempts; ++attempt)
        {
            const std::uint64_t before =
                snapshot_version_.load(std::memory_order_acquire);
            if ((before & 1U) != 0)
            {
                continue;
            }

            const Snapshot snapshot{
                .observed_raw_ms =
                observed_raw_ms_.load(std::memory_order_relaxed),
                .observed_unwrapped_ms =
                observed_unwrapped_ms_.load(std::memory_order_relaxed),
            };
            const std::uint64_t after =
                snapshot_version_.load(std::memory_order_acquire);
            if (before == after && (after & 1U) == 0)
            {
                return snapshot;
            }
        }

        return std::unexpected(
            LogicalPresentationClockFailure::SnapshotUnavailable);
    }

    std::expected<std::uint64_t, LogicalPresentationClockFailure>
    LogicalPresentationClock::Unwrap(const std::uint32_t raw_ms) const noexcept
    {
        const auto snapshot = ReadSnapshot();
        if (!snapshot)
        {
            return std::unexpected(snapshot.error());
        }

        const std::uint32_t wrapped_delta =
            raw_ms - snapshot->observed_raw_ms;
        if (wrapped_delta == kAmbiguousWrappedDelta)
        {
            return std::unexpected(
                LogicalPresentationClockFailure::TimestampAmbiguous);
        }
        const auto signed_delta = std::bit_cast<std::int32_t>(wrapped_delta);
        return AddSignedDelta(snapshot->observed_unwrapped_ms, signed_delta);
    }

    std::expected<gc::timing::CheckedRational,
                  LogicalPresentationClockFailure>
    LogicalPresentationClock::ProjectUnwrappedMilliseconds(
        const std::uint64_t unwrapped_ms) const noexcept
    {
        if (unwrapped_ms > static_cast<std::uint64_t>(
            (std::numeric_limits<std::int64_t>::max)()))
        {
            return std::unexpected(
                LogicalPresentationClockFailure::ArithmeticOverflow);
        }

        const auto result = gc::timing::CheckedRational::Whole(
                static_cast<std::int64_t>(unwrapped_ms))
            .Multiply(
                logical_output_rate_,
                kMillisecondsPerSecond);
        if (!result)
        {
            return std::unexpected(MapRationalFailure(result.error()));
        }
        return *result;
    }

    std::expected<gc::timing::CheckedRational,
                  LogicalPresentationClockFailure>
    LogicalPresentationClock::ProjectMultimediaMilliseconds(
        const std::uint32_t raw_ms) const noexcept
    {
        const auto unwrapped = Unwrap(raw_ms);
        if (!unwrapped)
        {
            return std::unexpected(unwrapped.error());
        }
        return ProjectUnwrappedMilliseconds(*unwrapped);
    }

    std::expected<gc::timing::CheckedRational,
                  LogicalPresentationClockFailure>
    LogicalPresentationClock::ProjectSystemTimeNanoseconds(
        const std::uint64_t system_time_ns) const noexcept
    {
        const std::uint64_t whole_ms =
            system_time_ns / kNanosecondsPerMillisecond;
        const auto base = ProjectMultimediaMilliseconds(
            static_cast<std::uint32_t>(whole_ms));
        if (!base)
        {
            return std::unexpected(base.error());
        }

        const std::uint64_t remainder_ns =
            system_time_ns % kNanosecondsPerMillisecond;
        const auto remainder = gc::timing::CheckedRational::Whole(
                static_cast<std::int64_t>(remainder_ns))
            .Multiply(
                logical_output_rate_,
                kNanosecondsPerSecond);
        if (!remainder)
        {
            return std::unexpected(MapRationalFailure(remainder.error()));
        }
        const auto result = base->Add(*remainder);
        if (!result)
        {
            return std::unexpected(MapRationalFailure(result.error()));
        }
        return *result;
    }

    std::expected<std::uint64_t, LogicalPresentationClockFailure>
    LogicalPresentationClock::WholeFrame(
        const std::expected<gc::timing::CheckedRational,
                            LogicalPresentationClockFailure>& coordinate) noexcept
    {
        if (!coordinate)
        {
            return std::unexpected(coordinate.error());
        }
        if (coordinate->numerator() < 0)
        {
            return std::unexpected(
                LogicalPresentationClockFailure::NegativeCoordinate);
        }

        const auto whole = coordinate->Floor();
        if (!whole)
        {
            return std::unexpected(MapRationalFailure(whole.error()));
        }
        if (*whole < 0)
        {
            return std::unexpected(
                LogicalPresentationClockFailure::NegativeCoordinate);
        }
        return static_cast<std::uint64_t>(*whole);
    }

    std::expected<std::uint64_t, LogicalPresentationClockFailure>
    LogicalPresentationClock::WholeFrameAt(
        const std::uint32_t raw_ms) const noexcept
    {
        return WholeFrame(ProjectMultimediaMilliseconds(raw_ms));
    }

    std::expected<std::uint64_t, LogicalPresentationClockFailure>
    LogicalPresentationClock::WholeFrameAtSystemTime(
        const std::uint64_t system_time_ns) const noexcept
    {
        return WholeFrame(ProjectSystemTimeNanoseconds(system_time_ns));
    }

    ExactJudgementTimelineResult LogicalPresentationClock::Resolve(
        const gc::timing::AbsoluteHostTime& timestamp,
        const ExactClockResolveIntent intent) const noexcept
    {
        if ((intent != ExactClockResolveIntent::FinalizedTimestamp &&
                intent != ExactClockResolveIntent::ProvisionalHorizon) ||
            invalidated_.load(std::memory_order_acquire))
        {
            return CountResult(Result(
                ExactClockStatus::Discontinuous, timeline_generation_));
        }

        const auto logical_output_frame =
            ProjectMultimediaMilliseconds(timestamp.multimedia_time_ms);
        if (!logical_output_frame)
        {
            const auto status =
                logical_output_frame.error() ==
                LogicalPresentationClockFailure::SnapshotUnavailable
                    ? ExactClockStatus::TemporarilyUnavailable
                    : ExactClockStatus::Discontinuous;
            return CountResult(Result(status, timeline_generation_));
        }
        if (logical_output_frame->numerator() < 0 ||
            invalidated_.load(std::memory_order_acquire))
        {
            return CountResult(Result(
                ExactClockStatus::Discontinuous, timeline_generation_));
        }

        return CountResult({
            .status = ExactClockStatus::Resolved,
            .timeline_generation = timeline_generation_,
            .logical_output_frame = *logical_output_frame,
            .available_output_tail = 0,
            .provider_anchor_sequence = 0,
            .provider_position = std::nullopt,
        });
    }

    ExactJudgementTimelineInfo LogicalPresentationClock::info() const noexcept
    {
        return {
            .domain =
            ExactJudgementTimelineDomain::LogicalMultimediaMilliseconds,
            .timeline_generation = timeline_generation_,
            .qpc_frequency = qpc_frequency_,
            .logical_output_rate = logical_output_rate_,
            .provider_period_frames = 0,
            .provider_output_latency_frames = 0,
            .timestamp_quantum_ns =
            static_cast<std::uint32_t>(kNanosecondsPerMillisecond),
        };
    }

    ExactJudgementTimelineCounters
    LogicalPresentationClock::counters() const noexcept
    {
        return {
            .publication_count = 0,
            .resolved_queries =
            resolved_queries_.load(std::memory_order_relaxed),
            .pending_queries =
            pending_queries_.load(std::memory_order_relaxed),
            .temporarily_unavailable_queries =
            temporarily_unavailable_queries_.load(std::memory_order_relaxed),
            .history_lost_queries =
            history_lost_queries_.load(std::memory_order_relaxed),
            .discontinuous_queries =
            discontinuous_queries_.load(std::memory_order_relaxed),
        };
    }

    void LogicalPresentationClock::Invalidate() noexcept
    {
        invalidated_.store(true, std::memory_order_release);
    }

    std::uint32_t LogicalPresentationClock::origin_raw_ms() const noexcept
    {
        return origin_raw_ms_;
    }

    ExactJudgementTimelineResult LogicalPresentationClock::CountResult(
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

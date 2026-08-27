#include "Audio/Asio/AsioLogicalTimeline.h"

#include <bit>
#include <limits>
#include <new>
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

        AsioLogicalTimelineFailure MapRationalFailure(
            const gc::timing::RationalError) noexcept
        {
            return AsioLogicalTimelineFailure::ArithmeticOverflow;
        }

        std::expected<std::uint64_t, AsioLogicalTimelineFailure> AddSignedDelta(
            const std::uint64_t base,
            const std::int32_t delta) noexcept
        {
            if (delta < 0)
            {
                const auto magnitude = std::uint64_t{0} -
                    static_cast<std::uint64_t>(delta);
                if (magnitude > base)
                {
                    return std::unexpected(
                        AsioLogicalTimelineFailure::NegativeCoordinate);
                }
                return base - magnitude;
            }

            const auto positive_delta = static_cast<std::uint64_t>(delta);
            if (positive_delta >
                (std::numeric_limits<std::uint64_t>::max)() - base)
            {
                return std::unexpected(
                    AsioLogicalTimelineFailure::ArithmeticOverflow);
            }
            return base + positive_delta;
        }
    } // namespace

    AsioLogicalTimeline::AsioLogicalTimeline(
        const std::uint32_t origin_raw_ms,
        const std::uint32_t output_sample_rate) noexcept
        : origin_raw_ms_(origin_raw_ms),
          output_sample_rate_(output_sample_rate),
          writer_raw_ms_(origin_raw_ms),
          observed_raw_ms_(origin_raw_ms)
    {
    }

    std::shared_ptr<AsioLogicalTimeline> AsioLogicalTimeline::Create(
        const std::uint32_t origin_raw_ms,
        const std::uint32_t output_sample_rate) noexcept
    {
        if (output_sample_rate == 0)
        {
            return {};
        }

        std::unique_ptr < AsioLogicalTimeline > timeline{
            new(std::nothrow)
            AsioLogicalTimeline(origin_raw_ms, output_sample_rate)
        };
        if (!timeline)
        {
            return {};
        }

        try
        {
            return std::shared_ptr < AsioLogicalTimeline >
            {
                std::move(timeline)
            };
        }
        catch (...)
        {
            return {};
        }
    }

    std::expected<void, AsioLogicalTimelineFailure>
    AsioLogicalTimeline::AdvanceNow(const std::uint32_t observed_raw_ms) noexcept
    {
        const std::uint32_t delta = observed_raw_ms - writer_raw_ms_;
        if (delta > static_cast<std::uint32_t>(
            (std::numeric_limits<std::int32_t>::max)()))
        {
            return std::unexpected(
                AsioLogicalTimelineFailure::WriterDeltaAmbiguous);
        }
        if (delta > (std::numeric_limits<std::uint64_t>::max)() -
            writer_unwrapped_ms_)
        {
            return std::unexpected(
                AsioLogicalTimelineFailure::ArithmeticOverflow);
        }

        const std::uint64_t version =
            snapshot_version_.load(std::memory_order_relaxed);
        if ((version & 1U) != 0 ||
            version > (std::numeric_limits<std::uint64_t>::max)() - 2)
        {
            return std::unexpected(
                AsioLogicalTimelineFailure::ArithmeticOverflow);
        }

        writer_raw_ms_ = observed_raw_ms;
        writer_unwrapped_ms_ += delta;

        snapshot_version_.fetch_add(1, std::memory_order_acq_rel);
        observed_raw_ms_.store(writer_raw_ms_, std::memory_order_relaxed);
        observed_unwrapped_ms_.store(writer_unwrapped_ms_,
                                     std::memory_order_relaxed);
        snapshot_version_.store(version + 2, std::memory_order_release);
        return {};
    }

    std::expected<AsioLogicalTimeline::Snapshot, AsioLogicalTimelineFailure>
    AsioLogicalTimeline::ReadSnapshot() const noexcept
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
            AsioLogicalTimelineFailure::SnapshotUnavailable);
    }

    std::expected<std::uint64_t, AsioLogicalTimelineFailure>
    AsioLogicalTimeline::Unwrap(const std::uint32_t raw_ms) const noexcept
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
                AsioLogicalTimelineFailure::TimestampAmbiguous);
        }
        const std::int32_t signed_delta =
            std::bit_cast<std::int32_t>(wrapped_delta);
        return AddSignedDelta(snapshot->observed_unwrapped_ms, signed_delta);
    }

    std::expected<gc::timing::CheckedRational, AsioLogicalTimelineFailure>
    AsioLogicalTimeline::ProjectUnwrappedMilliseconds(
        const std::uint64_t unwrapped_ms) const noexcept
    {
        if (unwrapped_ms > static_cast<std::uint64_t>(
            (std::numeric_limits<std::int64_t>::max)()))
        {
            return std::unexpected(
                AsioLogicalTimelineFailure::ArithmeticOverflow);
        }

        const auto result = gc::timing::CheckedRational::Whole(
                static_cast<std::int64_t>(unwrapped_ms))
            .Multiply(output_sample_rate_,
                      kMillisecondsPerSecond);
        if (!result)
        {
            return std::unexpected(MapRationalFailure(result.error()));
        }
        return *result;
    }

    std::expected<gc::timing::CheckedRational, AsioLogicalTimelineFailure>
    AsioLogicalTimeline::ProjectMultimediaMilliseconds(
        const std::uint32_t raw_ms) const noexcept
    {
        const auto unwrapped = Unwrap(raw_ms);
        if (!unwrapped)
        {
            return std::unexpected(unwrapped.error());
        }
        return ProjectUnwrappedMilliseconds(*unwrapped);
    }

    std::expected<gc::timing::CheckedRational, AsioLogicalTimelineFailure>
    AsioLogicalTimeline::ProjectSystemTimeNanoseconds(
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
            .Multiply(output_sample_rate_,
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

    std::expected<std::uint64_t, AsioLogicalTimelineFailure>
    AsioLogicalTimeline::WholeFrame(
        const std::expected<gc::timing::CheckedRational,
                            AsioLogicalTimelineFailure>& coordinate) noexcept
    {
        if (!coordinate)
        {
            return std::unexpected(coordinate.error());
        }
        if (coordinate->numerator() < 0)
        {
            return std::unexpected(
                AsioLogicalTimelineFailure::NegativeCoordinate);
        }

        const auto whole = coordinate->Floor();
        if (!whole)
        {
            return std::unexpected(MapRationalFailure(whole.error()));
        }
        if (*whole < 0)
        {
            return std::unexpected(
                AsioLogicalTimelineFailure::NegativeCoordinate);
        }
        return static_cast<std::uint64_t>(*whole);
    }

    std::expected<std::uint64_t, AsioLogicalTimelineFailure>
    AsioLogicalTimeline::WholePresentedFrameAt(
        const std::uint32_t raw_ms) const noexcept
    {
        return WholeFrame(ProjectMultimediaMilliseconds(raw_ms));
    }

    std::expected<std::uint64_t, AsioLogicalTimelineFailure>
    AsioLogicalTimeline::WholePresentedFrameAtSystemTime(
        const std::uint64_t system_time_ns) const noexcept
    {
        return WholeFrame(ProjectSystemTimeNanoseconds(system_time_ns));
    }

    std::uint32_t AsioLogicalTimeline::origin_raw_ms() const noexcept
    {
        return origin_raw_ms_;
    }

    std::uint32_t AsioLogicalTimeline::output_sample_rate() const noexcept
    {
        return output_sample_rate_;
    }
} // namespace gc::audio

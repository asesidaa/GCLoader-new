#include "Patches/Framerate/FramerateMonitor.h"

#include "Patches/Framerate/FrameratePolicy.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <span>

namespace gc::framerate
{
    FramerateMonitor::FramerateMonitor(
        std::uint32_t target_fps,
        std::int64_t qpc_frequency) noexcept
        : target_fps_{target_fps},
          qpc_frequency_{qpc_frequency},
          warmup_ticks_{qpc_frequency * 5},
          window_ticks_{qpc_frequency * 2}
    {
    }

    std::expected<FramerateMonitor, FramerateMonitorError>
    FramerateMonitor::Create(
        std::uint32_t target_fps,
        std::int64_t qpc_frequency) noexcept
    {
        if (!IsTargetFpsInRange(target_fps))
        {
            return std::unexpected(FramerateMonitorError::TargetOutOfRange);
        }
        if (qpc_frequency <= 0)
        {
            return std::unexpected(FramerateMonitorError::InvalidQpcFrequency);
        }
        if (qpc_frequency > std::numeric_limits<std::int64_t>::max() / 5)
        {
            return std::unexpected(FramerateMonitorError::QpcRangeOverflow);
        }
        return FramerateMonitor{target_fps, qpc_frequency};
    }

    std::optional<FramerateObservation> FramerateMonitor::Observe(
        std::int64_t qpc_timestamp) noexcept
    {
        if (!active_)
        {
            return std::nullopt;
        }
        if (!started_)
        {
            started_ = true;
            first_timestamp_ = qpc_timestamp;
            previous_timestamp_ = qpc_timestamp;
            return std::nullopt;
        }
        if (qpc_timestamp <= previous_timestamp_)
        {
            return FatalClock();
        }

        if (warming_up_)
        {
            previous_timestamp_ = qpc_timestamp;
            if (qpc_timestamp - first_timestamp_ < warmup_ticks_)
            {
                return std::nullopt;
            }
            warming_up_ = false;
            window_start_ = qpc_timestamp;
            return std::nullopt;
        }

        const auto interval = qpc_timestamp - previous_timestamp_;
        previous_timestamp_ = qpc_timestamp;
        if (interval_count_ < intervals_.size())
        {
            intervals_[interval_count_++] = interval;
        }
        else
        {
            storage_overflowed_ = true;
        }

        if (qpc_timestamp - window_start_ < window_ticks_)
        {
            return std::nullopt;
        }

        auto result = FinishWindow();
        window_start_ = qpc_timestamp;
        interval_count_ = 0;
        storage_overflowed_ = false;
        return result;
    }

    FramerateObservation FramerateMonitor::FinishWindow() noexcept
    {
        double measured_fps = 0.0;
        if (interval_count_ != 0)
        {
            std::ranges::sort(
                std::span{intervals_.data(), interval_count_});
            double median_ticks = 0.0;
            const auto middle = interval_count_ / 2;
            if ((interval_count_ & 1U) != 0)
            {
                median_ticks = static_cast<double>(intervals_[middle]);
            }
            else
            {
                median_ticks =
                    (static_cast<double>(intervals_[middle - 1]) +
                        static_cast<double>(intervals_[middle])) /
                    2.0;
            }
            if (median_ticks > 0.0)
            {
                measured_fps =
                    static_cast<double>(qpc_frequency_) / median_ticks;
            }
        }

        const double relative_error = std::fabs(
                measured_fps - static_cast<double>(target_fps_)) /
            static_cast<double>(target_fps_);
        const bool matches =
            !storage_overflowed_ && interval_count_ != 0 &&
            relative_error <= kFramerateTolerance;

        FramerateDecision decision{};
        if (matches)
        {
            ++matching_streak_;
            mismatching_streak_ = 0;
            decision = matching_streak_ >= kFramerateRequiredStreak
                           ? FramerateDecision::Validated
                           : FramerateDecision::WindowMatch;
        }
        else
        {
            ++mismatching_streak_;
            matching_streak_ = 0;
            decision = mismatching_streak_ >= kFramerateRequiredStreak
                           ? FramerateDecision::FatalMismatch
                           : FramerateDecision::WindowMismatch;
        }

        if (decision == FramerateDecision::Validated ||
            decision == FramerateDecision::FatalMismatch)
        {
            active_ = false;
        }

        return {
            .decision = decision,
            .target_fps = target_fps_,
            .measured_fps = measured_fps,
            .relative_error = relative_error,
            .interval_count = interval_count_,
            .matching_streak = matching_streak_,
            .mismatching_streak = mismatching_streak_,
            .storage_overflowed = storage_overflowed_,
        };
    }

    FramerateObservation FramerateMonitor::FatalClock() noexcept
    {
        active_ = false;
        return {
            .decision = FramerateDecision::FatalClock,
            .target_fps = target_fps_,
            .relative_error = 1.0,
            .mismatching_streak = kFramerateRequiredStreak,
        };
    }
} // namespace gc::framerate

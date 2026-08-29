#pragma once

#include "Audio/ExactJudgementTimeline.h"

#include <atomic>
#include <cstdint>
#include <expected>
#include <memory>

namespace gc::audio
{
    enum class LogicalPresentationClockFailure : std::uint8_t
    {
        InvalidConfiguration,
        WriterDeltaAmbiguous,
        TimestampAmbiguous,
        SnapshotUnavailable,
        NegativeCoordinate,
        ArithmeticOverflow,
    };

    class LogicalPresentationClock final : public ExactJudgementTimeline
    {
    public:
        [[nodiscard]] static std::shared_ptr<LogicalPresentationClock> Create(
            std::uint64_t timeline_generation,
            std::uint32_t origin_raw_ms,
            std::uint32_t logical_output_rate,
            std::int64_t qpc_frequency) noexcept;

        [[nodiscard]] std::expected<void, LogicalPresentationClockFailure>
        ObserveNow(std::uint32_t raw_ms) noexcept;
        [[nodiscard]] std::expected<gc::timing::CheckedRational,
                                    LogicalPresentationClockFailure>
        ProjectMultimediaMilliseconds(std::uint32_t raw_ms) const noexcept;
        [[nodiscard]] std::expected<gc::timing::CheckedRational,
                                    LogicalPresentationClockFailure>
        ProjectSystemTimeNanoseconds(std::uint64_t system_time_ns) const noexcept;
        [[nodiscard]] std::expected<std::uint64_t,
                                    LogicalPresentationClockFailure>
        WholeFrameAt(std::uint32_t raw_ms) const noexcept;
        [[nodiscard]] std::expected<std::uint64_t,
                                    LogicalPresentationClockFailure>
        WholeFrameAtSystemTime(std::uint64_t system_time_ns) const noexcept;

        [[nodiscard]] ExactJudgementTimelineResult Resolve(
            const gc::timing::AbsoluteHostTime& timestamp,
            ExactClockResolveIntent intent) const noexcept override;
        [[nodiscard]] ExactJudgementTimelineInfo info() const noexcept override;
        [[nodiscard]] ExactJudgementTimelineCounters counters() const noexcept override;
        void Invalidate() noexcept override;

        [[nodiscard]] std::uint32_t origin_raw_ms() const noexcept;

    private:
        struct Snapshot final
        {
            std::uint32_t observed_raw_ms{};
            std::uint64_t observed_unwrapped_ms{};
        };

        LogicalPresentationClock(
            std::uint64_t timeline_generation,
            std::uint32_t origin_raw_ms,
            std::uint32_t logical_output_rate,
            std::int64_t qpc_frequency) noexcept;

        [[nodiscard]] std::expected<Snapshot, LogicalPresentationClockFailure>
        ReadSnapshot() const noexcept;
        [[nodiscard]] std::expected<std::uint64_t, LogicalPresentationClockFailure>
        Unwrap(std::uint32_t raw_ms) const noexcept;
        [[nodiscard]] std::expected<gc::timing::CheckedRational,
                                    LogicalPresentationClockFailure>
        ProjectUnwrappedMilliseconds(std::uint64_t unwrapped_ms) const noexcept;
        [[nodiscard]] static std::expected<std::uint64_t,
                                           LogicalPresentationClockFailure>
        WholeFrame(const std::expected<gc::timing::CheckedRational,
                                       LogicalPresentationClockFailure>& coordinate) noexcept;
        [[nodiscard]] ExactJudgementTimelineResult CountResult(
            ExactJudgementTimelineResult&& result) const noexcept;

        const std::uint64_t timeline_generation_;
        const std::uint32_t origin_raw_ms_;
        const std::uint32_t logical_output_rate_;
        const std::int64_t qpc_frequency_;

        std::uint32_t writer_raw_ms_;
        std::uint64_t writer_unwrapped_ms_{};
        std::atomic_uint64_t snapshot_version_{};
        std::atomic_uint32_t observed_raw_ms_;
        std::atomic_uint64_t observed_unwrapped_ms_{};
        std::atomic_bool invalidated_{};
        mutable std::atomic_uint64_t resolved_queries_{};
        mutable std::atomic_uint64_t pending_queries_{};
        mutable std::atomic_uint64_t temporarily_unavailable_queries_{};
        mutable std::atomic_uint64_t history_lost_queries_{};
        mutable std::atomic_uint64_t discontinuous_queries_{};
    };
} // namespace gc::audio

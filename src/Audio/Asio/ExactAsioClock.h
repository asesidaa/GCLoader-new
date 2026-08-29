#pragma once

#include "Audio/Asio/AsioLogicalTimeline.h"
#include "Audio/ExactJudgementTimeline.h"

#include <atomic>
#include <cstdint>
#include <memory>

namespace gc::audio
{
    class ExactAsioClock final : public ExactJudgementTimeline
    {
    public:
        ~ExactAsioClock() override;

        [[nodiscard]] static std::shared_ptr<ExactAsioClock> Create(
            std::uint64_t timeline_generation,
            std::shared_ptr<const AsioLogicalTimeline> timeline,
            std::int64_t qpc_frequency,
            std::uint32_t period_frames,
            std::uint32_t output_latency_frames) noexcept;

        [[nodiscard]] ExactJudgementTimelineResult Resolve(
            const timing::AbsoluteHostTime& timestamp,
            ExactClockResolveIntent intent) const noexcept override;
        [[nodiscard]] ExactJudgementTimelineInfo info() const noexcept override;
        [[nodiscard]] ExactJudgementTimelineCounters counters() const noexcept override;
        void Invalidate() noexcept override;

    private:
        ExactAsioClock(
            std::uint64_t timeline_generation,
            std::shared_ptr<const AsioLogicalTimeline> timeline,
            std::int64_t qpc_frequency,
            std::uint32_t period_frames,
            std::uint32_t output_latency_frames) noexcept;

        [[nodiscard]] ExactJudgementTimelineResult CountResult(
            ExactJudgementTimelineResult&& result) const noexcept;

        std::uint64_t timeline_generation_{};
        std::shared_ptr<const AsioLogicalTimeline> timeline_;
        std::int64_t qpc_frequency_{};
        std::uint32_t period_frames_{};
        std::uint32_t output_latency_frames_{};
        std::atomic_bool invalidated_{};
        mutable std::atomic_uint64_t resolved_queries_{};
        mutable std::atomic_uint64_t pending_queries_{};
        mutable std::atomic_uint64_t temporarily_unavailable_queries_{};
        mutable std::atomic_uint64_t history_lost_queries_{};
        mutable std::atomic_uint64_t discontinuous_queries_{};
    };
} // namespace gc::audio

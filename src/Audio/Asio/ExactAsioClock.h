#pragma once

#include "Audio/Asio/AsioLogicalTimeline.h"
#include "Audio/Asio/AsioSubmittedOutputTail.h"
#include "Audio/ExactOutputClock.h"

#include <atomic>
#include <cstdint>
#include <memory>

namespace gc::audio
{
    class ExactAsioClock final : public ExactOutputClock
    {
    public:
        ~ExactAsioClock() override;

        [[nodiscard]] static std::shared_ptr<ExactAsioClock> Create(
            std::uint64_t endpoint_generation,
            std::shared_ptr<const AsioLogicalTimeline> timeline,
            std::shared_ptr<const AsioSubmittedOutputTail> submitted_tail,
            std::int64_t qpc_frequency,
            std::uint32_t period_frames,
            std::uint32_t output_latency_frames) noexcept;

        [[nodiscard]] ExactOutputClockResult Resolve(
            const gc::timing::AbsoluteHostTime& timestamp,
            ExactClockResolveIntent intent) const noexcept override;
        [[nodiscard]] ExactOutputClockInfo info() const noexcept override;
        [[nodiscard]] ExactOutputClockCounters counters() const noexcept override;
        void Invalidate() noexcept override;

    private:
        ExactAsioClock(
            std::uint64_t endpoint_generation,
            std::shared_ptr<const AsioLogicalTimeline> timeline,
            std::shared_ptr<const AsioSubmittedOutputTail> submitted_tail,
            std::int64_t qpc_frequency,
            std::uint32_t period_frames,
            std::uint32_t output_latency_frames) noexcept;

        [[nodiscard]] ExactOutputClockResult CountResult(
            ExactOutputClockResult result) const noexcept;

        std::uint64_t endpoint_generation_{};
        std::shared_ptr<const AsioLogicalTimeline> timeline_;
        std::shared_ptr<const AsioSubmittedOutputTail> submitted_tail_;
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

#pragma once

#include "Audio/ExactAudioTime.h"
#include "Timing/AbsoluteHostTime.h"

#include <cstdint>
#include <memory>
#include <string_view>

namespace gc::audio
{
    enum class ExactJudgementTimelineDomain : std::uint8_t
    {
        WasapiQpc,
        LogicalMultimediaMilliseconds,
    };

    [[nodiscard]] std::string_view ExactJudgementTimelineDomainName(ExactJudgementTimelineDomain domain) noexcept;

    struct ExactJudgementTimelineInfo final
    {
        ExactJudgementTimelineDomain domain{};
        std::uint64_t timeline_generation{};
        std::int64_t qpc_frequency{};
        std::uint32_t logical_output_rate{};
        std::uint32_t provider_period_frames{};
        std::uint32_t provider_output_latency_frames{};
        std::uint32_t timestamp_quantum_ns{};
    };

    struct ExactJudgementTimelineCounters final
    {
        std::uint64_t publication_count{};
        std::uint64_t resolved_queries{};
        std::uint64_t pending_queries{};
        std::uint64_t temporarily_unavailable_queries{};
        std::uint64_t history_lost_queries{};
        std::uint64_t discontinuous_queries{};
    };

    enum class ExactClockResolveIntent : std::uint8_t
    {
        FinalizedTimestamp,
        ProvisionalHorizon,
    };

    class ExactJudgementTimeline
    {
    public:
        virtual ~ExactJudgementTimeline() = default;

        [[nodiscard]] virtual ExactJudgementTimelineResult Resolve(const gc::timing::AbsoluteHostTime& timestamp,
                                                                   ExactClockResolveIntent intent) const noexcept = 0;
        [[nodiscard]] virtual ExactJudgementTimelineInfo info() const noexcept = 0;
        [[nodiscard]] virtual ExactJudgementTimelineCounters counters() const noexcept = 0;
        virtual void Invalidate() noexcept = 0;
    };

    [[nodiscard]] std::shared_ptr<const ExactJudgementTimeline> AcquireExactJudgementTimeline() noexcept;

    namespace detail
    {
        [[nodiscard]] std::uint64_t NextExactJudgementTimelineGeneration() noexcept;
        [[nodiscard]] bool RegisterExactJudgementTimeline(
            const std::shared_ptr<ExactJudgementTimeline>& provider) noexcept;
        void UnregisterExactJudgementTimeline(std::uint64_t expected_generation) noexcept;
    } // namespace detail
} // namespace gc::audio

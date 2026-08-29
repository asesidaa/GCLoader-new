#pragma once

#include "Timing/CheckedRational.h"

#include <cstdint>
#include <optional>

namespace gc::audio
{
    enum class ExactClockStatus : std::uint8_t
    {
        NoPlayback,
        Pending,
        OutsidePlayback,
        Resolved,
        TemporarilyUnavailable,
        HistoryLost,
        Discontinuous,
    };

    struct EndpointClockMapping
    {
        std::uint64_t origin_position{};
        std::uint64_t clock_frequency{};
        std::uint64_t origin_output_frame{};
        std::uint32_t output_sample_rate{};
    };

    struct ExactJudgementTimelineResult
    {
        ExactClockStatus status{};
        std::uint64_t timeline_generation{};
        std::optional<gc::timing::CheckedRational> logical_output_frame;
        std::uint64_t available_output_tail{};
        std::uint64_t provider_anchor_sequence{};
        std::optional<std::uint64_t> provider_position;
    };
} // namespace gc::audio

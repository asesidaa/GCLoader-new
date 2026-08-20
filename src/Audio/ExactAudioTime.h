#pragma once

#include "Timing/CheckedRational.h"

#include <cstdint>
#include <optional>

namespace gc::audio {

enum class ExactClockStatus : std::uint8_t {
    NoPlayback,
    Pending,
    OutsidePlayback,
    Resolved,
    TemporarilyUnavailable,
    HistoryLost,
    Discontinuous,
};

struct EndpointClockMapping {
    std::uint64_t origin_position{};
    std::uint64_t clock_frequency{};
    std::uint64_t origin_output_frame{};
    std::uint32_t output_sample_rate{};
};

struct ExactOutputClockResult {
    ExactClockStatus status{};
    std::uint64_t endpoint_generation{};
    std::optional<gc::timing::CheckedRational> output_frame;
    std::uint64_t submitted_output_tail{};
};

} // namespace gc::audio

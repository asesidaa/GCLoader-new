#pragma once

#include <array>
#include <cstdint>

namespace gc::audio {

enum class OutputPacingDecisionKind : std::uint8_t {
    Sequential,
    RecoverableGap,
    ChronicGap,
    InvalidClock,
};

struct OutputPacingDecision {
    OutputPacingDecisionKind kind{};
    std::uint64_t block_begin{};
    std::uint64_t block_end{};
    std::uint64_t discontinuity_begin{};
    std::uint64_t discontinuity_frames{};
    std::int64_t submitted_lead_frames{};
};

class OutputPacingTracker {
public:
    explicit OutputPacingTracker(
        std::uint32_t packet_frames,
        std::uint32_t output_sample_rate) noexcept;

    OutputPacingDecision Plan(std::uint64_t presented_frame) noexcept;
    bool Commit(const OutputPacingDecision&) noexcept;
    std::uint64_t submitted_tail() const noexcept;

private:
    bool RecordGap(std::uint64_t presented_frame) noexcept;

    std::uint32_t packet_frames_{};
    std::uint64_t gap_window_frames_{};
    std::uint64_t submitted_tail_{};
    std::uint64_t last_presented_frame_{};
    bool has_last_presentation_{};
    std::array<std::uint64_t, 3> gap_positions_{};
    std::uint8_t gap_count_{};
};

} // namespace gc::audio

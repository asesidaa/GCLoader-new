#include "Audio/Wasapi/OutputPacingTracker.h"

#include <limits>

namespace gc::audio {
namespace {

std::int64_t SaturatingSignedDifference(
    std::uint64_t left,
    std::uint64_t right) noexcept {
    constexpr auto signed_max = static_cast<std::uint64_t>(
        std::numeric_limits<std::int64_t>::max());
    if (left >= right) {
        const auto difference = left - right;
        return difference > signed_max
            ? std::numeric_limits<std::int64_t>::max()
            : static_cast<std::int64_t>(difference);
    }
    const auto difference = right - left;
    return difference > signed_max
        ? std::numeric_limits<std::int64_t>::min()
        : -static_cast<std::int64_t>(difference);
}

bool AlignUp(
    std::uint64_t value,
    std::uint32_t alignment,
    std::uint64_t* result) noexcept {
    if (alignment == 0 || result == nullptr) {
        return false;
    }
    const auto remainder = value % alignment;
    if (remainder == 0) {
        *result = value;
        return true;
    }
    const auto adjustment = alignment - remainder;
    if (value > std::numeric_limits<std::uint64_t>::max() - adjustment) {
        return false;
    }
    *result = value + adjustment;
    return true;
}

} // namespace

OutputPacingTracker::OutputPacingTracker(
    std::uint32_t packet_frames,
    std::uint32_t output_sample_rate) noexcept
    : packet_frames_(packet_frames),
      gap_window_frames_(output_sample_rate),
      submitted_tail_(packet_frames) {}

OutputPacingDecision OutputPacingTracker::Plan(
    std::uint64_t presented_frame) noexcept {
    if (packet_frames_ == 0 || gap_window_frames_ == 0 ||
        (has_last_presentation_ &&
         presented_frame < last_presented_frame_)) {
        return {.kind = OutputPacingDecisionKind::InvalidClock};
    }

    OutputPacingDecision decision{
        .kind = OutputPacingDecisionKind::Sequential,
        .block_begin = submitted_tail_,
        .discontinuity_begin = submitted_tail_,
        .submitted_lead_frames = SaturatingSignedDifference(
            submitted_tail_, presented_frame),
    };
    const bool has_gap = presented_frame > submitted_tail_;
    if (has_gap) {
        if (!AlignUp(
                presented_frame,
                packet_frames_,
                &decision.block_begin)) {
            return {.kind = OutputPacingDecisionKind::InvalidClock};
        }
        decision.discontinuity_frames =
            decision.block_begin - submitted_tail_;
    }

    if (decision.block_begin >
        std::numeric_limits<std::uint64_t>::max() - packet_frames_) {
        return {.kind = OutputPacingDecisionKind::InvalidClock};
    }
    decision.block_end = decision.block_begin + packet_frames_;
    if (has_gap) {
        decision.kind = RecordGap(presented_frame)
            ? OutputPacingDecisionKind::ChronicGap
            : OutputPacingDecisionKind::RecoverableGap;
    }
    last_presented_frame_ = presented_frame;
    has_last_presentation_ = true;
    return decision;
}

bool OutputPacingTracker::Commit(
    const OutputPacingDecision& decision) noexcept {
    if (packet_frames_ == 0 || gap_window_frames_ == 0 ||
        (decision.kind != OutputPacingDecisionKind::Sequential &&
         decision.kind != OutputPacingDecisionKind::RecoverableGap) ||
        decision.discontinuity_begin != submitted_tail_ ||
        decision.block_begin < submitted_tail_ ||
        decision.block_end < decision.block_begin ||
        decision.block_end - decision.block_begin != packet_frames_ ||
        decision.discontinuity_frames !=
            decision.block_begin - submitted_tail_ ||
        (decision.kind == OutputPacingDecisionKind::Sequential &&
         decision.discontinuity_frames != 0) ||
        (decision.kind == OutputPacingDecisionKind::RecoverableGap &&
         decision.discontinuity_frames == 0)) {
        return false;
    }
    submitted_tail_ = decision.block_end;
    return true;
}

std::uint64_t OutputPacingTracker::submitted_tail() const noexcept {
    return submitted_tail_;
}

bool OutputPacingTracker::RecordGap(
    std::uint64_t presented_frame) noexcept {
    std::uint8_t retained{};
    for (std::uint8_t index = 0; index < gap_count_; ++index) {
        if (presented_frame - gap_positions_[index] < gap_window_frames_) {
            gap_positions_[retained++] = gap_positions_[index];
        }
    }
    gap_count_ = retained;
    if (gap_count_ < gap_positions_.size()) {
        gap_positions_[gap_count_++] = presented_frame;
    }
    return gap_count_ >= gap_positions_.size();
}

} // namespace gc::audio

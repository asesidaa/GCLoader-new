#include "Patches/AbsoluteJudgement/JudgementStage.h"

#include <atomic>
#include <limits>

namespace gc::absolute_judgement {
namespace {

std::atomic_uint64_t g_next_stage_generation{1};

} // namespace

std::expected<void, JudgementStageError> JudgementStage::Begin(
    const std::uintptr_t tune_manager,
    const gc::timing::AbsoluteHostTime& stage_entry_time,
    const std::int32_t game_time_offset_ms,
    const std::int32_t hold_safe_frame,
    const std::int32_t slide_hold_safe_frame) noexcept {
    if (open_) {
        return std::unexpected(JudgementStageError::AlreadyOpen);
    }

    Reset();
    const auto generation = g_next_stage_generation.fetch_add(
        1, std::memory_order_relaxed);
    if (generation == 0 ||
        generation == (std::numeric_limits<std::uint64_t>::max)()) {
        return std::unexpected(JudgementStageError::GenerationExhausted);
    }

    generation_ = generation;
    tune_manager_ = tune_manager;
    entry_game_time_offset_ms_ = game_time_offset_ms;
    entry_hold_safe_frame_ = hold_safe_frame;
    entry_slide_hold_safe_frame_ = slide_hold_safe_frame;
    open_ = true;
    if (tune_manager == 0) {
        return std::unexpected(JudgementStageError::TuneManagerMissing);
    }
    if (!gc::input::CaptureGameplayTransitionCutoff(
            stage_entry_time, &cutoff_) || cutoff_.transport_epoch == 0) {
        failure_transport_status_ =
            gc::input::ReadGameplayTransitionStatus();
        if (failure_transport_status_.next_sequence ==
            (std::numeric_limits<std::uint64_t>::max)()) {
            return std::unexpected(
                JudgementStageError::InputSequenceExhausted);
        }
        return std::unexpected(
            JudgementStageError::InputTransportInactiveAtStageEntry);
    }
    if (cutoff_.qpc_frequency <= 0) {
        return std::unexpected(
            JudgementStageError::InputQpcFrequencyInvalidAtStageEntry);
    }
    if (hold_safe_frame != 0) {
        return std::unexpected(JudgementStageError::HoldSafeFrameNonZero);
    }
    if (slide_hold_safe_frame != 0) {
        return std::unexpected(
            JudgementStageError::SlideHoldSafeFrameNonZero);
    }
    return {};
}

std::expected<void, JudgementStageError>
JudgementStage::BindOrValidateNative(
    const NativeJudgementIdentity& native) noexcept {
    if (!open_) {
        return std::unexpected(JudgementStageError::StageNotOpen);
    }
    if (native.stage_generation != generation_) {
        return std::unexpected(JudgementStageError::StageGenerationChanged);
    }
    if (native.tune_manager != tune_manager_) {
        return std::unexpected(JudgementStageError::TuneManagerChanged);
    }
    if (native.tune == 0) {
        return std::unexpected(JudgementStageError::TuneMissing);
    }
    if (native.judgement_state == 0) {
        return std::unexpected(JudgementStageError::JudgementStateMissing);
    }
    if (native.score_state == 0) {
        return std::unexpected(JudgementStageError::ScoreStateMissing);
    }
    if (native.booster == 0) {
        return std::unexpected(JudgementStageError::BoosterMissing);
    }
    if (native.hold_safe_frame != 0) {
        return std::unexpected(JudgementStageError::HoldSafeFrameNonZero);
    }
    if (native.slide_hold_safe_frame != 0) {
        return std::unexpected(
            JudgementStageError::SlideHoldSafeFrameNonZero);
    }
    if (!bound_) {
        native_ = native;
        native_.game_time_offset_ms = entry_game_time_offset_ms_;
        native_.hold_safe_frame = entry_hold_safe_frame_;
        native_.slide_hold_safe_frame = entry_slide_hold_safe_frame_;
        bound_ = true;
        return {};
    }
    if (native_.tune != native.tune) {
        return std::unexpected(JudgementStageError::TuneChanged);
    }
    if (native_.judgement_state != native.judgement_state) {
        return std::unexpected(JudgementStageError::JudgementStateChanged);
    }
    if (native_.score_state != native.score_state) {
        return std::unexpected(JudgementStageError::ScoreStateChanged);
    }
    if (native_.booster != native.booster) {
        return std::unexpected(JudgementStageError::BoosterChanged);
    }
    if (native_.player != native.player) {
        return std::unexpected(JudgementStageError::PlayerChanged);
    }
    return {};
}

std::expected<void, JudgementStageError>
JudgementStage::BindTimelineOrValidate(
    const std::uint64_t timeline_generation,
    const std::int64_t timeline_qpc_frequency) noexcept {
    if (!open_ || !bound_ || timeline_generation == 0 ||
        timeline_qpc_frequency <= 0) {
        return std::unexpected(JudgementStageError::StageNotOpen);
    }
    if (timeline_qpc_frequency != cutoff_.qpc_frequency) {
        return std::unexpected(JudgementStageError::QpcFrequencyChanged);
    }
    if (timeline_generation_ == 0) {
        timeline_generation_ = timeline_generation;
        return {};
    }
    if (timeline_generation != timeline_generation_) {
        return std::unexpected(
            JudgementStageError::TimelineGenerationChanged);
    }
    return {};
}

void JudgementStage::Activate() noexcept {
    if (open_ && bound_ && timeline_generation_ != 0) {
        active_ = true;
    }
}

void JudgementStage::Reset() noexcept {
    native_ = {};
    cutoff_ = {};
    failure_transport_status_ = {};
    generation_ = 0;
    tune_manager_ = 0;
    timeline_generation_ = 0;
    entry_game_time_offset_ms_ = 0;
    entry_hold_safe_frame_ = 0;
    entry_slide_hold_safe_frame_ = 0;
    open_ = false;
    bound_ = false;
    active_ = false;
}

bool JudgementStage::open() const noexcept {
    return open_;
}

bool JudgementStage::active() const noexcept {
    return active_;
}

bool JudgementStage::bound() const noexcept {
    return bound_;
}

std::uint64_t JudgementStage::generation() const noexcept {
    return generation_;
}

std::uintptr_t JudgementStage::tune_manager() const noexcept {
    return tune_manager_;
}

const NativeJudgementIdentity& JudgementStage::native() const noexcept {
    return native_;
}

const gc::input::GameplayTransitionCutoff& JudgementStage::cutoff()
    const noexcept {
    return cutoff_;
}

std::uint64_t JudgementStage::timeline_generation() const noexcept {
    return timeline_generation_;
}

const gc::input::GameplayTransitionStatus&
JudgementStage::failure_transport_status() const noexcept {
    return failure_transport_status_;
}

} // namespace gc::absolute_judgement

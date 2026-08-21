#include "Patches/AbsoluteJudgement/JudgementStage.h"

#include <atomic>
#include <limits>

namespace gc::absolute_judgement {
namespace {

std::atomic_uint64_t g_next_stage_generation{1};

bool SameNativeObjectIdentity(
    const NativeJudgementIdentity& left,
    const NativeJudgementIdentity& right) noexcept {
    return left.stage_generation == right.stage_generation &&
        left.tune_manager == right.tune_manager && left.tune == right.tune &&
        left.judgement_state == right.judgement_state &&
        left.score_state == right.score_state &&
        left.booster == right.booster && left.player == right.player;
}

} // namespace

std::expected<void, JudgementStageError> JudgementStage::Begin(
    const std::uintptr_t tune_manager,
    const std::int64_t stage_entry_qpc,
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
    if (tune_manager == 0 ||
        !gc::input::CaptureGameplayTransitionCutoff(
            stage_entry_qpc, &cutoff_) ||
        cutoff_.transport_epoch == 0 || cutoff_.qpc_frequency <= 0) {
        return std::unexpected(
            JudgementStageError::InputCapabilityUnavailable);
    }
    if (hold_safe_frame != 0 || slide_hold_safe_frame != 0) {
        return std::unexpected(JudgementStageError::SafeFrameChanged);
    }
    return {};
}

std::expected<void, JudgementStageError>
JudgementStage::BindOrValidateNative(
    const NativeJudgementIdentity& native) noexcept {
    if (!open_ || native.stage_generation != generation_ ||
        native.tune_manager != tune_manager_ || native.tune == 0 ||
        native.judgement_state == 0 || native.score_state == 0 ||
        native.booster == 0) {
        return std::unexpected(JudgementStageError::NativeIdentityInvalid);
    }
    if (native.hold_safe_frame != 0 ||
        native.slide_hold_safe_frame != 0) {
        return std::unexpected(JudgementStageError::SafeFrameChanged);
    }
    if (!bound_) {
        native_ = native;
        native_.game_time_offset_ms = entry_game_time_offset_ms_;
        native_.hold_safe_frame = entry_hold_safe_frame_;
        native_.slide_hold_safe_frame = entry_slide_hold_safe_frame_;
        bound_ = true;
        return {};
    }
    if (!SameNativeObjectIdentity(native_, native)) {
        return std::unexpected(JudgementStageError::NativeIdentityChanged);
    }
    if (native.hold_safe_frame != 0 ||
        native.slide_hold_safe_frame != 0) {
        return std::unexpected(JudgementStageError::SafeFrameChanged);
    }
    return {};
}

std::expected<void, JudgementStageError>
JudgementStage::BindEndpointOrValidate(
    const std::uint64_t endpoint_generation,
    const std::int64_t endpoint_qpc_frequency) noexcept {
    if (!open_ || !bound_ || endpoint_generation == 0 ||
        endpoint_qpc_frequency <= 0) {
        return std::unexpected(JudgementStageError::NativeIdentityInvalid);
    }
    if (endpoint_qpc_frequency != cutoff_.qpc_frequency) {
        return std::unexpected(JudgementStageError::QpcFrequencyChanged);
    }
    if (endpoint_generation_ == 0) {
        endpoint_generation_ = endpoint_generation;
        return {};
    }
    if (endpoint_generation != endpoint_generation_) {
        return std::unexpected(
            JudgementStageError::EndpointGenerationChanged);
    }
    return {};
}

void JudgementStage::Activate() noexcept {
    if (open_ && bound_ && endpoint_generation_ != 0) {
        active_ = true;
    }
}

void JudgementStage::Reset() noexcept {
    native_ = {};
    cutoff_ = {};
    generation_ = 0;
    tune_manager_ = 0;
    endpoint_generation_ = 0;
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

std::uint64_t JudgementStage::endpoint_generation() const noexcept {
    return endpoint_generation_;
}

} // namespace gc::absolute_judgement

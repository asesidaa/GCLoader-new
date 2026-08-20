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
    const std::uintptr_t tune_manager) noexcept {
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
    open_ = true;
    if (tune_manager == 0 ||
        !gc::input::CaptureGameplayTransitionCutoff(&cutoff_) ||
        cutoff_.transport_epoch == 0 || cutoff_.qpc_frequency <= 0) {
        return std::unexpected(
            JudgementStageError::InputCapabilityUnavailable);
    }
    return {};
}

std::expected<void, JudgementStageError> JudgementStage::BindOrValidate(
    const NativeJudgementIdentity& native,
    const std::uint64_t endpoint_generation,
    const std::int64_t endpoint_qpc_frequency) noexcept {
    if (!open_ || native.stage_generation != generation_ ||
        native.tune_manager != tune_manager_ || native.tune == 0 ||
        native.judgement_state == 0 || native.score_state == 0 ||
        native.booster == 0 || endpoint_generation == 0 ||
        endpoint_qpc_frequency <= 0) {
        return std::unexpected(JudgementStageError::NativeIdentityInvalid);
    }
    if (native.hold_safe_frame != 0 ||
        native.slide_hold_safe_frame != 0) {
        return std::unexpected(JudgementStageError::SafeFrameChanged);
    }
    if (endpoint_qpc_frequency != cutoff_.qpc_frequency) {
        return std::unexpected(JudgementStageError::QpcFrequencyChanged);
    }

    if (!bound_) {
        native_ = native;
        endpoint_generation_ = endpoint_generation;
        bound_ = true;
        return {};
    }
    if (!SameNativeObjectIdentity(native_, native)) {
        return std::unexpected(JudgementStageError::NativeIdentityChanged);
    }
    if (endpoint_generation != endpoint_generation_) {
        return std::unexpected(
            JudgementStageError::EndpointGenerationChanged);
    }
    if (native.game_time_offset_ms != native_.game_time_offset_ms) {
        return std::unexpected(JudgementStageError::GameTimeOffsetChanged);
    }
    if (native.hold_safe_frame != native_.hold_safe_frame ||
        native.slide_hold_safe_frame != native_.slide_hold_safe_frame) {
        return std::unexpected(JudgementStageError::SafeFrameChanged);
    }
    return {};
}

std::expected<void, JudgementStageError> JudgementStage::ValidateCleanup(
    const std::uintptr_t tune_manager) const noexcept {
    if (!open_) {
        return {};
    }
    if (tune_manager == 0 || tune_manager != tune_manager_) {
        return std::unexpected(JudgementStageError::CleanupIdentityChanged);
    }
    return {};
}

void JudgementStage::Activate() noexcept {
    if (open_ && bound_) {
        active_ = true;
    }
}

void JudgementStage::Reset() noexcept {
    native_ = {};
    cutoff_ = {};
    generation_ = 0;
    tune_manager_ = 0;
    endpoint_generation_ = 0;
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

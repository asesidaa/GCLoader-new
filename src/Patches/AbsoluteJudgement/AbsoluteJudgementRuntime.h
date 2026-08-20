#pragma once

#include "Patches/AbsoluteJudgement/JudgementScope.h"

#include <cstdint>
#include <optional>

#include <safetyhook.hpp>

namespace gc::absolute_judgement {

void InitializeAbsoluteJudgementRuntime(
    std::uintptr_t executable_base) noexcept;

void BeginAbsoluteJudgementNativeStage(
    std::uintptr_t tune_manager) noexcept;
void EndAbsoluteJudgementNativeStage(
    std::uintptr_t tune_manager) noexcept;
[[nodiscard]] bool AbsoluteJudgementNativeStageOpen() noexcept;
[[nodiscard]] std::uint64_t AbsoluteJudgementStageGeneration() noexcept;

[[noreturn]] void FailAbsoluteJudgementQueryInvariant(
    JudgementQueryInvariant invariant,
    std::optional<JudgementHistoryError> history_error) noexcept;
[[noreturn]] void FailAbsoluteJudgementActiveStage(
    AbsoluteJudgementFatalReason reason) noexcept;

// Deliberately not noexcept: allocation while registering a newly observed
// authoritative playback history must reach the immediate loop-hook boundary.
void DispatchAbsoluteJudgementOuterCall(safetyhook::Context& context);

} // namespace gc::absolute_judgement

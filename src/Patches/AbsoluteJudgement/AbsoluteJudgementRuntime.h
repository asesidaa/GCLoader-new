#pragma once

#include "Audio/ExactJudgementTimeline.h"
#include "Patches/AbsoluteJudgement/AbsoluteJudgementDiagnostics.h"
#include "Patches/AbsoluteJudgement/JudgementScope.h"

#include <cstdint>
#include <initializer_list>
#include <optional>

#include <safetyhook.hpp>

namespace gc::absolute_judgement
{
    void InitializeAbsoluteJudgementRuntime(
        std::uintptr_t executable_base,
        gc::audio::ExactJudgementTimelineDomain expected_domain) noexcept;

    void BeginAbsoluteJudgementSemanticStage(
        std::uintptr_t tune_manager) noexcept;
    void ObserveAbsoluteJudgementGameplayInitialization(
        std::uintptr_t tune_manager) noexcept;
    void EndAbsoluteJudgementSemanticStage(
        std::uintptr_t tune_manager) noexcept;
    void EndAbsoluteJudgementSemanticStageForTestMode() noexcept;
    [[nodiscard]] bool AbsoluteJudgementSemanticStageOpen() noexcept;
    [[nodiscard]] std::uint64_t AbsoluteJudgementStageGeneration() noexcept;

    [[noreturn]] void FailAbsoluteJudgementQueryInvariant(
        JudgementQueryInvariant invariant,
        std::optional<JudgementHistoryError> history_error,
        std::uint64_t failure_operand0 = 0,
        std::uint64_t failure_operand1 = 0,
        std::uint8_t failure_operand_count = 0) noexcept;
    [[noreturn]] void FailAbsoluteJudgementActiveStage(
        AbsoluteJudgementFatalPredicate predicate,
        AbsoluteJudgementFatalReason category,
        std::initializer_list<std::uint64_t> operands = {}) noexcept;

    // Deliberately not noexcept: allocation while registering a newly observed
    // authoritative playback history must reach the immediate loop-hook boundary.
    void DispatchAbsoluteJudgementOuterCall(safetyhook::Context & context);
} // namespace gc::absolute_judgement

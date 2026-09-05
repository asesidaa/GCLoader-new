#pragma once

#include "Patches/AbsoluteJudgement/JudgementSettings.h"
#include "Patches/AbsoluteJudgement/NativeJudgementAbi.h"
#include "Patches/GameVersion/VersionedPlan.h"

#include <cstdint>

#include <safetyhook.hpp>

namespace gc::absolute_judgement
{
    namespace detail {
        struct QueryOriginals final {
            native_abi::PressedFn pressed{};
            native_abi::HeldFn held{};
            native_abi::ReleasedFn released{};
            native_abi::DirectionFn direction{};
            native_abi::HeldAgeFn held_age{};
            native_abi::TimingGradeFn timing_grade{};
        };
        extern QueryOriginals g_originals;
    }
    [[nodiscard]] std::expected<void, game_version::PlanError>
    PrepareAbsoluteJudgementRuntime(const game_version::ApprovedVersionedPlan&,
        const runtime_image::RuntimeImage&, const JudgementSettings&) noexcept;
    void CompleteAbsoluteJudgementStartup(const JudgementSettings&) noexcept;

    void HookGameplayInitialization(safetyhook::Context& context) noexcept;
    void HookSemanticStageEntry(safetyhook::Context& context) noexcept;
    void HookSemanticStageExit(safetyhook::Context& context) noexcept;
    void HookLoopGuard(safetyhook::Context& context) noexcept;
    std::uint8_t __fastcall HookPressed(
        void* self, void*, int id, int frame) noexcept;
    std::uint8_t __fastcall HookHeld(
        void* self, void*, int id, int frame) noexcept;
    std::uint8_t __fastcall HookReleased(
        void* self, void*, int id, int frame) noexcept;
    int __fastcall HookDirection(
        void* self,
        void*,
        int booster,
        float* x,
        float* y,
        int frame) noexcept;
    int __fastcall HookHeldAge(
        void* self, void*, unsigned int id) noexcept;
    int __fastcall HookTimingGrade(
        void* self,
        void*,
        const float* note,
        int recognition_ms) noexcept;
} // namespace gc::absolute_judgement

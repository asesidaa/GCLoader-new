#pragma once

#include <cstdint>

#include <safetyhook.hpp>

namespace gc::absolute_judgement {

void InitializeAbsoluteJudgementOrFatal() noexcept;

std::uint8_t __fastcall HookStageBegin(void* self, void*) noexcept;
int __fastcall HookStageEnd(void* self, void*) noexcept;
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

} // namespace gc::absolute_judgement

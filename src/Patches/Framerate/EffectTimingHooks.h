#pragma once
#include "Patches/Framerate/FramerateNativeAbi.h"
#include "Patches/Framerate/FramerateMenuTiming.h"
#include <safetyhook.hpp>

namespace gc::framerate::detail {
struct EffectTimingOriginals final {
    NavigatorAdvanceFn navigator_advance{};
};
extern EffectTimingOriginals g_effect_originals;
void* __fastcall HookNavigatorAdvance(void* self, void*);
void HookGameplayEffectAdvance(safetyhook::Context& context);
void HookEffectCadence6(safetyhook::Context& context);
void HookEffectCadence5(safetyhook::Context& context);
void HookEffectCadence4(safetyhook::Context& context);
void HookEffectCadence16A(safetyhook::Context& context);
void HookEffectCadence16B(safetyhook::Context& context);
void HookEffectCadence8(safetyhook::Context& context);
void HookRemoteCadenceA(safetyhook::Context& context);
void HookRemoteCadenceB(safetyhook::Context& context);
void HookGameplayBlink(safetyhook::Context& context);
void HookAuthoredOperandEax(safetyhook::Context& context);
void HookAuthoredOperandEcx(safetyhook::Context& context);
void HookAuthoredOperandEdx(safetyhook::Context& context);
void HookGameplayCountdownAssetFrame(safetyhook::Context& context);
void HookPlayerPositionInitialization(safetyhook::Context& context);
void HookPlayerPositionAssetFrame(safetyhook::Context& context);
void HookPlayerPositionDenominator(safetyhook::Context& context);
void HookEffectFlowItemFrame(safetyhook::Context& context);
void HookEffectTutorialElapsed(safetyhook::Context& context);
void HookEffectChartPreRollDuration(safetyhook::Context& context);
void HookEffectPlayerModuloDividend(safetyhook::Context& context);
void HookOuterFrame(safetyhook::Context&);
}

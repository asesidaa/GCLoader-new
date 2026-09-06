#pragma once
#include "Patches/WindowedWidescreen/WidescreenRuntime.h"

namespace gc::windowed_widescreen::detail {
struct GameplayHudOriginals final {
    native::OwnerCall task_dispatch{};
};
extern GameplayHudOriginals g_gameplay_originals;

void ResetScopedRenderState(
    WindowedWidescreenRuntime& runtime) noexcept;
int __fastcall TaskDispatchDetour(
    void* const task_node,
    void*) noexcept;
void GameplayStageBackgroundMid(safetyhook::Context&) noexcept;
void GameplayTrackMid(safetyhook::Context& context) noexcept;
void GameplayEffectsMid(safetyhook::Context& context) noexcept;
void GameplayEffectsEndMid(safetyhook::Context&) noexcept;
void GameplayFeedbackDrawBeginMid(
    safetyhook::Context& context) noexcept;
void GameplayFeedbackDrawEndMid(safetyhook::Context&) noexcept;
void TestModeNativeBeginMid(safetyhook::Context&) noexcept;
void TestModeNativeEndMid(safetyhook::Context&) noexcept;
void GameplayHudProjectionMid(safetyhook::Context& context) noexcept;
void StageTitleDrawBeginMid(safetyhook::Context&) noexcept;
void StageTitleDrawEndMid(safetyhook::Context&) noexcept;
void StagePlayersDrawBeginMid(safetyhook::Context&) noexcept;
void StagePlayersDrawEndMid(safetyhook::Context&) noexcept;
void TimedTextDrawBeginMid(safetyhook::Context&) noexcept;
void TimedTextDrawEndMid(safetyhook::Context&) noexcept;
void ChainLabelBeginMid(safetyhook::Context& context) noexcept;
void ChainDigitsBeginMid(safetyhook::Context&) noexcept;
void ChainGlowBeginMid(safetyhook::Context&) noexcept;
void HundredDigitsBeginMid(safetyhook::Context&) noexcept;
void CounterDrawEndMid(safetyhook::Context&) noexcept;
void BarDrawBeginMid(safetyhook::Context&) noexcept;
void BarDrawEndMid(safetyhook::Context&) noexcept;
void EffectPacketAllocatedMid(safetyhook::Context&) noexcept;
void EffectPacketSubmitMid(safetyhook::Context&) noexcept;
void EffectPacketEndMid(safetyhook::Context&) noexcept;
void ClipGateMid(safetyhook::Context& context) noexcept;
}

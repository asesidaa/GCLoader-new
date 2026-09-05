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
void NoteTutorialGroupBeginMid(safetyhook::Context&) noexcept;
void NoteTutorialGroupEndMid(safetyhook::Context&) noexcept;
void TestModeNativeBeginMid(safetyhook::Context&) noexcept;
void TestModeNativeEndMid(safetyhook::Context&) noexcept;
void GameplayHudProjectionMid(safetyhook::Context& context) noexcept;
void ComboBeginMid(safetyhook::Context& context) noexcept;
void ComboEndMid(safetyhook::Context&) noexcept;
void ClipGateMid(safetyhook::Context& context) noexcept;
}

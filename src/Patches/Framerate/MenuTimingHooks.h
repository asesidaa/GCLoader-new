#pragma once
#include "Patches/Framerate/FramerateNativeAbi.h"
#include "Patches/Framerate/FramerateMenuTiming.h"
#include <safetyhook.hpp>

namespace gc::framerate::detail {
struct MenuTimingOriginals final {
    MovieClipPreprocessFn movieclip_preprocess_visit{};
};
extern MenuTimingOriginals g_menu_originals;
extern thread_local MovieClipPreprocessDepth g_movieclip_preprocess_depth;
enum class UnlockRewardPromptTarget : std::uint8_t
{
    Transition,
    Stable,
};

[[nodiscard]] std::optional<UnlockRewardPromptTarget>
IdentifyUnlockRewardPromptHold(
    void* self,
    MovieClipAdvanceContext context) noexcept;

void __fastcall HookMovieClipPreprocessVisit(
    void* self,
    void*,
    int traversal_arg);
void HookRankingEntryCounterStore(safetyhook::Context& context);
void HookHitChartEntryCounterStore(safetyhook::Context& context);
void HookUnlockRewardCountdownStore(safetyhook::Context& context);
void HookUnlockRewardPrimaryStateStore(
    safetyhook::Context& context);
void HookUnlockRewardSecondaryStateStore(
    safetyhook::Context& context);
}

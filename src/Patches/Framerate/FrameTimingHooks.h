#pragma once
#include "Patches/Framerate/FramerateNativeAbi.h"
#include "Patches/Framerate/FramerateMenuTiming.h"
#include <safetyhook.hpp>

namespace gc::framerate::detail {
struct FrameTimingOriginals final {
    MovieClipGotoFn movieclip_goto{};
    MovieClipAdvanceFn movieclip_advance{};
};
extern FrameTimingOriginals g_frame_originals;
char __fastcall HookMovieClipGoto(
    void* self,
    void*,
    int frame,
    int subframe);
char __fastcall HookMovieClipAdvance(
    void* self,
    void*,
    char forward,
    char loop);
void HookPaletteCompare(safetyhook::Context& context);
void HookStageClipFrame(safetyhook::Context& context);
void HookIfblWait(safetyhook::Context& context);
void HookStageBgmPreload(safetyhook::Context& context);
void HookTuneCountdownCompare(safetyhook::Context& context);
void HookAudioSkipMargin(safetyhook::Context& context);
void HookAudioSkipInterval(safetyhook::Context& context);
void HookAudioResyncPolicy(safetyhook::Context& context);
void HookGameplaySongClock(safetyhook::Context& context);
}

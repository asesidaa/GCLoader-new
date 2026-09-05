#include "Audio/DirectSound/DirectSoundHook.h"
#include "Audio/AudioRuntimeState.h"
#include "Audio/AudioDiagnostics.h"
#include "Platform/Win32/Hooking/HookRegistry.h"

namespace gc::audio {
namespace {
using DirectSoundCreate8Fn = HRESULT (WINAPI*)(LPCGUID, LPDIRECTSOUND8*, LPUNKNOWN);
DirectSoundCreate8Fn g_original{};

HRESULT WINAPI DirectSoundCreate8Detour(
    LPCGUID device_guid, LPDIRECTSOUND8* output, LPUNKNOWN outer) noexcept {
    if (!output) return DSERR_INVALIDPARAM;
    *output = nullptr;
    if (device_guid) return DSERR_NODRIVER;
    if (outer) return DSERR_NOAGGREGATION;
    auto* controller = GetOrCreatePublishedAudioController();
    if (!controller) AbortAudioControllerAllocation();
    return CreateDirectSoundDevice(*controller, output);
}
}
std::expected<void, hooking::HookError> AddDirectSoundHook(
    hooking::HookPlan& plan) noexcept {
    return plan.AddInlineExport({"DirectSound", "DirectSoundCreate8"},
        {L"dsound.dll", "DirectSoundCreate8"}, &DirectSoundCreate8Detour, &g_original);
}
bool IsAudioHookCommitted() noexcept {
    return hooking::HookRegistry::ProcessLifetime().IsInstalled({"DirectSound", "DirectSoundCreate8"});
}
}

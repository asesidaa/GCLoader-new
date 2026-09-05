#include "Audio/DirectSound/DirectSoundHook.h"
#include "Diagnostics/FatalProcess.h"
#include "Platform/Win32/Hooking/HookRegistry.h"

namespace gc::audio {
namespace {
using DirectSoundCreate8Fn = HRESULT (WINAPI*)(LPCGUID, LPDIRECTSOUND8*, LPUNKNOWN);
DirectSoundCreate8Fn g_original{};
IAudioBackendControllerFactory* g_factory{};
IAudioBackendControllerReporter* g_reporter{};

HRESULT WINAPI DirectSoundCreate8Detour(
    LPCGUID device_guid, LPDIRECTSOUND8* output, LPUNKNOWN outer) noexcept {
    if (!output) return DSERR_INVALIDPARAM;
    *output = nullptr;
    if (device_guid) return DSERR_NODRIVER;
    if (outer) return DSERR_NOAGGREGATION;
    if (!g_factory || !g_reporter) diagnostics::AbortProcess({});
    auto* controller = g_factory->GetOrCreate();
    if (!controller) {
        g_reporter->FatalControllerAllocationFailure();
        return DSERR_OUTOFMEMORY;
    }
    return CreateDirectSoundDevice(*controller, output);
}
}
std::expected<void, hooking::HookError> AddDirectSoundHook(
    hooking::HookPlan& plan, IAudioBackendControllerFactory& factory,
    IAudioBackendControllerReporter& reporter) noexcept {
    g_factory = &factory;
    g_reporter = &reporter;
    return plan.AddInlineExport({"DirectSound", "DirectSoundCreate8"},
        {L"dsound.dll", "DirectSoundCreate8"}, &DirectSoundCreate8Detour, &g_original);
}
bool IsAudioHookCommitted() noexcept {
    return hooking::HookRegistry::ProcessLifetime().IsInstalled({"DirectSound", "DirectSoundCreate8"});
}
}

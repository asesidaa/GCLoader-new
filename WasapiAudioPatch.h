#pragma once

#include <Windows.h>
#include <MinHook.h>

namespace gc::audio {

enum class AudioHookStage {
    None,
    ResolveModule,
    ResolveExport,
    InitializeMinHook,
    CreateHook,
    QueueEnable,
    ApplyQueued,
};

struct AudioHookFailure {
    AudioHookStage stage{AudioHookStage::None};
    MH_STATUS status{MH_OK};
    DWORD win32_error{ERROR_SUCCESS};
    void* target{};
};

struct AudioMinHookApi {
    decltype(&MH_Initialize) initialize;
    decltype(&MH_CreateHook) create;
    decltype(&MH_QueueEnableHook) queue_enable;
    decltype(&MH_ApplyQueued) apply;
    decltype(&MH_DisableHook) disable;
    decltype(&MH_RemoveHook) remove;
};

bool InstallWasapiAudioHook(
    bool enabled,
    AudioMinHookApi api,
    AudioHookFailure* failure) noexcept;

bool WasapiAudioPatchInit() noexcept;

} // namespace gc::audio

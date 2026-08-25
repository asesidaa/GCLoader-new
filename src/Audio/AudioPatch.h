#pragma once

#include "Audio/AudioBackendController.h"

#include <Windows.h>
#include <MinHook.h>
#include <dsound.h>

namespace gc::audio
{
    using DirectSoundCreate8Fn = HRESULT (WINAPI*)(
        LPCGUID, LPDIRECTSOUND8*, LPUNKNOWN);

    enum class AudioHookStage
    {
        None,
        ValidateApi,
        ResolveModule,
        ResolveExport,
        InitializeMinHook,
        CreateHook,
        QueueEnable,
        ApplyQueued,
    };

    struct AudioHookFailure
    {
        AudioHookStage stage{AudioHookStage::None};
        MH_STATUS status{MH_OK};
        DWORD win32_error{ERROR_SUCCESS};
        void* target{};
        bool rollback_attempted{false};
        MH_STATUS rollback_disable_status{MH_OK};
        MH_STATUS rollback_remove_status{MH_OK};
        bool rollback_complete{true};
    };

    struct AudioMinHookApi
    {
        decltype(&MH_Initialize) initialize{};
        decltype(&MH_CreateHook) create{};
        decltype(&MH_QueueEnableHook) queue_enable{};
        decltype(&MH_ApplyQueued) apply{};
        decltype(&MH_DisableHook) disable{};
        decltype(&MH_RemoveHook) remove{};
    };

    // Enabled installation requires a nonnull failure record so the caller can
    // distinguish complete cleanup from a possibly live detour. Disabled
    // installation accepts nullptr and performs no hook work.
    bool InstallAudioHook(
        bool enabled,
        const AudioMinHookApi& api,
        AudioHookFailure* failure) noexcept;

    bool AudioPatchInit(AudioSettings settings) noexcept;
    [[nodiscard]] bool IsAudioHookCommitted() noexcept;
} // namespace gc::audio

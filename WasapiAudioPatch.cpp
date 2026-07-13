#include "WasapiAudioPatch.h"

#include "WasapiAudioPatchInternal.h"

#include <dsound.h>

namespace gc::audio {
namespace {

LPVOID g_original_direct_sound_create8{};
LPVOID g_committed_target{};

HRESULT WINAPI DirectSoundCreate8Detour(
    LPCGUID,
    LPDIRECTSOUND8*,
    LPUNKNOWN) {
    return DSERR_NODRIVER;
}

void set_failure(
    AudioHookFailure* failure,
    AudioHookStage stage,
    MH_STATUS status,
    DWORD win32_error,
    LPVOID target) noexcept {
    if (failure != nullptr) {
        *failure = {stage, status, win32_error, target};
    }
}

void rollback(AudioMinHookApi api, LPVOID target) noexcept {
    api.disable(target);
    api.remove(target);
}

} // namespace

namespace detail {

bool InstallWasapiAudioHookWithResolver(
    bool enabled,
    AudioMinHookApi minhook,
    AudioResolverApi resolver,
    AudioHookFailure* failure) noexcept {
    if (failure != nullptr) {
        *failure = {};
    }
    if (!enabled) {
        return true;
    }

    const auto module = resolver.get_module_handle(L"dsound.dll");
    if (module == nullptr) {
        set_failure(
            failure,
            AudioHookStage::ResolveModule,
            MH_OK,
            ERROR_MOD_NOT_FOUND,
            nullptr);
        return false;
    }

    const auto target = reinterpret_cast<LPVOID>(
        resolver.get_proc_address(module, "DirectSoundCreate8"));
    if (target == nullptr) {
        set_failure(
            failure,
            AudioHookStage::ResolveExport,
            MH_OK,
            ERROR_PROC_NOT_FOUND,
            nullptr);
        return false;
    }

    auto status = minhook.initialize();
    if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED) {
        set_failure(
            failure,
            AudioHookStage::InitializeMinHook,
            status,
            ERROR_SUCCESS,
            target);
        return false;
    }

    status = minhook.create(
        target,
        reinterpret_cast<LPVOID>(&DirectSoundCreate8Detour),
        &g_original_direct_sound_create8);
    if (status != MH_OK) {
        set_failure(
            failure,
            AudioHookStage::CreateHook,
            status,
            ERROR_SUCCESS,
            target);
        return false;
    }

    status = minhook.queue_enable(target);
    if (status != MH_OK) {
        set_failure(
            failure,
            AudioHookStage::QueueEnable,
            status,
            ERROR_SUCCESS,
            target);
        rollback(minhook, target);
        return false;
    }

    status = minhook.apply();
    if (status != MH_OK) {
        set_failure(
            failure,
            AudioHookStage::ApplyQueued,
            status,
            ERROR_SUCCESS,
            target);
        rollback(minhook, target);
        return false;
    }

    g_committed_target = target;
    return true;
}

} // namespace detail

bool InstallWasapiAudioHook(
    bool enabled,
    AudioMinHookApi api,
    AudioHookFailure* failure) noexcept {
    return detail::InstallWasapiAudioHookWithResolver(
        enabled,
        api,
        {GetModuleHandleW, GetProcAddress},
        failure);
}

bool WasapiAudioPatchInit() noexcept {
    return true;
}

} // namespace gc::audio

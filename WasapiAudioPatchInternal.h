#pragma once

#include "ExclusiveAudioEngine.h"
#include "WasapiAudioPatch.h"

#include <condition_variable>
#include <mutex>

namespace gc::audio::detail {

struct AudioResolverApi {
    decltype(&GetModuleHandleW) get_module_handle{};
    decltype(&GetProcAddress) get_proc_address{};
};

bool InstallWasapiAudioHookWithResolver(
    bool enabled,
    AudioMinHookApi minhook,
    AudioResolverApi resolver,
    AudioHookFailure* failure) noexcept;

class IExclusiveEngineStartup {
public:
    virtual ~IExclusiveEngineStartup() = default;
    virtual IAudioEngineServices* Start(
        AudioStartupFailure*) noexcept = 0;
};

class CachedExclusiveEngineFactory final : public IExclusiveEngineFactory {
public:
    explicit CachedExclusiveEngineFactory(
        IExclusiveEngineStartup&) noexcept;

    IAudioEngineServices* GetOrCreate(
        const AudioStartupFailure**) noexcept override;

private:
    enum class State {
        Uninitialized,
        Initializing,
        Succeeded,
        Failed,
    };

    IExclusiveEngineStartup& startup_;
    std::mutex mutex_;
    std::condition_variable condition_;
    State state_{State::Uninitialized};
    IAudioEngineServices* engine_{};
    AudioStartupFailure failure_{};
};

HRESULT InvokeDirectSoundCreate8Detour(
    LPCGUID device_guid,
    LPDIRECTSOUND8* output,
    LPUNKNOWN outer,
    IExclusiveEngineFactory& factory,
    IAudioStartupFailureReporter& reporter,
    DirectSoundCreate8Fn saved_original) noexcept;

} // namespace gc::audio::detail

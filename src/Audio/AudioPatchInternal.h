#pragma once

#include "Audio/AudioPatch.h"
#include "Audio/Wasapi/ExclusiveAudioEngine.h"

#include <cstdint>

namespace gc::audio::detail
{
    struct AudioResolverApi
    {
        decltype(&GetModuleHandleW) get_module_handle{};
        decltype(&GetProcAddress) get_proc_address{};
    };

    // Long-lived observers, factories, and reporters must own a copy of this
    // callback table; their constructor arguments are often temporaries.
    struct AudioPatchPlatformActions
    {
        void (*log_info)(const char*){};
        void (*log_error)(const char*){};
        void (*show_error)(const char*){};
        void (*terminate_process)(DWORD){};
        void (*fail_fast)(){};
    };

    struct AudioPatchInitDependencies
    {
        AudioMinHookApi minhook{};
        AudioResolverApi resolver{};
        AudioPatchPlatformActions platform{};
    };

    using CreateWasapiApiFn = std::unique_ptr<IWasapiApi> (*)() noexcept;
    using StartExclusiveAudioEngineFn =
    decltype(&ExclusiveAudioEngine::StartAndWait);

    bool InstallAudioHookWithResolver(
        bool enabled,
        const AudioMinHookApi& minhook,
        AudioResolverApi resolver,
        AudioHookFailure* failure) noexcept;

    bool AudioPatchInitWithDependencies(
        AudioBackend requested_backend,
        std::uint32_t configured_buffer_ms,
        const AudioPatchInitDependencies& dependencies);

    void ReportAudioStartupSucceeded(
        const EndpointInitialization&,
        const AudioPatchPlatformActions&) noexcept;
    void ReportAudioRuntimeSummary(
        const AudioRuntimeCountersSnapshot&,
        const AudioPatchPlatformActions&) noexcept;
    void ReportAudioRuntimeFailure(
        const EndpointInitialization&,
        const AudioFailure&,
        const AudioRuntimeCountersSnapshot&,
        const AudioPatchPlatformActions&) noexcept;
    void ReportAudioStartupFailure(
        const AudioStartupFailure&,
        const AudioPatchPlatformActions&) noexcept;
    std::unique_ptr<ExclusiveAudioEngine> StartProductionExclusiveAudioEngine(
        CreateWasapiApiFn,
        StartExclusiveAudioEngineFn,
        REFERENCE_TIME configured_duration,
        bool enable_absolute_time_judgement,
        const AudioPatchPlatformActions&,
        std::shared_ptr<IAudioEngineObserver>,
        AudioStartupFailure*) noexcept;

    HRESULT InvokeDirectSoundCreate8Detour(
        LPCGUID device_guid,
        LPDIRECTSOUND8* output,
        LPUNKNOWN outer,
        IAudioBackendControllerFactory& factory,
        IAudioBackendControllerReporter& reporter,
        DirectSoundCreate8Fn saved_original) noexcept;
} // namespace gc::audio::detail

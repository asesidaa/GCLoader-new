#pragma once

#include "Audio/AudioPatch.h"
#include "Audio/Asio/AsioOutputBackend.h"
#include "Audio/Wasapi/ExclusiveAudioEngine.h"

#include <condition_variable>
#include <cstdint>
#include <mutex>

namespace gc::audio::detail {

struct AudioResolverApi {
    decltype(&GetModuleHandleW) get_module_handle{};
    decltype(&GetProcAddress) get_proc_address{};
};

struct AudioPatchPlatformActions {
    void (*log_info)(const char*){};
    void (*log_error)(const char*){};
    void (*show_error)(const char*){};
    void (*terminate_process)(DWORD){};
    void (*fail_fast)(){};
};

struct AudioPatchInitDependencies {
    AudioMinHookApi minhook{};
    AudioResolverApi resolver{};
    AudioPatchPlatformActions platform{};
};

using CreateWasapiApiFn = std::unique_ptr<IWasapiApi> (*)() noexcept;
using StartExclusiveAudioEngineFn =
    decltype(&ExclusiveAudioEngine::StartAndWait);

bool InstallAudioHookWithResolver(
    bool enabled,
    AudioMinHookApi minhook,
    AudioResolverApi resolver,
    AudioHookFailure* failure) noexcept;

bool AudioPatchInitWithDependencies(
    gc::config::AudioBackend requested_backend,
    std::uint32_t configured_buffer_ms,
    AudioPatchInitDependencies dependencies);

void ReportAudioStartupSucceeded(
    const EndpointInitialization&,
    AudioPatchPlatformActions) noexcept;
void ReportAudioRuntimeSummary(
    const AudioRuntimeCountersSnapshot&,
    AudioPatchPlatformActions) noexcept;
void ReportAudioRuntimeFailure(
    const EndpointInitialization&,
    const AudioFailure&,
    const AudioRuntimeCountersSnapshot&,
    AudioPatchPlatformActions) noexcept;
void ReportAudioStartupFailure(
    const AudioStartupFailure&,
    AudioPatchPlatformActions) noexcept;
void ReportAsioStartupSucceeded(
    const AsioCapabilityReport&,
    AudioPatchPlatformActions) noexcept;
void ReportAsioRuntimeSummary(
    const AsioRuntimeCountersSnapshot&,
    AudioPatchPlatformActions) noexcept;
void ReportAsioRuntimeFailure(
    const AsioCapabilityReport*,
    const AsioFailure&,
    const AsioRuntimeCountersSnapshot&,
    AudioPatchPlatformActions) noexcept;

std::unique_ptr<ExclusiveAudioEngine> StartProductionExclusiveAudioEngine(
    CreateWasapiApiFn,
    StartExclusiveAudioEngineFn,
    REFERENCE_TIME configured_duration,
    AudioPatchPlatformActions,
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

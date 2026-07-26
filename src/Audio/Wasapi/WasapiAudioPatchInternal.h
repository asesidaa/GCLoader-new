#pragma once

#include "Audio/Wasapi/ExclusiveAudioEngine.h"
#include "Audio/Wasapi/WasapiAudioPatch.h"

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

bool InstallWasapiAudioHookWithResolver(
    bool enabled,
    AudioMinHookApi minhook,
    AudioResolverApi resolver,
    AudioHookFailure* failure) noexcept;

bool WasapiAudioPatchInitWithDependencies(
    bool enabled,
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
void ReportAudioDiagnosticStatus(
    const diagnostics::AudioFlightRecorderStatus&,
    AudioPatchPlatformActions) noexcept;

std::unique_ptr<ExclusiveAudioEngine> StartProductionExclusiveAudioEngine(
    CreateWasapiApiFn,
    StartExclusiveAudioEngineFn,
    REFERENCE_TIME configured_duration,
    AudioPatchPlatformActions,
    std::shared_ptr<IAudioEngineObserver>,
    diagnostics::IAudioDiagnosticSink*,
    AudioStartupFailure*) noexcept;

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

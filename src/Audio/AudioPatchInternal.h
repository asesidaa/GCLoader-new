#pragma once

#include "Audio/AudioPatch.h"
#include "Audio/AudioBackendController.h"
#include "Audio/Wasapi/ExclusiveAudioEngine.h"

#include <cstdint>

namespace gc::audio::detail
{
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

    using CreateWasapiApiFn = std::unique_ptr<IWasapiApi> (*)() noexcept;
    using StartExclusiveAudioEngineFn =
    decltype(&ExclusiveAudioEngine::StartAndWait);

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

} // namespace gc::audio::detail

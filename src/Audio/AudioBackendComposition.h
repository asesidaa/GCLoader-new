#pragma once

#include "Audio/Asio/AsioDriver.h"
#include "Audio/Asio/AsioDriverCatalog.h"
#include "Audio/AudioSettings.h"
#include "Audio/DirectSound/DirectSoundFacade.h"
#include "Audio/Wasapi/WasapiEndpoint.h"

namespace gc::audio {
struct AudioBackendControllerConfig;

[[nodiscard]] AudioBackendControllerConfig BuildAudioControllerConfig(const AudioSettings&);

// Owns driver discovery/construction until the ASIO owner thread takes them.
// WASAPI observers own their diagnostic values independently of this object.
class AudioBackendComposition final {
public:
    explicit AudioBackendComposition(const AudioSettings& settings);
    [[nodiscard]] std::unique_ptr<IAudioEngineServices> StartWasapi(
        REFERENCE_TIME duration, AudioStartupFailure*) noexcept;
    [[nodiscard]] std::unique_ptr<IAudioEngineServices> StartAsio(
        HWND window, const AsioStreamRequest&) noexcept;
private:
    const AudioBackend backend_;
    const bool exact_history_;
    std::unique_ptr<IAsioRegistrySource> registry_;
    std::unique_ptr<IAsioDriverFactory> driver_factory_;
};
} // namespace gc::audio

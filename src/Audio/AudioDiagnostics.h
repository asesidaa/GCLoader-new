#pragma once

#include "Audio/AudioSettings.h"
#include "Audio/Wasapi/ExclusiveAudioEngine.h"

#include <memory>
#include <string_view>

namespace gc::audio {
struct AudioBackendStartupFailure;

void ReportAudioConfig(AudioBackend backend, std::uint32_t buffer_ms);
void ReportAudioBufferHandoff(std::string_view stage, REFERENCE_TIME duration) noexcept;
[[nodiscard]] std::shared_ptr<IAudioEngineObserver> MakeAudioObserver(AudioBackend backend);
[[noreturn]] void AbortAudioBackendStartup(const AudioBackendStartupFailure&) noexcept;
[[noreturn]] void AbortAudioControllerAllocation() noexcept;
} // namespace gc::audio

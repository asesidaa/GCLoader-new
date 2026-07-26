#pragma once

#include <cstdint>

namespace gc::framerate {

enum class FramerateHookId;

[[nodiscard]] bool FramerateHookHasRuntimeBinding(
    FramerateHookId id) noexcept;
[[nodiscard]] bool FrameratePatchInit(bool wasapi_audio_committed);

namespace detail {

void PublishAudioResyncDiagnostic(
    std::int32_t drift_ms,
    std::int32_t margin_ms,
    bool readable,
    bool suppressed) noexcept;

} // namespace detail

} // namespace gc::framerate

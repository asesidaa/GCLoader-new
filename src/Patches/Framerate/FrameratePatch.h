#pragma once

namespace gc::framerate {

enum class FramerateHookId;

[[nodiscard]] bool FramerateHookHasRuntimeBinding(
    FramerateHookId id) noexcept;
[[nodiscard]] bool FrameratePatchInit(bool wasapi_audio_committed);

} // namespace gc::framerate

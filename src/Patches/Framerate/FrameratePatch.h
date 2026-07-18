#pragma once

namespace gc::framerate {

enum class FramerateHookId;

[[nodiscard]] bool FramerateHookHasRuntimeBinding(
    FramerateHookId id) noexcept;
[[nodiscard]] bool FrameratePatchInit();

} // namespace gc::framerate

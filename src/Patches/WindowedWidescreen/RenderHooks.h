#pragma once
#include "Patches/WindowedWidescreen/WidescreenRuntime.h"

namespace gc::windowed_widescreen::detail {
struct RenderOriginals final {
    native::OwnerCall frame_begin{};
    native::OwnerCall frame_end{};
    native::ViewportReset viewport_reset{};
    native::IntDimension screen_width_int{};
    native::FloatDimension screen_width_float{};
    native::IntDimension screen_height_int{};
    native::FloatDimension screen_height_float{};
    native::IntDimension target_width_int{};
    native::FloatDimension target_width_float{};
    native::IntDimension target_height_int{};
    native::FloatDimension target_height_float{};
};
extern RenderOriginals g_render_originals;

[[nodiscard]] bool PrepareRendererParticipant(WindowedWidescreenRuntime* runtime) noexcept;
[[nodiscard]] bool ActivateRendererResources(
    void* context,
    const std::uintptr_t renderer) noexcept;
[[nodiscard]] bool BeginCompositorFrame(void* context) noexcept;
[[nodiscard]] bool EndCompositorFrame(void* context) noexcept;
[[nodiscard]] bool RequestRuntimeSpace(
    void* context,
    const RenderSpace requested) noexcept;
[[nodiscard]] bool RequestRuntimeGameplayHudPlacement(
    WindowedWidescreenRuntime& runtime,
    const GameplayHudPlacement placement) noexcept;
std::uint32_t __cdecl ScreenWidthIntDetour() noexcept;
std::uint32_t __cdecl ScreenHeightIntDetour() noexcept;
float __cdecl ScreenWidthFloatDetour() noexcept;
float __cdecl ScreenHeightFloatDetour() noexcept;
std::uint32_t __cdecl TargetWidthIntDetour() noexcept;
std::uint32_t __cdecl TargetHeightIntDetour() noexcept;
float __cdecl TargetWidthFloatDetour() noexcept;
float __cdecl TargetHeightFloatDetour() noexcept;
int __cdecl ViewportResetDetour(int* const viewport) noexcept;
int __fastcall FrameBeginDetour(
    void* const renderer,
    void*) noexcept;
int __fastcall FrameEndDetour(
    void* const renderer,
    void*) noexcept;
}

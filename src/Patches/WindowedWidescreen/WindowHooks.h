#pragma once
#include "Patches/WindowedWidescreen/WidescreenRuntime.h"

namespace gc::windowed_widescreen::detail {
struct WindowOriginals final {
    native::ConfigApply config_apply{};
    native::OwnerCall window_device_create{};
    native::ResolutionSet logical_resolution_set{};
    native::TargetDimensionSet logical_target_width_set{};
    native::TargetDimensionSet logical_target_height_set{};
    native::MousePoll mouse_debug_poll{};
};
extern WindowOriginals g_window_originals;

POINT* __fastcall MouseDebugPollDetour(
    void* const owner,
    void*,
    std::uint32_t* const output) noexcept;
int __cdecl LogicalResolutionSetDetour(
    const std::uint32_t width,
    const std::uint32_t height) noexcept;
template <RenderDimensionAxis Axis, WidescreenContractSite Site>
int __cdecl LogicalTargetDimensionSetDetour(
    const std::uint32_t value) noexcept;
int __cdecl ConfigApplyDetour(const int config) noexcept;
int __fastcall WindowDeviceDetour(
    void* const renderer,
    void*) noexcept;
}

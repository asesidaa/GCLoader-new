#include "Patches/WindowedWidescreen/WindowHooks.h"
#include "Patches/WindowedWidescreen/WidescreenRuntime.h"
#include "Patches/WindowedWidescreen/RenderHooks.h"
#include <Windows.h>
#include <atomic>
#include <cstdint>

namespace gc::windowed_widescreen::detail {
WindowOriginals g_window_originals;

[[nodiscard]] bool ConfigVtableMatches(
    void* context,
    const std::uintptr_t config) noexcept
{
    auto* runtime = static_cast<WindowedWidescreenRuntime*>(context);
    if (runtime == nullptr || config == 0)
    {
        return false;
    }
    __try
    {
        const auto vtable =
            *reinterpret_cast<const std::uintptr_t*>(config);
        return vtable ==
            runtime->abi.main_config_vtable;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

[[nodiscard]] bool SetConfigWidth(
    void* context,
    const std::uintptr_t config,
    const std::uint32_t value,
    const int trailing) noexcept
{
    __try
    {
        const auto setter = static_cast<WindowedWidescreenRuntime*>(context)->abi.config_width_setter;
        (void)setter(
            reinterpret_cast<void*>(config),
            static_cast<int>(value),
            trailing);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

[[nodiscard]] bool SetConfigHeight(
    void* context,
    const std::uintptr_t config,
    const std::uint32_t value,
    const int trailing) noexcept
{
    __try
    {
        const auto setter = static_cast<WindowedWidescreenRuntime*>(context)->abi.config_height_setter;
        (void)setter(
            reinterpret_cast<void*>(config),
            static_cast<int>(value),
            trailing);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

[[nodiscard]] bool SetConfigResize(
    void* context,
    const std::uintptr_t config,
    const bool value) noexcept
{
    __try
    {
        const auto setter = static_cast<WindowedWidescreenRuntime*>(context)->abi.config_resize_setter;
        setter(reinterpret_cast<void*>(config), value ? 1 : 0);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

[[nodiscard]] bool SetConfigMinmax(
    void* context,
    const std::uintptr_t config,
    const bool minimize,
    const bool maximize) noexcept
{
    __try
    {
        const auto setter = static_cast<WindowedWidescreenRuntime*>(context)->abi.config_minmax_setter;
        setter(
            reinterpret_cast<void*>(config),
            minimize ? 1 : 0,
            maximize ? 1 : 0);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

[[nodiscard]] bool SetConfigMode(
    void* context,
    const std::uintptr_t config,
    const int first,
    const int second,
    const int third,
    const int fourth) noexcept
{
    __try
    {
        const auto setter = static_cast<WindowedWidescreenRuntime*>(context)->abi.config_mode_setter;
        (void)setter(
            reinterpret_cast<void*>(config),
            first,
            second,
            third,
            fourth);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

[[nodiscard]] int CallConfigOriginal(
    void* context,
    const std::uintptr_t config) noexcept
{
    return context && g_window_originals.config_apply
        ? g_window_originals.config_apply(static_cast<int>(config)) : 0;

}

[[nodiscard]] int CallWindowOriginal(
    void* context,
    const std::uintptr_t renderer) noexcept
{
    return context && g_window_originals.window_device_create
        ? g_window_originals.window_device_create(reinterpret_cast<void*>(renderer)) : 0;

}

[[nodiscard]] int CallLogicalResolutionSetOriginal(
    void* context,
    const std::uint32_t width,
    const std::uint32_t height) noexcept
{
    return context && g_window_originals.logical_resolution_set
        ? g_window_originals.logical_resolution_set(static_cast<int>(width), static_cast<int>(height)) : 0;

}

template <WidescreenContractSite Site>
[[nodiscard]] int CallLogicalTargetDimensionSetOriginal(
    void* context,
    const std::uint32_t value) noexcept
{
    const auto original = Site == WidescreenContractSite::logical_target_width_set
        ? g_window_originals.logical_target_width_set : g_window_originals.logical_target_height_set;
    return context && original ? original(static_cast<int>(value)) : 0;

}

[[nodiscard]] bool ValidateAndPlaceWindow(
    void* context,
    const std::uintptr_t renderer) noexcept
{
    auto* runtime = static_cast<WindowedWidescreenRuntime*>(context);
    if (runtime == nullptr)
    {
        return false;
    }
    runtime->last_window_policy_error.reset();
    const auto placed = ValidateAndPlaceRendererWindow(
        renderer,
        runtime->placement, runtime->abi.layout);
    if (!placed)
    {
        runtime->last_window_policy_error = placed.error();
        return false;
    }
    return true;
}

[[nodiscard]] std::uintptr_t CallMousePollOriginal(
    void* context,
    const std::uintptr_t owner,
    std::uint32_t* output) noexcept
{
    return context && g_window_originals.mouse_debug_poll
        ? reinterpret_cast<std::uintptr_t>(g_window_originals.mouse_debug_poll(
            reinterpret_cast<void*>(owner), output)) : 0;

}

POINT* __fastcall MouseDebugPollDetour(
    void* const owner,
    void*,
    std::uint32_t* const output) noexcept
{
    auto* runtime =
        g_callback_runtime.load(std::memory_order_acquire);
    if (runtime == nullptr)
    {
        return nullptr;
    }
    if (!RuntimeCallbacksAreActive(*runtime))
    {
        return reinterpret_cast<POINT*>(CallMousePollOriginal(
            runtime,
            reinterpret_cast<std::uintptr_t>(owner),
            output));
    }
    if (output == nullptr)
        PublishRenderRuntimeFatal(*runtime, {
            .stage = WindowedWidescreenOperationStage::invalid_actions});
    const auto native_result = CallMousePollOriginal(
        runtime, reinterpret_cast<std::uintptr_t>(owner), output);
    const auto& layout = runtime->abi.layout;
    if (output[layout.mouse_valid_word] == 1) {
        const auto mapped = runtime->resolution.ClientToNative(
            static_cast<std::int32_t>(output[layout.mouse_x_word]),
            static_cast<std::int32_t>(output[layout.mouse_y_word]));
        if (!mapped) {
            output[layout.mouse_valid_word] = 0;
        } else {
            output[layout.mouse_x_word] = static_cast<std::uint32_t>(mapped->x);
            output[layout.mouse_y_word] = static_cast<std::uint32_t>(mapped->y);
        }
    }
    return reinterpret_cast<POINT*>(native_result);
}

int __cdecl LogicalResolutionSetDetour(
    const std::uint32_t width,
    const std::uint32_t height) noexcept
{
    auto* runtime =
        g_callback_runtime.load(std::memory_order_acquire);
    if (runtime == nullptr)
    {
        return 0;
    }
    if (!RuntimeCallbacksAreActive(*runtime))
    {
        return CallLogicalResolutionSetOriginal(
            runtime,
            width,
            height);
    }
    const auto result = CallLogicalResolutionSetOriginal(runtime, kNativeWidth, kNativeHeight);
    (void)CallLogicalTargetDimensionSetOriginal<WidescreenContractSite::logical_target_width_set>(runtime, width);
    (void)CallLogicalTargetDimensionSetOriginal<WidescreenContractSite::logical_target_height_set>(runtime, height);
    return result;
}

template <RenderDimensionAxis Axis, WidescreenContractSite Site>
int __cdecl LogicalTargetDimensionSetDetour(
    const std::uint32_t value) noexcept
{
    auto* runtime =
        g_callback_runtime.load(std::memory_order_acquire);
    if (runtime == nullptr)
    {
        return 0;
    }
    if (!RuntimeCallbacksAreActive(*runtime))
    {
        return CallLogicalTargetDimensionSetOriginal<Site>(
            runtime,
            value);
    }
    return CallLogicalTargetDimensionSetOriginal<Site>(runtime, value);
}

int __cdecl ConfigApplyDetour(const int config) noexcept
{
    auto* runtime =
        g_callback_runtime.load(std::memory_order_acquire);
    if (runtime == nullptr)
    {
        return 0;
    }

    if (!RuntimeCallbacksAreActive(*runtime))
    {
        return CallConfigOriginal(
            runtime,
            static_cast<std::uintptr_t>(config));
    }
    const auto address = static_cast<std::uintptr_t>(config);
    const auto output = runtime->resolution.output_size();
    const auto result = CallConfigOriginal(runtime, address);
    if (!ConfigVtableMatches(runtime, address) ||
        !SetConfigWidth(runtime, address, output.width, 0) ||
        !SetConfigHeight(runtime, address, output.height, 0) ||
        !SetConfigResize(runtime, address, false) ||
        !SetConfigMinmax(runtime, address, true, false) ||
        !SetConfigMode(runtime, address, 1, 1, 1, 1))
        PublishRuntimeFatal({.stage = WindowedWidescreenOperationStage::config_override});
    return result;
}

int __fastcall WindowDeviceDetour(
    void* const renderer,
    void*) noexcept
{
    auto* runtime =
        g_callback_runtime.load(std::memory_order_acquire);
    if (runtime == nullptr)
    {
        return 0;
    }
    if (!RuntimeCallbacksAreActive(*runtime))
    {
        return CallWindowOriginal(
            runtime,
            reinterpret_cast<std::uintptr_t>(renderer));
    }
    const auto address = reinterpret_cast<std::uintptr_t>(renderer);
    const auto result = CallWindowOriginal(runtime, address);
    if (result == 0) return result;
    WindowedWidescreenOperationStage failed_stage{};
    if (!ValidateAndPlaceWindow(runtime, address))
        failed_stage = WindowedWidescreenOperationStage::window_policy;
    else if (!ActivateRendererResources(runtime, address))
        failed_stage = WindowedWidescreenOperationStage::resource_attach;
    if (failed_stage != WindowedWidescreenOperationStage::none)
        PublishRuntimeFatal({.stage = failed_stage,
            .window_policy_error = runtime->last_window_policy_error,
            .resource_error = runtime->last_resource_error,
            .d3d_failure = runtime->device.last_failure()});
    return result;
}


template int __cdecl LogicalTargetDimensionSetDetour<RenderDimensionAxis::width,
    WidescreenContractSite::logical_target_width_set>(std::uint32_t) noexcept;
template int __cdecl LogicalTargetDimensionSetDetour<RenderDimensionAxis::height,
    WidescreenContractSite::logical_target_height_set>(std::uint32_t) noexcept;

}

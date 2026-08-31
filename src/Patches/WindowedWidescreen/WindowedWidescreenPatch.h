#pragma once

#include "Patches/RendererDeviceLoss/RendererDeviceLossPatch.h"
#include "Patches/WindowedWidescreen/D3D9CompositorDevice.h"
#include "Patches/WindowedWidescreen/NativeWindowPolicy.h"
#include "Patches/WindowedWidescreen/WindowedWidescreenPatchTransaction.h"
#include "Patches/WindowedWidescreen/WindowedWidescreenSettings.h"

#include <cstdint>
#include <expected>
#include <optional>

namespace gc::windowed_widescreen
{
    enum class WindowedWidescreenOperationStage : std::uint8_t
    {
        none,
        invalid_actions,
        resolution,
        window_policy,
        resource_attach,
        hook_install,
        config_override,
        window_device,
        frame_begin,
        frame_end,
        task_dispatch,
        render_transition,
        projection,
        mouse_mapping,
        reset_pre,
        reset_post,
    };

    struct WindowedWidescreenError
    {
        WindowedWidescreenOperationStage stage{
            WindowedWidescreenOperationStage::none};
        std::optional<ResolutionError> resolution_error;
        std::optional<NativeWindowPolicyError> window_policy_error;
        std::optional<renderer_device_loss::RendererResourceError>
            resource_error;
        std::optional<WidescreenInstallError> install_error;
        std::optional<CompositorError> compositor_error;
        D3D9CompositorFailure d3d_failure{};
    };

    struct ConfigApplyHookActions
    {
        void* context{};
        int (*call_original)(void*, std::uintptr_t) noexcept{};
        bool (*config_vtable_matches)(void*, std::uintptr_t) noexcept{};
        bool (*set_width)(
            void*, std::uintptr_t, std::uint32_t, int) noexcept{};
        bool (*set_height)(
            void*, std::uintptr_t, std::uint32_t, int) noexcept{};
        bool (*set_resize)(
            void*, std::uintptr_t, bool) noexcept{};
        bool (*set_minmax)(
            void*, std::uintptr_t, bool, bool) noexcept{};
        bool (*set_mode)(
            void*, std::uintptr_t, int, int, int, int) noexcept{};
    };

    [[nodiscard]] std::expected<int, WindowedWidescreenError>
    RunConfigApplyHook(
        std::uintptr_t main_config_ptr,
        OutputSize output_size,
        const ConfigApplyHookActions& actions) noexcept;

    struct WindowDeviceHookActions
    {
        void* context{};
        int (*call_original)(void*, std::uintptr_t) noexcept{};
        bool (*validate_and_place)(void*, std::uintptr_t) noexcept{};
        bool (*activate_resources)(void*, std::uintptr_t) noexcept{};
    };

    [[nodiscard]] std::expected<int, WindowedWidescreenError>
    RunWindowDeviceHook(
        std::uintptr_t renderer_owner,
        const WindowDeviceHookActions& actions) noexcept;

    struct FrameBoundaryHookActions
    {
        void* context{};
        bool (*run_compositor)(void*) noexcept{};
        int (*call_original)(void*, std::uintptr_t) noexcept{};
    };

    [[nodiscard]] std::expected<int, WindowedWidescreenError>
    RunFrameBoundaryHook(
        std::uintptr_t renderer_owner,
        const FrameBoundaryHookActions& actions,
        WindowedWidescreenOperationStage stage) noexcept;

    [[nodiscard]] std::expected<void, WindowedWidescreenError>
    WindowedWidescreenPatchInit(
        WindowedWidescreenSettings settings) noexcept;
} // namespace gc::windowed_widescreen

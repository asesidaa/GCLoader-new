#pragma once

#include "Patches/RendererDeviceLoss/RendererDeviceLossPatch.h"
#include "Patches/WindowedWidescreen/D3D9CompositorDevice.h"
#include "Patches/WindowedWidescreen/NativeWindowPolicy.h"
#include "Patches/WindowedWidescreen/PassClassifier.h"
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
        dimension_query,
        viewport,
        projection,
        clip_bypass,
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
        std::optional<renderer_device_loss::RendererResetHookPairError>
            reset_hook_error;
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

    enum class RenderDimensionAxis : std::uint8_t;

    struct LogicalResolutionSetHookActions
    {
        void* context{};
        int (*call_original)(
            void*, std::uint32_t, std::uint32_t) noexcept{};
        int (*set_target_width)(void*, std::uint32_t) noexcept{};
        int (*set_target_height)(void*, std::uint32_t) noexcept{};
    };

    [[nodiscard]] std::expected<int, WindowedWidescreenError>
    RunLogicalResolutionSetHook(
        std::uint32_t requested_width,
        std::uint32_t requested_height,
        const LogicalResolutionSetHookActions& actions) noexcept;

    struct LogicalTargetDimensionSetHookActions
    {
        void* context{};
        int (*call_original)(void*, std::uint32_t) noexcept{};
    };

    [[nodiscard]] std::expected<int, WindowedWidescreenError>
    RunLogicalTargetDimensionSetHook(
        RenderDimensionAxis axis,
        std::uint32_t requested_value,
        const LogicalTargetDimensionSetHookActions& actions) noexcept;

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

    struct TaskDispatchHookActions
    {
        void* context{};
        bool (*read_pointer)(
            void*, std::uintptr_t, std::uintptr_t&) noexcept{};
        RenderSpace (*classify_task)(void*, std::uintptr_t) noexcept{};
        bool (*request_space)(void*, RenderSpace) noexcept{};
        int (*call_original)(void*, std::uintptr_t) noexcept{};
    };

    [[nodiscard]] std::expected<int, WindowedWidescreenError>
    RunTaskDispatchHook(
        std::uintptr_t task_node,
        const TaskDispatchHookActions& actions) noexcept;

    struct RenderSpaceHookActions
    {
        void* context{};
        bool (*request_space)(void*, RenderSpace) noexcept{};
    };

    [[nodiscard]] std::expected<void, WindowedWidescreenError>
    RunGameplaySpaceHook(
        GameplayPass pass,
        const RenderSpaceHookActions& actions) noexcept;

    enum class RenderDimensionAxis : std::uint8_t
    {
        width,
        height,
    };

    enum class RenderQueryRoute : std::uint8_t
    {
        native_passthrough,
        frame_virtualized,
    };

    [[nodiscard]] RenderQueryRoute ResolveRenderQueryRoute(
        bool compositor_frame_active) noexcept;

    struct RenderDimensionHookActions
    {
        void* context{};
        bool (*current_dimensions)(void*, RenderDimensions&) noexcept{};
    };

    [[nodiscard]] std::expected<std::uint32_t, WindowedWidescreenError>
    RunRenderDimensionInt(
        RenderDimensionAxis axis,
        const RenderDimensionHookActions& actions) noexcept;

    [[nodiscard]] std::expected<float, WindowedWidescreenError>
    RunRenderDimensionFloat(
        RenderDimensionAxis axis,
        const RenderDimensionHookActions& actions) noexcept;

    struct NativeViewport
    {
        float x{};
        float y{};
        float width{};
        float height{};
    };

    struct ViewportResetHookActions
    {
        void* context{};
        bool (*current_dimensions)(void*, RenderDimensions&) noexcept{};
        int (*call_original)(void*, const NativeViewport*) noexcept{};
    };

    [[nodiscard]] std::expected<int, WindowedWidescreenError>
    RunViewportResetHook(
        const NativeViewport* viewport,
        const ViewportResetHookActions& actions) noexcept;

    struct HudOrthographicArguments
    {
        float left{};
        float right{};
        float bottom{};
        float top{};
        float near_plane{};
        float far_plane{};
    };

    void ApplyNativeHudOrthographicArguments(
        HudOrthographicArguments& arguments) noexcept;

    [[nodiscard]] std::expected<void, WindowedWidescreenError>
    ApplyClipGateHook(
        std::uintptr_t image_base,
        std::uint32_t live_continuation_rva,
        std::uint32_t& instruction_pointer) noexcept;

    struct MousePollHookActions
    {
        void* context{};
        std::uintptr_t (*call_original)(
            void*, std::uintptr_t, std::uint32_t*) noexcept{};
    };

    [[nodiscard]] std::expected<std::uintptr_t, WindowedWidescreenError>
    RunMouseDebugPollHook(
        std::uintptr_t owner,
        std::uint32_t* output,
        const ResolutionModel& resolution,
        const MousePollHookActions& actions) noexcept;

    struct WindowedWidescreenInitializationGateActions
    {
        void* context{};
        bool (*initialize_enabled)(void*) noexcept{};
    };

    [[nodiscard]] std::expected<void, WindowedWidescreenError>
    RunWindowedWidescreenInitializationGate(
        bool enabled,
        const WindowedWidescreenInitializationGateActions& actions) noexcept;

    [[nodiscard]] std::expected<void, WindowedWidescreenError>
    WindowedWidescreenPatchInit(
        WindowedWidescreenSettings settings) noexcept;
} // namespace gc::windowed_widescreen

#pragma once

#include "Patches/RendererDeviceLoss/RendererDeviceLossPatch.h"
#include "Patches/WindowedWidescreen/D3D9CompositorDevice.h"
#include "Patches/WindowedWidescreen/NativeWindowPolicy.h"
#include "Patches/WindowedWidescreen/PassClassifier.h"
#include "Patches/WindowedWidescreen/WindowedWidescreenProfile.h"
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
        gameplay_hud_placement,
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
        std::optional<game_version::PlanError> plan_error;
        std::optional<CompositorError> compositor_error;
        D3D9CompositorFailure d3d_failure{};
    };

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

    void ApplyNativeHudOrthographicArguments(
        HudOrthographicArguments& arguments) noexcept;

    [[nodiscard]] std::expected<void, WindowedWidescreenError>
    ApplyClipGateHook(
        std::uintptr_t continuation,
        std::uint32_t& instruction_pointer) noexcept;

    [[nodiscard]] std::expected<void, WindowedWidescreenError> PrepareWidescreenRuntime(
        WindowedWidescreenSettings, const game_version::ApprovedVersionedPlan&,
        const runtime_image::RuntimeImage&) noexcept;
    void CompleteWidescreenStartup() noexcept;
    [[noreturn]] void AbortWidescreenStartup(const WindowedWidescreenError&) noexcept;
} // namespace gc::windowed_widescreen

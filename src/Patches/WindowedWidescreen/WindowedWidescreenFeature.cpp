#include "Patches/WindowedWidescreen/WindowedWidescreenFeature.h"
#include "Patches/WindowedWidescreen/WidescreenRuntime.h"
#include "Patches/WindowedWidescreen/WindowHooks.h"
#include "Patches/WindowedWidescreen/RenderHooks.h"
#include "Patches/WindowedWidescreen/GameplayHudHooks.h"
#include "Patches/WindowedWidescreen/NetworkStatusHooks.h"
#include <plog/Log.h>
#include <atomic>
#include <cstdint>
#include <expected>
#include <iomanip>
#include <memory>
#include <variant>

namespace gc::windowed_widescreen {
namespace detail {
std::atomic<WindowedWidescreenRuntime*> g_callback_runtime{};
void ReportUnknownTaskIdentity(
    void*,
    const std::uintptr_t identity) noexcept
{
#if defined(_DEBUG)
    PLOG_WARNING << "WindowedWidescreen: unknown task vtable=0x"
                 << std::hex << identity;
#else
    (void)identity;
#endif
}

void ReportUnknownTaskCapacity(void*) noexcept
{
#if defined(_DEBUG)
    PLOG_WARNING
        << "WindowedWidescreen: unknown task diagnostic capacity exhausted";
#endif
}

}
using namespace detail;
namespace {
std::unique_ptr<WindowedWidescreenRuntime> g_runtime_owner;
}
    std::expected<void, WindowedWidescreenError> PrepareWidescreenRuntime(
        const WindowedWidescreenSettings settings,
        const game_version::ApprovedVersionedPlan& plan,
        const runtime_image::RuntimeImage& image) noexcept {
        if (!settings.enabled()) return {};
        if (g_runtime_owner) return std::unexpected(WindowedWidescreenError{
            .stage = WindowedWidescreenOperationStage::hook_install});
        const auto* build = std::get_if<game_version::GameBuild>(&plan.context().build);
        const auto* variant = std::get_if<game_version::GameImageVariant>(&plan.context().variant);
        const auto* profile = build && variant ? ProfileFor(*build, *variant) : nullptr;
        if (!profile) return std::unexpected(WindowedWidescreenError{
            .stage = WindowedWidescreenOperationStage::hook_install,
            .plan_error = game_version::PlanError{.stage = game_version::PlanStage::unsupported_feature,
                .context = plan.context(), .feature = game_version::FeatureId::windowed_widescreen,
                .site = "profile"}});
        const auto abi = BuildWidescreenGameAbi(image, *profile, plan);
        if (!abi) return std::unexpected(WindowedWidescreenError{
            .stage = WindowedWidescreenOperationStage::hook_install, .plan_error = abi.error()});
        const auto resolution = ResolutionModel::Create(settings.output_width(), settings.output_height());
        if (!resolution) return std::unexpected(WindowedWidescreenError{
            .stage = WindowedWidescreenOperationStage::resolution, .resolution_error = resolution.error()});
        const auto placement = PrepareFixedWindowPlacement(
            resolution->output_size(), abi->layout.fixed_decorated_window_style);
        if (!placement) return std::unexpected(WindowedWidescreenError{
            .stage = WindowedWidescreenOperationStage::window_policy, .window_policy_error = placement.error()});
        try {
            // Publish stable state before the executor enables the first detour.
            // A later failure is fatal; ownership and resource registration are not undone.
            g_runtime_owner = std::make_unique<WindowedWidescreenRuntime>(
                settings, *resolution, *placement, *abi);
            if (!PrepareRendererParticipant(g_runtime_owner.get()))
                return std::unexpected(WindowedWidescreenError{
                    .stage = WindowedWidescreenOperationStage::resource_attach,
                    .resource_error = g_runtime_owner->last_resource_error});
            return {};
        } catch (...) {
            return std::unexpected(WindowedWidescreenError{
                .stage = WindowedWidescreenOperationStage::hook_install});
        }
    }

    void CompleteWidescreenStartup() noexcept {
        if (!g_runtime_owner ||
            !g_window_originals.config_apply ||
            !g_window_originals.window_device_create ||
            !g_render_originals.frame_begin ||
            !g_render_originals.frame_end ||
            !g_gameplay_originals.task_dispatch ||
            !g_window_originals.logical_resolution_set ||
            !g_window_originals.logical_target_width_set ||
            !g_window_originals.logical_target_height_set ||
            !g_render_originals.viewport_reset ||
            !g_window_originals.mouse_debug_poll ||
            !g_network_originals.network_status_movie_clip_accept ||
            !g_network_originals.network_status_shape_draw_visit ||
            !g_render_originals.screen_width_int ||
            !g_render_originals.screen_width_float ||
            !g_render_originals.screen_height_int ||
            !g_render_originals.screen_height_float ||
            !g_render_originals.target_width_int ||
            !g_render_originals.target_width_float ||
            !g_render_originals.target_height_int ||
            !g_render_originals.target_height_float)
            PublishRuntimeFatal({.stage = WindowedWidescreenOperationStage::hook_install});
        g_runtime_owner->active.store(true, std::memory_order_release);
        const auto& runtime = *g_runtime_owner;
        const auto rect = runtime.resolution.native_rect();
        PLOG_INFO << "WindowedWidescreen: profile installed"
            << " output=" << runtime.resolution.output_size().width << 'x'
            << runtime.resolution.output_size().height
            << " native_rect=" << rect.left << ',' << rect.top << ',' << rect.right << ',' << rect.bottom
            << " hud_placement=" << GameplayHudPlacementName(runtime.settings.gameplay_hud_placement())
            << " authored_stage_clip=bypassed placement_scope=selected_draws hooks=83 global_vtable_slots=2";
    }
} // namespace gc::windowed_widescreen

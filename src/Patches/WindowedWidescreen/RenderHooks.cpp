#include "Patches/WindowedWidescreen/RenderHooks.h"
#include "Patches/WindowedWidescreen/WidescreenRuntime.h"
#include "Patches/WindowedWidescreen/GameplayHudHooks.h"
#include "Patches/WindowedWidescreen/NetworkStatusHooks.h"
#include <Windows.h>
#include <plog/Log.h>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <span>

namespace gc::windowed_widescreen::detail {
RenderOriginals g_render_originals;

[[nodiscard]] bool FlushNativeBatches(void* opaque) noexcept
{
    auto* runtime = static_cast<WindowedWidescreenRuntime*>(opaque);
    if (!runtime || !runtime->abi.batch_flush) return false;
    __try { runtime->abi.batch_flush(); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }

}

[[nodiscard]] bool ReadNativeBatchCounts(
    WindowedWidescreenRuntime& runtime,
    NativeBatchCounts& pending) noexcept
{
    std::uintptr_t queue_base{};
    if (!ProductionRead(
            nullptr,
            runtime.abi.batch_queue_pointer,
            std::as_writable_bytes(std::span{&queue_base, 1})) ||
        queue_base == 0)
    {
        return false;
    }

    for (std::size_t index = 0; index < pending.size(); ++index)
    {
        const auto count_address = queue_base +
            runtime.abi.layout.batch_pending_count_offset +
            runtime.abi.layout.batch_queue_stride * index;
        if (!ProductionRead(
            nullptr,
            count_address,
            std::as_writable_bytes(
                std::span{&pending[index], 1})))
        {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool NativeBatchesAreEmpty(void* opaque) noexcept
{
    auto* runtime = static_cast<WindowedWidescreenRuntime*>(opaque);
    NativeBatchCounts pending{};
    if (runtime == nullptr ||
        !ReadNativeBatchCounts(*runtime, pending))
    {
        return false;
    }

    const bool empty = std::ranges::all_of(
        pending,
        [](const std::uint32_t count) { return count == 0; });
#if defined(_DEBUG)
    if (!empty && !runtime->pending_batches_reported)
    {
        runtime->pending_batches_reported = true;
        PLOG_ERROR
            << "WindowedWidescreen: native batches remained after flush counts="
            << pending[0] << ',' << pending[1] << ','
            << pending[2] << ',' << pending[3];
    }
#endif
    return empty;
}

[[nodiscard]] bool PrepareRendererParticipant(WindowedWidescreenRuntime* runtime) noexcept
{
    runtime->device.SetNativeBatchActions(NativeBatchActions{
        .context = runtime,
        .flush = &FlushNativeBatches,
        .empty = &NativeBatchesAreEmpty,
    });
    const auto failure_publisher =
        renderer_device_loss::RendererDeviceLossSetResetFailureActions({
            .context = runtime,
            .failure = +[](void* opaque_runtime,
                renderer_device_loss::RendererResetLifecycleStage stage,
                renderer_device_loss::RendererResourceError error) noexcept {
                auto* owner = static_cast<WindowedWidescreenRuntime*>(opaque_runtime);
                PublishRuntimeFatal({
                    .stage = stage == renderer_device_loss::RendererResetLifecycleStage::before_reset
                        ? WindowedWidescreenOperationStage::reset_pre
                        : WindowedWidescreenOperationStage::reset_post,
                    .resource_error = error,
                    .d3d_failure = owner->device.last_failure(),
                });
            },
        });
    if (!failure_publisher) {
        runtime->last_resource_error = failure_publisher.error();
        return false;
    }
    g_callback_runtime.store(runtime, std::memory_order_release);
    const auto attached =
        renderer_device_loss::RendererDeviceLossAttachResource(
            renderer_device_loss::RendererResourceParticipant{
                .context = runtime,
                .create = +[](
                void* opaque_runtime,
                const std::uintptr_t renderer_owner) noexcept
                {
                    auto* owner = static_cast<
                        WindowedWidescreenRuntime*>(opaque_runtime);
                    return owner != nullptr &&
                        owner->device.Create(renderer_owner);
                },
                .release = +[](void* opaque_runtime) noexcept
                {
                    auto* owner = static_cast<
                        WindowedWidescreenRuntime*>(opaque_runtime);
                    if (owner != nullptr)
                    {
                        ResetScopedRenderState(*owner);
                        owner->compositor.ResetForDeviceLoss();
                        owner->device.Release();
                    }
                },
            });
    if (!attached)
    {
        runtime->last_resource_error = attached.error();
        return false;
    }
    return true;
}

[[nodiscard]] bool ActivateRendererResources(
    void* context,
    const std::uintptr_t renderer) noexcept
{
    auto* runtime = static_cast<WindowedWidescreenRuntime*>(context);
    if (runtime == nullptr)
    {
        return false;
    }
    runtime->last_resource_error.reset();
    const auto activated =
        renderer_device_loss::RendererDeviceLossOnDeviceCreated(
            renderer);
    if (!activated)
    {
        runtime->last_resource_error = activated.error();
        return false;
    }
    return runtime->device.active();
}

[[nodiscard]] int CallFrameBeginOriginal(
    void* context,
    const std::uintptr_t renderer) noexcept
{
    return context && g_render_originals.frame_begin
        ? g_render_originals.frame_begin(reinterpret_cast<void*>(renderer)) : 0;

}

[[nodiscard]] int CallFrameEndOriginal(
    void* context,
    const std::uintptr_t renderer) noexcept
{
    return context && g_render_originals.frame_end
        ? g_render_originals.frame_end(reinterpret_cast<void*>(renderer)) : 0;

}

[[nodiscard]] bool BeginCompositorFrame(void* context) noexcept
{
    auto* runtime = static_cast<WindowedWidescreenRuntime*>(context);
    if (runtime == nullptr || !runtime->device.active())
    {
        return false;
    }
    const auto begun = runtime->compositor.BeginFrame();
    if (!begun)
    {
        runtime->last_compositor_error = begun.error();
        return false;
    }
    return true;
}

[[nodiscard]] bool EndCompositorFrame(void* context) noexcept
{
    auto* runtime = static_cast<WindowedWidescreenRuntime*>(context);
    if (runtime == nullptr || !runtime->device.active())
    {
        return false;
    }
    const auto ended = runtime->compositor.EndFrame();
    if (!ended)
    {
        runtime->last_compositor_error = ended.error();
        return false;
    }
    return true;
}

[[nodiscard]] bool RequestRuntimeSpace(
    void* context,
    const RenderSpace requested) noexcept
{
    auto* runtime = static_cast<WindowedWidescreenRuntime*>(context);
    if (runtime == nullptr || !runtime->device.active())
    {
        return false;
    }
    const auto changed = runtime->compositor.RequestSpace(requested);
    if (!changed)
    {
        runtime->last_compositor_error = changed.error();
        return false;
    }
    return true;
}

[[nodiscard]] bool RequestRuntimeGameplayHudPlacement(
    WindowedWidescreenRuntime& runtime,
    const GameplayHudPlacement placement) noexcept
{
    if (!runtime.device.active())
    {
        return false;
    }
    const auto changed =
        runtime.compositor.SetGameplayHudPlacement(placement);
    if (!changed)
    {
        runtime.last_compositor_error = changed.error();
        return false;
    }
    return true;
}

[[nodiscard]] bool ReadCurrentDimensions(
    void* context,
    RenderDimensions& dimensions) noexcept
{
    if (g_network_status_native_scope_depth != 0)
    {
        dimensions = RenderDimensions{
            .width = kNativeWidth,
            .height = kNativeHeight,
            .width_float = static_cast<float>(kNativeWidth),
            .height_float = static_cast<float>(kNativeHeight),
        };
        return true;
    }
    auto* runtime = static_cast<WindowedWidescreenRuntime*>(context);
    if (runtime == nullptr)
    {
        return false;
    }
    const auto current = runtime->compositor.CurrentDimensions();
    if (!current)
    {
        return false;
    }
    dimensions = *current;
    return true;
}

[[nodiscard]] bool ReadCurrentViewport(
    void* context,
    NativeViewport& viewport) noexcept
{
    auto* runtime = static_cast<WindowedWidescreenRuntime*>(context);
    if (runtime == nullptr)
    {
        return false;
    }
    if (g_network_status_native_scope_depth != 0)
    {
        const auto base_hud = ResolveGameplayHudViewport(
            runtime->resolution.output_size(),
            runtime->settings.gameplay_hud_placement());
        if (!base_hud)
        {
            return false;
        }
        viewport = NativeViewport{
            .x = static_cast<float>(base_hud->x),
            .y = static_cast<float>(base_hud->y),
            .width = static_cast<float>(base_hud->width),
            .height = static_cast<float>(base_hud->height),
        };
        return true;
    }
    const auto current_space = runtime->compositor.CurrentSpace();
    const auto dimensions = runtime->compositor.CurrentDimensions();
    if (!current_space || !dimensions)
    {
        return false;
    }

    viewport = NativeViewport{
        .x = 0.0F,
        .y = 0.0F,
        .width = dimensions->width_float,
        .height = dimensions->height_float,
    };
    if (*current_space != RenderSpace::gameplay_hud)
    {
        return true;
    }

    const auto gameplay_viewport = ResolveGameplayHudViewport(
        runtime->resolution.output_size(),
        runtime->compositor.gameplay_hud_placement());
    if (!gameplay_viewport)
    {
        return false;
    }
    viewport.x = static_cast<float>(gameplay_viewport->x);
    viewport.y = static_cast<float>(gameplay_viewport->y);
    return true;
}

[[nodiscard]] int CallViewportOriginal(
    void* context,
    const NativeViewport* viewport) noexcept
{
    return context && g_render_originals.viewport_reset
        ? g_render_originals.viewport_reset(viewport) : 0;

}

template <typename Value>
[[nodiscard]] Value CallDimensionOriginal(
    WindowedWidescreenRuntime& runtime,
    const WidescreenContractSite site) noexcept
{
    (void)runtime;
    switch (site) {
    case WidescreenContractSite::screen_width_int:
        return g_render_originals.screen_width_int ? static_cast<Value>(g_render_originals.screen_width_int()) : Value{};
    case WidescreenContractSite::screen_width_float:
        return g_render_originals.screen_width_float ? static_cast<Value>(g_render_originals.screen_width_float()) : Value{};
    case WidescreenContractSite::screen_height_int:
        return g_render_originals.screen_height_int ? static_cast<Value>(g_render_originals.screen_height_int()) : Value{};
    case WidescreenContractSite::screen_height_float:
        return g_render_originals.screen_height_float ? static_cast<Value>(g_render_originals.screen_height_float()) : Value{};
    case WidescreenContractSite::target_width_int:
        return g_render_originals.target_width_int ? static_cast<Value>(g_render_originals.target_width_int()) : Value{};
    case WidescreenContractSite::target_width_float:
        return g_render_originals.target_width_float ? static_cast<Value>(g_render_originals.target_width_float()) : Value{};
    case WidescreenContractSite::target_height_int:
        return g_render_originals.target_height_int ? static_cast<Value>(g_render_originals.target_height_int()) : Value{};
    case WidescreenContractSite::target_height_float:
        return g_render_originals.target_height_float ? static_cast<Value>(g_render_originals.target_height_float()) : Value{};
    default: return Value{};
    }

}

[[nodiscard]] std::uint32_t ReadDimensionInt(
    const RenderDimensionAxis axis,
    const WidescreenContractSite site) noexcept
{
    auto* runtime =
        g_callback_runtime.load(std::memory_order_acquire);
    if (runtime == nullptr)
    {
        return axis == RenderDimensionAxis::width
                   ? kNativeWidth
                   : kNativeHeight;
    }
    if (!RuntimeCallbacksAreActive(*runtime))
    {
        return CallDimensionOriginal<std::uint32_t>(*runtime, site);
    }
    if (ResolveRenderQueryRoute(runtime->compositor.frame_active()) ==
        RenderQueryRoute::native_passthrough)
    {
        return CallDimensionOriginal<std::uint32_t>(*runtime, site);
    }
    RenderDimensions dimensions{};
    if (!ReadCurrentDimensions(runtime, dimensions))
        PublishRenderRuntimeFatal(*runtime, {
            .stage = WindowedWidescreenOperationStage::dimension_query});
    return axis == RenderDimensionAxis::width ? dimensions.width : dimensions.height;
}

[[nodiscard]] float ReadDimensionFloat(
    const RenderDimensionAxis axis,
    const WidescreenContractSite site) noexcept
{
    auto* runtime =
        g_callback_runtime.load(std::memory_order_acquire);
    if (runtime == nullptr)
    {
        return axis == RenderDimensionAxis::width
                   ? static_cast<float>(kNativeWidth)
                   : static_cast<float>(kNativeHeight);
    }
    if (!RuntimeCallbacksAreActive(*runtime))
    {
        return CallDimensionOriginal<float>(*runtime, site);
    }
    if (ResolveRenderQueryRoute(runtime->compositor.frame_active()) ==
        RenderQueryRoute::native_passthrough)
    {
        return CallDimensionOriginal<float>(*runtime, site);
    }
    RenderDimensions dimensions{};
    if (!ReadCurrentDimensions(runtime, dimensions))
        PublishRenderRuntimeFatal(*runtime, {
            .stage = WindowedWidescreenOperationStage::dimension_query});
    return axis == RenderDimensionAxis::width ? dimensions.width_float : dimensions.height_float;
}

std::uint32_t __cdecl ScreenWidthIntDetour() noexcept
{
    return ReadDimensionInt(
        RenderDimensionAxis::width,
        WidescreenContractSite::screen_width_int);
}

std::uint32_t __cdecl ScreenHeightIntDetour() noexcept
{
    return ReadDimensionInt(
        RenderDimensionAxis::height,
        WidescreenContractSite::screen_height_int);
}

float __cdecl ScreenWidthFloatDetour() noexcept
{
    return ReadDimensionFloat(
        RenderDimensionAxis::width,
        WidescreenContractSite::screen_width_float);
}

float __cdecl ScreenHeightFloatDetour() noexcept
{
    return ReadDimensionFloat(
        RenderDimensionAxis::height,
        WidescreenContractSite::screen_height_float);
}

std::uint32_t __cdecl TargetWidthIntDetour() noexcept
{
    return ReadDimensionInt(
        RenderDimensionAxis::width,
        WidescreenContractSite::target_width_int);
}

std::uint32_t __cdecl TargetHeightIntDetour() noexcept
{
    return ReadDimensionInt(
        RenderDimensionAxis::height,
        WidescreenContractSite::target_height_int);
}

float __cdecl TargetWidthFloatDetour() noexcept
{
    return ReadDimensionFloat(
        RenderDimensionAxis::width,
        WidescreenContractSite::target_width_float);
}

float __cdecl TargetHeightFloatDetour() noexcept
{
    return ReadDimensionFloat(
        RenderDimensionAxis::height,
        WidescreenContractSite::target_height_float);
}

int __cdecl ViewportResetDetour(int* const viewport) noexcept
{
    auto* runtime =
        g_callback_runtime.load(std::memory_order_acquire);
    if (runtime == nullptr)
    {
        return 0;
    }
    const auto* typed =
        reinterpret_cast<const NativeViewport*>(viewport);
    if (!RuntimeCallbacksAreActive(*runtime))
    {
        return CallViewportOriginal(runtime, typed);
    }
    if (ResolveRenderQueryRoute(runtime->compositor.frame_active()) ==
        RenderQueryRoute::native_passthrough)
    {
        return CallViewportOriginal(runtime, typed);
    }
    NativeViewport current{};
    if (!ReadCurrentViewport(runtime, current))
        PublishRenderRuntimeFatal(*runtime, {
            .stage = WindowedWidescreenOperationStage::viewport});
    if (typed == nullptr) return CallViewportOriginal(runtime, &current);
    const NativeViewport translated{
        .x = current.x + typed->x, .y = current.y + typed->y,
        .width = typed->width, .height = typed->height};
    return CallViewportOriginal(runtime, &translated);
}

int __fastcall FrameBeginDetour(
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
        return CallFrameBeginOriginal(
            runtime,
            reinterpret_cast<std::uintptr_t>(renderer));
    }
    runtime->frame_sequence.fetch_add(1, std::memory_order_relaxed);
    runtime->gameplay_frame_active.store(
        false,
        std::memory_order_release);
    if (!BeginCompositorFrame(runtime))
        PublishRenderRuntimeFatal(*runtime, {
            .stage = WindowedWidescreenOperationStage::frame_begin});
    const auto result = CallFrameBeginOriginal(runtime, reinterpret_cast<std::uintptr_t>(renderer));
    if (FAILED(static_cast<HRESULT>(result))) {
        // Native BeginScene failure can skip frame end and enter device reset.
        ResetScopedRenderState(*runtime);
        runtime->compositor.ResetForDeviceLoss();
    }
    return result;
}

int __fastcall FrameEndDetour(
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
        return CallFrameEndOriginal(
            runtime,
            reinterpret_cast<std::uintptr_t>(renderer));
    }
    if (!EndCompositorFrame(runtime))
        PublishRenderRuntimeFatal(*runtime, {
            .stage = WindowedWidescreenOperationStage::frame_end});
    return CallFrameEndOriginal(runtime, reinterpret_cast<std::uintptr_t>(renderer));
}


}

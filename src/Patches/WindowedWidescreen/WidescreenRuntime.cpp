#include "Patches/WindowedWidescreen/WidescreenRuntime.h"
#include "Patches/WindowedWidescreen/GameplayFeedbackPlacement.h"
#include "Diagnostics/FatalProcess.h"
#include <Windows.h>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <format>
#include <limits>
#include <span>
#include <string>
#include <type_traits>
#include <utility>

namespace gc::windowed_widescreen::detail {
[[nodiscard]] bool RuntimeCallbacksAreActive(const WindowedWidescreenRuntime& runtime) noexcept {
    return runtime.active.load(std::memory_order_acquire);
}

[[noreturn]] void PublishRuntimeFatal(
    const WindowedWidescreenError& error) noexcept
{
    std::string log;
    try
    {
        log = std::format(
            "WindowedWidescreen: fatal stage={} window_policy_error={} resource_error={} d3d_stage={} d3d_hresult=0x{:08X} compositor_stage={} compositor_policy_error={} compositor_stable_space={} compositor_requested_space={} compositor_restore_attempted={} compositor_restore_succeeded={}",
            static_cast<unsigned>(error.stage),
            error.window_policy_error.has_value()
                ? static_cast<int>(*error.window_policy_error)
                : -1,
            error.resource_error.has_value()
                ? static_cast<int>(*error.resource_error)
                : -1,
            static_cast<unsigned>(error.d3d_failure.stage),
            static_cast<std::uint32_t>(
                error.d3d_failure.result),
            error.compositor_error.has_value()
                ? static_cast<int>(error.compositor_error->stage)
                : -1,
            error.compositor_error.has_value() &&
            error.compositor_error->policy_error.has_value()
                ? static_cast<int>(
                    *error.compositor_error->policy_error)
                : -1,
            error.compositor_error.has_value()
                ? static_cast<int>(error.compositor_error->stable_space)
                : -1,
            error.compositor_error.has_value()
                ? static_cast<int>(
                    error.compositor_error->requested_space)
                : -1,
            error.compositor_error.has_value() &&
            error.compositor_error->restoration_attempted
                ? 1
                : 0,
            error.compositor_error.has_value() &&
            error.compositor_error->restoration_succeeded
                ? 1
                : 0);
    }
    catch (...)
    {
        log = "WindowedWidescreen: fatal rendering invariant";
    }
    gc::diagnostics::AbortProcess({
        std::move(log),
        L"The windowed widescreen renderer could not continue safely. "
        L"Check the GCLoader log for the failing stage.",
        L"GCLoader windowed widescreen error"});
}

[[nodiscard]] bool ProductionRead(
    void*,
    const std::uintptr_t address,
    const std::span<std::byte> output) noexcept
{
    if (address == 0 || output.empty())
    {
        return false;
    }
    __try
    {
        std::memcpy(
            output.data(),
            reinterpret_cast<const void*>(address),
            output.size());
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

[[nodiscard]] bool ReadRuntimePointer(
    void*,
    const std::uintptr_t address,
    std::uintptr_t& value) noexcept
{
    return ProductionRead(
        nullptr,
        address,
        std::as_writable_bytes(std::span{&value, 1}));
}

[[noreturn]] void PublishRenderRuntimeFatal(
    WindowedWidescreenRuntime& runtime,
    WindowedWidescreenError error) noexcept
{
    error.compositor_error = runtime.last_compositor_error;
    error.d3d_failure = runtime.device.last_failure();
    PublishRuntimeFatal(error);
}

    RenderQueryRoute ResolveRenderQueryRoute(
        const bool compositor_frame_active) noexcept
    {
        return compositor_frame_active
                   ? RenderQueryRoute::frame_virtualized
                   : RenderQueryRoute::native_passthrough;
    }

    void ApplyNativeHudOrthographicArguments(
        HudOrthographicArguments& arguments) noexcept
    {
        static_assert(
            std::is_standard_layout_v<HudOrthographicArguments> &&
            sizeof(HudOrthographicArguments) == sizeof(float) * 6);
        arguments.right = static_cast<float>(kNativeWidth);
        arguments.bottom = static_cast<float>(kNativeHeight);
    }

    std::expected<void, WindowedWidescreenError> ApplyClipGateHook(
        const std::uintptr_t continuation,
        std::uint32_t& instruction_pointer) noexcept
    {
        if (continuation == 0 || continuation > std::numeric_limits<std::uint32_t>::max())
            return std::unexpected(WindowedWidescreenError{
                .stage = WindowedWidescreenOperationStage::clip_bypass});
        instruction_pointer = static_cast<std::uint32_t>(continuation);
        return {};

    }


}

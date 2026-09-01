#include "Patches/WindowedWidescreen/NativeWindowPolicy.h"

#include "Patches/WindowedWidescreen/WindowedWidescreenAbi.h"

#include <Windows.h>

#include <limits>
#include <optional>
#include <vector>

namespace gc::windowed_widescreen
{
    namespace
    {
        struct MonitorEnumerationContext
        {
            std::vector<MonitorWorkArea>* work_areas{};
            bool failed{};
        };

        BOOL CALLBACK CollectMonitorWorkArea(
            const HMONITOR monitor,
            HDC,
            LPRECT,
            const LPARAM opaque) noexcept
        {
            auto* context = reinterpret_cast<MonitorEnumerationContext*>(
                opaque);
            MONITORINFO info{.cbSize = sizeof(info)};
            if (context == nullptr || context->work_areas == nullptr ||
                !GetMonitorInfoW(monitor, &info))
            {
                if (context != nullptr)
                {
                    context->failed = true;
                }
                return FALSE;
            }

            try
            {
                context->work_areas->push_back(MonitorWorkArea{
                    .left = info.rcWork.left,
                    .top = info.rcWork.top,
                    .right = info.rcWork.right,
                    .bottom = info.rcWork.bottom,
                    .primary = (info.dwFlags & MONITORINFOF_PRIMARY) != 0,
                });
                return TRUE;
            }
            catch (...)
            {
                context->failed = true;
                return FALSE;
            }
        }

        [[nodiscard]] bool ReadRendererWindowContract(
            const std::uintptr_t renderer_owner,
            HWND& window,
            std::uint32_t& stored_style) noexcept
        {
            window = nullptr;
            stored_style = 0;
            if (renderer_owner == 0 ||
                renderer_owner >
                    std::numeric_limits<std::uintptr_t>::max() -
                        kRendererOwnerStyleOffset)
            {
                return false;
            }
            __try
            {
                window = *reinterpret_cast<HWND const*>(
                    renderer_owner + kRendererOwnerWindowOffset);
                stored_style = *reinterpret_cast<const std::uint32_t*>(
                    renderer_owner + kRendererOwnerStyleOffset);
                return window != nullptr;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                window = nullptr;
                stored_style = 0;
                return false;
            }
        }

        [[nodiscard]] bool ClientSizeMatches(
            const HWND window,
            const OutputSize expected) noexcept
        {
            RECT client{};
            if (!GetClientRect(window, &client))
            {
                return false;
            }
            return static_cast<std::int64_t>(client.right) - client.left ==
                    expected.width &&
                static_cast<std::int64_t>(client.bottom) - client.top ==
                    expected.height;
        }

        [[nodiscard]] std::optional<WindowPlacement> TryCenterPlacement(
            const MonitorWorkArea& monitor,
            const WindowOuterSize outer_size,
            const std::size_t monitor_index) noexcept
        {
            const auto work_width =
                static_cast<std::int64_t>(monitor.right) - monitor.left;
            const auto work_height =
                static_cast<std::int64_t>(monitor.bottom) - monitor.top;
            if (work_width <= 0 || work_height <= 0 ||
                static_cast<std::uint64_t>(work_width) < outer_size.width ||
                static_cast<std::uint64_t>(work_height) < outer_size.height)
            {
                return std::nullopt;
            }

            const auto x = static_cast<std::int64_t>(monitor.left) +
                (work_width - outer_size.width) / 2;
            const auto y = static_cast<std::int64_t>(monitor.top) +
                (work_height - outer_size.height) / 2;
            if (x < std::numeric_limits<std::int32_t>::min() ||
                x > std::numeric_limits<std::int32_t>::max() ||
                y < std::numeric_limits<std::int32_t>::min() ||
                y > std::numeric_limits<std::int32_t>::max())
            {
                return std::nullopt;
            }

            return WindowPlacement{
                .x = static_cast<std::int32_t>(x),
                .y = static_cast<std::int32_t>(y),
                .size = outer_size,
                .monitor_index = monitor_index,
            };
        }
    } // namespace

    std::expected<WindowPlacement, NativeWindowPolicyError>
    SelectWindowPlacement(
        const std::span<const MonitorWorkArea> monitors,
        const WindowOuterSize outer_size) noexcept
    {
        constexpr auto maximum_signed =
            static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max());
        if (outer_size.width == 0 || outer_size.height == 0 ||
            outer_size.width > maximum_signed ||
            outer_size.height > maximum_signed)
        {
            return std::unexpected(
                NativeWindowPolicyError::invalid_outer_size);
        }

        for (std::size_t index = 0; index < monitors.size(); ++index)
        {
            if (!monitors[index].primary)
            {
                continue;
            }
            if (auto placement =
                    TryCenterPlacement(monitors[index], outer_size, index))
            {
                return *placement;
            }
        }

        for (std::size_t index = 0; index < monitors.size(); ++index)
        {
            if (auto placement =
                    TryCenterPlacement(monitors[index], outer_size, index))
            {
                return *placement;
            }
        }

        return std::unexpected(
            NativeWindowPolicyError::no_fitting_work_area);
    }

    std::expected<PreparedWindowPlacement, NativeWindowPolicyError>
    PrepareFixedWindowPlacement(const OutputSize client_size) noexcept
    {
        if (client_size.width == 0 || client_size.height == 0 ||
            client_size.width >
                static_cast<std::uint32_t>(
                    std::numeric_limits<LONG>::max()) ||
            client_size.height >
                static_cast<std::uint32_t>(
                    std::numeric_limits<LONG>::max()))
        {
            return std::unexpected(
                NativeWindowPolicyError::invalid_outer_size);
        }

        RECT outer{
            .left = 0,
            .top = 0,
            .right = static_cast<LONG>(client_size.width),
            .bottom = static_cast<LONG>(client_size.height),
        };
        if (!AdjustWindowRectEx(
                &outer,
                kFixedDecoratedWindowStyle,
                FALSE,
                0))
        {
            return std::unexpected(
                NativeWindowPolicyError::frame_adjustment_failed);
        }
        const auto outer_width =
            static_cast<std::int64_t>(outer.right) - outer.left;
        const auto outer_height =
            static_cast<std::int64_t>(outer.bottom) - outer.top;
        if (outer_width <= 0 || outer_height <= 0 ||
            outer_width > std::numeric_limits<std::uint32_t>::max() ||
            outer_height > std::numeric_limits<std::uint32_t>::max())
        {
            return std::unexpected(
                NativeWindowPolicyError::arithmetic_overflow);
        }

        std::vector<MonitorWorkArea> work_areas;
        MonitorEnumerationContext context{.work_areas = &work_areas};
        if (!EnumDisplayMonitors(
                nullptr,
                nullptr,
                CollectMonitorWorkArea,
                reinterpret_cast<LPARAM>(&context)) ||
            context.failed || work_areas.empty())
        {
            return std::unexpected(
                NativeWindowPolicyError::monitor_enumeration_failed);
        }

        const auto selected = SelectWindowPlacement(
            work_areas,
            WindowOuterSize{
                .width = static_cast<std::uint32_t>(outer_width),
                .height = static_cast<std::uint32_t>(outer_height),
            });
        if (!selected)
        {
            return std::unexpected(selected.error());
        }
        return PreparedWindowPlacement{
            .client_size = client_size,
            .outer = *selected,
            .style = kFixedDecoratedWindowStyle,
        };
    }

    std::expected<void, NativeWindowPolicyError>
    ValidateAndPlaceRendererWindow(
        const std::uintptr_t renderer_owner,
        const PreparedWindowPlacement& placement) noexcept
    {
        HWND window = nullptr;
        std::uint32_t stored_style{};
        if (!ReadRendererWindowContract(
                renderer_owner,
                window,
                stored_style))
        {
            return std::unexpected(
                NativeWindowPolicyError::renderer_contract_failed);
        }
        if (!IsWindow(window))
        {
            return std::unexpected(
                NativeWindowPolicyError::renderer_window_invalid);
        }
        if (stored_style != placement.style)
        {
            return std::unexpected(
                NativeWindowPolicyError::stored_style_mismatch);
        }

        constexpr auto structural_style_mask =
            WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX |
            WS_MAXIMIZEBOX | WS_THICKFRAME;
        SetLastError(ERROR_SUCCESS);
        const auto actual_style = static_cast<std::uint32_t>(
            GetWindowLongPtrW(window, GWL_STYLE));
        if ((actual_style & structural_style_mask) != placement.style)
        {
            return std::unexpected(
                NativeWindowPolicyError::actual_style_mismatch);
        }
        if (!ClientSizeMatches(window, placement.client_size))
        {
            return std::unexpected(
                NativeWindowPolicyError::client_size_mismatch);
        }

        if (!SetWindowPos(
                window,
                nullptr,
                placement.outer.x,
                placement.outer.y,
                static_cast<int>(placement.outer.size.width),
                static_cast<int>(placement.outer.size.height),
                SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOZORDER))
        {
            return std::unexpected(
                NativeWindowPolicyError::window_move_failed);
        }
        if (!ClientSizeMatches(window, placement.client_size))
        {
            return std::unexpected(
                NativeWindowPolicyError::moved_client_size_mismatch);
        }
        return {};
    }
} // namespace gc::windowed_widescreen

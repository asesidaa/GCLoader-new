#include "Patches/WindowedWidescreen/NativeWindowPolicy.h"

#include <limits>
#include <optional>

namespace gc::windowed_widescreen
{
    namespace
    {
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
} // namespace gc::windowed_widescreen

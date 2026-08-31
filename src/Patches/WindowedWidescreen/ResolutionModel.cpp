#include "Patches/WindowedWidescreen/ResolutionModel.h"

#include <limits>

namespace gc::windowed_widescreen
{
    ResolutionModel::ResolutionModel(
        const OutputSize output_size,
        const NativeRect native_rect) noexcept
        : output_size_{output_size}, native_rect_{native_rect}
    {
    }

    std::expected<ResolutionModel, ResolutionError> ResolutionModel::Create(
        const std::uint32_t width,
        const std::uint32_t height) noexcept
    {
        if (width < kNativeWidth)
        {
            return std::unexpected(ResolutionError::width_below_native);
        }
        if (height < kNativeHeight)
        {
            return std::unexpected(ResolutionError::height_below_native);
        }

        constexpr auto maximum_signed =
            static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max());
        if (width > maximum_signed || height > maximum_signed)
        {
            return std::unexpected(ResolutionError::signed_range);
        }

        const auto pixel_area =
            static_cast<std::uint64_t>(width) * height;
        if (pixel_area > std::numeric_limits<std::uint32_t>::max())
        {
            return std::unexpected(ResolutionError::arithmetic_overflow);
        }

        const auto left = static_cast<std::int32_t>(
            (width - kNativeWidth) / 2);
        const auto top = static_cast<std::int32_t>(
            (height - kNativeHeight) / 2);
        return ResolutionModel{
            OutputSize{.width = width, .height = height},
            NativeRect{
                .left = left,
                .top = top,
                .right = left + static_cast<std::int32_t>(kNativeWidth),
                .bottom = top + static_cast<std::int32_t>(kNativeHeight),
            },
        };
    }

    std::optional<NativePoint> ResolutionModel::ClientToNative(
        const std::int32_t client_x,
        const std::int32_t client_y) const noexcept
    {
        if (client_x < native_rect_.left || client_x >= native_rect_.right ||
            client_y < native_rect_.top || client_y >= native_rect_.bottom)
        {
            return std::nullopt;
        }

        return NativePoint{
            .x = client_x - native_rect_.left,
            .y = client_y - native_rect_.top,
        };
    }
} // namespace gc::windowed_widescreen

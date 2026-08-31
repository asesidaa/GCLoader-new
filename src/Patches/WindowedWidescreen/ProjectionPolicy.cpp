#include "Patches/WindowedWidescreen/ProjectionPolicy.h"

#include "Patches/WindowedWidescreen/ResolutionModel.h"

#include <cmath>
#include <numbers>

namespace gc::windowed_widescreen
{
    namespace
    {
        constexpr double kNativeFieldOfViewDegrees = 75.0;
        constexpr double kMaximumFieldOfViewDegrees = 170.0;

        [[nodiscard]] constexpr double ToRadians(const double degrees) noexcept
        {
            return degrees * std::numbers::pi / 180.0;
        }

        [[nodiscard]] constexpr double ToDegrees(const double radians) noexcept
        {
            return radians * 180.0 / std::numbers::pi;
        }
    } // namespace

    std::expected<float, ProjectionError> TransformProjectionScale(
        const float native_scale,
        const std::uint32_t output_height) noexcept
    {
        if (!std::isfinite(native_scale) || native_scale <= 0.0F ||
            output_height == 0)
        {
            return std::unexpected(ProjectionError::invalid_scale);
        }

        const auto native_degrees =
            kNativeFieldOfViewDegrees * static_cast<double>(native_scale);
        if (!std::isfinite(native_degrees) ||
            native_degrees >= kMaximumFieldOfViewDegrees)
        {
            return std::unexpected(ProjectionError::fov_limit);
        }

        if (output_height == kNativeHeight)
        {
            return native_scale;
        }

        const auto native_radians = ToRadians(native_degrees);
        const auto height_ratio =
            static_cast<double>(output_height) / kNativeHeight;
        const auto expanded_radians = 2.0 * std::atan(
            std::tan(native_radians / 2.0) * height_ratio);
        const auto expanded_degrees = ToDegrees(expanded_radians);
        if (!std::isfinite(expanded_degrees) || expanded_degrees <= 0.0 ||
            expanded_degrees >= kMaximumFieldOfViewDegrees)
        {
            return std::unexpected(ProjectionError::fov_limit);
        }

        const auto transformed = static_cast<float>(
            expanded_degrees / kNativeFieldOfViewDegrees);
        if (!std::isfinite(transformed) || transformed <= 0.0F)
        {
            return std::unexpected(ProjectionError::invalid_scale);
        }
        return transformed;
    }
} // namespace gc::windowed_widescreen

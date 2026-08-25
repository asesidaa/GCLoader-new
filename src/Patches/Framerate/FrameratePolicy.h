#pragma once

#include <cstdint>

namespace gc::framerate
{
    inline constexpr std::uint32_t kMinimumTargetFps = 60;
    inline constexpr std::uint32_t kMaximumTargetFps = 500;

    [[nodiscard]] constexpr bool IsTargetFpsInRange(
        std::uint32_t value) noexcept
    {
        return value >= kMinimumTargetFps && value <= kMaximumTargetFps;
    }

    [[nodiscard]] constexpr bool IsGameplayValidatedTargetFps(
        std::uint32_t value) noexcept
    {
        switch (value)
        {
        case 60:
        case 120:
        case 144:
        case 165:
        case 240:
        case 360:
            return true;
        default:
            return false;
        }
    }
} // namespace gc::framerate

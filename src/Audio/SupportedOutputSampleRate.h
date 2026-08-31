#pragma once

#include <cstdint>

namespace gc::audio
{
    inline constexpr std::uint32_t kGamePrimarySampleRate = 44'100;
    inline constexpr std::uint32_t kFallbackEndpointSampleRate = 48'000;

    [[nodiscard]] constexpr bool IsSupportedOutputSampleRate(
        const std::uint32_t rate) noexcept
    {
        return rate == kGamePrimarySampleRate ||
            rate == kFallbackEndpointSampleRate;
    }
} // namespace gc::audio

#pragma once

#include <cstdint>
#include <string_view>

namespace gc::windowed_widescreen
{
    enum class StageClipPolicy : std::uint8_t
    {
        authored,
        live_frustum,
    };

    [[nodiscard]] constexpr std::string_view StageClipPolicyName(
        const StageClipPolicy policy) noexcept
    {
        switch (policy)
        {
        case StageClipPolicy::authored:
            return "authored";
        case StageClipPolicy::live_frustum:
            return "live_frustum";
        }
        return "unknown";
    }
} // namespace gc::windowed_widescreen

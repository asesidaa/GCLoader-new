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

    enum class ClipGateAction : std::uint8_t
    {
        continue_authored,
        jump_live_frustum,
    };

    [[nodiscard]] ClipGateAction SelectClipGateAction(
        StageClipPolicy policy) noexcept;

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

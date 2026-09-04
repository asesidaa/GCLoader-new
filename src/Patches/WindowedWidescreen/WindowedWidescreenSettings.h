#pragma once

#include <cstdint>
#include <string_view>

namespace gc::config
{
    class ConfigCompiler;
}

namespace gc::windowed_widescreen
{
    enum class GameplayHudPlacement : std::uint8_t
    {
        left,
        center,
        right,
    };

    [[nodiscard]] constexpr std::string_view GameplayHudPlacementName(
        const GameplayHudPlacement placement) noexcept
    {
        switch (placement)
        {
        case GameplayHudPlacement::left:
            return "left";
        case GameplayHudPlacement::center:
            return "center";
        case GameplayHudPlacement::right:
            return "right";
        }
        return "unknown";
    }

    class WindowedWidescreenSettings final
    {
    public:
        [[nodiscard]] bool enabled() const noexcept
        {
            return enabled_;
        }

        [[nodiscard]] std::uint32_t output_width() const noexcept
        {
            return output_width_;
        }

        [[nodiscard]] std::uint32_t output_height() const noexcept
        {
            return output_height_;
        }

        [[nodiscard]] GameplayHudPlacement gameplay_hud_placement()
            const noexcept
        {
            return gameplay_hud_placement_;
        }

    private:
        WindowedWidescreenSettings(
            const bool enabled,
            const std::uint32_t output_width,
            const std::uint32_t output_height,
            const GameplayHudPlacement gameplay_hud_placement) noexcept
            : enabled_(enabled),
              output_width_(output_width),
              output_height_(output_height),
              gameplay_hud_placement_(gameplay_hud_placement)
        {
        }

        friend class gc::config::ConfigCompiler;
        bool enabled_{};
        std::uint32_t output_width_{};
        std::uint32_t output_height_{};
        GameplayHudPlacement gameplay_hud_placement_{
            GameplayHudPlacement::center};
    };
} // namespace gc::windowed_widescreen

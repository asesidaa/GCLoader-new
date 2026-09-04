#pragma once

#include "Patches/WindowedWidescreen/ResolutionModel.h"
#include "Patches/WindowedWidescreen/WindowedWidescreenSettings.h"

#include <cstdint>
#include <expected>

namespace gc::windowed_widescreen
{
    struct GameplayHudViewport
    {
        std::uint32_t x{};
        std::uint32_t y{};
        std::uint32_t width{};
        std::uint32_t height{};

        bool operator==(const GameplayHudViewport&) const = default;
    };

    enum class GameplayFeedbackPlacementError : std::uint8_t
    {
        invalid_output,
    };

    [[nodiscard]] std::expected<
        GameplayHudViewport,
        GameplayFeedbackPlacementError>
    ResolveGameplayHudViewport(
        OutputSize output,
        GameplayHudPlacement placement) noexcept;

    [[nodiscard]] GameplayHudPlacement ResolveComboHudPlacement(
        std::int32_t entry) noexcept;

} // namespace gc::windowed_widescreen

#include "Patches/WindowedWidescreen/GameplayFeedbackPlacement.h"

namespace gc::windowed_widescreen
{
    std::expected<GameplayHudViewport, GameplayFeedbackPlacementError>
    ResolveGameplayHudViewport(
        const OutputSize output,
        const GameplayHudPlacement placement) noexcept
    {
        if (output.width < kNativeWidth || output.height != kNativeHeight)
        {
            return std::unexpected(
                GameplayFeedbackPlacementError::invalid_output);
        }

        std::uint32_t x{};
        switch (placement)
        {
        case GameplayHudPlacement::center:
            x = (output.width - kNativeWidth) / 2;
            break;
        case GameplayHudPlacement::left:
            x = 0;
            break;
        case GameplayHudPlacement::right:
            x = output.width - kNativeWidth;
            break;
        }

        return GameplayHudViewport{
            .x = x,
            .y = 0,
            .width = kNativeWidth,
            .height = kNativeHeight,
        };
    }

    GameplayHudPlacement ResolveComboHudPlacement(
        const std::int32_t entry) noexcept
    {
        switch (entry)
        {
        case 0:
            return GameplayHudPlacement::right;
        case 1:
            return GameplayHudPlacement::left;
        default:
            return GameplayHudPlacement::center;
        }
    }

} // namespace gc::windowed_widescreen

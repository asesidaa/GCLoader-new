#pragma once

#include "Audio/AudioSettings.h"
#include "Audio/DirectSound/GameplayAudioCursorObservation.h"
#include "Patches/Framerate/FramerateSettings.h"
#include "Patches/Framerate/FrameratePatchPlan.h"
#include "Patches/Framerate/GameplaySongClock.h"

#include <cstdint>
#include <optional>

namespace gc::framerate
{
    enum class FramerateHookId;
    enum class GameplayAudioClockPlan : std::uint8_t;

    [[nodiscard]] std::expected<PreparedFrameratePlan, game_version::PlanError>
    BuildFrameratePlan(game_version::GameBuild, game_version::GameImageVariant,
        const FramerateSettings&, audio::AudioBackend) noexcept;
    [[nodiscard]] std::expected<void, game_version::PlanError>
    PrepareFramerateRuntimeBindings(const game_version::ApprovedVersionedPlan&,
        const runtime_image::RuntimeImage&) noexcept;
    void CompleteFramerateStartup(const game_version::ApprovedVersionedPlan&) noexcept;

} // namespace gc::framerate

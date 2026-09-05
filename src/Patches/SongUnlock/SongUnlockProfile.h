#pragma once
#include "Patches/GameVersion/VersionedPlan.h"
namespace gc::song_unlock {
[[nodiscard]] std::expected<game_version::FeaturePlan, game_version::PlanError> BuildSongUnlockPlan(
    game_version::GameBuild, game_version::GameImageVariant, bool enabled) noexcept;
}

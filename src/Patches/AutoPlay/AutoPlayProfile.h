#pragma once
#include "Patches/GameVersion/VersionedPlan.h"
namespace gc::auto_play {
[[nodiscard]] std::expected<game_version::FeaturePlan, game_version::PlanError> BuildAutoPlayPlan(
    game_version::GameBuild, game_version::GameImageVariant, bool enabled) noexcept;
}

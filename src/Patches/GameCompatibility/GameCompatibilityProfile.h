#pragma once
#include "Patches/GameVersion/VersionedPlan.h"
namespace gc::game_compatibility {
[[nodiscard]] std::expected<game_version::FeaturePlan, game_version::PlanError> BuildGameCompatibilityPlan(
    game_version::GameBuild, game_version::GameImageVariant) noexcept;
}

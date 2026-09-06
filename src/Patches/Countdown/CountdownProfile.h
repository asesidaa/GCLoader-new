#pragma once
#include "Patches/GameVersion/VersionedPlan.h"
#include <span>
namespace gc::timer_freeze {
struct CountdownProfile final {
    game_version::GameBuild build;
    game_version::GameImageVariant variant;
    std::span<const game_version::VersionedOperation> operations;
};
[[nodiscard]] const CountdownProfile* ProfileFor(
    game_version::GameBuild, game_version::GameImageVariant) noexcept;
[[nodiscard]] std::expected<game_version::FeaturePlan, game_version::PlanError>
BuildCountdownPlan(game_version::GameBuild, game_version::GameImageVariant, bool enabled) noexcept;
}

#pragma once
#include "Patches/GameVersion/VersionedPlan.h"
namespace gc::timer_freeze {
struct CountdownProfile final {
    game_version::GameBuild build;
    game_version::GameImageVariant variant;
    std::array<game_version::VersionedOperation, 32> operations;
};
[[nodiscard]] const CountdownProfile* ProfileFor(
    game_version::GameBuild, game_version::GameImageVariant) noexcept;
[[nodiscard]] std::expected<game_version::FeaturePlan, game_version::PlanError>
BuildCountdownPlan(game_version::GameBuild, game_version::GameImageVariant, bool enabled) noexcept;
}

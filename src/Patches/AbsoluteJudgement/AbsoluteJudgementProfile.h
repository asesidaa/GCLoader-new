#pragma once
#include "Patches/AbsoluteJudgement/NativeJudgementAbi.h"
#include "Patches/GameVersion/VersionedPlan.h"

namespace gc::absolute_judgement {
struct AbsoluteJudgementProfile final {
    game_version::GameBuild build;
    game_version::GameImageVariant variant;
    std::array<game_version::VersionedOperation, 18> enabled_operations;
    // Stage entry still reads configuration in stock/ASIO mode.
    std::array<game_version::VersionedOperation, 4> disabled_operations;
    native_abi::NativeLayout layout;
};
[[nodiscard]] const AbsoluteJudgementProfile* ProfileFor(
    game_version::GameBuild, game_version::GameImageVariant) noexcept;
[[nodiscard]] std::expected<game_version::FeaturePlan, game_version::PlanError>
BuildAbsoluteJudgementPlan(game_version::GameBuild, game_version::GameImageVariant, bool enabled) noexcept;
}

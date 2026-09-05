#pragma once
#include "Patches/GameVersion/VersionedPlan.h"
namespace gc::auto_play {
[[nodiscard]] std::expected<void, game_version::PlanError> PrepareAutoPlayRuntime(
    const game_version::ApprovedVersionedPlan&) noexcept;
void ActivateAutoPlayMarker(const game_version::ApprovedVersionedPlan&) noexcept;
void AutoPlayMarkerMidHook(safetyhook::Context&) noexcept;
}

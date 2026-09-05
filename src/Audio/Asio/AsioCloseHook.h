#pragma once
#include "Patches/GameVersion/VersionedPlan.h"
namespace gc::audio::asio {
[[nodiscard]] std::expected<void, game_version::PlanError>
PrepareAsioCloseRuntime(const game_version::ApprovedVersionedPlan&) noexcept;
void OnOrdinaryAsioClose(safetyhook::Context&) noexcept;
}

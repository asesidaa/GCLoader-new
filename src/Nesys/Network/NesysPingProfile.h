#pragma once
#include "Patches/GameVersion/VersionedPlan.h"
namespace gc::nesys_service {
struct NesysPingProfile final {
    NesysBuild build;
    NesysImageVariant variant;
    std::array<game_version::VersionedOperation, 1> operations;
};
[[nodiscard]] const NesysPingProfile* PingProfileFor(NesysBuild, NesysImageVariant) noexcept;
[[nodiscard]] std::expected<game_version::FeaturePlan, game_version::PlanError>
BuildNesysPingPlan(NesysBuild, NesysImageVariant) noexcept;
void OnServicePingAddress(safetyhook::Context&) noexcept;
}

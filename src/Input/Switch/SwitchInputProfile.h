#pragma once
#include "Input/Switch/SwitchInputPolicy.h"
#include "Input/Switch/SwitchInputSettings.h"
#include "Patches/GameVersion/VersionedPlan.h"
#include <array>
namespace gc::switch_input {
using GameplayQueryFn = std::uint8_t(__thiscall*)(
    void* self, int input_device_id, LogicalInputId logical_input, int gameplay_frame);
struct DiagonalStackLayout final {
    std::ptrdiff_t native_match_offset{};
    std::ptrdiff_t target_direction_offset{};
    std::ptrdiff_t current_direction_offset{};
};
struct SwitchInputProfile final {
    game_version::GameBuild build;
    game_version::GameImageVariant variant;
    std::array<game_version::VersionedOperation, 3> operations;
    DiagonalStackLayout diagonal;
};
[[nodiscard]] const SwitchInputProfile* ProfileFor(
    game_version::GameBuild, game_version::GameImageVariant) noexcept;
[[nodiscard]] std::expected<game_version::FeaturePlan, game_version::PlanError> BuildSwitchInputPlan(
    game_version::GameBuild, game_version::GameImageVariant, const SwitchInputSettings&) noexcept;
}

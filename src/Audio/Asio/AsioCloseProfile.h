#pragma once
#include "Audio/Asio/AsioCloseHook.h"
namespace gc::audio::asio {
struct AsioCloseProfile final {
    game_version::GameBuild build;
    game_version::GameImageVariant variant;
    std::array<game_version::VersionedOperation, 1> operations;
};
[[nodiscard]] const AsioCloseProfile* ProfileFor(
    game_version::GameBuild, game_version::GameImageVariant) noexcept;
[[nodiscard]] std::expected<game_version::FeaturePlan, game_version::PlanError>
BuildAsioClosePlan(game_version::GameBuild, game_version::GameImageVariant) noexcept;
}

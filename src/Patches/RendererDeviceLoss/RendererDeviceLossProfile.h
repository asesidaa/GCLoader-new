#pragma once
#include "Patches/RendererDeviceLoss/RendererDeviceLossPatch.h"

namespace gc::renderer_device_loss {
struct RendererDeviceLossProfile final {
    game_version::GameBuild build;
    game_version::GameImageVariant variant;
    std::array<game_version::VersionedOperation, 10> operations;
    RendererNativeLayout layout;
};
[[nodiscard]] const RendererDeviceLossProfile* ProfileFor(
    game_version::GameBuild, game_version::GameImageVariant) noexcept;
[[nodiscard]] std::expected<game_version::FeaturePlan, game_version::PlanError>
BuildRendererDeviceLossPlan(game_version::GameBuild, game_version::GameImageVariant) noexcept;
}

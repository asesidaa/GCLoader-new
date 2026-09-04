#pragma once
#include "Patches/GameVersion/VersionedPlan.h"
namespace gc::config { class ValidatedConfig; }
namespace gc::loader {
struct StartupPlanError final {
    std::optional<game_version::DetectionError> detection;
    std::optional<game_version::PlanError> plan;
    std::optional<runtime_image::RuntimeImageError> memory;
};
struct PreparedGameVersionedStartup final {
    game_version::GameSelection selection;
    runtime_image::RuntimeImage image;
    game_version::ApprovedVersionedPlan plan;
};
[[nodiscard]] std::expected<PreparedGameVersionedStartup, StartupPlanError>
PrepareGameVersionedStartup(HMODULE process_module, const config::ValidatedConfig& config) noexcept;
}

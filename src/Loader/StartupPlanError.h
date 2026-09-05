#pragma once
#include "Patches/GameVersion/VersionedPlan.h"
namespace gc::loader {
struct StartupPlanError final {
    std::optional<game_version::DetectionError> detection;
    std::optional<game_version::PlanError> plan;
    std::optional<runtime_image::RuntimeImageError> memory;
};
}

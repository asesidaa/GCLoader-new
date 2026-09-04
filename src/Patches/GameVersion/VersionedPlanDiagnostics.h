#pragma once
#include "Patches/GameVersion/VersionedPlan.h"
#include "Diagnostics/FatalProcess.h"
namespace gc::game_version {
[[nodiscard]] diagnostics::FatalProcessReport FormatPlanError(const PlanError&);
[[nodiscard]] const char* PlanStageName(PlanStage) noexcept;
}

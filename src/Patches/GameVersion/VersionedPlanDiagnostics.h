#pragma once
#include "Patches/GameVersion/VersionedPlan.h"
#include "Diagnostics/FatalProcess.h"
namespace gc::game_version {
[[nodiscard]] std::string FormatContractBytes(const runtime_image::BytePattern&);
[[nodiscard]] std::string FormatPlanContext(const PlanContext&);
[[nodiscard]] diagnostics::FatalProcessReport FormatPlanError(const PlanError&);
[[nodiscard]] const char* PlanStageName(PlanStage) noexcept;
}

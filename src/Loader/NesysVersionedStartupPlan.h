#pragma once
#include "Loader/StartupPlanError.h"
namespace gc::nesys_service { class NesysSettings; }
namespace gc::loader {
struct PreparedNesysVersionedStartup final {
    game_version::NesysSelection selection;
    runtime_image::RuntimeImage image;
    game_version::ApprovedVersionedPlan plan;
};
[[nodiscard]] std::expected<std::optional<PreparedNesysVersionedStartup>, StartupPlanError>
PrepareNesysVersionedStartup(
    HMODULE process_module, const nesys_service::NesysSettings& settings) noexcept;
}

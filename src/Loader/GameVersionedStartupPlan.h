#pragma once
#include "Loader/StartupPlanError.h"
#include "Audio/AudioFeature.h"
namespace gc::config { class ValidatedConfig; }
namespace gc::loader {
struct PreparedGameVersionedStartup final {
    game_version::GameSelection selection;
    runtime_image::RuntimeImage image;
    game_version::ApprovedVersionedPlan plan;
    audio::PreparedAudioFeature audio;
};
[[nodiscard]] std::expected<PreparedGameVersionedStartup, StartupPlanError>
PrepareGameVersionedStartup(HMODULE process_module, const config::ValidatedConfig& config) noexcept;
}

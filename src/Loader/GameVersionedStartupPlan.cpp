#include "Loader/GameVersionedStartupPlan.h"

namespace gc::loader {
std::expected<PreparedGameVersionedStartup, StartupPlanError>
PrepareGameVersionedStartup(HMODULE process_module, const config::ValidatedConfig&) noexcept {
    if (process_module != GetModuleHandleW(nullptr))
        return std::unexpected(StartupPlanError{.detection = game_version::DetectionError{
            game_version::DetectionStage::identity,
            game_version::IdentityError{game_version::IdentityStage::module_path, ERROR_INVALID_HANDLE}}});
    auto detected = game_version::DetectGameBuild(process_module);
    if (!detected) return std::unexpected(StartupPlanError{.detection = detected.error()});
    const auto image = runtime_image::RuntimeImage::MainModule();
    if (!image) return std::unexpected(StartupPlanError{.memory = image.error()});
    game_version::VersionedPlanSet plans;
    if (const auto required = plans.Require({game_version::FeatureId::game_compatibility, true, true}); !required)
        return std::unexpected(StartupPlanError{.plan = required.error()});
    // Plans 06a-06h add feature builders explicitly here. Until they do, the
    // missing mandatory profile rejects preparation; this entry point is dormant.
    auto approved = plans.Validate(*image, *detected);
    if (!approved) return std::unexpected(StartupPlanError{.plan = approved.error()});
    auto selection = std::visit([&](auto& value) {
        return game_version::GameSelection{value.build, value.variant,
            approved->context().proof, std::move(value.identity)};
    }, *detected);
    return PreparedGameVersionedStartup{std::move(selection), *image, std::move(*approved)};
}
}

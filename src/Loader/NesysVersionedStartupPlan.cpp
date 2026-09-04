#include "Loader/NesysVersionedStartupPlan.h"

namespace gc::loader {
std::expected<PreparedNesysVersionedStartup, StartupPlanError>
PrepareNesysVersionedStartup(HMODULE process_module, const nesys_service::NesysSettings&) noexcept {
    if (process_module != GetModuleHandleW(nullptr))
        return std::unexpected(StartupPlanError{.detection = game_version::DetectionError{
            game_version::DetectionStage::identity,
            game_version::IdentityError{game_version::IdentityStage::module_path, ERROR_INVALID_HANDLE}}});
    auto detected = game_version::DetectNesysBuild(process_module);
    if (!detected) return std::unexpected(StartupPlanError{.detection = detected.error()});
    const auto image = runtime_image::RuntimeImage::MainModule();
    if (!image) return std::unexpected(StartupPlanError{.memory = image.error()});
    game_version::VersionedPlanSet plans;
    if (const auto required = plans.Require({game_version::FeatureId::nesys_ping, true, true}); !required)
        return std::unexpected(StartupPlanError{.plan = required.error()});
    // The NESYS builder is added in Plan 06h; no game builder belongs here.
    auto approved = plans.Validate(*image, *detected);
    if (!approved) return std::unexpected(StartupPlanError{.plan = approved.error()});
    auto selection = std::visit([&](auto& value) {
        return game_version::NesysSelection{value.build, value.variant,
            approved->context().proof, std::move(value.identity)};
    }, *detected);
    return PreparedNesysVersionedStartup{std::move(selection), *image, std::move(*approved)};
}
}

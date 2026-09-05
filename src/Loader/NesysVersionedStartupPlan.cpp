#include "Loader/NesysVersionedStartupPlan.h"
#include "Nesys/NesysSettings.h"
#include "Nesys/NesysServiceProcess.h"
#include "Nesys/Network/NesysPingProfile.h"

namespace gc::loader {
std::expected<std::optional<PreparedNesysVersionedStartup>, StartupPlanError>
PrepareNesysVersionedStartup(HMODULE process_module, const nesys_service::NesysSettings& settings) noexcept {
    const auto features = nesys_service::ResolveNesysFeaturePlan(
        nesys_service::ProcessRole::Service, settings.adapter_patch_enabled(),
        settings.registry_override().has_value());
    if (!features.service_ping_redirect) return std::optional<PreparedNesysVersionedStartup>{};
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
    const auto profile = std::visit([](const auto& selection) {
        return nesys_service::BuildNesysPingPlan(selection.build, selection.variant);
    }, *detected);
    if (!profile) return std::unexpected(StartupPlanError{.plan = profile.error()});
    if (const auto added = plans.Add(*profile); !added)
        return std::unexpected(StartupPlanError{.plan = added.error()});
    auto approved = plans.Validate(*image, *detected);
    if (!approved) return std::unexpected(StartupPlanError{.plan = approved.error()});
    auto selection = std::visit([&](auto& value) {
        return game_version::NesysSelection{value.build, value.variant,
            approved->context().proof, std::move(value.identity)};
    }, *detected);
    return PreparedNesysVersionedStartup{std::move(selection), *image, std::move(*approved)};
}
}

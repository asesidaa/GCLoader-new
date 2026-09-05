#include "Loader/TransitionalVersionedStartup.h"
#include "Loader/VersionedStartupExecutor.h"
#include "Config/ConfigCompiler.h"
#include "Input/Switch/SwitchInputPatch.h"
#include "Patches/GameVersion/VersionedPlanDiagnostics.h"
#include "Patches/GameCompatibility/GameCompatibilityProfile.h"
#include "Patches/AutoPlay/AutoPlayProfile.h"
#include "Patches/AutoPlay/AutoPlayPatch.h"
#include "Patches/SongUnlock/SongUnlockProfile.h"
#include "plog/Log.h"
#include <format>

namespace gc::loader {
namespace {
// Transitional staging only: DllMain keeps its existing early compatibility
// and post-config optional slots until every family joins the dormant complete
// plan. Delete this adapter at Plan09; intermediate DLLs are not release checkpoints.
std::optional<game_version::GameDetection> g_detection;
void Install(std::expected<game_version::FeaturePlan, game_version::PlanError> profile) {
    using namespace game_version;
    if (!profile) diagnostics::AbortProcess(FormatPlanError(profile.error()));
    const auto image = runtime_image::RuntimeImage::MainModule();
    if (!image) diagnostics::AbortProcess(FormatStartupInstallError({
        .stage = StartupInstallStage::image_binding, .memory = image.error()}));
    VersionedPlanSet plans;
    if (const auto result = plans.Require({profile->feature,
            profile->feature == FeatureId::game_compatibility, true}); !result)
        diagnostics::AbortProcess(FormatPlanError(result.error()));
    if (const auto result = plans.Add(*profile); !result)
        diagnostics::AbortProcess(FormatPlanError(result.error()));
    const auto approved = plans.Validate(*image, *g_detection);
    if (!approved) diagnostics::AbortProcess(FormatPlanError(approved.error()));
    if (const auto prepared = auto_play::PrepareAutoPlayRuntime(*approved); !prepared)
        diagnostics::AbortProcess(FormatPlanError(prepared.error()));
    if (const auto result = InstallApprovedVersionedPlan(
            *approved, *image, hooking::HookRegistry::ProcessLifetime()); !result)
        diagnostics::AbortProcess(FormatStartupInstallError(result.error()));
    auto_play::ActivateAutoPlayMarker(*approved);
    switch_input::ActivateSwitchInput(*approved);
}
}
void InstallTransitionalGameCompatibility() noexcept {
    try {
        auto detection = game_version::DetectGameBuild(GetModuleHandleW(nullptr));
        if (!detection) {
            const auto& error = detection.error();
            diagnostics::AbortProcess({
                std::format("Game build detection failed stage={} identity_stage={} win32_error={} cng_status={}",
                    static_cast<unsigned>(error.stage),
                    error.identity ? static_cast<unsigned>(error.identity->stage) : 0,
                    error.identity ? error.identity->win32_error : 0,
                    error.identity ? error.identity->cng_status : 0),
                L"GCLoader could not identify this game executable. Check loader-log.txt.",
                L"GCLoader executable validation error"});
        }
        g_detection = std::move(*detection);
        Install(std::visit([](const auto& selection) {
            return game_compatibility::BuildGameCompatibilityPlan(selection.build, selection.variant);
        }, *g_detection));
    } catch (...) { diagnostics::AbortProcess({}); }
}
void InstallTransitionalSwitchInput(const switch_input::SwitchInputSettings& settings) noexcept {
    try {
        if (settings.style() == input::GameplayInputStyle::Arcade) {
            PLOG_INFO << "SwitchInputPatch: requested_style=Arcade active_style=Arcade";
            return;
        }
        if (!g_detection) diagnostics::AbortProcess({});
        Install(std::visit([&](const auto& selection) {
            return switch_input::BuildSwitchInputPlan(selection.build, selection.variant, settings);
        }, *g_detection));
    } catch (...) { diagnostics::AbortProcess({}); }
}
void InstallTransitionalOptionalPatches(const config::ValidatedConfig& settings) noexcept {
    try {
        if (!g_detection) diagnostics::AbortProcess({});
        if (settings.enable_auto_play()) {
            Install(std::visit([](const auto& selection) {
                return auto_play::BuildAutoPlayPlan(selection.build, selection.variant, true);
            }, *g_detection));
        } else PLOG_INFO << "AutoPlayPatch: state=disabled";
        if (settings.unlock_all_songs_and_difficulties()) {
            Install(std::visit([](const auto& selection) {
                return song_unlock::BuildSongUnlockPlan(selection.build, selection.variant, true);
            }, *g_detection));
        } else PLOG_INFO << "SongUnlockPatch: state=disabled";
    } catch (...) { diagnostics::AbortProcess({}); }
}
}

#include "Loader/TransitionalVersionedStartup.h"
#include "Loader/VersionedStartupExecutor.h"
#include "Config/ConfigCompiler.h"
#include "Input/Switch/SwitchInputPatch.h"
#include "Patches/AbsoluteJudgement/AbsoluteJudgementPatch.h"
#include "Patches/AbsoluteJudgement/AbsoluteJudgementProfile.h"
#include "Patches/GameVersion/VersionedPlanDiagnostics.h"
#include "Patches/GameCompatibility/GameCompatibilityProfile.h"
#include "Patches/AutoPlay/AutoPlayProfile.h"
#include "Patches/AutoPlay/AutoPlayPatch.h"
#include "Patches/SongUnlock/SongUnlockProfile.h"
#include "Patches/Framerate/FrameratePatch.h"
#include "Patches/Countdown/CountdownProfile.h"
#include "Patches/TestModeTiming/TestModeTimingProfile.h"
#include "Patches/TestModeTiming/TimingSettingsPatch.h"
#include "Patches/RendererDeviceLoss/RendererDeviceLossProfile.h"
#include "Patches/WindowedWidescreen/WindowedWidescreenPatch.h"
#include "plog/Log.h"
#include <format>

namespace gc::loader {
namespace {
// Transitional staging only: DllMain keeps its existing early compatibility
// and post-config optional slots until every family joins the dormant complete
// plan. Delete this adapter at Plan09; intermediate DLLs are not release checkpoints.
std::optional<game_version::GameDetection> g_detection;
void Install(std::expected<game_version::FeaturePlan, game_version::PlanError> profile,
             const absolute_judgement::JudgementSettings* judgement = nullptr) {
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
    if (judgement) {
        if (const auto prepared = absolute_judgement::PrepareAbsoluteJudgementRuntime(
                *approved, *image, *judgement); !prepared)
            diagnostics::AbortProcess(FormatPlanError(prepared.error()));
    }
    if (profile->feature == FeatureId::test_mode_timing) {
        if (const auto prepared = test_mode_timing::PrepareTestModeTimingRuntime(*approved, *image); !prepared)
            diagnostics::AbortProcess(FormatPlanError(prepared.error()));
    }
    if (profile->feature == FeatureId::renderer_device_loss) {
        if (const auto prepared = renderer_device_loss::PrepareRendererDeviceLossRuntime(*approved, *image); !prepared)
            diagnostics::AbortProcess(FormatPlanError(prepared.error()));
    }
    if (const auto result = InstallApprovedVersionedPlan(
            *approved, *image, hooking::HookRegistry::ProcessLifetime()); !result)
        diagnostics::AbortProcess(FormatStartupInstallError(result.error()));
    if (profile->feature == FeatureId::test_mode_timing) test_mode_timing::CompleteTestModeTimingStartup();
    auto_play::ActivateAutoPlayMarker(*approved);
    switch_input::ActivateSwitchInput(*approved);
    if (judgement) absolute_judgement::CompleteAbsoluteJudgementStartup(*judgement);
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
void InstallTransitionalAbsoluteJudgement(const absolute_judgement::JudgementSettings& settings) noexcept {
    try {
        if (!g_detection) diagnostics::AbortProcess({});
        Install(std::visit([&](const auto& selection) {
            return absolute_judgement::BuildAbsoluteJudgementPlan(
                selection.build, selection.variant, settings.enabled());
        }, *g_detection), &settings);
    } catch (...) { diagnostics::AbortProcess({}); }
}
void InstallTransitionalFramerate(const framerate::FramerateSettings& settings, audio::AudioBackend backend) noexcept {
    using namespace game_version;
    try {
        if (!g_detection) diagnostics::AbortProcess({});
        auto frame = std::visit([&](const auto& selection) {
            return framerate::BuildFrameratePlan(selection.build, selection.variant, settings, backend);
        }, *g_detection);
        if (!frame) diagnostics::AbortProcess(FormatPlanError(frame.error()));
        const auto image = runtime_image::RuntimeImage::MainModule();
        if (!image) diagnostics::AbortProcess(FormatStartupInstallError({
            .stage = StartupInstallStage::image_binding, .memory = image.error()}));
        VersionedPlanSet plans;
        if (const auto required = plans.Require({FeatureId::framerate, true, true}); !required)
            diagnostics::AbortProcess(FormatPlanError(required.error()));
        if (const auto added = plans.Add(frame->feature_plan()); !added)
            diagnostics::AbortProcess(FormatPlanError(added.error()));
        if (settings.timer_freeze_enabled()) {
            auto timer = std::visit([](const auto& selection) {
                return timer_freeze::BuildCountdownPlan(selection.build, selection.variant, true);
            }, *g_detection);
            if (!timer) diagnostics::AbortProcess(FormatPlanError(timer.error()));
            constexpr std::array after_framerate{FeatureId::framerate};
            timer->install_after = after_framerate;
            if (const auto required = plans.Require({FeatureId::countdown, false, true}); !required)
                diagnostics::AbortProcess(FormatPlanError(required.error()));
            if (const auto added = plans.Add(*timer); !added)
                diagnostics::AbortProcess(FormatPlanError(added.error()));
        }
        const auto approved = plans.Validate(*image, *g_detection);
        if (!approved) diagnostics::AbortProcess(FormatPlanError(approved.error()));
        if (const auto prepared = framerate::PrepareFramerateRuntimeBindings(*approved, *image); !prepared)
            diagnostics::AbortProcess(FormatPlanError(prepared.error()));
        if (const auto installed = InstallApprovedVersionedPlan(
                *approved, *image, hooking::HookRegistry::ProcessLifetime()); !installed)
            diagnostics::AbortProcess(FormatStartupInstallError(installed.error()));
        framerate::CompleteFramerateStartup(*approved);
    } catch (...) { diagnostics::AbortProcess({}); }
}
void InstallTransitionalTestModeTiming() noexcept {
    try {
        if (!g_detection) diagnostics::AbortProcess({});
        Install(std::visit([](const auto& selection) {
            return test_mode_timing::BuildTestModeTimingPlan(selection.build, selection.variant);
        }, *g_detection));
    } catch (...) { diagnostics::AbortProcess({}); }
}
void InstallTransitionalRendererDeviceLoss() noexcept {
    try {
        if (!g_detection) diagnostics::AbortProcess({});
        Install(std::visit([](const auto& selection) {
            return renderer_device_loss::BuildRendererDeviceLossPlan(selection.build, selection.variant);
        }, *g_detection));
    } catch (...) { diagnostics::AbortProcess({}); }
}
void InstallTransitionalWidescreen(const windowed_widescreen::WindowedWidescreenSettings& settings) noexcept {
    using namespace game_version;
    try {
        if (!settings.enabled()) return;
        if (!g_detection) diagnostics::AbortProcess({});
        const auto image = runtime_image::RuntimeImage::MainModule();
        if (!image) diagnostics::AbortProcess(FormatStartupInstallError({
            .stage = StartupInstallStage::image_binding, .memory = image.error()}));
        const auto wide = std::visit([&](const auto& selection) {
            return windowed_widescreen::BuildWidescreenPlan(selection.build, selection.variant, true, *image);
        }, *g_detection);
        if (!wide) diagnostics::AbortProcess(FormatPlanError(wide.error()));
        VersionedPlanSet plans;
        if (const auto required = plans.Require({FeatureId::windowed_widescreen, false, true}); !required)
            diagnostics::AbortProcess(FormatPlanError(required.error()));
        if (const auto added = plans.Add(wide->feature_plan()); !added)
            diagnostics::AbortProcess(FormatPlanError(added.error()));
        const auto approved = plans.Validate(*image, *g_detection);
        if (!approved) diagnostics::AbortProcess(FormatPlanError(approved.error()));
        if (const auto prepared = windowed_widescreen::PrepareWidescreenRuntime(settings, *approved, *image); !prepared)
            windowed_widescreen::AbortWidescreenStartup(prepared.error());
        if (const auto installed = InstallApprovedVersionedPlan(
                *approved, *image, hooking::HookRegistry::ProcessLifetime()); !installed)
            diagnostics::AbortProcess(FormatStartupInstallError(installed.error()));
        windowed_widescreen::CompleteWidescreenStartup();
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

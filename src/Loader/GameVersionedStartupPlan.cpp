#include "Loader/GameVersionedStartupPlan.h"
#include "Config/ConfigCompiler.h"
#include "Input/Switch/SwitchInputProfile.h"
#include "Patches/GameCompatibility/GameCompatibilityProfile.h"
#include "Patches/AutoPlay/AutoPlayProfile.h"
#include "Patches/AbsoluteJudgement/AbsoluteJudgementProfile.h"
#include "Patches/SongUnlock/SongUnlockProfile.h"
#include "Patches/Framerate/FramerateFeature.h"
#include "Patches/Countdown/CountdownProfile.h"
#include "Patches/TestModeTiming/TestModeTimingProfile.h"
#include "Patches/RendererDeviceLoss/RendererDeviceLossProfile.h"
#include "Patches/WindowedWidescreen/WindowedWidescreenProfile.h"
#include "Audio/AudioFeature.h"
#include <array>

namespace gc::loader {
std::expected<PreparedGameVersionedStartup, StartupPlanError>
PrepareGameVersionedStartup(HMODULE process_module, const config::ValidatedConfig& settings) noexcept {
    try {
    if (process_module != GetModuleHandleW(nullptr))
        return std::unexpected(StartupPlanError{.detection = game_version::DetectionError{
            game_version::DetectionStage::identity,
            game_version::IdentityError{game_version::IdentityStage::module_path, ERROR_INVALID_HANDLE}}});
    auto detected = game_version::DetectGameBuild(process_module);
    if (!detected) return std::unexpected(StartupPlanError{.detection = detected.error()});
    const auto image = runtime_image::RuntimeImage::MainModule();
    if (!image) return std::unexpected(StartupPlanError{.memory = image.error()});

    using namespace game_version;
    const auto build = std::visit([](const auto& value) { return value.build; }, *detected);
    const auto variant = std::visit([](const auto& value) { return value.variant; }, *detected);
    auto audio = audio::PrepareAudioFeature(settings.audio(), build, variant);
    if (!audio) return std::unexpected(StartupPlanError{.plan = audio.error()});
    VersionedPlanSet plans;
    std::optional<FeatureId> previous;
    const auto append = [&](std::expected<FeaturePlan, PlanError> feature, bool mandatory = false)
        -> std::expected<void, StartupPlanError> {
        if (!feature) return std::unexpected(StartupPlanError{.plan = feature.error()});
        if (const auto required = plans.Require({feature->feature, mandatory, true}); !required)
            return std::unexpected(StartupPlanError{.plan = required.error()});
        // PlanSet copies this dependency. Explicit source order is the frozen
        // versioned install order, including optional-feature gaps.
        const std::array dependency{previous.value_or(feature->feature)};
        feature->install_after = previous ? std::span<const FeatureId>{dependency} : std::span<const FeatureId>{};
        if (const auto added = plans.Add(*feature); !added)
            return std::unexpected(StartupPlanError{.plan = added.error()});
        previous = feature->feature;
        return {};
    };
    if (auto r = append(game_compatibility::BuildGameCompatibilityPlan(build, variant), true); !r)
        return std::unexpected(r.error());
    if (settings.enable_auto_play())
        if (auto r = append(auto_play::BuildAutoPlayPlan(build, variant, true)); !r) return std::unexpected(r.error());
    if (settings.unlock_all_songs_and_difficulties())
        if (auto r = append(song_unlock::BuildSongUnlockPlan(build, variant, true)); !r) return std::unexpected(r.error());
    if (audio->versioned)
        if (auto r = append(*audio->versioned); !r) return std::unexpected(r.error());
    if (auto r = append(test_mode_timing::BuildTestModeTimingPlan(build, variant), true); !r)
        return std::unexpected(r.error());
    if (auto r = append(renderer_device_loss::BuildRendererDeviceLossPlan(build, variant), true); !r)
        return std::unexpected(r.error());
    if (settings.windowed_widescreen().enabled()) {
        auto wide = windowed_widescreen::BuildWidescreenPlan(build, variant, true, *image);
        if (!wide) return std::unexpected(StartupPlanError{.plan = wide.error()});
        if (auto r = append(wide->feature_plan()); !r) return std::unexpected(r.error());
    }
    if (auto r = append(absolute_judgement::BuildAbsoluteJudgementPlan(
            build, variant, settings.judgement().enabled()), true); !r) return std::unexpected(r.error());
    auto frame = framerate::BuildFrameratePlan(build, variant, settings.framerate(), settings.audio().backend());
    if (!frame) return std::unexpected(StartupPlanError{.plan = frame.error()});
    if (auto r = append(frame->feature_plan(), true); !r) return std::unexpected(r.error());
    if (settings.framerate().timer_freeze_enabled())
        if (auto r = append(timer_freeze::BuildCountdownPlan(build, variant, true)); !r) return std::unexpected(r.error());
    if (settings.switch_input().style() == input::GameplayInputStyle::Switch)
        if (auto r = append(switch_input::BuildSwitchInputPlan(build, variant, settings.switch_input())); !r)
            return std::unexpected(r.error());
    auto approved = plans.Validate(*image, *detected);
    if (!approved) return std::unexpected(StartupPlanError{.plan = approved.error()});
    auto selection = std::visit([&](auto& value) {
        return GameSelection{value.build, value.variant, approved->context().proof, std::move(value.identity)};
    }, *detected);
    return PreparedGameVersionedStartup{std::move(selection), *image, std::move(*approved), std::move(*audio)};
    } catch (...) {
        return std::unexpected(StartupPlanError{.plan = game_version::PlanError{
            .stage = game_version::PlanStage::allocation, .site = "game_startup_plan"}});
    }
}
}

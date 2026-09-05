#include "Loader/GameVersionedStartupPlan.h"
#include "Config/ConfigCompiler.h"
#include "Input/Switch/SwitchInputProfile.h"
#include "Patches/GameCompatibility/GameCompatibilityProfile.h"
#include "Patches/AutoPlay/AutoPlayProfile.h"
#include "Patches/AbsoluteJudgement/AbsoluteJudgementProfile.h"
#include "Patches/SongUnlock/SongUnlockProfile.h"
#include "Patches/Framerate/FrameratePatch.h"
#include "Patches/Countdown/CountdownProfile.h"
#include <array>

namespace gc::loader {
std::expected<PreparedGameVersionedStartup, StartupPlanError>
PrepareGameVersionedStartup(HMODULE process_module, const config::ValidatedConfig& settings) noexcept {
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
    const auto build = std::visit([](const auto& value) { return value.build; }, *detected);
    const auto variant = std::visit([](const auto& value) { return value.variant; }, *detected);
    const auto append = [&](std::expected<game_version::FeaturePlan, game_version::PlanError> profile,
                            std::span<const game_version::FeatureId> dependencies = {})
        -> std::expected<void, StartupPlanError> {
        if (!profile) return std::unexpected(StartupPlanError{.plan = profile.error()});
        profile->install_after = dependencies;
        if (const auto added = plans.Add(*profile); !added)
            return std::unexpected(StartupPlanError{.plan = added.error()});
        return {};
    };
    if (const auto result = append(game_compatibility::BuildGameCompatibilityPlan(build, variant)); !result)
        return std::unexpected(result.error());
    constexpr std::array after_compatibility{game_version::FeatureId::game_compatibility};
    if (settings.switch_input().style() == input::GameplayInputStyle::Switch) {
        if (const auto required = plans.Require({game_version::FeatureId::switch_input, false, true}); !required)
            return std::unexpected(StartupPlanError{.plan = required.error()});
        if (const auto result = append(switch_input::BuildSwitchInputPlan(
                build, variant, settings.switch_input()), after_compatibility); !result)
            return std::unexpected(result.error());
    }
    constexpr std::array after_auto_play{game_version::FeatureId::auto_play};
    if (settings.enable_auto_play()) {
        if (const auto required = plans.Require({game_version::FeatureId::auto_play, false, true}); !required)
            return std::unexpected(StartupPlanError{.plan = required.error()});
        if (const auto result = append(auto_play::BuildAutoPlayPlan(build, variant, true), after_compatibility); !result)
            return std::unexpected(result.error());
    }
    if (settings.unlock_all_songs_and_difficulties()) {
        if (const auto required = plans.Require({game_version::FeatureId::song_unlock, false, true}); !required)
            return std::unexpected(StartupPlanError{.plan = required.error()});
        if (const auto result = append(song_unlock::BuildSongUnlockPlan(build, variant, true),
                settings.enable_auto_play() ? after_auto_play : after_compatibility); !result)
            return std::unexpected(result.error());
    }
    // Lifecycle contracts are mandatory even when judgement replacement is off.
    // Runtime publication later requires prepared transition transport and the
    // selected committed audio route; those capabilities remain feature APIs.
    if (const auto required = plans.Require({game_version::FeatureId::absolute_judgement, true, true}); !required)
        return std::unexpected(StartupPlanError{.plan = required.error()});
    if (const auto result = append(absolute_judgement::BuildAbsoluteJudgementPlan(
            build, variant, settings.judgement().enabled()), after_compatibility); !result)
        return std::unexpected(result.error());
    if (const auto required = plans.Require({game_version::FeatureId::framerate, true, true}); !required)
        return std::unexpected(StartupPlanError{.plan = required.error()});
    auto frame = framerate::BuildFrameratePlan(build, variant, settings.framerate(), settings.audio().backend());
    if (!frame) return std::unexpected(StartupPlanError{.plan = frame.error()});
    constexpr std::array after_judgement{game_version::FeatureId::absolute_judgement};
    if (const auto result = append(frame->feature_plan(), after_judgement); !result)
        return std::unexpected(result.error());
    if (settings.framerate().timer_freeze_enabled()) {
        if (const auto required = plans.Require({game_version::FeatureId::countdown, false, true}); !required)
            return std::unexpected(StartupPlanError{.plan = required.error()});
        constexpr std::array after_framerate{game_version::FeatureId::framerate};
        if (const auto result = append(timer_freeze::BuildCountdownPlan(build, variant, true), after_framerate); !result)
            return std::unexpected(result.error());
    }
    // Remaining families join in06e-06h. This entry point stays dormant until
    // Plan09 switches all versioned startup through one complete barrier.
    auto approved = plans.Validate(*image, *detected);
    if (!approved) return std::unexpected(StartupPlanError{.plan = approved.error()});
    auto selection = std::visit([&](auto& value) {
        return game_version::GameSelection{value.build, value.variant,
            approved->context().proof, std::move(value.identity)};
    }, *detected);
    return PreparedGameVersionedStartup{std::move(selection), *image, std::move(*approved)};
}
}

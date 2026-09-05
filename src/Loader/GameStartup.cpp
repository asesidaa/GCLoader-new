#include "Loader/GameStartup.h"
#include "Loader/GameVersionedStartupPlan.h"
#include "Loader/NonVersionedHookPlan.h"
#include "Audio/AudioFeature.h"
#include "Audio/Asio/AsioCloseHook.h"
#include "Input/Polling/InputPollingRuntime.h"
#include "Input/Win32/ImeSuppression.h"
#include "Input/Switch/SwitchInputPatch.h"
#include "Patches/AutoPlay/AutoPlayPatch.h"
#include "Patches/AbsoluteJudgement/AbsoluteJudgementPatch.h"
#include "Patches/Framerate/FramerateFeature.h"
#include "Patches/TestModeTiming/TimingSettingsPatch.h"
#include "Patches/RendererDeviceLoss/RendererDeviceLossPatch.h"
#include "Patches/WindowedWidescreen/WindowedWidescreenFeature.h"
#include "Patches/GameVersion/VersionedPlanDiagnostics.h"
#include <plog/Log.h>
#include <format>

namespace gc::loader {
std::expected<void, StartupError> StartGame(HMODULE loader_module, GameProcessConfiguration configuration) noexcept {
    bool mutation_started{};
    const auto plan_failure = [](const game_version::PlanError& error) {
        return std::unexpected(StartupError{.versioned = StartupPlanError{.plan = error}});
    };
    try {
        auto& settings = configuration.settings;
        PLOG_INFO << "Configuration startup transaction persisted=" << configuration.persisted;
        for (const auto change : configuration.changes)
            PLOG_INFO << "Configuration startup repair=" << StartupConfigChangeName(change);
        PLOG_INFO << "System path prepared configured=" << configuration.system_root.configured_path
            << " redirect=" << configuration.system_root.redirect_enabled;

        // Complete native validation precedes every executable mutation.
        auto startup = PrepareGameVersionedStartup(GetModuleHandleW(nullptr), settings);
        if (!startup) return std::unexpected(StartupError{.versioned = std::move(startup.error())});
        auto exports = PrepareGameNonVersionedHooks(loader_module, settings, configuration.system_root, startup->audio);
        if (!exports) return std::unexpected(std::move(exports.error()));

        // Plan contributors retain stable original/handler storage while building
        // requests. Native runtime bindings and audio publication follow resolution.
        if (const auto ime = input::DisableProcessIme(); !ime)
            return std::unexpected(StartupError{.stage = StartupStage::ime, .win32_error = ime.error().win32_error});
        PLOG_INFO << "Input IME suppression active api=ImmDisableIME thread_selector=all";
        if (auto input = input::ConfigureInputPollingRuntime(settings.input()); !input)
            return std::unexpected(StartupError{.stage = StartupStage::input, .detail = std::move(input.error())});
        if (auto audio = audio::PublishAudioFeature(std::move(startup->audio), *exports); !audio)
            return std::unexpected(StartupError{.stage = StartupStage::feature,
                .feature = "Audio", .operation = "runtime_publication",
                .win32_error = audio.error().win32_error,
                .detail = std::format("audio_stage={}", static_cast<unsigned>(audio.error().stage))});
        const auto& plan = startup->plan;
        const auto& image = startup->image;
        if (auto r = auto_play::PrepareAutoPlayRuntime(plan); !r) return plan_failure(r.error());
        if (settings.audio().backend() == audio::AudioBackend::asio)
            if (auto r = audio::asio::PrepareAsioCloseRuntime(plan); !r) return plan_failure(r.error());
        if (auto r = test_mode_timing::PrepareTestModeTimingRuntime(plan, image); !r) return plan_failure(r.error());
        if (auto r = renderer_device_loss::PrepareRendererDeviceLossRuntime(plan, image); !r) return plan_failure(r.error());
        if (auto r = windowed_widescreen::PrepareWidescreenRuntime(settings.windowed_widescreen(), plan, image); !r) {
            if (r.error().plan_error) return plan_failure(*r.error().plan_error);
            const auto& e = r.error();
            return std::unexpected(StartupError{.stage = StartupStage::feature,
                .feature = "WindowedWidescreen", .operation = "runtime_preparation",
                .detail = std::format("stage={} resolution={} window_policy={} resource={} d3d_stage={} d3d_hresult=0x{:08X}",
                    static_cast<unsigned>(e.stage), e.resolution_error ? static_cast<int>(*e.resolution_error) : -1,
                    e.window_policy_error ? static_cast<int>(*e.window_policy_error) : -1,
                    e.resource_error ? static_cast<int>(*e.resource_error) : -1,
                    static_cast<unsigned>(e.d3d_failure.stage), static_cast<std::uint32_t>(e.d3d_failure.result))});
        }
        if (auto r = absolute_judgement::PrepareAbsoluteJudgementRuntime(plan, image, settings.judgement()); !r)
            return plan_failure(r.error());
        if (auto r = framerate::PrepareFramerateRuntimeBindings(plan, image); !r) return plan_failure(r.error());
        PLOG_INFO << "GameStartup: preflight complete " << game_version::FormatPlanContext(plan.context())
            << " versioned_sites=" << plan.sites().size() << " export_hooks=" << exports->requests().size();

        mutation_started = true;
        auto& registry = hooking::HookRegistry::ProcessLifetime();
        if (auto r = InstallApprovedVersionedPlan(plan, image, registry); !r)
            AbortForStartupError({.installation = std::move(r.error())});
        if (auto r = registry.Install(*exports); !r) AbortForStartupError({.hook = r.error()});
        PLOG_INFO << "NonVersionedHooks: installed requests=" << exports->requests().size();
        auto_play::ActivateAutoPlayMarker(plan);
        test_mode_timing::CompleteTestModeTimingStartup();
        if (settings.windowed_widescreen().enabled()) windowed_widescreen::CompleteWidescreenStartup();
        absolute_judgement::CompleteAbsoluteJudgementStartup(settings.judgement());
        framerate::CompleteFramerateStartup(plan);
        switch_input::ActivateSwitchInput(plan);
        if (!settings.enable_auto_play()) PLOG_INFO << "AutoPlayPatch: state=disabled";
        if (!settings.unlock_all_songs_and_difficulties()) PLOG_INFO << "SongUnlockPatch: state=disabled";
        if (settings.switch_input().style() == input::GameplayInputStyle::Arcade)
            PLOG_INFO << "SwitchInputPatch: requested_style=Arcade active_style=Arcade";
        return {};
    } catch (...) {
        StartupError error{.stage = StartupStage::exception, .feature = "GameStartup"};
        if (mutation_started) AbortForStartupError(error);
        return std::unexpected(std::move(error));
    }
}
}

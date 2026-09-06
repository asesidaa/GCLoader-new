#include "Patches/Framerate/FramerateFeature.h"
#include "Patches/Framerate/FramerateTimingRuntime.h"
#include <Windows.h>
#include <plog/Log.h>
#include <algorithm>

namespace gc::framerate {
using namespace detail;
namespace {
bool TargetRequired(FramerateNativeTarget target, const FramerateHookPlan& hooks) noexcept {
    const auto has = [&](FramerateHookId id) {
        return std::ranges::any_of(hooks.view(), [&](const auto& hook) { return hook.id == id; });
    };
    switch (target) {
    case FramerateNativeTarget::audio_resync_continuation: return has(FramerateHookId::AudioResyncPolicy);
    case FramerateNativeTarget::get_sound_manager:
    case FramerateNativeTarget::get_group_cursor:
    case FramerateNativeTarget::get_config: return has(FramerateHookId::GameplaySongClock);
    case FramerateNativeTarget::advance_gameplay_effect: return has(FramerateHookId::GameplayEffectAdvance);
    case FramerateNativeTarget::ranking_resume: return has(FramerateHookId::RankingEntryCounterStore);
    case FramerateNativeTarget::hitchart_resume: return has(FramerateHookId::HitChartEntryCounterStore);
    case FramerateNativeTarget::unlock_countdown_resume: return has(FramerateHookId::UnlockRewardCountdownStore);
    case FramerateNativeTarget::unlock_primary_resume: return has(FramerateHookId::UnlockRewardPrimaryStateStore);
    case FramerateNativeTarget::unlock_secondary_resume: return has(FramerateHookId::UnlockRewardSecondaryStateStore);
    case FramerateNativeTarget::count: return false;
    }
    return false;
}
}

std::expected<PreparedFrameratePlan, game_version::PlanError> BuildFrameratePlan(
    game_version::GameBuild build, game_version::GameImageVariant variant,
    const FramerateSettings& settings, audio::AudioBackend backend) noexcept {
    using namespace game_version;
    const auto invalid = [](std::string_view site) {
        return std::unexpected(PlanError{.stage = PlanStage::invalid_plan,
            .feature = FeatureId::framerate, .site = site});
    };
    try {
        if (g_runtime) return invalid("runtime_already_prepared");
        const auto* game = ProfileFor(build, variant);
        if (!game) return std::unexpected(PlanError{.stage = PlanStage::unsupported_feature,
            .feature = FeatureId::framerate});
        auto timing = FramerateTimingProfile::Create(settings.target_fps());
        LARGE_INTEGER frequency{};
        if (!timing || !QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0)
            return invalid("timing_profile_or_qpc");
        auto monitor = FramerateMonitor::Create(settings.target_fps(), frequency.QuadPart);
        if (!monitor) return invalid("cadence_monitor");
        GameplayAudioClockPlan audio_plan{};
        switch (backend) {
        case audio::AudioBackend::asio: audio_plan = GameplayAudioClockPlan::AsioQpcSongClock; break;
        case audio::AudioBackend::wasapi_exclusive:
            audio_plan = timing->gameplay_validated() ? GameplayAudioClockPlan::WasapiSharedSongClock
                                                    : GameplayAudioClockPlan::WasapiLegacyResync; break;
        case audio::AudioBackend::directsound: audio_plan = GameplayAudioClockPlan::OriginalWatchdog; break;
        default: return invalid("audio_backend");
        }
        std::optional<GameplaySongClock> song_clock;
        if (detail::UsesSharedGameplaySongClock(audio_plan)) {
            auto clock = GameplaySongClock::Create(settings.target_fps(), 1);
            if (!clock) return invalid("shared_song_clock");
            song_clock.emplace(std::move(*clock));
        }
        g_runtime.emplace(std::move(*timing), std::move(*monitor), frequency.QuadPart,
            audio_plan, std::move(song_clock));
        // Runtime addresses used by replacement operands now have process lifetime.
        g_runtime->game_profile = game;
        g_runtime->layout = game->layout;
        const auto direct = BuildFramerateDirectPatchPlan(*game, g_runtime->profile,
            reinterpret_cast<std::uintptr_t>(g_runtime->profile.target_fps_operand()));
        if (!direct) {
            switch (direct.error()) {
            case FrameratePatchPlanError::ProfileConversion: return invalid("direct_patch_profile_conversion");
            case FrameratePatchPlanError::OperandAddressOutOfRange: return invalid("direct_patch_operand_address_out_of_range");
            case FrameratePatchPlanError::Capacity: return invalid("direct_patch_capacity");
            }
            return invalid("direct_patch_values");
        }
        const auto hooks = BuildFramerateHookPlan(*game, !g_runtime->profile.native_timing(), audio_plan);
        PreparedFrameratePlan plan;
        for (const auto& write : direct->view()) plan.operations[plan.count++] = write;
        for (const auto& hook : hooks.view()) {
            auto bound = BindFramerateHook(hook);
            if (!bound) return std::unexpected(bound.error());
            plan.operations[plan.count++] = std::move(*bound);
        }
        for (const auto& target : game->targets)
            if (TargetRequired(target.id, hooks))
                plan.operations[plan.count++] = ReadOnlyContractOperation{target.site};
        g_runtime->startup_summary = {
            .direct_write_count = direct->count, .hook_count = hooks.count,
            .menu_repeat_initial = direct->menu_repeat_initial,
            .menu_repeat_interval = direct->menu_repeat_interval,
            .authored_frame_milliseconds = g_runtime->authored_frame_operand.frame_milliseconds,
            .effect_timing = game->effect_timing,
        };
        return plan;
    } catch (...) {
        return std::unexpected(PlanError{.stage = PlanStage::allocation,
            .feature = FeatureId::framerate, .site = "runtime_preparation"});
    }
}

std::expected<void, game_version::PlanError> PrepareFramerateRuntimeBindings(
    const game_version::ApprovedVersionedPlan& plan, const runtime_image::RuntimeImage& image) noexcept {
    using namespace game_version;
    bool included{};
    for (const auto& site : plan.sites()) {
        const auto& contract = site.contract();
        if (contract.feature != FeatureId::framerate) continue;
        included = true;
        if (!g_runtime || !g_runtime->game_profile || plan.image_base() != image.base() ||
            plan.image_size() != image.size() ||
            plan.context().build != SelectedBuild{g_runtime->game_profile->build} ||
            plan.context().variant != SelectedVariant{g_runtime->game_profile->variant})
            return std::unexpected(PlanError{.stage = PlanStage::invalid_plan,
                .context = plan.context(), .feature = FeatureId::framerate, .site = "runtime_binding"});
        if (contract.kind != VersionedOperationKind::read_only_contract) continue;
        const auto& targets = g_runtime->game_profile->targets;
        const auto found = std::ranges::find_if(targets,
            [&](const auto& target) { return target.site.site == contract.site && target.site.rva == contract.rva; });
        if (found == targets.end())
            return std::unexpected(PlanError{.stage = PlanStage::invalid_plan,
                .context = plan.context(), .feature = FeatureId::framerate, .site = contract.site});
        const auto address = image.Resolve({"framerate", contract.site, contract.rva}, contract.protected_span);
        if (!address)
            return std::unexpected(PlanError{.stage = PlanStage::address_range,
                .context = plan.context(), .feature = FeatureId::framerate, .site = contract.site,
                .rva = contract.rva, .memory = address.error()});
        if (*address != site.address)
            return std::unexpected(PlanError{.stage = PlanStage::invalid_plan,
                .context = plan.context(), .feature = FeatureId::framerate, .site = contract.site});
        g_runtime->native_targets[static_cast<std::size_t>(found->id)] = *address;
    }
    if (included) {
        const auto hooks = BuildFramerateHookPlan(*g_runtime->game_profile,
            !g_runtime->profile.native_timing(), g_runtime->audio_clock_plan);
        for (const auto& target : g_runtime->game_profile->targets)
            if (TargetRequired(target.id, hooks) && NativeTarget(target.id) == 0)
                return std::unexpected(PlanError{.stage = PlanStage::invalid_plan,
                    .context = plan.context(), .feature = FeatureId::framerate, .site = target.site.site});
    }
    return {};
}

void CompleteFramerateStartup(const game_version::ApprovedVersionedPlan& plan) noexcept {
    const bool included = std::ranges::any_of(plan.sites(), [](const auto& site) {
        return site.contract().feature == game_version::FeatureId::framerate;
    });
    if (!included) return;
    ReportFramerateStartup(g_runtime->profile, g_runtime->startup_summary);
    PLOG_INFO << "FrameratePatch: versioned startup committed direct_writes="
        << g_runtime->startup_summary.direct_write_count << " hooks=" << g_runtime->startup_summary.hook_count;
    PLOG_INFO << "FrameratePatch: menu_timing startup policy=corrected contracts=6 temporary=0";
}
} // namespace gc::framerate

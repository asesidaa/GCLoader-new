#include "Patches/Framerate/EffectTimingHooks.h"
#include "Patches/Framerate/FramerateTimingRuntime.h"
#include "Patches/Framerate/FramerateEffectTiming.h"
#include "Patches/Framerate/FramerateHookTransforms.h"
#include <Windows.h>
#include <plog/Log.h>
#include <atomic>
#include <cstdint>
#include <format>

namespace gc::framerate::detail {
EffectTimingOriginals g_effect_originals;
namespace {
using AdvanceGameplayEffect = void (__thiscall*)(void*);
}
[[nodiscard]] bool ResolveCadenceTestValue(
    std::uint32_t frame,
    std::uint32_t step,
    std::int32_t phase,
    std::uint32_t period,
    std::uint32_t& test_value) noexcept
{
    const auto run = detail::ShouldRunGameplayCadence(
        g_runtime->profile,
        g_runtime->audio_clock_plan,
        frame,
        step,
        phase,
        period);
    if (!run)
    {
        FatalRuntimeConversion("authored gameplay cadence");
        return false;
    }
    test_value = run.value() ? 0U : 1U;
    return true;
}

void ApplyEffectCadence(
    safetyhook::Context& context,
    FramerateHookId hook_id) noexcept
{
    const auto semantics =
        detail::GetGameplayCadenceHookSemantics(hook_id);
    if (!semantics.has_value())
    {
        FatalRuntimeConversion("effect cadence hook semantics");
        return;
    }

    std::uint32_t* test_register{};
    switch (semantics->test_register)
    {
    case detail::GameplayCadenceTestRegister::Eax:
        test_register = &context.eax;
        break;
    case detail::GameplayCadenceTestRegister::Ecx:
        test_register = &context.ecx;
        break;
    case detail::GameplayCadenceTestRegister::Edx:
        test_register = &context.edx;
        break;
    }

    std::uint32_t frame{};
    std::uint32_t step{1};
    const bool readable =
        detail::UsesSharedGameplaySongClock(
            g_runtime->audio_clock_plan)
            ? ReadTuneFrameAndStep(
                context, g_runtime->layout.judgement_tune_stack, frame, step)
            : ReadTuneFrame(context, g_runtime->layout.judgement_tune_stack, frame);
    if (!readable)
    {
        FatalRuntimeConversion("effect cadence tune-frame read");
        return;
    }

    std::int32_t phase{};
    if (semantics->has_signed_phase &&
        !ReadI32StackSafe(context, g_runtime->layout.remote_phase_stack, phase))
    {
        FatalRuntimeConversion("effect cadence phase read");
        return;
    }

    std::uint32_t test_value{};
    if (!ResolveCadenceTestValue(
        frame,
        step,
        phase,
        semantics->authored_period,
        test_value))
    {
        return;
    }
    *test_register = test_value;
    if (test_value == 0)
    {
        g_runtime->counters.effect_cadence_runs.fetch_add(
            1, std::memory_order_relaxed);
    }
    else
    {
        g_runtime->counters.effect_cadence_rejects.fetch_add(
            1, std::memory_order_relaxed);
    }
}

void ApplyRemoteCadence(safetyhook::Context& context) noexcept
{
    const auto frame = ReconstructUnsignedModuloDividend(
        context.eax, context.edx, 4);
    if (!frame)
    {
        FatalRuntimeConversion("remote cadence frame reconstruction");
        return;
    }

    std::uint32_t test_value{};
    if (!ResolveCadenceTestValue(
        frame.value(), 1, 0, 4, test_value))
    {
        return;
    }
    context.edx = test_value;
    if (test_value == 0)
    {
        g_runtime->counters.remote_cadence_runs.fetch_add(
            1, std::memory_order_relaxed);
    }
    else
    {
        g_runtime->counters.remote_cadence_rejects.fetch_add(
            1, std::memory_order_relaxed);
    }
}

void* __fastcall HookNavigatorAdvance(void* self, void*)
{
    if (!IsAuthored60HzTick())
    {
        g_runtime->counters.navigator_skips.fetch_add(
            1, std::memory_order_relaxed);
        return self;
    }
    g_runtime->counters.navigator_advances.fetch_add(
        1, std::memory_order_relaxed);
    return g_effect_originals.navigator_advance(self);
}

void HookGameplayEffectAdvance(safetyhook::Context& context)
{
    std::uint32_t frame{};
    std::uint32_t step{1};
    const bool shared_clock = detail::UsesSharedGameplaySongClock(
        g_runtime->audio_clock_plan);
    const bool readable = shared_clock
                              ? ReadTuneFrameAndStep(
                                  context, g_runtime->layout.semantic_tune_stack, frame, step)
                              : ReadTuneFrame(context, g_runtime->layout.semantic_tune_stack, frame);
    if (!readable)
    {
        FatalRuntimeConversion("gameplay effect tune-frame read");
        return;
    }

    if (shared_clock && step > frame)
    {
        FatalRuntimeConversion("gameplay effect tune-frame underflow");
        return;
    }
    const auto old_tick = shared_clock ? frame - step : frame;
    const auto advance_count =
        detail::CountGameplayEffectAdvances(
            g_runtime->profile,
            g_runtime->audio_clock_plan,
            old_tick,
            step);
    if (!advance_count)
    {
        FatalRuntimeConversion("gameplay effect authored-frame mapping");
        return;
    }

    if (advance_count.value() == 0)
    {
        g_runtime->counters.gameplay_effect_skips.fetch_add(
            1, std::memory_order_relaxed);
        context.eip += g_runtime->layout.effect_advance_skip;
        return;
    }

    g_runtime->counters.gameplay_effect_advances.fetch_add(
        advance_count.value(), std::memory_order_relaxed);
    if (advance_count.value() > 1)
    {
        const auto advance_gameplay_effect =
            reinterpret_cast<AdvanceGameplayEffect>(
                NativeTarget(FramerateNativeTarget::advance_gameplay_effect));
        for (std::uint32_t index = 1;
             index < advance_count.value();
             ++index)
        {
            advance_gameplay_effect(
                reinterpret_cast<void*>(
                    static_cast<std::uintptr_t>(context.ecx)));
        }
    }
}

void HookEffectCadence6(safetyhook::Context& context)
{
    ApplyEffectCadence(context, FramerateHookId::EffectCadence6);
}

void HookEffectCadence5(safetyhook::Context& context)
{
    ApplyEffectCadence(context, FramerateHookId::EffectCadence5);
}

void HookEffectCadence4(safetyhook::Context& context)
{
    ApplyEffectCadence(context, FramerateHookId::EffectCadence4);
}

void HookEffectCadence16A(safetyhook::Context& context)
{
    ApplyEffectCadence(context, FramerateHookId::EffectCadence16A);
}

void HookEffectCadence16B(safetyhook::Context& context)
{
    ApplyEffectCadence(context, FramerateHookId::EffectCadence16B);
}

void HookEffectCadence8(safetyhook::Context& context)
{
    ApplyEffectCadence(context, FramerateHookId::EffectCadence8);
}

void HookRemoteCadenceA(safetyhook::Context& context)
{
    ApplyRemoteCadence(context);
}

void HookRemoteCadenceB(safetyhook::Context& context)
{
    ApplyRemoteCadence(context);
}

void HookGameplayBlink(safetyhook::Context& context)
{
    const auto mapped = MapPositiveTargetFrameToAuthored60(
        g_runtime->profile, context.eax);
    if (!mapped)
    {
        FatalRuntimeConversion("gameplay blink authored-frame mapping");
        return;
    }
    context.eax = mapped.value();
    g_runtime->counters.gameplay_blink_mappings.fetch_add(
        1, std::memory_order_relaxed);
}

void HookAuthoredOperandEax(safetyhook::Context& context)
{
    RedirectEaxToAuthoredOperand(
        context, g_runtime->authored_frame_operand);
    g_runtime->counters.authored_operand_redirects.fetch_add(
        1, std::memory_order_relaxed);
}

void HookAuthoredOperandEcx(safetyhook::Context& context)
{
    RedirectEcxToAuthoredOperand(
        context, g_runtime->authored_frame_operand);
    g_runtime->counters.authored_operand_redirects.fetch_add(
        1, std::memory_order_relaxed);
}

void HookAuthoredOperandEdx(safetyhook::Context& context)
{
    RedirectEdxToAuthoredOperand(
        context, g_runtime->authored_frame_operand);
    g_runtime->counters.authored_operand_redirects.fetch_add(
        1, std::memory_order_relaxed);
}

void HookGameplayCountdownAssetFrame(safetyhook::Context& context)
{
    if (!MapCountdownAssetFrame(context, g_runtime->profile))
    {
        FatalRuntimeConversion(
            "gameplay countdown asset-frame mapping");
        return;
    }
    g_runtime->counters.countdown_asset_mappings.fetch_add(
        1, std::memory_order_relaxed);
}

void HookPlayerPositionInitialization(safetyhook::Context& context)
{
    if (!ScalePlayerPositionDurationEax(context, g_runtime->profile))
    {
        FatalRuntimeConversion(
            "player-position duration initialization");
        return;
    }
    g_runtime->counters.player_position_initializations.fetch_add(
        1, std::memory_order_relaxed);
}

void HookPlayerPositionAssetFrame(safetyhook::Context& context)
{
    std::uint32_t remaining{};
    const std::uint32_t address = context.edx + context.ecx * 4U +
        g_runtime->layout.player_position_remaining;
    if (!ReadU32Safe(address, remaining) || !MapPlayerPositionAssetFrame(
        context, g_runtime->profile, g_runtime->layout, remaining))
    {
        FatalRuntimeConversion(
            "player-position asset-frame mapping");
        return;
    }
    g_runtime->counters.player_position_asset_mappings.fetch_add(
        1, std::memory_order_relaxed);
}

void HookPlayerPositionDenominator(safetyhook::Context& context)
{
    std::uint32_t duration{};
    const std::uint32_t address = context.eax + g_runtime->layout.player_position_duration;
    if (!ReadU32Safe(address, duration) || !PreparePlayerPositionDenominator(
        context, g_runtime->profile,
        g_runtime->player_position_duration_operand, duration))
    {
        FatalRuntimeConversion(
            "player-position denominator scaling");
        return;
    }
    g_runtime->counters.player_position_denominator_redirects.fetch_add(
        1, std::memory_order_relaxed);
}

void HookEffectFlowItemFrame(safetyhook::Context& context)
{
    if (!MapEffectFrameEaxToAuthored60(context, g_runtime->profile))
    {
        FatalRuntimeConversion(
            "effect flow-item authored-frame mapping");
        return;
    }
    g_runtime->counters.effect_flow_item_mappings.fetch_add(
        1, std::memory_order_relaxed);
}

void HookEffectTutorialElapsed(safetyhook::Context& context)
{
    if (!MapEffectFrameEdxToAuthored60(context, g_runtime->profile))
    {
        FatalRuntimeConversion(
            "effect tutorial elapsed authored-frame mapping");
        return;
    }
    g_runtime->counters.effect_tutorial_elapsed_mappings.fetch_add(
        1, std::memory_order_relaxed);
}

void HookEffectChartPreRollDuration(safetyhook::Context& context)
{
    if (!ScaleEffectDurationEaxToTarget(context, g_runtime->profile))
    {
        FatalRuntimeConversion(
            "effect chart pre-roll duration scaling");
        return;
    }
    g_runtime->counters.effect_chart_preroll_scalings.fetch_add(
        1, std::memory_order_relaxed);
}

void HookEffectPlayerModuloDividend(safetyhook::Context& context)
{
    if (!MapEffectFrameEaxToAuthored60(context, g_runtime->profile))
    {
        FatalRuntimeConversion(
            "effect player modulo-dividend authored-frame mapping");
        return;
    }
    g_runtime->counters.effect_player_modulo_mappings.fetch_add(
        1, std::memory_order_relaxed);
}

void LogCadenceValidated(
    const FramerateObservation& observation) noexcept
{
    try
    {
        const auto message = std::format(
            "FrameratePatch: external cap validated target_fps={} "
            "measured_fps={:.6g} relative_error={:.6g} "
            "interval_count={} matching_windows=3",
            observation.target_fps,
            observation.measured_fps,
            observation.relative_error,
            observation.interval_count);
        PLOG_INFO << message;
    }
    catch (...)
    {
    }
}

void UpdateAuthored60HzTick() noexcept
{
    const bool tick = g_runtime->authored_clock.Advance();
    g_runtime->authored_60hz_tick.store(tick, std::memory_order_release);
    auto& counter = tick
                        ? g_runtime->counters.authored_ticks
                        : g_runtime->counters.authored_non_ticks;
    counter.fetch_add(1, std::memory_order_relaxed);
}

void MaybeLogRuntimeStats(std::int64_t now)
{
    if (g_runtime->previous_stats_qpc == 0)
    {
        g_runtime->previous_stats_qpc = now;
        return;
    }
    if (now - g_runtime->previous_stats_qpc <
        g_runtime->qpc_frequency * 5)
    {
        return;
    }
    g_runtime->previous_stats_qpc = now;
    const auto& counters = g_runtime->counters;
    const FramerateEffectRuntimeStats effect_stats{
        .flow_item_mappings = counters.effect_flow_item_mappings.load(
            std::memory_order_relaxed),
        .tutorial_elapsed_mappings =
        counters.effect_tutorial_elapsed_mappings.load(
            std::memory_order_relaxed),
        .chart_preroll_scalings =
        counters.effect_chart_preroll_scalings.load(
            std::memory_order_relaxed),
        .player_modulo_mappings =
        counters.effect_player_modulo_mappings.load(
            std::memory_order_relaxed),
    };
    const auto& menu = g_runtime->menu_counters;
    const FramerateMenuRuntimeStats menu_stats{
        .preprocessing_visits = menu.preprocessing_visits.load(
            std::memory_order_relaxed),
        .preprocessing_forced = menu.preprocessing_forced.load(
            std::memory_order_relaxed),
        .unlock_prompt_transition_holds =
        menu.unlock_prompt_transition_holds.load(
            std::memory_order_relaxed),
        .unlock_prompt_stable_holds =
        menu.unlock_prompt_stable_holds.load(
            std::memory_order_relaxed),
        .ranking_entry = {
            .commits = menu.ranking_entry.commits.load(
                std::memory_order_relaxed),
            .suppressions = menu.ranking_entry.suppressions.load(
                std::memory_order_relaxed),
        },
        .hitchart_entry = {
            .commits = menu.hitchart_entry.commits.load(
                std::memory_order_relaxed),
            .suppressions = menu.hitchart_entry.suppressions.load(
                std::memory_order_relaxed),
        },
        .unlock_countdown = {
            .commits = menu.unlock_countdown.commits.load(
                std::memory_order_relaxed),
            .suppressions = menu.unlock_countdown.suppressions.load(
                std::memory_order_relaxed),
        },
        .unlock_primary = {
            .commits = menu.unlock_primary.commits.load(
                std::memory_order_relaxed),
            .suppressions = menu.unlock_primary.suppressions.load(
                std::memory_order_relaxed),
        },
        .unlock_secondary = {
            .commits = menu.unlock_secondary.commits.load(
                std::memory_order_relaxed),
            .suppressions = menu.unlock_secondary.suppressions.load(
                std::memory_order_relaxed),
        },
    };
    PLOG_INFO << "FrameratePatch: runtime_stats"
        << " target_fps=" << g_runtime->profile.target_fps()
        << " outer=" << counters.outer_calls.load(
            std::memory_order_relaxed)
        << " authored60=" << counters.authored_ticks.load(
            std::memory_order_relaxed)
        << " non60=" << counters.authored_non_ticks.load(
            std::memory_order_relaxed)
        << " movieclip=" << counters.movieclip_calls.load(
            std::memory_order_relaxed)
        << "/skip=" << counters.movieclip_skips.load(
            std::memory_order_relaxed)
        << "/goto=" << counters.movieclip_goto_calls.load(
            std::memory_order_relaxed)
        << " navigator=" << counters.navigator_advances.load(
            std::memory_order_relaxed)
        << "/skip=" << counters.navigator_skips.load(
            std::memory_order_relaxed)
        << " stage_clip=" << counters.stage_clip_indices.load(
            std::memory_order_relaxed)
        << "/mapped=" << counters.stage_clip_mappings.load(
            std::memory_order_relaxed)
        << " ifbl_waits=" << counters.ifbl_wait_stores.load(
            std::memory_order_relaxed)
        << " bgm_preload=" << counters.bgm_preload_calls.load(
            std::memory_order_relaxed)
        << "/skip=" << counters.bgm_preload_skips.load(
            std::memory_order_relaxed)
        << " countdown_cmp_hits="
        << counters.countdown_compare_hits.load(
            std::memory_order_relaxed)
        << " audio_margin_clamps="
        << counters.audio_skip_margin_clamps.load(
            std::memory_order_relaxed)
        << "/interval_conversions="
        << counters.audio_skip_interval_conversions.load(
            std::memory_order_relaxed)
        << " gameplay_song_clock="
        << counters.gameplay_song_clock_epoch_changes.load(
            std::memory_order_relaxed)
        << "/reject="
        << counters.gameplay_song_clock_observation_rejections.load(
            std::memory_order_relaxed)
        << " gameplay_effect="
        << counters.gameplay_effect_advances.load(
            std::memory_order_relaxed)
        << "/skip=" << counters.gameplay_effect_skips.load(
            std::memory_order_relaxed)
        << " effect_cadence="
        << counters.effect_cadence_runs.load(
            std::memory_order_relaxed)
        << "/reject=" << counters.effect_cadence_rejects.load(
            std::memory_order_relaxed)
        << " remote_cadence="
        << counters.remote_cadence_runs.load(
            std::memory_order_relaxed)
        << "/reject=" << counters.remote_cadence_rejects.load(
            std::memory_order_relaxed)
        << " gameplay_blink="
        << counters.gameplay_blink_mappings.load(
            std::memory_order_relaxed)
        << " authored_operands="
        << counters.authored_operand_redirects.load(
            std::memory_order_relaxed)
        << " countdown_asset="
        << counters.countdown_asset_mappings.load(
            std::memory_order_relaxed)
        << " player_position="
        << counters.player_position_initializations.load(
            std::memory_order_relaxed)
        << "/asset="
        << counters.player_position_asset_mappings.load(
            std::memory_order_relaxed)
        << "/denominator="
        << counters.player_position_denominator_redirects.load(
            std::memory_order_relaxed)
        << FormatFramerateEffectRuntimeStats(effect_stats)
        << FormatFramerateMenuRuntimeStats(menu_stats);
}

void HookOuterFrame(safetyhook::Context&)
{
    LARGE_INTEGER now{};
    if (!QueryPerformanceCounter(&now))
    {
        ReportFramerateClockFailure(
            g_runtime->profile.target_fps());
        return;
    }

    if (auto observation = g_runtime->monitor.Observe(now.QuadPart))
    {
        switch (observation->decision)
        {
        case FramerateDecision::Validated:
            LogCadenceValidated(*observation);
            break;
        case FramerateDecision::FatalMismatch:
            ReportFramerateMismatch(
                *observation);
            break;
        case FramerateDecision::FatalClock:
            ReportFramerateClockFailure(
                g_runtime->profile.target_fps());
            break;
        case FramerateDecision::WindowMatch:
        case FramerateDecision::WindowMismatch:
            break;
        }
    }

    g_runtime->counters.outer_calls.fetch_add(
        1, std::memory_order_relaxed);
    if (!g_runtime->profile.native_timing())
    {
        UpdateAuthored60HzTick();
    }
    MaybeLogRuntimeStats(now.QuadPart);
}

}

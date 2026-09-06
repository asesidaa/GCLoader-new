#include "Patches/Framerate/FrameTimingHooks.h"
#include "Patches/Framerate/FramerateTimingRuntime.h"
#include "Patches/Framerate/FramerateHookTransforms.h"
#include "Patches/Framerate/MenuTimingHooks.h"
#include "Audio/AudioContractFatal.h"
#include "Audio/DirectSound/GameplayAudioCursorObservation.h"
#include "Patches/AbsoluteJudgement/AbsoluteJudgementRuntime.h"
#include <Windows.h>
#include <plog/Log.h>
#include <atomic>
#include <cstdint>
#include <expected>
#include <format>
#include <limits>
#include <optional>

namespace gc::framerate::detail {
FrameTimingOriginals g_frame_originals;
namespace {
thread_local int g_movieclip_goto_depth = 0;
constexpr std::int32_t kMinimumAudioSkipMarginMs = 48;
using GetSoundManager = void* (__cdecl*)();
using GetGroupPlayCursorMs = int (__thiscall*)(void*, int);
using GetConfig = void* (__cdecl*)();
}
char __fastcall HookMovieClipGoto(
    void* self,
    void*,
    int frame,
    int subframe)
{
    struct DepthGuard
    {
        DepthGuard() { ++g_movieclip_goto_depth; }
        ~DepthGuard() { --g_movieclip_goto_depth; }
    };

    DepthGuard guard;
    return g_frame_originals.movieclip_goto(self, frame, subframe);
}

char __fastcall HookMovieClipAdvance(
    void* self,
    void*,
    char forward,
    char loop)
{
    const auto context = g_movieclip_goto_depth > 0
                             ? MovieClipAdvanceContext::Goto
                             : g_movieclip_preprocess_depth.active()
                             ? MovieClipAdvanceContext::Preprocess
                             : MovieClipAdvanceContext::Ordinary;
    const bool authored_tick = IsAuthored60HzTick();
    const auto hold_target =
        IdentifyUnlockRewardPromptHold(self, context);
    auto decision =
        DecideMovieClipAdvance(context, authored_tick);

    if (hold_target)
    {
        decision.action =
            MovieClipAdvanceAction::ReturnSuccessWithoutMotion;
        auto& counter =
            hold_target.value() ==
            UnlockRewardPromptTarget::Transition
                ? g_runtime->menu_counters
                           .unlock_prompt_transition_holds
                : g_runtime->menu_counters
                           .unlock_prompt_stable_holds;
        counter.fetch_add(1, std::memory_order_relaxed);
    }
    if (decision.preprocessing_forced)
    {
        g_runtime->menu_counters.preprocessing_forced.fetch_add(
            1, std::memory_order_relaxed);
    }
    if (decision.action ==
        MovieClipAdvanceAction::ReturnSuccessWithoutMotion)
    {
        g_runtime->counters.movieclip_skips.fetch_add(
            1, std::memory_order_relaxed);
        return 1;
    }

    if (context == MovieClipAdvanceContext::Goto)
    {
        g_runtime->counters.movieclip_goto_calls.fetch_add(
            1, std::memory_order_relaxed);
    }
    else
    {
        g_runtime->counters.movieclip_calls.fetch_add(
            1, std::memory_order_relaxed);
    }
    return g_frame_originals.movieclip_advance(self, forward, loop);
}

void HookPaletteCompare(safetyhook::Context& context)
{
    std::uint32_t counter{};
    if (!ReadU32Safe(context.eax + g_runtime->layout.palette_counter, counter))
    {
        ReportFramerateRuntimeFailure(
            "palette counter read failed");
        return;
    }
    context.eflags = ApplyCmp32Flags(
        context.eflags,
        counter,
        g_runtime->profile.palette_frame_cap());
    context.eip += g_runtime->layout.palette_skip;
}

void HookStageClipFrame(safetyhook::Context& context)
{
    g_runtime->counters.stage_clip_indices.fetch_add(
        1, std::memory_order_relaxed);
    const auto mapped = g_runtime->profile.MapToAuthored60(context.ecx);
    if (!mapped)
    {
        FatalRuntimeConversion("stage clip frame mapping");
        return;
    }
    context.ecx = mapped.value();
    g_runtime->counters.stage_clip_mappings.fetch_add(
        1, std::memory_order_relaxed);
}

void HookIfblWait(safetyhook::Context& context)
{
    const auto scaled = ScaleIfblIntegerWait(
        g_runtime->profile, context.ecx);
    if (!scaled)
    {
        FatalRuntimeConversion("IFBL wait scaling");
        return;
    }
    if (WriteU32Safe(context.edx + g_runtime->layout.ifbl_wait, scaled.value()))
    {
        g_runtime->counters.ifbl_wait_stores.fetch_add(
            1, std::memory_order_relaxed);
        context.eip += g_runtime->layout.ifbl_skip;
    }
    else
    {
        FatalRuntimeConversion("IFBL wait store");
    }
}

void HookStageBgmPreload(safetyhook::Context& context)
{
    g_runtime->counters.bgm_preload_calls.fetch_add(
        1, std::memory_order_relaxed);
    if (!IsAuthored60HzTick())
    {
        g_runtime->counters.bgm_preload_skips.fetch_add(
            1, std::memory_order_relaxed);
        context.eip += g_runtime->layout.bgm_preload_skip;
    }
}

void HookTuneCountdownCompare(safetyhook::Context& context)
{
    std::uint32_t countdown{};
    if (!ReadU32Safe(FrameRegister(context, g_runtime->layout.tune_countdown_owner) +
        g_runtime->layout.tune_countdown, countdown))
    {
        FatalRuntimeConversion("countdown compare read");
        return;
    }
    const bool matches =
        countdown == g_runtime->profile.two_second_frames();
    SetZeroFlag(context, matches);
    if (matches)
    {
        g_runtime->counters.countdown_compare_hits.fetch_add(
            1, std::memory_order_relaxed);
    }
    context.eip += g_runtime->layout.countdown_compare_skip;
}

void HookAudioSkipMargin(safetyhook::Context& context)
{
    const auto margin_ms = ReadI32Stack(context, g_runtime->layout.audio_margin_stack);
    if (margin_ms <= 0 || margin_ms >= kMinimumAudioSkipMarginMs)
    {
        return;
    }
    if (WriteU32Safe(
        context.ebp + g_runtime->layout.audio_margin_stack,
        static_cast<std::uint32_t>(kMinimumAudioSkipMarginMs)))
    {
        g_runtime->counters.audio_skip_margin_clamps.fetch_add(
            1, std::memory_order_relaxed);
    }
}

void HookAudioSkipInterval(safetyhook::Context& context)
{
    std::uint32_t raw_interval{};
    if (!ReadU32Safe(context.ecx + g_runtime->layout.audio_interval, raw_interval))
    {
        FatalRuntimeConversion("audio interval read");
        return;
    }

    const auto interval = static_cast<std::int32_t>(raw_interval);
    if (interval <= 0)
    {
        return;
    }
    const auto scaled = g_runtime->profile.ScaleDurationFrames(interval);
    if (!scaled || scaled.value() <= 0)
    {
        FatalRuntimeConversion("audio interval scaling");
        return;
    }

    const auto high = static_cast<std::int64_t>(
        static_cast<std::int32_t>(context.edx));
    const auto dividend = high * (std::int64_t{1} << 32) +
        static_cast<std::uint32_t>(context.eax);
    const auto quotient = dividend / scaled.value();
    const auto remainder = dividend % scaled.value();
    if (quotient < std::numeric_limits<std::int32_t>::min() ||
        quotient > std::numeric_limits<std::int32_t>::max())
    {
        FatalRuntimeConversion("audio interval quotient overflow");
        return;
    }

    context.eax = static_cast<std::uint32_t>(
        static_cast<std::int32_t>(quotient));
    context.edx = static_cast<std::uint32_t>(
        static_cast<std::int32_t>(remainder));
    g_runtime->counters.audio_skip_interval_conversions.fetch_add(
        1, std::memory_order_relaxed);
    context.eip += g_runtime->layout.audio_interval_skip;
}

void HookAudioResyncPolicy(safetyhook::Context& context)
{
    std::int32_t drift_ms{};
    std::int32_t margin_ms{};
    if (!ReadI32StackSafe(context, g_runtime->layout.audio_drift_stack, drift_ms) ||
        !ReadI32StackSafe(context, g_runtime->layout.audio_margin_stack, margin_ms) ||
        margin_ms < 0)
    {
        return;
    }

    const auto abs_drift_ms = drift_ms < 0
                                  ? -static_cast<std::int64_t>(drift_ms)
                                  : static_cast<std::int64_t>(drift_ms);
    const bool suppressed =
        abs_drift_ms <= static_cast<std::int64_t>(margin_ms);
    if (suppressed)
    {
        context.eip = static_cast<std::uint32_t>(
            NativeTarget(FramerateNativeTarget::audio_resync_continuation));
    }
}

void HookGameplaySongClock(safetyhook::Context& context)
{
    context.eip += g_runtime->layout.song_clock_skip;

    if (!detail::UsesSharedGameplaySongClock(
            g_runtime->audio_clock_plan) ||
        !g_runtime->gameplay_song_clock.has_value())
    {
        FatalRuntimeConversion("shared song-clock runtime ownership");
        return;
    }

    const auto tune = static_cast<std::uintptr_t>(context.ecx);
    std::uint32_t current_tick{};
    if (tune == 0 ||
        !ReadU32Safe(
            tune + g_runtime->layout.tune_current_tick, current_tick))
    {
        FatalRuntimeConversion("shared song-clock tune read");
        return;
    }

    int group_cursor_ms = -1;
    std::optional<audio::GameplayAudioCursorObservation>
        cursor_observation;
    {
        audio::ScopedGameplayAudioCursorQuery cursor_query;
        const auto get_sound_manager =
            reinterpret_cast<GetSoundManager>(
                NativeTarget(FramerateNativeTarget::get_sound_manager));
        const auto get_group_play_cursor_ms =
            reinterpret_cast<GetGroupPlayCursorMs>(
                NativeTarget(FramerateNativeTarget::get_group_cursor));
        if (void* const sound_manager = get_sound_manager();
            sound_manager != nullptr)
        {
            group_cursor_ms = get_group_play_cursor_ms(
                sound_manager, g_runtime->layout.gameplay_sound_group);
        }
        cursor_observation = cursor_query.Consume();
    }

    if (g_runtime->audio_clock_plan ==
        GameplayAudioClockPlan::AsioQpcSongClock)
    {
        if (group_cursor_ms < 0 || !cursor_observation.has_value())
        {
            gc::audio::FailAudioContract(
                gc::audio::AudioContractFatalReason::
                LogicalStageClockUnavailable,
                static_cast<std::uint64_t>(
                    static_cast<std::uint32_t>(group_cursor_ms)),
                cursor_observation.has_value() ? 1u : 0u,
                current_tick);
        }
        const auto judgement_seconds =
            gc::absolute_judgement::
            ResolveAsioGameplayTimeForTune(*cursor_observation);
        const auto scaled = judgement_seconds.Multiply(
            g_runtime->profile.target_fps(), 1);
        const auto desired_tick = scaled
                                      ? scaled->Floor()
                                      : std::expected<
                                            std::int64_t,
                                            gc::timing::RationalError>(
                                            std::unexpected(
                                                gc::timing::
                                                RationalError::Overflow));
        if (!scaled || !desired_tick)
        {
            gc::audio::FailAudioContract(
                gc::audio::AudioContractFatalReason::
                LogicalStageClockArithmeticFailure,
                scaled.has_value() ? 0u : 1u,
                desired_tick.has_value() ? 0u : 1u,
                g_runtime->profile.target_fps());
        }
        const auto decision =
            g_runtime->gameplay_song_clock->AdvanceToDesiredTick(
                current_tick, *desired_tick);
        if (!decision)
        {
            gc::audio::FailAudioContract(
                gc::audio::AudioContractFatalReason::
                LogicalStageClockArithmeticFailure,
                static_cast<std::uint64_t>(decision.error()),
                static_cast<std::uint64_t>(*desired_tick),
                current_tick);
        }
        if (!WriteU32Safe(
            tune + g_runtime->layout.tune_step, decision->step))
        {
            FatalRuntimeConversion("ASIO QPC song-clock step write");
        }
        return;
    }

    const auto get_config = reinterpret_cast<GetConfig>(
        NativeTarget(FramerateNativeTarget::get_config));
    void* const config = get_config();
    std::uint32_t game_time_offset_raw{};
    if (config == nullptr ||
        !ReadU32Safe(
            reinterpret_cast<std::uintptr_t>(config) +
            g_runtime->layout.game_time_offset,
            game_time_offset_raw))
    {
        FatalRuntimeConversion("shared song-clock config read");
        return;
    }

    auto selection = detail::ResolveGameplaySongClockStep(
        g_runtime->gameplay_song_clock.value(),
        current_tick,
        static_cast<std::int32_t>(game_time_offset_raw),
        group_cursor_ms,
        cursor_observation);
    if (selection.observation_rejected)
    {
        g_runtime->counters.gameplay_song_clock_observation_rejections
                 .fetch_add(1, std::memory_order_relaxed);
    }
    if (!selection.decision.has_value())
    {
        return;
    }

    if (selection.decision->new_playback_epoch)
    {
        g_runtime->counters.gameplay_song_clock_epoch_changes.fetch_add(
            1, std::memory_order_relaxed);
        const auto& observation = selection.input.observation.value();
        PLOG_INFO << std::format(
            "FrameratePatch: gameplay_song_clock_epoch "
            "buffer_instance_id={} playback_generation={} "
            "source_frame={} output_frame={} current_tick={} "
            "desired_tick={} step={}",
            observation.buffer_instance_id,
            observation.playback_generation,
            observation.position,
            selection.input.output_frame,
            current_tick,
            selection.decision->desired_tick,
            selection.step);
    }

    if (!WriteU32Safe(tune + g_runtime->layout.tune_step, selection.step))
    {
        FatalRuntimeConversion("shared song-clock step write");
    }
}

}

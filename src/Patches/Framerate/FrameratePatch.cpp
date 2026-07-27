#include "Patches/Framerate/FrameratePatch.h"

#include "Audio/Diagnostics/AudioFlightRecorder.h"
#include "Audio/DirectSound/GameplayAudioCursorObservation.h"
#include "Config/config.h"
#include "Patches/Countdown/CountdownTimerFreeze.h"
#include "Patches/Framerate/FramerateAuthoredClock.h"
#include "Patches/Framerate/FramerateDiagnostics.h"
#include "Patches/Framerate/FramerateEffectTiming.h"
#include "Patches/Framerate/FramerateHookTransforms.h"
#include "Patches/Framerate/FramerateMenuTiming.h"
#include "Patches/Framerate/FramerateMonitor.h"
#include "Patches/Framerate/FrameratePatchPlan.h"
#include "Patches/Framerate/FrameratePatchTransaction.h"
#include "Patches/Framerate/FramerateProfile.h"

#include <Windows.h>
#include <plog/Log.h>
#include <safetyhook.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cstdint>
#include <limits>
#include <optional>
#include <sstream>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace gc::framerate {

namespace detail {

GameplaySongClockInputSelection SelectGameplaySongClockInput(
    int group_cursor_ms,
    std::optional<audio::GameplayAudioCursorObservation>
        cursor_observation) noexcept {
    if (group_cursor_ms >= 0) {
        if (cursor_observation.has_value() &&
            cursor_observation->state ==
                audio::GameplayAudioCursorState::Exact) {
            return {
                .state = GameplaySongClockInputState::Exact,
                .observation = SongClockObservation{
                    .kind =
                        SongClockObservationKind::ExactSourceFrame,
                    .position =
                        cursor_observation->source_frame_unwrapped,
                    .source_sample_rate =
                        cursor_observation->source_sample_rate,
                    .playback_generation =
                        cursor_observation->playback_generation,
                },
                .output_frame = cursor_observation->output_frame,
            };
        }
        return {
            .state = GameplaySongClockInputState::Rounded,
            .observation = SongClockObservation{
                .kind =
                    SongClockObservationKind::RoundedMilliseconds,
                .position =
                    static_cast<std::uint64_t>(group_cursor_ms),
            },
        };
    }

    if (cursor_observation.has_value() &&
        cursor_observation->state ==
            audio::GameplayAudioCursorState::Inactive) {
        return {
            .state = GameplaySongClockInputState::Inactive,
        };
    }
    return {
        .state = GameplaySongClockInputState::Failed,
    };
}

GameplaySongClockStepSelection ResolveGameplaySongClockStep(
    GameplaySongClock& clock,
    std::uint32_t current_tick,
    std::int32_t game_time_offset_ms,
    int group_cursor_ms,
    std::optional<audio::GameplayAudioCursorObservation>
        cursor_observation) noexcept {
    GameplaySongClockStepSelection result{
        .input = SelectGameplaySongClockInput(
            group_cursor_ms, cursor_observation),
    };
    if (!result.input.observation.has_value()) {
        return result;
    }

    const auto decision = clock.Observe(
        current_tick,
        game_time_offset_ms,
        result.input.observation.value());
    if (!decision) {
        result.observation_rejected = true;
        return result;
    }

    result.decision = decision.value();
    result.step = decision->step;
    return result;
}

void PublishAudioResyncDiagnostic(
    std::int32_t drift_ms,
    std::int32_t margin_ms,
    bool readable,
    bool suppressed) noexcept {
    LARGE_INTEGER ticks{};
    const auto qpc_ticks =
        QueryPerformanceCounter(&ticks) && ticks.QuadPart >= 0
        ? static_cast<std::uint64_t>(ticks.QuadPart)
        : 0;
    const auto decision = !readable
        ? audio::diagnostics::AudioResyncDecision::Unreadable
        : suppressed
            ? audio::diagnostics::AudioResyncDecision::
                SuppressedInMargin
            : audio::diagnostics::AudioResyncDecision::
                AllowedOutOfMargin;
    audio::diagnostics::PublishActiveAudioDiagnosticEvent({
        .kind =
            audio::diagnostics::AudioDiagnosticEventKind::AudioResync,
        .decision = static_cast<std::uint8_t>(decision),
        .signed_value0 = std::bit_cast<std::uint32_t>(drift_ms),
        .signed_value1 = std::bit_cast<std::uint32_t>(margin_ms),
        .qpc_ticks = qpc_ticks,
    });
}

} // namespace detail

namespace {

constexpr std::int32_t kMinimumAudioSkipMarginMs = 48;
constexpr std::uintptr_t kAudioResyncEpilogueRva = 0x002401D4;
constexpr std::size_t kMaximumMovieClipInstanceNameBytes = 32;
constexpr std::uintptr_t kGetSoundManagerRva = 0x00210400;
constexpr std::uintptr_t kGetGroupPlayCursorMsRva = 0x002122B0;
constexpr std::uintptr_t kGetConfigRva = 0x000011E0;
constexpr int kGameplaySoundGroup = 2;
constexpr std::uintptr_t kTuneCurrentTickOffset = 0x10;
constexpr std::uintptr_t kTuneStepOffset = 0x14;
constexpr std::uintptr_t kGameTimeOffsetOffset = 0x2C;

using GetSoundManager = void* (__cdecl*)();
using GetGroupPlayCursorMs = int (__thiscall*)(void*, int);
using GetConfig = void* (__cdecl*)();

struct FramerateHookStorage {
    safetyhook::InlineHook movieclip_goto{};
    safetyhook::InlineHook movieclip_advance{};
    safetyhook::InlineHook movieclip_preprocess_visit{};
    safetyhook::InlineHook navigator_advance{};
    safetyhook::MidHook palette_compare{};
    safetyhook::MidHook stage_clip_frame{};
    safetyhook::MidHook ifbl_wait{};
    safetyhook::MidHook stage_bgm_preload{};
    safetyhook::MidHook tune_countdown_compare{};
    safetyhook::MidHook audio_skip_margin{};
    safetyhook::MidHook audio_skip_interval{};
    safetyhook::MidHook audio_resync_policy{};
    safetyhook::MidHook gameplay_song_clock{};
    safetyhook::MidHook gameplay_effect_advance{};
    safetyhook::MidHook effect_cadence_6{};
    safetyhook::MidHook effect_cadence_5{};
    safetyhook::MidHook effect_cadence_4{};
    safetyhook::MidHook effect_cadence_16_a{};
    safetyhook::MidHook effect_cadence_16_b{};
    safetyhook::MidHook effect_cadence_8{};
    safetyhook::MidHook remote_cadence_a{};
    safetyhook::MidHook remote_cadence_b{};
    safetyhook::MidHook gameplay_blink{};
    safetyhook::MidHook great_good_lifetime_operand{};
    safetyhook::MidHook great_good_frame_operand{};
    safetyhook::MidHook effect_lifetime_a_operand{};
    safetyhook::MidHook effect_frame_a_operand{};
    safetyhook::MidHook effect_lifetime_b_operand{};
    safetyhook::MidHook effect_frame_b_operand{};
    safetyhook::MidHook direct_effect_frame_operand{};
    safetyhook::MidHook chart_effect_frame_a_operand{};
    safetyhook::MidHook chart_effect_frame_b_operand{};
    safetyhook::MidHook chart_effect_frame_c_operand{};
    safetyhook::MidHook chart_effect_frame_d_operand{};
    safetyhook::MidHook fixed_visual_frame_operand{};
    safetyhook::MidHook gameplay_countdown_asset_frame{};
    safetyhook::MidHook player_position_init_a{};
    safetyhook::MidHook player_position_init_b{};
    safetyhook::MidHook player_position_init_c{};
    safetyhook::MidHook player_position_init_d{};
    safetyhook::MidHook player_position_asset_frame{};
    safetyhook::MidHook player_position_denominator_a{};
    safetyhook::MidHook player_position_denominator_b{};
    safetyhook::MidHook effect_flow_item_frame{};
    safetyhook::MidHook effect_tutorial_elapsed{};
    safetyhook::MidHook effect_chart_preroll_duration{};
    safetyhook::MidHook effect_player_modulo_dividend{};
    safetyhook::MidHook ranking_entry_counter_store{};
    safetyhook::MidHook hitchart_entry_counter_store{};
    safetyhook::MidHook unlock_reward_countdown_store{};
    safetyhook::MidHook unlock_reward_primary_state_store{};
    safetyhook::MidHook unlock_reward_secondary_state_store{};
    safetyhook::MidHook outer_frame{};
};

struct FramerateRuntimeCounters {
    std::atomic_uint64_t outer_calls{0};
    std::atomic_uint64_t authored_ticks{0};
    std::atomic_uint64_t authored_non_ticks{0};
    std::atomic_uint64_t movieclip_calls{0};
    std::atomic_uint64_t movieclip_skips{0};
    std::atomic_uint64_t movieclip_goto_calls{0};
    std::atomic_uint64_t navigator_advances{0};
    std::atomic_uint64_t navigator_skips{0};
    std::atomic_uint64_t stage_clip_indices{0};
    std::atomic_uint64_t stage_clip_mappings{0};
    std::atomic_uint64_t ifbl_wait_stores{0};
    std::atomic_uint64_t bgm_preload_calls{0};
    std::atomic_uint64_t bgm_preload_skips{0};
    std::atomic_uint64_t countdown_compare_hits{0};
    std::atomic_uint64_t audio_skip_margin_clamps{0};
    std::atomic_uint64_t audio_skip_interval_conversions{0};
    std::atomic_uint64_t gameplay_effect_advances{0};
    std::atomic_uint64_t gameplay_effect_skips{0};
    std::atomic_uint64_t effect_cadence_runs{0};
    std::atomic_uint64_t effect_cadence_rejects{0};
    std::atomic_uint64_t remote_cadence_runs{0};
    std::atomic_uint64_t remote_cadence_rejects{0};
    std::atomic_uint64_t gameplay_blink_mappings{0};
    std::atomic_uint64_t authored_operand_redirects{0};
    std::atomic_uint64_t countdown_asset_mappings{0};
    std::atomic_uint64_t player_position_initializations{0};
    std::atomic_uint64_t player_position_asset_mappings{0};
    std::atomic_uint64_t player_position_denominator_redirects{0};
    std::atomic_uint64_t effect_flow_item_mappings{0};
    std::atomic_uint64_t effect_tutorial_elapsed_mappings{0};
    std::atomic_uint64_t effect_chart_preroll_scalings{0};
    std::atomic_uint64_t effect_player_modulo_mappings{0};
};

struct MenuCounterRuntimeCounters {
    std::atomic_uint64_t commits{0};
    std::atomic_uint64_t suppressions{0};
};

struct FramerateMenuRuntimeCounters {
    std::atomic_uint64_t preprocessing_visits{0};
    std::atomic_uint64_t preprocessing_forced{0};
    std::atomic_uint64_t unlock_prompt_transition_holds{0};
    std::atomic_uint64_t unlock_prompt_stable_holds{0};
    MenuCounterRuntimeCounters ranking_entry{};
    MenuCounterRuntimeCounters hitchart_entry{};
    MenuCounterRuntimeCounters unlock_countdown{};
    MenuCounterRuntimeCounters unlock_primary{};
    MenuCounterRuntimeCounters unlock_secondary{};
};

struct FramerateRuntimeState {
    FramerateRuntimeState(
        FramerateProfile profile_value,
        FramerateMonitor monitor_value,
        std::int64_t frequency_value,
        FrameratePlatformActions platform_value,
        GameplayAudioClockPlan audio_clock_plan_value,
        std::optional<GameplaySongClock>
            gameplay_song_clock_value) noexcept
        : profile{std::move(profile_value)},
          monitor{std::move(monitor_value)},
          authored_clock{profile},
          qpc_frequency{frequency_value},
          platform{platform_value},
          transaction{ProductionFramerateMemoryApi()},
          audio_clock_plan{audio_clock_plan_value},
          gameplay_song_clock{
              std::move(gameplay_song_clock_value)} {
    }

    FramerateProfile profile;
    FramerateMonitor monitor;
    Authored60PhaseClock authored_clock;
    std::int64_t qpc_frequency{};
    FrameratePlatformActions platform{};
    FrameratePatchTransaction transaction;
    GameplayAudioClockPlan audio_clock_plan{
        GameplayAudioClockPlan::OriginalWatchdog};
    std::optional<GameplaySongClock> gameplay_song_clock;
    FramerateHookStorage hooks;
    AuthoredFrameOperand authored_frame_operand{};
    PlayerPositionDurationOperand player_position_duration_operand{};
    FramerateRuntimeCounters counters;
    FramerateMenuRuntimeCounters menu_counters;
    std::atomic_bool fatal_published{false};
    std::atomic_bool authored_60hz_tick{true};
    std::int64_t previous_stats_qpc{};
};

struct FramerateHookOperationPlan {
    std::array<HookOperation, kMaximumFramerateHooks> operations{};
    std::size_t count{};

    [[nodiscard]] std::span<const HookOperation> view() const noexcept {
        return {operations.data(), count};
    }
};

std::optional<FramerateRuntimeState> g_runtime;
thread_local int g_movieclip_goto_depth = 0;
thread_local MovieClipPreprocessDepth g_movieclip_preprocess_depth;
char __fastcall HookMovieClipGoto(void*, void*, int, int);
char __fastcall HookMovieClipAdvance(void*, void*, char, char);
void __fastcall HookMovieClipPreprocessVisit(void*, void*, int);
void* __fastcall HookNavigatorAdvance(void*, void*);
void HookPaletteCompare(safetyhook::Context&);
void HookStageClipFrame(safetyhook::Context&);
void HookIfblWait(safetyhook::Context&);
void HookStageBgmPreload(safetyhook::Context&);
void HookTuneCountdownCompare(safetyhook::Context&);
void HookAudioSkipMargin(safetyhook::Context&);
void HookAudioSkipInterval(safetyhook::Context&);
void HookAudioResyncPolicy(safetyhook::Context&);
void HookGameplaySongClock(safetyhook::Context&);
void HookGameplayEffectAdvance(safetyhook::Context&);
void HookEffectCadence6(safetyhook::Context&);
void HookEffectCadence5(safetyhook::Context&);
void HookEffectCadence4(safetyhook::Context&);
void HookEffectCadence16A(safetyhook::Context&);
void HookEffectCadence16B(safetyhook::Context&);
void HookEffectCadence8(safetyhook::Context&);
void HookRemoteCadenceA(safetyhook::Context&);
void HookRemoteCadenceB(safetyhook::Context&);
void HookGameplayBlink(safetyhook::Context&);
void HookAuthoredOperandEax(safetyhook::Context&);
void HookAuthoredOperandEcx(safetyhook::Context&);
void HookAuthoredOperandEdx(safetyhook::Context&);
void HookGameplayCountdownAssetFrame(safetyhook::Context&);
void HookPlayerPositionInitialization(safetyhook::Context&);
void HookPlayerPositionAssetFrame(safetyhook::Context&);
void HookPlayerPositionDenominator(safetyhook::Context&);
void HookEffectFlowItemFrame(safetyhook::Context&);
void HookEffectTutorialElapsed(safetyhook::Context&);
void HookEffectChartPreRollDuration(safetyhook::Context&);
void HookEffectPlayerModuloDividend(safetyhook::Context&);
void HookRankingEntryCounterStore(safetyhook::Context&);
void HookHitChartEntryCounterStore(safetyhook::Context&);
void HookUnlockRewardCountdownStore(safetyhook::Context&);
void HookUnlockRewardPrimaryStateStore(safetyhook::Context&);
void HookUnlockRewardSecondaryStateStore(safetyhook::Context&);
void HookOuterFrame(safetyhook::Context&);
[[nodiscard]] bool IsAuthored60HzTick() noexcept;

[[nodiscard]] std::uintptr_t ExecutableBase() noexcept {
    static const auto base = reinterpret_cast<std::uintptr_t>(
        GetModuleHandleW(nullptr));
    return base;
}

[[nodiscard]] bool ReadU32Safe(
    std::uintptr_t address,
    std::uint32_t& value) noexcept {
    __try {
        value = *reinterpret_cast<volatile std::uint32_t*>(address);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

[[nodiscard]] bool ReadCStringSafe(
    std::uintptr_t address,
    std::span<char> destination,
    std::size_t& length) noexcept {
    length = 0;
    if (address == 0 || destination.empty()) {
        return false;
    }

    __try {
        for (std::size_t index = 0;
             index < destination.size();
             ++index) {
            const char value =
                *reinterpret_cast<const volatile char*>(
                    address + index);
            if (value == '\0') {
                length = index;
                return true;
            }
            destination[index] = value;
        }
        return false;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

[[nodiscard]] bool WriteU32Safe(
    std::uintptr_t address,
    std::uint32_t value) noexcept {
    __try {
        *reinterpret_cast<volatile std::uint32_t*>(address) = value;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

[[nodiscard]] bool ReadI32StackSafe(
    const safetyhook::Context& context,
    std::intptr_t offset,
    std::int32_t& value) noexcept {
    std::uint32_t raw{};
    if (!ReadU32Safe(context.ebp + offset, raw)) {
        return false;
    }
    value = static_cast<std::int32_t>(raw);
    return true;
}

[[nodiscard]] std::int32_t ReadI32Stack(
    const safetyhook::Context& context,
    std::intptr_t offset,
    std::int32_t fallback = 0) noexcept {
    std::int32_t value{};
    if (!ReadI32StackSafe(context, offset, value)) {
        return fallback;
    }
    return value;
}

void SetZeroFlag(safetyhook::Context& context, bool is_zero) noexcept {
    constexpr std::uint32_t kZeroFlag = 0x40;
    if (is_zero) {
        context.eflags |= kZeroFlag;
    } else {
        context.eflags &= ~kZeroFlag;
    }
}

void FatalRuntimeConversion(std::string_view operation) noexcept {
    ReportFramerateRuntimeFailure(
        operation,
        g_runtime->fatal_published,
        g_runtime->platform);
}

template <
    safetyhook::MidHook FramerateHookStorage::*Member,
    safetyhook::MidHookFn Callback,
    std::uintptr_t Rva>
bool InstallMidHook(void* opaque) noexcept {
    auto& state = *static_cast<FramerateRuntimeState*>(opaque);
    auto& hook = state.hooks.*Member;
    hook = safetyhook::create_mid(
        reinterpret_cast<void*>(ExecutableBase() + Rva), Callback);
    return static_cast<bool>(hook);
}

template <
    safetyhook::InlineHook FramerateHookStorage::*Member,
    auto Callback,
    std::uintptr_t Rva>
bool InstallInlineHook(void* opaque) noexcept {
    auto& state = *static_cast<FramerateRuntimeState*>(opaque);
    auto& hook = state.hooks.*Member;
    hook = safetyhook::create_inline(
        reinterpret_cast<void*>(ExecutableBase() + Rva),
        reinterpret_cast<void*>(Callback));
    return static_cast<bool>(hook);
}

template <auto Member>
void ResetOwnedHook(void* opaque) noexcept {
    auto& state = *static_cast<FramerateRuntimeState*>(opaque);
    (state.hooks.*Member).reset();
}

void AssignHookCallbacks(
    FramerateHookId id,
    HookOperation& operation) noexcept {
    switch (id) {
    case FramerateHookId::MovieClipGoto:
        operation.install = &InstallInlineHook<
            &FramerateHookStorage::movieclip_goto,
            HookMovieClipGoto,
            0x000DEA30>;
        operation.reset = &ResetOwnedHook<
            &FramerateHookStorage::movieclip_goto>;
        break;
    case FramerateHookId::MovieClipAdvance:
        operation.install = &InstallInlineHook<
            &FramerateHookStorage::movieclip_advance,
            HookMovieClipAdvance,
            0x000DF940>;
        operation.reset = &ResetOwnedHook<
            &FramerateHookStorage::movieclip_advance>;
        break;
    case FramerateHookId::PaletteCompare:
        operation.install = &InstallMidHook<
            &FramerateHookStorage::palette_compare,
            HookPaletteCompare,
            0x0022BA60>;
        operation.reset = &ResetOwnedHook<
            &FramerateHookStorage::palette_compare>;
        break;
    case FramerateHookId::StageClipFrame:
        operation.install = &InstallMidHook<
            &FramerateHookStorage::stage_clip_frame,
            HookStageClipFrame,
            0x00244054>;
        operation.reset = &ResetOwnedHook<
            &FramerateHookStorage::stage_clip_frame>;
        break;
    case FramerateHookId::IfblWait:
        operation.install = &InstallMidHook<
            &FramerateHookStorage::ifbl_wait,
            HookIfblWait,
            0x002309D4>;
        operation.reset = &ResetOwnedHook<
            &FramerateHookStorage::ifbl_wait>;
        break;
    case FramerateHookId::StageBgmPreload:
        operation.install = &InstallMidHook<
            &FramerateHookStorage::stage_bgm_preload,
            HookStageBgmPreload,
            0x0021001A>;
        operation.reset = &ResetOwnedHook<
            &FramerateHookStorage::stage_bgm_preload>;
        break;
    case FramerateHookId::TuneCountdownCompare:
        operation.install = &InstallMidHook<
            &FramerateHookStorage::tune_countdown_compare,
            HookTuneCountdownCompare,
            0x002648F7>;
        operation.reset = &ResetOwnedHook<
            &FramerateHookStorage::tune_countdown_compare>;
        break;
    case FramerateHookId::AudioSkipMargin:
        operation.install = &InstallMidHook<
            &FramerateHookStorage::audio_skip_margin,
            HookAudioSkipMargin,
            0x0024018F>;
        operation.reset = &ResetOwnedHook<
            &FramerateHookStorage::audio_skip_margin>;
        break;
    case FramerateHookId::AudioSkipInterval:
        operation.install = &InstallMidHook<
            &FramerateHookStorage::audio_skip_interval,
            HookAudioSkipInterval,
            0x002401BD>;
        operation.reset = &ResetOwnedHook<
            &FramerateHookStorage::audio_skip_interval>;
        break;
    case FramerateHookId::AudioResyncPolicy:
        operation.install = &InstallMidHook<
            &FramerateHookStorage::audio_resync_policy,
            HookAudioResyncPolicy,
            0x002401C4>;
        operation.reset = &ResetOwnedHook<
            &FramerateHookStorage::audio_resync_policy>;
        break;
    case FramerateHookId::GameplaySongClock:
        operation.install = &InstallMidHook<
            &FramerateHookStorage::gameplay_song_clock,
            HookGameplaySongClock,
            0x00264DB2>;
        operation.reset = &ResetOwnedHook<
            &FramerateHookStorage::gameplay_song_clock>;
        break;
    case FramerateHookId::GameplayEffectAdvance:
        operation.install = &InstallMidHook<
            &FramerateHookStorage::gameplay_effect_advance,
            HookGameplayEffectAdvance,
            0x00264E2D>;
        operation.reset = &ResetOwnedHook<
            &FramerateHookStorage::gameplay_effect_advance>;
        break;
    case FramerateHookId::EffectCadence6:
        operation.install = &InstallMidHook<
            &FramerateHookStorage::effect_cadence_6,
            HookEffectCadence6,
            0x0024063B>;
        operation.reset = &ResetOwnedHook<
            &FramerateHookStorage::effect_cadence_6>;
        break;
    case FramerateHookId::EffectCadence5:
        operation.install = &InstallMidHook<
            &FramerateHookStorage::effect_cadence_5,
            HookEffectCadence5,
            0x002408D7>;
        operation.reset = &ResetOwnedHook<
            &FramerateHookStorage::effect_cadence_5>;
        break;
    case FramerateHookId::EffectCadence4:
        operation.install = &InstallMidHook<
            &FramerateHookStorage::effect_cadence_4,
            HookEffectCadence4,
            0x00240C9C>;
        operation.reset = &ResetOwnedHook<
            &FramerateHookStorage::effect_cadence_4>;
        break;
    case FramerateHookId::EffectCadence16A:
        operation.install = &InstallMidHook<
            &FramerateHookStorage::effect_cadence_16_a,
            HookEffectCadence16A,
            0x00241213>;
        operation.reset = &ResetOwnedHook<
            &FramerateHookStorage::effect_cadence_16_a>;
        break;
    case FramerateHookId::EffectCadence16B:
        operation.install = &InstallMidHook<
            &FramerateHookStorage::effect_cadence_16_b,
            HookEffectCadence16B,
            0x0024122F>;
        operation.reset = &ResetOwnedHook<
            &FramerateHookStorage::effect_cadence_16_b>;
        break;
    case FramerateHookId::EffectCadence8:
        operation.install = &InstallMidHook<
            &FramerateHookStorage::effect_cadence_8,
            HookEffectCadence8,
            0x00241268>;
        operation.reset = &ResetOwnedHook<
            &FramerateHookStorage::effect_cadence_8>;
        break;
    case FramerateHookId::RemoteCadenceA:
        operation.install = &InstallMidHook<
            &FramerateHookStorage::remote_cadence_a,
            HookRemoteCadenceA,
            0x002632DB>;
        operation.reset = &ResetOwnedHook<
            &FramerateHookStorage::remote_cadence_a>;
        break;
    case FramerateHookId::RemoteCadenceB:
        operation.install = &InstallMidHook<
            &FramerateHookStorage::remote_cadence_b,
            HookRemoteCadenceB,
            0x00263646>;
        operation.reset = &ResetOwnedHook<
            &FramerateHookStorage::remote_cadence_b>;
        break;
    case FramerateHookId::GameplayBlink:
        operation.install = &InstallMidHook<
            &FramerateHookStorage::gameplay_blink,
            HookGameplayBlink,
            0x0024A1B9>;
        operation.reset = &ResetOwnedHook<
            &FramerateHookStorage::gameplay_blink>;
        break;
    case FramerateHookId::GreatGoodLifetimeOperand:
        operation.install = &InstallMidHook<
            &FramerateHookStorage::great_good_lifetime_operand,
            HookAuthoredOperandEax,
            0x002464A8>;
        operation.reset = &ResetOwnedHook<
            &FramerateHookStorage::great_good_lifetime_operand>;
        break;
    case FramerateHookId::GreatGoodFrameOperand:
        operation.install = &InstallMidHook<
            &FramerateHookStorage::great_good_frame_operand,
            HookAuthoredOperandEcx,
            0x00246528>;
        operation.reset = &ResetOwnedHook<
            &FramerateHookStorage::great_good_frame_operand>;
        break;
    case FramerateHookId::EffectLifetimeAOperand:
        operation.install = &InstallMidHook<
            &FramerateHookStorage::effect_lifetime_a_operand,
            HookAuthoredOperandEcx,
            0x00248F00>;
        operation.reset = &ResetOwnedHook<
            &FramerateHookStorage::effect_lifetime_a_operand>;
        break;
    case FramerateHookId::EffectFrameAOperand:
        operation.install = &InstallMidHook<
            &FramerateHookStorage::effect_frame_a_operand,
            HookAuthoredOperandEdx,
            0x00248F8C>;
        operation.reset = &ResetOwnedHook<
            &FramerateHookStorage::effect_frame_a_operand>;
        break;
    case FramerateHookId::EffectLifetimeBOperand:
        operation.install = &InstallMidHook<
            &FramerateHookStorage::effect_lifetime_b_operand,
            HookAuthoredOperandEcx,
            0x0024912B>;
        operation.reset = &ResetOwnedHook<
            &FramerateHookStorage::effect_lifetime_b_operand>;
        break;
    case FramerateHookId::EffectFrameBOperand:
        operation.install = &InstallMidHook<
            &FramerateHookStorage::effect_frame_b_operand,
            HookAuthoredOperandEdx,
            0x002491E0>;
        operation.reset = &ResetOwnedHook<
            &FramerateHookStorage::effect_frame_b_operand>;
        break;
    case FramerateHookId::DirectEffectFrameOperand:
        operation.install = &InstallMidHook<
            &FramerateHookStorage::direct_effect_frame_operand,
            HookAuthoredOperandEdx,
            0x00249C14>;
        operation.reset = &ResetOwnedHook<
            &FramerateHookStorage::direct_effect_frame_operand>;
        break;
    case FramerateHookId::ChartEffectFrameAOperand:
        operation.install = &InstallMidHook<
            &FramerateHookStorage::chart_effect_frame_a_operand,
            HookAuthoredOperandEcx,
            0x0024BC8B>;
        operation.reset = &ResetOwnedHook<
            &FramerateHookStorage::chart_effect_frame_a_operand>;
        break;
    case FramerateHookId::ChartEffectFrameBOperand:
        operation.install = &InstallMidHook<
            &FramerateHookStorage::chart_effect_frame_b_operand,
            HookAuthoredOperandEcx,
            0x0024CC8A>;
        operation.reset = &ResetOwnedHook<
            &FramerateHookStorage::chart_effect_frame_b_operand>;
        break;
    case FramerateHookId::ChartEffectFrameCOperand:
        operation.install = &InstallMidHook<
            &FramerateHookStorage::chart_effect_frame_c_operand,
            HookAuthoredOperandEdx,
            0x0024CCBE>;
        operation.reset = &ResetOwnedHook<
            &FramerateHookStorage::chart_effect_frame_c_operand>;
        break;
    case FramerateHookId::ChartEffectFrameDOperand:
        operation.install = &InstallMidHook<
            &FramerateHookStorage::chart_effect_frame_d_operand,
            HookAuthoredOperandEax,
            0x0024D836>;
        operation.reset = &ResetOwnedHook<
            &FramerateHookStorage::chart_effect_frame_d_operand>;
        break;
    case FramerateHookId::FixedVisualFrameOperand:
        operation.install = &InstallMidHook<
            &FramerateHookStorage::fixed_visual_frame_operand,
            HookAuthoredOperandEcx,
            0x00250AD5>;
        operation.reset = &ResetOwnedHook<
            &FramerateHookStorage::fixed_visual_frame_operand>;
        break;
    case FramerateHookId::GameplayCountdownAssetFrame:
        operation.install = &InstallMidHook<
            &FramerateHookStorage::gameplay_countdown_asset_frame,
            HookGameplayCountdownAssetFrame,
            0x00249A9C>;
        operation.reset = &ResetOwnedHook<
            &FramerateHookStorage::gameplay_countdown_asset_frame>;
        break;
    case FramerateHookId::PlayerPositionInitA:
        operation.install = &InstallMidHook<
            &FramerateHookStorage::player_position_init_a,
            HookPlayerPositionInitialization,
            0x00263240>;
        operation.reset = &ResetOwnedHook<
            &FramerateHookStorage::player_position_init_a>;
        break;
    case FramerateHookId::PlayerPositionInitB:
        operation.install = &InstallMidHook<
            &FramerateHookStorage::player_position_init_b,
            HookPlayerPositionInitialization,
            0x002632B2>;
        operation.reset = &ResetOwnedHook<
            &FramerateHookStorage::player_position_init_b>;
        break;
    case FramerateHookId::PlayerPositionInitC:
        operation.install = &InstallMidHook<
            &FramerateHookStorage::player_position_init_c,
            HookPlayerPositionInitialization,
            0x0026359B>;
        operation.reset = &ResetOwnedHook<
            &FramerateHookStorage::player_position_init_c>;
        break;
    case FramerateHookId::PlayerPositionInitD:
        operation.install = &InstallMidHook<
            &FramerateHookStorage::player_position_init_d,
            HookPlayerPositionInitialization,
            0x00263615>;
        operation.reset = &ResetOwnedHook<
            &FramerateHookStorage::player_position_init_d>;
        break;
    case FramerateHookId::PlayerPositionAssetFrame:
        operation.install = &InstallMidHook<
            &FramerateHookStorage::player_position_asset_frame,
            HookPlayerPositionAssetFrame,
            0x0024EF43>;
        operation.reset = &ResetOwnedHook<
            &FramerateHookStorage::player_position_asset_frame>;
        break;
    case FramerateHookId::PlayerPositionDenominatorA:
        operation.install = &InstallMidHook<
            &FramerateHookStorage::player_position_denominator_a,
            HookPlayerPositionDenominator,
            0x0024F76D>;
        operation.reset = &ResetOwnedHook<
            &FramerateHookStorage::player_position_denominator_a>;
        break;
    case FramerateHookId::PlayerPositionDenominatorB:
        operation.install = &InstallMidHook<
            &FramerateHookStorage::player_position_denominator_b,
            HookPlayerPositionDenominator,
            0x0024FD40>;
        operation.reset = &ResetOwnedHook<
            &FramerateHookStorage::player_position_denominator_b>;
        break;
    case FramerateHookId::EffectFlowItemFrame:
        operation.install = &InstallMidHook<
            &FramerateHookStorage::effect_flow_item_frame,
            HookEffectFlowItemFrame,
            0x001F0310>;
        operation.reset = &ResetOwnedHook<
            &FramerateHookStorage::effect_flow_item_frame>;
        break;
    case FramerateHookId::EffectTutorialElapsed:
        operation.install = &InstallMidHook<
            &FramerateHookStorage::effect_tutorial_elapsed,
            HookEffectTutorialElapsed,
            0x00249593>;
        operation.reset = &ResetOwnedHook<
            &FramerateHookStorage::effect_tutorial_elapsed>;
        break;
    case FramerateHookId::EffectChartPreRollDuration:
        operation.install = &InstallMidHook<
            &FramerateHookStorage::effect_chart_preroll_duration,
            HookEffectChartPreRollDuration,
            0x0024A934>;
        operation.reset = &ResetOwnedHook<
            &FramerateHookStorage::effect_chart_preroll_duration>;
        break;
    case FramerateHookId::EffectPlayerModuloDividend:
        operation.install = &InstallMidHook<
            &FramerateHookStorage::effect_player_modulo_dividend,
            HookEffectPlayerModuloDividend,
            0x0025072E>;
        operation.reset = &ResetOwnedHook<
            &FramerateHookStorage::effect_player_modulo_dividend>;
        break;
    case FramerateHookId::MovieClipPreprocessVisit:
        operation.install = &InstallInlineHook<
            &FramerateHookStorage::movieclip_preprocess_visit,
            HookMovieClipPreprocessVisit,
            0x000EFB90>;
        operation.reset = &ResetOwnedHook<
            &FramerateHookStorage::movieclip_preprocess_visit>;
        break;
    case FramerateHookId::RankingEntryCounterStore:
        operation.install = &InstallMidHook<
            &FramerateHookStorage::ranking_entry_counter_store,
            HookRankingEntryCounterStore,
            kRankingEntryCounterHookGeometry.hook_rva>;
        operation.reset = &ResetOwnedHook<
            &FramerateHookStorage::ranking_entry_counter_store>;
        break;
    case FramerateHookId::HitChartEntryCounterStore:
        operation.install = &InstallMidHook<
            &FramerateHookStorage::hitchart_entry_counter_store,
            HookHitChartEntryCounterStore,
            kHitChartEntryCounterHookGeometry.hook_rva>;
        operation.reset = &ResetOwnedHook<
            &FramerateHookStorage::hitchart_entry_counter_store>;
        break;
    case FramerateHookId::UnlockRewardCountdownStore:
        operation.install = &InstallMidHook<
            &FramerateHookStorage::unlock_reward_countdown_store,
            HookUnlockRewardCountdownStore,
            kUnlockRewardCountdownHookGeometry.hook_rva>;
        operation.reset = &ResetOwnedHook<
            &FramerateHookStorage::unlock_reward_countdown_store>;
        break;
    case FramerateHookId::UnlockRewardPrimaryStateStore:
        operation.install = &InstallMidHook<
            &FramerateHookStorage::unlock_reward_primary_state_store,
            HookUnlockRewardPrimaryStateStore,
            kUnlockRewardPrimaryHookGeometry.hook_rva>;
        operation.reset = &ResetOwnedHook<
            &FramerateHookStorage::unlock_reward_primary_state_store>;
        break;
    case FramerateHookId::UnlockRewardSecondaryStateStore:
        operation.install = &InstallMidHook<
            &FramerateHookStorage::unlock_reward_secondary_state_store,
            HookUnlockRewardSecondaryStateStore,
            kUnlockRewardSecondaryHookGeometry.hook_rva>;
        operation.reset = &ResetOwnedHook<
            &FramerateHookStorage::unlock_reward_secondary_state_store>;
        break;
    case FramerateHookId::NavigatorAdvance:
        operation.install = &InstallInlineHook<
            &FramerateHookStorage::navigator_advance,
            HookNavigatorAdvance,
            0x001B6310>;
        operation.reset = &ResetOwnedHook<
            &FramerateHookStorage::navigator_advance>;
        break;
    case FramerateHookId::OuterFrame:
        operation.install = &InstallMidHook<
            &FramerateHookStorage::outer_frame,
            HookOuterFrame,
            0x00058B70>;
        operation.reset = &ResetOwnedHook<
            &FramerateHookStorage::outer_frame>;
        break;
    }
}

[[nodiscard]] FramerateHookOperationPlan BuildHookOperations(
    std::span<const FramerateHookContract> contracts,
    FramerateRuntimeState& state) noexcept {
    FramerateHookOperationPlan plan{};
    for (const auto& contract : contracts) {
        auto& operation = plan.operations[plan.count++];
        operation.address = ExecutableBase() + contract.rva;
        operation.expected = contract.expected;
        operation.name = contract.name;
        operation.context = &state;
        AssignHookCallbacks(contract.id, operation);
    }
    return plan;
}

[[nodiscard]] bool IsAuthored60HzTick() noexcept {
    return g_runtime->authored_60hz_tick.load(std::memory_order_acquire);
}

enum class UnlockRewardPromptTarget : std::uint8_t {
    Transition,
    Stable,
};

[[nodiscard]] std::optional<UnlockRewardPromptTarget>
IdentifyUnlockRewardPromptHold(
    void* self,
    MovieClipAdvanceContext context) noexcept {
    if (context != MovieClipAdvanceContext::Ordinary) {
        return std::nullopt;
    }

    const auto movieclip = reinterpret_cast<std::uintptr_t>(self);
    std::uint32_t instance_name_hash{};
    if (!ReadU32Safe(
            movieclip + kMovieClipInstanceNameHashOffset,
            instance_name_hash)) {
        return std::nullopt;
    }

    UnlockRewardPromptTarget target{};
    if (instance_name_hash ==
        kUnlockRewardPromptTransitionNameHash) {
        target = UnlockRewardPromptTarget::Transition;
    } else if (instance_name_hash ==
               kUnlockRewardPromptStableNameHash) {
        target = UnlockRewardPromptTarget::Stable;
    } else {
        return std::nullopt;
    }

    std::uint32_t instance_name_address{};
    std::uint32_t owner{};
    if (!ReadU32Safe(
            movieclip + kMovieClipInstanceNameOffset,
            instance_name_address) ||
        instance_name_address == 0 ||
        !ReadU32Safe(
            movieclip + kMovieClipOwnerOffset,
            owner) ||
        owner == 0) {
        return std::nullopt;
    }

    std::array<
        char,
        kMaximumMovieClipInstanceNameBytes> instance_name{};
    std::size_t instance_name_length{};
    if (!ReadCStringSafe(
            instance_name_address,
            instance_name,
            instance_name_length)) {
        return std::nullopt;
    }

    const auto owner_address =
        static_cast<std::uintptr_t>(owner);
    std::uint32_t owner_name_hash{};
    std::uint32_t owner_name_address{};
    if (!ReadU32Safe(
            owner_address + kMovieClipInstanceNameHashOffset,
            owner_name_hash) ||
        !ReadU32Safe(
            owner_address + kMovieClipInstanceNameOffset,
            owner_name_address) ||
        owner_name_address == 0) {
        return std::nullopt;
    }

    std::array<
        char,
        kMaximumMovieClipInstanceNameBytes> owner_name{};
    std::size_t owner_name_length{};
    if (!ReadCStringSafe(
            owner_name_address,
            owner_name,
            owner_name_length)) {
        return std::nullopt;
    }

    std::uint32_t frame_low{};
    std::uint32_t frame_high{};
    std::uint32_t stopped{};
    if (!ReadU32Safe(
            movieclip + kMovieClipCurrentFrameLowOffset,
            frame_low) ||
        !ReadU32Safe(
            movieclip + kMovieClipCurrentFrameHighOffset,
            frame_high) ||
        !ReadU32Safe(
            movieclip + kMovieClipStopFlagOffset,
            stopped)) {
        return std::nullopt;
    }
    const std::uint64_t current_frame =
        (static_cast<std::uint64_t>(frame_high) << 32U) |
        frame_low;

    if (!ShouldHoldUnlockRewardPromptFrame(
            context,
            instance_name_hash,
            std::string_view{
                instance_name.data(),
                instance_name_length},
            owner_name_hash,
            std::string_view{
                owner_name.data(),
                owner_name_length},
            current_frame,
            stopped)) {
        return std::nullopt;
    }
    return target;
}

[[nodiscard]] bool ReadTuneFrame(
    const safetyhook::Context& context,
    std::intptr_t tune_stack_offset,
    std::uint32_t& frame) noexcept {
    std::uint32_t tune{};
    return ReadU32Safe(context.ebp + tune_stack_offset, tune) &&
        tune != 0 && ReadU32Safe(tune + 0x10, frame);
}

[[nodiscard]] bool ResolveCadenceTestValue(
    std::uint32_t frame,
    std::int32_t phase,
    std::uint32_t period,
    std::uint32_t& test_value) noexcept {
    const auto run = ShouldRunAuthored60Cadence(
        g_runtime->profile, frame, phase, period);
    if (!run) {
        FatalRuntimeConversion("authored gameplay cadence");
        return false;
    }
    test_value = run.value() ? 0U : 1U;
    return true;
}

void ApplyEffectCadence(
    safetyhook::Context& context,
    std::uint32_t& test_register,
    std::uint32_t period,
    bool has_phase) noexcept {
    std::uint32_t frame{};
    if (!ReadTuneFrame(context, -0x32C, frame)) {
        FatalRuntimeConversion("effect cadence tune-frame read");
        return;
    }

    std::int32_t phase{};
    if (has_phase && !ReadI32StackSafe(context, -0x1FC, phase)) {
        FatalRuntimeConversion("effect cadence phase read");
        return;
    }

    std::uint32_t test_value{};
    if (!ResolveCadenceTestValue(frame, phase, period, test_value)) {
        return;
    }
    test_register = test_value;
    if (test_value == 0) {
        g_runtime->counters.effect_cadence_runs.fetch_add(
            1, std::memory_order_relaxed);
    } else {
        g_runtime->counters.effect_cadence_rejects.fetch_add(
            1, std::memory_order_relaxed);
    }
}

void ApplyRemoteCadence(safetyhook::Context& context) noexcept {
    const auto frame = ReconstructUnsignedModuloDividend(
        context.eax, context.edx, 4);
    if (!frame) {
        FatalRuntimeConversion("remote cadence frame reconstruction");
        return;
    }

    std::uint32_t test_value{};
    if (!ResolveCadenceTestValue(
            frame.value(), 0, 4, test_value)) {
        return;
    }
    context.edx = test_value;
    if (test_value == 0) {
        g_runtime->counters.remote_cadence_runs.fetch_add(
            1, std::memory_order_relaxed);
    } else {
        g_runtime->counters.remote_cadence_rejects.fetch_add(
            1, std::memory_order_relaxed);
    }
}

char __fastcall HookMovieClipGoto(
    void* self,
    void*,
    int frame,
    int subframe) {
    struct DepthGuard {
        DepthGuard() { ++g_movieclip_goto_depth; }
        ~DepthGuard() { --g_movieclip_goto_depth; }
    };

    DepthGuard guard;
    return g_runtime->hooks.movieclip_goto
        .unsafe_thiscall<char>(self, frame, subframe);
}

void __fastcall HookMovieClipPreprocessVisit(
    void* self,
    void*,
    int traversal_arg) {
    MovieClipPreprocessScope scope{g_movieclip_preprocess_depth};
    g_runtime->menu_counters.preprocessing_visits.fetch_add(
        1, std::memory_order_relaxed);
    g_runtime->hooks.movieclip_preprocess_visit
        .unsafe_thiscall<void>(self, traversal_arg);
}

char __fastcall HookMovieClipAdvance(
    void* self,
    void*,
    char forward,
    char loop) {
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

    if (hold_target) {
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
    if (decision.preprocessing_forced) {
        g_runtime->menu_counters.preprocessing_forced.fetch_add(
            1, std::memory_order_relaxed);
    }
    if (decision.action ==
        MovieClipAdvanceAction::ReturnSuccessWithoutMotion) {
        g_runtime->counters.movieclip_skips.fetch_add(
            1, std::memory_order_relaxed);
        return 1;
    }

    if (context == MovieClipAdvanceContext::Goto) {
        g_runtime->counters.movieclip_goto_calls.fetch_add(
            1, std::memory_order_relaxed);
    } else {
        g_runtime->counters.movieclip_calls.fetch_add(
            1, std::memory_order_relaxed);
    }
    return g_runtime->hooks.movieclip_advance
        .unsafe_thiscall<char>(self, forward, loop);
}

void ApplyPermanentMenuCounterStore(
    safetyhook::Context& context,
    MenuCounterRuntimeCounters& counters,
    std::uintptr_t suppress_resume_rva) noexcept {
    const auto action = ApplyMenuCounterStoreGate(
        context,
        IsAuthored60HzTick(),
        ExecutableBase() + suppress_resume_rva);
    auto& counter =
        action == MenuCounterStoreAction::Commit
        ? counters.commits
        : counters.suppressions;
    counter.fetch_add(1, std::memory_order_relaxed);
}

void HookRankingEntryCounterStore(safetyhook::Context& context) {
    ApplyPermanentMenuCounterStore(
        context,
        g_runtime->menu_counters.ranking_entry,
        kRankingEntryCounterHookGeometry.suppress_resume_rva);
}

void HookHitChartEntryCounterStore(safetyhook::Context& context) {
    ApplyPermanentMenuCounterStore(
        context,
        g_runtime->menu_counters.hitchart_entry,
        kHitChartEntryCounterHookGeometry.suppress_resume_rva);
}

void HookUnlockRewardCountdownStore(safetyhook::Context& context) {
    ApplyPermanentMenuCounterStore(
        context,
        g_runtime->menu_counters.unlock_countdown,
        kUnlockRewardCountdownHookGeometry.suppress_resume_rva);
}

void HookUnlockRewardPrimaryStateStore(
    safetyhook::Context& context) {
    ApplyPermanentMenuCounterStore(
        context,
        g_runtime->menu_counters.unlock_primary,
        kUnlockRewardPrimaryHookGeometry.suppress_resume_rva);
}

void HookUnlockRewardSecondaryStateStore(
    safetyhook::Context& context) {
    ApplyPermanentMenuCounterStore(
        context,
        g_runtime->menu_counters.unlock_secondary,
        kUnlockRewardSecondaryHookGeometry.suppress_resume_rva);
}

void* __fastcall HookNavigatorAdvance(void* self, void*) {
    if (!IsAuthored60HzTick()) {
        g_runtime->counters.navigator_skips.fetch_add(
            1, std::memory_order_relaxed);
        return self;
    }
    g_runtime->counters.navigator_advances.fetch_add(
        1, std::memory_order_relaxed);
    return g_runtime->hooks.navigator_advance.unsafe_thiscall<void*>(self);
}

void HookPaletteCompare(safetyhook::Context& context) {
    std::uint32_t counter{};
    if (!ReadU32Safe(context.eax + 0x0C, counter)) {
        ReportFramerateRuntimeFailure(
            "palette counter read failed",
            g_runtime->fatal_published,
            g_runtime->platform);
        return;
    }
    context.eflags = ApplyCmp32Flags(
        context.eflags,
        counter,
        g_runtime->profile.palette_frame_cap());
    context.eip += 4;
}

void HookStageClipFrame(safetyhook::Context& context) {
    g_runtime->counters.stage_clip_indices.fetch_add(
        1, std::memory_order_relaxed);
    const auto mapped = g_runtime->profile.MapToAuthored60(context.ecx);
    if (!mapped) {
        FatalRuntimeConversion("stage clip frame mapping");
        return;
    }
    context.ecx = mapped.value();
    g_runtime->counters.stage_clip_mappings.fetch_add(
        1, std::memory_order_relaxed);
}

void HookIfblWait(safetyhook::Context& context) {
    const auto scaled = ScaleIfblIntegerWait(
        g_runtime->profile, context.ecx);
    if (!scaled) {
        FatalRuntimeConversion("IFBL wait scaling");
        return;
    }
    if (WriteU32Safe(context.edx + 0x3C, scaled.value())) {
        g_runtime->counters.ifbl_wait_stores.fetch_add(
            1, std::memory_order_relaxed);
        context.eip += 3;
    } else {
        FatalRuntimeConversion("IFBL wait store");
    }
}

void HookStageBgmPreload(safetyhook::Context& context) {
    g_runtime->counters.bgm_preload_calls.fetch_add(
        1, std::memory_order_relaxed);
    if (!IsAuthored60HzTick()) {
        g_runtime->counters.bgm_preload_skips.fetch_add(
            1, std::memory_order_relaxed);
        context.eip += 3;
    }
}

void HookTuneCountdownCompare(safetyhook::Context& context) {
    std::uint32_t countdown{};
    if (!ReadU32Safe(context.edx + 0x1D14, countdown)) {
        FatalRuntimeConversion("countdown compare read");
        return;
    }
    const bool matches =
        countdown == g_runtime->profile.two_second_frames();
    SetZeroFlag(context, matches);
    if (matches) {
        g_runtime->counters.countdown_compare_hits.fetch_add(
            1, std::memory_order_relaxed);
    }
    context.eip += 7;
}

void HookAudioSkipMargin(safetyhook::Context& context) {
    const auto margin_ms = ReadI32Stack(context, -0x24);
    if (margin_ms <= 0 || margin_ms >= kMinimumAudioSkipMarginMs) {
        return;
    }
    if (WriteU32Safe(
            context.ebp - 0x24,
            static_cast<std::uint32_t>(kMinimumAudioSkipMarginMs))) {
        g_runtime->counters.audio_skip_margin_clamps.fetch_add(
            1, std::memory_order_relaxed);
    }
}

void HookAudioSkipInterval(safetyhook::Context& context) {
    std::uint32_t raw_interval{};
    if (!ReadU32Safe(context.ecx + 0x3C, raw_interval)) {
        FatalRuntimeConversion("audio interval read");
        return;
    }

    const auto interval = static_cast<std::int32_t>(raw_interval);
    if (interval <= 0) {
        return;
    }
    const auto scaled = g_runtime->profile.ScaleDurationFrames(interval);
    if (!scaled || scaled.value() <= 0) {
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
        quotient > std::numeric_limits<std::int32_t>::max()) {
        FatalRuntimeConversion("audio interval quotient overflow");
        return;
    }

    context.eax = static_cast<std::uint32_t>(
        static_cast<std::int32_t>(quotient));
    context.edx = static_cast<std::uint32_t>(
        static_cast<std::int32_t>(remainder));
    g_runtime->counters.audio_skip_interval_conversions.fetch_add(
        1, std::memory_order_relaxed);
    context.eip += 3;
}

void HookAudioResyncPolicy(safetyhook::Context& context) {
    std::int32_t drift_ms{};
    std::int32_t margin_ms{};
    if (!ReadI32StackSafe(context, -0x0C, drift_ms) ||
        !ReadI32StackSafe(context, -0x24, margin_ms) ||
        margin_ms < 0) {
        detail::PublishAudioResyncDiagnostic(
            drift_ms, margin_ms, false, false);
        return;
    }

    const auto abs_drift_ms = drift_ms < 0
        ? -static_cast<std::int64_t>(drift_ms)
        : static_cast<std::int64_t>(drift_ms);
    const bool suppressed =
        abs_drift_ms <= static_cast<std::int64_t>(margin_ms);
    detail::PublishAudioResyncDiagnostic(
        drift_ms, margin_ms, true, suppressed);
    if (suppressed) {
        context.eip = static_cast<std::uint32_t>(
            ExecutableBase() + kAudioResyncEpilogueRva);
    }
}

void HookGameplaySongClock(safetyhook::Context& context) {
    context.eip += 5;

    if (g_runtime->audio_clock_plan !=
            GameplayAudioClockPlan::WasapiSharedSongClock ||
        !g_runtime->gameplay_song_clock.has_value()) {
        FatalRuntimeConversion("shared song-clock runtime ownership");
        return;
    }

    const auto tune = static_cast<std::uintptr_t>(context.ecx);
    std::uint32_t current_tick{};
    if (tune == 0 ||
        !ReadU32Safe(
            tune + kTuneCurrentTickOffset, current_tick)) {
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
                ExecutableBase() + kGetSoundManagerRva);
        const auto get_group_play_cursor_ms =
            reinterpret_cast<GetGroupPlayCursorMs>(
                ExecutableBase() + kGetGroupPlayCursorMsRva);
        if (void* const sound_manager = get_sound_manager();
            sound_manager != nullptr) {
            group_cursor_ms = get_group_play_cursor_ms(
                sound_manager, kGameplaySoundGroup);
        }
        cursor_observation = cursor_query.Consume();
    }

    const auto get_config = reinterpret_cast<GetConfig>(
        ExecutableBase() + kGetConfigRva);
    void* const config = get_config();
    std::uint32_t game_time_offset_raw{};
    if (config == nullptr ||
        !ReadU32Safe(
            reinterpret_cast<std::uintptr_t>(config) +
                kGameTimeOffsetOffset,
            game_time_offset_raw)) {
        FatalRuntimeConversion("shared song-clock config read");
        return;
    }

    const auto selection = detail::ResolveGameplaySongClockStep(
        g_runtime->gameplay_song_clock.value(),
        current_tick,
        static_cast<std::int32_t>(game_time_offset_raw),
        group_cursor_ms,
        cursor_observation);
    if (!selection.decision.has_value()) {
        return;
    }

    if (!WriteU32Safe(tune + kTuneStepOffset, selection.step)) {
        FatalRuntimeConversion("shared song-clock step write");
    }
}

void HookGameplayEffectAdvance(safetyhook::Context& context) {
    std::uint32_t frame{};
    if (!ReadTuneFrame(context, -0x2B4, frame)) {
        FatalRuntimeConversion("gameplay effect tune-frame read");
        return;
    }

    const auto boundary =
        IsAuthored60FrameBoundary(g_runtime->profile, frame);
    if (!boundary) {
        FatalRuntimeConversion("gameplay effect authored-frame mapping");
        return;
    }
    if (boundary.value()) {
        g_runtime->counters.gameplay_effect_advances.fetch_add(
            1, std::memory_order_relaxed);
        return;
    }

    g_runtime->counters.gameplay_effect_skips.fetch_add(
        1, std::memory_order_relaxed);
    context.eip += 5;
}

void HookEffectCadence6(safetyhook::Context& context) {
    ApplyEffectCadence(context, context.edx, 6, false);
}

void HookEffectCadence5(safetyhook::Context& context) {
    ApplyEffectCadence(context, context.edx, 5, false);
}

void HookEffectCadence4(safetyhook::Context& context) {
    ApplyEffectCadence(context, context.edx, 4, false);
}

void HookEffectCadence16A(safetyhook::Context& context) {
    ApplyEffectCadence(context, context.edx, 16, true);
}

void HookEffectCadence16B(safetyhook::Context& context) {
    ApplyEffectCadence(context, context.ecx, 16, true);
}

void HookEffectCadence8(safetyhook::Context& context) {
    ApplyEffectCadence(context, context.eax, 8, true);
}

void HookRemoteCadenceA(safetyhook::Context& context) {
    ApplyRemoteCadence(context);
}

void HookRemoteCadenceB(safetyhook::Context& context) {
    ApplyRemoteCadence(context);
}

void HookGameplayBlink(safetyhook::Context& context) {
    const auto mapped = MapPositiveTargetFrameToAuthored60(
        g_runtime->profile, context.eax);
    if (!mapped) {
        FatalRuntimeConversion("gameplay blink authored-frame mapping");
        return;
    }
    context.eax = mapped.value();
    g_runtime->counters.gameplay_blink_mappings.fetch_add(
        1, std::memory_order_relaxed);
}

void HookAuthoredOperandEax(safetyhook::Context& context) {
    RedirectEaxToAuthoredOperand(
        context, g_runtime->authored_frame_operand);
    g_runtime->counters.authored_operand_redirects.fetch_add(
        1, std::memory_order_relaxed);
}

void HookAuthoredOperandEcx(safetyhook::Context& context) {
    RedirectEcxToAuthoredOperand(
        context, g_runtime->authored_frame_operand);
    g_runtime->counters.authored_operand_redirects.fetch_add(
        1, std::memory_order_relaxed);
}

void HookAuthoredOperandEdx(safetyhook::Context& context) {
    RedirectEdxToAuthoredOperand(
        context, g_runtime->authored_frame_operand);
    g_runtime->counters.authored_operand_redirects.fetch_add(
        1, std::memory_order_relaxed);
}

void HookGameplayCountdownAssetFrame(safetyhook::Context& context) {
    if (!MapCountdownAssetFrame(context, g_runtime->profile)) {
        FatalRuntimeConversion(
            "gameplay countdown asset-frame mapping");
        return;
    }
    g_runtime->counters.countdown_asset_mappings.fetch_add(
        1, std::memory_order_relaxed);
}

void HookPlayerPositionInitialization(safetyhook::Context& context) {
    if (!ScalePlayerPositionDurationEax(context, g_runtime->profile)) {
        FatalRuntimeConversion(
            "player-position duration initialization");
        return;
    }
    g_runtime->counters.player_position_initializations.fetch_add(
        1, std::memory_order_relaxed);
}

void HookPlayerPositionAssetFrame(safetyhook::Context& context) {
    if (!MapPlayerPositionAssetFrame(
            context, g_runtime->profile, &ReadU32Safe)) {
        FatalRuntimeConversion(
            "player-position asset-frame mapping");
        return;
    }
    g_runtime->counters.player_position_asset_mappings.fetch_add(
        1, std::memory_order_relaxed);
}

void HookPlayerPositionDenominator(safetyhook::Context& context) {
    if (!PreparePlayerPositionDenominator(
            context,
            g_runtime->profile,
            g_runtime->player_position_duration_operand,
            &ReadU32Safe)) {
        FatalRuntimeConversion(
            "player-position denominator scaling");
        return;
    }
    g_runtime->counters.player_position_denominator_redirects.fetch_add(
        1, std::memory_order_relaxed);
}

void HookEffectFlowItemFrame(safetyhook::Context& context) {
    if (!MapEffectFrameEaxToAuthored60(context, g_runtime->profile)) {
        FatalRuntimeConversion(
            "effect flow-item authored-frame mapping");
        return;
    }
    g_runtime->counters.effect_flow_item_mappings.fetch_add(
        1, std::memory_order_relaxed);
}

void HookEffectTutorialElapsed(safetyhook::Context& context) {
    if (!MapEffectFrameEdxToAuthored60(context, g_runtime->profile)) {
        FatalRuntimeConversion(
            "effect tutorial elapsed authored-frame mapping");
        return;
    }
    g_runtime->counters.effect_tutorial_elapsed_mappings.fetch_add(
        1, std::memory_order_relaxed);
}

void HookEffectChartPreRollDuration(safetyhook::Context& context) {
    if (!ScaleEffectDurationEaxToTarget(context, g_runtime->profile)) {
        FatalRuntimeConversion(
            "effect chart pre-roll duration scaling");
        return;
    }
    g_runtime->counters.effect_chart_preroll_scalings.fetch_add(
        1, std::memory_order_relaxed);
}

void HookEffectPlayerModuloDividend(safetyhook::Context& context) {
    if (!MapEffectFrameEaxToAuthored60(context, g_runtime->profile)) {
        FatalRuntimeConversion(
            "effect player modulo-dividend authored-frame mapping");
        return;
    }
    g_runtime->counters.effect_player_modulo_mappings.fetch_add(
        1, std::memory_order_relaxed);
}

void LogCadenceValidated(
    const FramerateObservation& observation) noexcept {
    try {
        std::ostringstream stream;
        stream << "FrameratePatch: external cap validated"
               << " target_fps=" << observation.target_fps
               << " measured_fps=" << observation.measured_fps
               << " relative_error=" << observation.relative_error
               << " interval_count=" << observation.interval_count
               << " matching_windows=3";
        if (g_runtime->platform.log_info != nullptr) {
            g_runtime->platform.log_info(stream.str().c_str());
        }
    } catch (...) {
    }
}

void UpdateAuthored60HzTick() noexcept {
    const bool tick = g_runtime->authored_clock.Advance();
    g_runtime->authored_60hz_tick.store(tick, std::memory_order_release);
    auto& counter = tick
        ? g_runtime->counters.authored_ticks
        : g_runtime->counters.authored_non_ticks;
    counter.fetch_add(1, std::memory_order_relaxed);
}

void MaybeLogRuntimeStats(std::int64_t now) {
    if (g_runtime->previous_stats_qpc == 0) {
        g_runtime->previous_stats_qpc = now;
        return;
    }
    if (now - g_runtime->previous_stats_qpc <
        g_runtime->qpc_frequency * 5) {
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

void HookOuterFrame(safetyhook::Context&) {
    LARGE_INTEGER now{};
    if (!QueryPerformanceCounter(&now)) {
        ReportFramerateClockFailure(
            g_runtime->profile.target_fps(),
            g_runtime->fatal_published,
            g_runtime->platform);
        return;
    }

    if (auto observation = g_runtime->monitor.Observe(now.QuadPart)) {
        switch (observation->decision) {
        case FramerateDecision::Validated:
            LogCadenceValidated(*observation);
            break;
        case FramerateDecision::FatalMismatch:
            ReportFramerateMismatch(
                *observation,
                g_runtime->fatal_published,
                g_runtime->platform);
            break;
        case FramerateDecision::FatalClock:
            ReportFramerateClockFailure(
                g_runtime->profile.target_fps(),
                g_runtime->fatal_published,
                g_runtime->platform);
            break;
        case FramerateDecision::WindowMatch:
        case FramerateDecision::WindowMismatch:
            break;
        }
    }

    g_runtime->counters.outer_calls.fetch_add(
        1, std::memory_order_relaxed);
    if (g_runtime->profile.native_timing()) {
        return;
    }
    UpdateAuthored60HzTick();
    MaybeLogRuntimeStats(now.QuadPart);
}

[[nodiscard]] const char* PatchPlanErrorName(
    FrameratePatchPlanError error) noexcept {
    switch (error) {
    case FrameratePatchPlanError::ProfileConversion:
        return "profile_conversion";
    case FrameratePatchPlanError::OperandAddressOutOfRange:
        return "operand_address_out_of_range";
    case FrameratePatchPlanError::UnexpectedImageBase:
        return "unexpected_image_base";
    case FrameratePatchPlanError::Capacity:
        return "capacity";
    }
    return "unknown";
}

[[nodiscard]] const char* InstallStageName(
    FramerateInstallStage stage) noexcept {
    switch (stage) {
    case FramerateInstallStage::None:
        return "none";
    case FramerateInstallStage::Capacity:
        return "capacity";
    case FramerateInstallStage::InvalidDescriptor:
        return "invalid_descriptor";
    case FramerateInstallStage::PreflightRead:
        return "preflight_read";
    case FramerateInstallStage::PreflightMismatch:
        return "preflight_mismatch";
    case FramerateInstallStage::DirectWrite:
        return "direct_write";
    case FramerateInstallStage::HookInstall:
        return "hook_install";
    }
    return "unknown";
}

void FatalInstallPlanFailure(FrameratePatchPlanError error) noexcept {
    std::ostringstream detail;
    detail << "direct patch plan error=" << PatchPlanErrorName(error)
           << "; executable memory was not changed";
    ReportFramerateInitializationFailure(
        detail.str(),
        g_runtime->fatal_published,
        g_runtime->platform);
}

void FatalTransactionFailure(
    const FramerateInstallError& error) noexcept {
    std::ostringstream detail;
    detail << "transaction stage=" << InstallStageName(error.stage)
           << " name="
           << (error.operation_name != nullptr
                   ? error.operation_name
                   : "<unnamed>")
           << " index=" << error.operation_index
           << " rollback_attempted="
           << (error.rollback_attempted ? "true" : "false")
           << " rollback_complete="
           << (error.rollback_complete ? "true" : "false");
    PLOG_ERROR << "FrameratePatch: " << detail.str();
    ReportFramerateInitializationFailure(
        detail.str(),
        g_runtime->fatal_published,
        g_runtime->platform);
}

} // namespace

bool FramerateHookHasRuntimeBinding(FramerateHookId id) noexcept {
    HookOperation operation{};
    AssignHookCallbacks(id, operation);
    return operation.install != nullptr && operation.reset != nullptr;
}

bool FrameratePatchInit(bool wasapi_audio_committed) {
    static std::atomic_bool initialized{false};
    bool expected = false;
    if (!initialized.compare_exchange_strong(expected, true)) {
        return g_runtime.has_value() &&
            g_runtime->transaction.committed();
    }

    const auto actions = ProductionFrameratePlatformActions();
    const auto target = ConfigManager::instance().GetTargetFps();
    auto profile_result = FramerateProfile::Create(target);

    LARGE_INTEGER frequency{};
    if (!profile_result || !QueryPerformanceFrequency(&frequency) ||
        frequency.QuadPart <= 0) {
        static std::atomic_bool startup_fatal{false};
        ReportFramerateInitializationFailure(
            "profile or QPC preflight failed; executable memory was not changed",
            startup_fatal,
            actions);
        return false;
    }

    auto monitor_result = FramerateMonitor::Create(
        target, frequency.QuadPart);
    if (!monitor_result) {
        static std::atomic_bool startup_fatal{false};
        ReportFramerateInitializationFailure(
            "cadence monitor preflight failed; executable memory was not changed",
            startup_fatal,
            actions);
        return false;
    }

    const auto audio_clock_plan =
        !wasapi_audio_committed
        ? GameplayAudioClockPlan::OriginalWatchdog
        : profile_result->gameplay_validated()
            ? GameplayAudioClockPlan::WasapiSharedSongClock
            : GameplayAudioClockPlan::WasapiLegacyResync;
    std::optional<GameplaySongClock> gameplay_song_clock;
    if (audio_clock_plan ==
        GameplayAudioClockPlan::WasapiSharedSongClock) {
        auto clock_result = GameplaySongClock::Create(target, 1);
        if (!clock_result) {
            static std::atomic_bool startup_fatal{false};
            ReportFramerateInitializationFailure(
                "shared song-clock preflight failed; executable memory was not changed",
                startup_fatal,
                actions);
            return false;
        }
        gameplay_song_clock.emplace(
            std::move(clock_result.value()));
    }

    g_runtime.emplace(
        std::move(profile_result.value()),
        std::move(monitor_result.value()),
        frequency.QuadPart,
        actions,
        audio_clock_plan,
        std::move(gameplay_song_clock));

    const auto direct_plan = BuildFramerateDirectPatchPlan(
        ExecutableBase(),
        g_runtime->profile,
        reinterpret_cast<std::uintptr_t>(
            g_runtime->profile.target_fps_operand()));
    if (!direct_plan) {
        FatalInstallPlanFailure(direct_plan.error());
        return false;
    }

    const auto hook_plan = BuildFramerateHookPlan(
        !g_runtime->profile.native_timing(),
        g_runtime->audio_clock_plan);
    const auto hook_operations = BuildHookOperations(
        hook_plan.view(), *g_runtime);

    ReportFramerateStartup(
        g_runtime->profile,
        FramerateStartupPatchSummary{
            .direct_write_count = direct_plan->view().size(),
            .hook_count = hook_operations.view().size(),
            .menu_repeat_initial = direct_plan->menu_repeat_initial,
            .menu_repeat_interval = direct_plan->menu_repeat_interval,
            .authored_frame_milliseconds =
                g_runtime->authored_frame_operand.frame_milliseconds,
            .effect_timing = SummarizeEffectTimingManifest(),
        },
        actions);

    PLOG_INFO
        << "FrameratePatch: menu_timing startup"
        << " policy=corrected"
        << " contracts=6 temporary=0";

    const auto installed = g_runtime->transaction.Install(
        direct_plan->view(), hook_operations.view());
    if (!installed) {
        FatalTransactionFailure(installed.error());
        return false;
    }

    PLOG_INFO << "FrameratePatch: transaction committed"
              << " direct_writes=" << direct_plan->view().size()
              << " hooks=" << hook_operations.view().size();

    gc::timer_freeze::SetCountdownTimerFreezeEnabled(
        ConfigManager::instance().GetEnableTimerFreezePatches());
    gc::timer_freeze::CountdownTimerFreezeInit();
    return true;
}

} // namespace gc::framerate

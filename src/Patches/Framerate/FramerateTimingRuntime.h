#pragma once
#include "Patches/Framerate/FramerateFeature.h"
#include "Patches/Framerate/FramerateAuthoredClock.h"
#include "Patches/Framerate/FramerateMonitor.h"
#include "Patches/Framerate/FramerateDiagnostics.h"
#include "Patches/Framerate/FramerateGameProfile.h"
#include "Patches/Framerate/FramerateMenuTiming.h"
#include <atomic>
#include <span>

namespace gc::framerate {
    namespace detail
    {
        enum class GameplaySongClockInputState : std::uint8_t
        {
            Exact,
            Rounded,
            Inactive,
            Failed,
        };

        enum class GameplayCadenceTestRegister : std::uint8_t
        {
            Eax,
            Ecx,
            Edx,
        };

        struct GameplayCadenceHookSemantics
        {
            std::uint32_t authored_period{};
            GameplayCadenceTestRegister test_register{};
            bool has_signed_phase{};
        };

        struct GameplaySongClockInputSelection
        {
            GameplaySongClockInputState state{
                GameplaySongClockInputState::Failed
            };
            std::optional<SongClockObservation> observation{};
            std::uint64_t output_frame{};
        };

        struct GameplaySongClockStepSelection
        {
            GameplaySongClockInputSelection input{};
            std::optional<GameplaySongClockDecision> decision{};
            std::uint32_t step{1};
            bool observation_rejected{};
        };

        [[nodiscard]] GameplaySongClockInputSelection
        SelectGameplaySongClockInput(
            int group_cursor_ms,
            const std::optional<audio::GameplayAudioCursorObservation>&
            cursor_observation) noexcept;

        [[nodiscard]] GameplaySongClockStepSelection
        ResolveGameplaySongClockStep(
            GameplaySongClock& clock,
            std::uint32_t current_tick,
            std::int32_t game_time_offset_ms,
            int group_cursor_ms,
            const std::optional<audio::GameplayAudioCursorObservation>&
            cursor_observation) noexcept;

        [[nodiscard]] std::expected<bool, FramerateTimingProfileError>
        ShouldRunGameplayCadence(
            const FramerateTimingProfile& profile,
            GameplayAudioClockPlan audio_clock_plan,
            std::uint32_t current_tick,
            std::uint32_t step,
            std::int32_t phase,
            std::uint32_t authored_period) noexcept;

        [[nodiscard]] std::expected<std::uint32_t, FramerateTimingProfileError>
        CountGameplayEffectAdvances(
            const FramerateTimingProfile& profile,
            GameplayAudioClockPlan audio_clock_plan,
            std::uint32_t current_tick,
            std::uint32_t step) noexcept;

        [[nodiscard]] std::optional<GameplayCadenceHookSemantics>
        GetGameplayCadenceHookSemantics(
            FramerateHookId id) noexcept;
    } // namespace detail

namespace detail {
[[nodiscard]] bool UsesSharedGameplaySongClock(GameplayAudioClockPlan) noexcept;
struct FramerateRuntimeCounters
{
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
    std::atomic_uint64_t gameplay_song_clock_epoch_changes{0};
    std::atomic_uint64_t gameplay_song_clock_observation_rejections{0};
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

struct MenuCounterRuntimeCounters
{
    std::atomic_uint64_t commits{0};
    std::atomic_uint64_t suppressions{0};
};

struct FramerateMenuRuntimeCounters
{
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

struct FramerateRuntimeState
{
    FramerateRuntimeState(
        FramerateTimingProfile profile_value,
        FramerateMonitor monitor_value,
        std::int64_t frequency_value,
        GameplayAudioClockPlan audio_clock_plan_value,
        std::optional<GameplaySongClock>
        gameplay_song_clock_value) noexcept
        : profile{std::move(profile_value)},
          monitor{std::move(monitor_value)},
          authored_clock{profile},
          qpc_frequency{frequency_value},
          audio_clock_plan{audio_clock_plan_value},
          gameplay_song_clock{
              std::move(gameplay_song_clock_value)
          }
    {
    }

    FramerateTimingProfile profile;
    FramerateMonitor monitor;
    Authored60PhaseClock authored_clock;
    std::int64_t qpc_frequency{};
    GameplayAudioClockPlan audio_clock_plan{
        GameplayAudioClockPlan::OriginalWatchdog
    };
    std::optional<GameplaySongClock> gameplay_song_clock;
    FramerateNativeLayout layout{};
    std::array<std::uintptr_t, 10> native_targets{};
    const FramerateGameProfile* game_profile{};
    FramerateStartupPatchSummary startup_summary{};
    AuthoredFrameOperand authored_frame_operand{};
    PlayerPositionDurationOperand player_position_duration_operand{};
    FramerateRuntimeCounters counters;
    FramerateMenuRuntimeCounters menu_counters;
    std::atomic_bool authored_60hz_tick{true};
    std::int64_t previous_stats_qpc{};
};

extern std::optional<FramerateRuntimeState> g_runtime;
[[nodiscard]] std::uintptr_t NativeTarget(FramerateNativeTarget id) noexcept;
[[nodiscard]] bool ReadU32Safe(
    std::uintptr_t address,
    std::uint32_t& value) noexcept;
[[nodiscard]] bool WriteU32Safe(
    std::uintptr_t address,
    std::uint32_t value) noexcept;
[[nodiscard]] bool ReadI32StackSafe(
    const safetyhook::Context& context,
    std::intptr_t offset,
    std::int32_t& value) noexcept;
[[nodiscard]] std::int32_t ReadI32Stack(
    const safetyhook::Context& context,
    std::intptr_t offset,
    std::int32_t fallback = 0) noexcept;
void SetZeroFlag(safetyhook::Context& context, bool is_zero) noexcept;
void FatalRuntimeConversion(std::string_view operation) noexcept;
[[nodiscard]] bool IsAuthored60HzTick() noexcept;
[[nodiscard]] bool ReadTuneFrame(
    const safetyhook::Context& context,
    std::intptr_t tune_stack_offset,
    std::uint32_t& frame) noexcept;
[[nodiscard]] bool ReadTuneFrameAndStep(
    const safetyhook::Context& context,
    std::intptr_t tune_stack_offset,
    std::uint32_t& frame,
    std::uint32_t& step) noexcept;
}
}

#pragma once
#include <cstddef>
#include <cstdint>

namespace gc::framerate {
enum class FramerateHookId {
    MovieClipGoto,
    MovieClipAdvance,
    PaletteCompare,
    StageClipFrame,
    IfblWait,
    StageBgmPreload,
    TuneCountdownCompare,
    AudioSkipMargin,
    AudioSkipInterval,
    AudioResyncPolicy,
    GameplaySongClock,
    GameplayEffectAdvance,
    EffectCadence6,
    EffectCadence5,
    EffectCadence4,
    EffectCadence16A,
    EffectCadence16B,
    EffectCadence8,
    RemoteCadenceA,
    RemoteCadenceB,
    GameplayBlink,
    GreatGoodLifetimeOperand,
    GreatGoodFrameOperand,
    EffectLifetimeAOperand,
    EffectFrameAOperand,
    EffectLifetimeBOperand,
    EffectFrameBOperand,
    DirectEffectFrameOperand,
    ChartEffectFrameAOperand,
    ChartEffectFrameBOperand,
    ChartEffectFrameCOperand,
    ChartEffectFrameDOperand,
    FixedVisualFrameOperand,
    GameplayCountdownAssetFrame,
    PlayerPositionInitA,
    PlayerPositionInitB,
    PlayerPositionInitC,
    PlayerPositionInitD,
    PlayerPositionAssetFrame,
    PlayerPositionDenominatorA,
    PlayerPositionDenominatorB,
    EffectFlowItemFrame,
    EffectTutorialElapsed,
    EffectChartPreRollDuration,
    EffectPlayerModuloDividend,
    MovieClipPreprocessVisit,
    RankingEntryCounterStore,
    HitChartEntryCounterStore,
    UnlockRewardCountdownStore,
    UnlockRewardPrimaryStateStore,
    UnlockRewardSecondaryStateStore,
    NavigatorAdvance,
    OuterFrame,
};

enum class GameplayAudioClockPlan : std::uint8_t {
    OriginalWatchdog,
    WasapiLegacyResync,
    WasapiSharedSongClock,
    AsioQpcSongClock,
};


enum class FramerateNativeTarget {
    audio_resync_epilogue,
    get_sound_manager,
    get_group_cursor,
    get_config,
    advance_gameplay_effect,
    ranking_resume,
    hitchart_resume,
    unlock_countdown_resume,
    unlock_primary_resume,
    unlock_secondary_resume,
    count
};
struct FramerateNativeLayout final {
    std::size_t tune_current_tick{};
    std::size_t tune_step{};
    std::size_t game_time_offset{};
    int gameplay_sound_group{};
    std::ptrdiff_t judgement_tune_stack{};
    std::ptrdiff_t semantic_tune_stack{};
    std::ptrdiff_t remote_phase_stack{};
    std::size_t palette_counter{};
    std::size_t ifbl_wait{};
    std::size_t tune_countdown{};
    std::ptrdiff_t audio_margin_stack{};
    std::ptrdiff_t audio_drift_stack{};
    std::size_t audio_interval{};
    std::size_t movieclip_stop_flag{};
    std::size_t movieclip_instance_name{};
    std::size_t movieclip_instance_hash{};
    std::size_t movieclip_owner{};
    std::size_t movieclip_frame_low{};
    std::size_t movieclip_frame_high{};
    std::size_t player_position_remaining{};
    std::size_t player_position_duration{};
    std::uint32_t palette_skip{};
    std::uint32_t ifbl_skip{};
    std::uint32_t bgm_preload_skip{};
    std::uint32_t countdown_compare_skip{};
    std::uint32_t audio_interval_skip{};
    std::uint32_t song_clock_skip{};
    std::uint32_t effect_advance_skip{};
    std::uint32_t player_position_skip{};
};
using MovieClipGotoFn = char(__thiscall*)(void*, int, int);
using MovieClipAdvanceFn = char(__thiscall*)(void*, char, char);
using MovieClipPreprocessFn = void(__thiscall*)(void*, int);
using NavigatorAdvanceFn = void*(__thiscall*)(void*);
}

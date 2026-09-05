#include "Patches/Framerate/FrameratePatch.h"

#include "Diagnostics/FatalProcess.h"
#include "Audio/AudioContractFatal.h"
#include "Audio/DirectSound/GameplayAudioCursorObservation.h"
#include "Patches/AbsoluteJudgement/AbsoluteJudgementRuntime.h"
#include "Patches/Framerate/FramerateAuthoredClock.h"
#include "Patches/Framerate/FramerateDiagnostics.h"
#include "Patches/Framerate/FramerateEffectTiming.h"
#include "Patches/Framerate/FramerateHookTransforms.h"
#include "Patches/Framerate/FramerateMenuTiming.h"
#include "Patches/Framerate/FramerateMonitor.h"
#include "Patches/Framerate/FrameratePatchPlan.h"
#include "Patches/Framerate/FramerateTimingProfile.h"

#include <Windows.h>
#include <plog/Log.h>
#include <safetyhook.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <format>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace gc::framerate
{
    namespace detail
    {
        [[nodiscard]] bool UsesSharedGameplaySongClock(
            const GameplayAudioClockPlan plan) noexcept
        {
            return plan == GameplayAudioClockPlan::WasapiSharedSongClock ||
                plan == GameplayAudioClockPlan::AsioQpcSongClock;
        }

        GameplaySongClockInputSelection SelectGameplaySongClockInput(
            int group_cursor_ms,
            const std::optional<audio::GameplayAudioCursorObservation>&
            cursor_observation) noexcept
        {
            const auto* presented = cursor_observation.has_value()
                                        ? std::get_if<
                                              audio::
                                              PresentedOutputCursorObservation>(
                                              &cursor_observation->payload)
                                        : nullptr;
            if (presented != nullptr &&
                presented->state ==
                audio::GameplayAudioCursorState::Inactive)
            {
                return {
                    .state = GameplaySongClockInputState::Inactive,
                };
            }

            if (group_cursor_ms >= 0)
            {
                if (presented != nullptr &&
                    presented->state ==
                    audio::GameplayAudioCursorState::Exact)
                {
                    return {
                        .state = GameplaySongClockInputState::Exact,
                        .observation = SongClockObservation{
                            .kind =
                            SongClockObservationKind::ExactSourceFrame,
                            .position =
                            presented->source_frame_unwrapped,
                            .source_sample_rate =
                            presented->source_sample_rate,
                            .buffer_instance_id =
                            presented->buffer_instance_id,
                            .playback_generation =
                            presented->playback_generation,
                        },
                        .output_frame = presented->output_frame,
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

            return {
                .state = GameplaySongClockInputState::Failed,
            };
        }

        GameplaySongClockStepSelection ResolveGameplaySongClockStep(
            GameplaySongClock& clock,
            std::uint32_t current_tick,
            std::int32_t game_time_offset_ms,
            int group_cursor_ms,
            const std::optional<audio::GameplayAudioCursorObservation>&
            cursor_observation) noexcept
        {
            GameplaySongClockStepSelection result{
                .input = SelectGameplaySongClockInput(
                    group_cursor_ms, cursor_observation),
            };
            if (!result.input.observation.has_value())
            {
                return result;
            }

            const auto decision = clock.Observe(
                current_tick,
                game_time_offset_ms,
                result.input.observation.value());
            if (!decision)
            {
                result.observation_rejected = true;
                return result;
            }

            result.decision = decision.value();
            result.step = decision->step;
            return result;
        }

        std::expected<bool, FramerateTimingProfileError>
        ShouldRunGameplayCadence(
            const FramerateTimingProfile& profile,
            GameplayAudioClockPlan audio_clock_plan,
            std::uint32_t current_tick,
            std::uint32_t step,
            std::int32_t phase,
            std::uint32_t authored_period) noexcept
        {
            if (UsesSharedGameplaySongClock(audio_clock_plan))
            {
                return CrossesAuthored60Cadence(
                    profile,
                    current_tick,
                    step,
                    phase,
                    authored_period);
            }
            return ShouldRunAuthored60Cadence(
                profile, current_tick, phase, authored_period);
        }

        std::expected<std::uint32_t, FramerateTimingProfileError>
        CountGameplayEffectAdvances(
            const FramerateTimingProfile& profile,
            GameplayAudioClockPlan audio_clock_plan,
            std::uint32_t current_tick,
            std::uint32_t step) noexcept
        {
            if (UsesSharedGameplaySongClock(audio_clock_plan))
            {
                return CountCrossedAuthored60Ticks(
                    profile, current_tick, step);
            }

            const auto boundary =
                IsAuthored60FrameBoundary(profile, current_tick);
            if (!boundary)
            {
                return std::unexpected(boundary.error());
            }
            return boundary.value() ? 1U : 0U;
        }

        std::optional<GameplayCadenceHookSemantics>
        GetGameplayCadenceHookSemantics(
            FramerateHookId id) noexcept
        {
            using Register = GameplayCadenceTestRegister;
            switch (id)
            {
            case FramerateHookId::EffectCadence6:
                return GameplayCadenceHookSemantics{6, Register::Edx, false};
            case FramerateHookId::EffectCadence5:
                return GameplayCadenceHookSemantics{5, Register::Edx, false};
            case FramerateHookId::EffectCadence4:
                return GameplayCadenceHookSemantics{4, Register::Edx, false};
            case FramerateHookId::EffectCadence16A:
                return GameplayCadenceHookSemantics{16, Register::Edx, true};
            case FramerateHookId::EffectCadence16B:
                return GameplayCadenceHookSemantics{16, Register::Ecx, true};
            case FramerateHookId::EffectCadence8:
                return GameplayCadenceHookSemantics{8, Register::Eax, true};
            default:
                return std::nullopt;
            }
        }
    } // namespace detail

    namespace
    {
        constexpr std::int32_t kMinimumAudioSkipMarginMs = 48;
        constexpr std::size_t kMaximumMovieClipInstanceNameBytes = 32;

        using GetSoundManager = void* (__cdecl*)();
        using GetGroupPlayCursorMs = int (__thiscall*)(void*, int);
        using GetConfig = void* (__cdecl*)();
        using AdvanceGameplayEffect = void (__thiscall*)(void*);

        struct FramerateOriginals final {
            MovieClipGotoFn movieclip_goto{};
            MovieClipAdvanceFn movieclip_advance{};
            MovieClipPreprocessFn movieclip_preprocess_visit{};
            NavigatorAdvanceFn navigator_advance{};
        };

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
            FramerateOriginals originals;
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

        [[nodiscard]] std::uintptr_t NativeTarget(FramerateNativeTarget id) noexcept {
            return g_runtime->native_targets[static_cast<std::size_t>(id)];
        }

        [[nodiscard]] bool ReadU32Safe(
            std::uintptr_t address,
            std::uint32_t& value) noexcept
        {
            __try
            {
                value = *reinterpret_cast<volatile std::uint32_t*>(address);
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
        }

        [[nodiscard]] bool ReadCStringSafe(
            std::uintptr_t address,
            std::span<char> destination,
            std::size_t& length) noexcept
        {
            length = 0;
            if (address == 0 || destination.empty())
            {
                return false;
            }

            __try
            {
                for (std::size_t index = 0;
                     index < destination.size();
                     ++index)
                {
                    const char value =
                        *reinterpret_cast<const volatile char*>(
                            address + index);
                    if (value == '\0')
                    {
                        length = index;
                        return true;
                    }
                    destination[index] = value;
                }
                return false;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
        }

        [[nodiscard]] bool WriteU32Safe(
            std::uintptr_t address,
            std::uint32_t value) noexcept
        {
            __try
            {
                *reinterpret_cast<volatile std::uint32_t*>(address) = value;
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
        }

        [[nodiscard]] bool ReadI32StackSafe(
            const safetyhook::Context& context,
            std::intptr_t offset,
            std::int32_t& value) noexcept
        {
            std::uint32_t raw{};
            if (!ReadU32Safe(context.ebp + offset, raw))
            {
                return false;
            }
            value = static_cast<std::int32_t>(raw);
            return true;
        }

        [[nodiscard]] std::int32_t ReadI32Stack(
            const safetyhook::Context& context,
            std::intptr_t offset,
            std::int32_t fallback = 0) noexcept
        {
            std::int32_t value{};
            if (!ReadI32StackSafe(context, offset, value))
            {
                return fallback;
            }
            return value;
        }

        void SetZeroFlag(safetyhook::Context& context, bool is_zero) noexcept
        {
            constexpr std::uint32_t kZeroFlag = 0x40;
            if (is_zero)
            {
                context.eflags |= kZeroFlag;
            }
            else
            {
                context.eflags &= ~kZeroFlag;
            }
        }

        void FatalRuntimeConversion(std::string_view operation) noexcept
        {
            ReportFramerateRuntimeFailure(
                operation);
        }

        [[nodiscard]] std::expected<game_version::VersionedOperation, game_version::PlanError>
        BindHook(const FramerateHookContract& contract) noexcept {
            using namespace game_version;
            switch (contract.id) {
            case FramerateHookId::MovieClipGoto:
                return InlineHookOperation{contract.site, reinterpret_cast<void*>(HookMovieClipGoto),
                    hooking::OriginalPublisher::To(&g_runtime->originals.movieclip_goto)};
            case FramerateHookId::MovieClipAdvance:
                return InlineHookOperation{contract.site, reinterpret_cast<void*>(HookMovieClipAdvance),
                    hooking::OriginalPublisher::To(&g_runtime->originals.movieclip_advance)};
            case FramerateHookId::PaletteCompare:
                return MidHookOperation{contract.site, HookPaletteCompare};
            case FramerateHookId::StageClipFrame:
                return MidHookOperation{contract.site, HookStageClipFrame};
            case FramerateHookId::IfblWait:
                return MidHookOperation{contract.site, HookIfblWait};
            case FramerateHookId::StageBgmPreload:
                return MidHookOperation{contract.site, HookStageBgmPreload};
            case FramerateHookId::TuneCountdownCompare:
                return MidHookOperation{contract.site, HookTuneCountdownCompare};
            case FramerateHookId::AudioSkipMargin:
                return MidHookOperation{contract.site, HookAudioSkipMargin};
            case FramerateHookId::AudioSkipInterval:
                return MidHookOperation{contract.site, HookAudioSkipInterval};
            case FramerateHookId::AudioResyncPolicy:
                return MidHookOperation{contract.site, HookAudioResyncPolicy};
            case FramerateHookId::GameplaySongClock:
                return MidHookOperation{contract.site, HookGameplaySongClock};
            case FramerateHookId::GameplayEffectAdvance:
                return MidHookOperation{contract.site, HookGameplayEffectAdvance};
            case FramerateHookId::EffectCadence6:
                return MidHookOperation{contract.site, HookEffectCadence6};
            case FramerateHookId::EffectCadence5:
                return MidHookOperation{contract.site, HookEffectCadence5};
            case FramerateHookId::EffectCadence4:
                return MidHookOperation{contract.site, HookEffectCadence4};
            case FramerateHookId::EffectCadence16A:
                return MidHookOperation{contract.site, HookEffectCadence16A};
            case FramerateHookId::EffectCadence16B:
                return MidHookOperation{contract.site, HookEffectCadence16B};
            case FramerateHookId::EffectCadence8:
                return MidHookOperation{contract.site, HookEffectCadence8};
            case FramerateHookId::RemoteCadenceA:
                return MidHookOperation{contract.site, HookRemoteCadenceA};
            case FramerateHookId::RemoteCadenceB:
                return MidHookOperation{contract.site, HookRemoteCadenceB};
            case FramerateHookId::GameplayBlink:
                return MidHookOperation{contract.site, HookGameplayBlink};
            case FramerateHookId::GreatGoodLifetimeOperand:
                return MidHookOperation{contract.site, HookAuthoredOperandEax};
            case FramerateHookId::GreatGoodFrameOperand:
                return MidHookOperation{contract.site, HookAuthoredOperandEcx};
            case FramerateHookId::EffectLifetimeAOperand:
                return MidHookOperation{contract.site, HookAuthoredOperandEcx};
            case FramerateHookId::EffectFrameAOperand:
                return MidHookOperation{contract.site, HookAuthoredOperandEdx};
            case FramerateHookId::EffectLifetimeBOperand:
                return MidHookOperation{contract.site, HookAuthoredOperandEcx};
            case FramerateHookId::EffectFrameBOperand:
                return MidHookOperation{contract.site, HookAuthoredOperandEdx};
            case FramerateHookId::DirectEffectFrameOperand:
                return MidHookOperation{contract.site, HookAuthoredOperandEdx};
            case FramerateHookId::ChartEffectFrameAOperand:
                return MidHookOperation{contract.site, HookAuthoredOperandEcx};
            case FramerateHookId::ChartEffectFrameBOperand:
                return MidHookOperation{contract.site, HookAuthoredOperandEcx};
            case FramerateHookId::ChartEffectFrameCOperand:
                return MidHookOperation{contract.site, HookAuthoredOperandEdx};
            case FramerateHookId::ChartEffectFrameDOperand:
                return MidHookOperation{contract.site, HookAuthoredOperandEax};
            case FramerateHookId::FixedVisualFrameOperand:
                return MidHookOperation{contract.site, HookAuthoredOperandEcx};
            case FramerateHookId::GameplayCountdownAssetFrame:
                return MidHookOperation{contract.site, HookGameplayCountdownAssetFrame};
            case FramerateHookId::PlayerPositionInitA:
                return MidHookOperation{contract.site, HookPlayerPositionInitialization};
            case FramerateHookId::PlayerPositionInitB:
                return MidHookOperation{contract.site, HookPlayerPositionInitialization};
            case FramerateHookId::PlayerPositionInitC:
                return MidHookOperation{contract.site, HookPlayerPositionInitialization};
            case FramerateHookId::PlayerPositionInitD:
                return MidHookOperation{contract.site, HookPlayerPositionInitialization};
            case FramerateHookId::PlayerPositionAssetFrame:
                return MidHookOperation{contract.site, HookPlayerPositionAssetFrame};
            case FramerateHookId::PlayerPositionDenominatorA:
                return MidHookOperation{contract.site, HookPlayerPositionDenominator};
            case FramerateHookId::PlayerPositionDenominatorB:
                return MidHookOperation{contract.site, HookPlayerPositionDenominator};
            case FramerateHookId::EffectFlowItemFrame:
                return MidHookOperation{contract.site, HookEffectFlowItemFrame};
            case FramerateHookId::EffectTutorialElapsed:
                return MidHookOperation{contract.site, HookEffectTutorialElapsed};
            case FramerateHookId::EffectChartPreRollDuration:
                return MidHookOperation{contract.site, HookEffectChartPreRollDuration};
            case FramerateHookId::EffectPlayerModuloDividend:
                return MidHookOperation{contract.site, HookEffectPlayerModuloDividend};
            case FramerateHookId::MovieClipPreprocessVisit:
                return InlineHookOperation{contract.site, reinterpret_cast<void*>(HookMovieClipPreprocessVisit),
                    hooking::OriginalPublisher::To(&g_runtime->originals.movieclip_preprocess_visit)};
            case FramerateHookId::RankingEntryCounterStore:
                return MidHookOperation{contract.site, HookRankingEntryCounterStore};
            case FramerateHookId::HitChartEntryCounterStore:
                return MidHookOperation{contract.site, HookHitChartEntryCounterStore};
            case FramerateHookId::UnlockRewardCountdownStore:
                return MidHookOperation{contract.site, HookUnlockRewardCountdownStore};
            case FramerateHookId::UnlockRewardPrimaryStateStore:
                return MidHookOperation{contract.site, HookUnlockRewardPrimaryStateStore};
            case FramerateHookId::UnlockRewardSecondaryStateStore:
                return MidHookOperation{contract.site, HookUnlockRewardSecondaryStateStore};
            case FramerateHookId::NavigatorAdvance:
                return InlineHookOperation{contract.site, reinterpret_cast<void*>(HookNavigatorAdvance),
                    hooking::OriginalPublisher::To(&g_runtime->originals.navigator_advance)};
            case FramerateHookId::OuterFrame:
                return MidHookOperation{contract.site, HookOuterFrame};
            }
            return std::unexpected(PlanError{.stage = PlanStage::invalid_plan,
                .feature = FeatureId::framerate, .site = contract.site.site});
        }

        [[nodiscard]] bool IsAuthored60HzTick() noexcept
        {
            return g_runtime->authored_60hz_tick.load(std::memory_order_acquire);
        }

        enum class UnlockRewardPromptTarget : std::uint8_t
        {
            Transition,
            Stable,
        };

        [[nodiscard]] std::optional<UnlockRewardPromptTarget>
        IdentifyUnlockRewardPromptHold(
            void* self,
            MovieClipAdvanceContext context) noexcept
        {
            if (context != MovieClipAdvanceContext::Ordinary)
            {
                return std::nullopt;
            }

            const auto movieclip = reinterpret_cast<std::uintptr_t>(self);
            std::uint32_t instance_name_hash{};
            if (!ReadU32Safe(
                movieclip + g_runtime->layout.movieclip_instance_hash,
                instance_name_hash))
            {
                return std::nullopt;
            }

            UnlockRewardPromptTarget target{};
            if (instance_name_hash ==
                kUnlockRewardPromptTransitionNameHash)
            {
                target = UnlockRewardPromptTarget::Transition;
            }
            else if (instance_name_hash ==
                kUnlockRewardPromptStableNameHash)
            {
                target = UnlockRewardPromptTarget::Stable;
            }
            else
            {
                return std::nullopt;
            }

            std::uint32_t instance_name_address{};
            std::uint32_t owner{};
            if (!ReadU32Safe(
                    movieclip + g_runtime->layout.movieclip_instance_name,
                    instance_name_address) ||
                instance_name_address == 0 ||
                !ReadU32Safe(
                    movieclip + g_runtime->layout.movieclip_owner,
                    owner) ||
                owner == 0)
            {
                return std::nullopt;
            }

            std::array<
                char,
                kMaximumMovieClipInstanceNameBytes> instance_name{};
            std::size_t instance_name_length{};
            if (!ReadCStringSafe(
                instance_name_address,
                instance_name,
                instance_name_length))
            {
                return std::nullopt;
            }

            const auto owner_address =
                static_cast<std::uintptr_t>(owner);
            std::uint32_t owner_name_hash{};
            std::uint32_t owner_name_address{};
            if (!ReadU32Safe(
                    owner_address + g_runtime->layout.movieclip_instance_hash,
                    owner_name_hash) ||
                !ReadU32Safe(
                    owner_address + g_runtime->layout.movieclip_instance_name,
                    owner_name_address) ||
                owner_name_address == 0)
            {
                return std::nullopt;
            }

            std::array<
                char,
                kMaximumMovieClipInstanceNameBytes> owner_name{};
            std::size_t owner_name_length{};
            if (!ReadCStringSafe(
                owner_name_address,
                owner_name,
                owner_name_length))
            {
                return std::nullopt;
            }

            std::uint32_t frame_low{};
            std::uint32_t frame_high{};
            std::uint32_t stopped{};
            if (!ReadU32Safe(
                    movieclip + g_runtime->layout.movieclip_frame_low,
                    frame_low) ||
                !ReadU32Safe(
                    movieclip + g_runtime->layout.movieclip_frame_high,
                    frame_high) ||
                !ReadU32Safe(
                    movieclip + g_runtime->layout.movieclip_stop_flag,
                    stopped))
            {
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
                    instance_name_length
                },
                owner_name_hash,
                std::string_view{
                    owner_name.data(),
                    owner_name_length
                },
                current_frame,
                stopped))
            {
                return std::nullopt;
            }
            return target;
        }

        [[nodiscard]] bool ReadTuneFrame(
            const safetyhook::Context& context,
            std::intptr_t tune_stack_offset,
            std::uint32_t& frame) noexcept
        {
            std::uint32_t tune{};
            return ReadU32Safe(context.ebp + tune_stack_offset, tune) &&
                tune != 0 && ReadU32Safe(tune + g_runtime->layout.tune_current_tick, frame);
        }

        [[nodiscard]] bool ReadTuneFrameAndStep(
            const safetyhook::Context& context,
            std::intptr_t tune_stack_offset,
            std::uint32_t& frame,
            std::uint32_t& step) noexcept
        {
            std::uint32_t tune{};
            return ReadU32Safe(context.ebp + tune_stack_offset, tune) &&
                tune != 0 &&
                ReadU32Safe(tune + g_runtime->layout.tune_current_tick, frame) &&
                ReadU32Safe(tune + g_runtime->layout.tune_step, step);
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
            return g_runtime->originals.movieclip_goto(self, frame, subframe);
        }

        void __fastcall HookMovieClipPreprocessVisit(
            void* self,
            void*,
            int traversal_arg)
        {
            MovieClipPreprocessScope scope{g_movieclip_preprocess_depth};
            g_runtime->menu_counters.preprocessing_visits.fetch_add(
                1, std::memory_order_relaxed);
            g_runtime->originals.movieclip_preprocess_visit(self, traversal_arg);
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
            return g_runtime->originals.movieclip_advance(self, forward, loop);
        }

        void ApplyPermanentMenuCounterStore(
            safetyhook::Context& context,
            MenuCounterRuntimeCounters& counters,
            std::uintptr_t suppress_resume_address) noexcept
        {
            const auto action = ApplyMenuCounterStoreGate(
                context,
                IsAuthored60HzTick(),
                suppress_resume_address);
            auto& counter =
                action == MenuCounterStoreAction::Commit
                    ? counters.commits
                    : counters.suppressions;
            counter.fetch_add(1, std::memory_order_relaxed);
        }

        void HookRankingEntryCounterStore(safetyhook::Context& context)
        {
            ApplyPermanentMenuCounterStore(
                context,
                g_runtime->menu_counters.ranking_entry,
                NativeTarget(FramerateNativeTarget::ranking_resume));
        }

        void HookHitChartEntryCounterStore(safetyhook::Context& context)
        {
            ApplyPermanentMenuCounterStore(
                context,
                g_runtime->menu_counters.hitchart_entry,
                NativeTarget(FramerateNativeTarget::hitchart_resume));
        }

        void HookUnlockRewardCountdownStore(safetyhook::Context& context)
        {
            ApplyPermanentMenuCounterStore(
                context,
                g_runtime->menu_counters.unlock_countdown,
                NativeTarget(FramerateNativeTarget::unlock_countdown_resume));
        }

        void HookUnlockRewardPrimaryStateStore(
            safetyhook::Context& context)
        {
            ApplyPermanentMenuCounterStore(
                context,
                g_runtime->menu_counters.unlock_primary,
                NativeTarget(FramerateNativeTarget::unlock_primary_resume));
        }

        void HookUnlockRewardSecondaryStateStore(
            safetyhook::Context& context)
        {
            ApplyPermanentMenuCounterStore(
                context,
                g_runtime->menu_counters.unlock_secondary,
                NativeTarget(FramerateNativeTarget::unlock_secondary_resume));
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
            return g_runtime->originals.navigator_advance(self);
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
            if (!ReadU32Safe(context.edx + g_runtime->layout.tune_countdown, countdown))
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

        // SafetyHook fixes the mid-hook callback signature to a mutable Context&.
        // ReSharper disable once CppParameterMayBeConstPtrOrRef
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
                    NativeTarget(FramerateNativeTarget::audio_resync_epilogue));
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

    } // namespace


namespace {
bool TargetRequired(FramerateNativeTarget target, const FramerateHookPlan& hooks) noexcept {
    const auto has = [&](FramerateHookId id) {
        return std::ranges::any_of(hooks.view(), [&](const auto& hook) { return hook.id == id; });
    };
    switch (target) {
    case FramerateNativeTarget::audio_resync_epilogue: return has(FramerateHookId::AudioResyncPolicy);
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
            auto bound = BindHook(hook);
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

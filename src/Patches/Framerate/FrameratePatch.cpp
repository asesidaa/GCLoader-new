#include "Patches/Framerate/FrameratePatch.h"

#include "Config/config.h"
#include "Patches/Countdown/CountdownTimerFreeze.h"
#include "Patches/Framerate/FramerateAuthoredClock.h"
#include "Patches/Framerate/FramerateDiagnostics.h"
#include "Patches/Framerate/FramerateHookTransforms.h"
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
#include <cstdint>
#include <limits>
#include <optional>
#include <sstream>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace gc::framerate {

namespace {

constexpr std::int32_t kMinimumAudioSkipMarginMs = 48;

struct FramerateHookStorage {
    safetyhook::InlineHook movieclip_goto{};
    safetyhook::InlineHook movieclip_advance{};
    safetyhook::MidHook palette_compare{};
    safetyhook::MidHook stage_clip_frame{};
    safetyhook::MidHook ifbl_wait{};
    safetyhook::MidHook stage_bgm_preload{};
    safetyhook::MidHook tune_countdown_compare{};
    safetyhook::MidHook audio_skip_margin{};
    safetyhook::MidHook audio_skip_interval{};
    safetyhook::MidHook audio_resync_diagnostic{};
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
    safetyhook::MidHook outer_frame{};
};

struct FramerateRuntimeCounters {
    std::atomic_uint64_t outer_calls{0};
    std::atomic_uint64_t authored_ticks{0};
    std::atomic_uint64_t authored_non_ticks{0};
    std::atomic_uint64_t movieclip_calls{0};
    std::atomic_uint64_t movieclip_skips{0};
    std::atomic_uint64_t movieclip_goto_calls{0};
    std::atomic_uint64_t stage_clip_indices{0};
    std::atomic_uint64_t stage_clip_mappings{0};
    std::atomic_uint64_t ifbl_wait_stores{0};
    std::atomic_uint64_t bgm_preload_calls{0};
    std::atomic_uint64_t bgm_preload_skips{0};
    std::atomic_uint64_t countdown_compare_hits{0};
    std::atomic_uint64_t audio_resync_seeks{0};
    std::atomic_uint64_t audio_resync_margin_seeks{0};
    std::atomic_uint64_t audio_resync_interval_seeks{0};
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
};

struct FramerateRuntimeState {
    FramerateRuntimeState(
        FramerateProfile profile_value,
        FramerateMonitor monitor_value,
        std::int64_t frequency_value,
        FrameratePlatformActions platform_value) noexcept
        : profile{std::move(profile_value)},
          monitor{std::move(monitor_value)},
          authored_clock{profile},
          qpc_frequency{frequency_value},
          platform{platform_value},
          transaction{ProductionFramerateMemoryApi()} {
    }

    FramerateProfile profile;
    FramerateMonitor monitor;
    Authored60PhaseClock authored_clock;
    std::int64_t qpc_frequency{};
    FrameratePlatformActions platform{};
    FrameratePatchTransaction transaction;
    FramerateHookStorage hooks;
    AuthoredFrameOperand authored_frame_operand{};
    FramerateRuntimeCounters counters;
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

char __fastcall HookMovieClipGoto(void*, void*, int, int);
char __fastcall HookMovieClipAdvance(void*, void*, char, char);
void HookPaletteCompare(safetyhook::Context&);
void HookStageClipFrame(safetyhook::Context&);
void HookIfblWait(safetyhook::Context&);
void HookStageBgmPreload(safetyhook::Context&);
void HookTuneCountdownCompare(safetyhook::Context&);
void HookAudioSkipMargin(safetyhook::Context&);
void HookAudioSkipInterval(safetyhook::Context&);
void HookAudioResyncDiagnostic(safetyhook::Context&);
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
void HookOuterFrame(safetyhook::Context&);

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
    case FramerateHookId::AudioResyncDiagnostic:
        operation.install = &InstallMidHook<
            &FramerateHookStorage::audio_resync_diagnostic,
            HookAudioResyncDiagnostic,
            0x002401C4>;
        operation.reset = &ResetOwnedHook<
            &FramerateHookStorage::audio_resync_diagnostic>;
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
    } guard;
    return g_runtime->hooks.movieclip_goto.unsafe_thiscall<char>(
        self, frame, subframe);
}

char __fastcall HookMovieClipAdvance(
    void* self,
    void*,
    char forward,
    char loop) {
    if (g_movieclip_goto_depth > 0) {
        g_runtime->counters.movieclip_goto_calls.fetch_add(
            1, std::memory_order_relaxed);
        return g_runtime->hooks.movieclip_advance.unsafe_thiscall<char>(
            self, forward, loop);
    }
    if (!IsAuthored60HzTick()) {
        g_runtime->counters.movieclip_skips.fetch_add(
            1, std::memory_order_relaxed);
        return 1;
    }
    g_runtime->counters.movieclip_calls.fetch_add(
        1, std::memory_order_relaxed);
    return g_runtime->hooks.movieclip_advance.unsafe_thiscall<char>(
        self, forward, loop);
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
    const auto scaled = ScalePositiveDuration(
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

void HookAudioResyncDiagnostic(safetyhook::Context& context) {
    const auto expected_ms = ReadI32Stack(context, -0x08);
    const auto cursor_ms = ReadI32Stack(context, -0x10, -1);
    const auto drift_ms = ReadI32Stack(context, -0x0C);
    const auto margin_ms = ReadI32Stack(context, -0x24);
    std::uint32_t tune{};
    (void)ReadU32Safe(context.ebp - 0x28, tune);

    std::uint32_t frame_counter{};
    std::uint32_t frame_step{};
    (void)ReadU32Safe(
        static_cast<std::uintptr_t>(tune) + 0x10, frame_counter);
    (void)ReadU32Safe(
        static_cast<std::uintptr_t>(tune) + 0x14, frame_step);

    const auto abs_drift = drift_ms < 0
        ? -static_cast<std::int64_t>(drift_ms)
        : static_cast<std::int64_t>(drift_ms);
    const bool margin_seek = abs_drift > margin_ms;
    const auto total = g_runtime->counters.audio_resync_seeks.fetch_add(
        1, std::memory_order_relaxed) + 1;
    if (margin_seek) {
        g_runtime->counters.audio_resync_margin_seeks.fetch_add(
            1, std::memory_order_relaxed);
    } else {
        g_runtime->counters.audio_resync_interval_seeks.fetch_add(
            1, std::memory_order_relaxed);
    }

    const auto target = g_runtime->profile.target_fps();
    const auto periodic_interval = std::max(1U, target / 2);
    if (total <= target || (total % periodic_interval) == 0) {
        PLOG_INFO << "FrameratePatch_AUDIO: target_fps=" << target
                  << " resync_seek=" << total
                  << " reason=" << (margin_seek ? "margin" : "interval")
                  << " frame=" << frame_counter
                  << " step=" << frame_step
                  << " expected_ms=" << expected_ms
                  << " cursor_ms=" << cursor_ms
                  << " drift_ms=" << drift_ms
                  << " abs_drift_ms=" << abs_drift
                  << " skip_margin_ms=" << margin_ms;
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
              << " audio_resync=" << counters.audio_resync_seeks.load(
                     std::memory_order_relaxed)
              << "/margin=" << counters.audio_resync_margin_seeks.load(
                     std::memory_order_relaxed)
              << "/interval="
              << counters.audio_resync_interval_seeks.load(
                     std::memory_order_relaxed)
              << "/margin_clamps="
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
                     std::memory_order_relaxed);
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

bool FrameratePatchInit() {
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

    g_runtime.emplace(
        std::move(profile_result.value()),
        std::move(monitor_result.value()),
        frequency.QuadPart,
        actions);

    ReportFramerateStartup(g_runtime->profile, actions);
    const auto direct_plan = BuildFramerateDirectPatchPlan(
        ExecutableBase(),
        g_runtime->profile,
        reinterpret_cast<std::uintptr_t>(
            g_runtime->profile.target_fps_operand()));
    if (!direct_plan) {
        FatalInstallPlanFailure(direct_plan.error());
        return false;
    }

    const auto hook_operations = BuildHookOperations(
        FramerateHookContracts(!g_runtime->profile.native_timing()),
        *g_runtime);
    const auto installed = g_runtime->transaction.Install(
        direct_plan->view(), hook_operations.view());
    if (!installed) {
        FatalTransactionFailure(installed.error());
        return false;
    }

    gc::timer_freeze::SetCountdownTimerFreezeEnabled(
        ConfigManager::instance().GetEnableTimerFreezePatches());
    gc::timer_freeze::CountdownTimerFreezeInit();
    return true;
}

} // namespace gc::framerate

#include "Patches/Framerate/FramerateEffectTiming.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace gc::framerate {

namespace {

template <typename... Values>
constexpr BytePattern EffectPattern(Values... values) noexcept {
    static_assert(sizeof...(Values) <= kMaximumPatternBytes);
    BytePattern result{};
    result.size = static_cast<std::uint8_t>(sizeof...(Values));
    std::size_t index = 0;
    ((result.bytes[index++] =
          static_cast<std::byte>(static_cast<std::uint8_t>(values))), ...);
    return result;
}

constexpr EffectTimingSite HookSite(
    const char* stable_id,
    std::uintptr_t boundary_rva,
    EffectClockDomain source,
    EffectClockDomain consumer,
    EffectTimingDisposition disposition,
    FramerateHookId hook_id,
    const char* evidence) noexcept {
    return {
        stable_id,
        boundary_rva,
        source,
        consumer,
        disposition,
        hook_id,
        evidence,
    };
}

constexpr EffectTimingSite EvidenceSite(
    const char* stable_id,
    std::uintptr_t boundary_rva,
    EffectClockDomain source,
    EffectClockDomain consumer,
    EffectTimingDisposition disposition,
    const char* evidence) noexcept {
    return {
        stable_id,
        boundary_rva,
        source,
        consumer,
        disposition,
        std::nullopt,
        evidence,
    };
}

constexpr std::array<EffectRegistrationSite, 34> kRegistrationSites{{
    {0x001F02F5, "sub_5F0220",
        "Target-frame CTune flow value crosses to effect frame at 0x001F0310."},
    {0x00240674, "GC120FPS_GameplayUpdate_120FrameToMs_JudgeMsWindows",
        "Registration reaches the frame-zero store at 0x0024067C."},
    {0x00240941, "GC120FPS_GameplayUpdate_120FrameToMs_JudgeMsWindows",
        "Registration reaches the frame-zero store at 0x0024094C."},
    {0x00240CDE, "GC120FPS_GameplayUpdate_120FrameToMs_JudgeMsWindows",
        "Registration reaches the frame-zero store at 0x00240CE9."},
    {0x002412B5, "GC120FPS_GameplayUpdate_120FrameToMs_JudgeMsWindows",
        "Registration reaches the frame-zero store at 0x002412C0."},
    {0x00244BC0, "target-cue effect owner A",
        "Definition length times normalized progress stores at 0x00244BDE."},
    {0x00244D30, "target-cue effect owner B",
        "Definition length times normalized progress stores at 0x00244D4E."},
    {0x00244E20, "target-cue effect owner C",
        "Definition length times normalized progress stores at 0x00244E3E."},
    {0x00244F10, "target-cue effect owner D",
        "Definition length times normalized progress stores at 0x00244F2E."},
    {0x00245000, "target-cue effect owner E",
        "Definition length times normalized progress stores at 0x0024501E."},
    {0x00246517, "sub_6463F0",
        "Authored-ms operands at 0x002464A8 and 0x00246528 reach 0x00246533."},
    {0x00246693, "sub_646650",
        "Registration reaches the frame-zero store at 0x0024669B."},
    {0x00248F75, "main effect A",
        "Authored-ms operands at 0x00248F00 and 0x00248F8C reach 0x00248F97."},
    {0x002491C9, "main effect B",
        "Authored-ms operands at 0x0024912B and 0x002491E0 reach 0x002491F1."},
    {0x002498E8, "tutorial effect pair A",
        "Shared mapped target-frame value from 0x00249593 stores at 0x002498F9."},
    {0x0024999C, "tutorial effect pair B",
        "Shared mapped target-frame value from 0x00249593 stores at 0x002499AD."},
    {0x00249A53, "gameplay countdown effect",
        "Mapped authored asset frame stores at 0x00249A9C."},
    {0x00249BEC, "direct gameplay effect",
        "Authored-ms operand at 0x00249C14 reaches the store at 0x00249C22."},
    {0x0024B61C, "chart effect normalized owner A",
        "Normalized authored frame stores at 0x0024B680."},
    {0x0024BB11, "chart effect normalized owner B",
        "Normalized authored frame stores at 0x0024BB6A."},
    {0x0024BC19, "chart effect mixed owner A",
        "Branches reach authored-ms store 0x0024BC99 or normalized store 0x0024BCC6."},
    {0x0024BF72, "chart effect normalized owner C",
        "Normalized authored frame stores at 0x0024BF9C."},
    {0x0024C56C, "chart effect shared normalized owner A",
        "Shared normalized authored frame stores at 0x0024C935."},
    {0x0024C5CA, "chart effect shared normalized owner B",
        "Shared normalized authored frame stores at 0x0024C935."},
    {0x0024C607, "chart effect shared normalized owner C",
        "Shared normalized authored frame stores at 0x0024C935."},
    {0x0024C8DC, "chart effect shared normalized owner D",
        "Shared normalized authored frame stores at 0x0024C935."},
    {0x0024CB4D, "chart effect mixed owner B",
        "Branches reach authored-ms operands, zero clamp, or normalized store 0x0024CD12."},
    {0x0024CBC0, "chart effect mixed owner C",
        "Branches reach authored-ms operands, zero clamp, or normalized store 0x0024CD12."},
    {0x0024CBFD, "chart effect mixed owner D",
        "Branches reach authored-ms operands, zero clamp, or normalized store 0x0024CD12."},
    {0x0024D710, "chart effect mixed owner E",
        "Branches reach authored-ms operand 0x0024D836 or normalized store 0x0024D871."},
    {0x0024D779, "chart effect mixed owner F",
        "Branches reach authored-ms operand 0x0024D836 or normalized store 0x0024D871."},
    {0x0024D7C4, "chart effect mixed owner G",
        "Branches reach authored-ms operand 0x0024D836 or normalized store 0x0024D871."},
    {0x0024EF82, "player-position elapsed effect",
        "Mapped elapsed value from 0x0024EF43 stores at 0x0024EF93."},
    {0x00250689, "player-position modulo effect",
        "Mapped dividend at 0x0025072E reaches remainder store 0x00250736."},
}};

constexpr std::array<EffectDurationQuerySite, 9> kDurationQuerySites{{
    {0x00246463, "sub_6463F0 GREAT/GOOD",
        "Authored-length selection and lifetime."},
    {0x0024647D, "sub_6463F0 GREAT/GOOD",
        "Authored-length selection and lifetime."},
    {0x00248EA7, "main effect A",
        "Authored-length selection and lifetime."},
    {0x00248EBF, "main effect A",
        "Authored-length selection and lifetime."},
    {0x00249104, "main effect B",
        "Authored-length selection and lifetime."},
    {0x0024962C, "tutorial shared elapsed owner",
        "Authored-duration comparison after shared mapping at 0x00249593."},
    {0x00249653, "tutorial shared elapsed owner",
        "Authored-duration comparison after shared mapping at 0x00249593."},
    {0x00249790, "tutorial shared elapsed owner",
        "Authored-duration comparison after shared mapping at 0x00249593."},
    {0x0024A92F, "chart pre-roll owner",
        "Target-frame comparison after duration scaling at 0x0024A934."},
}};

constexpr std::array<FramerateHookContract, 34> kEffectHookContracts{{
    {FramerateHookId::GameplayEffectAdvance, 0x00264E2D,
        EffectPattern(0xE8, 0x6E, 0xBA, 0xF8, 0xFF),
        "gameplay effect authored-60Hz advance"},
    {FramerateHookId::EffectCadence6, 0x0024063B,
        EffectPattern(0x85, 0xD2),
        "gameplay effect period-6 cadence"},
    {FramerateHookId::EffectCadence5, 0x002408D7,
        EffectPattern(0x85, 0xD2),
        "gameplay effect period-5 cadence"},
    {FramerateHookId::EffectCadence4, 0x00240C9C,
        EffectPattern(0x85, 0xD2),
        "gameplay effect period-4 cadence"},
    {FramerateHookId::EffectCadence16A, 0x00241213,
        EffectPattern(0x85, 0xD2),
        "gameplay effect period-16 cadence A"},
    {FramerateHookId::EffectCadence16B, 0x0024122F,
        EffectPattern(0x81, 0xE1, 0x0F, 0x00, 0x00, 0x80),
        "gameplay effect period-16 cadence B"},
    {FramerateHookId::EffectCadence8, 0x00241268,
        EffectPattern(0x85, 0xC0),
        "gameplay effect period-8 cadence"},
    {FramerateHookId::RemoteCadenceA, 0x002632DB,
        EffectPattern(0x85, 0xD2),
        "remote gameplay period-4 cadence A"},
    {FramerateHookId::RemoteCadenceB, 0x00263646,
        EffectPattern(0x85, 0xD2),
        "remote gameplay period-4 cadence B"},
    {FramerateHookId::GameplayBlink, 0x0024A1B9,
        EffectPattern(0xD1, 0xF8),
        "gameplay authored-frame blink"},
    {FramerateHookId::GreatGoodLifetimeOperand, 0x002464A8,
        EffectPattern(0xD8, 0x48, 0x18),
        "GREAT/GOOD lifetime authored-ms operand (dead EAX)"},
    {FramerateHookId::GreatGoodFrameOperand, 0x00246528,
        EffectPattern(0xD8, 0x71, 0x18),
        "GREAT/GOOD frame authored-ms operand (dead ECX)"},
    {FramerateHookId::EffectLifetimeAOperand, 0x00248F00,
        EffectPattern(0xD8, 0x49, 0x18),
        "effect lifetime A authored-ms operand (dead ECX)"},
    {FramerateHookId::EffectFrameAOperand, 0x00248F8C,
        EffectPattern(0xD8, 0x72, 0x18),
        "effect frame A authored-ms operand (dead EDX)"},
    {FramerateHookId::EffectLifetimeBOperand, 0x0024912B,
        EffectPattern(0xD8, 0x49, 0x18),
        "effect lifetime B authored-ms operand (dead ECX)"},
    {FramerateHookId::EffectFrameBOperand, 0x002491E0,
        EffectPattern(0xD8, 0x72, 0x18),
        "effect frame B authored-ms operand (dead EDX)"},
    {FramerateHookId::DirectEffectFrameOperand, 0x00249C14,
        EffectPattern(0xD8, 0x72, 0x18),
        "direct effect frame authored-ms operand (dead EDX)"},
    {FramerateHookId::ChartEffectFrameAOperand, 0x0024BC8B,
        EffectPattern(0xD8, 0x71, 0x18),
        "chart effect frame A authored-ms operand (dead ECX)"},
    {FramerateHookId::ChartEffectFrameBOperand, 0x0024CC8A,
        EffectPattern(0xD8, 0x71, 0x18),
        "chart effect frame B authored-ms operand (dead ECX)"},
    {FramerateHookId::ChartEffectFrameCOperand, 0x0024CCBE,
        EffectPattern(0xD8, 0x72, 0x18),
        "chart effect frame C authored-ms operand (dead EDX)"},
    {FramerateHookId::ChartEffectFrameDOperand, 0x0024D836,
        EffectPattern(0xD8, 0x70, 0x18),
        "chart effect frame D authored-ms operand (dead EAX)"},
    {FramerateHookId::FixedVisualFrameOperand, 0x00250AD5,
        EffectPattern(0xD8, 0x71, 0x18),
        "fixed visual frame authored-ms operand (dead ECX)"},
    {FramerateHookId::GameplayCountdownAssetFrame, 0x00249A9C,
        EffectPattern(0x89, 0x48, 0x08),
        "gameplay countdown authored asset-frame mapping"},
    {FramerateHookId::PlayerPositionInitA, 0x00263240,
        EffectPattern(0x89, 0x84, 0x91, 0x54, 0x1D, 0x00, 0x00),
        "player-position duration initialization A"},
    {FramerateHookId::PlayerPositionInitB, 0x002632B2,
        EffectPattern(0x89, 0x84, 0x8A, 0x54, 0x1D, 0x00, 0x00),
        "player-position duration initialization B"},
    {FramerateHookId::PlayerPositionInitC, 0x0026359B,
        EffectPattern(0x89, 0x84, 0x8A, 0x54, 0x1D, 0x00, 0x00),
        "player-position duration initialization C"},
    {FramerateHookId::PlayerPositionInitD, 0x00263615,
        EffectPattern(0x89, 0x84, 0x8A, 0x54, 0x1D, 0x00, 0x00),
        "player-position duration initialization D"},
    {FramerateHookId::PlayerPositionAssetFrame, 0x0024EF43,
        EffectPattern(0x2B, 0x84, 0x8A, 0x54, 0x1D, 0x00, 0x00),
        "player-position authored asset-frame mapping"},
    {FramerateHookId::PlayerPositionDenominatorA, 0x0024F76D,
        EffectPattern(0xDB, 0x80, 0xC4, 0x00, 0x00, 0x00),
        "player-position scaled denominator A"},
    {FramerateHookId::PlayerPositionDenominatorB, 0x0024FD40,
        EffectPattern(0xDB, 0x80, 0xC4, 0x00, 0x00, 0x00),
        "player-position scaled denominator B"},
    {FramerateHookId::EffectFlowItemFrame, 0x001F0310,
        EffectPattern(0x89, 0x42, 0x08),
        "effect flow-item authored-frame mapping"},
    {FramerateHookId::EffectTutorialElapsed, 0x00249593,
        EffectPattern(0x89, 0x95, 0x74, 0xFF, 0xFF, 0xFF),
        "tutorial shared elapsed authored-frame mapping"},
    {FramerateHookId::EffectChartPreRollDuration, 0x0024A934,
        EffectPattern(0x89, 0x45, 0x9C),
        "chart pre-roll authored-duration scaling"},
    {FramerateHookId::EffectPlayerModuloDividend, 0x0025072E,
        EffectPattern(0xF7, 0xF9),
        "player effect authored modulo dividend mapping"},
}};

constexpr std::array<EffectTimingSite, 67> kTimingSites{{
    HookSite(
        "hook.gameplay-effect-advance", 0x00264E2D,
        EffectClockDomain::TargetFrame, EffectClockDomain::Authored60Frame,
        EffectTimingDisposition::ManagerGated,
        FramerateHookId::GameplayEffectAdvance,
        "Manager advance is gated on authored-60 boundaries; 0x4000 effects may bypass it."),
    HookSite(
        "hook.effect-cadence-6", 0x0024063B,
        EffectClockDomain::TargetFrame, EffectClockDomain::Authored60Frame,
        EffectTimingDisposition::Hook, FramerateHookId::EffectCadence6,
        "Period-6 cadence is converted to the authored-60 phase."),
    HookSite(
        "hook.effect-cadence-5", 0x002408D7,
        EffectClockDomain::TargetFrame, EffectClockDomain::Authored60Frame,
        EffectTimingDisposition::Hook, FramerateHookId::EffectCadence5,
        "Period-5 cadence is converted to the authored-60 phase."),
    HookSite(
        "hook.effect-cadence-4", 0x00240C9C,
        EffectClockDomain::TargetFrame, EffectClockDomain::Authored60Frame,
        EffectTimingDisposition::Hook, FramerateHookId::EffectCadence4,
        "Period-4 cadence is converted to the authored-60 phase."),
    HookSite(
        "hook.effect-cadence-16-a", 0x00241213,
        EffectClockDomain::TargetFrame, EffectClockDomain::Authored60Frame,
        EffectTimingDisposition::Hook, FramerateHookId::EffectCadence16A,
        "Period-16 cadence A is converted to the authored-60 phase."),
    HookSite(
        "hook.effect-cadence-16-b", 0x0024122F,
        EffectClockDomain::TargetFrame, EffectClockDomain::Authored60Frame,
        EffectTimingDisposition::Hook, FramerateHookId::EffectCadence16B,
        "Period-16 cadence B is converted to the authored-60 phase."),
    HookSite(
        "hook.effect-cadence-8", 0x00241268,
        EffectClockDomain::TargetFrame, EffectClockDomain::Authored60Frame,
        EffectTimingDisposition::Hook, FramerateHookId::EffectCadence8,
        "Period-8 cadence is converted to the authored-60 phase."),
    HookSite(
        "hook.remote-cadence-a", 0x002632DB,
        EffectClockDomain::TargetFrame, EffectClockDomain::Authored60Frame,
        EffectTimingDisposition::Hook, FramerateHookId::RemoteCadenceA,
        "Remote period-4 cadence A feeds CTune visuals."),
    HookSite(
        "hook.remote-cadence-b", 0x00263646,
        EffectClockDomain::TargetFrame, EffectClockDomain::Authored60Frame,
        EffectTimingDisposition::Hook, FramerateHookId::RemoteCadenceB,
        "Remote period-4 cadence B feeds CTune visuals."),
    HookSite(
        "hook.gameplay-blink", 0x0024A1B9,
        EffectClockDomain::TargetFrame, EffectClockDomain::Authored60Frame,
        EffectTimingDisposition::Hook, FramerateHookId::GameplayBlink,
        "Target gameplay frame is mapped before the authored blink calculation."),
    HookSite(
        "hook.great-good-lifetime-operand", 0x002464A8,
        EffectClockDomain::Milliseconds, EffectClockDomain::Authored60Frame,
        EffectTimingDisposition::Hook,
        FramerateHookId::GreatGoodLifetimeOperand,
        "Final x87 divisor is redirected to authored 1000/60 milliseconds."),
    HookSite(
        "hook.great-good-frame-operand", 0x00246528,
        EffectClockDomain::Milliseconds, EffectClockDomain::Authored60Frame,
        EffectTimingDisposition::Hook,
        FramerateHookId::GreatGoodFrameOperand,
        "Final x87 divisor is redirected to authored 1000/60 milliseconds."),
    HookSite(
        "hook.effect-lifetime-a-operand", 0x00248F00,
        EffectClockDomain::Milliseconds, EffectClockDomain::Authored60Frame,
        EffectTimingDisposition::Hook,
        FramerateHookId::EffectLifetimeAOperand,
        "Main effect A lifetime uses the authored-frame millisecond operand."),
    HookSite(
        "hook.effect-frame-a-operand", 0x00248F8C,
        EffectClockDomain::Milliseconds, EffectClockDomain::Authored60Frame,
        EffectTimingDisposition::Hook, FramerateHookId::EffectFrameAOperand,
        "Main effect A frame uses the authored-frame millisecond operand."),
    HookSite(
        "hook.effect-lifetime-b-operand", 0x0024912B,
        EffectClockDomain::Milliseconds, EffectClockDomain::Authored60Frame,
        EffectTimingDisposition::Hook,
        FramerateHookId::EffectLifetimeBOperand,
        "Main effect B lifetime uses the authored-frame millisecond operand."),
    HookSite(
        "hook.effect-frame-b-operand", 0x002491E0,
        EffectClockDomain::Milliseconds, EffectClockDomain::Authored60Frame,
        EffectTimingDisposition::Hook, FramerateHookId::EffectFrameBOperand,
        "Main effect B frame uses the authored-frame millisecond operand."),
    HookSite(
        "hook.direct-effect-frame-operand", 0x00249C14,
        EffectClockDomain::Milliseconds, EffectClockDomain::Authored60Frame,
        EffectTimingDisposition::Hook,
        FramerateHookId::DirectEffectFrameOperand,
        "Direct effect frame uses the authored-frame millisecond operand."),
    HookSite(
        "hook.chart-effect-frame-a-operand", 0x0024BC8B,
        EffectClockDomain::Milliseconds, EffectClockDomain::Authored60Frame,
        EffectTimingDisposition::Hook,
        FramerateHookId::ChartEffectFrameAOperand,
        "Chart effect frame A uses the authored-frame millisecond operand."),
    HookSite(
        "hook.chart-effect-frame-b-operand", 0x0024CC8A,
        EffectClockDomain::Milliseconds, EffectClockDomain::Authored60Frame,
        EffectTimingDisposition::Hook,
        FramerateHookId::ChartEffectFrameBOperand,
        "Chart effect frame B uses the authored-frame millisecond operand."),
    HookSite(
        "hook.chart-effect-frame-c-operand", 0x0024CCBE,
        EffectClockDomain::Milliseconds, EffectClockDomain::Authored60Frame,
        EffectTimingDisposition::Hook,
        FramerateHookId::ChartEffectFrameCOperand,
        "Chart effect frame C uses the authored-frame millisecond operand."),
    HookSite(
        "hook.chart-effect-frame-d-operand", 0x0024D836,
        EffectClockDomain::Milliseconds, EffectClockDomain::Authored60Frame,
        EffectTimingDisposition::Hook,
        FramerateHookId::ChartEffectFrameDOperand,
        "Chart effect frame D uses the authored-frame millisecond operand."),
    HookSite(
        "hook.fixed-visual-frame-operand", 0x00250AD5,
        EffectClockDomain::Milliseconds, EffectClockDomain::NonCtuneData,
        EffectTimingDisposition::NonCtuneOutOfScope,
        FramerateHookId::FixedVisualFrameOperand,
        "Existing fixed-visual correction is retained but is not a CTune effect producer."),
    HookSite(
        "hook.gameplay-countdown-asset-frame", 0x00249A9C,
        EffectClockDomain::TargetFrame, EffectClockDomain::Authored60Frame,
        EffectTimingDisposition::Hook,
        FramerateHookId::GameplayCountdownAssetFrame,
        "Countdown target frame is mapped before the effect frame store."),
    HookSite(
        "hook.player-position-init-a", 0x00263240,
        EffectClockDomain::Authored60Frame, EffectClockDomain::TargetFrame,
        EffectTimingDisposition::Hook, FramerateHookId::PlayerPositionInitA,
        "Authored player-position duration A is scaled to target frames."),
    HookSite(
        "hook.player-position-init-b", 0x002632B2,
        EffectClockDomain::Authored60Frame, EffectClockDomain::TargetFrame,
        EffectTimingDisposition::Hook, FramerateHookId::PlayerPositionInitB,
        "Authored player-position duration B is scaled to target frames."),
    HookSite(
        "hook.player-position-init-c", 0x0026359B,
        EffectClockDomain::Authored60Frame, EffectClockDomain::TargetFrame,
        EffectTimingDisposition::Hook, FramerateHookId::PlayerPositionInitC,
        "Authored player-position duration C is scaled to target frames."),
    HookSite(
        "hook.player-position-init-d", 0x00263615,
        EffectClockDomain::Authored60Frame, EffectClockDomain::TargetFrame,
        EffectTimingDisposition::Hook, FramerateHookId::PlayerPositionInitD,
        "Authored player-position duration D is scaled to target frames."),
    HookSite(
        "hook.player-position-asset-frame", 0x0024EF43,
        EffectClockDomain::TargetFrame, EffectClockDomain::Authored60Frame,
        EffectTimingDisposition::Hook,
        FramerateHookId::PlayerPositionAssetFrame,
        "Player-position elapsed target frames map to an authored asset frame."),
    HookSite(
        "hook.player-position-denominator-a", 0x0024F76D,
        EffectClockDomain::Authored60Frame, EffectClockDomain::TargetFrame,
        EffectTimingDisposition::Hook,
        FramerateHookId::PlayerPositionDenominatorA,
        "Authored duration denominator A is scaled for target-frame progress."),
    HookSite(
        "hook.player-position-denominator-b", 0x0024FD40,
        EffectClockDomain::Authored60Frame, EffectClockDomain::TargetFrame,
        EffectTimingDisposition::Hook,
        FramerateHookId::PlayerPositionDenominatorB,
        "Authored duration denominator B is scaled for target-frame progress."),
    HookSite(
        "hook.effect-flow-item-frame", 0x001F0310,
        EffectClockDomain::TargetFrame, EffectClockDomain::Authored60Frame,
        EffectTimingDisposition::Hook, FramerateHookId::EffectFlowItemFrame,
        "sub_5F0220 stores a duration divided by runtime frame milliseconds."),
    HookSite(
        "hook.effect-tutorial-elapsed", 0x00249593,
        EffectClockDomain::TargetFrame, EffectClockDomain::Authored60Frame,
        EffectTimingDisposition::Hook,
        FramerateHookId::EffectTutorialElapsed,
        "Shared tutorial elapsed value feeds three authored-length comparisons and two stores."),
    HookSite(
        "hook.effect-chart-pre-roll-duration", 0x0024A934,
        EffectClockDomain::Authored60Frame, EffectClockDomain::TargetFrame,
        EffectTimingDisposition::Hook,
        FramerateHookId::EffectChartPreRollDuration,
        "Authored duration is compared with a distance divided by runtime frame milliseconds."),
    HookSite(
        "hook.effect-player-modulo-dividend", 0x0025072E,
        EffectClockDomain::TargetFrame, EffectClockDomain::Authored60Frame,
        EffectTimingDisposition::Hook,
        FramerateHookId::EffectPlayerModuloDividend,
        "Tune target frame must map before signed division by authored definition length."),

    EvidenceSite(
        "site.001F0D04", 0x001F0D04,
        EffectClockDomain::ConstantOrSentinel,
        EffectClockDomain::Authored60Frame,
        EffectTimingDisposition::ResetOrConstant,
        "Engine reset helper writes frame zero."),
    EvidenceSite(
        "site.001F1E2A", 0x001F1E2A,
        EffectClockDomain::ConstantOrSentinel,
        EffectClockDomain::Authored60Frame,
        EffectTimingDisposition::ResetOrConstant,
        "Engine reset helper writes frame zero."),
    EvidenceSite(
        "site.001F34B0", 0x001F34B0,
        EffectClockDomain::ConstantOrSentinel,
        EffectClockDomain::Authored60Frame,
        EffectTimingDisposition::ResetOrConstant,
        "Engine reset helper writes frame zero."),
    EvidenceSite(
        "site.0024067C", 0x0024067C,
        EffectClockDomain::ConstantOrSentinel,
        EffectClockDomain::Authored60Frame,
        EffectTimingDisposition::ResetOrConstant,
        "Registration branch writes frame zero."),
    EvidenceSite(
        "site.0024094C", 0x0024094C,
        EffectClockDomain::ConstantOrSentinel,
        EffectClockDomain::Authored60Frame,
        EffectTimingDisposition::ResetOrConstant,
        "Registration branch writes frame zero."),
    EvidenceSite(
        "site.00240CE9", 0x00240CE9,
        EffectClockDomain::ConstantOrSentinel,
        EffectClockDomain::Authored60Frame,
        EffectTimingDisposition::ResetOrConstant,
        "Registration branch writes frame zero."),
    EvidenceSite(
        "site.002412C0", 0x002412C0,
        EffectClockDomain::ConstantOrSentinel,
        EffectClockDomain::Authored60Frame,
        EffectTimingDisposition::ResetOrConstant,
        "Registration branch writes frame zero."),
    EvidenceSite(
        "site.0024669B", 0x0024669B,
        EffectClockDomain::ConstantOrSentinel,
        EffectClockDomain::Authored60Frame,
        EffectTimingDisposition::ResetOrConstant,
        "Registration branch writes frame zero."),
    EvidenceSite(
        "site.0024CCE1", 0x0024CCE1,
        EffectClockDomain::ConstantOrSentinel,
        EffectClockDomain::Authored60Frame,
        EffectTimingDisposition::ResetOrConstant,
        "Negative-frame clamp writes zero."),

    EvidenceSite(
        "site.00244BDE", 0x00244BDE,
        EffectClockDomain::NormalizedProgress,
        EffectClockDomain::Authored60Frame,
        EffectTimingDisposition::AlreadyAuthoredNormalized,
        "Authored definition length is multiplied by normalized progress."),
    EvidenceSite(
        "site.00244D4E", 0x00244D4E,
        EffectClockDomain::NormalizedProgress,
        EffectClockDomain::Authored60Frame,
        EffectTimingDisposition::AlreadyAuthoredNormalized,
        "Authored definition length is multiplied by normalized progress."),
    EvidenceSite(
        "site.00244E3E", 0x00244E3E,
        EffectClockDomain::NormalizedProgress,
        EffectClockDomain::Authored60Frame,
        EffectTimingDisposition::AlreadyAuthoredNormalized,
        "Authored definition length is multiplied by normalized progress."),
    EvidenceSite(
        "site.00244F2E", 0x00244F2E,
        EffectClockDomain::NormalizedProgress,
        EffectClockDomain::Authored60Frame,
        EffectTimingDisposition::AlreadyAuthoredNormalized,
        "Authored definition length is multiplied by normalized progress."),
    EvidenceSite(
        "site.0024501E", 0x0024501E,
        EffectClockDomain::NormalizedProgress,
        EffectClockDomain::Authored60Frame,
        EffectTimingDisposition::AlreadyAuthoredNormalized,
        "Authored definition length is multiplied by normalized progress."),
    EvidenceSite(
        "site.0024B680", 0x0024B680,
        EffectClockDomain::NormalizedProgress,
        EffectClockDomain::Authored60Frame,
        EffectTimingDisposition::AlreadyAuthoredNormalized,
        "Chart definition length is multiplied by normalized progress."),
    EvidenceSite(
        "site.0024BB6A", 0x0024BB6A,
        EffectClockDomain::NormalizedProgress,
        EffectClockDomain::Authored60Frame,
        EffectTimingDisposition::AlreadyAuthoredNormalized,
        "Chart definition length is multiplied by normalized progress."),
    EvidenceSite(
        "site.0024BCC6", 0x0024BCC6,
        EffectClockDomain::NormalizedProgress,
        EffectClockDomain::Authored60Frame,
        EffectTimingDisposition::AlreadyAuthoredNormalized,
        "Chart definition length is multiplied by normalized progress."),
    EvidenceSite(
        "site.0024BF9C", 0x0024BF9C,
        EffectClockDomain::NormalizedProgress,
        EffectClockDomain::Authored60Frame,
        EffectTimingDisposition::AlreadyAuthoredNormalized,
        "Chart definition length is multiplied by normalized progress."),
    EvidenceSite(
        "site.0024C935", 0x0024C935,
        EffectClockDomain::NormalizedProgress,
        EffectClockDomain::Authored60Frame,
        EffectTimingDisposition::AlreadyAuthoredNormalized,
        "Chart definition length is multiplied by normalized progress."),
    EvidenceSite(
        "site.0024CD12", 0x0024CD12,
        EffectClockDomain::NormalizedProgress,
        EffectClockDomain::Authored60Frame,
        EffectTimingDisposition::AlreadyAuthoredNormalized,
        "Chart definition length is multiplied by normalized progress."),
    EvidenceSite(
        "site.0024D871", 0x0024D871,
        EffectClockDomain::NormalizedProgress,
        EffectClockDomain::Authored60Frame,
        EffectTimingDisposition::AlreadyAuthoredNormalized,
        "Chart definition length is multiplied by normalized progress."),

    EvidenceSite(
        "site.001F3266", 0x001F3266,
        EffectClockDomain::Authored60Frame,
        EffectClockDomain::Authored60Frame,
        EffectTimingDisposition::ChildInherited,
        "Child frame derives from its already-authored parent through sub_5F17A0."),

    EvidenceSite(
        "site.0024A574", 0x0024A574,
        EffectClockDomain::NonCtuneData, EffectClockDomain::NonCtuneData,
        EffectTimingDisposition::NonCtuneOutOfScope,
        "Third dword of a 12-byte vector is copied to a call stack."),
    EvidenceSite(
        "site.0024C487", 0x0024C487,
        EffectClockDomain::NonCtuneData, EffectClockDomain::NonCtuneData,
        EffectTimingDisposition::NonCtuneOutOfScope,
        "Chart 3D vector copy, not an effect-frame store."),
    EvidenceSite(
        "site.0024C4C5", 0x0024C4C5,
        EffectClockDomain::NonCtuneData, EffectClockDomain::NonCtuneData,
        EffectTimingDisposition::NonCtuneOutOfScope,
        "Chart 3D vector copy, not an effect-frame store."),
    EvidenceSite(
        "site.0024CA7D", 0x0024CA7D,
        EffectClockDomain::NonCtuneData, EffectClockDomain::NonCtuneData,
        EffectTimingDisposition::NonCtuneOutOfScope,
        "Chart 3D vector copy, not an effect-frame store."),
    EvidenceSite(
        "site.0024CABB", 0x0024CABB,
        EffectClockDomain::NonCtuneData, EffectClockDomain::NonCtuneData,
        EffectTimingDisposition::NonCtuneOutOfScope,
        "Chart 3D vector copy, not an effect-frame store."),
    EvidenceSite(
        "site.0024D3E0", 0x0024D3E0,
        EffectClockDomain::NonCtuneData, EffectClockDomain::NonCtuneData,
        EffectTimingDisposition::NonCtuneOutOfScope,
        "Chart 3D vector copy, not an effect-frame store."),
    EvidenceSite(
        "site.0024D41E", 0x0024D41E,
        EffectClockDomain::NonCtuneData, EffectClockDomain::NonCtuneData,
        EffectTimingDisposition::NonCtuneOutOfScope,
        "Chart 3D vector copy, not an effect-frame store."),
    EvidenceSite(
        "site.00250926", 0x00250926,
        EffectClockDomain::NonCtuneData, EffectClockDomain::NonCtuneData,
        EffectTimingDisposition::NonCtuneOutOfScope,
        "Player-position 3D vector copy, not an effect-frame store."),
    EvidenceSite(
        "site.00250A8D", 0x00250A8D,
        EffectClockDomain::NonCtuneData, EffectClockDomain::NonCtuneData,
        EffectTimingDisposition::NonCtuneOutOfScope,
        "Player-position 3D vector copy, not an effect-frame store."),
    EvidenceSite(
        "site.00250BBB", 0x00250BBB,
        EffectClockDomain::NonCtuneData, EffectClockDomain::NonCtuneData,
        EffectTimingDisposition::NonCtuneOutOfScope,
        "Player-position 3D vector copy, not an effect-frame store."),
    EvidenceSite(
        "site.00250C8D", 0x00250C8D,
        EffectClockDomain::NonCtuneData, EffectClockDomain::NonCtuneData,
        EffectTimingDisposition::NonCtuneOutOfScope,
        "Player-position 3D vector copy, not an effect-frame store."),
}};

} // namespace

std::span<const EffectRegistrationSite>
EffectRegistrationSites() noexcept {
    return kRegistrationSites;
}

std::span<const EffectDurationQuerySite>
EffectDurationQuerySites() noexcept {
    return kDurationQuerySites;
}

std::span<const EffectTimingSite>
EffectTimingSites() noexcept {
    return kTimingSites;
}

std::span<const FramerateHookContract>
FramerateEffectHookContracts() noexcept {
    return kEffectHookContracts;
}

EffectTimingManifestSummary
SummarizeEffectTimingManifest() noexcept {
    EffectTimingManifestSummary summary{
        .timing_sites = kTimingSites.size(),
        .registration_sites = kRegistrationSites.size(),
        .duration_queries = kDurationQuerySites.size(),
        .hook_contracts = kEffectHookContracts.size(),
    };

    for (const auto& site : kTimingSites) {
        switch (site.disposition) {
        case EffectTimingDisposition::Hook:
            break;
        case EffectTimingDisposition::ManagerGated:
            ++summary.manager_gated;
            break;
        case EffectTimingDisposition::AlreadyAuthoredNormalized:
            ++summary.already_authored;
            break;
        case EffectTimingDisposition::ResetOrConstant:
            ++summary.reset_or_constant;
            break;
        case EffectTimingDisposition::ChildInherited:
            ++summary.child_inherited;
            break;
        case EffectTimingDisposition::NonCtuneOutOfScope:
            ++summary.non_ctune_out_of_scope;
            break;
        }
    }
    return summary;
}

} // namespace gc::framerate

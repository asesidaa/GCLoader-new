#include "Patches/Framerate/FramerateGameProfile.h"
#include "Patches/Framerate/FrameTimingHooks.h"
#include "Patches/Framerate/EffectTimingHooks.h"
#include "Patches/Framerate/MenuTimingHooks.h"
namespace gc::framerate {
namespace {
using namespace game_version;
using runtime_image::PatternOf;
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


EffectTimingManifestSummary
SummarizeNativeEffectTiming() noexcept {
    EffectTimingManifestSummary summary{
        .timing_sites = kTimingSites.size(),
        .registration_sites = kRegistrationSites.size(),
        .duration_queries = kDurationQuerySites.size(),
        .hook_contracts = 34,
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


FramerateGameProfile MakeProfile(GameImageVariant variant) noexcept {
    static_assert(std::tuple_size_v<decltype(FramerateGameProfile::writes)> == 17);
    static_assert(std::tuple_size_v<decltype(FramerateGameProfile::hooks)> == 53);
    return {GameBuild::groove_coaster_471, variant, {
        FramerateWriteContract{{FeatureId::framerate, "gameplay frame milliseconds",
            VersionedOperationKind::byte_patch, 0x002FC0A0, 4, PatternOf<0x55, 0x55, 0x85, 0x41>(), {}, 0},
            FramerateWriteValue::frame_ms, runtime_image::MemoryKind::data},
        FramerateWriteContract{{FeatureId::framerate, "visual frame milliseconds",
            VersionedOperationKind::byte_patch, 0x002F4604, 4, PatternOf<0x55, 0x55, 0x85, 0x41>(), {}, 1},
            FramerateWriteValue::frame_ms, runtime_image::MemoryKind::data},
        FramerateWriteContract{{FeatureId::framerate, "gameplay frame seconds",
            VersionedOperationKind::byte_patch, 0x002FC280, 4, PatternOf<0x89, 0x88, 0x88, 0x3C>(), {}, 2},
            FramerateWriteValue::frame_seconds, runtime_image::MemoryKind::data},
        FramerateWriteContract{{FeatureId::framerate, "render smoothing step",
            VersionedOperationKind::byte_patch, 0x002E8F00, 4, PatternOf<0x00, 0x00, 0x80, 0x40>(), {}, 3},
            FramerateWriteValue::smoothing, runtime_image::MemoryKind::data},
        FramerateWriteContract{{FeatureId::framerate, "render offset-decay step",
            VersionedOperationKind::byte_patch, 0x002E8F04, 4, PatternOf<0x00, 0x00, 0xA0, 0x40>(), {}, 4},
            FramerateWriteValue::decay, runtime_image::MemoryKind::data},
        FramerateWriteContract{{FeatureId::framerate, "XIO repeat initial duration",
            VersionedOperationKind::byte_patch, 0x00055CCC, 6, PatternOf<0xC7, 0x00, 0x10, 0x00, 0x00, 0x00>(), {}, 5},
            FramerateWriteValue::repeat_initial, runtime_image::MemoryKind::code},
        FramerateWriteContract{{FeatureId::framerate, "XIO repeat next duration",
            VersionedOperationKind::byte_patch, 0x00055CDD, 6, PatternOf<0xC7, 0x00, 0x08, 0x00, 0x00, 0x00>(), {}, 6},
            FramerateWriteValue::repeat_next, runtime_image::MemoryKind::code},
        FramerateWriteContract{{FeatureId::framerate, "native keyboard repeat initial duration",
            VersionedOperationKind::byte_patch, 0x0005F843, 10, PatternOf<0xC7, 0x86, 0xD4, 0x02, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00>(), {}, 7},
            FramerateWriteValue::repeat_initial, runtime_image::MemoryKind::code},
        FramerateWriteContract{{FeatureId::framerate, "native keyboard repeat next duration",
            VersionedOperationKind::byte_patch, 0x0005F84D, 10, PatternOf<0xC7, 0x86, 0xD8, 0x02, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00>(), {}, 8},
            FramerateWriteValue::repeat_next, runtime_image::MemoryKind::code},
        FramerateWriteContract{{FeatureId::framerate, "gameplay countdown duration",
            VersionedOperationKind::byte_patch, 0x002645EE, 10, PatternOf<0xC7, 0x80, 0x14, 0x1D, 0x00, 0x00, 0x78, 0x00, 0x00, 0x00>(), {}, 9},
            FramerateWriteValue::two_seconds, runtime_image::MemoryKind::code},
        FramerateWriteContract{{FeatureId::framerate, "render EAX countdown duration",
            VersionedOperationKind::byte_patch, 0x00249A5E, 5, PatternOf<0xB8, 0x78, 0x00, 0x00, 0x00>(), {}, 10},
            FramerateWriteValue::two_seconds, runtime_image::MemoryKind::code},
        FramerateWriteContract{{FeatureId::framerate, "render EDX countdown duration",
            VersionedOperationKind::byte_patch, 0x00249A73, 5, PatternOf<0xBA, 0x78, 0x00, 0x00, 0x00>(), {}, 11},
            FramerateWriteValue::two_seconds, runtime_image::MemoryKind::code},
        FramerateWriteContract{{FeatureId::framerate, "palette normalizer operand one",
            VersionedOperationKind::byte_patch, 0x0022BACF, 6, PatternOf<0xD8, 0x2D, 0xAC, 0xBB, 0x6F, 0x00>(), {}, 12},
            FramerateWriteValue::target_operand, runtime_image::MemoryKind::code},
        FramerateWriteContract{{FeatureId::framerate, "palette normalizer operand two",
            VersionedOperationKind::byte_patch, 0x0022BAD5, 6, PatternOf<0xD8, 0x35, 0xAC, 0xBB, 0x6F, 0x00>(), {}, 13},
            FramerateWriteValue::target_operand, runtime_image::MemoryKind::code},
        FramerateWriteContract{{FeatureId::framerate, "chart seconds-to-frames operand",
            VersionedOperationKind::byte_patch, 0x00262CB6, 6, PatternOf<0xD8, 0x0D, 0xAC, 0xBB, 0x6F, 0x00>(), {}, 14},
            FramerateWriteValue::target_operand, runtime_image::MemoryKind::code},
        FramerateWriteContract{{FeatureId::framerate, "non-song menu repeat initial duration",
            VersionedOperationKind::byte_patch, 0x00382CE8, 4, PatternOf<0x10, 0x00, 0x00, 0x00>(), {}, 15},
            FramerateWriteValue::menu_initial, runtime_image::MemoryKind::data},
        FramerateWriteContract{{FeatureId::framerate, "non-song menu repeat interval",
            VersionedOperationKind::byte_patch, 0x00382CEC, 4, PatternOf<0x03, 0x00, 0x00, 0x00>(), {}, 16},
            FramerateWriteValue::menu_interval, runtime_image::MemoryKind::data}
    }, {
        FramerateHookContract{FramerateHookId::MovieClipGoto,
            {FeatureId::framerate, "MovieClipGoto", VersionedOperationKind::inline_hook,
             0x000DEA30, 7, PatternOf<0x6A, 0xFF, 0x68, 0xC9, 0x38, 0x67, 0x00>(), {}, 17}},
        FramerateHookContract{FramerateHookId::MovieClipAdvance,
            {FeatureId::framerate, "MovieClipAdvance", VersionedOperationKind::inline_hook,
             0x000DF940, 5, PatternOf<0x56, 0x8B, 0xF1, 0x8B, 0x06, 0x8B, 0x90, 0x4C, 0x01, 0x00, 0x00>(), {}, 18}},
        FramerateHookContract{FramerateHookId::PaletteCompare,
            {FeatureId::framerate, "PaletteCompare", VersionedOperationKind::mid_hook,
             0x0022BA60, 6, PatternOf<0x83, 0x78, 0x0C, 0x3C, 0x7D, 0x0F>(), {}, 19}},
        FramerateHookContract{FramerateHookId::StageClipFrame,
            {FeatureId::framerate, "StageClipFrame", VersionedOperationKind::mid_hook,
             0x00244054, 9, PatternOf<0x89, 0x4D, 0xF8, 0x8B, 0x95, 0x80, 0xFE, 0xFF, 0xFF>(), {}, 20}},
        FramerateHookContract{FramerateHookId::IfblWait,
            {FeatureId::framerate, "IfblWait", VersionedOperationKind::mid_hook,
             0x002309D4, 6, PatternOf<0x89, 0x4A, 0x3C, 0x8B, 0x55, 0xF8>(), {}, 21}},
        FramerateHookContract{FramerateHookId::StageBgmPreload,
            {FeatureId::framerate, "StageBgmPreload", VersionedOperationKind::mid_hook,
             0x0021001A, 6, PatternOf<0x83, 0xC0, 0x01, 0x8B, 0x4D, 0xF8>(), {}, 22}},
        FramerateHookContract{FramerateHookId::TuneCountdownCompare,
            {FeatureId::framerate, "TuneCountdownCompare", VersionedOperationKind::mid_hook,
             0x002648F7, 7, PatternOf<0x83, 0xBA, 0x14, 0x1D, 0x00, 0x00, 0x78>(), {}, 23}},
        FramerateHookContract{FramerateHookId::AudioSkipMargin,
            {FeatureId::framerate, "AudioSkipMargin", VersionedOperationKind::mid_hook,
             0x0024018F, 6, PatternOf<0x8B, 0x45, 0xF4, 0x99, 0x33, 0xC2>(), {}, 24}},
        FramerateHookContract{FramerateHookId::AudioSkipInterval,
            {FeatureId::framerate, "AudioSkipInterval", VersionedOperationKind::mid_hook,
             0x002401BD, 5, PatternOf<0xF7, 0x79, 0x3C, 0x85, 0xD2>(), {}, 25}},
        FramerateHookContract{FramerateHookId::AudioResyncPolicy,
            {FeatureId::framerate, "AudioResyncPolicy", VersionedOperationKind::mid_hook,
             0x002401C4, 9, PatternOf<0x8B, 0x55, 0xF8, 0x52, 0xE8, 0x33, 0x02, 0xFD, 0xFF, 0x8B, 0xC8, 0xE8, 0x2C, 0x12, 0xFD, 0xFF, 0x5E, 0x8B, 0xE5, 0x5D, 0xC3>(), {}, 26}},
        FramerateHookContract{FramerateHookId::GameplaySongClock,
            {FeatureId::framerate, "GameplaySongClock", VersionedOperationKind::mid_hook,
             0x00264DB2, 5, PatternOf<0xE8, 0xB9, 0xB2, 0xFD, 0xFF>(), {}, 27}},
        FramerateHookContract{FramerateHookId::GameplayEffectAdvance,
            {FeatureId::framerate, "GameplayEffectAdvance", VersionedOperationKind::mid_hook,
             0x00264E2D, 5, PatternOf<0xE8, 0x6E, 0xBA, 0xF8, 0xFF>(), {}, 28}},
        FramerateHookContract{FramerateHookId::EffectCadence6,
            {FeatureId::framerate, "EffectCadence6", VersionedOperationKind::mid_hook,
             0x0024063B, 8, PatternOf<0x85, 0xD2, 0x0F, 0x85, 0x36, 0x02, 0x00, 0x00>(), {}, 29}},
        FramerateHookContract{FramerateHookId::EffectCadence5,
            {FeatureId::framerate, "EffectCadence5", VersionedOperationKind::mid_hook,
             0x002408D7, 8, PatternOf<0x85, 0xD2, 0x0F, 0x85, 0x29, 0x03, 0x00, 0x00>(), {}, 30}},
        FramerateHookContract{FramerateHookId::EffectCadence4,
            {FeatureId::framerate, "EffectCadence4", VersionedOperationKind::mid_hook,
             0x00240C9C, 8, PatternOf<0x85, 0xD2, 0x0F, 0x85, 0xB3, 0x01, 0x00, 0x00>(), {}, 31}},
        FramerateHookContract{FramerateHookId::EffectCadence16A,
            {FeatureId::framerate, "EffectCadence16A", VersionedOperationKind::mid_hook,
             0x00241213, 11, PatternOf<0x85, 0xD2, 0x74, 0x59, 0x83, 0xBD, 0x00, 0xFE, 0xFF, 0xFF, 0x01>(), {}, 32}},
        FramerateHookContract{FramerateHookId::EffectCadence16B,
            {FeatureId::framerate, "EffectCadence16B", VersionedOperationKind::mid_hook,
             0x0024122F, 6, PatternOf<0x81, 0xE1, 0x0F, 0x00, 0x00, 0x80>(), {}, 33}},
        FramerateHookContract{FramerateHookId::EffectCadence8,
            {FeatureId::framerate, "EffectCadence8", VersionedOperationKind::mid_hook,
             0x00241268, 8, PatternOf<0x85, 0xC0, 0x0F, 0x85, 0xF7, 0x04, 0x00, 0x00>(), {}, 34}},
        FramerateHookContract{FramerateHookId::RemoteCadenceA,
            {FeatureId::framerate, "RemoteCadenceA", VersionedOperationKind::mid_hook,
             0x002632DB, 8, PatternOf<0x85, 0xD2, 0x0F, 0x85, 0x42, 0x01, 0x00, 0x00>(), {}, 35}},
        FramerateHookContract{FramerateHookId::RemoteCadenceB,
            {FeatureId::framerate, "RemoteCadenceB", VersionedOperationKind::mid_hook,
             0x00263646, 8, PatternOf<0x85, 0xD2, 0x0F, 0x85, 0xE9, 0x00, 0x00, 0x00>(), {}, 36}},
        FramerateHookContract{FramerateHookId::GameplayBlink,
            {FeatureId::framerate, "GameplayBlink", VersionedOperationKind::mid_hook,
             0x0024A1B9, 7, PatternOf<0xD1, 0xF8, 0x25, 0x01, 0x00, 0x00, 0x80>(), {}, 37}},
        FramerateHookContract{FramerateHookId::GreatGoodLifetimeOperand,
            {FeatureId::framerate, "GreatGoodLifetimeOperand", VersionedOperationKind::mid_hook,
             0x002464A8, 5, PatternOf<0xD8, 0x48, 0x18, 0xDE, 0xC1>(), {}, 38}},
        FramerateHookContract{FramerateHookId::GreatGoodFrameOperand,
            {FeatureId::framerate, "GreatGoodFrameOperand", VersionedOperationKind::mid_hook,
             0x00246528, 8, PatternOf<0xD8, 0x71, 0x18, 0xE8, 0xF0, 0x3A, 0xEC, 0xFF>(), {}, 39}},
        FramerateHookContract{FramerateHookId::EffectLifetimeAOperand,
            {FeatureId::framerate, "EffectLifetimeAOperand", VersionedOperationKind::mid_hook,
             0x00248F00, 5, PatternOf<0xD8, 0x49, 0x18, 0xDE, 0xC1>(), {}, 40}},
        FramerateHookContract{FramerateHookId::EffectFrameAOperand,
            {FeatureId::framerate, "EffectFrameAOperand", VersionedOperationKind::mid_hook,
             0x00248F8C, 8, PatternOf<0xD8, 0x72, 0x18, 0xE8, 0x8C, 0x10, 0xEC, 0xFF>(), {}, 41}},
        FramerateHookContract{FramerateHookId::EffectLifetimeBOperand,
            {FeatureId::framerate, "EffectLifetimeBOperand", VersionedOperationKind::mid_hook,
             0x0024912B, 5, PatternOf<0xD8, 0x49, 0x18, 0xDE, 0xC1>(), {}, 42}},
        FramerateHookContract{FramerateHookId::EffectFrameBOperand,
            {FeatureId::framerate, "EffectFrameBOperand", VersionedOperationKind::mid_hook,
             0x002491E0, 6, PatternOf<0xD8, 0x72, 0x18, 0xD9, 0x5D, 0xA0>(), {}, 43}},
        FramerateHookContract{FramerateHookId::DirectEffectFrameOperand,
            {FeatureId::framerate, "DirectEffectFrameOperand", VersionedOperationKind::mid_hook,
             0x00249C14, 8, PatternOf<0xD8, 0x72, 0x18, 0xE8, 0x04, 0x04, 0xEC, 0xFF>(), {}, 44}},
        FramerateHookContract{FramerateHookId::ChartEffectFrameAOperand,
            {FeatureId::framerate, "ChartEffectFrameAOperand", VersionedOperationKind::mid_hook,
             0x0024BC8B, 8, PatternOf<0xD8, 0x71, 0x18, 0xE8, 0x8D, 0xE3, 0xEB, 0xFF>(), {}, 45}},
        FramerateHookContract{FramerateHookId::ChartEffectFrameBOperand,
            {FeatureId::framerate, "ChartEffectFrameBOperand", VersionedOperationKind::mid_hook,
             0x0024CC8A, 8, PatternOf<0xD8, 0x71, 0x18, 0xE8, 0x8E, 0xD3, 0xEB, 0xFF>(), {}, 46}},
        FramerateHookContract{FramerateHookId::ChartEffectFrameCOperand,
            {FeatureId::framerate, "ChartEffectFrameCOperand", VersionedOperationKind::mid_hook,
             0x0024CCBE, 8, PatternOf<0xD8, 0x72, 0x18, 0xE8, 0x5A, 0xD3, 0xEB, 0xFF>(), {}, 47}},
        FramerateHookContract{FramerateHookId::ChartEffectFrameDOperand,
            {FeatureId::framerate, "ChartEffectFrameDOperand", VersionedOperationKind::mid_hook,
             0x0024D836, 8, PatternOf<0xD8, 0x70, 0x18, 0xE8, 0xE2, 0xC7, 0xEB, 0xFF>(), {}, 48}},
        FramerateHookContract{FramerateHookId::FixedVisualFrameOperand,
            {FeatureId::framerate, "FixedVisualFrameOperand", VersionedOperationKind::mid_hook,
             0x00250AD5, 8, PatternOf<0xD8, 0x71, 0x18, 0xE8, 0x43, 0x95, 0xEB, 0xFF>(), {}, 49}},
        FramerateHookContract{FramerateHookId::GameplayCountdownAssetFrame,
            {FeatureId::framerate, "GameplayCountdownAssetFrame", VersionedOperationKind::mid_hook,
             0x00249A9C, 6, PatternOf<0x89, 0x48, 0x08, 0x51, 0xD9, 0xEE>(), {}, 50}},
        FramerateHookContract{FramerateHookId::PlayerPositionInitA,
            {FeatureId::framerate, "PlayerPositionInitA", VersionedOperationKind::mid_hook,
             0x00263240, 7, PatternOf<0x89, 0x84, 0x91, 0x54, 0x1D, 0x00, 0x00>(), {}, 51}},
        FramerateHookContract{FramerateHookId::PlayerPositionInitB,
            {FeatureId::framerate, "PlayerPositionInitB", VersionedOperationKind::mid_hook,
             0x002632B2, 7, PatternOf<0x89, 0x84, 0x8A, 0x54, 0x1D, 0x00, 0x00>(), {}, 52}},
        FramerateHookContract{FramerateHookId::PlayerPositionInitC,
            {FeatureId::framerate, "PlayerPositionInitC", VersionedOperationKind::mid_hook,
             0x0026359B, 7, PatternOf<0x89, 0x84, 0x8A, 0x54, 0x1D, 0x00, 0x00>(), {}, 53}},
        FramerateHookContract{FramerateHookId::PlayerPositionInitD,
            {FeatureId::framerate, "PlayerPositionInitD", VersionedOperationKind::mid_hook,
             0x00263615, 7, PatternOf<0x89, 0x84, 0x8A, 0x54, 0x1D, 0x00, 0x00>(), {}, 54}},
        FramerateHookContract{FramerateHookId::PlayerPositionAssetFrame,
            {FeatureId::framerate, "PlayerPositionAssetFrame", VersionedOperationKind::mid_hook,
             0x0024EF43, 7, PatternOf<0x2B, 0x84, 0x8A, 0x54, 0x1D, 0x00, 0x00>(), {}, 55}},
        FramerateHookContract{FramerateHookId::PlayerPositionDenominatorA,
            {FeatureId::framerate, "PlayerPositionDenominatorA", VersionedOperationKind::mid_hook,
             0x0024F76D, 6, PatternOf<0xDB, 0x80, 0xC4, 0x00, 0x00, 0x00>(), {}, 56}},
        FramerateHookContract{FramerateHookId::PlayerPositionDenominatorB,
            {FeatureId::framerate, "PlayerPositionDenominatorB", VersionedOperationKind::mid_hook,
             0x0024FD40, 6, PatternOf<0xDB, 0x80, 0xC4, 0x00, 0x00, 0x00>(), {}, 57}},
        FramerateHookContract{FramerateHookId::EffectFlowItemFrame,
            {FeatureId::framerate, "EffectFlowItemFrame", VersionedOperationKind::mid_hook,
             0x001F0310, 6, PatternOf<0x89, 0x42, 0x08, 0x8B, 0x45, 0xF8>(), {}, 58}},
        FramerateHookContract{FramerateHookId::EffectTutorialElapsed,
            {FeatureId::framerate, "EffectTutorialElapsed", VersionedOperationKind::mid_hook,
             0x00249593, 6, PatternOf<0x89, 0x95, 0x74, 0xFF, 0xFF, 0xFF>(), {}, 59}},
        FramerateHookContract{FramerateHookId::EffectChartPreRollDuration,
            {FeatureId::framerate, "EffectChartPreRollDuration", VersionedOperationKind::mid_hook,
             0x0024A934, 6, PatternOf<0x89, 0x45, 0x9C, 0x8B, 0x45, 0x98>(), {}, 60}},
        FramerateHookContract{FramerateHookId::EffectPlayerModuloDividend,
            {FeatureId::framerate, "EffectPlayerModuloDividend", VersionedOperationKind::mid_hook,
             0x0025072E, 8, PatternOf<0xF7, 0xF9, 0x8B, 0x85, 0xDC, 0xFC, 0xFF, 0xFF>(), {}, 61}},
        FramerateHookContract{FramerateHookId::MovieClipPreprocessVisit,
            {FeatureId::framerate, "MovieClipPreprocessVisit", VersionedOperationKind::inline_hook,
             0x000EFB90, 7, PatternOf<0x6A, 0xFF, 0x68, 0x10, 0x49, 0x67, 0x00>(), {}, 62}},
        FramerateHookContract{FramerateHookId::RankingEntryCounterStore,
            {FeatureId::framerate, "RankingEntryCounterStore", VersionedOperationKind::mid_hook,
             0x00216EB4, 5, PatternOf<0x8B, 0x4D, 0xE0, 0x89, 0x01>(), {}, 63}},
        FramerateHookContract{FramerateHookId::HitChartEntryCounterStore,
            {FeatureId::framerate, "HitChartEntryCounterStore", VersionedOperationKind::mid_hook,
             0x0026562F, 6, PatternOf<0x8B, 0x8D, 0x6C, 0xFF, 0xFF, 0xFF>(), {}, 64}},
        FramerateHookContract{FramerateHookId::UnlockRewardCountdownStore,
            {FeatureId::framerate, "UnlockRewardCountdownStore", VersionedOperationKind::mid_hook,
             0x00030DA3, 6, PatternOf<0x89, 0x90, 0x6C, 0x37, 0x00, 0x00>(), {}, 65}},
        FramerateHookContract{FramerateHookId::UnlockRewardPrimaryStateStore,
            {FeatureId::framerate, "UnlockRewardPrimaryStateStore", VersionedOperationKind::mid_hook,
             0x00030E54, 6, PatternOf<0x89, 0x81, 0xD4, 0x37, 0x00, 0x00>(), {}, 66}},
        FramerateHookContract{FramerateHookId::UnlockRewardSecondaryStateStore,
            {FeatureId::framerate, "UnlockRewardSecondaryStateStore", VersionedOperationKind::mid_hook,
             0x00030F23, 6, PatternOf<0x89, 0x90, 0xD4, 0x37, 0x00, 0x00>(), {}, 67}},
        FramerateHookContract{FramerateHookId::NavigatorAdvance,
            {FeatureId::framerate, "NavigatorAdvance", VersionedOperationKind::inline_hook,
             0x001B6310, 6, PatternOf<0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x08, 0x89, 0x4D, 0xFC, 0x8B, 0x45, 0xFC, 0x8B, 0x48, 0x60>(), {}, 68}},
        FramerateHookContract{FramerateHookId::OuterFrame,
            {FeatureId::framerate, "OuterFrame", VersionedOperationKind::mid_hook,
             0x00058B70, 5, PatternOf<0x56, 0x8B, 0xF1, 0x8B, 0x06, 0x8B, 0x50, 0x24>(), {}, 69}}
    }, {
        FramerateTargetContract{FramerateNativeTarget::audio_resync_epilogue,
            {FeatureId::framerate, "audio_resync_epilogue", VersionedOperationKind::read_only_contract,
             0x002401D4, 5, PatternOf<0x5E, 0x8B, 0xE5, 0x5D, 0xC3>(), {}, 0,
             SiteDisposition::verify_only}},
        FramerateTargetContract{FramerateNativeTarget::get_sound_manager,
            {FeatureId::framerate, "get_sound_manager", VersionedOperationKind::read_only_contract,
             0x00210400, 8, PatternOf<0x55, 0x8B, 0xEC, 0xA1, 0x9C, 0x24, 0x7F, 0x00>(), {}, 0,
             SiteDisposition::verify_only}},
        FramerateTargetContract{FramerateNativeTarget::get_group_cursor,
            {FeatureId::framerate, "get_group_cursor", VersionedOperationKind::read_only_contract,
             0x002122B0, 10, PatternOf<0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68, 0x9B, 0x8D, 0x67, 0x00>(), {}, 0,
             SiteDisposition::verify_only}},
        FramerateTargetContract{FramerateNativeTarget::get_config,
            {FeatureId::framerate, "get_config", VersionedOperationKind::read_only_contract,
             0x000011E0, 8, PatternOf<0x55, 0x8B, 0xEC, 0xE8, 0xE8, 0xFF, 0xFF, 0xFF>(), {}, 0,
             SiteDisposition::verify_only}},
        FramerateTargetContract{FramerateNativeTarget::advance_gameplay_effect,
            {FeatureId::framerate, "advance_gameplay_effect", VersionedOperationKind::read_only_contract,
             0x001F08A0, 9, PatternOf<0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x10, 0x89, 0x4D, 0xF0>(), {}, 0,
             SiteDisposition::verify_only}},
        FramerateTargetContract{FramerateNativeTarget::ranking_resume,
            {FeatureId::framerate, "ranking_resume", VersionedOperationKind::read_only_contract,
             0x00216EB9, 10, PatternOf<0xE9, 0xCA, 0xFD, 0xFF, 0xFF, 0xE8, 0xED, 0xFB, 0xF4, 0xFF>(), {}, 0,
             SiteDisposition::verify_only}},
        FramerateTargetContract{FramerateNativeTarget::hitchart_resume,
            {FeatureId::framerate, "hitchart_resume", VersionedOperationKind::read_only_contract,
             0x00265637, 8, PatternOf<0x8D, 0x4D, 0xA4, 0xE8, 0x21, 0xC0, 0xDC, 0xFF>(), {}, 0,
             SiteDisposition::verify_only}},
        FramerateTargetContract{FramerateNativeTarget::unlock_countdown_resume,
            {FeatureId::framerate, "unlock_countdown_resume", VersionedOperationKind::read_only_contract,
             0x00030DA9, 10, PatternOf<0x8B, 0x4D, 0xDC, 0x83, 0xB9, 0x6C, 0x37, 0x00, 0x00, 0x00>(), {}, 0,
             SiteDisposition::verify_only}},
        FramerateTargetContract{FramerateNativeTarget::unlock_primary_resume,
            {FeatureId::framerate, "unlock_primary_resume", VersionedOperationKind::read_only_contract,
             0x00030E5A, 10, PatternOf<0x8B, 0x55, 0xDC, 0x83, 0xBA, 0xD4, 0x37, 0x00, 0x00, 0x1F>(), {}, 0,
             SiteDisposition::verify_only}},
        FramerateTargetContract{FramerateNativeTarget::unlock_secondary_resume,
            {FeatureId::framerate, "unlock_secondary_resume", VersionedOperationKind::read_only_contract,
             0x00030F29, 9, PatternOf<0x8B, 0x4D, 0xDC, 0x81, 0xC1, 0x70, 0x37, 0x00, 0x00>(), {}, 0,
             SiteDisposition::verify_only}}
    }, {
        .tune_current_tick = 0x10,
        .tune_step = 0x14,
        .game_time_offset = 0x2C,
        .gameplay_sound_group = 2,
        .judgement_tune_stack = -0x32C,
        .semantic_tune_stack = -0x2B4,
        .remote_phase_stack = -0x1FC,
        .palette_counter = 0x0C,
        .ifbl_wait = 0x3C,
        .tune_countdown = 0x1D14,
        .audio_margin_stack = -0x24,
        .audio_drift_stack = -0x0C,
        .audio_interval = 0x3C,
        .movieclip_stop_flag = 0x11C,
        .movieclip_instance_name = 0x120,
        .movieclip_instance_hash = 0x140,
        .movieclip_owner = 0x150,
        .movieclip_frame_low = 0x178,
        .movieclip_frame_high = 0x17C,
        .player_position_remaining = 0x1D54,
        .player_position_duration = 0xC4,
        .palette_skip = 4,
        .ifbl_skip = 3,
        .bgm_preload_skip = 3,
        .countdown_compare_skip = 7,
        .audio_interval_skip = 3,
        .song_clock_skip = 5,
        .effect_advance_skip = 5,
        .player_position_skip = 7,
    }, SummarizeNativeEffectTiming()};
}
}


[[nodiscard]] std::expected<game_version::VersionedOperation, game_version::PlanError>
BindFramerateHook(const FramerateHookContract& contract) noexcept {
    using namespace game_version;
    using namespace detail;
    switch (contract.id) {
    case FramerateHookId::MovieClipGoto:
        return InlineHookOperation{contract.site, reinterpret_cast<void*>(HookMovieClipGoto),
            hooking::OriginalPublisher::To(&g_frame_originals.movieclip_goto)};
    case FramerateHookId::MovieClipAdvance:
        return InlineHookOperation{contract.site, reinterpret_cast<void*>(HookMovieClipAdvance),
            hooking::OriginalPublisher::To(&g_frame_originals.movieclip_advance)};
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
            hooking::OriginalPublisher::To(&g_menu_originals.movieclip_preprocess_visit)};
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
            hooking::OriginalPublisher::To(&g_effect_originals.navigator_advance)};
    case FramerateHookId::OuterFrame:
        return MidHookOperation{contract.site, HookOuterFrame};
    }
    return std::unexpected(PlanError{.stage = PlanStage::invalid_plan,
        .feature = FeatureId::framerate, .site = contract.site.site});
}

const FramerateGameProfile* ProfileFor(GameBuild build, GameImageVariant variant) noexcept {
    static const std::array profiles{
        MakeProfile(GameImageVariant::clean), MakeProfile(GameImageVariant::legacy_patched),
        MakeProfile(GameImageVariant::locally_verified)};
    for (const auto& profile : profiles)
        if (profile.build == build && profile.variant == variant) return &profile;
    return nullptr;
}
}

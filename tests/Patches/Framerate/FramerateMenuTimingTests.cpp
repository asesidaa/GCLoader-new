#include "Patches/Framerate/FramerateAuthoredClock.h"
#include "Patches/Framerate/FramerateMenuTiming.h"
#include "Patches/Framerate/FrameratePatchPlan.h"
#include "Patches/Framerate/FramerateProfile.h"

#include <safetyhook.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <iostream>
#include <optional>
#include <span>
#include <string_view>
#include <thread>
#include <utility>

namespace {

thread_local gc::framerate::MovieClipVisitTracker g_thread_visits;

int Expect(bool condition, const char* name) {
    if (condition) {
        return 0;
    }
    std::cerr << "Expectation failed: " << name << "\n";
    return 1;
}

gc::framerate::BytePattern Pattern(
    std::initializer_list<std::uint8_t> values) {
    gc::framerate::BytePattern pattern{};
    pattern.size = static_cast<std::uint8_t>(values.size());
    std::transform(
        values.begin(),
        values.end(),
        pattern.bytes.begin(),
        [](std::uint8_t value) {
            return static_cast<std::byte>(value);
        });
    return pattern;
}

safetyhook::Context CanaryContext() {
    safetyhook::Context context{};
    auto* bytes = reinterpret_cast<unsigned char*>(&context);
    for (std::size_t index = 0; index < sizeof(context); ++index) {
        bytes[index] = static_cast<unsigned char>((index % 251U) + 1U);
    }
    return context;
}

bool ContextEquals(
    const safetyhook::Context& left,
    const safetyhook::Context& right) {
    return std::memcmp(&left, &right, sizeof(left)) == 0;
}

bool ContextEqualsExceptEip(
    const safetyhook::Context& actual,
    const safetyhook::Context& original,
    std::uint32_t expected_eip) {
    if (actual.eip != expected_eip) {
        return false;
    }
    auto restored = actual;
    restored.eip = original.eip;
    return ContextEquals(restored, original);
}

bool ContainsInteriorEntry(
    std::uintptr_t hook_rva,
    std::size_t overwrite_length,
    std::uintptr_t target_rva) {
    return target_rva > hook_rva &&
        target_rva < hook_rva + overwrite_length;
}

std::uintptr_t g_expected_menu_read_address{};
std::uint32_t g_menu_read_value{};
bool g_menu_read_succeeds{};

bool StubMenuReadU32(
    std::uintptr_t address,
    std::uint32_t& value) noexcept {
    if (!g_menu_read_succeeds ||
        address != g_expected_menu_read_address) {
        return false;
    }
    value = g_menu_read_value;
    return true;
}

std::uint32_t TargetCallsForAuthoredSteps(
    std::uint32_t target_fps,
    std::uint32_t authored_steps) {
    using namespace gc::framerate;
    const auto profile = FramerateProfile::Create(target_fps).value();
    Authored60PhaseClock clock{profile};
    std::uint32_t calls = 0;
    std::uint32_t commits = 0;
    while (commits < authored_steps) {
        const bool tick = clock.Advance();
        ++calls;
        if (DecideMenuCounterStore(
                MenuTimingMode::Correct,
                tick) == MenuCounterStoreAction::Commit) {
            ++commits;
        }
    }
    return calls;
}

} // namespace

int main() {
    using namespace gc::framerate;
    int failures = 0;

    struct ExpectedMenuHook {
        FramerateHookId id;
        std::uintptr_t rva;
        BytePattern expected;
        std::string_view name;
        MenuTimingHookKind kind;
    };
    const std::array expected_menu_hooks{
        ExpectedMenuHook{
            FramerateHookId::MovieClipPreprocessVisit,
            0x000EFB90,
            Pattern({0x6A, 0xFF, 0x68, 0x10, 0x49, 0x67, 0x00}),
            "MovieClip preprocessing visitor scope",
            MenuTimingHookKind::Inline},
        ExpectedMenuHook{
            FramerateHookId::RankingEntryCounterStore,
            0x00216EB4,
            Pattern({0x8B, 0x4D, 0xE0, 0x89, 0x01}),
            "Ranking entry authored counter store",
            MenuTimingHookKind::Mid},
        ExpectedMenuHook{
            FramerateHookId::HitChartEntryCounterStore,
            0x0026562F,
            Pattern({0x8B, 0x8D, 0x6C, 0xFF, 0xFF, 0xFF}),
            "HitChart entry authored counter store",
            MenuTimingHookKind::Mid},
        ExpectedMenuHook{
            FramerateHookId::UnlockRewardCountdownStore,
            0x00030DA3,
            Pattern({0x89, 0x90, 0x6C, 0x37, 0x00, 0x00}),
            "UnlockReward countdown authored counter store",
            MenuTimingHookKind::Mid},
        ExpectedMenuHook{
            FramerateHookId::UnlockRewardPrimaryStateStore,
            0x00030E54,
            Pattern({0x89, 0x81, 0xD4, 0x37, 0x00, 0x00}),
            "UnlockReward primary-state authored counter store",
            MenuTimingHookKind::Mid},
        ExpectedMenuHook{
            FramerateHookId::UnlockRewardSecondaryStateStore,
            0x00030F23,
            Pattern({0x89, 0x90, 0xD4, 0x37, 0x00, 0x00}),
            "UnlockReward secondary-state authored counter store",
            MenuTimingHookKind::Mid},
    };
    const auto menu_hooks = FramerateMenuTimingHookSites();
    failures += Expect(
        menu_hooks.size() == expected_menu_hooks.size(),
        "menu timing manifest contains exactly six permanent hooks");
    for (std::size_t index = 0;
         index < expected_menu_hooks.size() &&
         index < menu_hooks.size();
         ++index) {
        const auto& actual = menu_hooks[index];
        const auto& expected = expected_menu_hooks[index];
        failures += Expect(
            actual.contract.id == expected.id &&
                actual.contract.rva == expected.rva &&
                actual.contract.expected == expected.expected &&
                actual.contract.name != nullptr &&
                std::string_view{actual.contract.name} == expected.name &&
                actual.kind == expected.kind,
            "menu hook has exact ID, RVA, bytes, name, and kind");
    }
    failures += Expect(
        std::none_of(
            menu_hooks.begin(),
            menu_hooks.end(),
            [](const auto& hook) {
                return hook.contract.rva == 0x000D1730;
            }),
        "temporary MovieClip Stop hook is absent");

    failures += Expect(
        ContainsInteriorEntry(
            0x00216EB7,
            7,
            0x00216EB9) &&
            ContainsInteriorEntry(
                0x00265635,
                5,
                0x00265637),
        "old short-store detours contain proven external branch targets");
    failures += Expect(
        !ContainsInteriorEntry(
            0x00216EB4,
            5,
            0x00216EB9) &&
            !ContainsInteriorEntry(
                0x0026562F,
                6,
                0x00265637),
        "relocated detours exclude proven external branch targets");

    struct ExpectedCounterGeometry {
        MenuCounterHookGeometry actual;
        std::uintptr_t hook_rva;
        std::uintptr_t suppress_resume_rva;
    };
    constexpr std::array expected_counter_geometries{
        ExpectedCounterGeometry{
            kRankingEntryCounterHookGeometry,
            0x00216EB4,
            0x00216EB9},
        ExpectedCounterGeometry{
            kHitChartEntryCounterHookGeometry,
            0x0026562F,
            0x00265637},
        ExpectedCounterGeometry{
            kUnlockRewardCountdownHookGeometry,
            0x00030DA3,
            0x00030DA9},
        ExpectedCounterGeometry{
            kUnlockRewardPrimaryHookGeometry,
            0x00030E54,
            0x00030E5A},
        ExpectedCounterGeometry{
            kUnlockRewardSecondaryHookGeometry,
            0x00030F23,
            0x00030F29},
    };
    for (const auto& expected : expected_counter_geometries) {
        failures += Expect(
            expected.actual.hook_rva == expected.hook_rva &&
                expected.actual.suppress_resume_rva ==
                    expected.suppress_resume_rva,
            "counter hook geometry has exact binding and continuation RVAs");
    }

    struct AdvanceCase {
        MenuTimingMode mode;
        MovieClipAdvanceContext context;
        bool authored_tick;
        MovieClipAdvanceAction action;
        bool preprocessing_non_tick_skip;
        bool preprocessing_forced;
    };
    constexpr std::array advance_cases{
        AdvanceCase{
            MenuTimingMode::Observe,
            MovieClipAdvanceContext::Goto,
            false,
            MovieClipAdvanceAction::ExecuteOriginal,
            false,
            false},
        AdvanceCase{
            MenuTimingMode::Observe,
            MovieClipAdvanceContext::Goto,
            true,
            MovieClipAdvanceAction::ExecuteOriginal,
            false,
            false},
        AdvanceCase{
            MenuTimingMode::Correct,
            MovieClipAdvanceContext::Goto,
            false,
            MovieClipAdvanceAction::ExecuteOriginal,
            false,
            false},
        AdvanceCase{
            MenuTimingMode::Correct,
            MovieClipAdvanceContext::Goto,
            true,
            MovieClipAdvanceAction::ExecuteOriginal,
            false,
            false},
        AdvanceCase{
            MenuTimingMode::Observe,
            MovieClipAdvanceContext::Ordinary,
            false,
            MovieClipAdvanceAction::ReturnSuccessWithoutMotion,
            false,
            false},
        AdvanceCase{
            MenuTimingMode::Observe,
            MovieClipAdvanceContext::Ordinary,
            true,
            MovieClipAdvanceAction::ExecuteOriginal,
            false,
            false},
        AdvanceCase{
            MenuTimingMode::Correct,
            MovieClipAdvanceContext::Ordinary,
            false,
            MovieClipAdvanceAction::ReturnSuccessWithoutMotion,
            false,
            false},
        AdvanceCase{
            MenuTimingMode::Correct,
            MovieClipAdvanceContext::Ordinary,
            true,
            MovieClipAdvanceAction::ExecuteOriginal,
            false,
            false},
        AdvanceCase{
            MenuTimingMode::Observe,
            MovieClipAdvanceContext::Preprocess,
            false,
            MovieClipAdvanceAction::ReturnSuccessWithoutMotion,
            true,
            false},
        AdvanceCase{
            MenuTimingMode::Observe,
            MovieClipAdvanceContext::Preprocess,
            true,
            MovieClipAdvanceAction::ExecuteOriginal,
            false,
            false},
        AdvanceCase{
            MenuTimingMode::Correct,
            MovieClipAdvanceContext::Preprocess,
            false,
            MovieClipAdvanceAction::ExecuteOriginal,
            false,
            true},
        AdvanceCase{
            MenuTimingMode::Correct,
            MovieClipAdvanceContext::Preprocess,
            true,
            MovieClipAdvanceAction::ExecuteOriginal,
            false,
            false},
    };
    for (const auto& test : advance_cases) {
        const auto decision = DecideMovieClipAdvance(
            test.mode,
            test.context,
            test.authored_tick);
        failures += Expect(
            decision.action == test.action &&
                decision.preprocessing_non_tick_skip ==
                    test.preprocessing_non_tick_skip &&
                decision.preprocessing_forced ==
                    test.preprocessing_forced,
            "MovieClip advance policy matches the complete decision matrix");
    }

    struct DiagnosticIdentityCase {
        std::uint32_t instance_name_hash;
        std::string_view instance_name;
        std::uint32_t parent_name_hash;
        std::string_view parent_name;
        MovieClipDiagnosticTarget expected;
    };
    constexpr std::array diagnostic_identity_cases{
        DiagnosticIdentityCase{
            kStampCardNameHash,
            "imc_scard",
            0,
            "",
            MovieClipDiagnosticTarget::StampCard},
        DiagnosticIdentityCase{
            kStampWindowNameHash,
            "imc_window",
            0,
            "",
            MovieClipDiagnosticTarget::StampWindow},
        DiagnosticIdentityCase{
            kUnlockRewardPromptTransitionNameHash,
            "imc_tx",
            kUnlockRewardNavigatorNameHash,
            "imc_un_navi",
            MovieClipDiagnosticTarget::UnlockPromptTransition},
        DiagnosticIdentityCase{
            kUnlockRewardPromptStableNameHash,
            "igr_un_instmsg01_img",
            kUnlockRewardNavigatorNameHash,
            "imc_un_navi",
            MovieClipDiagnosticTarget::UnlockPromptStable},
        DiagnosticIdentityCase{
            kUnlockRewardPromptTransitionNameHash,
            "imc_tx",
            0x11111111,
            "wrong_parent",
            MovieClipDiagnosticTarget::None},
        DiagnosticIdentityCase{
            kStampCardNameHash,
            "imc_window",
            0,
            "",
            MovieClipDiagnosticTarget::None},
        DiagnosticIdentityCase{
            0x22222222,
            "unrelated",
            0,
            "",
            MovieClipDiagnosticTarget::None},
    };
    for (const auto& test : diagnostic_identity_cases) {
        failures += Expect(
            ClassifyMovieClipDiagnosticTarget(
                test.instance_name_hash,
                test.instance_name,
                test.parent_name_hash,
                test.parent_name) == test.expected,
            "tracked MovieClip diagnostics require exact hash/name identity");
    }
    failures += Expect(
        kMovieClipDefinitionOffset == 0x118 &&
            kMovieClipStopFlagOffset == 0x11C &&
            kMovieClipInstanceNameOffset == 0x120 &&
            kMovieClipInstanceNameHashOffset == 0x140 &&
            kMovieClipOwnerOffset == 0x150 &&
            kMovieClipCurrentFrameLowOffset == 0x178 &&
            kMovieClipCurrentFrameHighOffset == 0x17C,
        "binary-proven MovieClip layout keeps diagnostic ownership at +0x150");
    failures += Expect(
        ClassifyMovieClipDiagnosticCandidate(
            kUnlockRewardPromptTransitionNameHash,
            "") == MovieClipDiagnosticTarget::UnlockPromptTransition &&
            ClassifyMovieClipDiagnosticCandidate(
                kUnlockRewardPromptStableNameHash,
                "") == MovieClipDiagnosticTarget::UnlockPromptStable &&
            ClassifyMovieClipDiagnosticCandidate(
                kUnlockRewardPromptTransitionNameHash,
                "unreadable_or_stale") ==
                MovieClipDiagnosticTarget::UnlockPromptTransition &&
            ClassifyMovieClipDiagnosticCandidate(
                0x22222222,
                "imc_tx") == MovieClipDiagnosticTarget::None,
        "UnlockReward diagnostics survive missing names and owner links by hash");
    failures += Expect(
        MovieClipDiagnosticTargetName(
            MovieClipDiagnosticTarget::StampCard) == "stamp_scard" &&
            MovieClipDiagnosticTargetName(
                MovieClipDiagnosticTarget::StampWindow) ==
                "stamp_window" &&
            MovieClipDiagnosticTargetName(
                MovieClipDiagnosticTarget::UnlockPromptTransition) ==
                "unlock_transition" &&
            MovieClipDiagnosticTargetName(
                MovieClipDiagnosticTarget::UnlockPromptStable) ==
                "unlock_stable" &&
            MovieClipDiagnosticTargetName(
                MovieClipDiagnosticTarget::None) == "none",
        "tracked MovieClip diagnostics expose stable log names");

    struct UnlockPromptHoldCase {
        MenuTimingMode mode;
        MovieClipAdvanceContext context;
        std::uint32_t instance_name_hash;
        std::string_view instance_name;
        std::uint32_t parent_name_hash;
        std::string_view parent_name;
        std::uint64_t current_frame;
        std::uint32_t stopped;
        bool should_hold;
    };
    constexpr std::array unlock_prompt_hold_cases{
        UnlockPromptHoldCase{
            MenuTimingMode::Correct,
            MovieClipAdvanceContext::Ordinary,
            0xFCDA0604,
            "imc_tx",
            0x59FE24C8,
            "imc_un_navi",
            1,
            0,
            true},
        UnlockPromptHoldCase{
            MenuTimingMode::Correct,
            MovieClipAdvanceContext::Ordinary,
            0x9D55AF65,
            "igr_un_instmsg01_img",
            0x59FE24C8,
            "imc_un_navi",
            1,
            0,
            true},
        UnlockPromptHoldCase{
            MenuTimingMode::Correct,
            MovieClipAdvanceContext::Ordinary,
            0xFCDA0604,
            "imc_tx",
            0x59FE24C8,
            "imc_un_navi",
            2,
            0,
            false},
        UnlockPromptHoldCase{
            MenuTimingMode::Correct,
            MovieClipAdvanceContext::Ordinary,
            0x9D55AF65,
            "igr_un_instmsg01_img",
            0x59FE24C8,
            "imc_un_navi",
            1,
            1,
            false},
        UnlockPromptHoldCase{
            MenuTimingMode::Observe,
            MovieClipAdvanceContext::Ordinary,
            0xFCDA0604,
            "imc_tx",
            0x59FE24C8,
            "imc_un_navi",
            1,
            0,
            false},
        UnlockPromptHoldCase{
            MenuTimingMode::Correct,
            MovieClipAdvanceContext::Goto,
            0xFCDA0604,
            "imc_tx",
            0x59FE24C8,
            "imc_un_navi",
            1,
            0,
            false},
        UnlockPromptHoldCase{
            MenuTimingMode::Correct,
            MovieClipAdvanceContext::Preprocess,
            0x9D55AF65,
            "igr_un_instmsg01_img",
            0x59FE24C8,
            "imc_un_navi",
            1,
            0,
            false},
        UnlockPromptHoldCase{
            MenuTimingMode::Correct,
            MovieClipAdvanceContext::Ordinary,
            0xFCDA0604,
            "imc_tx",
            0x59FE24C8,
            "imc_other_navi",
            1,
            0,
            false},
        UnlockPromptHoldCase{
            MenuTimingMode::Correct,
            MovieClipAdvanceContext::Ordinary,
            0xFCDA0604,
            "imc_tx_other",
            0x59FE24C8,
            "imc_un_navi",
            1,
            0,
            false},
        UnlockPromptHoldCase{
            MenuTimingMode::Correct,
            MovieClipAdvanceContext::Ordinary,
            0x9D55AF65,
            "igr_un_instmsg01_img",
            0x59FE24C8,
            "imc_un_navi_other",
            1,
            0,
            false},
        UnlockPromptHoldCase{
            MenuTimingMode::Correct,
            MovieClipAdvanceContext::Ordinary,
            0,
            "imc_tx",
            0x59FE24C8,
            "imc_un_navi",
            1,
            0,
            false},
        UnlockPromptHoldCase{
            MenuTimingMode::Correct,
            MovieClipAdvanceContext::Ordinary,
            0xFCDA0604,
            "imc_tx",
            0,
            "imc_un_navi",
            1,
            0,
            false},
    };
    for (const auto& test : unlock_prompt_hold_cases) {
        failures += Expect(
            ShouldHoldUnlockRewardPromptFrame(
                test.mode,
                test.context,
                test.instance_name_hash,
                test.instance_name,
                test.parent_name_hash,
                test.parent_name,
                test.current_frame,
                test.stopped) == test.should_hold,
            "only playing exact UnlockReward prompt children already on visible frame one are held under the exact navigator during corrected ordinary playback");
    }

    failures += Expect(
        DecideMenuCounterStore(MenuTimingMode::Observe, true) ==
                MenuCounterStoreAction::Commit &&
            DecideMenuCounterStore(MenuTimingMode::Observe, false) ==
                MenuCounterStoreAction::WouldSuppress &&
            DecideMenuCounterStore(MenuTimingMode::Correct, true) ==
                MenuCounterStoreAction::Commit &&
            DecideMenuCounterStore(MenuTimingMode::Correct, false) ==
                MenuCounterStoreAction::Suppress,
        "menu counter decision distinguishes observe and correction");

    for (const std::uint32_t suppress_resume_eip :
         {0x13572468U, 0x24681357U}) {
        auto context = CanaryContext();
        const auto before = context;
        const auto action = ApplyMenuCounterStoreGate(
            context,
            MenuTimingMode::Correct,
            false,
            suppress_resume_eip);
        failures += Expect(
            action == MenuCounterStoreAction::Suppress &&
                ContextEqualsExceptEip(
                    context,
                    before,
                    suppress_resume_eip),
            "suppressed store assigns only the exact continuation EIP");
    }
    for (const auto test : {
             std::pair{MenuTimingMode::Observe, true},
             std::pair{MenuTimingMode::Observe, false},
             std::pair{MenuTimingMode::Correct, true}}) {
        auto context = CanaryContext();
        const auto before = context;
        const auto action = ApplyMenuCounterStoreGate(
            context,
            test.first,
            test.second,
            0xDEADBEEFU);
        failures += Expect(
            action != MenuCounterStoreAction::Suppress &&
                ContextEquals(context, before),
            "non-suppressed store preserves the complete x86 context");
    }

    auto ranking_context = CanaryContext();
    ranking_context.ebp = 0x10002000;
    const auto ranking_context_before = ranking_context;
    g_expected_menu_read_address = 0x10001FE0;
    g_menu_read_value = 0x12345678;
    g_menu_read_succeeds = true;
    const auto ranking_destination =
        ResolveMenuCounterDestinationFromFrame(
            ranking_context,
            -0x20,
            &StubMenuReadU32);
    failures += Expect(
        ranking_destination.has_value() &&
            *ranking_destination == 0x12345678 &&
            ContextEquals(ranking_context, ranking_context_before),
        "Ranking destination resolves from EBP-20 without context mutation");

    auto hitchart_context = CanaryContext();
    hitchart_context.ebp = 0x20003000;
    const auto hitchart_context_before = hitchart_context;
    g_expected_menu_read_address = 0x20002F6C;
    g_menu_read_value = 0x23456789;
    const auto hitchart_destination =
        ResolveMenuCounterDestinationFromFrame(
            hitchart_context,
            -0x94,
            &StubMenuReadU32);
    failures += Expect(
        hitchart_destination.has_value() &&
            *hitchart_destination == 0x23456789 &&
            ContextEquals(hitchart_context, hitchart_context_before),
        "HitChart destination resolves from EBP-94 without context mutation");

    g_expected_menu_read_address = 0x20002F6C;
    g_menu_read_succeeds = false;
    failures += Expect(
        !ResolveMenuCounterDestinationFromFrame(
             hitchart_context,
             -0x94,
             &StubMenuReadU32)
             .has_value(),
        "frame-local destination rejects a failed diagnostic read");

    g_menu_read_succeeds = true;
    g_menu_read_value = 0;
    failures += Expect(
        !ResolveMenuCounterDestinationFromFrame(
             hitchart_context,
             -0x94,
             &StubMenuReadU32)
             .has_value(),
        "frame-local destination rejects a null counter pointer");

    MovieClipPreprocessTracker preprocess;
    failures += Expect(
        preprocess.ObserveStop(0x1000, 7) ==
            PreprocessStopObservation::OutsidePreprocess,
        "Stop outside preprocessing is identified");
    {
        MovieClipPreprocessScope scope{preprocess, 0x1000, 7};
        failures += Expect(
            preprocess.active() && preprocess.depth() == 1 &&
                preprocess.ObserveStop(0x1000, 7) ==
                    PreprocessStopObservation::InPreprocess,
            "Stop in preprocessing without a skipped advance is not causal");
    }
    failures += Expect(
        !preprocess.active() && preprocess.depth() == 0,
        "RAII preprocessing scope restores depth");
    {
        MovieClipPreprocessScope scope{preprocess, 0x1000, 7};
        preprocess.RecordSkippedAdvance(0x1000, 7);
        failures += Expect(
            preprocess.ObserveStop(0x1000, 7) ==
                    PreprocessStopObservation::CausalAfterSkippedAdvance &&
                preprocess.ObserveStop(0x1000, 7) ==
                    PreprocessStopObservation::InPreprocess,
            "same-object same-epoch causal Stop is consumed once");
    }
    {
        MovieClipPreprocessScope scope{preprocess, 0x1000, 7};
        preprocess.RecordSkippedAdvance(0x1000, 7);
        failures += Expect(
            preprocess.ObserveStop(0x2000, 7) ==
                    PreprocessStopObservation::InPreprocess &&
                preprocess.ObserveStop(0x1000, 8) ==
                    PreprocessStopObservation::InPreprocess,
            "different object or epoch is not a causal Stop");
    }
    {
        MovieClipPreprocessScope outer{preprocess, 0x1000, 11};
        preprocess.RecordSkippedAdvance(0x1000, 11);
        {
            MovieClipPreprocessScope inner{preprocess, 0x2000, 11};
            preprocess.RecordSkippedAdvance(0x2000, 11);
            failures += Expect(
                preprocess.depth() == 2 &&
                    preprocess.ObserveStop(0x2000, 11) ==
                        PreprocessStopObservation::
                            CausalAfterSkippedAdvance &&
                    preprocess.ObserveStop(0x1000, 11) ==
                        PreprocessStopObservation::
                            CausalAfterSkippedAdvance,
                "nested preprocessing scopes attribute distinct MovieClips");
        }
        failures += Expect(
            preprocess.depth() == 1,
            "nested preprocessing scope restores its own depth");
    }
    {
        MovieClipPreprocessScope unattributed{preprocess, 0, 13};
        preprocess.RecordSkippedAdvance(0x3000, 13);
        failures += Expect(
            preprocess.ObserveStop(0x3000, 13) ==
                PreprocessStopObservation::CausalAfterSkippedAdvance,
            "failed pointer attribution binds to the skipped MovieClip");
    }
    for (std::size_t index = 0;
         index < MovieClipPreprocessTracker::kMaximumTrackedDepth + 1;
         ++index) {
        preprocess.Enter(0x4000 + index * 0x10, 17);
    }
    preprocess.RecordSkippedAdvance(0x4000, 17);
    failures += Expect(
        preprocess.depth() ==
                MovieClipPreprocessTracker::kMaximumTrackedDepth + 1 &&
            preprocess.ObserveStop(0x4000, 17) ==
                PreprocessStopObservation::InPreprocess,
        "overflow preprocessing depth remains active without attribution");
    for (std::size_t index = 0;
         index < MovieClipPreprocessTracker::kMaximumTrackedDepth + 1;
         ++index) {
        preprocess.Leave();
    }
    failures += Expect(
        !preprocess.active() && preprocess.depth() == 0,
        "overflow preprocessing depth is fully restored");

    MovieClipVisitTracker visits;
    failures += Expect(
        visits.Observe(0x1000, 1) == MovieClipVisitObservation{},
        "first MovieClip visit is ordinary");
    failures += Expect(
        visits.Observe(0x1000, 1).same_epoch_revisit,
        "same pointer in one epoch is a revisit");
    failures += Expect(
        !visits.Observe(0x1000, 2).same_epoch_revisit,
        "new epoch is not a revisit");
    failures += Expect(
        visits.Observe(0x5000, 2).hash_collision,
        "different pointer in an occupied same-epoch slot is a collision");

    const auto main_first_visit = g_thread_visits.Observe(0x6000, 19);
    failures += Expect(
        main_first_visit == MovieClipVisitObservation{},
        "main thread begins with an empty revisit tracker");
    std::atomic_bool worker_first_visit{false};
    std::jthread worker{[&worker_first_visit] {
        worker_first_visit.store(
            g_thread_visits.Observe(0x6000, 19) ==
                MovieClipVisitObservation{},
            std::memory_order_relaxed);
    }};
    worker.join();
    failures += Expect(
        worker_first_visit.load(std::memory_order_relaxed),
        "MovieClip revisit trackers are isolated between threads");

    for (const std::uint32_t target : {60U, 120U, 144U, 240U}) {
        const auto profile = FramerateProfile::Create(target).value();
        Authored60PhaseClock clock{profile};
        std::uint32_t commits = 0;
        for (std::uint32_t call = 0; call < target; ++call) {
            commits += DecideMenuCounterStore(
                           MenuTimingMode::Correct,
                           clock.Advance()) ==
                    MenuCounterStoreAction::Commit
                ? 1U
                : 0U;
        }
        failures += Expect(
            commits == 60,
            "corrected store policy commits exactly 60 times per second");

        for (const std::uint32_t authored_steps :
             {10U, 25U, 8U, 30U, 10U}) {
            const auto calls = TargetCallsForAuthoredSteps(
                target,
                authored_steps);
            const double actual_seconds =
                static_cast<double>(calls) / target;
            const double authored_seconds =
                static_cast<double>(authored_steps) / 60.0;
            failures += Expect(
                std::abs(actual_seconds - authored_seconds) <= 1.0 / 60.0,
                "menu transition duration stays within one authored frame");
        }
    }
    {
        const auto profile = FramerateProfile::Create(144).value();
        Authored60PhaseClock clock{profile};
        constexpr std::array expected{
            true, false, false, true, false, true,
            false, false, true, false, true, false,
        };
        for (const bool authored_tick : expected) {
            failures += Expect(
                clock.Advance() == authored_tick,
                "144 FPS phase sequence remains rational");
        }
    }

    failures += Expect(
        ActiveMenuTimingMode() == MenuTimingMode::Correct,
        "Stage B binary is unambiguously corrected");

    const auto active_preprocess = DecideMovieClipAdvance(
        ActiveMenuTimingMode(),
        MovieClipAdvanceContext::Preprocess,
        false);
    failures += Expect(
        active_preprocess.action ==
                MovieClipAdvanceAction::ExecuteOriginal &&
            active_preprocess.preprocessing_forced &&
            !active_preprocess.preprocessing_non_tick_skip,
        "active build preserves non-tick preprocessing movement");

    struct ActiveCounterCase {
        std::uintptr_t suppress_resume_eip;
        const char* expectation;
    };
    constexpr std::array active_counter_cases{
        ActiveCounterCase{
            0x00616EB9,
            "active build freezes Ranking entry state on non-ticks"},
        ActiveCounterCase{
            0x00665637,
            "active build freezes HitChart entry state on non-ticks"},
        ActiveCounterCase{
            0x00430DA9,
            "active build freezes UnlockReward countdown on non-ticks"},
        ActiveCounterCase{
            0x00430E5A,
            "active build freezes UnlockReward primary state on non-ticks"},
        ActiveCounterCase{
            0x00430F29,
            "active build freezes flashing UnlockReward text state on non-ticks"},
    };
    for (const auto& test : active_counter_cases) {
        auto context = CanaryContext();
        const auto before = context;
        const auto action = ApplyMenuCounterStoreGate(
            context,
            ActiveMenuTimingMode(),
            false,
            test.suppress_resume_eip);
        failures += Expect(
            action == MenuCounterStoreAction::Suppress &&
                ContextEqualsExceptEip(
                    context,
                    before,
                    static_cast<std::uint32_t>(
                        test.suppress_resume_eip)),
            test.expectation);
    }

    FramerateMenuRuntimeStats stats{
        .preprocessing_visits = 1,
        .preprocessing_non_tick_skips = 2,
        .preprocessing_forced = 3,
        .preprocessing_stops = 4,
        .preprocessing_causal_stops = 5,
        .movieclip_same_epoch_revisits = 6,
        .movieclip_hash_collisions = 7,
        .unlock_prompt_transition_holds = 22,
        .unlock_prompt_stable_holds = 23,
        .ranking_entry = {.commits = 8, .suppressions = 9},
        .hitchart_entry = {.commits = 10, .suppressions = 11},
        .unlock_countdown = {
            .commits = 12,
            .suppressions = 13,
            .boundaries = 14},
        .unlock_primary = {
            .commits = 15,
            .suppressions = 16,
            .boundaries = 17},
        .unlock_secondary = {
            .commits = 18,
            .suppressions = 19,
            .boundaries = 20},
        .diagnostic_read_failures = 56,
    };
    stats.movieclip_diagnostics[0] = {
        .ordinary_runs = 24,
        .ordinary_skips = 25,
        .preprocess_runs = 26,
        .preprocess_skips = 27,
        .goto_calls = 28,
        .frame_changes = 29,
        .samples_logged = 30,
        .samples_suppressed = 31,
    };
    stats.movieclip_diagnostics[1] = {
        .ordinary_runs = 32,
        .ordinary_skips = 33,
        .preprocess_runs = 34,
        .preprocess_skips = 35,
        .goto_calls = 36,
        .frame_changes = 37,
        .samples_logged = 38,
        .samples_suppressed = 39,
    };
    stats.movieclip_diagnostics[2] = {
        .ordinary_runs = 40,
        .ordinary_skips = 41,
        .preprocess_runs = 42,
        .preprocess_skips = 43,
        .goto_calls = 44,
        .frame_changes = 45,
        .samples_logged = 46,
        .samples_suppressed = 47,
    };
    stats.movieclip_diagnostics[3] = {
        .ordinary_runs = 48,
        .ordinary_skips = 49,
        .preprocess_runs = 50,
        .preprocess_skips = 51,
        .goto_calls = 52,
        .frame_changes = 53,
        .samples_logged = 54,
        .samples_suppressed = 55,
    };
    constexpr std::string_view observe_stats =
        " menu_timing_mode=observe"
        " movieclip_preprocess=1/2/3"
        " movieclip_preprocess_stop=4/5"
        " movieclip_revisit=6/7"
        " unlock_prompt_holds=22/23"
        " movieclip_diag_stamp_scard=24/25/26/27/28/29/30/31"
        " movieclip_diag_stamp_window=32/33/34/35/36/37/38/39"
        " movieclip_diag_unlock_transition=40/41/42/43/44/45/46/47"
        " movieclip_diag_unlock_stable=48/49/50/51/52/53/54/55"
        " ranking_entry=8/9"
        " hitchart_entry=10/11"
        " unlock_countdown=12/13/14"
        " unlock_state_primary=15/16/17"
        " unlock_state_secondary=18/19/20"
        " menu_diagnostic_read_failures=56";
    constexpr std::string_view correct_stats =
        " menu_timing_mode=correct"
        " movieclip_preprocess=1/2/3"
        " movieclip_preprocess_stop=4/5"
        " movieclip_revisit=6/7"
        " unlock_prompt_holds=22/23"
        " movieclip_diag_stamp_scard=24/25/26/27/28/29/30/31"
        " movieclip_diag_stamp_window=32/33/34/35/36/37/38/39"
        " movieclip_diag_unlock_transition=40/41/42/43/44/45/46/47"
        " movieclip_diag_unlock_stable=48/49/50/51/52/53/54/55"
        " ranking_entry=8/9"
        " hitchart_entry=10/11"
        " unlock_countdown=12/13/14"
        " unlock_state_primary=15/16/17"
        " unlock_state_secondary=18/19/20"
        " menu_diagnostic_read_failures=56";
    failures += Expect(
        FormatFramerateMenuRuntimeStats(
            MenuTimingMode::Observe,
            stats) == observe_stats,
        "observe formatter publishes every menu diagnostic field");
    failures += Expect(
        FormatFramerateMenuRuntimeStats(
            MenuTimingMode::Correct,
            stats) == correct_stats,
        "correct formatter changes only the mode field");

    return failures == 0 ? 0 : 1;
}

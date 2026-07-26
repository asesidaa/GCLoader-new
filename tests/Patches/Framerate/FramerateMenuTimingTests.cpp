#include "Patches/Framerate/FramerateAuthoredClock.h"
#include "Patches/Framerate/FramerateMenuTiming.h"
#include "Patches/Framerate/FrameratePatchPlan.h"
#include "Patches/Framerate/FramerateProfile.h"

#include <safetyhook.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <iostream>
#include <string_view>

namespace {

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

std::uint32_t TargetCallsForAuthoredSteps(
    std::uint32_t target_fps,
    std::uint32_t authored_steps) {
    using namespace gc::framerate;
    const auto profile = FramerateProfile::Create(target_fps).value();
    Authored60PhaseClock clock{profile};
    std::uint32_t calls = 0;
    std::uint32_t commits = 0;
    while (commits < authored_steps) {
        ++calls;
        if (DecideMenuCounterStore(clock.Advance()) ==
            MenuCounterStoreAction::Commit) {
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
        MovieClipAdvanceContext context;
        bool authored_tick;
        MovieClipAdvanceAction action;
        bool preprocessing_forced;
    };
    constexpr std::array advance_cases{
        AdvanceCase{
            MovieClipAdvanceContext::Goto,
            false,
            MovieClipAdvanceAction::ExecuteOriginal,
            false},
        AdvanceCase{
            MovieClipAdvanceContext::Goto,
            true,
            MovieClipAdvanceAction::ExecuteOriginal,
            false},
        AdvanceCase{
            MovieClipAdvanceContext::Ordinary,
            false,
            MovieClipAdvanceAction::ReturnSuccessWithoutMotion,
            false},
        AdvanceCase{
            MovieClipAdvanceContext::Ordinary,
            true,
            MovieClipAdvanceAction::ExecuteOriginal,
            false},
        AdvanceCase{
            MovieClipAdvanceContext::Preprocess,
            false,
            MovieClipAdvanceAction::ExecuteOriginal,
            true},
        AdvanceCase{
            MovieClipAdvanceContext::Preprocess,
            true,
            MovieClipAdvanceAction::ExecuteOriginal,
            false},
    };
    for (const auto& test : advance_cases) {
        const auto decision = DecideMovieClipAdvance(
            test.context,
            test.authored_tick);
        failures += Expect(
            decision.action == test.action &&
                decision.preprocessing_forced ==
                    test.preprocessing_forced,
            "permanent MovieClip policy matches the complete decision matrix");
    }

    struct UnlockPromptHoldCase {
        MovieClipAdvanceContext context;
        std::uint32_t instance_name_hash;
        std::string_view instance_name;
        std::uint32_t owner_name_hash;
        std::string_view owner_name;
        std::uint64_t current_frame;
        std::uint32_t stopped;
        bool should_hold;
    };
    constexpr std::array unlock_prompt_hold_cases{
        UnlockPromptHoldCase{
            MovieClipAdvanceContext::Ordinary,
            0xFCDA0604,
            "imc_tx",
            0x59FE24C8,
            "imc_un_navi",
            1,
            0,
            true},
        UnlockPromptHoldCase{
            MovieClipAdvanceContext::Ordinary,
            0x9D55AF65,
            "igr_un_instmsg01_img",
            0x59FE24C8,
            "imc_un_navi",
            1,
            0,
            true},
        UnlockPromptHoldCase{
            MovieClipAdvanceContext::Ordinary,
            0xFCDA0604,
            "imc_tx",
            0x59FE24C8,
            "imc_un_navi",
            2,
            0,
            false},
        UnlockPromptHoldCase{
            MovieClipAdvanceContext::Ordinary,
            0x9D55AF65,
            "igr_un_instmsg01_img",
            0x59FE24C8,
            "imc_un_navi",
            1,
            1,
            false},
        UnlockPromptHoldCase{
            MovieClipAdvanceContext::Goto,
            0xFCDA0604,
            "imc_tx",
            0x59FE24C8,
            "imc_un_navi",
            1,
            0,
            false},
        UnlockPromptHoldCase{
            MovieClipAdvanceContext::Preprocess,
            0x9D55AF65,
            "igr_un_instmsg01_img",
            0x59FE24C8,
            "imc_un_navi",
            1,
            0,
            false},
        UnlockPromptHoldCase{
            MovieClipAdvanceContext::Ordinary,
            0xFCDA0604,
            "imc_tx",
            0x59FE24C8,
            "imc_other_navi",
            1,
            0,
            false},
        UnlockPromptHoldCase{
            MovieClipAdvanceContext::Ordinary,
            0xFCDA0604,
            "imc_tx_other",
            0x59FE24C8,
            "imc_un_navi",
            1,
            0,
            false},
        UnlockPromptHoldCase{
            MovieClipAdvanceContext::Ordinary,
            0,
            "imc_tx",
            0x59FE24C8,
            "imc_un_navi",
            1,
            0,
            false},
        UnlockPromptHoldCase{
            MovieClipAdvanceContext::Ordinary,
            0x9D55AF65,
            "igr_un_instmsg01_img",
            0,
            "imc_un_navi",
            1,
            0,
            false},
    };
    for (const auto& test : unlock_prompt_hold_cases) {
        failures += Expect(
            ShouldHoldUnlockRewardPromptFrame(
                test.context,
                test.instance_name_hash,
                test.instance_name,
                test.owner_name_hash,
                test.owner_name,
                test.current_frame,
                test.stopped) == test.should_hold,
            "only playing frame-one UnlockReward prompts under the exact owner are held");
    }

    failures += Expect(
        DecideMenuCounterStore(true) ==
                MenuCounterStoreAction::Commit &&
            DecideMenuCounterStore(false) ==
                MenuCounterStoreAction::Suppress,
        "permanent counter policy uses the shared authored phase");

    for (const std::uint32_t suppress_resume_eip :
         {0x13572468U, 0x24681357U}) {
        auto context = CanaryContext();
        const auto before = context;
        const auto action = ApplyMenuCounterStoreGate(
            context,
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
    {
        auto context = CanaryContext();
        const auto before = context;
        const auto action = ApplyMenuCounterStoreGate(
            context,
            true,
            0x13572468U);
        failures += Expect(
            action == MenuCounterStoreAction::Commit &&
                ContextEquals(context, before),
            "authored store commits without changing captured context");
    }

    MovieClipPreprocessDepth preprocess_depth;
    {
        MovieClipPreprocessScope outer{preprocess_depth};
        failures += Expect(
            preprocess_depth.active() &&
                preprocess_depth.depth() == 1,
            "outer preprocessing scope is active");
        {
            MovieClipPreprocessScope inner{preprocess_depth};
            failures += Expect(
                preprocess_depth.active() &&
                    preprocess_depth.depth() == 2,
                "nested preprocessing scope increments depth");
        }
        failures += Expect(
            preprocess_depth.active() &&
                preprocess_depth.depth() == 1,
            "nested preprocessing scope restores outer depth");
    }
    failures += Expect(
        !preprocess_depth.active() &&
            preprocess_depth.depth() == 0,
        "preprocessing scope restores zero depth");

    for (const std::uint32_t target : {60U, 120U, 144U, 240U}) {
        const auto profile = FramerateProfile::Create(target).value();
        Authored60PhaseClock clock{profile};
        std::uint32_t commits = 0;
        for (std::uint32_t call = 0; call < target; ++call) {
            commits += DecideMenuCounterStore(clock.Advance()) ==
                    MenuCounterStoreAction::Commit
                ? 1U
                : 0U;
        }
        failures += Expect(
            commits == 60,
            "permanent store policy commits exactly 60 times per second");

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

    struct ActiveCounterCase {
        std::uintptr_t suppress_resume_eip;
        const char* expectation;
    };
    constexpr std::array active_counter_cases{
        ActiveCounterCase{
            0x00616EB9,
            "permanent policy freezes Ranking entry state on non-ticks"},
        ActiveCounterCase{
            0x00665637,
            "permanent policy freezes HitChart entry state on non-ticks"},
        ActiveCounterCase{
            0x00430DA9,
            "permanent policy freezes UnlockReward countdown on non-ticks"},
        ActiveCounterCase{
            0x00430E5A,
            "permanent policy freezes UnlockReward primary state on non-ticks"},
        ActiveCounterCase{
            0x00430F29,
            "permanent policy freezes UnlockReward secondary state on non-ticks"},
    };
    for (const auto& test : active_counter_cases) {
        auto context = CanaryContext();
        const auto before = context;
        const auto action = ApplyMenuCounterStoreGate(
            context,
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

    const FramerateMenuRuntimeStats stats{
        .preprocessing_visits = 1,
        .preprocessing_forced = 2,
        .unlock_prompt_transition_holds = 3,
        .unlock_prompt_stable_holds = 4,
        .ranking_entry = {.commits = 5, .suppressions = 6},
        .hitchart_entry = {.commits = 7, .suppressions = 8},
        .unlock_countdown = {.commits = 9, .suppressions = 10},
        .unlock_primary = {.commits = 11, .suppressions = 12},
        .unlock_secondary = {.commits = 13, .suppressions = 14},
    };
    constexpr std::string_view expected_stats =
        " movieclip_preprocess=1/2"
        " unlock_prompt_holds=3/4"
        " ranking_entry=5/6"
        " hitchart_entry=7/8"
        " unlock_countdown=9/10"
        " unlock_state_primary=11/12"
        " unlock_state_secondary=13/14";
    failures += Expect(
        FormatFramerateMenuRuntimeStats(stats) == expected_stats,
        "final formatter publishes only permanent lightweight totals");

    return failures == 0 ? 0 : 1;
}

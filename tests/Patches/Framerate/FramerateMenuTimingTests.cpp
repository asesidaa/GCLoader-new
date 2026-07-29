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

    const auto menu_hooks = FramerateMenuTimingHookSites();
    failures += Expect(
        !menu_hooks.empty(),
        "menu timing manifest is not empty");
    for (std::size_t index = 0; index < menu_hooks.size(); ++index) {
        const auto& hook = menu_hooks[index];
        failures += Expect(
            hook.contract.rva != 0 &&
                hook.contract.expected.size != 0 &&
                hook.contract.name != nullptr &&
                !std::string_view{hook.contract.name}.empty(),
            "menu hook has an installable contract");
        for (std::size_t other = index + 1;
             other < menu_hooks.size();
             ++other) {
            failures += Expect(
                hook.contract.id != menu_hooks[other].contract.id &&
                    hook.contract.rva !=
                        menu_hooks[other].contract.rva,
                "menu hook IDs and RVAs are unique");
        }
    }
    failures += Expect(
        std::none_of(
            menu_hooks.begin(),
            menu_hooks.end(),
            [](const auto& hook) {
                return hook.contract.rva == 0x000D1730;
            }),
        "temporary MovieClip Stop hook is absent");

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

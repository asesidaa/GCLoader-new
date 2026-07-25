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
    std::uint32_t instruction_length) {
    if (actual.eip != original.eip + instruction_length) {
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
            FramerateHookId::MovieClipStopDiagnostic,
            0x000D1730,
            Pattern({
                0xC7, 0x81, 0x1C, 0x01, 0x00, 0x00,
                0x01, 0x00, 0x00, 0x00, 0xC3}),
            "MovieClip preprocessing stop diagnostic",
            MenuTimingHookKind::Inline},
        ExpectedMenuHook{
            FramerateHookId::RankingEntryCounterStore,
            0x00216EB7,
            Pattern({0x89, 0x01}),
            "Ranking entry authored counter store",
            MenuTimingHookKind::Mid},
        ExpectedMenuHook{
            FramerateHookId::HitChartEntryCounterStore,
            0x00265635,
            Pattern({0x89, 0x01}),
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
        "menu timing manifest contains exactly seven hooks");
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

    for (const std::uint32_t instruction_length : {2U, 6U}) {
        auto context = CanaryContext();
        const auto before = context;
        const auto action = ApplyMenuCounterStoreGate(
            context,
            MenuTimingMode::Correct,
            false,
            instruction_length);
        failures += Expect(
            action == MenuCounterStoreAction::Suppress &&
                ContextEqualsExceptEip(
                    context,
                    before,
                    instruction_length),
            "suppressed store changes only EIP by its instruction length");
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
            6);
        failures += Expect(
            action != MenuCounterStoreAction::Suppress &&
                ContextEquals(context, before),
            "non-suppressed store preserves the complete x86 context");
    }

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
        ActiveMenuTimingMode() == MenuTimingMode::Observe,
        "Stage A binary is unambiguously observe-only");
    FramerateMenuRuntimeStats stats{
        .preprocessing_visits = 1,
        .preprocessing_non_tick_skips = 2,
        .preprocessing_forced = 3,
        .preprocessing_stops = 4,
        .preprocessing_causal_stops = 5,
        .movieclip_same_epoch_revisits = 6,
        .movieclip_hash_collisions = 7,
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
        .diagnostic_read_failures = 21,
    };
    constexpr std::string_view observe_stats =
        " menu_timing_mode=observe"
        " movieclip_preprocess=1/2/3"
        " movieclip_preprocess_stop=4/5"
        " movieclip_revisit=6/7"
        " ranking_entry=8/9"
        " hitchart_entry=10/11"
        " unlock_countdown=12/13/14"
        " unlock_state_primary=15/16/17"
        " unlock_state_secondary=18/19/20"
        " menu_diagnostic_read_failures=21";
    constexpr std::string_view correct_stats =
        " menu_timing_mode=correct"
        " movieclip_preprocess=1/2/3"
        " movieclip_preprocess_stop=4/5"
        " movieclip_revisit=6/7"
        " ranking_entry=8/9"
        " hitchart_entry=10/11"
        " unlock_countdown=12/13/14"
        " unlock_state_primary=15/16/17"
        " unlock_state_secondary=18/19/20"
        " menu_diagnostic_read_failures=21";
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

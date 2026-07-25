# Stage A Observe-Only 2D Menu Timing Diagnostics Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add all seven checked menu-timing hooks, bounded causal/revisit/value diagnostics, and a durable runtime record while preserving the currently deployed MovieClip and raw-counter behavior exactly.

**Architecture:** Add a focused `FramerateMenuTiming` policy/diagnostic component and keep executable ownership in `FrameratePatch.cpp`. An internal `Observe` build installs the future correction boundaries but does not force preprocessing movement or suppress any raw store. It records authored-phase decisions, preprocessing stops, per-screen activation, store values/boundaries, and same-epoch MovieClip revisits. The existing shared authored clock remains authoritative and every new hook participates in the existing preflight-first transaction.

**Tech Stack:** C++23, Win32/x86, SafetyHook 0.6.9, CMake/Ninja `msvc32-debug` and `msvc32-release` presets, CTest, PowerShell, plog, and the daemon-verified `game471.exe.i64` hook contracts.

**Design:** [Complete 2D Menu Timing Fix Design](../../../specs/2026-07-25-complete-2d-menu-timing-design.md)

**Plan-set constraints:** [Complete 2D Menu Timing Plan Set](../README.md)

**Implementation baseline:** `817b25d` (`docs: design complete 2D menu timing fixes`)

## Global Constraints

- Execute inline in
  `H:\gc\artifacts\GCLoader\.worktrees\ctune-effect-timing`; do not dispatch
  implementation agents.
- Stage A is observe-only. The five original stores must execute on every
  phase, and preprocessing on a non-authored phase must retain the current
  success-without-motion behavior.
- Install the temporary Stop diagnostic and all six permanent boundaries now.
  Do not defer contract/binding/rollback coverage.
- Do not remove or simplify any diagnostic introduced by this plan during
  Stage A or Stage B.
- Do not touch `H:\gc\data`, `game471.exe`, `game471.exe.i64`, any XFL/RVB,
  or an unrelated loader subsystem.
- Use `ReadU32Safe` only for diagnostic attribution. A failed read increments a
  diagnostic counter and leaves behavior unchanged.
- Preserve the existing optional WASAPI selection: native plan counts remain
  one/two without/with committed WASAPI; transformed counts become 52/53.
- Keep `NavigatorAdvance` at full-contract index 51 and `OuterFrame` at index
  52.
- Do not deploy until the source is committed, both presets pass, the release
  DLL is archived by SHA-256, the user authorizes the live copy, and the game
  is stopped.
- Do not truncate or delete `H:\gc\loader-log.txt`. Snapshot it additively
  before and after the user run.
- Never describe Stage A as a fix. Its runtime result is diagnostic evidence
  only.

## File and Responsibility Map

| File | Responsibility |
|---|---|
| `src/Patches/Framerate/FramerateMenuTiming.h/.cpp` | Observe/Correct policy, store-gate context mutation, preprocessing causal state, 1,024-slot revisit tracker, menu hook manifest, and stable stats formatting |
| `src/Patches/Framerate/FrameratePatchTransaction.h` | Raise the checked hook capacity from 46 to 53 |
| `src/Patches/Framerate/FrameratePatchPlan.h/.cpp` | Add seven IDs and merge the menu contract view before Navigator/OuterFrame |
| `src/Patches/Framerate/FrameratePatch.cpp` | Own hooks, thread-local scopes, counters/latches, Observe callbacks, outer epoch, activation/sample logs, and five-second publication |
| `src/Patches/CMakeLists.txt` | Compile the focused component |
| `tests/Patches/Framerate/FramerateMenuTimingTests.cpp` | Policy matrix, cadence simulation, nested causal state, revisit behavior, context isolation, and formatter |
| `tests/Patches/Framerate/FramerateDiagnosticsTests.cpp` | Keep transformed startup fixtures aligned with the Stage A/B 52-hook no-WASAPI plan |
| `tests/Patches/Framerate/FrameratePatchPlanTests.cpp` | Exact seven contracts, 53-contract order, selection counts, and native exclusion |
| `tests/Patches/Framerate/FrameratePatchTransactionTests.cpp` | Full-capacity failure injection and rollback at 53 hook positions |
| `tests/Patches/Framerate/FramerateRuntimeTests.cpp` | Non-null runtime bindings for every full transformed contract |
| `tests/Patches/CMakeLists.txt` | Register `FramerateMenuTimingTests` |
| `docs/reverse-engineering/2d-menu-timing-runtime-validation.md` | Append-only baseline, binary identities, exact runtime exercises, log evidence, interpretation, and user verdict |

---

### Task 1: Define the Observe/Correct Policy and Bounded Diagnostic State

**Files:**
- Create: `src/Patches/Framerate/FramerateMenuTiming.h`
- Create: `src/Patches/Framerate/FramerateMenuTiming.cpp`
- Create: `tests/Patches/Framerate/FramerateMenuTimingTests.cpp`
- Modify: `src/Patches/CMakeLists.txt`
- Modify: `tests/Patches/CMakeLists.txt`

**Interfaces:**

```cpp
namespace gc::framerate {

enum class MenuTimingMode {
    Observe,
    Correct,
};

[[nodiscard]] MenuTimingMode ActiveMenuTimingMode() noexcept;
[[nodiscard]] std::string_view MenuTimingModeName(
    MenuTimingMode mode) noexcept;

enum class MovieClipAdvanceContext {
    Ordinary,
    Goto,
    Preprocess,
};

enum class MovieClipAdvanceAction {
    ExecuteOriginal,
    ReturnSuccessWithoutMotion,
};

struct MovieClipAdvanceDecision {
    MovieClipAdvanceAction action{MovieClipAdvanceAction::ExecuteOriginal};
    bool preprocessing_non_tick_skip{};
    bool preprocessing_forced{};
};

[[nodiscard]] MovieClipAdvanceDecision DecideMovieClipAdvance(
    MenuTimingMode mode,
    MovieClipAdvanceContext context,
    bool authored_tick) noexcept;

enum class MenuCounterStoreAction {
    Commit,
    WouldSuppress,
    Suppress,
};

[[nodiscard]] MenuCounterStoreAction DecideMenuCounterStore(
    MenuTimingMode mode,
    bool authored_tick) noexcept;

[[nodiscard]] MenuCounterStoreAction ApplyMenuCounterStoreGate(
    safetyhook::Context& context,
    MenuTimingMode mode,
    bool authored_tick,
    std::uint32_t instruction_length) noexcept;

enum class PreprocessStopObservation {
    OutsidePreprocess,
    InPreprocess,
    CausalAfterSkippedAdvance,
};

class MovieClipPreprocessTracker {
public:
    static constexpr std::size_t kMaximumTrackedDepth = 32;

    void Enter(std::uintptr_t movieclip, std::uint64_t outer_epoch) noexcept;
    void Leave() noexcept;
    void RecordSkippedAdvance(
        std::uintptr_t movieclip,
        std::uint64_t outer_epoch) noexcept;
    [[nodiscard]] PreprocessStopObservation ObserveStop(
        std::uintptr_t movieclip,
        std::uint64_t outer_epoch) noexcept;
    [[nodiscard]] bool active() const noexcept;
    [[nodiscard]] std::size_t depth() const noexcept;

private:
    struct Frame {
        std::uintptr_t movieclip{};
        std::uint64_t outer_epoch{};
        bool skipped_advance{};
    };

    std::array<Frame, kMaximumTrackedDepth> frames_{};
    std::size_t depth_{};
};

class MovieClipPreprocessScope {
public:
    MovieClipPreprocessScope(
        MovieClipPreprocessTracker& tracker,
        std::uintptr_t movieclip,
        std::uint64_t outer_epoch) noexcept;
    ~MovieClipPreprocessScope() noexcept;

    MovieClipPreprocessScope(const MovieClipPreprocessScope&) = delete;
    MovieClipPreprocessScope& operator=(
        const MovieClipPreprocessScope&) = delete;

private:
    MovieClipPreprocessTracker* tracker_{};
};

struct MovieClipVisitObservation {
    bool same_epoch_revisit{};
    bool hash_collision{};

    friend bool operator==(
        const MovieClipVisitObservation&,
        const MovieClipVisitObservation&) = default;
};

class MovieClipVisitTracker {
public:
    static constexpr std::size_t kSlotCount = 1024;

    [[nodiscard]] MovieClipVisitObservation Observe(
        std::uintptr_t movieclip,
        std::uint64_t outer_epoch) noexcept;

private:
    struct Slot {
        std::uintptr_t movieclip{};
        std::uint64_t outer_epoch{};
    };

    std::array<Slot, kSlotCount> slots_{};
};

struct MenuCounterPathStats {
    std::uint64_t commits{};
    std::uint64_t suppressions{};
};

struct MenuCounterBoundaryPathStats {
    std::uint64_t commits{};
    std::uint64_t suppressions{};
    std::uint64_t boundaries{};
};

struct FramerateMenuRuntimeStats {
    std::uint64_t preprocessing_visits{};
    std::uint64_t preprocessing_non_tick_skips{};
    std::uint64_t preprocessing_forced{};
    std::uint64_t preprocessing_stops{};
    std::uint64_t preprocessing_causal_stops{};
    std::uint64_t movieclip_same_epoch_revisits{};
    std::uint64_t movieclip_hash_collisions{};
    MenuCounterPathStats ranking_entry{};
    MenuCounterPathStats hitchart_entry{};
    MenuCounterBoundaryPathStats unlock_countdown{};
    MenuCounterBoundaryPathStats unlock_primary{};
    MenuCounterBoundaryPathStats unlock_secondary{};
    std::uint64_t diagnostic_read_failures{};
};

[[nodiscard]] std::string FormatFramerateMenuRuntimeStats(
    MenuTimingMode mode,
    const FramerateMenuRuntimeStats& stats);

} // namespace gc::framerate
```

- [ ] **Step 1: Register the focused source and test target**

Use explicit header dependencies:

```cpp
#include <safetyhook.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
```

The test additionally includes the existing authored clock/profile headers
plus `<atomic>`, `<cmath>`, `<thread>`, and `<utility>`. Task 2 adds
`FrameratePatchPlan.h` and `<span>` when the hook-site manifest is introduced.

Create the header with the complete declarations in the Interfaces block.
Create `FramerateMenuTiming.cpp` with only:

```cpp
#include "Patches/Framerate/FramerateMenuTiming.h"
```

This gives CMake a real source file while deliberately leaving the functions
undefined for the RED link step.

Add `Framerate/FramerateMenuTiming.cpp` to `gc_runtime_patches`. Add:

```cmake
add_executable(FramerateMenuTimingTests
        Framerate/FramerateMenuTimingTests.cpp)
target_link_libraries(FramerateMenuTimingTests PRIVATE gc_runtime_patches)
add_test(NAME FramerateMenuTimingTests COMMAND FramerateMenuTimingTests)
```

Regenerate the debug build so the new target exists:

```powershell
cmake --preset msvc32-debug
```

- [ ] **Step 2: Write the complete failing policy matrix**

Use the repository's existing `Expect(bool, const char*)` executable-test
style. Cover these exact decision rows:

| Mode | Context | Authored tick | Execute original | Preprocess skip | Preprocess forced |
|---|---|---:|---:|---:|---:|
| Observe | Goto | false/true | yes | no | no |
| Correct | Goto | false/true | yes | no | no |
| Observe | Ordinary | false | no | no | no |
| Observe | Ordinary | true | yes | no | no |
| Correct | Ordinary | false | no | no | no |
| Correct | Ordinary | true | yes | no | no |
| Observe | Preprocess | false | no | yes | no |
| Observe | Preprocess | true | yes | no | no |
| Correct | Preprocess | false | yes | no | yes |
| Correct | Preprocess | true | yes | no | no |

Cover the store matrix:

```cpp
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
```

For the synthetic context test, initialize every x86 field to a distinct
value, call the helper for two-byte and six-byte stores, and assert:

```cpp
const auto before = context;
const auto action = ApplyMenuCounterStoreGate(
    context, MenuTimingMode::Correct, false, 6);
failures += Expect(
    action == MenuCounterStoreAction::Suppress &&
        context.eip == before.eip + 6 &&
        context.eax == before.eax &&
        context.ebx == before.ebx &&
        context.ecx == before.ecx &&
        context.edx == before.edx &&
        context.esi == before.esi &&
        context.edi == before.edi &&
        context.ebp == before.ebp &&
        context.esp == before.esp &&
        context.eflags == before.eflags,
    "suppressed six-byte store changes only EIP");
```

Repeat with length two. For Observe/authored, Observe/non-authored, and
Correct/authored, assert the complete context remains unchanged.

- [ ] **Step 3: Write failing preprocessing-state tests**

Exercise:

1. a Stop outside preprocessing;
2. an in-scope Stop without a skipped advance;
3. one skipped advance followed by Stop on the same object/epoch;
4. a different object;
5. a different epoch;
6. nested scopes with distinct MovieClips;
7. automatic depth restoration after each RAII destructor; and
8. a failed-attribution scope entered with `movieclip == 0`, then bound by the
   MovieClip passed to `RecordSkippedAdvance`.

The central causal assertion is:

```cpp
MovieClipPreprocessTracker tracker;
{
    MovieClipPreprocessScope scope{tracker, 0x1000, 7};
    tracker.RecordSkippedAdvance(0x1000, 7);
    failures += Expect(
        tracker.ObserveStop(0x1000, 7) ==
            PreprocessStopObservation::CausalAfterSkippedAdvance,
        "same-object same-epoch stop is causal");
}
failures += Expect(
    !tracker.active() && tracker.depth() == 0,
    "RAII preprocessing scope restores depth");
```

`ObserveStop` must consume the causal marker so a second Stop is recorded only
as an in-preprocessing Stop.

- [ ] **Step 4: Write failing revisit and thread-isolation tests**

Use aligned test pointers that deliberately collide under
`(movieclip >> 4) & 1023`.

```cpp
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
    "different pointer in occupied same-epoch slot is a collision");
```

Add an actual `thread_local MovieClipVisitTracker` in the test translation
unit. Observe a pointer on the main thread, observe the same pointer/epoch in a
`std::jthread`, and assert the worker sees a first visit. Join before checking
the result.

- [ ] **Step 5: Write failing cadence simulations**

For each target in `{60U, 120U, 144U, 240U}`, instantiate the existing
`FramerateProfile` and `Authored60PhaseClock`. Run exactly `target` calls
through `DecideMenuCounterStore(MenuTimingMode::Correct, tick)` and assert
exactly 60 commits.

Use this helper for transition duration:

```cpp
std::uint32_t TargetCallsForAuthoredSteps(
    std::uint32_t target_fps,
    std::uint32_t authored_steps) {
    const auto profile = FramerateProfile::Create(target_fps).value();
    Authored60PhaseClock clock{profile};
    std::uint32_t calls = 0;
    std::uint32_t commits = 0;
    while (commits < authored_steps) {
        const bool tick = clock.Advance();
        ++calls;
        if (DecideMenuCounterStore(
                MenuTimingMode::Correct, tick) ==
            MenuCounterStoreAction::Commit) {
            ++commits;
        }
    }
    return calls;
}
```

For authored step counts `{10U, 25U, 8U, 30U, 10U}`, representing Ranking,
HitChart first-three, HitChart later-entry, Unlock primary, and Unlock
secondary, assert:

```cpp
const double actual_seconds =
    static_cast<double>(calls) / target_fps;
const double authored_seconds =
    static_cast<double>(authored_steps) / 60.0;
failures += Expect(
    std::abs(actual_seconds - authored_seconds) <= 1.0 / 60.0,
    "menu transition duration stays within one authored frame");
```

Lock the first twelve 144 FPS phase decisions to:

```cpp
constexpr std::array expected144{
    true, false, false, true, false, true,
    false, false, true, false, true, false,
};
```

This prevents an implementation that assumes an integer target/60 divisor.

- [ ] **Step 6: Write the failing formatter and active-mode assertions**

Stage A must assert:

```cpp
failures += Expect(
    ActiveMenuTimingMode() == MenuTimingMode::Observe,
    "Stage A binary is unambiguously observe-only");
```

Populate every stats field with a unique number and assert the exact stable
format:

```text
 menu_timing_mode=observe movieclip_preprocess=1/2/3 movieclip_preprocess_stop=4/5 movieclip_revisit=6/7 ranking_entry=8/9 hitchart_entry=10/11 unlock_countdown=12/13/14 unlock_state_primary=15/16/17 unlock_state_secondary=18/19/20 menu_diagnostic_read_failures=21
```

Assert the same values with `MenuTimingMode::Correct` change only the first
field to `menu_timing_mode=correct`.

The formatter intentionally omits Ranking/HitChart boundary fields because
the approved runtime line specifies only `commit/suppress` for those paths.

- [ ] **Step 7: Run the new test and confirm RED**

```powershell
cmake --build --preset msvc32-debug --target FramerateMenuTimingTests
```

Expected: compile/link failure because the focused component does not yet
exist.

- [ ] **Step 8: Implement the policy functions**

Implement the decision core exactly:

```cpp
MenuTimingMode ActiveMenuTimingMode() noexcept {
    return MenuTimingMode::Observe;
}

std::string_view MenuTimingModeName(MenuTimingMode mode) noexcept {
    switch (mode) {
    case MenuTimingMode::Observe:
        return "observe";
    case MenuTimingMode::Correct:
        return "correct";
    }
    return "invalid";
}

MovieClipAdvanceDecision DecideMovieClipAdvance(
    MenuTimingMode mode,
    MovieClipAdvanceContext context,
    bool authored_tick) noexcept {
    if (context == MovieClipAdvanceContext::Goto) {
        return {};
    }
    if (context == MovieClipAdvanceContext::Preprocess) {
        if (authored_tick) {
            return {};
        }
        if (mode == MenuTimingMode::Correct) {
            return {
                .action = MovieClipAdvanceAction::ExecuteOriginal,
                .preprocessing_forced = true,
            };
        }
        return {
            .action =
                MovieClipAdvanceAction::ReturnSuccessWithoutMotion,
            .preprocessing_non_tick_skip = true,
        };
    }
    return {
        .action = authored_tick
            ? MovieClipAdvanceAction::ExecuteOriginal
            : MovieClipAdvanceAction::ReturnSuccessWithoutMotion,
    };
}

MenuCounterStoreAction DecideMenuCounterStore(
    MenuTimingMode mode,
    bool authored_tick) noexcept {
    if (authored_tick) {
        return MenuCounterStoreAction::Commit;
    }
    return mode == MenuTimingMode::Observe
        ? MenuCounterStoreAction::WouldSuppress
        : MenuCounterStoreAction::Suppress;
}

MenuCounterStoreAction ApplyMenuCounterStoreGate(
    safetyhook::Context& context,
    MenuTimingMode mode,
    bool authored_tick,
    std::uint32_t instruction_length) noexcept {
    const auto action = DecideMenuCounterStore(mode, authored_tick);
    if (action == MenuCounterStoreAction::Suppress) {
        context.eip += instruction_length;
    }
    return action;
}
```

- [ ] **Step 9: Implement the bounded trackers**

For preprocessing, increment depth even beyond the 32 attributed frames so
`active()` remains behaviorally correct. Only index `frames_` when
`depth_ <= kMaximumTrackedDepth`. While depth exceeds that limit, do not
attribute skipped advances to an outer tracked frame; report a Stop only as
`InPreprocess`. Otherwise search tracked frames from innermost to outermost.
Permit a zero-attributed frame to bind to the MovieClip observed by
`RecordSkippedAdvance`. Match both object and outer epoch before returning a
causal Stop.

For revisits, use:

```cpp
const std::size_t index =
    (movieclip >> 4U) & (MovieClipVisitTracker::kSlotCount - 1U);
auto& slot = slots_[index];
const MovieClipVisitObservation observation{
    .same_epoch_revisit =
        slot.outer_epoch == outer_epoch &&
        slot.movieclip == movieclip &&
        movieclip != 0,
    .hash_collision =
        slot.outer_epoch == outer_epoch &&
        slot.movieclip != 0 &&
        slot.movieclip != movieclip,
};
slot = {.movieclip = movieclip, .outer_epoch = outer_epoch};
return observation;
```

Keep the tracker allocation-free and lock-free.

- [ ] **Step 10: Implement the exact formatter**

Use one `std::ostringstream`; emit every field in the order locked by Step 6.
Publish `commits/suppressions` for `MenuCounterPathStats` and
`commits/suppressions/boundaries` for
`MenuCounterBoundaryPathStats`.

- [ ] **Step 11: Build and run GREEN**

```powershell
cmake --build --preset msvc32-debug --target FramerateMenuTimingTests
ctest --preset msvc32-debug -R "^FramerateMenuTimingTests$"
```

Expected: PASS.

- [ ] **Step 12: Commit the focused component**

```powershell
git add -- `
  src/Patches/Framerate/FramerateMenuTiming.h `
  src/Patches/Framerate/FramerateMenuTiming.cpp `
  src/Patches/CMakeLists.txt `
  tests/Patches/Framerate/FramerateMenuTimingTests.cpp `
  tests/Patches/CMakeLists.txt
git commit -m "test: define menu timing diagnostic policy"
```

---

### Task 2: Add the Seven Exact Contracts and Full-Capacity Rollback

**Files:**
- Modify: `src/Patches/Framerate/FramerateMenuTiming.h`
- Modify: `src/Patches/Framerate/FramerateMenuTiming.cpp`
- Modify: `src/Patches/Framerate/FrameratePatchTransaction.h`
- Modify: `src/Patches/Framerate/FrameratePatchPlan.h`
- Modify: `src/Patches/Framerate/FrameratePatchPlan.cpp`
- Modify: `tests/Patches/Framerate/FramerateMenuTimingTests.cpp`
- Modify: `tests/Patches/Framerate/FrameratePatchPlanTests.cpp`
- Modify: `tests/Patches/Framerate/FrameratePatchTransactionTests.cpp`

**Interfaces:**

```cpp
enum class MenuTimingHookKind {
    Inline,
    Mid,
};

struct MenuTimingHookSite {
    FramerateHookContract contract{};
    MenuTimingHookKind kind{};
};

[[nodiscard]] std::span<const MenuTimingHookSite>
FramerateMenuTimingHookSites() noexcept;
```

Add these IDs immediately before `NavigatorAdvance`:

```cpp
MovieClipPreprocessVisit,
MovieClipStopDiagnostic,
RankingEntryCounterStore,
HitChartEntryCounterStore,
UnlockRewardCountdownStore,
UnlockRewardPrimaryStateStore,
UnlockRewardSecondaryStateStore,
```

- [ ] **Step 1: Make exact contract/count/order tests fail**

Add the same exact-byte helper already used by the patch-plan test:

```cpp
BytePattern Pattern(std::initializer_list<std::uint8_t> values) {
    BytePattern pattern{};
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
```

Add `<algorithm>` and `<initializer_list>` to the test includes.

Add a seven-row expected table to `FramerateMenuTimingTests.cpp`:

```cpp
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
```

Compare ID, RVA, bytes, full name, and kind for every row.

Update plan assertions to:

```cpp
static_assert(kMaximumFramerateHooks == 53);
failures += Expect(
    transformed_hooks.size() == 53,
    "Stage A full transformed view has 53 contracts");
failures += Expect(
    transformed_hooks[51].id == FramerateHookId::NavigatorAdvance &&
        transformed_hooks[52].id == FramerateHookId::OuterFrame,
    "Navigator and OuterFrame remain final");
failures += Expect(
    BuildFramerateHookPlan(false, false).count == 1 &&
        BuildFramerateHookPlan(false, true).count == 2,
    "native selection preserves optional WASAPI only");
failures += Expect(
    BuildFramerateHookPlan(true, false).count == 52 &&
        BuildFramerateHookPlan(true, true).count == 53,
    "Stage A transformed selection has exact optional counts");
```

Assert that none of the seven menu IDs occurs in either native plan.

Change transaction capacity assertions and success text from 46 to 53. Leave
the failure-injection loops parameterized by `kMaximumFramerateHooks` so every
position is exercised.

- [ ] **Step 2: Run the focused tests and confirm RED**

```powershell
cmake --build --preset msvc32-debug --target `
  FramerateMenuTimingTests `
  FrameratePatchPlanTests `
  FrameratePatchTransactionTests
```

Expected: compile/assertion failures because the IDs, contracts, and capacity
are not present.

- [ ] **Step 3: Add the menu hook manifest**

Add the seven exact sites from Step 1 as a `constexpr std::array` in
`FramerateMenuTiming.cpp`. Reuse a bounded local `Pattern` builder with
`static_assert(sizeof...(Values) <= kMaximumPatternBytes)`. Return the array
through `FramerateMenuTimingHookSites()`.

The array order is fixed:

```cpp
MovieClipPreprocessVisit
MovieClipStopDiagnostic
RankingEntryCounterStore
HitChartEntryCounterStore
UnlockRewardCountdownStore
UnlockRewardPrimaryStateStore
UnlockRewardSecondaryStateStore
```

- [ ] **Step 4: Raise the single hook-capacity authority**

Change only the hook capacity:

```cpp
inline constexpr std::size_t kMaximumFramerateHooks = 53;
```

Keep `kMaximumFramerateWrites == 17` and
`kMaximumPatternBytes == 32`.

- [ ] **Step 5: Merge menu contracts before the post-effect hooks**

Include `FramerateMenuTiming.h` in `FrameratePatchPlan.cpp`. In
`AllHookContracts()`:

```cpp
static_assert(kMaximumFramerateHooks == 53);
```

Merge in this order:

```cpp
for (const auto& contract : kPreEffectHookContracts) {
    result[index++] = contract;
}
for (const auto& contract : FramerateEffectHookContracts()) {
    result[index++] = contract;
}
for (const auto& site : FramerateMenuTimingHookSites()) {
    result[index++] = site.contract;
}
for (const auto& contract : kPostEffectHookContracts) {
    result[index++] = contract;
}
```

Do not change `BuildFramerateHookPlan` selection logic. That preserves the
independently optional WASAPI hook and keeps all menu hooks transformed-only.

- [ ] **Step 6: Extend the existing exact full-contract array**

In `FrameratePatchPlanTests.cpp`, change the expected array type to 53 and
insert the exact seven entries between `EffectPlayerModuloDividend` and
`NavigatorAdvance`. Extend the existing comparison to include non-null,
non-empty names:

```cpp
failures += Expect(
    transformed_hooks[index].id == expected_hooks[index].id &&
        transformed_hooks[index].rva == expected_hooks[index].rva &&
        transformed_hooks[index].expected == expected_hooks[index].expected &&
        transformed_hooks[index].name != nullptr &&
        std::string_view{transformed_hooks[index].name}.empty() == false,
    "exact hook ID/RVA/byte/name contract");
```

The focused menu-manifest test owns exact new names and kinds; the merged-plan
test owns the full 53-entry order.

- [ ] **Step 7: Run plan and rollback tests GREEN**

```powershell
cmake --build --preset msvc32-debug --target `
  FramerateMenuTimingTests `
  FrameratePatchPlanTests `
  FrameratePatchTransactionTests
ctest --preset msvc32-debug -R `
  "^(FramerateMenuTimingTests|FrameratePatchPlanTests|FrameratePatchTransactionTests)$"
```

Expected: PASS, including rollback from every one of 53 hook positions.

- [ ] **Step 8: Commit contracts and capacity**

```powershell
git add -- `
  src/Patches/Framerate/FramerateMenuTiming.h `
  src/Patches/Framerate/FramerateMenuTiming.cpp `
  src/Patches/Framerate/FrameratePatchTransaction.h `
  src/Patches/Framerate/FrameratePatchPlan.h `
  src/Patches/Framerate/FrameratePatchPlan.cpp `
  tests/Patches/Framerate/FramerateMenuTimingTests.cpp `
  tests/Patches/Framerate/FrameratePatchPlanTests.cpp `
  tests/Patches/Framerate/FrameratePatchTransactionTests.cpp
git commit -m "feat: add checked menu timing hook contracts"
```

---

### Task 3: Wire All Seven Hooks in Observe Mode

**Files:**
- Modify: `src/Patches/Framerate/FrameratePatch.cpp`
- Modify: `tests/Patches/Framerate/FramerateRuntimeTests.cpp`
- Modify: `tests/Patches/Framerate/FramerateDiagnosticsTests.cpp`

**Interfaces:**

```cpp
void __fastcall HookMovieClipPreprocessVisit(void*, void*, int);
void __fastcall HookMovieClipStopDiagnostic(void*, void*);
void HookRankingEntryCounterStore(safetyhook::Context&);
void HookHitChartEntryCounterStore(safetyhook::Context&);
void HookUnlockRewardCountdownStore(safetyhook::Context&);
void HookUnlockRewardPrimaryStateStore(safetyhook::Context&);
void HookUnlockRewardSecondaryStateStore(safetyhook::Context&);
```

IDA-verified register contracts:

| Hook | Destination | Computed new value | Store length |
|---|---|---|---:|
| Ranking | `[ECX]` | `EAX` | 2 |
| HitChart | `[ECX]` | `EAX` | 2 |
| Unlock countdown | `[EAX+0x376C]` | `EDX` | 6 |
| Unlock primary | `[ECX+0x37D4]` | `EAX` | 6 |
| Unlock secondary | `[EAX+0x37D4]` | `EDX` | 6 |

The preprocessing visitor is `void __thiscall(int this, int traversal_arg)`;
its current MovieClip pointer is at `this+0x7C`. Stop is
`void __thiscall(void* movieclip)`.

- [ ] **Step 1: Make runtime-binding coverage fail**

Keep the existing loop over `FramerateHookContracts(true)` and update its
capacity assertion to 53. Add explicit binding checks for all seven new IDs:

```cpp
for (const auto id : {
         FramerateHookId::MovieClipPreprocessVisit,
         FramerateHookId::MovieClipStopDiagnostic,
         FramerateHookId::RankingEntryCounterStore,
         FramerateHookId::HitChartEntryCounterStore,
         FramerateHookId::UnlockRewardCountdownStore,
         FramerateHookId::UnlockRewardPrimaryStateStore,
         FramerateHookId::UnlockRewardSecondaryStateStore}) {
    failures += Expect(
        FramerateHookHasRuntimeBinding(id),
        "menu timing hook has an explicit runtime binding");
}
```

Run:

```powershell
cmake --build --preset msvc32-debug --target FramerateRuntimeTests
```

Expected: test failure because all seven IDs currently map to null install/reset
callbacks.

- [ ] **Step 2: Add hook ownership, counters, and latches**

Add to `FramerateHookStorage`:

```cpp
safetyhook::InlineHook movieclip_preprocess_visit{};
safetyhook::InlineHook movieclip_stop_diagnostic{};
safetyhook::MidHook ranking_entry_counter_store{};
safetyhook::MidHook hitchart_entry_counter_store{};
safetyhook::MidHook unlock_reward_countdown_store{};
safetyhook::MidHook unlock_reward_primary_state_store{};
safetyhook::MidHook unlock_reward_secondary_state_store{};
```

Add:

```cpp
struct MenuCounterRuntimeCounters {
    std::atomic_uint64_t commits{0};
    std::atomic_uint64_t suppressions{0};
    std::atomic_uint64_t boundaries{0};
};

struct FramerateMenuRuntimeCounters {
    std::atomic_uint64_t preprocessing_visits{0};
    std::atomic_uint64_t preprocessing_non_tick_skips{0};
    std::atomic_uint64_t preprocessing_forced{0};
    std::atomic_uint64_t preprocessing_stops{0};
    std::atomic_uint64_t preprocessing_causal_stops{0};
    std::atomic_uint64_t movieclip_same_epoch_revisits{0};
    std::atomic_uint64_t movieclip_hash_collisions{0};
    MenuCounterRuntimeCounters ranking_entry{};
    MenuCounterRuntimeCounters hitchart_entry{};
    MenuCounterRuntimeCounters unlock_countdown{};
    MenuCounterRuntimeCounters unlock_primary{};
    MenuCounterRuntimeCounters unlock_secondary{};
    std::atomic_uint64_t diagnostic_read_failures{0};
};

struct FramerateMenuDiagnosticLatches {
    std::atomic_bool preprocessing_activation{false};
    std::atomic_bool preprocessing_causal_sample{false};
    std::atomic_bool revisit_activation{false};
    std::atomic_bool ranking_activation{false};
    std::atomic_bool ranking_sample{false};
    std::atomic_bool hitchart_activation{false};
    std::atomic_bool hitchart_sample{false};
    std::atomic_bool unlock_activation{false};
    std::atomic_bool unlock_countdown_sample{false};
    std::atomic_bool unlock_primary_sample{false};
    std::atomic_bool unlock_secondary_sample{false};
};
```

Embed both structs in `FramerateRuntimeState`. Add:

```cpp
std::atomic_uint64_t outer_epoch{0};
```

Add thread-local state beside `g_movieclip_goto_depth`:

```cpp
thread_local MovieClipPreprocessTracker g_movieclip_preprocess_tracker;
thread_local MovieClipVisitTracker g_movieclip_visit_tracker;
```

- [ ] **Step 3: Add exact install/reset bindings**

Add two `InstallInlineHook` cases at RVAs `0x000EFB90` and `0x000D1730`.
Add five `InstallMidHook` cases at RVAs `0x00216EB7`, `0x00265635`,
`0x00030DA3`, `0x00030E54`, and `0x00030F23`. Use the corresponding storage
member and callback in each case.

Do not share the Stop storage with `MovieClipAdvance` and do not install any
menu callback for the native contract view.

- [ ] **Step 4: Add one-shot logging helpers**

Add a local compare-exchange helper that catches formatting/logging failures:

```cpp
template <typename Builder>
void LogMenuDiagnosticOnce(
    std::atomic_bool& latch,
    Builder&& builder) noexcept {
    bool expected = false;
    if (!latch.compare_exchange_strong(
            expected, true, std::memory_order_relaxed)) {
        return;
    }
    try {
        PLOG_INFO << builder();
    } catch (...) {
    }
}
```

Every line starts with `FrameratePatch:` and obtains its mode text from
`MenuTimingModeName(ActiveMenuTimingMode())`. Stage A therefore emits
`menu_timing_mode=observe`, while the same retained code identifies Stage B as
Correct without a second logging edit.

- [ ] **Step 5: Wrap the preprocessing visitor with RAII**

Implement:

```cpp
void __fastcall HookMovieClipPreprocessVisit(
    void* self,
    void*,
    int traversal_arg) {
    std::uint32_t raw_movieclip = 0;
    if (!ReadU32Safe(
            reinterpret_cast<std::uintptr_t>(self) + 0x7C,
            raw_movieclip)) {
        g_runtime->menu_counters.diagnostic_read_failures.fetch_add(
            1, std::memory_order_relaxed);
    }

    const auto epoch =
        g_runtime->outer_epoch.load(std::memory_order_acquire);
    MovieClipPreprocessScope scope{
        g_movieclip_preprocess_tracker,
        static_cast<std::uintptr_t>(raw_movieclip),
        epoch};
    g_runtime->menu_counters.preprocessing_visits.fetch_add(
        1, std::memory_order_relaxed);

    LogMenuDiagnosticOnce(
        g_runtime->menu_latches.preprocessing_activation,
        [] {
            std::ostringstream stream;
            stream
                << "FrameratePatch: menu_timing_activation"
                << " menu_timing_mode="
                << MenuTimingModeName(ActiveMenuTimingMode())
                << " path=movieclip_preprocess"
                << " screen=global_asset_load";
            return stream.str();
        });

    g_runtime->hooks.movieclip_preprocess_visit
        .unsafe_thiscall<void>(self, traversal_arg);
}
```

The RAII scope must remain active when the target-pointer read fails. Pointer
attribution is diagnostic; preprocessing context is behavioral.

- [ ] **Step 6: Refactor MovieClip advance through the policy**

Classify goto before preprocessing, then ordinary:

```cpp
const auto context = g_movieclip_goto_depth > 0
    ? MovieClipAdvanceContext::Goto
    : g_movieclip_preprocess_tracker.active()
        ? MovieClipAdvanceContext::Preprocess
        : MovieClipAdvanceContext::Ordinary;
const bool authored_tick = IsAuthored60HzTick();
const auto epoch =
    g_runtime->outer_epoch.load(std::memory_order_acquire);
```

For ordinary calls only, feed `self` and `epoch` to the 1,024-slot tracker.
Increment the two revisit counters. On the first same-epoch revisit, emit:

```text
FrameratePatch: menu_timing_activation menu_timing_mode=observe path=movieclip_same_epoch_revisit screen=ordinary_movieclip
```

Apply:

```cpp
const auto decision = DecideMovieClipAdvance(
    ActiveMenuTimingMode(), context, authored_tick);
```

On `preprocessing_non_tick_skip`, increment the preprocessing skip counter and
call:

```cpp
g_movieclip_preprocess_tracker.RecordSkippedAdvance(
    reinterpret_cast<std::uintptr_t>(self), epoch);
```

On `preprocessing_forced`, increment the forced counter. Stage A's active mode
must make this counter remain zero.

Preserve existing broad counters:

- goto execution increments `movieclip_goto_calls`;
- all other original executions increment `movieclip_calls`;
- every return-without-motion increments `movieclip_skips`; and
- the skipped return value remains exactly `1`.

- [ ] **Step 7: Observe preprocessing Stop without changing it**

Implement:

```cpp
void __fastcall HookMovieClipStopDiagnostic(void* self, void*) {
    const auto epoch =
        g_runtime->outer_epoch.load(std::memory_order_acquire);
    const auto observation = g_movieclip_preprocess_tracker.ObserveStop(
        reinterpret_cast<std::uintptr_t>(self), epoch);

    if (observation != PreprocessStopObservation::OutsidePreprocess) {
        g_runtime->menu_counters.preprocessing_stops.fetch_add(
            1, std::memory_order_relaxed);
    }
    if (observation ==
        PreprocessStopObservation::CausalAfterSkippedAdvance) {
        g_runtime->menu_counters.preprocessing_causal_stops.fetch_add(
            1, std::memory_order_relaxed);
        LogMenuDiagnosticOnce(
            g_runtime->menu_latches.preprocessing_causal_sample,
            [self, epoch] {
                std::ostringstream stream;
                stream
                    << "FrameratePatch: menu_timing_sample"
                    << " menu_timing_mode="
                    << MenuTimingModeName(ActiveMenuTimingMode())
                    << " path=movieclip_preprocess_stop"
                    << " cause=skipped_non_tick_advance"
                    << " movieclip=" << self
                    << " outer_epoch=" << epoch;
                return stream.str();
            });
    }

    g_runtime->hooks.movieclip_stop_diagnostic
        .unsafe_thiscall<void>(self);
}
```

The original Stop trampoline is unconditional and is the final action.

- [ ] **Step 8: Implement one shared store-observation helper**

Use a local descriptor that supplies path/screen/asset, instruction length,
optional boundary value, counters, and latches. The helper must:

1. call `ApplyMenuCounterStoreGate` exactly once;
2. categorize `Commit` as commit and both `WouldSuppress`/`Suppress` as
   suppression;
3. read the old value best-effort;
4. increment `diagnostic_read_failures` on failure without changing the
   action;
5. count a boundary only when the original store will execute and
   `old != boundary && new_value == boundary`;
6. emit one activation and one successful value sample per path; and
7. include mode, object/destination, outer epoch, old value, computed new
   value, authored phase, and action in the sample.

Use these descriptors:

| Path | Screen | Asset | Boundary |
|---|---|---|---:|
| `ranking_entry` | `attract_ranking` | `ranking.rvb` | none |
| `hitchart_entry` | `attract_hitchart` | `hitchart.rvb` | none |
| `unlock_countdown` | `postplay_unlock_reward` | `unlock_reward.rvb` | 0 |
| `unlock_state_primary` | `postplay_unlock_reward` | `unlock_reward.rvb` | 31 |
| `unlock_state_secondary` | `postplay_unlock_reward` | `unlock_reward.rvb` | 43 |

Ranking and HitChart each have their own activation latch. All three Unlock
paths share one `unlock_reward` activation latch but keep separate sample
latches.

Activation output is exact:

```text
path=ranking_entry screen=attract_ranking asset=ranking.rvb
path=hitchart_entry screen=attract_hitchart asset=hitchart.rvb
path=unlock_reward screen=postplay_unlock_reward asset=unlock_reward.rvb
```

The first-value samples use their specific paths
`unlock_countdown`, `unlock_state_primary`, and
`unlock_state_secondary`; only the shared activation uses
`path=unlock_reward`.

- [ ] **Step 9: Add the five exact mid-hook callbacks**

Each callback contains only address/new-value extraction plus the shared
helper call:

```cpp
void HookRankingEntryCounterStore(safetyhook::Context& context) {
    ObserveMenuCounterStore(
        context,
        RankingStoreDescriptor(),
        context.ecx,
        context.eax);
}

void HookHitChartEntryCounterStore(safetyhook::Context& context) {
    ObserveMenuCounterStore(
        context,
        HitChartStoreDescriptor(),
        context.ecx,
        context.eax);
}

void HookUnlockRewardCountdownStore(safetyhook::Context& context) {
    ObserveMenuCounterStore(
        context,
        UnlockCountdownStoreDescriptor(),
        context.eax + 0x376C,
        context.edx);
}

void HookUnlockRewardPrimaryStateStore(safetyhook::Context& context) {
    ObserveMenuCounterStore(
        context,
        UnlockPrimaryStoreDescriptor(),
        context.ecx + 0x37D4,
        context.eax);
}

void HookUnlockRewardSecondaryStateStore(safetyhook::Context& context) {
    ObserveMenuCounterStore(
        context,
        UnlockSecondaryStoreDescriptor(),
        context.eax + 0x37D4,
        context.edx);
}
```

Stage A's `WouldSuppress` action must leave `EIP` unchanged at all five sites.

- [ ] **Step 10: Publish the outer epoch before affected work**

At the first line of `HookOuterFrame`, before QPC observation:

```cpp
g_runtime->outer_epoch.fetch_add(1, std::memory_order_release);
```

The epoch is diagnostic only. Do not read it in either timing decision.

- [ ] **Step 11: Add startup identity and five-second aggregate fields**

After the existing startup report and before transaction installation, emit
from the active mode:

```cpp
PLOG_INFO
    << "FrameratePatch: menu_timing startup"
    << " menu_timing_mode="
    << MenuTimingModeName(ActiveMenuTimingMode())
    << " contracts=7 permanent=6 temporary=1";
```

In `MaybeLogRuntimeStats`, snapshot the menu atomics into
`FramerateMenuRuntimeStats` and append
`FormatFramerateMenuRuntimeStats(ActiveMenuTimingMode(), menu_stats)`.

Do not reset counters after logging; all five-second lines remain cumulative.

Update both transformed startup fixtures in
`FramerateDiagnosticsTests.cpp` from `hook_count = 45` to
`hook_count = 52`, and update the exact `hooks=45` assertion to
`hooks=52`. Native remains one hook without optional WASAPI.

- [ ] **Step 12: Run focused and complete framerate tests**

```powershell
cmake --build --preset msvc32-debug --target `
  FramerateMenuTimingTests `
  FrameratePatchPlanTests `
  FrameratePatchTransactionTests `
  FramerateRuntimeTests `
  FramerateDiagnosticsTests `
  FramerateAuthoredClockTests
ctest --preset msvc32-debug -R "^Framerate.*Tests$"
```

Expected: PASS. In particular, every one of 53 full transformed contracts has
a runtime binding.

- [ ] **Step 13: Commit Observe-mode runtime wiring**

```powershell
git add -- `
  src/Patches/Framerate/FrameratePatch.cpp `
  tests/Patches/Framerate/FramerateRuntimeTests.cpp `
  tests/Patches/Framerate/FramerateDiagnosticsTests.cpp
git commit -m "feat: observe missing 2D menu timing paths"
```

---

### Task 4: Start the Append-Only Runtime Validation Record

**Files:**
- Create: `docs/reverse-engineering/2d-menu-timing-runtime-validation.md`

**Interfaces:**

```markdown
# 2D Menu Timing Runtime Validation

## Immutable Inputs

| Input | Identity |
|---|---|
| `H:\gc\game471.exe` | SHA-256 `FEAD3BD4D0E0985F101965EDC417DD2B96522F8716FF789D84618FEB0D7A2522` |
| `H:\gc\game471.exe.i64` | SHA-256 `55D119762B0706549AB5AA9C7D5D2DDF3C902AE322462D025D570C8181C50C1F` |
| Source worktree | `H:\gc\artifacts\GCLoader\.worktrees\ctune-effect-timing` |

## Evidence Rules

- Entries are append-only.
- Static, log, visual, timing, and input verdicts are recorded separately.
- Zero activation means unexercised, not safe.
- Untested FPS targets and unreproduced screens remain unaccepted.
- Only the user supplies visual/timing/input acceptance.

## Stage A — Observe-Only Diagnostics

### Build identity

Status: build not yet produced.

### Deployment

Status: not yet authorized.

### Runtime exercises

Status: user run not yet performed.

### Codex interpretation

Status: no runtime log has been supplied.

### User verdict

Stage A is diagnostic-only and carries no fix verdict.

## Stage B — Corrected with Diagnostics Retained

Status: gated on completed Stage A evidence.

## Stage C — Accepted Diagnostic Cleanup

Status: gated on explicit user acceptance of Stage B.
```

- [ ] **Step 1: Create the record with immutable identities and rules**

Use the exact structure above. Recompute both hashes and require them to match
before committing:

```powershell
Get-FileHash -Algorithm SHA256 `
  'H:\gc\game471.exe', `
  'H:\gc\game471.exe.i64'
```

- [ ] **Step 2: Record current reproduction flags**

Read, do not edit:

```powershell
Select-String -LiteralPath 'H:\gc\data\expconfig.cfg' -Pattern `
  'DoNotDisplayRanking', `
  'DoNotDisplayHitChart', `
  'ForceSkipReward'
```

Append their exact current values under Stage A. If any value is nonzero,
record the resulting unavailable path rather than changing configuration
without the user.

- [ ] **Step 3: Commit the validation ledger**

```powershell
git add -- docs/reverse-engineering/2d-menu-timing-runtime-validation.md
git commit -m "docs: start 2D menu timing runtime validation"
```

---

### Task 5: Verify, Archive, Deploy, and Collect Stage A Evidence

**Files:**
- Modify after build/user run:
  `docs/reverse-engineering/2d-menu-timing-runtime-validation.md`
- Runtime archive:
  `H:\gc\artifacts\runtime-builds\2d-menu-timing`
- Live deployment after authorization:
  `H:\gc\iDmacDrv32.dll`
- Runtime log:
  `H:\gc\loader-log.txt`

**Interfaces:**

```text
menu_timing_mode=observe
full contracts=53
transformed installed=52/53 without/with WASAPI
native installed=1/2 without/with WASAPI
Navigator index=51
OuterFrame index=52
Stage A source and tests committed
```

- [ ] **Step 1: Run both complete preset gates**

From a shell initialized with the x86 MSVC environment:

```powershell
cmake --preset msvc32-debug
cmake --build --preset msvc32-debug
ctest --preset msvc32-debug

cmake --preset msvc32-release
cmake --build --preset msvc32-release
ctest --preset msvc32-release
```

Expected: both full builds and both full CTest suites pass.

- [ ] **Step 2: Run structural closeout checks**

```powershell
rg -n "kMaximumFramerateHooks = 53" src tests
rg -n "MovieClipPreprocessVisit|MovieClipStopDiagnostic|RankingEntryCounterStore|HitChartEntryCounterStore|UnlockRewardCountdownStore|UnlockRewardPrimaryStateStore|UnlockRewardSecondaryStateStore" `
  src/Patches/Framerate tests/Patches/Framerate
rg -n "menu_timing_mode=observe|movieclip_preprocess|ranking_entry|hitchart_entry|unlock_state_secondary" `
  src/Patches/Framerate tests/Patches/Framerate
rg -n "T[B]D|TO[D]O|place[h]older" `
  docs/superpowers/plans/2026-07-25-complete-2d-menu-timing `
  docs/reverse-engineering/2d-menu-timing-runtime-validation.md
git diff --check
git status --short
```

Expected:

- one authoritative capacity of 53;
- all seven IDs present in manifest, merged plan, binding, and tests;
- Observe mode is the active internal mode;
- no unfinished markers;
- no uncommitted implementation change.

- [ ] **Step 3: Inspect and hash the release DLL**

The preset output is:

`build-msvc32-release\dist\iDmacDrv32.dll`

Run:

```powershell
$candidate = (Resolve-Path `
  'build-msvc32-release\dist\iDmacDrv32.dll').Path
Get-Item -LiteralPath $candidate |
  Select-Object FullName, Length, LastWriteTimeUtc
Get-FileHash -Algorithm SHA256 -LiteralPath $candidate
& $env:ComSpec /d /s /c `
  '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && dumpbin /headers build-msvc32-release\dist\iDmacDrv32.dll | findstr /i "machine x86"'
```

Expected: a nonzero x86 DLL and a stable SHA-256.

- [ ] **Step 4: Archive the immutable Stage A binary**

```powershell
$candidate = (Resolve-Path `
  'build-msvc32-release\dist\iDmacDrv32.dll').Path
$candidateHash = (Get-FileHash -Algorithm SHA256 `
  -LiteralPath $candidate).Hash
$stageDirectory = Join-Path `
  'H:\gc\artifacts\runtime-builds\2d-menu-timing\stage-a-observe' `
  $candidateHash
New-Item -ItemType Directory -Force -Path $stageDirectory | Out-Null
Copy-Item -LiteralPath $candidate `
  -Destination (Join-Path $stageDirectory 'iDmacDrv32.dll')
Get-FileHash -Algorithm SHA256 `
  -LiteralPath (Join-Path $stageDirectory 'iDmacDrv32.dll')
```

Expected: archive and candidate hashes match. Do not delete or reuse this
directory in Stage B or C.

Append the actual source commit, path, size, timestamp, and hash to Stage A's
build identity using `apply_patch`.

- [ ] **Step 5: Commit the Stage A build identity**

```powershell
git add -- docs/reverse-engineering/2d-menu-timing-runtime-validation.md
git commit -m "docs: record Stage A menu diagnostic build"
```

This commit records static evidence only and must continue to say that the
user run has not yet occurred.

- [ ] **Step 6: Pause for explicit deployment authorization**

Report:

```text
Stage A implementation and static verification complete; observe-only gameplay run pending
```

Do not copy the live DLL until the user authorizes deployment and confirms the
game and any DLL-loading utility are stopped.

- [ ] **Step 7: Preserve the live DLL and log, then deploy**

After authorization:

```powershell
$timestamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$preDeploy = Join-Path `
  'H:\gc\artifacts\runtime-builds\2d-menu-timing\pre-deploy' `
  $timestamp
New-Item -ItemType Directory -Force -Path $preDeploy | Out-Null
Copy-Item -LiteralPath 'H:\gc\iDmacDrv32.dll' `
  -Destination (Join-Path $preDeploy 'iDmacDrv32.dll')
if (Test-Path -LiteralPath 'H:\gc\loader-log.txt') {
    Copy-Item -LiteralPath 'H:\gc\loader-log.txt' `
      -Destination (Join-Path $preDeploy 'loader-log.before.txt')
}

$candidate = (Resolve-Path `
  'build-msvc32-release\dist\iDmacDrv32.dll').Path
Copy-Item -LiteralPath $candidate `
  -Destination 'H:\gc\iDmacDrv32.dll' -Force
Get-FileHash -Algorithm SHA256 `
  -LiteralPath $candidate, 'H:\gc\iDmacDrv32.dll'
```

Expected: candidate and live hashes match. Do not remove the pre-deploy
snapshot.

- [ ] **Step 8: Give the user the Stage A exercise matrix**

Start with 240 FPS because it maximizes non-authored observations. Ask the user
to restart the game, then perform:

1. traverse Select Mode, Select Game, Select Music, Results, and Unlock Reward;
2. wait through attract rotation until Ranking appears;
3. wait through attract rotation until HitChart appears;
4. complete a credit into an eligible UnlockReward flow if available;
5. exercise `selectmode2`, `selectgame2`, `selectmusic2`, `result_local`, and
   `unlock_reward`;
6. use the bottom-right Navigator on Select Mode, Select Music, and Results;
7. observe boot/legal/news wall time and ordinary input responsiveness; and
8. exit the game and supply `H:\gc\loader-log.txt`.

Stage A visuals are expected to retain the current defect. The user is not
being asked to accept a fix.

- [ ] **Step 9: Snapshot and extract the supplied log**

```powershell
$candidateHash = (Get-FileHash -Algorithm SHA256 `
  -LiteralPath 'H:\gc\iDmacDrv32.dll').Hash
$stageDirectory = Join-Path `
  'H:\gc\artifacts\runtime-builds\2d-menu-timing\stage-a-observe' `
  $candidateHash
$timestamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$logName = "loader-log.target-240.$timestamp.txt"
Copy-Item -LiteralPath 'H:\gc\loader-log.txt' `
  -Destination (Join-Path $stageDirectory $logName)
Select-String -LiteralPath 'H:\gc\loader-log.txt' -Pattern `
  'menu_timing', `
  'runtime_stats', `
  'external cap validated', `
  'transaction committed', `
  'FrameratePatch: fatal'
```

Do not truncate or move the live log.
For an additional 60/120/144 run, replace only the numeric `target-240`
portion with the target actually exercised; retain every timestamped file.

- [ ] **Step 10: Interpret evidence without overclaiming**

Record:

- configured and measured FPS;
- startup mode and exact hook count;
- each activation that occurred;
- final cumulative commit/would-suppress/boundary values;
- preprocessing visit, non-tick-skip, Stop, and causal-Stop totals;
- revisit and collision totals;
- diagnostic read failures;
- exact screens the user actually reached;
- Navigator and News/Notice control observations;
- unreproduced screens; and
- untested targets.

Interpretation rules:

- `suppress` means would-suppress in Observe mode;
- `preprocessing_forced` must be zero;
- a causal Stop greater than zero confirms the runtime collision;
- a causal Stop of zero does not disprove the static defect unless the
  preprocessing path and non-tick skips were exercised;
- a zero path counter means unexercised;
- a nonzero revisit counter does not authorize deduplication; and
- plausible ratios do not establish visual correctness.

If the log is ambiguous, repeat only the missing path or use 120/144 FPS as
needed. Keep each log snapshot.

- [ ] **Step 11: Append and commit Stage A runtime evidence**

Use `apply_patch` to append the evidence and the user's diagnostic observations
to the validation record. Preserve the initial pending text as historical
context or mark it superseded without deleting it.

```powershell
git add -- docs/reverse-engineering/2d-menu-timing-runtime-validation.md
git commit -m "test: record Stage A menu timing diagnostics"
```

Stage B may begin only after this checkpoint exists. If a user-run log has not
been supplied, stop here.

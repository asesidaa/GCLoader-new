# Stage C Post-Acceptance 2D Menu Timing Diagnostic Cleanup Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** After explicit user acceptance, remove only the temporary Stop/causal/revisit/value-sample diagnostic machinery, retain all six permanent timing corrections and lightweight cumulative totals, and produce a final separately hashed 52-contract DLL for user-owned smoke testing.

**Architecture:** First remove the temporary Stop contract and binding under exact 52-contract tests. Then collapse the staged Observe/Correct policy to the accepted permanent policy, replace attributed preprocessing state with a minimal thread-local RAII depth guard, remove the 1,024-slot revisit tracker and all verbose/value diagnostics, and retain compact commit/suppress and preprocessing visit/forced totals. Hook bytes, store boundaries, shared authored phase, transaction preflight, optional WASAPI behavior, Navigator ordering, and `OuterFrame` ownership remain unchanged.

**Tech Stack:** C++23, Win32/x86, SafetyHook 0.6.9, CMake/Ninja presets, CTest, PowerShell, plog, and the append-only Stage A/B runtime evidence.

**Design:** [Complete 2D Menu Timing Fix Design](../../../specs/2026-07-25-complete-2d-menu-timing-design.md)

**Plan-set constraints:** [Complete 2D Menu Timing Plan Set](../README.md)

**Required predecessor:** [Stage B Corrected with Diagnostics Retained](../stage-b-corrections/PLAN.md)

## Destructive Entry Gate

Do not edit any source in this plan until all boxes are true:

- [ ] The Stage B Correct DLL is archived by SHA-256.
- [ ] Stage B runtime logs are archived.
- [ ] Codex's static/log interpretation is committed.
- [ ] The user's visual/timing/input verdict is committed separately.
- [ ] The user explicitly accepted the stated tested targets/screens.
- [ ] The user explicitly authorized temporary-diagnostic cleanup.
- [ ] Untested targets and unreproduced screens are listed honestly.
- [ ] The worktree is clean.

Read and quote the exact authorization in the Stage C section of
`docs/reverse-engineering/2d-menu-timing-runtime-validation.md` before the first
removal commit.

If authorization is absent or ambiguous, stop. Keep the full 53-contract
Stage B build intact.

## Global Constraints

- Execute inline in the existing worktree; do not dispatch implementation
  agents.
- Remove only items explicitly listed by the approved design:
  `MovieClipStopDiagnostic`, causal Stop state/counters/sample, value/boundary
  samples and reads, activation latches/lines, the outer diagnostic epoch, the
  1,024-slot revisit tracker, and the temporary mode selector.
- Retain all six permanent menu hooks and exact byte contracts.
- Retain lightweight cumulative preprocessing visit/forced and per-store
  commit/suppress totals in the existing five-second runtime line.
- Retain exact policy, cadence, store-context, contract, binding, order,
  capacity, and rollback tests.
- Do not remove any audit, design, plan, runtime-validation, archived DLL, or
  archived log.
- Do not modify game data, the executable, the IDB, XFL/RVB assets, Navigator,
  News/Notice, input, elapsed seconds, or an unrelated loader subsystem.
- Final capacity is exactly 52. Transformed installed counts are 51/52
  without/with optional WASAPI. Native counts remain one/two
  without/with optional WASAPI.
- `NavigatorAdvance` is full-contract index 50 and `OuterFrame` is index 51.
- Do not deploy until both presets pass, the cleanup source is committed, the
  final DLL is archived, the user authorizes the live copy, and the game is
  stopped.
- Static success is not the final smoke-test verdict.

## Final Retained Surface

| ID | RVA | Expected bytes | Kind |
|---|---:|---|---|
| `MovieClipPreprocessVisit` | `0x000EFB90` | `6A FF 68 10 49 67 00` | Inline |
| `RankingEntryCounterStore` | `0x00216EB7` | `89 01` | Mid |
| `HitChartEntryCounterStore` | `0x00265635` | `89 01` | Mid |
| `UnlockRewardCountdownStore` | `0x00030DA3` | `89 90 6C 37 00 00` | Mid |
| `UnlockRewardPrimaryStateStore` | `0x00030E54` | `89 81 D4 37 00 00` | Mid |
| `UnlockRewardSecondaryStateStore` | `0x00030F23` | `89 90 D4 37 00 00` | Mid |

---

### Task 1: Remove the Temporary Stop Hook Under Exact 52-Contract Tests

**Files:**
- Modify: `tests/Patches/Framerate/FramerateMenuTimingTests.cpp`
- Modify: `tests/Patches/Framerate/FrameratePatchPlanTests.cpp`
- Modify: `tests/Patches/Framerate/FrameratePatchTransactionTests.cpp`
- Modify: `tests/Patches/Framerate/FramerateRuntimeTests.cpp`
- Modify: `tests/Patches/Framerate/FramerateDiagnosticsTests.cpp`
- Modify: `src/Patches/Framerate/FrameratePatchTransaction.h`
- Modify: `src/Patches/Framerate/FrameratePatchPlan.h`
- Modify: `src/Patches/Framerate/FrameratePatchPlan.cpp`
- Modify: `src/Patches/Framerate/FramerateMenuTiming.cpp`
- Modify: `src/Patches/Framerate/FrameratePatch.cpp`

**Interfaces:**

```cpp
FramerateHookId::MovieClipStopDiagnostic
```

**Retained ordering:**

```text
existing 44 pre/menu-independent contracts
MovieClipPreprocessVisit
RankingEntryCounterStore
HitChartEntryCounterStore
UnlockRewardCountdownStore
UnlockRewardPrimaryStateStore
UnlockRewardSecondaryStateStore
NavigatorAdvance
OuterFrame
```

- [ ] **Step 1: Update capacity and manifest expectations first**

Change test assertions to:

```cpp
static_assert(kMaximumFramerateHooks == 52);
failures += Expect(
    FramerateMenuTimingHookSites().size() == 6,
    "final menu timing manifest has six permanent hooks");
failures += Expect(
    transformed_hooks.size() == 52,
    "final full transformed view has 52 contracts");
failures += Expect(
    transformed_hooks[50].id == FramerateHookId::NavigatorAdvance &&
        transformed_hooks[51].id == FramerateHookId::OuterFrame,
    "final Navigator and OuterFrame order");
failures += Expect(
    BuildFramerateHookPlan(true, false).count == 51 &&
        BuildFramerateHookPlan(true, true).count == 52,
    "final transformed optional-WASAPI counts");
failures += Expect(
    BuildFramerateHookPlan(false, false).count == 1 &&
        BuildFramerateHookPlan(false, true).count == 2,
    "final native selection preserves optional WASAPI");
```

Remove the Stop row from the focused six-row expected menu manifest and the
full expected contract array. Assert that no contract has RVA `0x000D1730`.

Keep the transaction failure-injection loops parameterized by the capacity and
update exact success wording to 52.

In `FramerateRuntimeTests`, remove the Stop ID from the explicit menu-ID list
and retain the six permanent IDs.

In `FramerateDiagnosticsTests.cpp`, update the two transformed no-WASAPI
fixtures and the exact startup string from 52 hooks to the final 51 hooks.
Native remains one hook without optional WASAPI.

- [ ] **Step 2: Run tests and confirm RED**

```powershell
cmake --build --preset msvc32-debug --target `
  FramerateMenuTimingTests `
  FramerateDiagnosticsTests `
  FrameratePatchPlanTests `
  FrameratePatchTransactionTests `
  FramerateRuntimeTests
```

Expected: capacity/order failures while production still contains 53
contracts.

- [ ] **Step 3: Remove the Stop ID and contract**

Remove `MovieClipStopDiagnostic` from `FramerateHookId`. Remove its exact site
from the `FramerateMenuTimingHookSites()` array. Keep the other six rows
byte-for-byte unchanged and in their existing relative order.

Change:

```cpp
inline constexpr std::size_t kMaximumFramerateHooks = 52;
```

Update `AllHookContracts()`:

```cpp
static_assert(kMaximumFramerateHooks == 52);
```

Do not change selection logic.

- [ ] **Step 4: Remove only the Stop runtime ownership**

Remove:

```cpp
safetyhook::InlineHook movieclip_stop_diagnostic{};
void __fastcall HookMovieClipStopDiagnostic(void*, void*);
```

Remove the corresponding `AssignHookCallbacks` case and callback body. Do not
remove preprocessing RAII, causal data, revisit data, activation data, or
store diagnostics yet; Task 2 performs that cleanup under focused tests.

- [ ] **Step 5: Run exact plan/binding/rollback tests GREEN**

```powershell
cmake --build --preset msvc32-debug --target `
  FramerateMenuTimingTests `
  FramerateDiagnosticsTests `
  FrameratePatchPlanTests `
  FrameratePatchTransactionTests `
  FramerateRuntimeTests
ctest --preset msvc32-debug -R `
  "^(FramerateMenuTimingTests|FramerateDiagnosticsTests|FrameratePatchPlanTests|FrameratePatchTransactionTests|FramerateRuntimeTests)$"
```

Expected:

- six exact menu sites;
- no Stop ID/RVA/binding;
- full contract count 52;
- transformed plan counts 51/52;
- native plan counts one/two;
- every retained contract bound; and
- rollback succeeds at every one of 52 positions.

- [ ] **Step 6: Commit the contract-level cleanup**

```powershell
git add -- `
  src/Patches/Framerate/FrameratePatchTransaction.h `
  src/Patches/Framerate/FrameratePatchPlan.h `
  src/Patches/Framerate/FrameratePatchPlan.cpp `
  src/Patches/Framerate/FramerateMenuTiming.cpp `
  src/Patches/Framerate/FrameratePatch.cpp `
  tests/Patches/Framerate/FramerateMenuTimingTests.cpp `
  tests/Patches/Framerate/FrameratePatchPlanTests.cpp `
  tests/Patches/Framerate/FrameratePatchTransactionTests.cpp `
  tests/Patches/Framerate/FramerateRuntimeTests.cpp `
  tests/Patches/Framerate/FramerateDiagnosticsTests.cpp
git commit -m "refactor: remove accepted menu stop diagnostic hook"
```

---

### Task 2: Collapse to Permanent Policy and Lightweight Totals

**Files:**
- Modify: `src/Patches/Framerate/FramerateMenuTiming.h`
- Modify: `src/Patches/Framerate/FramerateMenuTiming.cpp`
- Modify: `src/Patches/Framerate/FrameratePatch.cpp`
- Modify: `tests/Patches/Framerate/FramerateMenuTimingTests.cpp`

**Interfaces:**

```cpp
namespace gc::framerate {

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
    bool preprocessing_forced{};
};

[[nodiscard]] MovieClipAdvanceDecision DecideMovieClipAdvance(
    MovieClipAdvanceContext context,
    bool authored_tick) noexcept;

enum class MenuCounterStoreAction {
    Commit,
    Suppress,
};

[[nodiscard]] MenuCounterStoreAction DecideMenuCounterStore(
    bool authored_tick) noexcept;

[[nodiscard]] MenuCounterStoreAction ApplyMenuCounterStoreGate(
    safetyhook::Context& context,
    bool authored_tick,
    std::uint32_t instruction_length) noexcept;

class MovieClipPreprocessDepth {
public:
    void Enter() noexcept;
    void Leave() noexcept;
    [[nodiscard]] bool active() const noexcept;
    [[nodiscard]] std::uint32_t depth() const noexcept;

private:
    std::uint32_t depth_{};
};

class MovieClipPreprocessScope {
public:
    explicit MovieClipPreprocessScope(
        MovieClipPreprocessDepth& depth) noexcept;
    ~MovieClipPreprocessScope() noexcept;

    MovieClipPreprocessScope(const MovieClipPreprocessScope&) = delete;
    MovieClipPreprocessScope& operator=(
        const MovieClipPreprocessScope&) = delete;

private:
    MovieClipPreprocessDepth* depth_{};
};

struct MenuCounterPathStats {
    std::uint64_t commits{};
    std::uint64_t suppressions{};
};

struct FramerateMenuRuntimeStats {
    std::uint64_t preprocessing_visits{};
    std::uint64_t preprocessing_forced{};
    MenuCounterPathStats ranking_entry{};
    MenuCounterPathStats hitchart_entry{};
    MenuCounterPathStats unlock_countdown{};
    MenuCounterPathStats unlock_primary{};
    MenuCounterPathStats unlock_secondary{};
};

[[nodiscard]] std::string FormatFramerateMenuRuntimeStats(
    const FramerateMenuRuntimeStats& stats);

} // namespace gc::framerate
```

Retain `MenuTimingHookKind`, `MenuTimingHookSite`, and
`FramerateMenuTimingHookSites()` exactly as finalized in Task 1.

- [ ] **Step 1: Rewrite focused tests to the final interface first**

Remove tests for:

- `MenuTimingMode`;
- `ActiveMenuTimingMode`;
- `MenuTimingModeName`;
- Observe decisions and `WouldSuppress`;
- `MovieClipPreprocessTracker`;
- `PreprocessStopObservation`;
- `MovieClipVisitTracker`;
- same-epoch revisit/collision behavior;
- thread-local tracker isolation;
- causal Stop state; and
- diagnostic value/boundary formatter fields.

Retain a reduced nested-depth test:

```cpp
MovieClipPreprocessDepth depth;
{
    MovieClipPreprocessScope outer{depth};
    failures += Expect(
        depth.active() && depth.depth() == 1,
        "outer preprocessing scope is active");
    {
        MovieClipPreprocessScope inner{depth};
        failures += Expect(
            depth.active() && depth.depth() == 2,
            "nested preprocessing scope increments depth");
    }
    failures += Expect(
        depth.active() && depth.depth() == 1,
        "nested preprocessing scope restores outer depth");
}
failures += Expect(
    !depth.active() && depth.depth() == 0,
    "preprocessing scope restores zero depth");
```

Retain and rewrite the permanent policy matrix:

| Context | Authored tick | Execute original | Preprocess forced |
|---|---:|---:|---:|
| Goto | false/true | yes | no |
| Ordinary | false | no | no |
| Ordinary | true | yes | no |
| Preprocess | false | yes | yes |
| Preprocess | true | yes | no |

Retain the complete store-context test:

```cpp
failures += Expect(
    DecideMenuCounterStore(true) ==
            MenuCounterStoreAction::Commit &&
        DecideMenuCounterStore(false) ==
            MenuCounterStoreAction::Suppress,
    "final menu counter policy uses the shared authored phase");
```

For both two-byte and six-byte stores, assert that non-authored suppression
changes only `EIP`. Assert authored commit changes no context field.

Retain cadence simulation at 60/120/144/240 and all five transition step
counts. Update calls to use `DecideMenuCounterStore(tick)`. Retain the exact
first-twelve 144 FPS pattern.

Lock the final formatter to:

```text
 movieclip_preprocess=1/2 ranking_entry=3/4 hitchart_entry=5/6 unlock_countdown=7/8 unlock_state_primary=9/10 unlock_state_secondary=11/12
```

- [ ] **Step 2: Run the focused test and confirm RED**

```powershell
cmake --build --preset msvc32-debug --target FramerateMenuTimingTests
```

Expected: compile failure because production still exposes the staged
diagnostic API.

- [ ] **Step 3: Remove the temporary mode and diagnostic types**

Delete from the focused component:

- `MenuTimingMode`;
- active-mode and mode-name functions;
- `WouldSuppress`;
- preprocessing pointer/epoch/skipped-advance tracking and Stop observation;
- MovieClip revisit tracker/observation;
- boundary fields;
- Stop/revisit/read-failure stats; and
- the staged formatter fields.

Implement the permanent decision core:

```cpp
MovieClipAdvanceDecision DecideMovieClipAdvance(
    MovieClipAdvanceContext context,
    bool authored_tick) noexcept {
    if (context == MovieClipAdvanceContext::Goto) {
        return {};
    }
    if (context == MovieClipAdvanceContext::Preprocess) {
        return {
            .action = MovieClipAdvanceAction::ExecuteOriginal,
            .preprocessing_forced = !authored_tick,
        };
    }
    return {
        .action = authored_tick
            ? MovieClipAdvanceAction::ExecuteOriginal
            : MovieClipAdvanceAction::ReturnSuccessWithoutMotion,
    };
}

MenuCounterStoreAction DecideMenuCounterStore(
    bool authored_tick) noexcept {
    return authored_tick
        ? MenuCounterStoreAction::Commit
        : MenuCounterStoreAction::Suppress;
}

MenuCounterStoreAction ApplyMenuCounterStoreGate(
    safetyhook::Context& context,
    bool authored_tick,
    std::uint32_t instruction_length) noexcept {
    const auto action = DecideMenuCounterStore(authored_tick);
    if (action == MenuCounterStoreAction::Suppress) {
        context.eip += instruction_length;
    }
    return action;
}
```

Implement the compact formatter in the exact order locked by Step 1.

- [ ] **Step 4: Replace attributed preprocessing state with depth-only RAII**

Remove:

```cpp
thread_local MovieClipPreprocessTracker g_movieclip_preprocess_tracker;
thread_local MovieClipVisitTracker g_movieclip_visit_tracker;
```

Implement `MovieClipPreprocessDepth` and `MovieClipPreprocessScope` from the
final interface as the retained, unit-tested context guard. Add:

```cpp
thread_local MovieClipPreprocessDepth g_movieclip_preprocess_depth;
```

In `HookMovieClipPreprocessVisit`, remove the `this+0x7C` diagnostic read,
outer epoch, activation log, and causal scope. Keep:

```cpp
void __fastcall HookMovieClipPreprocessVisit(
    void* self,
    void*,
    int traversal_arg) {
    MovieClipPreprocessScope scope{g_movieclip_preprocess_depth};
    g_runtime->menu_counters.preprocessing_visits.fetch_add(
        1, std::memory_order_relaxed);
    g_runtime->hooks.movieclip_preprocess_visit
        .unsafe_thiscall<void>(self, traversal_arg);
}
```

- [ ] **Step 5: Simplify MovieClip advance without changing behavior**

Classify:

```cpp
const auto context = g_movieclip_goto_depth > 0
    ? MovieClipAdvanceContext::Goto
    : g_movieclip_preprocess_depth.active()
        ? MovieClipAdvanceContext::Preprocess
        : MovieClipAdvanceContext::Ordinary;
const auto decision =
    DecideMovieClipAdvance(context, IsAuthored60HzTick());
```

Remove revisit observation, causal recording, outer epoch reads, and
preprocessing skip counters. Retain:

- goto call count;
- ordinary/original call count;
- ordinary skip count and return value `1`; and
- preprocessing forced count when the decision marks it.

- [ ] **Step 6: Simplify the five store callbacks**

Remove:

- destination memory reads;
- old/new samples;
- boundary classification;
- path/screen/asset descriptors;
- activation/sample latches; and
- diagnostic-read failures.

Use one helper:

```cpp
void ApplyPermanentMenuCounterStore(
    safetyhook::Context& context,
    MenuCounterRuntimeCounters& counters,
    std::uint32_t instruction_length) noexcept {
    const auto action = ApplyMenuCounterStoreGate(
        context, IsAuthored60HzTick(), instruction_length);
    auto& counter = action == MenuCounterStoreAction::Commit
        ? counters.commits
        : counters.suppressions;
    counter.fetch_add(1, std::memory_order_relaxed);
}
```

Keep the five callbacks and their exact instruction lengths:

```cpp
void HookRankingEntryCounterStore(safetyhook::Context& context) {
    ApplyPermanentMenuCounterStore(
        context, g_runtime->menu_counters.ranking_entry, 2);
}

void HookHitChartEntryCounterStore(safetyhook::Context& context) {
    ApplyPermanentMenuCounterStore(
        context, g_runtime->menu_counters.hitchart_entry, 2);
}

void HookUnlockRewardCountdownStore(safetyhook::Context& context) {
    ApplyPermanentMenuCounterStore(
        context, g_runtime->menu_counters.unlock_countdown, 6);
}

void HookUnlockRewardPrimaryStateStore(
    safetyhook::Context& context) {
    ApplyPermanentMenuCounterStore(
        context, g_runtime->menu_counters.unlock_primary, 6);
}

void HookUnlockRewardSecondaryStateStore(
    safetyhook::Context& context) {
    ApplyPermanentMenuCounterStore(
        context, g_runtime->menu_counters.unlock_secondary, 6);
}
```

The callbacks no longer need destination/register interpretation because
suppression changes only `EIP`; the exact byte contracts still prove the
instruction lengths and sites.

- [ ] **Step 7: Remove verbose runtime state and retain compact publication**

Remove:

- `FramerateMenuDiagnosticLatches`;
- causal/revisit/read/boundary atomics;
- `outer_epoch` and its `HookOuterFrame` increment;
- one-shot logging helpers;
- all `menu_timing_activation` and `menu_timing_sample` lines; and
- staged mode text.

Retain a lightweight startup line:

```text
FrameratePatch: menu_timing startup policy=corrected contracts=6 temporary=0
```

Snapshot only the final fields into `FramerateMenuRuntimeStats` and append
`FormatFramerateMenuRuntimeStats(menu_stats)` to the existing cumulative
five-second line.

- [ ] **Step 8: Run the focused final test GREEN**

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

Expected: PASS with permanent policy, six exact menu contracts, compact stats,
and no temporary diagnostic type.

- [ ] **Step 9: Verify the cleanup diff is narrow**

```powershell
git diff --stat
git diff --check
git diff -- `
  src/Patches/Framerate/FramerateMenuTiming.h `
  src/Patches/Framerate/FramerateMenuTiming.cpp `
  src/Patches/Framerate/FrameratePatch.cpp `
  tests/Patches/Framerate/FramerateMenuTimingTests.cpp
```

Confirm that all six callbacks, their bindings, their exact contracts, and
compact counters remain.

- [ ] **Step 10: Commit diagnostic trimming**

```powershell
git add -- `
  src/Patches/Framerate/FramerateMenuTiming.h `
  src/Patches/Framerate/FramerateMenuTiming.cpp `
  src/Patches/Framerate/FrameratePatch.cpp `
  tests/Patches/Framerate/FramerateMenuTimingTests.cpp
git commit -m "refactor: trim accepted menu timing diagnostics"
```

---

### Task 3: Verify, Archive, Deploy, and Smoke-Test the Final DLL

**Files:**
- Modify:
  `docs/reverse-engineering/2d-menu-timing-runtime-validation.md`
- Runtime archive:
  `H:\gc\artifacts\runtime-builds\2d-menu-timing`
- Live deployment after authorization:
  `H:\gc\iDmacDrv32.dll`
- Runtime log:
  `H:\gc\loader-log.txt`

**Interfaces:**

```text
policy=corrected
permanent menu hooks=6
temporary menu hooks=0
full transformed contracts=52
transformed installed=51/52 without/with WASAPI
native installed=1/2 without/with WASAPI
Navigator full index=50
OuterFrame full index=51
rollback positions=52
```

- [ ] **Step 1: Run both complete preset gates**

```powershell
cmake --preset msvc32-debug
cmake --build --preset msvc32-debug
ctest --preset msvc32-debug

cmake --preset msvc32-release
cmake --build --preset msvc32-release
ctest --preset msvc32-release
```

Expected: both complete builds and CTest suites pass.

- [ ] **Step 2: Run positive structural checks**

```powershell
rg -n "kMaximumFramerateHooks = 52" src tests
rg -n "MovieClipPreprocessVisit|RankingEntryCounterStore|HitChartEntryCounterStore|UnlockRewardCountdownStore|UnlockRewardPrimaryStateStore|UnlockRewardSecondaryStateStore" `
  src/Patches/Framerate tests/Patches/Framerate
rg -n "movieclip_preprocess=|ranking_entry=|hitchart_entry=|unlock_countdown=|unlock_state_primary=|unlock_state_secondary=" `
  src/Patches/Framerate tests/Patches/Framerate
rg -n "transformed_hooks.size\\(\\) == 52|NavigatorAdvance.*50|OuterFrame.*51" `
  tests/Patches/Framerate
git diff --check
git status --short
```

Expected: one capacity authority, six permanent IDs, compact stats, exact final
order, and a clean worktree.

- [ ] **Step 3: Assert temporary code is absent**

Run:

```powershell
$temporaryMatches = rg -n `
  "MovieClipStopDiagnostic|MovieClipPreprocessTracker|MovieClipVisitTracker|PreprocessStopObservation|menu_timing_activation|menu_timing_sample|preprocessing_causal_stops|movieclip_same_epoch_revisits|movieclip_hash_collisions|diagnostic_read_failures|MenuTimingMode|WouldSuppress|outer_epoch" `
  src/Patches/Framerate `
  tests/Patches/Framerate
if ($LASTEXITCODE -eq 0) {
    $temporaryMatches
    throw 'Temporary menu timing diagnostics remain'
}
if ($LASTEXITCODE -gt 1) {
    throw 'Temporary diagnostic search failed'
}
```

Expected: no matches and no thrown error.

This negative check applies only to source/tests. Historical design, plan,
audit, and runtime-validation documents intentionally retain those names.

- [ ] **Step 4: Scan for unfinished documentation markers**

```powershell
$unfinished = rg -n "T[B]D|TO[D]O|place[h]older" `
  docs/superpowers/plans/2026-07-25-complete-2d-menu-timing `
  docs/reverse-engineering/2d-menu-timing-runtime-validation.md
if ($LASTEXITCODE -eq 0) {
    $unfinished
    throw 'Unfinished documentation marker remains'
}
if ($LASTEXITCODE -gt 1) {
    throw 'Unfinished documentation scan failed'
}
Write-Output 'No unfinished documentation markers'
```

Expected: no matches.

- [ ] **Step 5: Inspect and archive the final release DLL**

```powershell
$candidate = (Resolve-Path `
  'build-msvc32-release\dist\iDmacDrv32.dll').Path
Get-Item -LiteralPath $candidate |
  Select-Object FullName, Length, LastWriteTimeUtc
$candidateHash = (Get-FileHash -Algorithm SHA256 `
  -LiteralPath $candidate).Hash
& $env:ComSpec /d /s /c `
  '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && dumpbin /headers build-msvc32-release\dist\iDmacDrv32.dll | findstr /i "machine x86"'

$stageDirectory = Join-Path `
  'H:\gc\artifacts\runtime-builds\2d-menu-timing\stage-c-final' `
  $candidateHash
New-Item -ItemType Directory -Force -Path $stageDirectory | Out-Null
Copy-Item -LiteralPath $candidate `
  -Destination (Join-Path $stageDirectory 'iDmacDrv32.dll')
Get-FileHash -Algorithm SHA256 `
  -LiteralPath (Join-Path $stageDirectory 'iDmacDrv32.dll')
```

Expected: nonzero x86 final DLL, candidate/archive hashes match, and the hash
differs from both Stage A and Stage B.

- [ ] **Step 6: Append final static identity**

Use `apply_patch` to append:

- cleanup authorization;
- both cleanup commits;
- branch and exact source commit;
- final DLL path, size, timestamp, and hash;
- executable/IDB identities;
- complete build/test results;
- exact 52/51/52 count proof;
- negative temporary-code search; and
- all still-untested targets/paths.

Do not replace Stage A or Stage B evidence.

- [ ] **Step 7: Commit the final static build identity**

```powershell
git add -- docs/reverse-engineering/2d-menu-timing-runtime-validation.md
git commit -m "docs: record final menu timing build"
```

This commit does not claim that the final smoke test passed.

- [ ] **Step 8: Pause for final deployment authorization**

Report:

```text
Stage C cleanup and static verification complete; final smoke-test deployment pending
```

Wait for the user to authorize the live copy and confirm the game is stopped.

- [ ] **Step 9: Preserve Stage B live state and deploy Stage C**

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

Require the pre-deploy live hash to match the recorded Stage B hash or record
the discrepancy. Do not remove either prior binary.

- [ ] **Step 10: Have the user perform the final smoke matrix**

At minimum:

1. run the accepted 60 FPS baseline;
2. run 240 FPS stress;
3. run 144 FPS if it was part of Stage B acceptance;
4. restart at each target;
5. revisit every Ranking, HitChart, and UnlockReward path that was reproduced
   in Stage B;
6. traverse representative nested MovieClip screens;
7. exercise Navigator; and
8. observe News/Notice and input responsiveness.

The user confirms that cleanup did not change accepted behavior. Any path that
was never reproduced remains unaccepted rather than becoming a smoke pass.

- [ ] **Step 11: Verify final lightweight logs**

Confirm:

- startup reports `policy=corrected contracts=6 temporary=0`;
- transformed hook count is 51/52 without/with WASAPI;
- native count is one/two without/with WASAPI;
- exercised permanent counters increase;
- no temporary activation/sample fields appear;
- no preflight/fatal error occurs; and
- the measured cap matches the selected target.

Snapshot the final log under the Stage C hash directory with a unique
target/timestamp name:

```powershell
$liveHash = (Get-FileHash -Algorithm SHA256 `
  -LiteralPath 'H:\gc\iDmacDrv32.dll').Hash
$stageDirectory = Join-Path `
  'H:\gc\artifacts\runtime-builds\2d-menu-timing\stage-c-final' `
  $liveHash
$logDirectory = Join-Path $stageDirectory 'runtime-logs'
New-Item -ItemType Directory -Force -Path $logDirectory | Out-Null

function Save-StageCLog {
    param([Parameter(Mandatory)][uint32]$TargetFps)

    $timestamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    $name = "loader-log.target-$TargetFps.$timestamp.txt"
    Copy-Item -LiteralPath 'H:\gc\loader-log.txt' `
      -Destination (Join-Path $logDirectory $name)
}
```

Call `Save-StageCLog` once with the actual tested target after each run. Do not
truncate the live log.

- [ ] **Step 12: Append the user's final smoke verdict**

Record visual, timing, and input verdicts separately, plus all untested or
unreproduced paths. If smoke fails, preserve the Stage C binary/log and stop
for diagnosis; do not silently restore, delete evidence, or claim completion.

- [ ] **Step 13: Commit final runtime evidence**

```powershell
git add -- docs/reverse-engineering/2d-menu-timing-runtime-validation.md
git commit -m "test: record final menu timing smoke test"
```

Only after this commit and a passing user smoke may the work be described as:

```text
implemented, statically verified, accepted on the recorded targets/screens, and cleaned up
```

Always list targets or screens that remain untested or unreproduced.

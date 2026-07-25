# Stage B Corrected 2D Menu Timing with Diagnostics Retained Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Activate the already-tested preprocessing exemption and five raw-store gates in one separately hashed Correct-mode DLL while retaining every Stage A diagnostic and collecting user-owned 60/120/144/240 runtime acceptance.

**Architecture:** Stage A already installs and tests both policy branches. Stage B changes only the internal active mode from `Observe` to `Correct` and locks that binary identity with a failing current-mode test. The preprocessing visitor then forces semantic MovieClip movement through the original trampoline on non-authored phases, and each exact raw-store hook advances only `EIP` on non-authored phases. The temporary Stop hook, causal tracker, revisit tracker, value/boundary reads, one-shot samples, activation lines, and five-second totals remain byte-for-byte present.

**Tech Stack:** C++23, Win32/x86, SafetyHook 0.6.9, CMake/Ninja presets, CTest, PowerShell, plog, and the Stage A append-only runtime ledger.

**Design:** [Complete 2D Menu Timing Fix Design](../../../specs/2026-07-25-complete-2d-menu-timing-design.md)

**Plan-set constraints:** [Complete 2D Menu Timing Plan Set](../README.md)

**Required predecessor:** [Stage A Observe-Only Diagnostics](../stage-a-diagnostics/PLAN.md)

## Global Constraints

- Execute inline in the existing `ctune-effect-timing` worktree; do not
  dispatch implementation agents.
- Do not begin until Stage A has a committed user-run log interpretation in
  `docs/reverse-engineering/2d-menu-timing-runtime-validation.md`.
- Do not remove, disable, bypass, shrink, or stop installing any Stage A
  diagnostic.
- Keep hook capacity at 53 and keep the temporary Stop hook installed.
- Keep the exact 52/53 transformed installed counts without/with optional
  WASAPI. Preserve native one/two counts without/with optional WASAPI.
- Do not change any hook RVA, expected bytes, instruction length, calling
  convention, or ordering.
- Do not gate whole callbacks. Correct-mode store suppression changes `EIP`
  only.
- Do not make outer epoch or diagnostic-read success part of a behavior
  decision.
- Do not deduplicate MovieClips regardless of Stage A revisit totals.
- Do not overwrite the archived Stage A DLL or its log snapshots.
- Do not deploy until both presets pass, the source change is committed, the
  Correct DLL is archived by SHA-256, the user authorizes deployment, and the
  game is stopped.
- A successful static gate or counter ratio is not gameplay acceptance.
- Stage C remains locked until the user explicitly accepts this corrected
  build and authorizes diagnostic cleanup.

## Stage B Entry Checklist

Before editing source, require all of the following:

- [ ] Stage A source commits exist.
- [ ] Stage A release DLL path and SHA-256 are recorded.
- [ ] Stage A live deployment is recorded.
- [ ] At least one Stage A user-run log is archived.
- [ ] Exact exercised and unexercised paths are recorded.
- [ ] Codex's Stage A interpretation is committed.
- [ ] The worktree is clean.

Verify:

```powershell
git status --short
git log -8 --oneline
Get-Content -Raw `
  'docs/reverse-engineering/2d-menu-timing-runtime-validation.md'
```

If the Stage A runtime section still says no log was supplied, stop. Do not
activate correction without preserving the diagnostic baseline.

---

### Task 1: Lock and Activate Correct Mode

**Files:**
- Modify: `tests/Patches/Framerate/FramerateMenuTimingTests.cpp`
- Modify: `src/Patches/Framerate/FramerateMenuTiming.cpp`

**Interfaces:**

```cpp
[[nodiscard]] MenuTimingMode ActiveMenuTimingMode() noexcept;
```

The policy outcomes that become active are:

```cpp
DecideMovieClipAdvance(
    MenuTimingMode::Correct,
    MovieClipAdvanceContext::Preprocess,
    false)
```

returns `ExecuteOriginal` with `preprocessing_forced == true`, and:

```cpp
DecideMenuCounterStore(MenuTimingMode::Correct, false)
```

returns `Suppress`.

- [ ] **Step 1: Change the active-build test first**

Replace the Stage A active-mode assertion with:

```cpp
failures += Expect(
    ActiveMenuTimingMode() == MenuTimingMode::Correct,
    "Stage B binary is unambiguously corrected");
```

Add a current-mode integration assertion rather than testing only the enum:

```cpp
const auto active_preprocess = DecideMovieClipAdvance(
    ActiveMenuTimingMode(),
    MovieClipAdvanceContext::Preprocess,
    false);
failures += Expect(
    active_preprocess.action ==
            MovieClipAdvanceAction::ExecuteOriginal &&
        active_preprocess.preprocessing_forced &&
        !active_preprocess.preprocessing_non_tick_skip,
    "active build forces non-tick preprocessing movement");

safetyhook::Context active_store{};
active_store.eip = 0x1000;
const auto active_store_action = ApplyMenuCounterStoreGate(
    active_store,
    ActiveMenuTimingMode(),
    false,
    6);
failures += Expect(
    active_store_action == MenuCounterStoreAction::Suppress &&
        active_store.eip == 0x1006,
    "active build suppresses a non-tick six-byte store");
```

- [ ] **Step 2: Run the focused test and confirm RED**

```powershell
cmake --build --preset msvc32-debug --target FramerateMenuTimingTests
ctest --preset msvc32-debug -R "^FramerateMenuTimingTests$"
```

Expected: failure only because `ActiveMenuTimingMode()` still returns Observe.
If another assertion fails, repair Stage A before changing mode.

- [ ] **Step 3: Make the one behavior-selection change**

Change:

```cpp
MenuTimingMode ActiveMenuTimingMode() noexcept {
    return MenuTimingMode::Correct;
}
```

Do not modify either decision table, any runtime callback, any counter, or any
diagnostic.

- [ ] **Step 4: Run the focused policy suite GREEN**

```powershell
cmake --build --preset msvc32-debug --target FramerateMenuTimingTests
ctest --preset msvc32-debug -R "^FramerateMenuTimingTests$"
```

Expected: PASS, including both Observe/Correct policy coverage, the active
Correct integration, the 144 FPS rational phase pattern, five transition
durations, tracker behavior, and formatter outputs.

- [ ] **Step 5: Prove that the source delta did not remove diagnostics**

```powershell
git diff -- `
  src/Patches/Framerate/FramerateMenuTiming.cpp `
  tests/Patches/Framerate/FramerateMenuTimingTests.cpp
git diff --stat
git diff --check
```

Expected source delta:

- one production return value changes from Observe to Correct; and
- tests change the active-build expectation and add the active integration
  assertion.

There must be no deletion in `FrameratePatch.cpp`, no hook-count change, and no
diagnostic-field change.

- [ ] **Step 6: Commit the corrected mode**

```powershell
git add -- `
  src/Patches/Framerate/FramerateMenuTiming.cpp `
  tests/Patches/Framerate/FramerateMenuTimingTests.cpp
git commit -m "fix: correct remaining 2D menu timing paths"
```

---

### Task 2: Run the Corrected 53-Contract Static Gate

**Files:**
- Modify only if a test exposes a defect in Stage A-owned source.
- Read: `docs/reverse-engineering/2d-menu-timing-runtime-validation.md`

**Interfaces:**

```text
active mode=correct
all seven Stage A hooks still present
temporary Stop hook still present
full transformed contracts=53
transformed installed=52/53 without/with WASAPI
native installed=1/2 without/with WASAPI
Navigator full index=51
OuterFrame full index=52
all 53 runtime bindings non-null
rollback succeeds at all 53 positions
```

- [ ] **Step 1: Run the focused debug gate**

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

Expected: PASS.

- [ ] **Step 2: Run both complete preset gates**

```powershell
cmake --preset msvc32-debug
cmake --build --preset msvc32-debug
ctest --preset msvc32-debug

cmake --preset msvc32-release
cmake --build --preset msvc32-release
ctest --preset msvc32-release
```

Expected: both complete builds and both complete CTest suites pass.

- [ ] **Step 3: Verify every diagnostic remains represented**

```powershell
rg -n "MovieClipStopDiagnostic|preprocessing_causal_stops|movieclip_same_epoch_revisits|movieclip_hash_collisions|diagnostic_read_failures" `
  src/Patches/Framerate tests/Patches/Framerate
rg -n "menu_timing_activation|menu_timing_sample|movieclip_preprocess_stop|movieclip_same_epoch_revisit" `
  src/Patches/Framerate
rg -n "kMaximumFramerateHooks = 53|transformed_hooks.size\\(\\) == 53" `
  src tests
rg -n "T[B]D|TO[D]O|place[h]older" `
  docs/superpowers/plans/2026-07-25-complete-2d-menu-timing `
  docs/reverse-engineering/2d-menu-timing-runtime-validation.md
git diff --check
git status --short
```

Expected: every temporary diagnostic is still live and the worktree is clean.

- [ ] **Step 4: Inspect the release DLL**

```powershell
$candidate = (Resolve-Path `
  'build-msvc32-release\dist\iDmacDrv32.dll').Path
Get-Item -LiteralPath $candidate |
  Select-Object FullName, Length, LastWriteTimeUtc
Get-FileHash -Algorithm SHA256 -LiteralPath $candidate
& $env:ComSpec /d /s /c `
  '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && dumpbin /headers build-msvc32-release\dist\iDmacDrv32.dll | findstr /i "machine x86"'
```

Expected: nonzero x86 release DLL. Its SHA-256 must differ from the archived
Stage A Observe DLL.

- [ ] **Step 5: Archive the immutable Stage B DLL**

```powershell
$candidate = (Resolve-Path `
  'build-msvc32-release\dist\iDmacDrv32.dll').Path
$candidateHash = (Get-FileHash -Algorithm SHA256 `
  -LiteralPath $candidate).Hash
$stageDirectory = Join-Path `
  'H:\gc\artifacts\runtime-builds\2d-menu-timing\stage-b-correct' `
  $candidateHash
New-Item -ItemType Directory -Force -Path $stageDirectory | Out-Null
Copy-Item -LiteralPath $candidate `
  -Destination (Join-Path $stageDirectory 'iDmacDrv32.dll')
Get-FileHash -Algorithm SHA256 `
  -LiteralPath (Join-Path $stageDirectory 'iDmacDrv32.dll')
```

Expected: candidate and archive hashes match. Do not overwrite the Stage A
archive.

- [ ] **Step 6: Append the Stage B build identity**

Use `apply_patch` to append:

- exact source commit;
- branch;
- candidate/archive paths;
- size and UTC timestamp;
- SHA-256;
- executable and IDB hashes;
- full test commands/results; and
- exact 53/52/53 count proof.

Do not edit or replace the Stage A section.

- [ ] **Step 7: Commit the corrected build identity**

```powershell
git add -- docs/reverse-engineering/2d-menu-timing-runtime-validation.md
git commit -m "docs: record Stage B corrected menu build"
```

This is a static/build record, not a runtime acceptance commit.

---

### Task 3: Deploy the Corrected DLL Without Losing Stage A

**Files:**
- Runtime archive:
  `H:\gc\artifacts\runtime-builds\2d-menu-timing`
- Live deployment after authorization:
  `H:\gc\iDmacDrv32.dll`
- Runtime log:
  `H:\gc\loader-log.txt`
- Modify:
  `docs/reverse-engineering/2d-menu-timing-runtime-validation.md`

**Interfaces:**

```text
build-msvc32-release\dist\iDmacDrv32.dll
    -> immutable stage-b-correct SHA-256 archive
    -> explicitly authorized H:\gc\iDmacDrv32.dll deployment
```

- [ ] **Step 1: Pause for explicit deployment authorization**

Report:

```text
Stage B corrected implementation and static verification complete; corrected gameplay deployment pending
```

Wait until the user authorizes deployment and confirms the game and any
DLL-loading utility are stopped.

- [ ] **Step 2: Preserve the currently deployed Stage A state**

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
Get-FileHash -Algorithm SHA256 `
  -LiteralPath (Join-Path $preDeploy 'iDmacDrv32.dll')
```

Require the saved live hash to equal the Stage A deployed hash recorded in the
validation document. If it differs, record the unexpected live identity before
continuing.

- [ ] **Step 3: Copy and hash-verify Stage B**

```powershell
$candidate = (Resolve-Path `
  'build-msvc32-release\dist\iDmacDrv32.dll').Path
Copy-Item -LiteralPath $candidate `
  -Destination 'H:\gc\iDmacDrv32.dll' -Force
Get-FileHash -Algorithm SHA256 `
  -LiteralPath $candidate, 'H:\gc\iDmacDrv32.dll'
```

Expected: hashes match and differ from Stage A.

- [ ] **Step 4: Record deployment identity before launch**

Append the deployment timestamp, previous live hash/archive path, new live
hash, and user authorization to the Stage B section using `apply_patch`.

---

### Task 4: Perform User-Owned Corrected Runtime Acceptance

**Files:**
- Read/snapshot: `H:\gc\loader-log.txt`
- Modify after user results:
  `docs/reverse-engineering/2d-menu-timing-runtime-validation.md`

**Interfaces:**

```text
user: exact screen actions and visual/timing/input verdict
loader: startup identity, hook count, five-second counters, fatal state
Codex: log correlation and append-only interpretation
```

**Target matrix:**

| Target | Role |
|---:|---|
| 60 | Native baseline; no new menu hook may install |
| 120 | Integer 2:1 transformed cadence |
| 144 | Required non-integer rational cadence |
| 240 | 4:1 stress target |

- [ ] **Step 1: Re-read runtime flags**

Before each run, read and record without editing:

```powershell
Select-String -LiteralPath 'H:\gc\data\expconfig.cfg' -Pattern `
  'DoNotDisplayRanking', `
  'DoNotDisplayHitChart', `
  'ForceSkipReward'
```

If a screen cannot be reached, record it as unreproduced.

- [ ] **Step 2: Have the user restart for each target**

For each target:

1. use the existing live target-FPS control rather than editing
   `data\system.cfg`;
2. set the external cap to the same target;
3. fully restart so preprocessing/load activity is captured;
4. confirm startup and external-cap validation lines identify the selected
   target; and
5. preserve the resulting log additively.

At 60 FPS, confirm the transaction contains one/two hooks without/with
committed WASAPI and none of the seven menu IDs. Above 60, confirm
`menu_timing_mode=correct` and 52/53 installed hooks without/with WASAPI.

- [ ] **Step 3: Exercise the exact screen matrix at every available target**

| Path | User action | Required visual/timing verdict |
|---|---|---|
| Ranking | Wait through attract rotation for `CRankingTask` / `ranking.rvb`; watch `tg_rank01..30` | Ten-step row entry no longer accelerates with target FPS and matches the 60 FPS reset-to-settle behavior within one authored frame |
| HitChart | Wait through attract rotation for `CHitChartTask` / `hitchart.rvb`; watch `tg_01..30` | First-three 25-step and later-entry eight-step choreography no longer accelerates |
| UnlockReward | Complete a credit with an eligible reward and enter `CUnlockRewardTask` / `unlock_reward.rvb` | Countdown/state-driven panels, arrows, text, and item/coin presentation no longer accelerate; elapsed-time motion, drawing, and input remain normal |
| PreProcessor | Restart, then traverse Select Mode, Select Game, Select Music, Results, Unlock Reward | No nested subanimation is prematurely stopped or missing |
| Revisit stress | Exercise `selectmode2`, `selectgame2`, `selectmusic2`, `result_local`, `unlock_reward` | Observation only; no deduplication behavior |
| Navigator | Use bottom-right Navigator in Select Mode, Select Music, Results | Existing authored timing and input remain correct |
| News/Notice | Observe boot/legal/news wall time and input | Native elapsed behavior remains correct |

- [ ] **Step 4: Check Correct-mode log invariants**

For every transformed run:

- `preprocessing_non_tick_skips` must remain zero;
- `preprocessing_forced` should increase if preprocessing occurs on a
  non-authored phase;
- `preprocessing_causal_stops` must remain zero;
- `preprocessing_stops` may be nonzero because legitimate preprocessing stops
  remain possible;
- each exercised raw-store path must show both commit and suppress totals at
  targets above 60;
- Unlock boundary totals count only stores that actually execute;
- diagnostic read failures must be recorded and must not correlate with a
  behavior change;
- revisit/collision totals remain observation-only; and
- Navigator's existing run/skip totals must remain consistent with the shared
  authored phase.

At 240 FPS, an exercised store's event ratio should approach one commit to
three suppressions. At 144 FPS, accept only the deterministic rational
aggregate; do not expect a fixed integer pattern.

- [ ] **Step 5: Snapshot each log**

After each target:

```powershell
$liveHash = (Get-FileHash -Algorithm SHA256 `
  -LiteralPath 'H:\gc\iDmacDrv32.dll').Hash
$stageDirectory = Join-Path `
  'H:\gc\artifacts\runtime-builds\2d-menu-timing\stage-b-correct' `
  $liveHash
$targetDirectory = Join-Path $stageDirectory 'runtime-logs'
New-Item -ItemType Directory -Force -Path $targetDirectory | Out-Null

function Save-StageBLog {
    param([Parameter(Mandatory)][uint32]$TargetFps)

    $timestamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    $name = "loader-log.target-$TargetFps.$timestamp.txt"
    Copy-Item -LiteralPath 'H:\gc\loader-log.txt' `
      -Destination (Join-Path $targetDirectory $name)
}
```

After each run, call exactly one of:

```powershell
Save-StageBLog -TargetFps 60
Save-StageBLog -TargetFps 120
Save-StageBLog -TargetFps 144
Save-StageBLog -TargetFps 240
```

Call only the target actually exercised. Do not replace another target's log
and do not truncate the live log.

- [ ] **Step 6: Extract evidence for interpretation**

```powershell
Select-String -LiteralPath 'H:\gc\loader-log.txt' -Pattern `
  'menu_timing', `
  'runtime_stats', `
  'external cap validated', `
  'transaction committed', `
  'FrameratePatch: fatal'
```

Codex correlates the extracted counters with the exact screens the user says
were reached. Do not infer a screen from a nonzero aggregate alone.

- [ ] **Step 7: Record separate evidence dimensions**

For every `target × path`, append:

```text
configured FPS
measured FPS
exact screen/action
activation observed
counter excerpt
causal-stop result
revisit/collision result
Codex static/log interpretation
user visual verdict
user timing verdict
user input verdict
unreproduced reason
```

Use `pass`, `fail`, or `not exercised` independently for visual, timing, and
input. Never convert `not exercised` into `pass`.

- [ ] **Step 8: Handle a failed run without cleanup**

If any corrected behavior fails:

1. keep the Stage A and Stage B binaries/logs;
2. leave all diagnostics installed;
3. append the failure;
4. use systematic debugging against the failed path; and
5. do not begin Stage C.

Do not revert to a broad callback gate or MovieClip deduplication.

---

### Task 5: Commit Acceptance Evidence and Enforce the Cleanup Gate

**Files:**
- Modify: `docs/reverse-engineering/2d-menu-timing-runtime-validation.md`

**Interfaces:**

```text
Stage B evidence commit
explicit user acceptance
explicit user authorization to remove temporary diagnostics
```

- [ ] **Step 1: Append Codex interpretation**

Record exact build/hash/count evidence, path activations, cumulative values,
causal-stop result, controls, and all untested targets. State only:

```text
Stage B static and log evidence is consistent with the intended correction
```

unless the user has also supplied a visual/timing/input verdict.

- [ ] **Step 2: Record the user's words separately**

Append the user's explicit verdict with the tested FPS targets and screens.
If the user accepts only a subset, record the exact subset and keep the rest
unaccepted.

- [ ] **Step 3: Commit the Stage B evidence**

```powershell
git add -- docs/reverse-engineering/2d-menu-timing-runtime-validation.md
git commit -m "test: record Stage B menu timing acceptance"
```

If the user has not supplied a verdict, do not create this acceptance commit.

- [ ] **Step 4: Require explicit cleanup authorization**

Stage C is unlocked only by an explicit user statement that:

1. accepts the corrected behavior for the stated targets/screens; and
2. authorizes removal of the temporary diagnostics.

A successful build, zero causal-stop count, expected ratio, or general
statement that the logs look good is insufficient.

If authorization is absent, stop with all Stage A/B diagnostics intact.

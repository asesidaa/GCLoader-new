# Absolute-Time Judgement Correction Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use
> `superpowers:executing-plans` to implement this plan task-by-task. Steps use
> checkbox (`- [ ]`) syntax for tracking. Do not dispatch subagents unless the
> user explicitly reauthorizes them.

**Goal:** Correct the opt-in WASAPI-exclusive absolute-time judgement path so
every successfully observed physical transition, except the spec's finite
counted loss policies, reaches original native recognition/score at one
continuous absolute song coordinate, independent of 60/144/165/240 FPS.

**Architecture:** Preserve the proven 1000-Hz QPC journal, checked rational
math, immutable query scopes, native recognition/score/original-tail calls, and
event/heartbeat scheduler topology. Replace object-lifetime stage ownership
with the native state-18 semantic epoch, bind one exact clock anchor per stage,
make history causal by time and sequence, restore native query compatibility,
and allow only predicate-proven visible hard stops.

**Tech Stack:** Windows x86 C++23, CMake presets, MSVC HostX86/x86, SafetyHook,
WASAPI exact endpoint clock, QPC transition journal, checked rational
arithmetic, plog, PowerShell 7.

**Spec:**
`docs/superpowers/specs/2026-08-22-absolute-time-judgement-correction-design.md`

**Audit:**
`docs/superpowers/audits/2026-08-21-absolute-judgement-authoritative-full-audit.md`

## Global constraints

- Use Superpowers only. Do not use GSD.
- Execute inline and review each task inline. No agents are authorized by this
  plan.
- Do not delete or rewrite failed historical specs, plans, evidence, or logs.
- Preserve unrelated dirty-worktree changes. The existing unbuilt
  `JudgementHistory.cpp` lower-bound deletion must be absorbed deliberately by
  Task 3, not overwritten or treated as the full fix.
- `enable_absolute_time_judgement` remains startup-only, opt-in, and false by
  default.
- Enabled mode remains WASAPI-exclusive, requires `input_poll_hz = 1000`, and
  supports only live zero `HoldSafeFrame`/`SlideHoldSafeFrame`.
- Do not modify the independent framerate/shared-Tune/visual hooks or make
  target FPS an input to judgement time.
- Do not add native/absolute fallback, retry ladders, timeouts, replay input,
  note routing, loader sound/effect calls, or CBooster ring reconstruction.
- Do not add, restore, generate, or run automated gameplay tests, CTest, TDD
  fixtures, or implementation-derived expected-value models.
- Use C++23 `std::format`/`std::format_to` for new formatting. Do not add string
  streams.
- Do not add C++ `try`/`catch`. Standard-library throws through `noexcept`
  terminate normally and use the existing crash-dump path.
- RAII dynamic storage is allowed when ownership is leak-free. Remove the
  playback-history vectors if the one-anchor design leaves them unused; do not
  add fixed storage merely to avoid allocation.
- A Boolean/status failure hard-stops only when the exact predicate is an
  explicit unsupported mode, finite resource limit, or proven internal
  invariant. Log the predicate and operands before termination.
- Diagnostics saturate or become unavailable; diagnostics never terminate
  gameplay.
- Builds use persisted PowerShell 7 scripts under `H:\gc\temp`; do not invoke
  ad-hoc nested PowerShell processes.
- Build success is compilation evidence only. Only supported-game behavior is
  gameplay acceptance.
- Deployment is authorized after all static, Debug, Release, and ABI gates pass.
  Back up the installed DLL first and do not edit `H:\gc\config.toml`.

## File and responsibility map

| File | Responsibility after correction |
|---|---|
| `src/Patches/AbsoluteJudgement/NativeJudgementAbi.h` | Guarded semantic stage-entry/exit sites and native ABI constants |
| `src/Patches/AbsoluteJudgement/AbsoluteJudgementPatch.{h,cpp}` | All-or-none hook installation, semantic lifecycle hooks, query trampolines, owned-loop dispatch |
| `src/Patches/AbsoluteJudgement/AbsoluteJudgementRuntime.{h,cpp}` | Runtime lifecycle entry/exit and one native transaction per scheduled scope |
| `src/Patches/AbsoluteJudgement/JudgementStage.{h,cpp}` | One semantic state-18 generation and immutable stage capability/identity |
| `src/Patches/AbsoluteJudgement/JudgementClockResolver.{h,cpp}` | Stage-entry watermark, one BGM Play anchor, exact QPC-to-J projection |
| `src/Patches/AbsoluteJudgement/JudgementHistory.{h,cpp}` | Time-causal event/state-only history and native-compatible queries |
| `src/Patches/AbsoluteJudgement/JudgementScope.{h,cpp}` | Local immutable scope ownership and neutral/native query behavior |
| `src/Patches/AbsoluteJudgement/JudgementScheduler.{h,cpp}` | Drain, bind/wait, ordered event/heartbeat selection, overload, commit |
| `src/Patches/AbsoluteJudgement/AbsoluteJudgementDiagnostics.{h,cpp}` | Truthful nonfatal diagnostics and exact visible fatal records |
| `src/Input/Polling/GameplayTransitionJournal.{h,cpp}` | Stage-entry QPC/cutoff handoff accounting only; existing producer remains unchanged |
| `src/Input/Polling/InputPollingRuntime.cpp` | Predicate-exact logging before the existing success-only QPC abort; publication behavior remains unchanged |
| `docs/superpowers/audits/2026-08-21-absolute-judgement-authoritative-full-audit.md` | Append exact lifecycle patch-site evidence found before mutation |
| `H:\gc\temp\build-asio-audio-backend.ps1` | Persistent Debug/Release configure/build entry point |
| `H:\gc\temp\inspect-asio-audio-backend-abi.ps1` | Persistent Release PE/export/hook ABI inspection |

Configuration, exact-WASAPI, audio-mixer, loader-initialization, and framerate
files are expected to remain source-unchanged. Input publication behavior also
remains unchanged except for the two tabled journal/runtime responsibilities.
If an executor finds an unavoidable interface change outside the table, stop
and update the spec/plan before editing it.

---

### Task 1: Replace object lifetime with the semantic stage epoch

**Files:**

- Modify:
  `docs/superpowers/audits/2026-08-21-absolute-judgement-authoritative-full-audit.md`
- Modify: `src/Patches/AbsoluteJudgement/NativeJudgementAbi.h`
- Modify: `src/Patches/AbsoluteJudgement/AbsoluteJudgementPatch.cpp`
- Modify: `src/Patches/AbsoluteJudgement/AbsoluteJudgementRuntime.{h,cpp}`
- Modify: `src/Patches/AbsoluteJudgement/JudgementStage.{h,cpp}`
- Modify: `src/Patches/AbsoluteJudgement/JudgementScheduler.{h,cpp}`
- Modify: `src/Patches/AbsoluteJudgement/AbsoluteJudgementDiagnostics.{h,cpp}`
- Modify: `src/Input/Polling/GameplayTransitionJournal.{h,cpp}`
- Create: `H:\gc\temp\build-asio-audio-backend.ps1`

**Interfaces:**

- Produces:
  `BeginAbsoluteJudgementSemanticStage(std::uintptr_t tune_manager) noexcept`
- Produces:
  `EndAbsoluteJudgementSemanticStage(std::uintptr_t tune_manager) noexcept`
- Produces:
  `JudgementScheduler::BeginSemanticStage(std::uintptr_t, std::int64_t)`
- Produces:
  `JudgementScheduler::EndSemanticStage(std::uintptr_t)`
- Preserves: one all-or-none set of eight absolute-judgement sites
- Consumed by: Tasks 2, 5, 6, and 8

- [ ] **Step 1: Record the dirty-worktree boundary**

Run:

```powershell
git status --short
git diff -- docs/superpowers/plans/2026-08-20-absolute-time-judgement.md
git diff -- src/Patches/AbsoluteJudgement/JudgementHistory.cpp
```

Record the two pre-existing modifications in the execution log. Do not reset,
checkout, stash, or overwrite either file.

- [ ] **Step 2: Persist the reusable build script before the first build**

Create `H:\gc\temp\build-asio-audio-backend.ps1` with this contract:

```powershell
[CmdletBinding()]
param(
    [ValidateSet('msvc32-debug', 'msvc32-release')]
    [string] $Preset = 'msvc32-debug',
    [string[]] $Target = @(),
    [switch] $Fresh,
    [string] $RepoRoot =
        'H:\gc\artifacts\GCLoader\.worktrees\asio-audio-backend'
)

$ErrorActionPreference = 'Stop'
$vsDevShell =
    'C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\Tools\Launch-VsDevShell.ps1'
$env:GC_ASIO_SDK_DIR = 'H:\gc\artifacts\ASIOSDK'

if (-not (Test-Path -LiteralPath $RepoRoot -PathType Container)) {
    throw "Repository root not found: $RepoRoot"
}
if (-not (Test-Path -LiteralPath $vsDevShell -PathType Leaf)) {
    throw "Visual Studio developer shell not found: $vsDevShell"
}

& $vsDevShell -Arch x86 -HostArch x86 -SkipAutomaticLocation
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
if ($env:VSCMD_ARG_TGT_ARCH -ne 'x86' -or
    $env:VSCMD_ARG_HOST_ARCH -ne 'x86') {
    throw "Expected HostX86/x86 environment"
}

Push-Location -LiteralPath $RepoRoot
try {
    $configure = @('--preset', $Preset)
    if ($Fresh) { $configure = @('--fresh') + $configure }
    & cmake @configure
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    $build = @('--build', '--preset', $Preset)
    if ($Target.Count -ne 0) {
        $build += '--target'
        $build += $Target
    }
    & cmake @build
    exit $LASTEXITCODE
}
finally {
    Pop-Location
}
```

This script is environmental tooling, not a repository source change.

- [ ] **Step 3: Close the exact semantic hook-site contract before mutation**

Use the already-established Tune state-machine path to export instruction
windows for:

1. the start of the committed states-16/17 shared transition, before the
   frame-zero input call and BGM `Play`; and
2. the state-18 end branch immediately before it writes/selects state 19 and
   skips judgement for that iteration.

Append one table to the authoritative audit containing, for both sites:

- loaded VA and image-relative RVA;
- at least 12 guarded original bytes without splitting an instruction;
- register/stack location of `CTuneGameManager*`;
- overwritten instruction semantics;
- SafetyHook hook type and continuation address; and
- ordering proof relative to frame-zero input, BGM `Play`, state write, and the
  `0x6401E0` judgement call.

Do not edit source if either site has multiple uncontrolled predecessors, an
ambiguous receiver, or no instruction-safe continuation. That is a concrete
implementation blocker, not permission to restore construction/cleanup
lifecycle.

- [ ] **Step 4: Replace the two lifecycle site contracts**

In `NativeJudgementAbi.h`, remove construction `0x2629A0` and cleanup
`0x262080` as gameplay begin/end sites. Add the two recorded semantic RVAs,
guarded prefixes, continuations, and receiver extraction helpers. Keep the
loop and five query ABI constants unchanged.

The installed set remains:

```text
semantic stage entry
semantic stage exit
owned judgement loop
pressed
held
released
direction
held age
```

Construction and cleanup may remain documented native ownership facts but are
not installed lifecycle hooks.

- [ ] **Step 5: Rename and implement lifecycle APIs**

Use these public runtime signatures:

```cpp
void BeginAbsoluteJudgementSemanticStage(
    std::uintptr_t tune_manager) noexcept;
void EndAbsoluteJudgementSemanticStage(
    std::uintptr_t tune_manager) noexcept;
[[nodiscard]] bool AbsoluteJudgementSemanticStageOpen() noexcept;
```

The entry hook calls `QueryPerformanceCounter` once, then calls:

```cpp
void JudgementScheduler::BeginSemanticStage(
    std::uintptr_t tune_manager,
    std::int64_t stage_entry_qpc,
    std::int32_t game_time_offset_ms,
    std::int32_t hold_safe_frame,
    std::int32_t slide_hold_safe_frame) noexcept;
```

`BeginSemanticStage` performs the synchronized transport cutoff, starts a fresh
generation, resets stage-owned state, and stores `stage_entry_qpc` plus the
three configuration values read by the entry callback. This preserves the
specification's stage-entry `GameTimeOffset` anchor and rejects nonzero safe
frames before gameplay. It does not require an existing BGM origin.

Change the cutoff API to accept that timestamp:

```cpp
bool CaptureGameplayTransitionCutoff(
    std::int64_t stage_entry_qpc,
    GameplayTransitionCutoff* output) noexcept;
```

Add `stage_entry_qpc` and `stage_entry_handoff_drops` to
`GameplayTransitionCutoff`. While holding the existing journal mutex, count
every queued record with `qpc_ticks >= stage_entry_qpc` before clearing
the prefix. Those records become held baseline only. Log/count the loss so it
cannot be mistaken for successful event delivery; repeat any later acceptance
run in which the count is nonzero.

The exit hook calls:

```cpp
void JudgementScheduler::EndSemanticStage(
    std::uintptr_t tune_manager) noexcept;
```

It accounts accepted cleanup loss, logs semantic end, clears stage-owned state,
and closes the generation before native state 19 begins.

- [ ] **Step 6: Make the owned loop fail closed**

Replace the current silent return:

```cpp
if (!AbsoluteJudgementNativeStageOpen()) {
    return;
}
```

with a named lifecycle failure containing the current native state, Tune
pointer, stage generation, hook RVA, and thread ID. Query hooks outside a local
scope continue to trampoline; only the audited state-18 owned loop is forbidden
from falling back.

- [ ] **Step 7: Remove old lifecycle terminology**

Rename `NativeStageOpen`, `native_stage_open`, and construction/cleanup log
labels to `semantic_stage_*` throughout the owned module. Do not retain a
construction or cleanup interception merely to observe lifecycle state: the
audited state-18 exit transition is the sole semantic close, and the installed
set remains exactly eight sites.

- [ ] **Step 8: Run focused static checks and compile**

Run:

```powershell
rg -n "2629A0|262080|BeginNativeStage|EndNativeStage|NativeStageOpen" `
  src/Patches/AbsoluteJudgement
rg -n "return;" src/Patches/AbsoluteJudgement/AbsoluteJudgementPatch.cpp
& 'H:\gc\temp\build-asio-audio-backend.ps1' `
  -Preset msvc32-debug -Target gc_runtime_patches
```

Expected static result: old addresses appear only as documented non-lifecycle
ownership facts, and the loop guard has no silent stock path. Expected build
result: `gc_runtime_patches` succeeds; this is not runtime proof.

- [ ] **Step 9: Review and commit the lifecycle correction**

Review only the Task 1 diff, verify no framerate/audio behavior changed, then:

```powershell
git add -- `
  docs/superpowers/audits/2026-08-21-absolute-judgement-authoritative-full-audit.md `
  src/Patches/AbsoluteJudgement/NativeJudgementAbi.h `
  src/Patches/AbsoluteJudgement/AbsoluteJudgementPatch.cpp `
  src/Patches/AbsoluteJudgement/AbsoluteJudgementRuntime.h `
  src/Patches/AbsoluteJudgement/AbsoluteJudgementRuntime.cpp `
  src/Patches/AbsoluteJudgement/JudgementStage.h `
  src/Patches/AbsoluteJudgement/JudgementStage.cpp `
  src/Patches/AbsoluteJudgement/JudgementScheduler.h `
  src/Patches/AbsoluteJudgement/JudgementScheduler.cpp `
  src/Patches/AbsoluteJudgement/AbsoluteJudgementDiagnostics.h `
  src/Patches/AbsoluteJudgement/AbsoluteJudgementDiagnostics.cpp `
  src/Input/Polling/GameplayTransitionJournal.h `
  src/Input/Polling/GameplayTransitionJournal.cpp
git commit -m "Correct absolute judgement stage lifecycle"
```

Do not stage the pre-existing old plan modification.

---

### Task 2: Replace playback remapping with one continuous stage anchor

**Files:**

- Modify: `src/Patches/AbsoluteJudgement/JudgementClockResolver.{h,cpp}`
- Modify: `src/Patches/AbsoluteJudgement/JudgementStage.{h,cpp}`
- Modify: `src/Patches/AbsoluteJudgement/JudgementScheduler.{h,cpp}`
- Modify: `src/Patches/AbsoluteJudgement/AbsoluteJudgementDiagnostics.{h,cpp}`

**Interfaces:**

- Produces: `JudgementStageClockAnchor`
- Produces: `JudgementClockResolver::Reset(...)`
- Produces: `JudgementClockResolver::TryBind(...)`
- Produces: `JudgementClockResolver::ResolveQpc(...)`
- Consumed by: Task 5 scheduler selection

- [ ] **Step 1: Replace the binding data model**

Replace `ObservedPlaybackHistory`, multi-history validation, closed-frontier
state, and the vector-bearing `JudgementClockBinding` with:

```cpp
struct JudgementStageClockAnchor final {
    std::uint64_t stage_generation{};
    std::uint64_t endpoint_generation{};
    std::uint64_t buffer_instance_id{};
    std::uint64_t playback_generation{};
    std::uint64_t output_origin{};
    std::uint64_t source_origin{};
    std::uint32_t output_rate{};
    std::uint32_t source_rate{};
    std::int32_t game_time_offset_ms{};
    std::shared_ptr<const gc::audio::ExactWasapiClock> endpoint;
};

enum class JudgementClockStatus : std::uint8_t {
    Pending,
    TemporarilyUnavailable,
    Resolved,
    UnsupportedContinuity,
    HistoryLostBeforeBinding,
    CheckedArithmeticFailure,
};

struct JudgementClockResult final {
    JudgementClockStatus status{JudgementClockStatus::Pending};
    std::optional<gc::timing::CheckedRational> output_frame;
    std::optional<gc::timing::CheckedRational> judgement_seconds;
    std::uint64_t endpoint_anchor_sequence{};
    std::optional<std::uint64_t> endpoint_position;
};
```

`JudgementClockBinding` stores at most one anchor. Remove
`observed_stage_bgm_histories` and `history_diagnostics_` if no remaining
diagnostic consumes them. Do not replace dead vectors with fixed arrays.

- [ ] **Step 2: Implement stage reset and entry watermark**

Use these resolver operations:

```cpp
void Reset(std::uint64_t stage_generation,
           std::int64_t stage_entry_qpc,
           std::int32_t game_time_offset_ms) noexcept;

[[nodiscard]] bool bound() const noexcept;
[[nodiscard]] const JudgementStageClockAnchor& anchor() const noexcept;
```

Reset clears the prior anchor and records the immutable stage generation, entry
QPC, and `GameTimeOffset`. A live offset change after binding is diagnostic
only; it cannot alter the stored value.

- [ ] **Step 3: Bind the first qualifying stage Play**

Implement:

```cpp
JudgementClockResult TryBind(
    const gc::audio::GameplayAudioCursorObservation& selected,
    std::shared_ptr<const gc::audio::ExactWasapiClock> endpoint,
    std::span<gc::audio::ExactPlaybackEpoch> scratch) noexcept;
```

The operation must:

1. resolve `stage_entry_qpc` through the endpoint to exact output `O_entry`;
2. copy the selected history once into scratch;
3. ignore every epoch with the wrong buffer or endpoint generation;
4. find the earliest `Play` epoch with `output_origin >= O_entry` that is not
   later than the selected observation's current playback generation;
5. require nonzero source/output rates and matching selected history identity;
6. store exactly that epoch as the immutable anchor; and
7. never bind a `Seek` as a new stage origin.

`Pending`/`TemporarilyUnavailable` retain work without timeout. Loss of required
entry-QPC or playback history before binding is the exact unsupported
capability predicate. A normal current `Seek` may still bind through its
preceding retained `Play`.

- [ ] **Step 4: Implement one exact QPC projection**

Implement:

```cpp
JudgementClockResult ResolveQpc(std::int64_t qpc_ticks) const noexcept;
```

After endpoint projection returns exact `O`, calculate only:

```text
J = source_origin/source_rate
  + game_time_offset_ms/1000
  + (O - output_origin)/output_rate
```

Allow `O < output_origin`. Do not inspect later playback epochs, current source
cursor, closed frontier, natural end, or overlap equality. Endpoint generation
change becomes `UnsupportedContinuity`; endpoint temporary unavailability stays
nonfatal and pending.

- [ ] **Step 5: Delete remapping state and false invariants**

Remove code and fatal emitters for:

- `FindFirstPlaybackOrigin` selecting `scratch[0]`;
- `ValidateRetainedHistories` cross-buffer equality;
- per-Play/Seek source-frame remapping;
- `OutsidePlayback` and closed-frontier judgement statuses;
- backward `J` caused by selecting a later epoch;
- `GameTimeOffsetChanged` as fatal; and
- playback-history prefix eviction after anchor as a judgement failure.

Later Play/Seek/closure counters may remain nonfatal diagnostics only if they
have truthful producers.

- [ ] **Step 6: Prove the formula contains no render term**

Run:

```powershell
rg -n "target_fps|frame_milliseconds|OutsidePlayback|closed_frontier|FindFirstPlaybackOrigin|ValidateRetainedHistories" `
  src/Patches/AbsoluteJudgement/JudgementClockResolver.h `
  src/Patches/AbsoluteJudgement/JudgementClockResolver.cpp `
  src/Patches/AbsoluteJudgement/JudgementScheduler.h `
  src/Patches/AbsoluteJudgement/JudgementScheduler.cpp
rg -n "game_time_offset|source_origin|output_origin|source_rate|output_rate" `
  src/Patches/AbsoluteJudgement/JudgementClockResolver.cpp
& 'H:\gc\temp\build-asio-audio-backend.ps1' `
  -Preset msvc32-debug -Target gc_runtime_patches
```

Expected: no forbidden resolver terms, one direct anchor formula, successful
focused build.

- [ ] **Step 7: Review and commit the one-anchor clock**

```powershell
git add -- `
  src/Patches/AbsoluteJudgement/JudgementClockResolver.h `
  src/Patches/AbsoluteJudgement/JudgementClockResolver.cpp `
  src/Patches/AbsoluteJudgement/JudgementStage.h `
  src/Patches/AbsoluteJudgement/JudgementStage.cpp `
  src/Patches/AbsoluteJudgement/JudgementScheduler.h `
  src/Patches/AbsoluteJudgement/JudgementScheduler.cpp `
  src/Patches/AbsoluteJudgement/AbsoluteJudgementDiagnostics.h `
  src/Patches/AbsoluteJudgement/AbsoluteJudgementDiagnostics.cpp
git commit -m "Bind one continuous absolute judgement clock"
```

---

### Task 3: Make retained input history chronological and time-causal

**Files:**

- Modify: `src/Patches/AbsoluteJudgement/JudgementHistory.{h,cpp}`
- Modify: `src/Patches/AbsoluteJudgement/JudgementScheduler.{h,cpp}`

**Interfaces:**

- Produces: `StateOnlyReason::{AcceptedLate,Overload}`
- Produces: `JudgementHistory::AppendStateOnly(...)`
- Produces: `JudgementHistory::ConvertResolvedToStateOnly(...)`
- Produces: `JudgementHistory::ReleasedInWindow(...)`
- Consumed by: Tasks 4 and 5

- [ ] **Step 1: Replace baseline-only storage with timestamped state-only storage**

Replace:

```cpp
enum class BaselineOnlyReason : std::uint8_t {
    OutsidePlayback,
    AcceptedLate,
    Overload,
};
```

with:

```cpp
enum class StateOnlyReason : std::uint8_t {
    AcceptedLate,
    Overload,
};
```

Every retained entry, including state-only entries, stores a complete
`ResolvedGameplayTransition` with exact `judgement_seconds`. There is no API
that accepts an untimestamped transport record after semantic stage entry.

- [ ] **Step 2: Unify append, conversion, and delivery consumption**

Use these APIs:

```cpp
std::expected<void, JudgementHistoryError> Append(
    const ResolvedGameplayTransition& transition) noexcept;

std::expected<void, JudgementHistoryError> AppendStateOnly(
    const ResolvedGameplayTransition& transition,
    StateOnlyReason reason) noexcept;

std::expected<void, JudgementHistoryError> ConvertResolvedToStateOnly(
    std::uint64_t sequence,
    StateOnlyReason reason) noexcept;
```

The scheduler advances `next_delivery_sequence_` exactly once when either an
event scope or state-only record is consumed. Accepted-late records behind the
committed frontier are consumed state-only immediately in sequence; overload
records stay ordered until their chronological turn.

- [ ] **Step 3: Correct retained-base counting and pruning**

`CountResolvedAtOrBefore(first_sequence, ready)` must:

- reject only `first_sequence > next_sequence_`;
- begin iteration at `max(first_sequence, base_next_sequence_)`;
- count resolved entries through `ready`; and
- never call a consumed state-only prefix lost history.

This deliberately absorbs the pre-existing one-line dirty edit but does not
stop at it.

`StateAt(query_time,prefix)` and `PruneBefore(query_time,prefix)` must require
both sequence eligibility and `entry.judgement_seconds <= query_time` before
applying/folding an entry. Preserve the inclusive `4/60` resolved-edge suffix
needed by paired queries.

- [ ] **Step 4: Add exact historical released-window lookup**

Add:

```cpp
std::expected<bool, JudgementHistoryError> ReleasedInWindow(
    std::uint32_t control,
    const gc::timing::CheckedRational& window_end,
    std::uint64_t history_prefix_end_sequence) const noexcept;
```

The window is exactly:

```text
window_end - 1/60 < event.J <= window_end
```

Iterate only resolved events below the immutable prefix. For every qualifying
event, reuse the existing falling-edge ordinary/composite/paired algebra. A
paired current-window constituent may use the other constituent within the
selected inclusive prior `4/60` exact history. Return false if the finite
window is not retained or lies in the causal future; do not produce
`HistoryLost` merely for this compatibility query.

- [ ] **Step 5: Remove playback-derived history vocabulary and counters**

Remove `OutsidePlayback`, `outside_playback_baseline_records`, and any branch
that deletes an edge because no playback epoch covers its QPC. Keep separately
named accepted-late and overload state-only counts.

- [ ] **Step 6: Perform static trace checks and focused build**

Manually trace these two cases in code comments or the task report, without an
executable fixture:

```text
release J=2.000, query J=1.993 -> release must not affect held state
history base=66, delivery request=64 -> counting starts at retained base, no fatal
```

Then run:

```powershell
rg -n "OutsidePlayback|BaselineOnlyReason|ApplyBaselineOnly" `
  src/Patches/AbsoluteJudgement
rg -n "judgement_seconds.*Compare|base_next_sequence|next_delivery_sequence" `
  src/Patches/AbsoluteJudgement/JudgementHistory.cpp `
  src/Patches/AbsoluteJudgement/JudgementScheduler.cpp
& 'H:\gc\temp\build-asio-audio-backend.ps1' `
  -Preset msvc32-debug -Target gc_runtime_patches
```

Expected: no playback-derived history category and successful focused build.

- [ ] **Step 7: Review and commit causal history**

```powershell
git add -- `
  src/Patches/AbsoluteJudgement/JudgementHistory.h `
  src/Patches/AbsoluteJudgement/JudgementHistory.cpp `
  src/Patches/AbsoluteJudgement/JudgementScheduler.h `
  src/Patches/AbsoluteJudgement/JudgementScheduler.cpp
git commit -m "Make absolute input history time causal"
```

---

### Task 4: Restore native-compatible scoped queries

**Files:**

- Modify: `src/Patches/AbsoluteJudgement/JudgementScope.{h,cpp}`
- Modify: `src/Patches/AbsoluteJudgement/AbsoluteJudgementPatch.cpp`
- Modify: `src/Patches/AbsoluteJudgement/AbsoluteJudgementRuntime.cpp`
- Modify: `src/Patches/AbsoluteJudgement/AbsoluteJudgementDiagnostics.{h,cpp}`

**Interfaces:**

- Consumes: `JudgementHistory::ReleasedInWindow(...)`
- Preserves: current-frame pressed invariant
- Produces: native-neutral invalid controls/selectors
- Produces: unrelated-thread trampoline behavior

- [ ] **Step 1: Return native-neutral values for unsupported control IDs**

Inside a valid local scope:

```text
Pressed(control outside 0..19)  -> 0
Held(control outside 0..19)     -> 0
Released(control outside 0..19) -> 0
HeldAge(control >= 20)          -> 0
```

Return `Answered` neutral results. Do not create `InvalidControl`, history
failure, or active fatal for these values.

- [ ] **Step 2: Split direction pointer validity from selector behavior**

If `x == nullptr || y == nullptr`, emit the exact proven native call-contract
predicate. Otherwise initialize both outputs to zero before selector dispatch.

For selectors `0`, `1`, and `2`, retain the current native priority,
cancellation, and selector-2 return-bit algebra. For another selector, return:

```cpp
static_cast<int>(reinterpret_cast<std::uintptr_t>(x))
```

after zeroing both outputs. This matches the supported x86 helper ABI and is
not a booster-device count check.

- [ ] **Step 3: Support non-current released frames**

Keep current-frame release isolated to `scope.falling`. For another frame:

```cpp
const auto window_end = TranslateRequestedFrame(scope, requested_frame);
return history.ReleasedInWindow(
    control, *window_end, scope.history_prefix_end_sequence);
```

Do not reject `requested_frame != native_frame`. Future or unretained windows
return false. Preserve the exact current-frame restriction for pressed because
all scoped pressed callers are proven current-frame.

- [ ] **Step 4: Remove the global foreign-thread fatal**

If the current thread has no TLS scope, return `Inactive` and let the hook call
its native trampoline, even when another thread owns a different local scope.
When TLS scope exists, continue to require its installing thread, stage
generation, fixed receiver, immutable data, and destructor ownership.

- [ ] **Step 5: Keep one fixed CBooster invariant**

Do not add booster discovery, side arrays, or multiple-device handling. Inside
the local scope, the receiver must equal the one CBooster captured for that
semantic generation. Missing input manager/CBooster at stage use remains an
explicit unsupported predicate.

- [ ] **Step 6: Run query-domain static checks and focused build**

```powershell
rg -n "InvalidControl|InvalidDirectionArguments|requested_frame !=|g_active_scope_thread" `
  src/Patches/AbsoluteJudgement/JudgementScope.h `
  src/Patches/AbsoluteJudgement/JudgementScope.cpp `
  src/Patches/AbsoluteJudgement/AbsoluteJudgementPatch.cpp
rg -n "ReleasedInWindow|TranslateRequestedFrame|reinterpret_cast<std::uintptr_t>" `
  src/Patches/AbsoluteJudgement
& 'H:\gc\temp\build-asio-audio-backend.ps1' `
  -Preset msvc32-debug -Target gc_runtime_patches
```

Expected: no fatal invalid-control/selector/non-current-release path, local
scope invariants retained, successful build.

- [ ] **Step 7: Review and commit native query compatibility**

```powershell
git add -- `
  src/Patches/AbsoluteJudgement/JudgementScope.h `
  src/Patches/AbsoluteJudgement/JudgementScope.cpp `
  src/Patches/AbsoluteJudgement/AbsoluteJudgementPatch.cpp `
  src/Patches/AbsoluteJudgement/AbsoluteJudgementRuntime.cpp `
  src/Patches/AbsoluteJudgement/AbsoluteJudgementDiagnostics.h `
  src/Patches/AbsoluteJudgement/AbsoluteJudgementDiagnostics.cpp
git commit -m "Restore native absolute query compatibility"
```

---

### Task 5: Reconnect the scheduler to semantic lifecycle, one clock, and causal history

**Files:**

- Modify: `src/Patches/AbsoluteJudgement/JudgementScheduler.{h,cpp}`
- Modify: `src/Patches/AbsoluteJudgement/AbsoluteJudgementRuntime.{h,cpp}`
- Modify: `src/Patches/AbsoluteJudgement/AbsoluteJudgementDiagnostics.{h,cpp}`

**Interfaces:**

- Consumes: semantic stage lifecycle from Task 1
- Consumes: one-anchor resolver from Task 2
- Consumes: causal history from Task 3
- Preserves: exact event/heartbeat scope API used by runtime

- [ ] **Step 1: Simplify outer-call clock acquisition**

`PrepareOuterCall` must:

1. verify semantic stage identity and zero safe frames;
2. drain all currently available journal records in sequence;
3. acquire one endpoint/current group-2 observation;
4. bind the anchor if it is not yet bound;
5. map unresolved transition QPCs through the bound anchor in sequence;
6. map `now_qpc` through the same anchor for the ready horizon; and
7. select ordered work without consulting playback source epochs again.

Before binding, retain all records and return no scope. There is no elapsed
timer or stock call. After binding, natural drain/stop/Seek cannot freeze the
stage coordinate or convert an event state-only.

- [ ] **Step 2: Enforce one chronological delivery cursor**

Delete any independent bookkeeping that lets history storage consume a record
without advancing its delivery disposition. `next_delivery_sequence_` advances
on exactly these operations:

```text
commit EventEligible scope
consume AcceptedLateStateOnly
consume OverloadStateOnly
```

It never advances merely because a record was drained or mapped. History
pruning uses committed time/prefix, not delivery-cursor equality.

- [ ] **Step 3: Preserve the approved batch topology**

Retain:

```text
event batch     = exactly one event scope
heartbeat batch = one to three heartbeat scopes
```

For each scope, call native recognition once and score once. After the selected
batch, reach the original native tail exactly once. An event batch contains no
second recognition that can clear its transient native sound/effect bytes
before that tail.

- [ ] **Step 4: Preserve exact heartbeat construction and overload policy**

Construct every boundary directly as `index/60`; never increment a rounded
time. Keep newest 32 simultaneously ready undelivered events eligible. Mark
older excess state-only and consume them at their chronological turn. Keep
accepted-late separate from overload. Unknown eviction remains a resource
failure.

- [ ] **Step 5: Remove obsolete scheduler state**

Delete state used only by the failed model, including:

- selected playback-history vectors;
- frozen/closed-frontier judgement coordinates;
- source-frame comparisons after anchor;
- `OutsidePlayback` counters;
- per-epoch conflict/backward handling;
- persistent overload shrink checks caused by a regressing ready frontier; and
- cleanup-time semantic stage closure.

Retain current one-anchor output/J diagnostics only when they have a producer.

- [ ] **Step 6: Run scheduler construction review and focused build**

Use a written static trace for:

```text
stage entry -> press/release before BGM Play -> anchor binds -> negative-J events
normal Seek -> later input -> same monotonic J
natural audio drain -> later stage input -> same monotonic J until state 18 exits
ten-boundary hitch -> heartbeat batches of 3/3/3/1 with no skipped boundary
33 ready events -> oldest state-only, newest 32 eligible in exact order
```

Then run:

```powershell
rg -n "OutsidePlayback|closed_frontier|frozen_j|last_source_frame|PlaybackMappingConflict" `
  src/Patches/AbsoluteJudgement/JudgementScheduler.h `
  src/Patches/AbsoluteJudgement/JudgementScheduler.cpp
rg -n "BoundaryAt|kProtectedReadyEventCount|outer_event_scope_count" `
  src/Patches/AbsoluteJudgement/JudgementScheduler.cpp
& 'H:\gc\temp\build-asio-audio-backend.ps1' `
  -Preset msvc32-debug -Target iDmacDrv32
```

Expected: failed-model state absent, exact batching present, successful Debug
DLL build.

- [ ] **Step 7: Review and commit scheduler reconnection**

```powershell
git add -- `
  src/Patches/AbsoluteJudgement/JudgementScheduler.h `
  src/Patches/AbsoluteJudgement/JudgementScheduler.cpp `
  src/Patches/AbsoluteJudgement/AbsoluteJudgementRuntime.h `
  src/Patches/AbsoluteJudgement/AbsoluteJudgementRuntime.cpp `
  src/Patches/AbsoluteJudgement/AbsoluteJudgementDiagnostics.h `
  src/Patches/AbsoluteJudgement/AbsoluteJudgementDiagnostics.cpp
git commit -m "Reconnect the absolute judgement scheduler"
```

---

### Task 6: Replace false assertions with exact fatal predicates and nonfatal diagnostics

**Files:**

- Modify: `src/Patches/AbsoluteJudgement/AbsoluteJudgementDiagnostics.{h,cpp}`
- Modify: `src/Patches/AbsoluteJudgement/AbsoluteJudgementPatch.cpp`
- Modify: `src/Patches/AbsoluteJudgement/AbsoluteJudgementRuntime.cpp`
- Modify: `src/Patches/AbsoluteJudgement/JudgementStage.cpp`
- Modify: `src/Patches/AbsoluteJudgement/JudgementClockResolver.cpp`
- Modify: `src/Patches/AbsoluteJudgement/JudgementHistory.cpp`
- Modify: `src/Patches/AbsoluteJudgement/JudgementScope.cpp`
- Modify: `src/Patches/AbsoluteJudgement/JudgementScheduler.cpp`
- Modify: `src/Input/Polling/InputPollingRuntime.cpp`

**Interfaces:**

- Produces: `AbsoluteJudgementFailureClass`
- Produces: `AbsoluteJudgementFatalPredicate`
- Produces: `AbsoluteJudgementFatalRecord`
- Produces: visible, flushed, no-timeout active fatal surface

- [x] **Step 1: Replace broad reasons as primary evidence**

Add:

```cpp
enum class AbsoluteJudgementFailureClass : std::uint8_t {
    ExplicitlyUnsupported,
    ResourceLimit,
    ProvenInternalInvariant,
};

struct AbsoluteJudgementFatalRecord final {
    AbsoluteJudgementFatalPredicate predicate{};
    AbsoluteJudgementFailureClass classification{};
    std::uint64_t stage_generation{};
    AbsoluteJudgementFatalReason category{};
    std::array<std::uint64_t, 8> operands{};
    std::uint8_t operand_count{};
};
```

Define these stable predicate values:

```cpp
enum class AbsoluteJudgementFatalPredicate : std::uint32_t {
    StartupSitePrefixMismatch,
    StartupHookCreateFailed,
    StartupHookEnableFailed,
    StartupHookTransactionInvalid,
    GameImageAddressInvalid,
    SemanticStageAlreadyOpen,
    SemanticStageMissingAtOwnedLoop,
    SemanticStageExitWithoutOpen,
    SemanticStageReceiverMismatch,
    CleanupWhileSemanticStageOpen,
    StageGenerationExhausted,
    QueryPerformanceCounterFailed,
    InputTransportRateNot1000,
    InputTransportInactiveAtStageEntry,
    InputTransportWorkerBecameInactive,
    InputTransportEpochChanged,
    InputQpcFrequencyInvalidAtStageEntry,
    InputQpcFrequencyChanged,
    InputManagerMissing,
    BoosterMissing,
    TuneMissing,
    JudgementStateMissing,
    ScoreStateMissing,
    PlayerIndexInvalid,
    TuneIdentityChanged,
    JudgementStateIdentityChanged,
    ScoreStateIdentityChanged,
    PlayerIdentityChanged,
    BoosterIdentityChanged,
    HoldSafeFrameNonZero,
    SlideHoldSafeFrameNonZero,
    EndpointProviderMissingAtStageExit,
    StageOriginUnboundAtStageExit,
    EndpointGenerationChanged,
    EndpointProviderIdentityChanged,
    EndpointPublicationSequenceRegressed,
    EndpointQpcFrequencyMismatch,
    EndpointProjectionDiscontinuous,
    StageOriginHistoryLost,
    PlaybackHistoryObjectChangedBeforeAnchor,
    PlaybackHistoryEndpointChangedBeforeAnchor,
    TransportEvicted,
    TransportSequenceDiscontinuous,
    TransportMaskMismatch,
    TransportDrainContradiction,
    UnresolvedCapacityExhausted,
    HistoryCapacityExhausted,
    SequenceExhausted,
    RationalOperationUnrepresentable,
    HistoryNotInitialized,
    HistoryPrefixBeyondNext,
    HistoryPromisedEntryMissing,
    HistoryBaselineMaskInvalid,
    ResolvedCoordinateRegressed,
    DeliveryOrderViolated,
    UnresolvedFrontEmpty,
    ScopeAlreadyActive,
    ScopeTlsOwnerMismatch,
    ScopeGenerationMismatch,
    ScopeReceiverMismatch,
    ScopeLifetimeMismatch,
    PressedFrameMismatch,
    DirectionOutputNull,
    RecognitionScoreTopologyMismatch,
    CommitTopologyMismatch,
    StartupFatalPublisherReturned,
    TerminateProcessReturned,
};
```

Map each enum to a static predicate name/expression and fixed operand labels.
If the mechanical emitter scan finds a terminating Boolean not represented
here, add one equally specific predicate before committing; never map it to a
generic default.

Use the audit's classification, not the call site that happened to detect the
predicate: unsupported input configuration/continuity and unsupported endpoint
availability/generation are `ExplicitlyUnsupported`; fixed-capacity,
generation/sequence exhaustion, and core rational representability are
`ResourceLimit`; preflighted address, stable identity, registry publication,
history, scope, and native-transaction contradictions are
`ProvenInternalInvariant`. A later playback-history observation after the
single anchor is diagnostic and must never emit one of the before-anchor
history predicates.

Do not retain `NativeStateMismatch`, `RetainedHistoryLost`, or
`CheckedArithmeticFailure` as the only logged fact. A broad category may remain
secondary metadata.

- [x] **Step 2: Make all observations nonfatal**

Change these operations to saturate, invalidate, or log unavailable:

```text
query/result counters
batch/statistics counters
score read/regression/delta counters
transient arrange/free-tap publication reads and counts
delivery-delay conversion
Play/Seek diagnostic counts and comparisons
final accounting comparison
diagnostic monotonic subtraction
```

None may call `FatalActiveStage`, `FailActiveStage`, `std::abort`, or fail-fast.
Keep recognition-without-score and incomplete scope commit fatal because native
state is partially mutated.

- [x] **Step 3: Remove C++ exception control flow**

Replace `HookLoopGuard`'s `try`, `catch(std::bad_alloc)`, and `catch(...)` with
one direct call to `DispatchAbsoluteJudgementOuterCall(context)`. Remove
`StorageAllocationFailure` and `UnexpectedInternalException` emitters. Remove
the enum values if no persisted log-compatibility requirement consumes them;
otherwise mark them retired and never emit them.

Retain native-memory SEH guards because they are ABI probes, not C++ exception
recovery.

- [x] **Step 4: Implement the visible active fatal surface**

The first fatal path must perform, in this order:

```text
atomically latch first predicate
stop recognition dispatch
format one predicate record with std::format_to_n into fixed local storage
PLOG_FATAL the exact record and snapshot
FlushActiveProcessLog
show a modal MessageBoxW with no timeout
TerminateProcess(GetCurrentProcess(), 0xA7)
fail-fast/abort only if TerminateProcess unexpectedly returns
```

The dialog text must include the stable predicate ID and tell the operator to
retain `loader-log.txt`. A second concurrent fatal emergency-logs its predicate
and terminates; it must not wait indefinitely without recording the second
condition.

Use static predicate/operand labels and fixed `std::array<char, N>` /
`std::array<wchar_t, N>` buffers in the fatal path. Record truncation explicitly.
Do not allocate `std::string` while trying to report a fatal condition and do
not add an exception handler around formatting.

- [x] **Step 5: Give every raw abort a preceding exact log**

Apply the audit's raw-abort inventory to:

- input polling QPC failure;
- invalid history reset mask;
- empty unresolved-front access;
- scope rollback/destructor ownership;
- startup fatal publisher unexpectedly returning;
- install-stage default/fallthrough;
- repeated active fatal; and
- `TerminateProcess` unexpectedly returning.

Diagnostic subtraction must lose its abort entirely.

- [x] **Step 6: Run the mechanical failure-surface scan**

```powershell
rg -n "try\s*\{|catch\s*\(|bad_alloc|UnexpectedInternalException|StorageAllocationFailure" `
  src/Patches/AbsoluteJudgement
rg -n "Fatal\(|Fail|abort\(|TerminateProcess|__fastfail" `
  src/Patches/AbsoluteJudgement `
  src/Input/Polling/InputPollingRuntime.cpp
rg -n "ostringstream|wostringstream" src/Patches/AbsoluteJudgement
```

For every termination line, record the immediately preceding stable predicate
and operand construction in the task report. Expected: no C++ catch, no string
stream, no raw unexplained abort.

- [x] **Step 7: Compile and commit the failure-policy correction**

```powershell
& 'H:\gc\temp\build-asio-audio-backend.ps1' `
  -Preset msvc32-debug -Target iDmacDrv32
git add -- `
  src/Patches/AbsoluteJudgement `
  src/Input/Polling/InputPollingRuntime.cpp
git commit -m "Make absolute judgement failures predicate exact"
```

Do not stage unrelated input changes.

**Implemented failure-policy record (2026-08-22):**

- The terminating contract is now a predicate record. All 75 terminating
  predicate values, plus the non-terminating `None` sentinel, have a static
  name, Boolean-expression description, and fixed operand-label set; failure
  classification is derived centrally rather
  than selected by the detecting call site.
- Broad reason values remain secondary log categories only. History errors
  were split into capacity, prefix, promised-entry, sequence-exhaustion, and
  baseline-mask predicates, with the actual failed operands retained at the
  point of detection.
- Score reads/regressions/deltas, transient publication reads/counts,
  query/stat counters, delivery-delay conversion, final accounting, and
  diagnostic interval subtraction now invalidate, clamp, saturate, or warn.
  None enters the active fatal path.
- The active fatal path atomically latches the first exact predicate, blocks
  further recognition on the game thread, formats into fixed storage, logs and
  flushes, displays an untimed modal dialog containing the predicate ID, and
  terminates. A concurrent fatal logs both IDs and terminates without the old
  infinite wait.
- Raw-abort inventory is closed: input QPC failure logs
  `QueryPerformanceCounterFailed`; invalid history baseline returns
  `HistoryBaselineMaskInvalid`; empty unresolved access logs
  `UnresolvedFrontEmpty`; TLS rollback/destruction logs exact scope ownership;
  startup-publisher return logs `StartupFatalPublisherReturned`; install
  fallthrough logs `StartupHookTransactionInvalid`; and every unexpected
  `TerminateProcess` return logs `TerminateProcessReturned` before fail-fast.
- All C++ `try`/`catch` control flow was removed from the absolute judgement
  path and its 1000 Hz input runtime. Native-memory `__try`/`__except` probes
  remain because they are ABI guards, not exception recovery.
- Mechanical scan found no string streams, retired broad exception reasons,
  hidden process waits, or unexplained aborts in the task scope. The predicate
  descriptor coverage scan reported `enum_count=76`,
  `descriptor_count=76`, with no missing or extra entry (`None` included).
- `msvc32-debug` target `iDmacDrv32` linked successfully after the correction.
  This is static/build evidence only; it is not gameplay acceptance.

---

### Task 7: Perform controller review, complete builds, and x86 ABI inspection

**Files:**

- Review: all files changed by Tasks 1-6
- Create: `H:\gc\temp\inspect-asio-audio-backend-abi.ps1`
- Do not create repository tests or fixtures

**Interfaces:**

- Produces: static review record
- Produces: complete Debug and Release build evidence
- Produces: PE32/export/hook ABI evidence
- Consumed by: Task 8 deployment

- [ ] **Step 1: Review the complete diff against the spec and audit**

Inspect:

```powershell
git diff b623f75..HEAD -- `
  src/Patches/AbsoluteJudgement `
  src/Input/Polling/GameplayTransitionJournal.h `
  src/Input/Polling/GameplayTransitionJournal.cpp `
  src/Input/Polling/InputPollingRuntime.cpp
git diff --check
```

Ignore newline-only churn as instructed. Verify behaviorally:

- semantic entry precedes frame-zero input/BGM Play;
- semantic exit is the state-18 -> 19 branch;
- every stage/retry gets one fresh generation;
- one anchor formula has no target-FPS term;
- no playback state deletes stage input;
- state-only history is time-causal;
- current event isolation and non-current release support coexist;
- invalid controls/selectors are native-neutral;
- unrelated threads trampoline;
- held-age/paired selected policy is unchanged;
- newest-32 and three-heartbeat policies remain;
- no diagnostic kills gameplay;
- every hard stop has exact proof/log operands and visible prompt;
- no C++ catch or mixed fallback exists; and
- framerate/audio behavior outside exact read-only observation is unchanged.

Fix each behavioral defect inline and commit it with a message naming the
invariant. Do not make formatting-only commits.

- [ ] **Step 2: Create the persistent ABI script**

Create `H:\gc\temp\inspect-asio-audio-backend-abi.ps1`. It must:

1. enter the same VS18 HostX86/x86 environment as the build script;
2. inspect
   `build-msvc32-release\dist\iDmacDrv32.dll` with `dumpbin /headers`;
3. require machine `14C (x86)`, magic `10B (PE32)`, and Windows GUI subsystem;
4. compare `dumpbin /exports` against
   `src\Driver\iDmac\iDmacDrv32.def`, including legacy
   `iDmacDrvProgramDownload @15`;
5. disassemble the Release `AbsoluteJudgementPatch.cpp.obj`;
6. require the expected x86 stack cleanup for pressed/held/released (`ret 8`),
   direction (`ret 10h`), and held-age (`ret 4`);
7. identify both new semantic mid-hook thunks and the owned loop guard; and
8. print the Release DLL SHA-256 only after every assertion passes.

The semantic entry/exit hooks use `safetyhook::Context`; they do not receive a
fabricated `__fastcall` return cleanup contract.

Use this script body:

```powershell
[CmdletBinding()]
param(
    [string] $RepoRoot =
        'H:\gc\artifacts\GCLoader\.worktrees\asio-audio-backend'
)

$ErrorActionPreference = 'Stop'
$vsDevShell =
    'C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\Tools\Launch-VsDevShell.ps1'
& $vsDevShell -Arch x86 -HostArch x86 -SkipAutomaticLocation
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
if ($env:VSCMD_ARG_TGT_ARCH -ne 'x86' -or
    $env:VSCMD_ARG_HOST_ARCH -ne 'x86') {
    throw 'Expected HostX86/x86 environment'
}

$dumpbin = (Get-Command dumpbin.exe -ErrorAction Stop).Source
$dll = Join-Path $RepoRoot `
    'build-msvc32-release\dist\iDmacDrv32.dll'
$object = Join-Path $RepoRoot `
    'build-msvc32-release\src\Patches\CMakeFiles\gc_runtime_patches.dir\AbsoluteJudgement\AbsoluteJudgementPatch.cpp.obj'
$definition = Join-Path $RepoRoot 'src\Driver\iDmac\iDmacDrv32.def'
foreach ($path in @($dll, $object, $definition)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required Release artifact not found: $path"
    }
}

function Invoke-Dumpbin {
    param([Parameter(Mandatory)][string[]] $Arguments)
    $output = & $dumpbin @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "dumpbin failed: $($Arguments -join ' ')"
    }
    return @($output)
}

function Assert-Match {
    param(
        [Parameter(Mandatory)][AllowEmptyCollection()][string[]] $Lines,
        [Parameter(Mandatory)][string] $Pattern,
        [Parameter(Mandatory)][string] $Description
    )
    if (-not ($Lines -match $Pattern)) {
        throw "ABI inspection failed: $Description"
    }
}

$headers = Invoke-Dumpbin -Arguments @('/headers', $dll)
Assert-Match $headers '^\s*14C machine \(x86\)\s*$' 'DLL is not x86'
Assert-Match $headers '^\s*10B magic # \(PE32\)\s*$' 'DLL is not PE32'
Assert-Match $headers '^\s*2 subsystem \(Windows GUI\)\s*$' `
    'DLL subsystem changed'

$exports = Invoke-Dumpbin -Arguments @('/exports', $dll)
$expectedExports = foreach ($line in Get-Content -LiteralPath $definition) {
    if ($line -match '^\s*(iDmacDrv\w+)\s+@(\d+)\s*$') {
        [pscustomobject]@{ Name = $Matches[1]; Ordinal = [int]$Matches[2] }
    }
}
foreach ($expected in $expectedExports) {
    $name = [regex]::Escape($expected.Name)
    $pattern =
        "^\s*$($expected.Ordinal)\s+[0-9A-F]+\s+[0-9A-F]+\s+$name(?:\s|$)"
    Assert-Match $exports $pattern `
        "missing export $($expected.Name) @$($expected.Ordinal)"
}
Assert-Match $exports `
    '^\s*15\s+[0-9A-F]+\s+[0-9A-F]+\s+iDmacDrvProgramDownload(?:\s|$)' `
    'legacy iDmacDrvProgramDownload @15 changed'

$disassembly = Invoke-Dumpbin -Arguments @('/disasm', $object)
$requiredHookSymbols = @(
    'HookSemanticStageEntry',
    'HookSemanticStageExit',
    'HookLoopGuard'
)
foreach ($symbol in $requiredHookSymbols) {
    Assert-Match $disassembly ([regex]::Escape($symbol)) `
        "missing hook symbol $symbol"
}

$expectedReturns = [ordered]@{
    HookPressed = 'ret 8'
    HookHeld = 'ret 8'
    HookReleased = 'ret 8'
    HookDirection = 'ret 10h'
    HookHeldAge = 'ret 4'
}
foreach ($entry in $expectedReturns.GetEnumerator()) {
    $start = -1
    for ($index = 0; $index -lt $disassembly.Count; ++$index) {
        if ($disassembly[$index] -match
            "^\?$($entry.Key)@.*:$") {
            $start = $index
            break
        }
    }
    if ($start -lt 0) {
        throw "Hook not found: $($entry.Key)"
    }

    $returns = [System.Collections.Generic.List[string]]::new()
    for ($index = $start + 1; $index -lt $disassembly.Count; ++$index) {
        $line = $disassembly[$index]
        if ($line -match '^\S.*:$') { break }
        if ($line -match '\bret(?:\s+([0-9A-F]+h?|0))?\s*$') {
            $returns.Add(
                $(if ($Matches[1]) { "ret $($Matches[1])" } else { 'ret' }))
        }
    }
    $actual = @($returns | Sort-Object -Unique)
    if ($actual.Count -ne 1 -or $actual[0] -ne $entry.Value) {
        throw "$($entry.Key): expected $($entry.Value), found $($actual -join ', ')"
    }
    "ABI return: $($entry.Key) -> $($actual[0])"
}

$hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $dll).Hash
"Release ABI inspection passed: $hash"
```

- [ ] **Step 3: Run complete Debug and Release builds**

```powershell
& 'H:\gc\temp\build-asio-audio-backend.ps1' `
  -Preset msvc32-debug -Fresh
& 'H:\gc\temp\build-asio-audio-backend.ps1' `
  -Preset msvc32-release -Fresh
```

Expected: both complete preset graphs succeed. Do not run CTest.

- [ ] **Step 4: Run ABI inspection and record hashes**

```powershell
& 'H:\gc\temp\inspect-asio-audio-backend-abi.ps1'
Get-FileHash -Algorithm SHA256 `
  'build-msvc32-debug\dist\iDmacDrv32.dll', `
  'build-msvc32-release\dist\iDmacDrv32.dll'
```

Record the exact output in the execution report. This is build/ABI evidence,
not gameplay acceptance.

- [ ] **Step 5: Confirm the repository boundary**

```powershell
git status --short
git diff --name-only b623f75..HEAD
```

Expected changed source is limited to the planned absolute-judgement module,
the stage-entry cutoff accounting in `GameplayTransitionJournal.{h,cpp}`, and
the predicate-exact QPC abort log in `InputPollingRuntime.cpp`. The old plan
modification remains unstaged unless the user separately authorizes it.

---

### Task 8: Back up and deploy the verified Release DLL

**Files:**

- Read: `build-msvc32-release\dist\iDmacDrv32.dll`
- Back up: `H:\gc\iDmacDrv32.dll`
- Deploy: `H:\gc\iDmacDrv32.dll`
- Preserve unchanged: `H:\gc\config.toml`

**Interfaces:**

- Consumes: passing Task 7 Release and ABI evidence
- Produces: installed Release DLL plus recoverable backup and hashes
- Runtime acceptance remains user-operated

- [ ] **Step 1: Verify the game is not running**

```powershell
$game = Get-Process -Name 'game471' -ErrorAction SilentlyContinue
if ($game) {
    throw 'game471 is still running; deployment cannot replace the DLL safely'
}
```

Do not terminate an unexpected live game process automatically.

- [ ] **Step 2: Resolve exact source and backup paths**

```powershell
$releaseDll =
    'H:\gc\artifacts\GCLoader\.worktrees\asio-audio-backend\build-msvc32-release\dist\iDmacDrv32.dll'
$runtimeDll = 'H:\gc\iDmacDrv32.dll'
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$backupDir =
    "H:\gc\artifacts\runtime-backups\$stamp-absolute-judgement-correction"

if (-not (Test-Path -LiteralPath $releaseDll -PathType Leaf)) {
    throw "Verified Release DLL missing: $releaseDll"
}
New-Item -ItemType Directory -Path $backupDir | Out-Null
Copy-Item -LiteralPath $runtimeDll `
    -Destination (Join-Path $backupDir 'iDmacDrv32.dll')
```

The backup is recoverable by copying it back while the game is stopped.

- [ ] **Step 3: Deploy and verify byte identity**

```powershell
Copy-Item -LiteralPath $releaseDll -Destination $runtimeDll -Force
$sourceHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $releaseDll).Hash
$runtimeHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $runtimeDll).Hash
if ($sourceHash -ne $runtimeHash) {
    throw "Deployment hash mismatch: source=$sourceHash runtime=$runtimeHash"
}
"Deployed SHA256: $runtimeHash"
"Backup directory: $backupDir"
```

Do not alter the existing runtime configuration.

- [ ] **Step 4: Hand off the first operator run**

Ask the user to run one 240-FPS song. The first log review must establish:

```text
one semantic entry before BGM Play
one anchor bound to this stage's Play
nonzero heartbeat recognition/score during no-input periods
early free-tap and hidden/ad-lib sound when performed
nonzero event/query/recognition/score evidence for real input
one semantic exit at state 18 -> 19
no OutsidePlayback input count
stage_entry_handoff_drops=0 for the acceptance run
accepted-late, overload, and cleanup-loss counters are reported and overload/cleanup are zero
no false retained-history fatal
no diagnostic fatal
```

Do not claim the correction accepted until the user reports game behavior and
the corresponding `H:\gc\loader-log.txt` is reviewed.

---

## Plan self-review coverage

| Spec requirement / audit defect | Implementing task |
|---|---|
| D-01, D-16 semantic lifecycle and arbitrary songs/retries | Task 1 |
| Counted stage-entry handoff loss with zero required for acceptance | Tasks 1 and 8 |
| D-02 no owned-loop stock fallback | Tasks 1 and 6 |
| D-03, D-07, D-08, D-17 one clock/offset/watermark | Task 2 |
| D-04 no playback-gap edge deletion | Tasks 2, 3, and 5 |
| D-05 stale history/delivery cursor | Task 3 and Task 5 |
| D-06 time-causal state-only history | Task 3 |
| D-09 invalid controls | Task 4 |
| D-10 selector versus device identity | Task 4 |
| D-11 non-current released query support | Tasks 3 and 4 |
| D-12 unrelated-thread trampoline | Task 4 |
| D-13 diagnostic-only conditions | Task 6 |
| D-14 no C++ exception flow; allocation optional | Tasks 2 and 6 |
| D-15 exact fatal predicates and visible prompt | Task 6 |
| Event isolation, original tail, newest-32, heartbeat cap | Task 5 |
| Original exact-time/native-policy goal | Tasks 2, 4, 5, and Task 8 acceptance |
| Zero cumulative rounding at 144/165 | Tasks 2, 5, and Task 8 acceptance |
| No tests/replay; build versus game proof separation | Global constraints, Tasks 7-8 |

No requirement is assigned to an automated test. No source implementation may
begin by consulting the superseded old plan instead of this plan and its linked
spec/audit.

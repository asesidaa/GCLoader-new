> **HARD GOAL: Gameplay judgement must be driven by absolute song time and must not depend on render or update framerate, whatever native-boundary changes are required.**

# High-FPS Absolute-Time Redesign Failure Index

**Date:** 2026-08-18

**Status:** Redesign stopped. No current loader design or implementation is approved.

**Purpose:** External-redesign handoff. This file records the known failures,
the current dirty-worktree state, the evidence that remains authoritative, and
the shortest path to resume without repeating the failed work.

## Read This First

1. The hard goal above is the goal. It is not optional and is not shorthand
   for reproducing a frame-based approximation at a different rate.
2. Native game logic remains authoritative for candidate construction, note
   normalization, handler order and return values, component aggregation,
   lifecycle, grade, score, effects, and post-descriptor free input.
3. Those two rules do not prohibit changing the native timing driver. Calling
   the real native recognition and score routines at absolute times may be a
   valid design. It is currently unproven, not categorically forbidden.
4. Loader-side reimplementation of native note or input-query semantics is not
   acceptable evidence of correctness.
5. Do not implement from any prior spec, plan, or current `AbsoluteTime` source.
   They are failure artifacts only.
6. Do not repeat the full native audit. Start from E-042 through E-046 and use
   narrow raw-disassembly checks through `ida-cli` only for a concrete missing
   ABI or control-flow fact.

## Source and Evidence Boundaries

| Item | Path or identity | Status |
|---|---|---|
| Source worktree | `H:\gc\artifacts\GCLoader\.worktrees\asio-audio-backend` | Dirty, quarantined |
| Branch baseline | `asio-audio-backend` at `a6f7ed1` | Preserve unrelated ASIO work |
| Runtime/evidence root | `H:\gc` | Read-only unless deployment is explicitly requested |
| IDA database | `H:\gc\game471.exe.i64` | Existing daemon; use `ida-cli` |
| Final audited IDB hash | `3F911E373D18F4C3F11DACF5759AB7FF08847A4F365E8C0ED17B2896E7C47163` | E-046 authority |
| Sealed audit root | `H:\gc\artifacts\GCLoader\.planning\debug\high-fps-timing-domains` | Primary static evidence |
| Worktree pipeline summary | `docs/reverse-engineering/high-fps-input-judgement-pipeline.md` | Secondary and stale after E-044; not final authority |

## Current Worktree Quarantine

At the time of this handoff, `git status` shows all of the following together:

- deletion of the tracked `src/Input/HighFps/` bridge and its tests;
- deletion of the tracked Switch gameplay-hook transaction files;
- modifications to input polling, Switch input, framerate integration, CMake,
  and their tests;
- an untracked `src/Input/AbsoluteTime/` implementation;
- untracked `tests/Input/AbsoluteTime/` tests;
- the withdrawn 2026-08-18 spec and plan;
- an untracked pipeline summary and `tools/analysis/ida_game471.py` bridge.

The tracked diff before this documentation handoff reports 38 files changed,
516 insertions, and 13,061 deletions. This is not a clean implementation base.
Do not build, deploy, or use it as gameplay evidence. Do not revert it blindly:
the tree also contains unrelated ASIO work and user-requested cleanup. An
external redesign must first choose an explicit source baseline and preserve
unrelated changes.

No current static test result, build result, game run, or cabinet acceptance
validates the `AbsoluteTime` implementation.

## Artifact Authority Index

### Authoritative native evidence

| Evidence | What it establishes |
|---|---|
| `evidence/E-042-class-aware-gameplay-pipeline.md` | Physical input path, `CBooster` history/query surface, native judgement entry, timing operand, note-family and result overview |
| `evidence/E-043-native-catchup-loader-scope-audit.md` | Outer state-machine order, native catch-up loop, per-step recognition/score pairing, integer millisecond boundary, old loader scope |
| `evidence/E-044-native-result-publication-closure.md` | Judgement-state versus score-state ownership, grade aggregation, score counters, hidden chart notes versus free input |
| `evidence/E-045-native-component-freeinput-closure.md` | Pressed queries are pure, two booster components are not lanes, native candidate/handler order, free-input conflicts and gates |
| `evidence/E-046-native-normalization-progression-closure.md` | Raw/canonical/effective type matrix, alias and mode rewriting, same-row progression, catch-up edge uniqueness, final IDB hash |

All five paths are relative to:
`H:\gc\artifacts\GCLoader\.planning\debug\high-fps-timing-domains`.

### Rejected design documents

The following are legacy/failure records. Their retained goals and observations
may be useful, but none is design authority:

| Date | Spec | Disposition |
|---|---|---|
| 2026-07-19 | `docs/superpowers/specs/2026-07-19-complete-high-fps-timing-fix-design.md` | Broader timing legacy; not an absolute-time judgement design |
| 2026-08-09 | `docs/superpowers/specs/2026-08-09-high-fps-input-edge-diagnostics-design.md` | Diagnostics iteration only |
| 2026-08-10 | `docs/superpowers/specs/2026-08-10-high-fps-input-transition-bridge-design.md` | Rejected transition-bridge architecture |
| 2026-08-15 | `docs/superpowers/specs/2026-08-15-high-fps-input-judgement-transactions-design.md` | Rejected loader-owned transaction/routing architecture |
| 2026-08-15 | `docs/superpowers/specs/2026-08-15-high-fps-late-gate-preview-correction-design.md` | Rejected note-timing correction layer |
| 2026-08-15 | `docs/superpowers/specs/2026-08-15-high-fps-one-shot-input-lifetime-correction-design.md` | Rejected local lifetime correction |
| 2026-08-16 | `docs/superpowers/specs/2026-08-16-high-fps-song-timed-input-judgement-design.md` | Rejected song-timed query emulation |
| 2026-08-16 | `docs/superpowers/specs/2026-08-16-high-fps-authoritative-input-judgement-correction-design.md` | Rejected corrective layer over the same wrong boundary |
| 2026-08-18 | `docs/superpowers/specs/2026-08-18-absolute-time-judgement-redesign.md` | Withdrawn; invalid adapter and unproved scheduler |

The corresponding files under `docs/superpowers/plans/` are also rejected and
must not be executed. The number of overlapping specs/plans is itself a process
failure: patch-on-patch documents obscured which assumptions had been invalidated.

## Complete Known Failure Ledger

This is the complete set of failures known from the current source review and
sealed audit as of 2026-08-18. Unknowns are listed separately rather than
silently treated as working behavior.

### F-001: The hard goal was lost

The work drifted from "judgement uses absolute song time and is independent of
framerate" into "recreate selected 60-FPS input-history behavior inside the
loader." That substituted an implementation tactic for the actual goal and
left frame-derived assumptions in control.

### F-002: Existing loader architecture was treated as a foundation

Legacy bridge concepts, hook layout, note routing, and test structure were
retained after the user explicitly limited old code to goal/reference value.
The redesign therefore inherited the defects it was supposed to replace.

### F-003: Loader code reimplemented native gameplay decisions

Prior designs and the current `NativeInputQueryAdapter` answer pressed, held,
released, historical-held, paired-forgiveness, consecutive-age, and direction
questions themselves. Earlier layers also classified descriptors, associated
edges with notes, routed current-note versus free input, and adjusted late/grade
arguments. These are native-game semantics, not loader transport facts.

### F-004: Native consecutive-held units were wrong

Raw disassembly establishes that `0x62DAA0` returns a consecutive-held frame
count. `0x62DC60` increments active counters once per captured history frame,
and the first held frame returns `1`. Current code instead returns elapsed
milliseconds and returns `0` at the rise instant:

- `src/Input/AbsoluteTime/NativeInputQueryAdapter.cpp:74-122`
- `src/Input/AbsoluteTime/NativeInputQueryAdapter.cpp:179-194`
- `src/Input/AbsoluteTime/AbsoluteJudgementPatch.cpp:1072-1084`

No millisecond conversion ABI was proven. The 33.333333 ms and 66.666667 ms
claims built on top of this were therefore not valid native-unit claims.

### F-005: Native frame arguments were discarded

Pressed, held, released, and direction detours accept a native `frame` argument
but active-scope dispatch ignores it. `HeldAtMinusTwo()` exists but is never
selected by any detour. The implementation consequently answers all historical
queries from the current view:

- `src/Input/AbsoluteTime/AbsoluteJudgementPatch.cpp:951-1064`
- `src/Input/AbsoluteTime/NativeInputQueryAdapter.cpp:167-177`

### F-006: Control IDs `10..19` were treated as masks

Native control IDs are identifiers. The table at `0x6F4A6C` defines pairs
`{0,5}`, `{1,6}`, `{2,7}`, `{3,8}`, `{4,9}`. IDs `10..14` use native composite
OR behavior; IDs `15..19` use paired current/four-prior-frame forgiveness.
Current `NativeControlMask()` maps only `0..9` and casts every other ID directly
to a bitmask at `src/Input/AbsoluteTime/AbsoluteJudgementPatch.cpp:471-486`.
Pressed, held, and released behavior for those IDs is consequently wrong.

### F-007: Direction semantics were fabricated incorrectly

The native selector values are `0` left, `1` right, and `2` combined. Native
vectors preserve diagonals. Native up writes `y -= 1`; down writes `y += 1`.
Current code chooses a side with `control >= 5`, rejects every multi-direction
state with `std::has_single_bit`, drops selector `2`, and reverses vertical
signs:

- `src/Input/AbsoluteTime/AbsoluteInputView.cpp:9-41`
- `src/Input/AbsoluteTime/AbsoluteJudgementPatch.cpp:1029-1057`

### F-008: Descriptor identity and free input were repeatedly mis-modeled

Earlier documents confused raw, canonical, and effective types; classified raw
`B/C/D/E` as generic lifecycle/default values; conflated hidden chart types
`7/8` with post-descriptor free input; and misread descriptor mute/unmute/late
fields. E-044 through E-046 supersede all of those claims. Loader-side routing
by a copied note matrix remains prohibited even when the matrix itself is now
known, because native code already applies it.

### F-009: Native edge ownership was replaced with loader claims

Native pressed queries are pure and non-consuming. Both booster-component
passes and the guarded free-input path intentionally observe the same history
frame. Earlier designs consumed, claimed, rerouted, or re-associated an edge by
descriptor. This contradicted `0x62DFB0`, candidate construction, and the
component/free-input order proved by E-045/E-046.

### F-010: Fallback and recovery behavior contradicted determinism

Prior work added or proposed raw-native fallback, retry/recovery, disabled
latches, "no-watermark activation," epoch invalidation, and silent scheduling
shutdown. For this deterministic in-process patch, these are alternate gameplay
behaviors. A runtime contract status that should be impossible is an assertion
and hard abort. Install-time transaction failure aborts before activation. It
does not select another judgement mode.

Current tests still describe errors as disabling scheduling, for example
`tests/Input/AbsoluteTime/AbsoluteJudgementSchedulerTests.cpp:298-389`.

### F-011: The absolute scheduler was asserted before its call schedule was proven

Current `AbsoluteJudgementScheduler` calls the complete native recognition and
score pair once per input cohort and optionally once more at an invented
"settlement" horizon:

- `src/Input/AbsoluteTime/AbsoluteJudgementScheduler.cpp:76-110`
- `src/Input/AbsoluteTime/AbsoluteJudgementScheduler.cpp:207-265`

It reuses one outer frame token for all of those calls. The first no-input
horizon can also advance without a recognition call. No evidence proves that
this schedule preserves every stateful effect of `0x5D68E0` and `0x5CF930`,
or that their frame-token consumers remain valid under that call pattern.

Calling the native core at absolute cohort or progression times is not itself
prohibited. It may be the correct boundary. The failure was declaring one call
per cohort plus one settlement call correct before proving the schedule, ABI,
frame-token semantics, no-input progression, and outer commit relationship from
raw disassembly.

### F-012: The native ABI gate was deferred until after implementation

The 2026-08-18 spec admitted that arbitrary native calls and frame-token safety
were an open kill gate, while its plan and source had already implemented those
calls. Function-pointer types and cleanup cannot be accepted from IDA's inferred
prototype. They require raw x86 push/register/stack-cleanup proof at the actual
caller and callee boundaries before any scheduler implementation.

### F-013: Tests replaced the game with fake callbacks

`tests/Input/AbsoluteTime/AbsoluteJudgementSchedulerTests.cpp` defines
`FakeNativeCalls`; its recognition callback only records arguments and directly
queries the loader adapter. These tests prove the invented loader schedule and
invented query behavior agree with themselves. They do not execute candidate
selection, normalization, handlers, lifecycle, grade, score, effects, or free
input.

The same defect applies to previous "native reference" matcher tests that
copied selected disassembly into test code. A copied model is not an independent
oracle and cannot prove game equivalence.

### F-014: Claimed note coverage did not exist

Current tests do not execute native raw/canonical/effective descriptors, hidden
notes, post-descriptor free input, or effective types `0..F`. Passing journal,
clock, adapter, or fake-callback tests cannot support a statement about any note
family. Most previous behavioral tests are invalid as gameplay-correctness
evidence.

### F-015: Static/build proof was allowed to imply gameplay correctness

Unit tests, patch preflight, ABI inspection, a successful x86 build, and IDA
evidence are static evidence. Only instrumented native-process behavior and
operator/cabinet runs can establish gameplay acceptance. Earlier work blurred
that boundary and claimed stronger coverage than it had.

### F-016: Implementation began before the design's kill gates were closed

The code was written while the scheduler boundary, ABI, native input-history
unit, direction contract, and composite/paired IDs were unresolved. This made
the implementation a source of assumptions rather than a consequence of the
audit.

### F-017: Cleanup and implementation state were not controlled

Old and new paths coexisted during part of the work, making tests ambiguous.
Later, large legacy deletion work dominated progress while the replacement
boundary remained invalid. Progress reporting did not make the actual amount
of implemented versus deleted code clear soon enough.

### F-018: Investigation scope and tooling instructions were violated

The work reopened broad upstream input implementation files instead of staying
at the downstream judgement boundary, repeated analysis already sealed by the
audit, and used broad user-profile/home discovery when the exact `ida-cli`
target and bridge were already known. It also relied on inferred ABI labels
instead of raw disassembly until challenged.

### F-019: Work was delegated or expanded after inline-only direction

The user required implementation inline without agents. Any prior delegation,
parallel design work, or expansion into unrelated loader subsystems is not part
of an acceptable continuation. External work should still remain one coherent
owner unless the user explicitly changes that constraint.

### F-020: The design accumulated defensive machinery without a real failure model

Watermarks, activation states, recovery paths, many status enums, overflow
policies, and diagnostics became design tasks even though the game and patch
operate deterministically. This obscured the core boundary and created behavior
for states that should be impossible by construction or fatal on detection.

## Current Source Issue Index

| Area | Current file | Disposition |
|---|---|---|
| Loader-owned native query answers | `src/Input/AbsoluteTime/NativeInputQueryAdapter.*` | Reject architecture; do not repair incrementally |
| Fabricated direction state | `src/Input/AbsoluteTime/AbsoluteInputView.cpp` | Reject implementation |
| Incorrect control-ID and query detours | `src/Input/AbsoluteTime/AbsoluteJudgementPatch.cpp` | Reject active query-hook design |
| Cohort plus settlement scheduler | `src/Input/AbsoluteTime/AbsoluteJudgementScheduler.*` | Unproven architecture; quarantine |
| Assumed direct-call ABI | `src/Input/AbsoluteTime/NativeJudgementAbi.*` | Recheck from raw disassembly; do not trust current types |
| Clock and journal primitives | `src/Input/AbsoluteTime/AbsoluteSongClock.*`, `AbsoluteInputJournal.*` | Loader-owned utilities only; salvageability undecided |
| Runtime/patch integration | `src/Input/AbsoluteTime/AbsoluteJudgementRuntime.*`, `AbsoluteJudgementPatch.*` | Do not activate or deploy |
| Gameplay-behavior tests | `tests/Input/AbsoluteTime/*` | Fake-native correctness claims invalid |
| Integration/CMake edits | files listed by `git status` | Quarantine until a new baseline is selected |

## Native Address Index

| Address | Proven role |
|---:|---|
| `0x659920` | Gameplay input-frame entry |
| `0x62CFB0` | `CBooster` history capture |
| `0x659860`, `0x62D980` | Fill skipped history with held state; no new pressed edge |
| `0x62DC60` | Increment consecutive-held frame counters |
| `0x62DAA0` | Query consecutive-held frame count |
| `0x62DFB0` | Pure pressed-history query |
| `0x62DF50` | Held-history query |
| `0x62DD30` | Released-history query |
| `0x62E290`, `0x62E480` | Direction-vector construction/output |
| `0x62E560` | Control-to-booster-component mapping |
| `0x6F4A6C` | Composite/paired constituent table |
| `0x6630B0` | Tune gameplay frame state machine |
| `0x664DDC` | Fill current/skipped input history before judgement |
| `0x664E06` | Call judgement-frame processor |
| `0x664E23` | Commit authoritative outer gameplay frame after judgement |
| `0x6401E0` | Loop pending native frames and pair recognition with score |
| `0x6401EF` | Load live `16.66666603088379` frame-to-ms operand |
| `0x5D68E0` | Complete native recognition step |
| `0x5CF930` | Native score-state processing for the same integer ms |
| `0x5D4E70` | Build ordered native candidate list |
| `0x5D5720` | Dispatch effective descriptor type |
| `0x5D0E00` | Native timing grade |
| `0x5D04F0` | Native long-form duration grade |
| `0x5D1110` | Aggregate component grades |
| `0x5D2780` | Get resolved grade |
| `0x5D0820` | Publish note/effect metadata |
| `0x5D2040` | Guarded post-descriptor free input |
| `0x5D58D0` | Update row free-input eligibility for the next step |
| `0x5EB210` | Raw -> canonical -> effective normalization |

## Facts That Remain Valid

- Native judgement and score receive the same integer recognition millisecond
  for each native pending-frame step.
- The current binary derives that millisecond from a frame token and the live
  `16.66666603088379` operand.
- The outer native processor may already make multiple recognition/score calls
  during catch-up; therefore multiple calls are not inherently invalid.
- Candidate selection, type normalization, component order, handler success,
  lifecycle, grade, score, effects, and free input must remain native.
- One native history frame is shared non-consumingly by both booster passes and
  the guarded free-input path.
- Raw `B -> A`, `C/E -> 9`, and `D -> 4`; effective type `0` is skipped.
- Loader code must not recreate the E-046 matrix to route gameplay. The matrix
  exists to verify that native ownership is preserved.
- Static evidence is not runtime or cabinet acceptance.

## Unknowns the External Redesign Must Resolve

1. What exact absolute-time schedule replaces or drives `0x6401E0` without
   introducing loader-owned note policy?
2. May `0x5D68E0` and `0x5CF930` be invoked more than once for one outer frame,
   and with which distinct frame metadata, based on raw caller/callee ABI?
3. Which native state changes are call-count-sensitive even when integer song
   time is unchanged or repeated?
4. How is no-input progression driven in absolute time without inventing a
   generic "settlement" policy or inspecting note deadlines in loader code?
5. How does the native `CBooster` history remain authoritative while physical
   edge timing and all temporal queries become framerate-independent?
6. Is the correct boundary a replacement of the outer frame-to-time driver, a
   native history/time representation change, or both?
7. Which frame-token arguments are gameplay-timing inputs versus opaque native
   progression identities?
8. What is the exact x86 ABI at every proposed call or hook? IDA prototype text
   is insufficient; push order, `ECX`, return register, and stack cleanup must
   be read from disassembly.

These are design questions. None may be answered by copying game behavior into
a C++ test model.

## Acceptable Proof for the Next Design

### Retainable automated tests

- exact loader-owned clock/QPC mapping with independently derived arithmetic;
- lossless bounded journal ordering and concurrency;
- guarded patch signatures and transactional installation;
- x86 ABI wrappers checked against disassembly-derived fixtures;
- assertion/hard-abort behavior for impossible runtime statuses.

These prove only loader infrastructure.

### Invalid gameplay tests

- fake recognition or score callbacks used as evidence of note correctness;
- copied native matcher, descriptor, or lifecycle models;
- tests that route raw/effective note IDs in loader code;
- FPS matrices whose expected result comes from the same loader policy under
  test;
- source-grep tests or duplicated RVA/type tables updated with production.

### Required native/runtime evidence

- narrow `ida-cli` disassembly for the selected downstream timing boundary and
  exact x86 ABI;
- native-process instrumentation showing actual call order, times, frame
  metadata, query results, descriptor results, and score publication;
- operator/cabinet comparison across the required FPS values;
- explicit separation of static/build results from gameplay acceptance.

## External Resume Sequence

1. Read this index.
2. Read E-042 through E-046 conclusions and referenced raw artifacts only as
   needed; do not regenerate the audit.
3. Inspect `git status`; choose and document a clean source baseline while
   preserving unrelated ASIO work.
4. Treat all prior input/judgement implementations as legacy failure artifacts.
5. State the hard goal verbatim at line 1 of the new external design.
6. Compare candidate downstream boundaries. Do not assume that native-core
   rescheduling is forbidden, and do not assume that it is safe.
7. Close the scheduler, frame-token, and ABI gates with narrow raw disassembly.
8. Present the design before writing production code.
9. Test loader-owned infrastructure only with unit tests; use the native process
   and cabinet for gameplay claims.

## Suggested Skills

- `ida-cli`: reuse the existing daemon and inspect only exact missing ABI or
  control-flow facts.
- `superpowers:brainstorming`: compare native-boundary designs before code.
- `superpowers:writing-plans`: only after the user approves a new design.
- `superpowers:verification-before-completion`: keep static, native-process,
  and cabinet proof claims separate.

# Complete 2D Menu Timing Fix Design

- Status: approved design, awaiting implementation plan
- Date: 2026-07-25
- Source worktree: `H:\gc\artifacts\GCLoader\.worktrees\ctune-effect-timing`
- Branch baseline: `ctune-effect-timing` at `2354d0f`

## Objective

Correct every material 2D menu timing problem proven by the 2026-07-25 audit
without throttling rendering, input, elapsed-time task work, or semantic
MovieClip actions.

The implementation must:

1. make the universal MovieClip timing gate safe for `Anim::PreProcessor`;
2. preserve authored cadence for Ranking and HitChart entry choreography;
3. preserve authored cadence for UnlockReward's three raw invocation-count
   stores while leaving its elapsed-seconds work native;
4. add comprehensive, bounded diagnostics before enabling those behavior
   changes;
5. retain the diagnostics while the user performs runtime acceptance; and
6. remove only temporary diagnostics after the user explicitly confirms the
   corrected build.

Builds, automated tests, byte verification, deployment preparation, and log
interpretation belong to Codex. All in-game and visual acceptance belongs to
the user. A successful build or plausible counter ratio is never sufficient to
claim that the runtime issue is fixed.

## Evidence baseline

The design is based on:

- `docs/reverse-engineering/2d-menu-timing-audit/coverage-matrix.md`;
- `docs/reverse-engineering/2d-menu-timing-audit/ida-trace.md`;
- `docs/reverse-engineering/2d-menu-timing-audit/patch-inventory.md`; and
- `docs/reverse-engineering/2d-menu-timing-audit/xfl-inventory.md`.

The fixed artifacts audited were:

- executable: `H:\gc\game471.exe`;
- executable SHA-256:
  `FEAD3BD4D0E0985F101965EDC417DD2B96522F8716FF789D84618FEB0D7A2522`;
- IDB: `H:\gc\game471.exe.i64`; and
- IDB SHA-256:
  `55D119762B0706549AB5AA9C7D5D2DDF3C902AE322462D025D570C8181C50C1F`.

The XFL census covered all 59 projects in
`H:\gc\artifacts\2d_boost`. Fifty-seven projects contain a reachable
multi-frame child and 55 contain a depth-2-or-deeper child. This design
therefore treats nested MovieClip playback as normal required coverage, not an
optional edge case.

## Confirmed problem set

### PreProcessor context collision

Normal root and nested MovieClip playback, forward/reverse playback, and
recurring traversal converge on the universal one-frame primitive at
`0x004DF940`. The current loader gates that callee and returns success without
motion on a non-authored tick.

`Anim::PreProcessor` visitor `0x004EFB90` also invokes the same primitive. It
records the current frame, requests forward/reverse movement, compares the
frame before and after, and invokes MovieClip `Stop` at `0x004D1730` if the
frame did not change. The current success-without-motion return does not
satisfy that caller's contract and can stop the clip.

### Ranking raw draw counter

`CRankingTask` registers draw callback `0x00616C60`. Tags `tg_rank01` through
`tg_rank30` reset entry counters to zero. The callback draws each entry from
the current counter and stores the incremented value at `0x00616EB7`.

The store executes once per draw callback, is not elapsed-time based, and does
not pass through MovieClip or Navigator timing.

### HitChart raw draw counter

`CHitChartTask` registers draw callback `0x00665230`. Tags `tg_01` through
`tg_30` reset entry counters. The callback derives position and alpha from the
counter, draws the entry, and stores the incremented value at `0x00665635`.

This is a second independent per-draw authored-frame clock.

### UnlockReward mixed timing domains

The recurrent UnlockReward update `0x00430C00`, reached through IFBL callback
`0x005F7D20`, contains correct elapsed-seconds fields and three raw stores:

- countdown store at `0x00430DA3`;
- state-range 1 through 31 store at `0x00430E54`; and
- state-range 33 through 43 store at `0x00430F23`.

Throttling the whole callback would incorrectly throttle input, rendering, and
the elapsed-time fields. Only the raw stores may consume authored cadence.

## Scope

### In scope

- a preprocessing context guard;
- five exact counter-store gates;
- a temporary preprocessing `Stop` diagnostic hook;
- comprehensive path, value, boundary, and MovieClip revisit diagnostics;
- exact hook contracts, transaction capacity, bindings, and rollback;
- pure policy and diagnostic tests;
- a durable menu reproduction and runtime-validation record; and
- post-acceptance removal of temporary diagnostics.

### Out of scope

- restoring broad News or Notice task gates;
- gating `Anim::DrawTraverse`;
- throttling whole Ranking, HitChart, or UnlockReward callbacks;
- changing global delta seconds;
- changing input sampling or repeat behavior;
- changing Navigator timing;
- suppressing repeated MovieClip calls per object;
- scaling XFL `frameRate="30"` metadata;
- changing RVB/XFL/game data; and
- claiming runtime success without the user's in-game verdict.

Same-object MovieClip visits within one outer epoch are diagnostic-only. A
future deduplication change requires separate runtime evidence and a separate
approved design.

## Chosen architecture

Use exact context and write boundaries.

The existing `Authored60PhaseClock`, `OuterFrame`, MovieClip goto depth,
ordinary MovieClip advance, Navigator advance, and IFBL behavior remain the
clock foundation.

A focused `FramerateMenuTiming` component provides testable policy and bounded
diagnostic helpers. SafetyHook ownership and executable callbacks remain in
`FrameratePatch.cpp`, consistent with the existing runtime boundary.

### Policy surface

The focused component defines:

- `MenuTimingMode` with temporary `Observe` and `Correct` modes;
- `MovieClipAdvanceContext` with `Ordinary`, `Goto`, and `Preprocess`;
- `DecideMovieClipAdvance(context, authored_tick)`;
- `DecideMenuCounterStore(authored_tick)`;
- preprocessing observation state sufficient to associate a skipped advance
  with a subsequent `Stop` on the same MovieClip;
- a fixed-capacity MovieClip visit tracker; and
- a plain runtime-stat snapshot and formatter.

The diagnostic and corrected DLLs are separately hashed builds. No new
user-facing configuration key is added. Stage A initializes the internal mode
as `Observe`; Stage B initializes it as `Correct`; Stage C removes the
temporary mode once acceptance is complete.

This avoids strict-config churn and makes every tested DLL's behavior
unambiguous from its identity and startup diagnostics.

### MovieClip decision order

`HookMovieClipAdvance` classifies each call in this order:

1. goto depth greater than zero: execute the original immediately;
2. preprocessing depth greater than zero:
   - Observe mode: retain the existing ordinary decision but record whether a
     non-authored call was skipped;
   - Correct mode: execute the original immediately; and
3. ordinary call:
   - authored tick: execute the original;
   - non-authored tick: return the existing success value without motion.

Goto and preprocessing are semantic operations, not recurring scheduler ticks.
They never share the ordinary skip decision in the corrected build.

### Counter-store decision

All affected callbacks continue to run every target update.

In Observe mode, each hook records what the decision would have been and then
allows the original store.

In Correct mode:

- authored tick: allow the original store;
- non-authored tick: advance `EIP` by the exact store length and leave the
  destination unchanged.

No hook suppresses surrounding calculations, rendering, task logic, input, or
elapsed-seconds updates.

## Hook contract

The current full transformed contract set has a capacity of 46 hooks. The
installed transformed plan contains 45 when the optional WASAPI resync policy
is excluded and 46 when it is included.

Stages A and B add seven contracts, producing a temporary transformed capacity
of 53. Their installed transformed plans contain 52 hooks without the optional
WASAPI resync policy and 53 with it. Stage C removes the temporary MovieClip
`Stop` diagnostic contract, leaving six permanent additions, a final
transformed capacity of 52, and installed counts of 51/52 without/with the
optional WASAPI policy.

| ID | VA | RVA | Expected bytes | Kind | Lifetime |
|---|---:|---:|---|---|---|
| `MovieClipPreprocessVisit` | `0x004EFB90` | `0x000EFB90` | `6A FF 68 10 49 67 00` | Inline | Permanent |
| `MovieClipStopDiagnostic` | `0x004D1730` | `0x000D1730` | `C7 81 1C 01 00 00 01 00 00 00 C3` | Inline | Stages A/B only |
| `RankingEntryCounterStore` | `0x00616EB7` | `0x00216EB7` | `89 01` | Mid | Permanent |
| `HitChartEntryCounterStore` | `0x00665635` | `0x00265635` | `89 01` | Mid | Permanent |
| `UnlockRewardCountdownStore` | `0x00430DA3` | `0x00030DA3` | `89 90 6C 37 00 00` | Mid | Permanent |
| `UnlockRewardPrimaryStateStore` | `0x00430E54` | `0x00030E54` | `89 81 D4 37 00 00` | Mid | Permanent |
| `UnlockRewardSecondaryStateStore` | `0x00430F23` | `0x00030F23` | `89 90 D4 37 00 00` | Mid | Permanent |

All new menu contracts are installed before Navigator and `OuterFrame`.
`OuterFrame` remains the final transformed contract. Native 60 FPS mode
continues to install only `OuterFrame`; the diagnostic hooks do not alter the
native baseline.

### Store-site safety

At Ranking and HitChart, the destination counter has already supplied the
current frame's drawing parameters. Suppressing the final store freezes the
counter for the next target update without suppressing the current draw.

At UnlockReward:

- after `0x00430DA3`, the game explicitly compares the destination countdown
  in memory against zero;
- after `0x00430E54`, the game explicitly compares the destination state in
  memory against 31; and
- `0x00430F23` only advances the later state range before subsequent task
  checks.

On a non-authored tick, the unchanged memory value therefore cannot cross a
boundary or fire a transition early. Register and flag state produced by the
surrounding native code is left untouched.

## Preprocessing context and causal diagnostics

The preprocessing inline hook uses RAII to publish a thread-local visitor
scope around the original `0x004EFB90` call. The scope records:

- nesting depth;
- the currently preprocessed MovieClip pointer, obtained from the visitor's
  target;
- whether that MovieClip experienced a non-authored skipped advance; and
- the outer epoch in which the event occurred.

The temporary `MovieClipStopDiagnostic` hook always calls the original
`0x004D1730`. During Stages A and B it records:

- all stops within preprocessing;
- stops on the same MovieClip after a non-authored skipped advance; and
- the first causal object/epoch sample.

In Observe mode this confirms whether the statically proven collision occurs
in the exercised runtime paths. In Correct mode no preprocessing advance is
skipped, so the causal-stop counter must remain zero. Legitimate
PreProcessor-originated stops may still exist and are recorded separately.

The temporary Stop hook changes no behavior and is removed only in Stage C.

## Shared phase and outer epoch

`OuterFrame` remains the single publisher of the authored tick. It also
publishes a monotonically increasing outer epoch before affected 2D callbacks.

All six behavior fixes consume the same phase. No new accumulator or clock is
introduced.

The outer epoch exists only for diagnostics. It must not participate in the
decision to advance, store, or suppress.

## Comprehensive diagnostics

### Five-second cumulative statistics

The existing `FrameratePatch: runtime_stats` line gains:

- `movieclip_preprocess=visits/non_tick_skips/forced`;
- `movieclip_preprocess_stop=all/causal`;
- `movieclip_revisit=same_epoch/hash_collision`;
- `ranking_entry=commit/suppress`;
- `hitchart_entry=commit/suppress`;
- `unlock_countdown=commit/suppress/boundary`;
- `unlock_state_primary=commit/suppress/boundary`; and
- `unlock_state_secondary=commit/suppress/boundary`.

In Observe mode, `suppress` means “would suppress.” In Correct mode it means
the store was actually suppressed. Every startup and runtime-stat line
identifies the mode.

### One-time activation and sample lines

Each path emits one activation line per process:

- `movieclip_preprocess`, screen class `global_asset_load`;
- `ranking_entry`, screen `attract_ranking`, asset `ranking.rvb`;
- `hitchart_entry`, screen `attract_hitchart`, asset `hitchart.rvb`;
- `unlock_reward`, screen `postplay_unlock_reward`, asset
  `unlock_reward.rvb`; and
- `movieclip_same_epoch_revisit`, screen class `ordinary_movieclip`.

The first relevant value or boundary sample may include object address, outer
epoch, old value, computed new value, and authored phase. Per-frame or
per-entry logging is prohibited.

A zero counter means only that the supplied run did not exercise the path.

Diagnostic value reads use the existing safe memory helpers. A failed
best-effort diagnostic read increments a diagnostic-read-failure counter and
does not change the store decision, call the fatal conversion path, or suppress
native behavior. The preprocessing depth guard also remains active if its
optional MovieClip-pointer attribution read fails, because pointer attribution
is diagnostic while the context exemption is behavioral.

### Observe-only MovieClip revisit tracker

The tracker is thread-local, fixed at 1,024 direct-mapped slots, and performs
no allocation or locking. Each slot stores MovieClip pointer and outer epoch.

For ordinary MovieClip calls only:

- the same pointer already recorded in the same epoch increments
  `same_epoch`;
- a different pointer replacing a slot in the same epoch increments
  `hash_collision`; and
- all other observations replace the slot.

Goto and preprocessing calls are excluded because immediate semantic movement
is expected there.

The tracker never changes behavior. It is removed in Stage C. A nonzero revisit
count becomes evidence for a separate investigation, not authorization for
deduplication.

## Diagnostic-first delivery sequence

### Stage A: observe-only diagnostic build

1. Add the focused policy/tests and all seven temporary hook contracts.
2. Install every new hook in observe-only behavior.
3. Preserve the current MovieClip skip behavior and every raw counter store.
4. Build, test, verify the 53-contract maximum and the exact 52/53 installed
   transformed counts without/with the optional WASAPI hook, and deploy the
   separately hashed diagnostic DLL.
5. The user exercises the reproduction matrix and supplies the loader log.
6. Codex correlates activation, would-suppress, boundary, causal-stop, and
   revisit evidence and records the results.

No runtime issue is claimed fixed in Stage A.

### Stage B: corrected build with diagnostics retained

1. Switch the internal menu timing mode from `Observe` to `Correct`.
2. Force preprocessing advances through the original MovieClip primitive.
3. Suppress the five raw counter stores on non-authored ticks.
4. Keep the temporary Stop hook, value samples, activation logs, and revisit
   tracker unchanged.
5. Build, test, verify, and deploy a newly hashed corrected DLL.
6. The user repeats the affected screens and supplies the loader log and
   visual/input verdict.

Codex may report static success and interpret counters. Only the user may
accept runtime behavior.

### Stage C: post-acceptance cleanup

Stage C begins only after the user explicitly confirms that the corrected
behavior works.

Remove:

- `MovieClipStopDiagnostic`;
- preprocessing causal-stop observation machinery;
- value/boundary sample lines that are no longer needed;
- one-time verbose activation markers; and
- the 1,024-slot MovieClip revisit tracker.

Retain:

- all six permanent behavior hooks;
- lightweight cumulative run/suppress totals for each permanent path;
- audit, design, plan, and runtime-validation documents; and
- exact byte, binding, transaction, and policy tests.

The final transformed hook capacity is 52, with exact installed counts of
51/52 without/with the optional WASAPI hook. Cleanup receives its own build
and static verification, followed by a user-owned runtime smoke test.

Nothing is removed during Stages A or B.

## Menu reproduction matrix

The runtime-validation record must name the exact screen exercised rather than
only say “menus tested.”

| Path | Concrete screen/task | Reproduction |
|---|---|---|
| Ranking | `CRankingTask`, `ranking.rvb` | Leave the cabinet idle through the attract rotation. `H:\gc\data\expconfig.cfg` currently has `DoNotDisplayRanking = 0`. Observe the rank rows' slide/fade entry under `tg_rank01..30`. |
| HitChart | `CHitChartTask`, `hitchart.rvb` | Leave the cabinet idle through the attract rotation. `H:\gc\data\expconfig.cfg` currently has `DoNotDisplayHitChart = 0`. Observe the chart entries' slide/fade under `tg_01..30`. |
| UnlockReward | `CUnlockRewardTask`, `unlock_reward.rvb` | Use a profile with an eligible reward, complete a credit, and enter the post-play unlock/reward flow. `ForceSkipReward = 0` currently. Exercise panels, arrows, reward text, and item/coin presentation. If no eligible reward exists, record the path as unreproduced rather than passed. |
| PreProcessor | global RVB/XFL load | Restart for each target FPS so startup loads are observed. Then traverse Select Mode, Select Game, Select Music, Results, and Unlock Reward, which are representative deep nested-playhead families. |
| Revisit stress | ordinary nested MovieClips | Exercise `selectmode2`, `selectgame2`, `selectmusic2`, `result_local`, and `unlock_reward`, all of which have deep child graphs. Record revisit/collision totals only. |
| Navigator control | shared bottom-right native Navigator | Exercise Select Mode, Select Music, and Results. Its existing run/skip behavior must remain unchanged. |
| News/Notice control | native elapsed-time tasks | Observe boot/legal/news wall time and input responsiveness. No broad task gate is added. |

Ranking and HitChart are attract-loop tasks, not ordinary selectable gameplay
menus. Their diagnostic activation may require waiting through the complete
attract rotation and valid data for entry tags.

## Automated verification

### Focused policy tests

Add `FramerateMenuTimingTests` covering:

- every `MovieClipAdvanceContext` on authored and non-authored ticks;
- Observe versus Correct preprocessing decisions;
- nested preprocessing scopes and causal skipped-advance association;
- menu counter decisions;
- the 1,024-slot tracker for first visit, same-epoch revisit, new epoch,
  collision, and thread-local isolation; and
- stat formatting in both modes.

### Cadence simulation

Simulate complete clock cycles at 60, 120, 144, and 240 FPS:

- transformed counter commits total exactly 60 per target second;
- Ranking's ten-step entry transition consumes the same wall time;
- HitChart's first-three-entry 25-step transition and later-entry eight-step
  transition consume the same wall time;
- UnlockReward's 30-step primary range consumes the same wall time;
- UnlockReward's ten-step secondary range consumes the same wall time; and
- 144 FPS uses the deterministic rational pattern rather than an assumed
  integer divisor.

### Store-gate semantics

Test the mid-hook helper with a synthetic `safetyhook::Context`:

- Correct/authored: `EIP` is unchanged;
- Correct/non-authored: `EIP` advances by exactly 2 bytes for Ranking/HitChart
  or 6 bytes for an UnlockReward store;
- Observe mode: `EIP` is unchanged regardless of phase;
- general-purpose registers and flags are unchanged; and
- counters distinguish would-suppress from actual suppression.

### Plan, binding, and transaction tests

Stage A/B assertions:

- transformed capacity and full contract count are exactly 53;
- installed transformed counts are exactly 52 without optional WASAPI resync
  and 53 with it;
- all seven new IDs, RVAs, bytes, names, and hook kinds match;
- every contract has a non-null runtime binding;
- Navigator is penultimate and `OuterFrame` is last;
- native 60 has only `OuterFrame`; and
- rollback succeeds from every possible hook position at full capacity.

Stage C assertions:

- transformed capacity and full contract count are exactly 52;
- installed transformed counts are exactly 51 without optional WASAPI resync
  and 52 with it;
- the temporary Stop ID and binding are absent;
- the six permanent contracts remain exact;
- Navigator and `OuterFrame` ordering remains unchanged; and
- full-capacity rollback still succeeds.

## Runtime acceptance

Runtime testing is performed by the user.

The intended target matrix is:

- 60 FPS native baseline;
- 120 FPS transformed;
- 144 FPS transformed non-integer cadence; and
- 240 FPS transformed stress target.

Stage A may begin with 240 FPS because it maximizes non-authored observations.
Additional Stage A targets are used when the first log is ambiguous or a path
is not exercised.

For Stage B:

- Ranking and HitChart reset-to-settle time should match the user's 60 FPS
  baseline within one authored frame;
- UnlockReward raw boundaries must occur after the same authored-step counts
  without changing elapsed-time animation, rendering, or input;
- preprocessing must no longer produce a causal stop after a skipped advance;
- Navigator, News/Notice, and ordinary input must remain correct; and
- any untested target or unreproduced screen remains explicitly unaccepted.

Counters prove hook activity and cadence decisions. They do not prove visual
correctness. The runtime-validation record stores the user's verdict
separately from static and log evidence.

## Durable runtime-validation record

Create
`docs/reverse-engineering/2d-menu-timing-runtime-validation.md`.

For every diagnostic, corrected, and cleanup DLL, record:

- commit and branch;
- DLL path, size, timestamp, and SHA-256;
- executable and IDB identities;
- configured and measured FPS;
- relevant `expconfig.cfg` values;
- exact screens and actions exercised;
- activation and cumulative counter excerpts;
- preprocessing causal-stop and MovieClip revisit results;
- Codex's static/log interpretation;
- the user's visual, timing, and input verdict;
- untested targets; and
- unreproduced paths.

This document is append-only through the three stages. Earlier diagnostic
evidence is not replaced when the corrected build is deployed.

## Expected source changes

The implementation plan should limit changes to:

- `src/Patches/Framerate/FramerateMenuTiming.h`;
- `src/Patches/Framerate/FramerateMenuTiming.cpp`;
- `src/Patches/Framerate/FrameratePatch.cpp`;
- `src/Patches/Framerate/FrameratePatchPlan.h`;
- `src/Patches/Framerate/FrameratePatchPlan.cpp`;
- `src/Patches/CMakeLists.txt`;
- `tests/Patches/Framerate/FramerateMenuTimingTests.cpp`;
- `tests/Patches/Framerate/FrameratePatchPlanTests.cpp`;
- `tests/Patches/Framerate/FramerateRuntimeTests.cpp`;
- `tests/Patches/CMakeLists.txt`;
- the runtime-validation document; and
- this design's implementation plan.

No game data, executable, IDB, XFL, RVB, MTX, or unrelated loader subsystem is
modified.

## Rejected alternatives

### Gate whole callbacks

Rejected because Ranking and HitChart callbacks perform drawing, and
UnlockReward combines input, rendering, seconds-based fields, and raw counters.
Whole-callback throttling would skip valid native-rate work.

### Gate DrawTraverse or move the MovieClip hook broadly upstream

Rejected because earlier broad experiments disturbed 2D timing and input, and
the audit proves the current ordinary MovieClip sink already covers normal root
and nested playheads. Only preprocessing needs an exemption.

### Convert counters, thresholds, and interpolation factors

Rejected because it requires reconciling every counter consumer. UnlockReward's
numeric ranges are both counters and semantic states, making value scaling
especially fragile.

### Deduplicate MovieClip objects

Rejected because repeated same-object visits are not yet proven to be
incorrect. The approved scope observes them without changing behavior.

### Add a public diagnostic configuration key

Rejected because the loader's configuration is strict. A temporary user-facing
key would require coordinated config/GUI churn and later removal. Separately
hashed Observe and Correct DLLs provide an unambiguous staged workflow without
expanding the public configuration contract.

## Completion definition

The work is complete only when:

1. Stage A diagnostics are implemented, statically verified, deployed, and
   exercised by the user;
2. Stage A logs confirm which paths ran and are preserved in the validation
   record;
3. Stage B fixes are implemented with all comprehensive diagnostics retained;
4. automated verification passes for the 53-contract corrected build and its
   exact 52/53 installed transformed counts;
5. the user explicitly accepts the in-game behavior for the targets/screens
   they tested;
6. Stage C removes only temporary diagnostics;
7. automated verification passes for the 52-contract final build and its exact
   51/52 installed transformed counts;
8. the user completes the final runtime smoke test; and
9. untested targets or unreproduced paths are reported honestly.

No audit artifact or earlier runtime record is removed during completion.

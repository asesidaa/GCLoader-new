---
status: design_pending_review
trigger: "The crash is gone, but at 240 FPS transitions are prolonged, card-scan/menu input is hard to register, in-game animation timing remains broken, and non-song-selection menus scroll too quickly. Determine which systems are absolute-time versus tick-based and redesign the high-FPS timing patch from binary evidence."
created: 2026-07-19
updated: 2026-08-17
mode: diagnose-only
---

# High-FPS timing domains

## Symptoms

- Expected: judgement behavior retains its 60 FPS real-time semantics.
- Expected: 2D animation, menu navigation, and transitions retain their 60 FPS real-time duration at higher render rates.
- Expected: stage/chart rendering and input sampling become smoother at higher render rates.
- Actual at 240 FPS: intervals between transitions are prolonged.
- Actual at 240 FPS: input on the card-scan result/menu is difficult to register and feels dropped.
- Actual at 240 FPS: in-game animation timing remains incorrect.
- Actual at 240 FPS: menu scrolling, especially outside song selection, is accelerated.
- Error state: the former loading crash is gone after relocating `EffectCadence16B`.
- Reproduction: configure 240 FPS, then exercise card-scan results, non-song-selection menus, transitions, and an in-game stage.

## Current Focus

- hypothesis: The current regressions come from crossing four distinct domains: absolute elapsed time, target-rate simulation ticks, authored 60 Hz asset frames, and control-flow counts.
- test: Completed static traces from the native scheduler through Notice/News, IFBL, input repeat, and the full physical-input → normalized descriptor → judgement/result/free-input pipeline.
- expecting: Removing complete-task gates and converting only at explicit integer-duration or authored-asset sinks will preserve 60 FPS wall-time behavior while retaining target-rate judgement/chart/input updates.
- next_action: Audit the current loader source against E-046 and write a separate correction design; do not edit production code until that design is accepted.
- reasoning_checkpoint: The native gameplay pipeline is statically closed through raw/canonical/effective normalization, per-row progression, result publication, and free input. Native proof does not establish current loader compliance or runtime acceptance.
- tdd_checkpoint: Not applicable during diagnose-only work; implementation tests begin only after design approval.

## Evidence

- timestamp: 2026-07-19
  source: user runtime report
  observation: The relocated hook removed the loading crash, but 240 FPS behavior is worse across transitions, input, animation, and menu navigation.
  implication: The crash and the timing correctness problem were independent; safe hook placement did not validate hook semantics.

- timestamp: 2026-07-19
  source: `H:\gc\config.toml` and `H:\gc\loader-log.txt` inspected after the report
  observation: The presently available runtime capture is a 60 FPS run (`target_fps=60`), not the reported 240 FPS reproduction.
  implication: It can establish baseline startup/input state but cannot validate or refute any 240 FPS timing hypothesis.

- timestamp: 2026-07-19
  source: current GCLoader source
  observation: Transformed timing currently combines direct constant replacement, duration scaling, authored-frame mapping, and a QPC-derived shared 60 Hz boolean gate.
  implication: Every hook must be checked against the original variable semantics and actual caller cadence; common naming is not evidence of a common clock domain.

- timestamp: 2026-07-19
  source: live IDA daemon on `H:\gc\game471.exe.i64`
  observation: The legal notice owns a 2.0-second delta-time IFBL wait but the loader calls its complete update only at 60 Hz; at 240 FPS this predicts an 8-second wait.
  implication: Notice and News must remain native-rate tasks; only their frame-counted MovieClip/IFBL operations may be adapted.

- timestamp: 2026-07-19
  source: live IDA judgement/effect traces
  observation: Judgement compares milliseconds correctly at target rate, while GREAT/GOOD and several direct effect writers use the target 4.1667 ms frame size as an authored asset-frame duration.
  implication: Retain the target gameplay clock and restore 16.6667 ms only at proven asset-frame lifetime/index sinks.

- timestamp: 2026-07-19
  source: live IDA IFBL/menu traces
  observation: The non-song repeat helper still uses 16/3 native ticks, and the IFBL loop hook scales a repetition count rather than a wait.
  implication: Scale the repeat thresholds, retain IFBL integer-wait scaling, and remove loop-count scaling.

## Eliminated

- hypothesis: The loading crash explains the remaining high-FPS behavior.
  reason: The user confirms the crash is gone while all timing/input symptoms remain.

## Resolution

- root_cause: Mixed-clock patching: complete elapsed-time tasks are throttled, target-frame milliseconds leak into authored asset frames, native-duration thresholds remain unscaled, and an IFBL control-flow count is scaled as time.
- design: Keep absolute-time and gameplay simulation native; scale only integer durations; map only final authored asset indices/cadences; never scale control-flow cardinality. Exact hook decisions and sites are in `high-fps-timing-domains/RESULTS.md`.
- verification: Static IDA/source diagnosis and production implementation are complete. The operator accepted the deployed 240 FPS correction on 2026-07-20 and requested commit and merge; this does not imply a quantitative four-rate matrix was captured.
- files_changed: Production framerate source/tests and the persistent investigation documents under `.planning/debug/high-fps-timing-domains*`.

## Companion artifacts

See `.planning/debug/high-fps-timing-domains/README.md`.

## Class-aware gameplay audit (2026-08-17)

The class-aware static audit begins in
`high-fps-timing-domains/evidence/E-042-class-aware-gameplay-pipeline.md` and
is finalized by E-046.
It connects the RTTI-confirmed physical input chain through `CBooster`,
`CGameMainTask`/`CDemoPlayTask`, and `CTuneGameManager` to the dispatcher and
judgement core. The corrected audit explicitly covers raw, canonical, and
effective note types `0–15` plus post-descriptor free input, preserving the
one-lane Switch model and separating start/end judgement from in-between
hold/interval validity. Chart types `7/8` are `HIDDEN/HIDDEN2` hidden notes and
are not the free-input path.

The live operand at `0x6401EF` uses `0x6FC0A0 = 16.666666 ms/frame`; the prior
IDA comment describing an 8.333 ms/120-Hz judgement clock was stale and has
been corrected. This is a static binary finding, not runtime acceptance and
not an approval to change the production high-FPS patch.

The saved IDB annotations and their rename/comment ledger are in
`high-fps-timing-domains/CLASS-AWARE-ANNOTATION-LEDGER.md`.

## Native catch-up and loader-scope audit (2026-08-17)

E-043 completes the static boundary from the physical transition journal
through native song-clock catch-up, descriptor dispatch, judgement, and result
publication. The native update fills history, processes judgement, and commits
the authoritative frame in that order; a catch-up step can invoke the native
judgement core up to 3, 7, 8, or 12 times at 60, 144, 165, or 240 FPS.

The audited loader revision consumed one timestamp-ordered transition sample
per native core call, and its `JudgementInputScope` remained open through both
booster-component passes, lifecycle/aggregation, and post-descriptor free
input. E-045 proves native pressed queries are deliberately non-consuming and
every caller in the recognition step must see one stable frame fact. E-043's
note-dependent recomputation concern is a historical source-audit lead, not a
proven current defect. E-043/E-045 remain static evidence only; no production
patch or runtime acceptance is implied.

## Native result-publication closure (2026-08-17)

E-044 closes the remaining native ownership question. `CTuneGameManager+0x254`
holds the 444-byte per-player judgement state, while `+0x26C` holds a separate
368-byte non-polymorphic score state. Timing and duration grade helpers feed
descriptor/component aggregation; `GameplayScoreState_ProcessJudgementFrame`
then consumes resolved grades and updates the separate MISS/GOOD/COOL/GREAT
counters. `GameplayJudgementState_PublishNoteResultMetadata` owns note/effect
metadata, not those score counters.

E-044 closes score/result publication; E-046 closes the later normalization and
progression questions. The saved IDB and E-042 through E-046 records are static
evidence, not runtime acceptance or permission to implement a speculative fix.

## Native normalization/progression final closure (2026-08-17)

Chart preparation first bakes raw chart types into canonical and effective
descriptor types. The authoritative recognition-time pipeline is:

`physical switch state` → `GWInput/CBooster frame history` → `integer-ms
catch-up recognition step` → `first incomplete effective descriptor per
internal chart row` → `effective-family judgement` → `long-note
continuation/end` → `grade/result/score/effects` → `guarded post-descriptor
free input` → `next-step row eligibility refresh`.

E-046 proves raw aliases `B→A`, `C/E→9`, and `D→4`, with effective type `0`
skipped. Candidate lists contain at most one first-incomplete descriptor per
row, so a same-row follower cannot see the current edge. A later catch-up step
may expose it, but skipped native history carries held state only and cannot
recreate the earlier pressed edge. Cross-row candidates share the pure frame
lookup only under native fixed-list ordering and handler failure rules.

Free input runs before `GameplayJudgementState_UpdateRowFreeInputEligibility`
refreshes the `+0xCC` row gate for the following recognition step; that helper
does not advance a cursor. One immutable full-state sample, native candidate
and conflict ownership, original long-note/grade/free-input policy, locked
Switch forgiveness, and static timing values other than `GameTimeOffset` and
`JudgTimeOffset` are mandatory design inputs. The final saved IDB SHA-256 is
`3F911E373D18F4C3F11DACF5759AB7FF08847A4F365E8C0ED17B2896E7C47163`.

The native phase is complete. The next phase is a fresh current-source audit
followed by an explicit loader design; no loader implementation has been
selected or changed by this closure.

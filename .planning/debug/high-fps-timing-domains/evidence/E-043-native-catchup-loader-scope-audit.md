# E-043: Native catch-up and loader judgement-scope audit

Date: 2026-08-17
Target: `H:\gc\game471.exe.i64`
Mode: static IDA/source evidence only; no production patch, build, deployment,
runtime binary, or configuration change

## Purpose and provenance

This record continues E-042 at the boundary where the native gameplay frame,
the song-clock catch-up step, and the loader's high-FPS judgement bridge meet.
It is the evidence record for the complete sequence
physical transition -> timeline sample -> native catch-up call -> descriptor
dispatch -> judgement/result hooks. It does not approve an implementation.

- Current IDB before the pending annotation batch: SHA-256
  `8395E2B83A1C00424B98CBFD1DD747F0A3197C1007A6C9EB522288F96F7C81BC`.
- Native catch-up/judgement artifact:
  `H:\gc\runs\20260816T210335Z-a3aabe78\artifacts\game471-catchup-judgement-boundaries-2026-08-17.json`
  (SHA-256
  `710FE451CC0A9C3A9A3B9E9EB65529AD422DAEC8113C324198D2178266D3D110`).
- Loader source revision audited: worktree
  `H:\gc\artifacts\GCLoader\.worktrees\asio-audio-backend`, commit
  `a6f7ed1f10b79617fcbd5a065e6a0e2e32c62b32`; the tracked judgement/input
  sources were clean at that revision.
- Earlier class, physical-input, and note-type evidence is not regenerated;
  it is linked from E-042 and the persisted class-aware todo.

## Native frame and catch-up order

The active Tune update at `0x6630B0` has a fixed order:

1. `0x664DDC` calls `GameplayInput_SetCurrentFrameAndFillHistory` for
   `Tune+0x10 + Tune+0x14`.
2. `0x664E06` calls the judgement-frame processor at `0x6401E0`.
3. `0x664E23` commits `Tune+0x10 += Tune+0x14` only after judgement returns.

The history helpers at `0x659860` and `0x62D980` propagate the previous held
mask into skipped frame entries. They do not poll the physical device again and
do not create a new physical pressed edge.

`0x6401E0` loops `m = 1 .. Tune+0x14`. For each iteration it computes

```
frame = Tune+0x10 + m
recognition_ms = trunc(frame * milliseconds_per_frame)
```

and calls `0x5D68E0` followed by the companion state/score processing at
`0x6402C9`, using the same recognition time and frame metadata for that
iteration. In the unmodified IDB the operand at `0x6401EF` loads
`0x6FC0A0 = 16.66666603088379` ms/frame. The runtime framerate source can
rewrite that shared operand to `FramerateProfile::frame_milliseconds()` at
RVA `0x002FC0A0`; the native argument is still an integer millisecond value,
so non-integral frame periods are truncated at this boundary.

The current song-clock helper constructs its catch-up limit as
`floor(50 ms * target_fps)` (with a denominator of 1000, minimum one), then
selects at most that many pending frame ticks per outer update. The resulting
upper bounds are:

| Target rate | Maximum song-clock step | Maximum `0x5D68E0` calls in one update |
|---:|---:|---:|
| 60 FPS | 3 | 3 |
| 144 FPS | 7 | 7 |
| 165 FPS | 8 | 8 |
| 240 FPS | 12 | 12 |

These are backlog limits, not guaranteed call counts. The authoritative
physical transition timestamp remains the QPC-derived song-time value in the
loader timeline; the native recognition argument is the frame-derived integer
millisecond projection. `GameTimeOffset` enters the song-clock observation;
`JudgTimeOffset` and all other judgement-window values remain native/static
settings under the audit rules.

## Physical transition to one judgement sample

The source path is:

`InputPollingRuntime::Publish` -> `InputTransitionJournal` ->
`HighFpsInputBridge::BeginJudgement` -> `SongTimedInputTimeline::BuildSample`.

`BeginJudgement` drains the transport journal, maps transitions against the
current song-time anchor, and creates one `JudgementInputScope` for the native
`0x5D68E0` invocation. `BuildSample` consumes pending transitions in timestamp
order while `transition.time <= recognition_ms`; consumed transitions are
removed from the pending queue exactly once for that scope. A transition after
the recognition boundary remains deferred. The sample carries the pressed
pulse, current held mask, authored-history lookback, and QPC-derived edge
metadata.

This means a catch-up update with several native steps creates several scopes:
an edge is visible in the first step whose recognition boundary crosses its
physical time, and held/history state then persists according to the authored
60-Hz history model. The transition is not independently resampled for every
catch-up step.

## Scope lifetime versus native descriptor lifetime

This section originally recorded the source risk that E-045 has now resolved:

1. `hook_core` calls `BeginGameplayJudgement` before entering the original
   `0x5D68E0` and calls `EndGameplayJudgement` only after it returns.
2. The native core iterates its current descriptor/component collections and
   can call `0x5D5720` more than once before returning. The same core call then
   reaches the post-descriptor free-input path at `0x5D2040`.
3. `hook_dispatcher` decodes each descriptor, calls `BeginGameplayNote`, invokes
   the native dispatcher, and calls `EndGameplayNote`.
4. `JudgementInputScope::BeginCurrentNote` reroutes only when
   `note_identity` changes. `RouteForNote` otherwise resets and recomputes
   `current_note_pulses_`, `free_tap_pulses_`, `free_tap_branch_edge_`, and the
   free-input branch flag from the same immutable `sample_.pressed` and edge
   arrays.
5. No descriptor completion consumes or marks a transition in the sample.
   A later native query can therefore observe the same pressed-frame fact, but
   a later loader route can also replace the note/free-input view derived from
   that fact.

E-045 proves that the first behavior is intentional: native `CBooster`
pressed queries are pure, both booster-component passes share one frame, and
candidate order, component conflicts, lifecycle, and static free-input gates
own selection. The defect is therefore not missing descriptor-level edge
consumption. The remaining source risk is the loader's note-dependent
recomputation and replacement of what should be one stable native frame view.

## Loader hook coverage

The high-FPS judgement set has eight dedicated sites:

| Role | RVA | Bridge responsibility |
|---|---:|---|
| Core entry | `0x001D68E0` | Opens/closes one judgement scope around the native core call. |
| Dispatcher | `0x001D5720` | Associates a descriptor/component with the scope and records the native result. |
| Direction matcher | `0x001D2E50` | Captures flick/slide direction head and continuation phases. |
| Held age | `0x002594D0` | Supplies authored-60-Hz held age for recognized direction-family calls. |
| Direction | `0x00259390` | Supplies the scoped direction result for recognized direction-family calls. |
| Late gate | `0x001D0BE0` | Adjusts the recognized note-time argument using the selected/gate edge. |
| Grade | `0x001D0E00` | Adjusts the recognized grade-time argument where the caller contract matches. |
| Free-input branch | `0x001D76CE` | Observes/forces the native post-descriptor free-input permission byte from the scope state. |

The independent Switch query hooks remain `0x00259640` (pressed) and
`0x00259570` (held). Switch aliases and the native forgiving composite/paired
rules remain separate from the high-FPS scope; the audit does not introduce a
lane or simultaneous-note abstraction.

## Historical loader note-type snapshot

The following table records the loader-side classification present at the
audited source revision. It is retained as provenance, not as a native matrix
or an approved design contract. E-046 later proved that raw `B/C/E/D` are
normalized to canonical `A/9/9/4` before native dispatch, so the historical
`11–14 lifecycle/default` classification is stale. The design phase must audit
which descriptor field and normalization stage the current loader actually
observes before changing source; that source-field audit is deliberately not
performed in the native phase.

| IDs | Native family | Recognized scoped queries/branches |
|---|---|---|
| `0` | none/default | Dispatcher only; no dedicated query contract. |
| `1` | normal tap | Pressed, late gate, grade. |
| `2` | flick | Direction-matcher head, late gate, grade; pressed queries not classified as a normal-family edge. |
| `3` | hold | Pressed start, held continuation, late gate. |
| `4` | scratch | Four directional pressed queries, late gate. |
| `5` | beat | Pressed edge, late gate. |
| `6` | merry-go-round/variant | Normal-family pressed, late gate, grade. |
| `7` | chart `HIDDEN` | Normal-family pressed, late gate, grade. |
| `8` | chart `HIDDEN2` | Normal-family pressed, late gate, grade. |
| `9` | critical | Normal-family pressed, late gate, grade. |
| `10` | slide-hold | Direction head/continuation, held, held-age, direction, late gate. |
| `11–14` | **stale classification** | Native raw `B–E` alias to `A/9/4/9`; current loader field handling requires a separate source audit. |
| `15` | dual-hold | Pressed start, held continuation, late gate. |
| post-descriptor free input | `GameplayNoteType::None` loader sentinel after descriptor pass | Pressed callers `0x001D20E0`/`0x001D2176` and free-input branch permission; distinct from chart types `7/8`. |

At that historical revision, calls outside the explicit loader query contract
returned the native value and could record a contract anomaly. This snapshot
does not prove that the current loader routes normalized families correctly and
must not be copied into the redesign.

For long notes, start/end result selection remains separate from held,
direction, and interval validity. The original Switch rules (including
composite/paired IDs and the four-frame forgiving lookback) remain the source
of truth for any future correction.

## Result/publication boundary and conclusion

E-042 proves the native timing-grade helper and note-type routing. E-044 closes
the separate judgement-state, score-state, grade aggregation, score-counter,
and note-metadata ownership paths, and proves that chart `HIDDEN/HIDDEN2`
notes are distinct from post-descriptor free input. E-043 adds the temporal
ownership fact: one QPC transition sample is scoped to a whole native
judgement-core call, not to each descriptor inside it.

The complete native static pipeline is now documented, but the audit is not a
runtime acceptance report and no fix is implemented here. E-045 resolves the
native selection policy: do not consume or claim an edge per descriptor.
The next design must keep one stable sample across both booster components and
free-input processing, preserve the integer recognition-time boundary and all
normalized native families, retain original forgiving rules, and keep
`target_fps <= 60` as a no-op bridge policy.

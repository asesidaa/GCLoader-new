> **RETAINED NATIVE EVIDENCE — NOT A LOADER DESIGN.** Use it with the completed
> E-042 through E-046 audit; do not infer approval of any removed patch.

# Groove Coaster Input-to-Judgement Pipeline

Date started: 2026-08-17
Status: Native static pipeline complete; loader audit and runtime validation remain separate
Binary: `H:\gc\game471.exe`
IDA database: `H:\gc\game471.exe.i64`

## Purpose

This is the canonical, durable record for the complete gameplay path from a
physical input transition to the native judgement result. It replaces
session-local recollection as the starting point for high-FPS input work.

Every verified finding must be recorded here and reflected in saved IDA
names, types, variable names, or comments where it materially improves the
native database. Neither chat history nor an unsaved daemon session counts as
durable audit state.

Do not implement or deploy another judgement correction from a partial trace.
First complete and reconcile the acquisition, frame/timing, note dispatch,
per-note input, grade/result, and loader-hook sections in this document.

## Resume Contract

After a context compact or in a new session:

1. Read this document before inspecting individual hooks or proposing a fix.
2. Connect to the existing daemon for `H:\gc\game471.exe.i64` with `ida-cli`.
3. Treat only entries marked **Verified** as established native behavior.
4. Recheck the current executable or IDB before promoting a hypothesis.
5. Add verified function names, variable names, types, and comments to the IDB.
6. Save the IDB after each coherent annotation batch and record it below.
7. Keep static/build evidence separate from actual 60/240 FPS game acceptance.

IDA annotations are a required deliverable of this audit. Names and comments
must describe verified semantics rather than speculative intent. Use proposal
helpers before mutations, and do not rename uncertain functions merely to make
the call graph look complete.

## Evidence Rules

- **Verified:** current IDB decompilation and disassembly agree, relevant
  callers/callees were checked, and the claim is specific enough to annotate.
- **Source verified:** current GCLoader source establishes loader-side behavior.
- **Runtime observed:** a named runtime log or manual game run establishes the
  observation; this does not prove the inferred native cause.
- **Hypothesis:** plausible but still requires native or runtime evidence.

## Audit Coverage

| Stage | Required evidence | Status |
|---|---|---|
| Physical devices and polling | Keyboard, Raw Input/HID, XInput, mapper, publication timestamp | Source verified |
| iDmac/FastIO handoff | Register `0x4120`, game import caller, bit mapping | Verified |
| Native input sampling | Current state, pressed/held history, authored frame indexing | Verified |
| Game-frame scheduling | Render frame, gameplay frame, catch-up calls, song-time source | Verified |
| Current-note selection | Descriptor iteration, mute/unmute/late boundaries, free tap | Verified |
| Dispatcher | Note IDs `0..15`, booster-component loop, return meaning | Verified |
| Per-note judgement | Every queried input and state transition for each note type | Verified |
| Result production | GREAT/GOOD/COOL/MISS, long-note completion, effects/sounds/accounting | Verified |
| Loader intervention | Every hook, patched argument/result, fallback, and timing domain | Pending |
| Runtime validation | 240 FPS first, then 60 FPS no-op validation | Pending |

## Native Annotation Ledger

Record each persisted IDB batch here. Include the exact functions and the
semantic reason for each name/comment so later work can audit or revise it.

| Date | IDB changes | Evidence | Saved |
|---|---|---|---|
| 2026-08-17 | Renamed and commented acquisition functions `0x4B4EA0`, `0x4B4500`, `0x456360`, and `0x455C80`; annotated the `0x4120` read at `0x4B5153` | Current decompilation/disassembly and caller checks | Yes |
| 2026-08-17 | Renamed and commented `CBooster` history functions `0x62CFB0`, `0x62D670`, `0x62DC60`, `0x62DFB0`, `0x62DF50`, `0x62DD30`, `0x62DAA0`, and `0x62E480`, plus gameplay wrappers `0x659640`, `0x659570`, `0x6594D0`, and `0x659390` | Current decompilation, RTTI/vtable identity, and focused native artifacts | Yes |
| 2026-08-17 | Renamed proven manager accessors `0x412230` → `CTuneGameManager_GetMember147`, `0x412250` → `CTuneGameManager_GetMember149At`, and `0x4380E0` → `CTuneGameManager_GetMember155At`; added repeatable comments to the manager timing path, dispatcher, all dedicated handlers, free-tap path, generic progression, and result helpers | E-042 and `CLASS-AWARE-ANNOTATION-LEDGER.md` in the sibling audit evidence tree | Yes; saved IDB SHA-256 `0E6F205CCC69C99681E3236F7A9B9BFF84D768E99D897CDA6D42D83621D1617C` |
| 2026-08-17 | Annotated native catch-up order, integer recognition-time projection, whole-core loader scope, and the `milliseconds_per_frame` local | E-043 and catch-up artifact | Yes; saved IDB SHA-256 `93D9F5CD83B49E2842971084CC8887F10EBECB8F4A8D32E0E4C2555AE08427CC` |
| 2026-08-17 | Renamed/commented the score-frame processor, resolved-grade getter, timing/duration grade helpers, component aggregator, and note-result metadata publisher | E-044 result-publication and score-state artifacts | Yes; final saved IDB SHA-256 `C45D598BEDA2183D3DD79D58D83F444B448E7983CC800114D4C8FD36ECF78AFC` |

### Annotation batch details (2026-08-17)

The following are the exact new mutations in the saved batch. Class-qualified
names are limited to the three accessors whose receiver is proven by RTTI,
constructor writes, and manager callsites; note handlers remain generic EAs.

| EA | Old name | New name or repeatable comment | Reason |
|---|---|---|---|
| `0x412230` | `sub_412230` | `CTuneGameManager_GetMember147` | Reads manager member `+147`. |
| `0x412250` | `sub_412250` | `CTuneGameManager_GetMember149At` | Bounds-checks/indexes manager collection `+149`. |
| `0x4380E0` | `sub_4380E0` | `CTuneGameManager_GetMember155At` | Bounds-checks/indexes manager collection `+155`. |
| `0x6401E0` | — | `CTuneGameManager judgement-frame processor: converts the frame-domain current value to milliseconds, then calls the judgement core. Windows and offsets remain millisecond-domain.` | Manager judgement entry. |
| `0x6401EF` | — | `Live operand loads 0x6FC0A0 = 16.666666 ms/frame (60-FPS binary constant). Do not describe this instruction as an 8.333 ms/120-Hz conversion.` | Corrects stale timing annotation. |
| `0x6630B0` | — | `CTuneGameManager gameplay state machine: advances the native gameplay frame and reaches judgement/update work on each native update; target-rate conversion belongs at explicit consumers.` | Manager state-machine entry. |
| `0x5D5720` | — | `Note-type dispatcher: 1/7/8 normal tap, 2 flick, 3 hold, 4 scratch, 5 beat, 6 variant, 9 critical tap, A slide-hold, F dual-hold; 0/B/C/D/E use default progression.` | Complete dispatcher map. |
| `0x5D1D50` | — | `Common tap judgement core: evaluates the pressed-edge input path and timing window; caller-specific wrappers publish result/effect state.` | Shared tap core. |
| `0x5D1FA0` | — | `Normal tap wrapper for note types 1, 7, and 8; invokes the common tap core and publishes the accepted note state.` | Normal tap wrapper. |
| `0x5D1F70` | — | `Critical-tap wrapper for note type 9; invokes the common pressed-edge tap core.` | Critical tap wrapper. |
| `0x5D3320` | — | `Flick handler for note type 2; combines the pressed/flick helper path with the common timing/result publication path.` | Flick handler. |
| `0x5D41B0` | — | `Hold handler for note type 3: pressed start, held continuation, and separate start/end versus interval validity.` | Hold handler. |
| `0x5D3C60` | — | `Scratch handler for note type 4: samples four directional pressed controls and maintains direction state; start/end is distinct from interval validity.` | Scratch handler. |
| `0x5D3920` | — | `Beat handler for note type 5: pressed-edge timing plus the common hold/interval state helpers.` | Beat handler. |
| `0x5D35C0` | — | `Slide-hold handler for note type A: held/consecutive-held input and direction matching, with distinct start/end and interval checks.` | Slide-hold handler. |
| `0x5D5540` | — | `Dual-hold wrapper for note type F: delegates hold processing and checks the other active component.` | Dual-hold wrapper. |
| `0x5D5660` | — | `Variant handler for note type 6: resolves a component/sub-index, then invokes the common tap timing path.` | Variant handler. |
| `0x5D2040` | — | `Post-chart free-input path, separate from chart types 7 HIDDEN and 8 HIDDEN2. It checks active chart conflicts, then queries controls 4 and 9 from the same immutable frame history and emits effect event type 4.` | Free-input path, not a chart hidden note. |
| `0x5D58D0` | — | `Generic interval/post-note progression and default handling for note types without a dedicated dispatcher target.` | Default/interval path. |
| `0x5D0E00` | — | `Computes the timing-grade index from absolute timing error; debug labels at 0x6E7C00 are MISS, GOOD, COOL, GREAT.` | Grade-index helper. |
| `0x5D09C0` | — | `Returns component/effect selection values; do not treat this helper as the GREAT/GOOD/MISS grade mapper.` | Effect selector boundary. |
| `0x5D0820` | — | `Publishes per-note state and result/effect metadata; score state is kept distinct from visual/audio effect selection.` | Publication boundary. |

### Result-publication closure batch (2026-08-17)

| EA | Persisted name | Verified responsibility |
|---|---|---|
| `0x5CF930` | `GameplayScoreState_ProcessJudgementFrame` | Consumes resolved judgement grades for one recognition timestamp and updates streak/combo-derived score state. |
| `0x5D2780` | `GameplayJudgementState_GetResolvedGrade` | Returns the descriptor grade or the selected component grade. |
| `0x5D04F0` | `GameplayJudgementState_ComputeDurationGrade` | Computes grade `0..3` from accepted-duration coverage. |
| `0x5D0E00` | `GameplayJudgementState_ComputeTimingGrade` | Computes grade `0..3` from absolute recognition-time error. |
| `0x5D1110` | `GameplayJudgementState_AggregateComponentGrades` | Aggregates component grades/timestamps into the descriptor result. |
| `0x5D0820` | `GameplayJudgementState_PublishNoteResultMetadata` | Publishes note/effect metadata; does not own score counters. |

## Pipeline Map

### 1. GCLoader acquisition and publication — Source verified

- Raw keyboard transitions are decoded, applied to `InputMapper`, and
  published immediately by `InputPollingRuntime::OnRawInput`.
- Changed Raw HID state and timer-polled XInput state both flow through
  `ApplyControllerState`, which updates mapped controller bindings and calls
  `Publish`.
- `Publish` obtains the complete FastIO word from `InputMapper::GetInput`,
  atomically exchanges `g_published_input`, and records a QPC-stamped
  transition when the word changes.
- `iDmacDrvRegisterRead` returns `ReadPublishedInput()` for
  `FIO_NODE_0_INPUT` (`0x4120`). This export does not transform the published
  word.

Source anchors:

- `src/Input/Polling/InputPollingRuntime.cpp`
- `src/Input/Polling/InputMapper.cpp`
- `src/Input/HighFps/HighFpsInputBridge.cpp`
- `src/Driver/iDmac/iDmacDrv32.cpp`
- `src/Driver/iDmac/RegisterOpTypes.h`

### 2. Native iDmac/FastIO handoff — Verified

1. `XioFioBoard_PollRegistersAndBuffers` (`0x4B4EA0`) calls the iDmac register
   read wrapper at `0x4B5153` with register `0x4120` and destination board
   snapshot offset `+0x4`.
2. `GWInputDeviceXioFioBoost_UpdateSnapshotFromIdmac` (`0x4B4500`) performs
   the regular device update and copies the board value from its object offset
   `+0x24` (embedded board snapshot `+0x4`) into generic input snapshot offset
   `+0x78`, with native device-status-bit handling.
3. `GWInputXio_UpdatePollAggregate` (`0x456360`) polls registered device
   vfuncs, aggregates their current words, and calls
   `GWInputXio_ComputeHeldPressedReleasedRepeat` (`0x455C80`) for the aggregate
   groups.
4. The edge helper computes `pressed = current & (current ^ previous)` and
   `released = ~current & (current ^ previous)`, stores current as previous,
   and advances per-bit repeat counters.

`XioFioBoard_PollRegistersAndBuffers` is also used during probe and
initialization. Its existence does not establish the regular gameplay cadence;
that cadence must be derived from the caller schedule.

### 3. `CBooster` gameplay history — Verified

- RTTI and the vtable identify the object at this layer as `CBooster`.
- `CBooster_UpdateInputHistoryAtFrame` (`0x62CFB0`) samples the ten gameplay
  controls, builds a held mask, records per-side pressed counts, writes or ORs
  the mask into the circular history entry for the supplied frame, records the
  latest frame, and updates 20 consecutive-held counters.
- `CBooster_FrameToHistoryRingIndex` (`0x62D670`) maps a frame plus relative
  offset into the circular history. `CBooster_IsControlHeldAtFrame`
  (`0x62DF50`) reads an entry, while `CBooster_GetConsecutiveHeldFrameCount`
  (`0x62DAA0`) reads the current per-control run length.
- For basic control IDs `0..9`, `CBooster_WasControlPressedAtFrame`
  (`0x62DFB0`) requires held now and not held in the prior frame;
  `CBooster_WasControlReleasedAtFrame` (`0x62DD30`) is the inverse transition.
- Composite IDs `10..14` accept either constituent edge. Composite IDs
  `15..19` accept both constituent edges in the current frame, or one current
  edge plus the other within the preceding four frames. These are native
  forgiving rules and must be preserved.
- `CBooster_GetDirectionVectorAtFrame` (`0x62E480`) derives an X/Y vector from
  the selected history entry and booster/control group.
- Gameplay-facing wrappers at `0x659640`, `0x659570`, `0x6594D0`, and
  `0x659390` select the requested device and expose pressed, held, held-age,
  and raw direction-code queries. A frame argument of `-1` on the pressed,
  held, and direction paths selects the input manager's current frame.

The live task traces bound the native order: aggregate polling feeds the
gameplay capture entry, `CBooster` history is updated before the manager's
judgement-frame work, and the manager state machine reaches the judgement core
on its native update path. Runtime cadence and acceptance remain separate from
this static call graph.

### 4. Gameplay task and timing domain — Verified

- `CSeqTaskBase` lifecycle slots provide the generic task state machine;
  `CGameMainTask_Update_StateMachine` owns the active gameplay branch.
- `CDemoPlayTask` slot 6 reaches the same
  `CTuneGameManager_RunGameplayFrameStateMachine` (`0x6630B0`) path; it is not
  evidence for a separate judgement algorithm.
- `CTuneGameManager` is RTTI-backed by vtable `0x6FA64C` and locator
  `0x70F6B0`. Its constructor initializes the collections at member offsets
  `+147`, `+149`, and `+155`; the three accessor names are persisted above.
- `CTuneGameManager_ProcessJudgementFrame` (`0x6401E0`) converts the current
  frame-domain value to milliseconds and calls `0x5D68E0`. The live operand at
  `0x6401EF` loads `0x6FC0A0`, bytes `55 55 85 41`, which decode as
  `16.66666603088379` ms/frame. The former 8.333 ms/120-Hz comment was stale.
- The active manager step fills gameplay input history, invokes judgement, and
  commits the authoritative frame in that order. `0x6401E0` loops once per
  pending catch-up step and passes `trunc(frame * milliseconds_per_frame)` to
  the judgement core. The loader's 50 ms backlog cap permits at most
  3/7/8/12 native recognition calls at 60/144/165/240 FPS.
- Skipped native history entries copy held state; they do not poll the physical
  device again or create new pressed edges.
- `GameTimeOffset` and `JudgTimeOffset` are the only variable timing settings
  in this audit. Other judgement settings are treated as static millisecond
  values and are not rescaled.

### 5. Note selection and dispatch — Verified

`0x5D4E70` prepares the current note/component state and `0x5D5720` dispatches
the note type. `0x5D58D0` performs generic interval and post-note progression,
including the default path. The native model remains a single Switch lane;
generic engine support is not promoted to a live simultaneous-note rule.

### 6. Result and publication boundary — Verified

- `CTuneGameManager+0x254` stores the 444-byte per-player
  `GameplayJudgementState`. `+0x26C` stores the separate 368-byte
  `GameplayScoreState`; its constructor writes byte zero at offset `0`, not a
  vtable, so it is non-polymorphic.
- `GameplayJudgementState_ComputeTimingGrade` (`0x5D0E00`) and
  `GameplayJudgementState_ComputeDurationGrade` (`0x5D04F0`) produce grades
  `0..3`; `GameplayJudgementState_AggregateComponentGrades` (`0x5D1110`)
  resolves component results. The labels are `MISS`, `GOOD`, `COOL`, `GREAT`.
- `GameplayScoreState_ProcessJudgementFrame` (`0x5CF930`) follows judgement
  processing with the same integer `recognition_ms`, retrieves the resolved
  descriptor/component grade through `0x5D2780`, and updates counters at
  score-state offsets `+120/+124/+128/+132` for grades `0/1/2/3`.
- `0x5D09C0` returns component/effect selection values (`-1/0/1/3`); it is
  not labeled as the GREAT/GOOD/MISS mapper.
- `GameplayJudgementState_PublishNoteResultMetadata` (`0x5D0820`) publishes
  per-note result/effect metadata. It does not own the score-state grade
  counters.

## Per-Note Matrix

The following matrix is the complete static coverage for IDs `0..15` and the
post-chart free-input path. “Generic progression” means no dedicated input/result
handler was proven for that ID; the common interval/post-note path still runs.

| Type | Dispatcher/handler | Input query | Judgement/state boundary |
|---|---|---|---|
| `0` | default → `0x5D58D0` | no dedicated query proven | generic progression |
| `1` | `0x5D1FA0` → `0x5D1D50` | pressed edge | tap start/result |
| `2` | `0x5D3320` | pressed/flick helper path | flick start/result |
| `3` | `0x5D41B0` | pressed start + held continuation | hold start/end; interval validity |
| `4` | `0x5D3C60` | four directional pressed samples + direction state | scratch start/end; interval validity |
| `5` | `0x5D3920` | pressed edge | beat start/result |
| `6` | `0x5D5660` → common core | component/sub-index plus common timing path | variant start/result |
| `7` (`HIDDEN`) | `0x5D1FA0` → `0x5D1D50` | pressed edge | hidden-chart-note tap start/result |
| `8` (`HIDDEN2`) | `0x5D1FA0` → `0x5D1D50` | pressed edge | hidden-chart-note tap start/result |
| `9` | `0x5D1F70` → `0x5D1D50` | pressed edge | critical-tap start/result |
| `A` | `0x5D35C0` | held, consecutive-held count, direction matching | slide-hold start/end; interval validity |
| `B` | default → `0x5D58D0` | no dedicated query proven | generic progression |
| `C` | default → `0x5D58D0` | no dedicated query proven | generic progression |
| `D` | default → `0x5D58D0` | no dedicated query proven | generic progression |
| `E` | default → `0x5D58D0` | no dedicated query proven | generic progression |
| `F` | `0x5D5540` → `0x5D41B0` | hold wrapper + other active-component check | dual-hold start/end; interval validity |
| Post-chart free input | `0x5D2040` after chart pass | pressed controls `4` and `9`, conflict suppression | free-input/key-sound effect path; no chart grade/score result |

For hold, slide-hold, scratch, beat, and dual-hold rows, start/end result
selection is separate from in-between held/interval validity. Chart
`HIDDEN/HIDDEN2` notes are real descriptors and must not be conflated with
post-chart free input. The matrix does not invent lanes, simultaneous-note
behavior, or a new forgiving rule; the native Switch composition and paired-ID
four-frame lookback remain the source of truth.

## Loader Hook Reconciliation

The native pipeline is now documented before loader intervention is considered.
This record does not approve a new high-FPS patch. Existing loader hooks must
be reconciled against this matrix in a separate implementation plan, with
`target_fps=60` remaining a no-op for gameplay timing changes and with the
original forgiving Switch rules preserved. The known source boundary is that
one `JudgementInputScope` spans a complete native recognition call, while
descriptor routing and the later free-input branch can both recompute state
from the same immutable sample. Native discovery is closed; this remaining
ownership question belongs to the loader bridge.

## Supporting evidence

- `H:\gc\runs\20260815T182438Z-297470b1\artifacts\game471-class-aware-audit-batch-2026-08-17.json`
- `H:\gc\runs\20260815T182438Z-297470b1\artifacts\game471-note-type-status-batch-2026-08-17.json`
- `H:\gc\runs\20260815T182438Z-297470b1\artifacts\game471-note-switch-target-batch-2026-08-17.json`
- `H:\gc\artifacts\GCLoader\.planning\debug\high-fps-timing-domains\evidence\E-042-class-aware-gameplay-pipeline.md`
- `H:\gc\artifacts\GCLoader\.planning\debug\high-fps-timing-domains\evidence\E-043-native-catchup-loader-scope-audit.md`
- `H:\gc\artifacts\GCLoader\.planning\debug\high-fps-timing-domains\evidence\E-044-native-result-publication-closure.md`
- `H:\gc\artifacts\GCLoader\.planning\debug\high-fps-timing-domains\CLASS-AWARE-ANNOTATION-LEDGER.md`
- `H:\gc\runs\20260816T210335Z-a3aabe78\artifacts\game471-native-result-publication-closure-pass6.json`
- `H:\gc\runs\20260816T210335Z-a3aabe78\artifacts\game471-score-state-owner-pass5-2026-08-17.json`

## Remaining questions

E-042 through E-044 resolve the native call graph, frame conversion, note
coverage, input semantics, grade generation, score accounting, and result
metadata publication. Only loader-hook reconciliation and runtime acceptance
remain open:

- Which current loader hooks alter raw helper outputs versus already-normalized
  or already-offset native values, and which maintain state across descriptors?
- How should one QPC transition sample assign note-descriptor ownership before
  the separate post-chart free-input branch without changing native forgiving
  rules or stable 60 FPS judgements?
- Which focused 240 FPS and 60 FPS runs demonstrate the redesign's note-type,
  grade, free-input, performance, and no-op behavior?

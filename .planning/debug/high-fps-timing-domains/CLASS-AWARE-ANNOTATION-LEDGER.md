# Class-aware IDA annotation ledger

Target: `H:\gc\game471.exe.i64`
Audit date: 2026-08-17
Mutation policy: rename only when RTTI receiver ownership is proven; use
repeatable comments for binary evidence and timing-domain caveats.

The E-042 annotation batch started from IDB SHA-256
`1DDF946D051BF3628FFAADC4A1928F7CE6879AD5B92CE160E1EF331828A0A8A2`. The
E-043 catch-up annotation batch started from the already annotated IDB
`8395E2B83A1C00424B98CBFD1DD747F0A3197C1007A6C9EB522288F96F7C81BC`.
The source evidence for every row is E-042 through E-046 and the linked
batched artifacts.

## Renames

| EA | Old name | New name | Reason | Evidence |
|---|---|---|---|---|
| `0x412230` | `sub_412230` | `CTuneGameManager_GetMember147` | Reads `this[147]`; constructor, cleanup, and manager callsites prove the receiver is `CTuneGameManager`. | E-042; class-aware batch |
| `0x412250` | `sub_412250` | `CTuneGameManager_GetMember149At` | Bounds-checks and indexes `this[149]`, a manager-owned collection used by judgement and gameplay paths. | E-042; class-aware batch |
| `0x4380E0` | `sub_4380E0` | `CTuneGameManager_GetMember155At` | Bounds-checks and indexes `this[155]`, a manager-owned collection used by judgement/render paths. | E-042; class-aware batch |

No class-qualified name is applied to the free/static note handlers. Their
receiver is a gameplay context, but the RTTI evidence does not prove a unique
class owner for those helper EAs.

## Repeatable comments

| EA | Previous repeatable comment | New repeatable comment | Evidence |
|---|---|---|---|
| `0x6401E0` | `RTTI/class-aware non-virtual member: CTuneGameManager judgement-frame processor, called with the same manager object after input-history advancement. Timing semantics remain under audit.` | `CTuneGameManager judgement-frame processor: converts the frame-domain current value to milliseconds, then calls the judgement core. Windows and offsets remain millisecond-domain.` | E-042; class-aware batch |
| `0x6401EF` | `120Hz patch: judgement current ms = frame * 8.333333; judgement windows remain ms.` | `Live operand loads 0x6FC0A0 = 16.666666 ms/frame (60-FPS binary constant). Do not describe this instruction as an 8.333 ms/120-Hz conversion.` | E-042; live bytes |
| `0x6630B0` | `Central Tune gameplay state machine. Full 120Hz plan keeps Tune+0x14=1 and advances Tune+0x10 every update; convert frame/ms constants instead of skipping updates.` | `CTuneGameManager gameplay state machine. In the active Tune step it fills input history for Tune+0x10 + Tune+0x14, invokes judgement, then advances authoritative frame Tune+0x10 only after judgement returns.` | E-042; class-aware batch |
| `0x5D5720` | *(empty)* | **Superseded E-042 comment:** `Note-type dispatcher: 1/7/8 normal tap, 2 flick, 3 hold, 4 scratch, 5 beat, 6 variant, 9 critical tap, A slide-hold, F dual-hold; 0/B/C/D/E use default progression.` E-046 disproves the raw `B–E` clause. The current IDB comment instead documents per-component dispatch return semantics without repeating a raw-type table. | E-042 initial dispatcher batch; E-046 correction |
| `0x5D1D50` | *(empty)* | `Common tap judgement core: evaluates the pressed-edge input path and timing window; caller-specific wrappers publish result/effect state.` | E-042; handler artifacts |
| `0x5D1FA0` | *(empty)* | `Normal tap wrapper for note types 1, 7, and 8; invokes the common tap core and publishes the accepted note state.` | E-042; dispatcher/handler artifacts |
| `0x5D1F70` | *(empty)* | `Critical-tap wrapper for note type 9; invokes the common pressed-edge tap core.` | E-042; dispatcher/handler artifacts |
| `0x5D3320` | *(empty)* | `Flick handler for note type 2; combines the pressed/flick helper path with the common timing/result publication path.` | E-042; flick artifact |
| `0x5D41B0` | *(empty)* | `Hold handler for note type 3: pressed start, held continuation, and separate start/end versus interval validity.` | E-042; hold artifact |
| `0x5D3C60` | *(empty)* | `Scratch handler for note type 4: samples four directional pressed controls and maintains direction state; start/end is distinct from interval validity.` | E-042; scratch artifact |
| `0x5D3920` | *(empty)* | `Beat handler for note type 5: pressed-edge timing plus the common hold/interval state helpers.` | E-042; beat artifact |
| `0x5D35C0` | *(empty)* | `Slide-hold handler for note type A: held/consecutive-held input and direction matching, with distinct start/end and interval checks.` | E-042; slide-hold artifact |
| `0x5D5540` | *(empty)* | `Dual-hold wrapper for note type F: delegates hold processing and checks the other active component.` | E-042; dual-hold artifact |
| `0x5D5660` | *(empty)* | `Variant handler for note type 6: resolves a component/sub-index, then invokes the common tap timing path.` | E-042; dispatcher/handler artifacts |
| `0x5D2040` | *(empty)* | `Post-chart free-input path, separate from chart types 7 HIDDEN and 8 HIDDEN2. It checks active chart conflicts, then queries controls 4 and 9 from the same immutable frame history and emits effect event type 4.` | E-042 initial trace; E-044 current readback |
| `0x5D58D0` | *(empty)* | **Superseded E-042 comment:** `Generic interval/post-note progression and default handling for note types without a dedicated dispatcher target.` E-046 proves this helper refreshes row free-input eligibility and is not generic type progression or cursor advancement. | E-042 initial note-switch batch; E-046 correction |
| `0x5D0E00` | *(empty)* | `Computes the timing-grade index from absolute timing error; debug labels at 0x6E7C00 are MISS, GOOD, COOL, GREAT.` | E-042; result trace |
| `0x5D09C0` | *(empty)* | `Returns component/effect selection values; do not treat this helper as the GREAT/GOOD/MISS grade mapper.` | E-042; result trace |
| `0x5D0820` | *(empty)* | `Publishes per-note state and result/effect metadata; score state is kept distinct from visual/audio effect selection.` | E-042; result trace |

## E-043 catch-up annotation batch

The following mutations were applied in one daemon request after the E-043
static catch-up/scope audit and saved once.

### Repeatable instruction comments

| EA | Previous repeatable comment | New repeatable comment | Evidence |
|---|---|---|---|
| `0x6402A9` | `Judgement core is ms-domain; frame arg is metadata/effect index. Windows compare against baked note ms fields and system.cfg ms settings.` | `Per native catch-up step: calls the judgement core with integer recognition_ms and frame index; this is the single core invocation around which the loader opens one JudgementInputScope.` | E-043; catch-up artifact |
| `0x6402C9` | *(empty)* | `Per native catch-up step: companion state/score processing consumes the same integer recognition_ms after the judgement core; it is not a second physical-input sample.` | E-043; catch-up artifact |

### Function comments

| EA | Previous function comment | New function comment | Evidence |
|---|---|---|---|
| `0x6401E0` | `Gameplay judgement caller converts frame index to current ms, then calls ms-domain judgement core. Use 1000/120 when frame counter advances at 120Hz; do not scale judgement windows.` | `CTuneGameManager judgement-frame processor: for each pending native step, computes trunc((Tune+0x10+m) * milliseconds_per_frame), calls the judgement core, then companion state/score processing. The frame-to-ms projection is integer and separate from physical transition sampling.` | E-043; catch-up artifact |
| `0x5D68E0` | `Judgement core is ms-domain; frame arg is metadata/effect index. Windows compare against baked note ms fields and system.cfg ms settings.` | `Native judgement core: one loader JudgementInputScope spans this complete call; the core may dispatch multiple descriptors and then run post-chart free-tap logic. Timing windows remain millisecond-domain.` | E-043; loader/core scope audit |
| `0x6630B0` | `Central Tune gameplay state machine. Full 120Hz plan keeps Tune+0x14=1 and advances Tune+0x10 every update; convert frame/ms constants instead of skipping updates.` | `CTuneGameManager gameplay state machine: fills input history, runs judgement/catch-up, and commits Tune+0x10 only after judgement returns. Skipped history entries propagate held state rather than new physical edges.` | E-043; native update-order audit |

### Hex-Rays local rename

| EA | Previous local name | New local name | Reason | Evidence |
|---|---|---|---|---|
| `0x6401E0` (`[ebp-0x0C]`) | `_120Hz_patch:_judgement_current_ms___frame___8.333333__judgement` | `milliseconds_per_frame` | Removes a stale 120-Hz label; the live operand is a configurable frame-millisecond projection and is truncated at the native recognition boundary. | E-043; live decompile/operand |

## E-044 result-publication annotation batch

The following six function renames and repeatable comments were proposed,
applied, saved once, and read back from the existing daemon after the native
result/score ownership closure.

| EA | Old name | New name | Repeatable comment | Evidence |
|---|---|---|---|---|
| `0x5CF930` | `sub_5CF930` | `GameplayScoreState_ProcessJudgementFrame` | `Processes one score-state frame for the CTuneGameManager recognition timestamp: consumes resolved judgement grades, then updates streak/combo-derived score state.` | E-044; score-state owner artifact |
| `0x5D2780` | `sub_5D2780` | `GameplayJudgementState_GetResolvedGrade` | `Returns resolved grade 0..3 from descriptor +4 when the component selector is -1, or from the selected component +4 otherwise; labels are MISS, GOOD, COOL, GREAT.` | E-044; result-publication artifact |
| `0x5D04F0` | `sub_5D04F0` | `GameplayJudgementState_ComputeDurationGrade` | `Computes grade 0..3 from accepted-duration coverage over the descriptor interval; GREAT-only mode maps every non-GREAT result to MISS.` | E-044; result-publication artifact |
| `0x5D0E00` | `sub_5D0E00` | `GameplayJudgementState_ComputeTimingGrade` | `Computes grade 0..3 from absolute recognition-time error against descriptor timing thresholds; labels are MISS, GOOD, COOL, GREAT.` | E-044; result-publication artifact |
| `0x5D1110` | `sub_5D1110` | `GameplayJudgementState_AggregateComponentGrades` | `Aggregates resolved component grades and timestamps into the descriptor grade/timestamp, finalizes unresolved components, and reports whether the descriptor grade is non-MISS.` | E-044; result-publication artifact |
| `0x5D0820` | `sub_5D0820` | `GameplayJudgementState_PublishNoteResultMetadata` | `Publishes per-player note result/effect metadata and its replace-or-increment flag; score-state grade counters are updated through the separate GameplayScoreState path.` | E-044; result-publication artifact |

## E-045 native component/free-input closure

### Rename

| EA | Old name | New name | Evidence |
|---|---|---|---|
| `0x5E8B80` | `sub_5E8B80` | `GameplayNoteDescriptor_IsMute` | Native debug label `IsMute`, score-kind `16`, score suppression, and free-input gate callers in E-045 |

### Repeatable comments

| EA | Previous repeatable comment | New repeatable comment | Evidence |
|---|---|---|---|
| `0x5E8B80` | *(empty)* | `Returns descriptor+0x08 IsMute. Muted descriptors use score kind 16 and do not close the native post-descriptor free-input gate.` | E-045 |
| `0x5D4E70` | `Builds the first incomplete eligible descriptor per chart collection, sorts candidates by authored recognition time, then prunes them. Components 0/1 use +0x54 cursor rows; -1 uses global +0x3C cursors.` | `Builds the first incomplete candidate per native chart row for booster component 0 or 1, orders candidates by effective descriptor time, and preserves native overlap/tie filtering. These components are not gameplay lanes.` | E-045 |
| `0x5D68E0` | `Processes one integer-millisecond recognition step. It scans exactly two booster components (0 forward, 1 reverse), then lifecycle/reconciliation and guarded free-input processing; physical queries are immutable history lookups.` | `Processes one integer-millisecond recognition step: both booster-component passes observe the same non-consuming frame history, each pass stops after its first successful candidate, then native lifecycle/aggregation and guarded free-input processing run.` | E-045 |
| `0x5D2040` | `Post-chart free-input path, separate from chart types 7 HIDDEN and 8 HIDDEN2. It checks active chart conflicts, then queries controls 4 and 9 from the same immutable frame history and emits effect event type 4.` | `Post-descriptor free-input path, distinct from chart HIDDEN/HIDDEN2. Rechecks controls 4/9, rejects active-component conflicts, and applies FreeTapDisableTimeAfterMark/FreeTapDisableAfterMissMark before effect event type 4.` | E-045 |
| `0x62DFB0` | `Pressed-at-frame query. IDs 0..9 require active now and inactive in the prior frame. IDs 10..14 accept either constituent pressed edge. IDs 15..19 accept both constituent edges now, or one now plus the other within the prior four frames.` | `Pure pressed-at-frame history query; it never consumes an edge. IDs 0..9 compare current/prior bits, 10..14 accept either constituent, and 15..19 preserve the native four-frame paired-input forgiveness.` | E-045 |

The `0x5D68E0` function comment was also replaced with:
`Native recognition step: two booster-component passes share one non-consuming
frame-history sample; each pass accepts at most one candidate before native
lifecycle/grade aggregation and guarded post-descriptor free input.`

## E-046 normalization/progression final annotation batch

The final native batch was applied and saved before the documentation cleanup.
It corrects the raw/canonical/effective type model, row eligibility helpers,
and same-row progression semantics.

### Renames

| EA | Old name | New name | Evidence |
|---|---|---|---|
| `0x5D2A00` | `sub_5D2A00` | `GameplayJudgementState_RefineRowFreeInputEligibility` | E-046; progression pass42 |
| `0x5D58D0` | `sub_5D58D0` | `GameplayJudgementState_UpdateRowFreeInputEligibility` | E-046; progression pass42 |
| `0x5E8BA0` | `sub_5E8BA0` | `GameplayNoteDescriptor_IsPairedOrDual` | E-046; normalization pass41 |
| `0x5E8BF0` | `sub_5E8BF0` | `GameplayNoteType_IsLongForm` | E-046; normalization pass41 |
| `0x5E8C30` | `sub_5E8C30` | `GameplayNoteDescriptor_IsVariantType6` | E-046; normalization pass41 |
| `0x5E8C70` | `sub_5E8C70` | `GameplayNoteDescriptor_IsSuppressed` | E-046; normalization pass41 |
| `0x5E8F30` | `sub_5E8F30` | `GameplayNoteDescriptor_IsLongForm` | E-046; normalization pass41 |

### Repeatable comments

| EA | Final meaning recorded | Evidence |
|---|---|---|
| `0x5D2A00` | Refines row free-input eligibility at state `+0xCC`; does not advance a cursor. | E-046; progression pass42 |
| `0x5D4E70` | Builds one first-incomplete effective-nonzero candidate per row from component `+0x54` or generic `+0x3C` cursors; same-row followers are excluded. | E-046; progression pass42 |
| `0x5D58D0` | Refreshes next-step row free-input eligibility at `+0xCC`; does not advance a cursor. | E-046; progression pass42 |
| `0x5D68E0` | Processes fixed per-row candidate lists for both components, then lifecycle/result/free input and next-step row-gate refresh. | E-046; progression pass42 |
| `0x5E8BA0` | Tests effective or canonical type for paired/multi-component families `9/F`. | E-046; normalization pass41 |
| `0x5E8BF0` | Recognizes long-form types `3/4/5/A/F`. | E-046; normalization pass41 |
| `0x5E8C30` | Tests effective or canonical type for variant type `6`. | E-046; normalization pass41 |
| `0x5E8C70` | Tests effective or canonical type for suppressed type `0`; effective `0` is skipped. | E-046; normalization pass41 |
| `0x5E8F30` | Selects effective/canonical type and applies the long-form predicate. | E-046; normalization pass41 |
| `0x5EB210` | Bakes timings, maps raw aliases `B→A`, `C/E→9`, `D→4`, stores canonical `+0x04`, rewrites effective `+0x00`, and suppresses equal-time duplicates. | E-046; normalization pass41 |

The final `0x5D68E0` function comment records that fixed first-incomplete lists
exclude same-row followers, two booster components share one non-consuming
frame fact, and free input precedes next-step row-gate refresh.

## Verification record

- The initial three class-qualified renames are direct manager-member accessors
  backed by RTTI and constructor/caller evidence; later judgement/descriptor
  helper renames are backed by the focused native closure artifacts.
- Handler comments intentionally avoid class-qualified ownership claims.
- The stale 120-Hz/8.333 ms comments are replaced before the IDB is saved.
- The E-042 post-save IDB SHA-256 was
  `0E6F205CCC69C99681E3236F7A9B9BFF84D768E99D897CDA6D42D83621D1617C`.
- The E-043 post-save IDB SHA-256 is
  `93D9F5CD83B49E2842971084CC8887F10EBECB8F4A8D32E0E4C2555AE08427CC`.
- The E-044 post-save IDB SHA-256 at that intermediate checkpoint is
  `C45D598BEDA2183D3DD79D58D83F444B448E7983CC800114D4C8FD36ECF78AFC`.
- The E-045 post-save IDB SHA-256 at that intermediate checkpoint is
  `48FD32C6AB3C2374D9E77E6447F8F6BB5614700BA304C4A9E632CDCF14C77DEC`.
- The E-046 final post-save IDB SHA-256 is
  `3F911E373D18F4C3F11DACF5759AB7FF08847A4F365E8C0ED17B2896E7C47163`.
- A fresh daemon connection read back all three names and all nineteen
  repeatable comments after saving; no annotation was missing.
- A fresh daemon connection also read back the two E-043 instruction comments,
  three E-043 function comments, and the `milliseconds_per_frame` local name;
  `restore_user_lvar_settings` returned one saved local setting.
- A fresh daemon connection read back all six E-044 function names and
  repeatable comments, plus the already-correct `0x5D2040` free-input comment.
- A fresh daemon connection read back the E-045 rename, five repeatable
  comments, and the corrected `0x5D68E0` function comment after saving.
- A later fresh daemon connection read back all seven E-046 names, ten
  repeatable comments, and the final `0x5D68E0` function comment. The readback
  artifact is `native-annotation-readback-pass43.json`, SHA-256
  `48AA2536D052881C20C902FEB05D4109D3185E50E88D935B4F3EDE5001A6F274`.

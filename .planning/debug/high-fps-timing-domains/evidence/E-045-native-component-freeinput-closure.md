# E-045: Native booster-component and free-input closure

Date: 2026-08-17
Target: `H:\gc\game471.exe.i64`
Scope: native game behavior only; no loader implementation or deployment

## Purpose and provenance

This record resolves the native question left open by E-043: whether a
physical pressed edge is supposed to be consumed by one descriptor, and how
the one-lane game orders chart judgement versus free input.

- Focused IDA artifact:
  `H:\gc\runs\20260816T210335Z-a3aabe78\artifacts\game471-native-component-freeinput-closure-2026-08-17.json`
  (SHA-256
  `E308A5047C2A5DAE91E8BF8F51FB15E622B4FB96F500D2B4D52E41D55174964C`).
- Intermediate saved IDB after the E-045 rename/comment batch: SHA-256
  `48FD32C6AB3C2374D9E77E6447F8F6BB5614700BA304C4A9E632CDCF14C77DEC`.
- E-046 records the later final annotation batch and final saved IDB hash.
- E-042 through E-044 remain the source of the class, note-type, catch-up,
  grade, score, and result-publication evidence. They were not regenerated.

## Pressed edges are native frame facts

`CBooster_WasControlPressedAtFrame` (`0x62DFB0`) is a pure history query. For
ordinary controls it compares the current and previous history masks; for
composite and paired controls it recursively queries those same masks. It does
not mutate the history, mark an edge consumed, or remember which descriptor
asked first.

Therefore every native caller in one recognition step is intentionally able
to observe the same pressed-frame fact. Descriptor ownership is implemented by
the judgement control flow, not by destructive input reads.

## Two booster components, not two lanes

`GameplayJudgementState_Initialize` (`0x5D4510`) creates exactly two cursor
rows at state `+0x54`. The common tap core at `0x5D1D50` maps component `0` to
control `4` and component `1` to control `9`; `BoosterControlId_ToComponentSet`
(`0x62E560`) maps controls `0..4` to component `0` and `5..9` to component `1`.

These are the two physical booster components of the one gameplay lane. They
are not independent gameplay lanes and do not imply simultaneous chart notes.

For each component, `GameplayJudgementState_BuildOrderedCandidateList`
(`0x5D4E70`) finds the first incomplete descriptor in each native chart row,
orders the candidates by effective descriptor time, and applies the original
overlap/tie filtering. `GameplayJudgementState_ProcessRecognitionStep`
(`0x5D68E0`) then:

1. runs component `0` candidates in native forward order;
2. stops that component pass after the first handler returning success;
3. runs component `1` candidates in native reverse order;
4. stops that component pass after the first success;
5. only then runs descriptor lifecycle, component aggregation, result
   publication, and free-input handling.

Each component list is fixed before its pass begins and contains at most one
first-incomplete descriptor from any internal chart row. A following descriptor
in the same row is therefore absent from both current component lists and
cannot observe the same pressed edge after lifecycle completes the earlier
descriptor.

In the real one-lane path both component passes operate on the same current
chart note before lifecycle advances it. This is why the native code can
preserve both-button and paired-input forgiveness without inventing a second
lane or a global edge-claim table.

## Native free-input eligibility

After the two component passes, the core rebuilds the current candidate set
with component selector `-1`. Free input is not limited to the end of the
song; it is a post-descriptor path inside every recognition step.

For every current candidate whose native opening boundary at descriptor
`+0x98` has been crossed, an unmuted descriptor closes the free-input gate.
The descriptor `+0x08` predicate is proven by the native debug label
`IsMute`, score-kind `16`, and score-suppression callers and is now named
`GameplayNoteDescriptor_IsMute` at `0x5E8B80`.

When the gate remains open, `GameplayJudgementState_ProcessFreeInput`
(`0x5D2040`) still does not blindly accept the edge. It:

- builds component sets for controls `4` and `9`;
- rejects either control when an active chart-note component conflicts;
- queries the same non-consuming pressed history used by chart handlers;
- applies the existing `FreeTapDisableTimeAfterMark` and
  `FreeTapDisableAfterMissMark` event gate; and
- emits effect event type `4` only if those native checks pass.

`ProcessFreeInput` runs before
`GameplayJudgementState_UpdateRowFreeInputEligibility` (`0x5D58D0`) refreshes
the per-row byte at judgement-state `+0xCC`. That refresh does not advance a
descriptor cursor; it prepares row eligibility for the following recognition
step.

With the current static configuration,
`FreeTapDisableTimeAfterMark` is 200 ms and
`FreeTapDisableAfterMissMark` is enabled. These remain native constants for
the high-FPS design; only `GameTimeOffset` and `JudgTimeOffset` are variable.

## Hidden notes remain chart descriptors

Chart types `7` and `8` (`HIDDEN`/`HIDDEN2`, called ad-lib notes in
player-facing terminology) still dispatch through the normal tap handler and
produce native descriptor/component results. The post-descriptor free-input
path is separate.

The note type and the independent descriptor `IsMute` field must not be
collapsed into a loader sentinel. Native ordering handles a hidden chart
descriptor first, records its result/effect state, and only then evaluates the
guarded free-input path.

## Corrected loader design boundary

E-043 correctly found that one loader scope spans both component passes,
descriptor lifecycle, and free input, but its tentative edge-ownership concern
was too broad. Re-observing the same pressed edge is native behavior and must
not be "fixed" with descriptor-level or global consumption.

The remaining source-audit lead is narrower: at the revision captured by
E-043, the bridge rerouted and recomputed the effective pressed/free-input view
when descriptor identity changed, even though native callers observe one
stable frame fact. The design phase must first re-audit current source; if that
behavior remains, it must be removed while leaving native candidate order,
component conflicts, lifecycle, and static timing gates in ownership.

## IDA persistence

The E-045 batch:

- renamed `0x5E8B80` from `sub_5E8B80` to
  `GameplayNoteDescriptor_IsMute`;
- updated repeatable comments at `0x5E8B80`, `0x5D4E70`, `0x5D68E0`,
  `0x5D2040`, and `0x62DFB0`; and
- replaced the stale `0x5D68E0` function comment with the two-component,
  non-consuming native contract.

The database was saved and hashed after those mutations. No game executable,
runtime configuration, loader source, DLL, or deployed artifact changed.

## Evidence boundary

This closes native selection/ownership. E-046 adds the final descriptor
normalization, same-row progression, catch-up edge-uniqueness, and row-gate
ordering proof. Neither record proves that the current loader implements the
contract, and neither is 240-FPS gameplay acceptance. Loader source review and
redesign follow as a separate phase.

# E-044: Native result-publication closure

Date: 2026-08-17
Target: `H:\gc\game471.exe.i64`
Mode: static IDA evidence only; no production patch, build, deployment,
runtime binary, or configuration change

## Purpose and provenance

This record closes the remaining native ownership question left by E-042 and
E-043: which per-player object owns judgement state, which separate object
owns score counters, how grades reach that score object, and whether chart
`HIDDEN`/`HIDDEN2` notes are the same path as post-descriptor free input.

- Result/publication closure artifact:
  `H:\gc\runs\20260816T210335Z-a3aabe78\artifacts\game471-native-result-publication-closure-pass6.json`
  (SHA-256
  `1CAA9694F4C0B98FD6D73D7C0DDEF9BEA81B7A20333B859D0CD593DB65DA8929`).
- Score-state owner artifact:
  `H:\gc\runs\20260816T210335Z-a3aabe78\artifacts\game471-score-state-owner-pass5-2026-08-17.json`
  (SHA-256
  `1DD670D12EDC1D3972C19E6A48D59ED628E31D7649ED7379499CE22846961407`).
- Native note-ownership closure artifact:
  `H:\gc\runs\20260816T210335Z-a3aabe78\artifacts\game471-native-note-ownership-closure-pass4.json`
  (SHA-256
  `9C4EFDE702C0E93A9EBE2ED3D161AB0373EA23A95984C618A45E024E142D822E`).
- Native terminology/string artifact:
  `H:\gc\runs\20260816T210335Z-a3aabe78\artifacts\game471-native-note-terminology-strings-pass5.json`
  (SHA-256
  `0311EA16BD9340EE1F9F66227C4B0E6860106EE375E17123594DC26312E27E66`).

## Per-player state ownership

The construction path rooted at `0x6629A0` creates two different per-player
objects and stores them in parallel `CTuneGameManager` collections:

| Manager offset | Allocation | Proven role |
|---:|---:|---|
| `+0x254` | `0x1BC` bytes (444) | `GameplayJudgementState`: descriptor/component state, input judgement, timing/duration grades, and note-result metadata |
| `+0x26C` | `0x170` bytes (368) | `GameplayScoreState`: resolved-grade counters and streak/combo-derived score state |

The score-state constructor at `0x5CE070` writes byte zero to object offset
`0`; it does not install a vtable. The score state is therefore
non-polymorphic, and its semantic name is based on construction, manager-field,
caller, and counter behavior rather than RTTI.

## Grade and score flow

For every pending recognition step, `CTuneGameManager` processor `0x6401E0`
uses the same integer `recognition_ms` for the judgement-state call and the
following score-state call. The native ownership chain is:

1. `GameplayJudgementState_ProcessRecognitionStep` (`0x5D68E0`) updates the
   per-player judgement state at manager `+0x254`.
2. `GameplayJudgementState_ComputeTimingGrade` (`0x5D0E00`) produces the
   timing grade used by timing-graded tap/flick paths.
3. `GameplayJudgementState_ComputeDurationGrade` (`0x5D04F0`) produces the
   accepted-duration grade used by long-form mechanics.
4. `GameplayJudgementState_AggregateComponentGrades` (`0x5D1110`) resolves
   component grades/timestamps into the descriptor result.
5. `GameplayJudgementState_GetResolvedGrade` (`0x5D2780`) returns the
   descriptor grade, or the selected component grade when a component selector
   is present.
6. `GameplayScoreState_ProcessJudgementFrame` (`0x5CF930`) consumes those
   resolved grades through the separate manager `+0x26C` object and updates
   score-state counters.

The grade values and score-state counter offsets are:

| Grade | Meaning | Score-state counter offset |
|---:|---|---:|
| `0` | `MISS` | `+120` |
| `1` | `GOOD` | `+124` |
| `2` | `COOL` | `+128` |
| `3` | `GREAT` | `+132` |

`GameplayJudgementState_PublishNoteResultMetadata` (`0x5D0820`) publishes
per-player note/effect metadata and its replace-or-increment flag. It does not
own the four score-state grade counters. Visual/audio result publication and
score accounting are distinct native responsibilities.

## Hidden chart notes versus post-descriptor free input

Chart note types `7` and `8` are explicitly named `HIDDEN` and `HIDDEN2` in
the native terminology tables. They are real chart descriptors routed through
the normal-tap wrapper `0x5D1FA0`, common tap judgement `0x5D1D50`, grade
resolution, and score accounting described above. These are the chart hidden
notes referred to as ad-lib notes in player-facing discussion.

`GameplayJudgementState_ProcessFreeInput` (`0x5D2040`) is a different path. It
runs after chart-descriptor processing within each recognition step, checks
active-chart conflicts, queries controls `4` and `9` from the same immutable
native frame history, and emits effect event type `4`. It is post-descriptor
free input/key-sound behavior, not a path limited to the end of the song; it is
not note type `7`, note type `8`, or a second chart-note judgement path.

Consequently, loader documentation and code must not use “ad-lib” as a synonym
for the `GameplayNoteType::None`/free-input sentinel. Hidden chart notes and
post-descriptor free input have different native ownership, judgement, grade, and
score consequences.

## Persisted IDA annotations

The following six function names and repeatable comments were proposed,
applied, saved, and read back through the existing IDA-CLI daemon:

- `0x5CF930` `GameplayScoreState_ProcessJudgementFrame`
- `0x5D2780` `GameplayJudgementState_GetResolvedGrade`
- `0x5D04F0` `GameplayJudgementState_ComputeDurationGrade`
- `0x5D0E00` `GameplayJudgementState_ComputeTimingGrade`
- `0x5D1110` `GameplayJudgementState_AggregateComponentGrades`
- `0x5D0820` `GameplayJudgementState_PublishNoteResultMetadata`

The already-persisted comment at `0x5D2040` was read back and already carried
the correct `HIDDEN`/`HIDDEN2` versus free-input distinction, so no additional
mutation was made there. The final saved IDB SHA-256 is
`C45D598BEDA2183D3DD79D58D83F444B448E7983CC800114D4C8FD36ECF78AFC`.

## Closure and remaining boundary

Native discovery for the physical-input -> frame/history -> descriptor ->
per-note judgement -> grade aggregation -> score/result pipeline is complete.
No unresolved native enum owner or score-publication caveat remains.

The remaining design problem is loader-owned: one QPC sample currently spans
the whole native recognition-step call, while loader descriptor routing and
post-descriptor free-input routing can both reconsider fields derived from that same
sample. That source-level ownership must be reconciled against the now-closed
native order. E-044 does not select or implement that correction and is not
runtime gameplay acceptance.

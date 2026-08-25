# E-046: Native descriptor normalization and progression closure

Date: 2026-08-17
Target: `H:\gc\game471.exe.i64`
Mode: static native-game evidence only; no loader implementation, build, or deployment

## Purpose

This record corrects the stale raw-type matrix in E-042 and closes the final
native ordering question: which descriptor type reaches judgement, which
candidate can observe one pressed frame, and when a later descriptor becomes
eligible during catch-up.

The native game has one gameplay lane and two physical booster components.
References to internal chart rows below describe storage/candidate structures,
not gameplay lanes or a live simultaneous-note mechanic.

## Provenance

- Handler lifecycle evidence:
  `H:\gc\runs\20260816T210335Z-a3aabe78\artifacts\native-handler-lifecycle-pass34.json`
  (SHA-256
  `97AB93012B86BA340770CDEA319CE64E00CB46E836BE67BC7476DB02EB8E7B6A`).
- Consecutive-candidate/catch-up evidence:
  `H:\gc\runs\20260816T210335Z-a3aabe78\artifacts\native-consecutive-candidate-catchup-pass40.json`
  (SHA-256
  `AE9E45F364CCEBB7DA17D4C2DEFC3979B9879B6F38D960C50EF8BF6E91BEA76D`).
- Raw/canonical/effective normalization evidence:
  `H:\gc\runs\20260816T210335Z-a3aabe78\artifacts\native-raw-canonical-effective-matrix-pass41.json`
  (SHA-256
  `5AD64F549E5DEAFF58A833CF6F18DBF65B67BBC2E6930C17B8E5D6D26C67C09A`).
- Focused progression/call-order evidence:
  `H:\gc\runs\20260816T210335Z-a3aabe78\artifacts\native-progression-cursor-closure-pass42.json`
  (SHA-256
  `8AD7B2B4C592AAB1A4797DD1FC62F98ACA73FA34E6B9E33170909676490FFA50`).
- Post-save annotation readback:
  `H:\gc\runs\20260816T210335Z-a3aabe78\artifacts\native-annotation-readback-pass43.json`
  (SHA-256
  `48AA2536D052881C20C902FEB05D4109D3185E50E88D935B4F3EDE5001A6F274`).
- Final saved IDB SHA-256:
  `3F911E373D18F4C3F11DACF5759AB7FF08847A4F365E8C0ED17B2896E7C47163`.

## Three note-type values

The descriptor does not retain one immutable type value from chart load through
judgement:

- raw type is the chart value before `0x5EB210` normalization;
- canonical type is stored at descriptor `+0x04` after alias normalization;
- effective type is descriptor `+0x00` after mode rewriting and duplicate
  suppression, and is the value used by candidate construction and dispatch.

Descriptor `+0x0C` is render/effect asset metadata. Alias and mode conversion
also rewrite it, but it is not the judgement-routing type.

`0x5EB210` first applies the raw aliases `B -> A`, `C/E -> 9`, and `D -> 4`,
then copies that normalized value into canonical `+0x04`. Mode conversion and
equal-time suppression modify effective `+0x00` only; canonical type remains
available to helpers whose selector flag is `1`.

## Complete raw/canonical/effective matrix

The mode columns show the base effective type before the separate equal-time
cross-row suppression pass.

| Raw | Canonical | Ordinary effective | Mode `2` | Mode `17` | Native judgement family |
|---|---|---|---|---|---|
| `0` | `0` | `0` | `0` | `0` | suppressed/none; skipped by candidate construction |
| `1` | `1` | `1` | `1` | `1` | tap |
| `2` | `2` | `2` | `1` | `2` | flick; tap in mode `2` |
| `3` | `3` | `3` | `3` | `3` | hold; long-form |
| `4` | `4` | `4` | `3` | `4` | scratch; long-form; hold in mode `2` |
| `5` | `5` | `5` | `3` | `5` | beat; long-form; hold in mode `2` |
| `6` | `6` | `6` | `6` | `6` | variant/component-offset tap |
| `7` | `7` | `7` | `7` | `7` | `HIDDEN` chart tap |
| `8` | `8` | `8` | `8` | `8` | `HIDDEN2` chart tap |
| `9` | `9` | `9` | `9` | `1` | critical/paired tap; tap in mode `17` |
| `A` | `A` | `A` | `3` | `A` | slide-hold; long-form; hold in mode `2` |
| `B` | `A` | `A` | `3` | `A` | raw slide-hold alias; same judgement as `A` |
| `C` | `9` | `9` | `9` | `1` | raw critical alias; same judgement as `9` |
| `D` | `4` | `4` | `3` | `4` | raw scratch alias; same judgement as `4` |
| `E` | `9` | `9` | `9` | `1` | raw critical alias; same judgement as `9` |
| `F` | `F` | `F` | `3` | `3` | dual-hold; long-form; hold in modes `2` and `17` |

The equal-time cross-row pass then applies these effective-only rules:

- in mode `17`, duplicate effective tap `1` or duplicate flick `2` suppresses
  the later descriptor to effective `0`;
- in other modes, duplicate effective tap `1` converts the earlier descriptor
  to effective critical `9` and suppresses the later descriptor to effective
  `0`.

`GameplayNoteDescriptor_IsSuppressed` (`0x5E8C70`) proves that effective type
`0` is skipped during candidate scanning. Therefore raw `B`, `C`, `D`, and `E`
do not fall through to a generic handler, and raw `0` does not undergo generic
note progression. The former E-042/F-073 wording was incorrect.

## Candidate construction and handler return

`GameplayJudgementState_BuildOrderedCandidateList` (`0x5D4E70`) is called at
`0x5D6A9D` once for each booster component and at `0x5D6C9E` with selector
`-1` for the generic descriptor/lifecycle pass. For each internal chart row it:

1. begins at the row's saved index;
2. skips effective type `0` and already-complete component/descriptor state;
3. inserts only the first incomplete descriptor from that row;
4. saves that descriptor index back to the selected cursor structure; and
5. orders and prunes the resulting cross-row candidates with the native
   time/overlap rules.

Component selectors `0` and `1` use cursor rows beginning at judgement-state
`+0x54`. Selector `-1` uses the generic cursor structure at `+0x3C`.

At `0x5D6BF4`, a dispatcher return of `1` ends the current component's
candidate pass. The handler evidence establishes these important cases:

- tap/flick-style handlers return success after recording an attempted result;
- a long-note start remains active and returns failure;
- successful non-MISS long-note finalization returns success; and
- `sub_5D0BE0` can mark an overdue component MISS, after which its enclosing
  handlers return failure and allow the pass to inspect a later candidate.

Thus a later candidate from another internal chart row can be examined in the
same component pass only when it was already present in the fixed candidate
list and every earlier candidate returned failure. Its pressed query is still
a pure lookup of the same native history frame. This engine behavior does not
imply that the shipped one-lane chart uses simultaneous notes.

## Consecutive notes in one row

A following descriptor in the same internal chart row cannot observe the same
pressed edge during the same recognition step:

- it was not included in the prebuilt component candidate list;
- both component passes finish before descriptor lifecycle/aggregation;
- the generic `-1` list is also built from one first-incomplete descriptor per
  row before lifecycle advances completion; and
- the saved row index is not advanced to the following descriptor until a
  later candidate build observes the current descriptor as complete.

`GameplayJudgementState_UpdateRowFreeInputEligibility` (`0x5D58D0`) is not a
cursor-advance helper. At `0x5D789E`, after guarded free-input processing at
`0x5D76E4`, it recomputes the per-row eligibility byte at judgement-state
`+0xCC`. `GameplayJudgementState_RefineRowFreeInputEligibility` (`0x5D2A00`)
refines that byte from fixed descriptor boundaries and long-form state. Neither
function moves the descriptor index.

## Catch-up edge uniqueness

`CTuneGameManager_ProcessJudgementFrame` (`0x6401E0`) invokes one complete
`0x5D68E0` recognition step for each pending native frame, with an integer
`recognition_ms`. A later catch-up step can rebuild the row list and expose the
next descriptor, but it cannot replay the prior pressed edge:

- `CBooster_WasControlPressedAtFrame` is a non-consuming history query;
- the pressed edge exists only in its actual captured history frame; and
- `0x659860`/`0x62D980` fill skipped frames with held masks and do not synthesize
  pressed edges.

So native catch-up has neither same-row edge reuse nor synthetic edge replay.
Cross-row reuse is possible only inside the one original recognition step and
under the native failure/ordering rules above.

## Free input and timing boundary

Post-descriptor free input remains separate from chart types `7` and `8`.
`GameplayJudgementState_ProcessFreeInput` (`0x5D2040`) uses the generic current
candidate set, descriptor `IsMute`, active-component conflicts, and the fixed
`FreeTapDisableTimeAfterMark`/`FreeTapDisableAfterMissMark` gates before it
queries controls `4` and `9`. It produces no chart grade or score result.

All judgement profiles and free-input timing settings other than
`GameTimeOffset` and `JudgTimeOffset` remain static design inputs. This audit
does not propose changing their values or replacing native candidate, conflict,
long-note interval, grade, or free-input policy.

## Loader-design consequence

The next source audit must reconcile the bridge with these native facts:

- route by effective native behavior while retaining canonical identity where
  a native helper distinguishes it;
- expose one stable physical sample throughout one recognition step;
- never consume an edge per descriptor and never synthesize it for a later
  catch-up frame;
- do not let a same-row following note see the earlier note's edge; and
- leave candidate order, handler success/failure, lifecycle, component
  conflicts, and static free-input gates under native control.

This record closes the native question. It does not establish that the current
loader satisfies the contract and does not constitute runtime gameplay
acceptance at 240 FPS or any other target rate.

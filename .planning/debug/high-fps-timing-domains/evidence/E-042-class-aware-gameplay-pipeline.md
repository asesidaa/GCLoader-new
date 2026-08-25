# E-042: Class-aware gameplay input and judgement pipeline

Date: 2026-08-17
Target: `H:\gc\game471.exe.i64`
Mode: static IDA/RTTI evidence only; no production patch or deployment change

## Provenance

- IDA database at the start of annotation: SHA-256
  `1DDF946D051BF3628FFAADC4A1928F7CE6879AD5B92CE160E1EF331828A0A8A2`.
- PyClassInformer parser `H:\PyClassInformer\pyclassinformer\msvc_rtti.py`:
  SHA-256 `1F54206708CB9EB6A079F3D37D04A3B8717E0D097B1079174D05E523D94C0FEE`.
- RTTI dump:
  `H:\gc\runs\20260815T182438Z-297470b1\artifacts\game471-pyclassinformer-rtti.json`
  (1,096 vtable records; SHA-256
  `930B31D55AB598CB6E2425810025FFD1A30655E80102C61A4FE25204941AB91`).
- Class/method index:
  `H:\gc\runs\20260815T182438Z-297470b1\artifacts\game471-pyclassinformer-class-method-index-2026-08-17.json`
  (801 classes, 1,096 vtables, 3,852 method EAs; SHA-256
  `C411345C5010D678294B0A57E5C60CA7230E0F16E1B239A3063BF21AB14E7F71`).
- Class-aware batch:
  `H:\gc\runs\20260815T182438Z-297470b1\artifacts\game471-class-aware-audit-batch-2026-08-17.json`
  (SHA-256 `6729F001D1D1F381F6BF939C0C68CF89712D7E91C4467EA69A4D0F1C50E8D2BE`).
- Note-type batch:
  `H:\gc\runs\20260815T182438Z-297470b1\artifacts\game471-note-type-status-batch-2026-08-17.json`
  (SHA-256 `2A7A720A205CD8FEAA79DB32FD19C57A5A6DC319BBE1C1A631E014FAE515524C`).
- Dispatcher/target batch:
  `H:\gc\runs\20260815T182438Z-297470b1\artifacts\game471-note-switch-target-batch-2026-08-17.json`
  (SHA-256 `B2A980C8714D9B473CB0A0207CD99FA53C400EDCC0705F71419718D58B74B708`).

## Pipeline

The physical path is `gw::GWInputDeviceXioFio_BOOST` vtable `0x6B7880`, slot 2,
method `0x4B4500` (iDmac/FIO snapshot translation), into
`gw::GWInputXio` vtable `0x6AE400`, slot 0, method `0x456360` (aggregate poll).
The gameplay input-frame entry at `0x659920` captures the snapshot and updates
the `CBooster` history (`0x62CFB0`). `CBooster` exposes pressed, held,
released, direction, and consecutive-held queries at `0x62DFB0`, `0x62DF50`,
`0x62DD30`, `0x62E480`, and `0x62DAA0`. The documented control IDs are ordinary
`0–9`, composites `10–14`, and paired IDs `15–19` with the native four-frame
lookback. This is the one-lane Switch model; no lane abstraction or unsupported
simultaneous-note rule is inferred.

`CGameMainTask` and the shared `CSeqTaskBase` lifecycle drive the gameplay task.
`CGameMainTask_Update_StateMachine` and the `CDemoPlayTask` slot-6 path reach
`CTuneGameManager_RunGameplayFrameStateMachine` at `0x6630B0`, which reaches
`CTuneGameManager_ProcessJudgementFrame` at `0x6401E0`. The latter computes the
current millisecond value and calls the judgement core at `0x5D68E0`.

The live instruction at `0x6401EF` loads `0x6FC0A0`, whose bytes are
`55 55 85 41`, the float `16.66666603088379` milliseconds. It is therefore a
60-FPS frame-to-millisecond conversion in this database. The old comment that
described `8.333333`/120 Hz was stale and is corrected in the IDA annotation
ledger. Judgement windows and static timing values remain millisecond-domain;
only `GameTimeOffset` and `JudgTimeOffset` are variable for the high-FPS design.

## Note normalization and complete matrix

E-046 corrects the initial raw-type interpretation formerly recorded here.
`0x5EB210` maintains three distinct values: the raw chart type, canonical type
at descriptor `+0x04`, and effective dispatch type at `+0x00`. The dispatcher
at `0x5D5720` reads the effective type. Mode rewriting and equal-time
suppression change effective type only.

| Raw | Canonical | Ordinary effective | Mode `2` | Mode `17` | Native judgement family/input |
|---|---|---|---|---|---|
| `0` | `0` | `0` | `0` | `0` | suppressed/none; candidate construction skips it |
| `1` | `1` | `1` | `1` | `1` | tap; pressed edge |
| `2` | `2` | `2` | `1` | `2` | flick; tap in mode `2` |
| `3` | `3` | `3` | `3` | `3` | hold; pressed start and held continuation |
| `4` | `4` | `4` | `3` | `4` | scratch; four directional pressed queries; hold in mode `2` |
| `5` | `5` | `5` | `3` | `5` | beat; pressed edge; hold in mode `2` |
| `6` | `6` | `6` | `6` | `6` | variant/component-offset tap |
| `7` | `7` | `7` | `7` | `7` | `HIDDEN` chart tap; pressed edge |
| `8` | `8` | `8` | `8` | `8` | `HIDDEN2` chart tap; pressed edge |
| `9` | `9` | `9` | `9` | `1` | critical/paired tap; tap in mode `17` |
| `A` | `A` | `A` | `3` | `A` | slide-hold; held age/direction; hold in mode `2` |
| `B` | `A` | `A` | `3` | `A` | raw slide-hold alias; same judgement as `A` |
| `C` | `9` | `9` | `9` | `1` | raw critical alias; same judgement as `9` |
| `D` | `4` | `4` | `3` | `4` | raw scratch alias; same judgement as `4` |
| `E` | `9` | `9` | `9` | `1` | raw critical alias; same judgement as `9` |
| `F` | `F` | `F` | `3` | `3` | dual-hold; hold in modes `2` and `17` |

Effective families route to normal tap (`1/7/8`), flick (`2`), hold (`3`),
scratch (`4`), beat (`5`), variant (`6`), critical tap (`9`), slide-hold
(`A`), or dual-hold (`F`). Effective `0` is skipped; raw `B`, `C`, `D`, and
`E` never fall through a generic judgement path. Equal-time suppression can
convert or suppress effective tap/flick descriptors while retaining canonical
identity for helpers that explicitly request it.

For long-form types `3/4/5/A/F`, start/end result selection remains separate
from in-between held/direction/interval validity. Chart types `7/8` are native
`HIDDEN/HIDDEN2` descriptors and are distinct from post-descriptor free input
at `0x5D2040`, which queries controls `4` and `9` under native conflict and
fixed timing gates and produces no chart grade or score result. The shipped
game remains one lane; internal chart rows and two booster components do not
establish a simultaneous-note mechanic.

## Result and publication boundary

`GameplayJudgementState_ComputeTimingGrade` (`0x5D0E00`) computes grades `0–3`
from absolute timing error, while
`GameplayJudgementState_ComputeDurationGrade` (`0x5D04F0`) computes the
accepted-duration grade for long-form mechanics.
`GameplayJudgementState_AggregateComponentGrades` (`0x5D1110`) resolves
component grades into the descriptor result, and
`GameplayJudgementState_GetResolvedGrade` (`0x5D2780`) returns the descriptor
or selected-component grade. The labels are `MISS`, `GOOD`, `COOL`, `GREAT`.

The per-player judgement object is stored at `CTuneGameManager+0x254`; the
separate non-polymorphic score object is stored at `+0x26C`.
`GameplayScoreState_ProcessJudgementFrame` (`0x5CF930`) consumes resolved
grades and updates counters at score-state offsets `+120`, `+124`, `+128`, and
`+132` for `MISS`, `GOOD`, `COOL`, and `GREAT` respectively.
`GameplayJudgementState_PublishNoteResultMetadata` (`0x5D0820`) owns separate
note/effect metadata, not those score counters. E-044 records the complete
ownership proof and removes the former score-publication caveat.

## Evidence boundary

This record proves static class ownership, call flow, input-query usage, and
timing-domain operands. It does not establish runtime gameplay acceptance at
240 FPS or any other target rate.

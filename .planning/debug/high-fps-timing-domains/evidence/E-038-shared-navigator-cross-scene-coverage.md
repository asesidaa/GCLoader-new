# E-038: shared navigator cross-scene coverage

## Scope and backend

This audit uses the running IDA daemon on `H:\gc\game471.exe.i64` (`idalib`)
to determine whether the bottom-right navigator belongs only to song selection
or is shared by other scenes. No runtime probe or production patch was added.

## Direct mode-selection proof

The task factory at `0x00651700` names `CSelectGameTask` and constructs it with
`sub_5B4080`. That constructor installs the primary
`CSelectGameTask::vftable` at `0x006FABE0`. Vtable slot 4 points to
`sub_5B3CF0`, which obtains the navigator singleton through `sub_5B6040` and
registers `sub_5B77F0` when the task is in its active state.

The equivalent song-selection factory at `0x00652D42` names
`CSelectMusicTask` and constructs it with `sub_5AA190`. Its primary vtable at
`0x006FAE2C` points slot 4 to `sub_5ACE80`, which registers the same
`sub_5B77F0` navigator callback under the same active-state condition.

Mode selection and song selection therefore do not own separate navigator
animation clocks. Both use the singleton at `dword_7F2524` and the same
callback/state-advance/draw pipeline.

## Complete callback-registration caller audit

`sub_5B77F0` has ten code callers. Their vtable ownership is:

| Task | Vtable method/caller |
|---|---:|
| `CSelectMusicTask` | `0x005ACE80` |
| `CSelectGameTask` (mode selection) | `0x005B3CF0` |
| `CMatchingTask` | `0x005B8270` |
| `CDifficultyTask` | `0x005BD3D0` |
| `CCustomTask` | `0x005C3EE0` |
| `CUnlockRewardTask` | `0x005F76E0` |
| `CRewardTask` | `0x00601140` |
| `CResultTask` | `0x00605800` |
| `CResultLocalTask` | `0x006069D0` |
| `CResultEventSoloTask` | `0x0060BF30` |

These task methods all register the common routine `sub_5B77F0`; most do so
when their inherited active-state field equals 3. `sub_5B77F0` registers
`sub_5B77B0` at draw priority 1200. That callback always obtains the same
global renderer, advances it through `sub_5B6310`, and draws it through
`sub_5B6C30`.

## Correction scope

The cadence defect and its correction are global to this navigator subsystem,
not song-select-specific. Hooking scene methods or individual assets would
create inconsistent behavior between menus. Gating the sole manual state
advance `sub_5B6310` to the existing authored-60-Hz tick covers every mapped
navigator-bearing task while leaving the shared draw callback native-rate.

This cross-scene reuse is expected and desirable: all mapped scenes inherited
the original 60-Hz frame-count semantics, so they should all receive the same
authored-cadence correction.

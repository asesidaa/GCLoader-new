# E-026: start2 card-confirm clock ownership

Date: 2026-07-20

IDA backend: existing daemon for `H:\gc\game471.exe.i64`, `idalib`

## Asset ownership

- `start2_xfl\DOMDocument.xml` places `UNIQUE_213` at root as
  `imc_card_01` and `UNIQUE_166` as `imc_card_02`.
- `UNIQUE_213` contains the no-card/confirmation states:
  `jf_card_01_timeout` at frame 43, `tg_card_01_timeout` at frame 52,
  `jf_card_01_05` at frame 53, and `tg_card_01_05` at frame 68.
- The XFL contains visual states and ActionScript label jumps, but no timer
  variable. The `frameRate="30"` value is emitted unconditionally by
  `mtx_rvb_to_xfl.py`; it is not recovered RVB metadata.

## Native timer ownership

- `sub_5A35B0` returns the shared `CStartTask` countdown object at
  `0x0082FECC`.
- `sub_5A36D0` initializes its remaining and reset values directly from a
  floating-point seconds argument. Card/start paths use values including
  30.9 seconds.
- `sub_5A37D0` obtains the global frame timer and passes its delta seconds to
  `sub_5A35E0`.
- `sub_5A35E0` subtracts that delta, clamps at zero, and updates the visible
  countdown widget through the object returned by `sub_5F4BD0`.
- `sub_5A5E80` performs that decrement, checks timeout, and reads pressed
  edges 5/10 and 6/11. Logical countdown and input acceptance therefore share
  this callback's service cadence.

## IFBL ownership

- Static initializer `sub_699B60` installs `sub_5A5E80` in the card-flow
  descriptor entry at `0x00811F00` as IFBL opcode 1.
- In `0x006304B0`, opcode 1 invokes the callback. A false result writes retry
  counter 1; the next interpreter update decrements it before testing, so the
  same callback is eligible again immediately.
- That retry path does not execute IFBL opcode `0x11` at `0x006309D4` and is
  therefore not affected by the loader's integer-wait scaling hook.

## Remaining runtime question

The static graph proves the countdown is absolute-time based and the visible
timer is updated by the same callback that samples input. It does not prove
the callback's observed 240 FPS call rate or the delta value it consumes.
The diagnostic hook at RVA `0x001A5E80` records callback count, cumulative
delta, last delta, and remaining timer without changing registers or control
flow.

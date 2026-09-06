# GC 2.06 selection and result countdown correction

Date: 2026-09-06. Native target: `H:\gc2_game\game_decrypted.exe`, examined
through its existing IDA database with image base `0x00400000`.

## Scope and cause

The operator reported that song selection still counted down with timer freeze
enabled. The existing profile selected the right callback, RVA `0x18F630`, but
the wrong countdown inside it. Its first pair at `0x18F692` / `0x18F6B1`
decrements `flt_7AA63C`, an input-repeat delay. The visible selection timer is
`flt_7AA650`, decremented by the next pair at `0x18F6F6` / `0x18F715`.
The earlier port's `select_music_primary` mapping and IDB comments on the first
pair are incorrect; function correspondence did not establish field ownership.

This correction is limited to song selection, course selection and result
screens. A newly frozen countdown must have a native player-confirm path that
does not depend on the timeout. No new freeze sites are added for card/name
entry, event regulations, unlock screens, game-over waits, or other timed
transitions. Existing profile entries outside this correction are preserved.

## Native state and manual progression

| Scene | Countdown callback RVA | Timer state (preferred VA) | Independent player-confirm path |
| --- | --- | --- | --- |
| Song selection | `0x18F630` | `0x7AA650`, initialized to 90.9 by `0x18BBD0` | While idle, calls `0x18F1D0`; native input query `0x225A10(14, -1)` returns success, setting the decision flag. Confirmation closes the timer UI, commits the selected music/difficulty, and returns 1. |
| Event course selection | `0x198850` | `0x7AA560` | Queries input 14 after the timer block and sets the same completion flag used by timeout. Plays `SE_KETTEI`, closes the timer UI and returns 1. Left/right course navigation remains in the non-confirmed path. |
| Event score results | `0x1E2050` | `0x79408C`, initialized to 15.9 by `0x1E09A0` | Queries input 14 after the timer block. Confirmation closes the timer UI and returns 1 independently of timer expiry. |

Song selection has a third delta pair at `0x18F832` / `0x18F854` for the
selection-movement timer `flt_7AA640`. Both that timer and the input-repeat
delay remain native. The input-repeat initializer at `0x18F1D0` sets a 0.35
second delay; freezing that field was unrelated to the visible countdown.

Course registration `0x2565A0` places callback `0x198850` between
`jf_course_start` / `tg_course_start` and the decision/end sequence. Its task
type is `CEventCourseTask`. Event-score-result registration `0x24F1C0` places
callback `0x1E2050` between `jf_res_open` / `tg_res_fullopen` and
`jf_res_close`. `CResultEventScoreTask` update `0x1E1EE0` uses the enclosing
registration at `0x24F580`, which invokes that result sequence. Native vtable
and RTTI evidence identifies this as an event-score result screen.

The existing result callbacks at `0x1DCF30` (play result), `0x1DEC70` (final
ranking), `0x1E0820` (event-result tabs), and `0x1DAF30` / `0x1DB0B0` (total
reward results) were also inspected: each queries input 14 independently of
timer expiry before returning completion. Their existing freeze pairs remain
unchanged.

## Checked patch sites

All addresses below are RVAs. Each five-byte call targets `0x203F80`.
That helper loads `[this + 0x18]` into x87 ST(0) and returns. The existing
replacement `D9 EE 90 90 90` supplies zero in ST(0) at the selected call only;
the timer comparison, visible-number update and manual-input handling remain
in place. Both comparison and non-clamped subtraction calls must be replaced.

| Scene | Call RVA | Original bytes from IDA and its input executable |
| --- | --- | --- |
| Song selection | `0x18F6F6` | `E8 85 48 07 00` |
| Song selection | `0x18F715` | `E8 66 48 07 00` |
| Course selection | `0x198874` | `E8 07 B7 06 00` |
| Course selection | `0x198893` | `E8 E8 B6 06 00` |
| Event score results | `0x1E2074` | `E8 07 1F 02 00` |
| Event score results | `0x1E2093` | `E8 E8 1E 02 00` |

`CountdownProfile.cpp` replaces the two incorrect song-selection entries and
adds the two course and two event-score-result entries. The 2.06 profile now
contains 30 calls in 15 pairs. The existing versioned preflight and installation
path guard these entries; the 4.71 profile and shared delta helper are unchanged.

## Evidence and acceptance

Read-only IDA queries and native outputs are in `.codex-tmp/countdown206/`:
`inspect_native.py`, `focused_native_contract.json`, `registrations.json`,
`lifecycle.json`, `result_scene_owner.json`, and `inspect_screen_types.py/.json`.
The query client requires an IDA backend and disconnects after each batch.
The six changed call sites match the original bytes in both IDA and the actual
input executable, using IDA's file-offset mapping. No IDB edits were made.

Complete `cmake --build --preset msvc32-debug` and
`cmake --build --preset msvc32-release` builds passed, including linkage of
`dist/iDmacDrv32.dll` in each build directory. Logs are
`.codex-tmp/countdown206/build-debug.log` and `build-release.log`. CLion reports
no errors; its remaining warning suggests making the unchanged profile array
`constexpr`. No unit/synthetic native tests are added or run, following
repository policy. No agent-run deployment or target-process session was
performed.

On 2026-09-07, the operator confirmed that the countdown correction works and
authorized committing it. Runtime acceptance is based on that operator report,
separate from the compilation and static control-flow evidence above.

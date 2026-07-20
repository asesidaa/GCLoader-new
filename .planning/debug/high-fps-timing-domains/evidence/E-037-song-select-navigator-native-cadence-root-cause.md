# E-037: shared navigator native cadence root cause

## Scope and backend

This trace uses the running IDA daemon on
`H:\gc\game471.exe.i64` (`idalib`) and follows the bottom-right navigator,
initially reported in song selection, from resource loading through state
update and rendering. E-038 proves that the same global subsystem is also
registered by mode selection and several other navigator-bearing tasks. No
runtime probe or production patch was added.

## Actual resource and renderer

`sub_5B7F00` creates a per-navigator resource entry when the requested
navigator ID is not already present. Its callback object reaches
`sub_5C88D0`, which formats and loads:

- `data/2d_boost/navigator/%03d_%s/base.dds`
- `data/2d_boost/navigator/%03d_%s/face.dds` when record byte `+0x0D` is set

The entry stores the navigator ID at `+0x4C`. `sub_5B6B60` selects that entry
into the global renderer state at `dword_7F2524` (`+0x3C`, and optionally
`+0x40`).

The draw routine `sub_5B6C30` finds the selected entry, draws its base texture
at coordinates derived from screen width/height minus texture width/height,
and overlays a selected 180x180 cell from the face texture. This bottom/right
placement and the navigator-ID-backed `base.dds`/`face.dds` lookup identify it
as the visual reported by the operator.

This rejects both earlier candidates:

- `selectmusic2.rvb`'s child `imc_navi` is only the small song-select prompt.
- `navi_<id>_<name>.rvb/.mtx` is a standalone MovieClip asset family, but it is
  not the native render/update path responsible for this bottom-right visual.

This is not owned by song selection. E-038 maps `CSelectGameTask`,
`CSelectMusicTask`, and eight additional tasks to the same callback registration
function and global renderer.

## Render callback and update order

`sub_5B77F0` registers callback `sub_5B77B0` at draw priority 1200. Whenever
the global navigator renderer is visible, that callback performs this order:

1. `sub_5B6310(dword_7F2524)` -- advance manual animation state;
2. `sub_5B61C0()` -- save/set render state;
3. `sub_5B6C30(dword_7F2524)` -- draw base and selected face cells;
4. `sub_5B6100()` -- restore render state.

`sub_5B6310` has exactly one code caller, `sub_5B77B0`. It is therefore a
narrow navigator-animation state sink, not a shared MovieClip or task update.

## Frame-counted state in `sub_5B6310`

All progression below happens once per invocation, with no elapsed-time read:

- State `+0x60` selects a ten-step transition. Counter `+0x64` increments by
  one until 10 and directly drives scale, position, and alpha interpolation.
- Counter `+0x70` decrements by one. On expiry, flag `+0x74` toggles and the
  counter is reset to 2 for one phase, 30 for a short alternate phase, or a
  randomized 180..307 for the long interval.
- Counter `+0x7C` decrements by one. On expiry, flag `+0x80` toggles and the
  counter is reset to 5, 11, or 17 according to state `+0x84`.

`sub_5B6C30` consumes state `+0x68` and flags `+0x6C`, `+0x74`, and `+0x80` to
choose face-texture cells. Initial/reset paths `sub_5B6080`, `sub_5B6B60`, and
`sub_5B78A0` initialize the same counter/flag group.

## Exact high-FPS failure

Because `sub_5B6310` is called by a render callback, it advances once per
presented frame. At the original 60 FPS, N counts last `N / 60` seconds; at
240 FPS, the same counts last `N / 240` seconds. Representative durations are:

| Count | 60 FPS | 240 FPS |
|---:|---:|---:|
| 10 | 166.7 ms | 41.7 ms |
| 30 | 500 ms | 125 ms |
| 180..307 | 3.0..5.12 s | 0.75..1.28 s |
| 5 / 11 / 17 | 83.3 / 183.3 / 283.3 ms | 20.8 / 45.8 / 70.8 ms |

This predicts the operator's 4x-fast animation exactly. The existing
`0x004DF940` MovieClip gate cannot affect it because this renderer does not
advance a MovieClip.

## Narrow correction design

Keep callback `sub_5B77B0`, render-state setup/restoration, and
`sub_5B6C30` drawing at native presentation cadence. Gate only
`sub_5B6310` to the loader's existing authored-60-Hz tick:

- authored tick: execute the original `sub_5B6310` once;
- non-authored tick: return the unchanged renderer object;
- 60 FPS/patch disabled: preserve original one-update-per-render behavior.

This preserves every original discrete count and randomized interval without
scaling constants individually, while keeping the navigator rendered on every
high-FPS frame. Because all navigator-bearing tasks converge on this one state
advance, it corrects the navigator consistently across mode selection, song
selection, and the other mapped scenes. It is narrower than gating the entire
callback and independent of the shared MovieClip hook.

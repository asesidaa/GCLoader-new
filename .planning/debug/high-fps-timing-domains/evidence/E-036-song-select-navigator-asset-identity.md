# E-036: standalone navigator RVB candidate audit (rejected target)

## Scope

This evidence records the initially plausible standalone navigator RVB family
and its timeline structure. E-037 supersedes the target mapping: the
bottom-right song-select character reported by the operator is rendered from
per-navigator `base.dds`/`face.dds` sprite sheets by a manual native state
machine. It is neither this standalone RVB path nor the small `imc_navi`
prompt embedded in `selectmusic2.rvb`.

## Runtime asset family

Standalone character-animation assets exist under:

`H:\gc\data\2d_boost\navigator\navi_<id>_<name>.rvb/.mtx`

The first entry inspected is:

- `navi_001_yume.rvb` (22,270 bytes)
- `navi_001_yume.mtx` (2,268,480 bytes)

This family is separate from
`H:\gc\data\2d_boost\selectmusic2.rvb`. The latter contains a small UI prompt
named `imc_navi`. E-037 proves that neither timeline is the reported
bottom-right sprite-renderer cadence defect.

## Decoded timeline

The existing converter was run against `navi_001_yume` and produced:

`H:\gc\artifacts\2d_boost\navi_001_yume_xfl`

The conversion reported 9 MovieClips, 14 shapes, and 14 textures. The root
document declares `frameRate="30"` and contains the following control labels:

- frame 0: `jf_ini` and `stop()`
- frame 1: `jf_ope_start`
- frame 11: `tg_ope_start` and `stop()`
- frames 12/13: fade-in control markers
- frames 14/15: fade-out control markers
- frame 16: `jf_ope_end`
- frame 25: `tg_ope_end` and `stop()`

The visible character also contains explicit nested loop timelines:

- `UNIQUE_9`, `UNIQUE_10`, `UNIQUE_17`, `UNIQUE_20`, and `UNIQUE_21` use
  `lf_loop` at frame 0, execute `gotoAndPlay("lf_loop")` after six advancing
  frames, and expose `lf_stay` at frame 7.
- `UNIQUE_26` is a 12-frame `lf_loop` timeline whose last frame loops to its
  start.
- `UNIQUE_23` label `jf_ope_mouth_talk` sends five nested mouth clips to
  `lf_loop`; `jf_ope_mouth_stay` sends the same clips to `lf_stay`.

These are authored, frame-counted MovieClip loops. At 240 native updates per
second, advancing them on every update would produce a 4x speed relative to a
60-update baseline, which made this a valid candidate. The native trace in
E-037 rejects that candidate for the reported bottom-right visual.

## Resolution

The existing loader hook at `0x004DF940` gates the shared one-frame MovieClip
advance primitive. E-037 identifies a completely separate path: callback
`0x005B77B0` calls the manual counter update at `0x005B6310` and then draws the
navigator's `base.dds`/`face.dds` layers at the screen edge in `0x005B6C30`.
Consequently this RVB audit remains useful asset context but is not evidence
for the active defect or its fix.

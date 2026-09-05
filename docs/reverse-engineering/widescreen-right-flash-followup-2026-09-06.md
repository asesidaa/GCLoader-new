# Right-side flashing after judgment text correction

The operator confirms GREAT text is correctly placed with Release DLL
CD4AF32C591B4BEC401035221B0BFBE7F77DBABEA61334D29CC5853EDC8A02A8, but reports
an unidentified flashing effect on the right. The native track-grade effect
selection was an unnecessary placement override. After its removal, the operator
confirmed the flashing effect was gone and authorized diagnostic cleanup and
closeout on 2026-09-06.

## Latest log

The 03:33:33 launch's deployed DLL matched that artifact. The 5,181,966-byte,
6,078-line log is preserved in .codex-tmp/widescreen-flash-20260906/loader-log.txt;
the streaming analysis is in summary.json beside it. Counts below are sampled
submissions across the two observed stage intervals, not effect activation counts.

| Exact owner | Samples | Submission viewport | Projection/texture |
| --- | ---: | --- | --- |
| Text slots 18/30 | 50 | 1556,0,720,1280 | Orthographic; 0x7D7DAC |
| Track-effect slot 93 | 2 | 1556,0,720,1280 | Perspective; 0x7D7E18 |
| Track-effect slot 96 | 235 | 1556,0,720,1280 | Perspective; 0x7D7E18 |
| Track-effect slot 97 | 19 | 1556,0,720,1280 | Perspective; 0x7D7E18 |
| Tutorial slots B5/B6/BB hex | 6 | 1556,0,720,1280 | Orthographic; 0x7D7DD0 |
| Perfect announcement, slot 5 | 2 | 778,0,720,1280 | Orthographic; centered |

All 829 sampled allocation/submission/restoration triples are complete, and all
829 restore the allocation viewport. The final totals report zero dropped
samples, read failures, packet collisions, unowned allocations, stale pairs,
or root-depth overflows. These observations point to wrong owner selection;
they do not establish a viewport restoration failure.

## Native interpretation

Fresh IDA-CLI analysis of the supported game471 input confirms:

- 6463F0 positions slots 93..97 at the player/track world position, applies
  scale 0.03 and an angle read from judgment state. 5D0780 chooses that angle
  using rand() % 360. These are distinct from the fixed HUD text in 648D40.
- 5C8A80 loads efcdata.dat for bank 0 and effect.dat for bank 1. 5F03B0 uses
  the big-endian definition count and offset table; 5F1F70 resolves the sprite
  part and its authored animation curves.
- The actual data/effect/game/effect.dat definition 32, used by slot 96,
  is one sprite lasting 12 authored frames (about 200 ms). Its scale curve
  expands from 1 to 2.25 to 2.75, and its opacity fades out. Combined with the
  randomized angle and misplaced viewport, this is consistent with a short
  rotating/expanding flash appearing on the right after judgments.

The log does not contain rendered pixels or the operator's exact flash times.
The subsequent operator run confirms that removing this owner selection resolves
the reported flash; the precise animation interpretation above is based on the
native code and authored asset.
The placement policy nevertheless requires these world effects to stay on the
track. Retaining them in the HUD selection was an error in the earlier design.

## Correction and acceptance

Removed slots 93..97 from production placement matching. The positive selection
now consists of primary fixed-position text slots 12,15,18,24,27,30 decimal and
the established tutorial slots. Bar/counter draw boundaries are unchanged.
There are no new or changed native hook spans.

The operator confirms the flash is gone with the correction. At closeout,
the deployed DLL hash matches the correction artifact below. Temporary tracing
and its eight diagnostic-only hooks were then removed; the placement policy and
its permanent draw hooks remain. See the
[closed diagnostic record](widescreen-placement-diagnostics.md) for cleanup
verification and the rebuilt artifact.

Both full Debug and Release preset builds passed without compiler/linker
warnings or errors. CLion completed inspections of all four changed source
files without errors. git diff --check passed. No synthetic tests were added.

Release artifact: build-msvc32-release/dist/iDmacDrv32.dll.
SHA-256: 558CF8CB0071013F55017CA437AFE0423C95DEB6978C397FD09CCA2C4799E48C.
Matching symbols: build-msvc32-release/src/iDmacDrv32.pdb.
This artifact was deployed for the operator's confirming run. Cleanup produces
a separate build without tracing; no deployment occurred during cleanup.

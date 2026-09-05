# Widescreen diagnostic song run, 2026-09-06

Status: runtime evidence analyzed; no placement correction implemented during
this analysis. The run confirms overly broad placement and a deferred judgment
viewport mismatch. Late-effect coverage is incomplete because the diagnostic
sampler filled.

## Run identity and retained evidence

- Original: H:/gc/loader-log.txt, 16,046,216 bytes, 18,944 physical lines.
- Preserved copy: .codex-tmp/widescreen-runtime-20260906/loader-log.txt.
  All log line references below apply to that copy and the inspected original.
- Startup 02:22:04; gameplay trace 02:23:14 through 02:25:38,
  one diagnostic stage, native song time 0 through 143,629 ms.
- Output 2276 x 1280; configured HUD placement right. Centered native viewport
  would be (778,0,720,1280); the right viewport is (1556,0,720,1280).
- Installed hooks: 47. Temporary tracing enabled automatically.
- Deployed and Release-build DLL SHA-256 both:
  823089937F200F714E63F89031614C07017C300E49C88A5299A96FC6CE246967.
- Auto-play and absolute-judgment diagnostics were also enabled.
- The trace does not identify the song/difficulty or the operator's observed
  visualizer failure time. Do not infer those from the stage sequence number.

Streaming extraction is in
.codex-tmp/widescreen-runtime-20260906/analyze_log.py.
summary.json, samples.csv, and packet_pairs.json preserve parsed evidence;
focus_log.py prints bounded aggregates. The parser reads one source line at
a time. The retained log is the authoritative source.

## Confirmed placement observations

| Exact element or seam | Evidence | Observation |
| --- | --- | --- |
| Orthographic effects entry | 144 samples, lines 99 through 18839 | Every sampled entry already has the right viewport. Bar placement affects the surrounding effects pass. |
| Stage-start image, helper case 0 | Line 111, song time 124 ms, native first/current frame 30 | Right viewport after texture and time gates. |
| Large stage-start song-title helper, case 1 | Line 112, 249 ms, native first/current frame 60 | Right viewport after gates. |
| Stage-start player-display helper cases 2 and 6 | Lines 116–117, 499 ms, native first/current frame 120 | Right viewport after gates. Only these player-display cases were observed; do not claim all 13 cases executed. |
| CHAIN label | 137 samples, lines 405 through 18854 | Right viewport. |
| Ordinary chain digits | 137 samples, lines 406 through 18855 | Right viewport; final sampled count 3068, draw position (470,467), scale (1,1). |
| Numeric glow | 331 samples across distinct loop variants 0/1/2 | Right viewport. Separate calls from ordinary digits and the rounded-hundred number. |
| Enlarged rounded-hundred number | 17 samples for 100 through 1700; first line 1034 | Right viewport, draw position (470,467). At 100 the countdown is 30. |
| Expanding hundred-chain rectangular lines | 17 samples for 100 through 1700; first line 1035 | Right viewport; first line vertex is native (360,640,0). At 100, time is 14,583 ms and countdown is 30. A native centered point maps to physical x=1916 instead of x=1138 under the recorded orthographic mapping. |
| CTune slot 0, group 5, bank 0, native template ID 6 | Submission line 497, 8583 ms | Root position (360,640,0), scale (1.35,1.35,1), right viewport, group-6 scope inactive. |
| CTune slot 1, group 5, bank 0, native template ID 7 | Submission line 1032, 14,583 ms | Same centered root position/scale and right viewport, group-6 scope inactive. |
| CTune slots 18 and 30 decimal, group 6, bank 0 | Submission lines 370 and 409; 28 samples per slot | Right viewport with group-6 scope active. Both use definition pointer 0x1bec7846 and position (720,380,0). Separate from the note-tutorial slot set. |
| Selected note tutorials | Slot 181 decimal/B5 hex: line 2017; slot 187 decimal/BB hex: line 4179 and three later samples | Right viewport with group-6 scope active. This run does not establish every tutorial variant. |

The group-5 observations directly demonstrate collateral placement outside the
counter and group-6 brackets. They do not identify which effect the operator
described as a broken song visualizer.

Because configured bar placement and the group-6 override are both right in
this run, their viewport values alone cannot distinguish which setting caused
a given group-6 sprite to move. Native control flow establishes that both scopes
are broad; exact owner selection is required.

## Judgment ownership does not survive deferred submission

All 5,553 sampled allocation records have exactly one corresponding submission
record with the same stage/pair. For 197 pairs, the only compared changes are
judgment-scope flag, viewport, and scissor rectangle:

| Field | At packet allocation | At private-queue submission call |
| --- | --- | --- |
| Judgment scope | 1 | 0 |
| Viewport | (1556,0,720,1280) | (0,0,2276,1280) |
| Scissor rectangle | (1556,0,2276,1280) | (0,0,2276,1280) |

Affected sampled owners are CTune slot 93 (1 pair), slot 96 (170 pairs), and
slot 97 (26 pairs), all group 3 / bank 1. A non-startup example is allocation
line 366 and submission line 367 at 6866 ms, slot 96. Texture, root, manager,
frame, render target, projection, and source/stored packet world matrix match.
Scissor testing is disabled in this example, so the scissor-rectangle change
must not itself be described as observed clipping.

Live IDA-CLI rechecked the same game471.exe.i64 input, SHA-256
FEAD3BD4D0E0985F101965EDC417DD2B96522F8716FF789D84618FEB0D7A2522.
The private drain calls 5F0600(packet) at 5F10C4; this helper checks the texture,
applies the packet world matrix, and calls 57AC60. The general 5C9B10 batch flush
does not drain this queue. Root-visit placement therefore does not enclose the
deferred submission.

These are native pre-call device-state observations, not exhaustive GPU draw
capture. They do not establish every judgment sprite's final visual position.

The correction needs to carry selected root ownership onto its queued packets
and apply placement at submission, restoring the enclosing state afterward.
Do not drain the private queue early per root: that would change native texture
grouping/order.

## Diagnostic coverage failure

Every retained sample has native_ok=1, slot_lookup_ok=1, device_ok=127,
and batches_ok=1. The final totals at line 18938 report:

- 1,462,452 root visits;
- 1,470,925 allocations and 1,470,925 submission-call observations;
- 18,604 emitted samples;
- no read failures, packet collisions, or unowned allocations;
- no pending records;
- **448,930 dropped diagnostic attempts**.

The sampler stores keys by point/root pointer/detail/variant, without eviction
during a stage. Exactly 1024 distinct sampler keys were emitted: 971 root keys
and 53 other keys. The last new key is at line 16512, song time 92,500 ms.
The 93,004 ms totals at line 16547 already show 347 drops.

Existing keys continue sampling to the end, but new keys can no longer be
inserted. This is not a trace ending early and is not a count of dropped game
draws. The log does not separate every possible drop source, but full-table
rejection is directly established by the observed key count and implementation.

Consequences:

- No stage-result slots 2–6 were captured.
- No staff-sprite records were captured.
- Hundred-number/rectangle records stop at 1700 even though ordinary digits
  later show 3068: later hundreds require new keys.
- Newly appearing visualizer/root identities after saturation can be absent.

Absence of these records does not show that the elements did not execute or
that they were centered. Static evidence still places result roots inside
group 6 and staff drawing inside the surrounding orthographic effects pass,
but this run adds no direct late-element record for them.

Before another capture, reserve sampling capacity for named seams and exact
selected/result CTune owners independently of incidental root pointers.
Bound and recycle the unclassified-root sampler; do not merely enlarge the
same permanently accumulating table. Separate key-capacity drops from event
buffer drops. Keep temporary diagnostics automatic and remove them afterward.

## Consequences for the finer patch

1. Restore a centered baseline for ordinary orthographic stage effects.
   Select actual score/gauge/compact-name draws for configured bar placement;
   the entire 5E3EC0 routine is not a bar-only owner.
2. Select CHAIN label, ordinary digits, numeric glow, and rounded-hundred number
   explicitly according to their intended counter placement. Procedural
   rectangular lines must not fall inside that scope.
3. Replace group-wide tutorial placement with exact desired tutorial roots.
   Result roots and unselected group members inherit their ordinary placement.
4. Preserve selected judgment/tutorial ownership across packet creation and
   submission. Restore enclosing device state, maintaining native order.
5. Treat visualizer identity and finish/staff runtime confirmation as open.
   This run establishes the scope and queue problems above; it does not prove
   all reported symptoms have been isolated or fixed.

The IDA request is
.codex-tmp/widescreen-runtime-20260906/native_run_owners.py; its output is
.codex-tmp/widescreen-diagnostics-20260906/native_run_owners.json.
It rechecks effects-pass ownership, effect initialization, main-HUD structure,
packet submission, introduction, and manager traversal.

No production source, configuration, or deployed artifact was changed during
this log analysis.

# Widescreen placement diagnostics

Status: closed on 2026-09-06. The operator confirms GREAT text placement and
that the right-side flash is gone. Temporary tracing and its eight diagnostic-only
hooks have been removed after that acceptance. See the
[right-side flash follow-up](widescreen-right-flash-followup-2026-09-06.md) and the
[judgment text follow-up](widescreen-judgment-text-followup-2026-09-06.md).
The [implementation plan](../superpowers/plans/2026-09-06-widescreen-exact-draw-placement.md)
contains the selected draw sites and guarded bytes. The
[first diagnostic run](widescreen-placement-diagnostic-run-2026-09-06.md) records
the original faults and the old sampler's 92.5-second capacity failure.

## Implemented placement boundaries

The ordinary orthographic effects viewport is centered independently of the
configured top bar. Selected draws save the actual viewport/scissor and the
depth-enable, depth-write, and stencil states changed by placement. They restore
those values at their native post-call boundary, without undoing the helper's
normal texture, blend, or matrix changes.

Twenty explicit bar calls cover difficulty icons, the stage-count panel/digits,
gauge and backing, score caption/digits, alternate-mode panels/counters, the
mode/player/status panels, and the compact top names. The combined 5E3EC0
function is never bracketed as a whole. Panel positions were rechecked in
5E4C60; compact names are at y=38..57 in 5E1FA0.

| Element | Selected owner | Placement |
| --- | --- | --- |
| Top bar | Twenty explicit call pairs in the implementation plan | Configured left/center/right |
| CHAIN label | 5E4503 to 5E4508 | Entry 0 right, entry 1 left, other entries center |
| Ordinary digits | 5E4550 to 5E4555 | Same entry placement |
| Numeric glow | 5E4609 to 5E4611 | Same entry placement |
| Rounded-hundred number | 5E4762 to 5E4767 | Same entry placement |
| Expanding rectangular lines | 5E4ADD, 5E4B05, 5E4B24; no override | Centered enclosing viewport |
| Primary fixed-position judgment text | Exact CTune slots 12,15,18,24,27,30 decimal | Right at actual submission |
| Player 1 track-position grade effects | Slots 93–97 decimal; no override | Native 3D viewport and world placement |
| Note tutorials | Exact slots B2,B3,B4,B5,B6,BA,BB,C0 hex, plus B9 direction companion | Right at actual submission |
| Introduction, staff roll, result slots 2–6 | No override | Centered enclosing viewport |
| Other stage effects | No override | Ordinary native render-space placement |

For managed effects, the generic root visit establishes ownership without
changing the viewport. Selected root ownership is associated with packet
addresses at allocation. Every observed allocation invalidates an old address,
including unselected allocations; submission consumes the association.
The loader does not write native packet fields or reorder texture buckets.
Unmanaged selected roots retain a direct-draw scope.

The private drain at 5F10C4 calls 5F0600, which applies the packet world matrix
and draws through 57AC60. Its post-call hook at 5F10C9 restores the enclosing
state. The group-6 traversal has no placement override or diagnostic hooks.

A necessary four-batch flush is separate from that private queue. IDA showed
that 5C8FA0 changes vertex streams, declarations, and shaders. The selected-draw
path skips empty general batches; when batches are pending, it preserves the
pipeline around their flush before continuing the native selected draw.
It never forces an early private-queue drain.

## Runtime acceptance and cleanup

The operator reported the placement corrections were mostly in place, then
confirmed GREAT text was correctly positioned, and finally confirmed the
right-side flash was gone. The deployed DLL checked at closeout has SHA-256
558CF8CB0071013F55017CA437AFE0423C95DEB6978C397FD09CCA2C4799E48C,
matching the flash correction build.

The accepted draw policy is retained. Cleanup removes per-draw capture,
sampling buffers, device snapshots, frame-end emission, and temporary startup
messages. Normal startup and failure reporting remain. The enabled profile
now installs 83 hooks: 18 inline, 63 mid, and two global vtable slots. Its
preflight covers 86 byte contracts and nine pointer contracts, 95 operations
in total.

The rebuilt DLL without temporary tracing is at
build-msvc32-release/dist/iDmacDrv32.dll, with matching symbols at
build-msvc32-release/src/iDmacDrv32.pdb. SHA-256:
7CB548E3173BD77BA6E55FB9BC2F478FA958C87A0CA1067567E4E7DB8B24F1D9.
It has not been deployed or run as part of cleanup. No further diagnostic run
is requested for this closeout.

## Historical sampling and fields

The following describes the removed tracer for interpreting preserved logs.
It is not an active feature or a launch procedure.

Named draws use their own 256-key pool; CTune effect slots have 512 separately
indexed keys; incidental roots use a recyclable 128-key pool. Noncritical roots
are limited to four samples per second independently of the named/slot pools.
Known result slots, judgment slots, and group-5/group-6 effects retain priority.
Hundred-number and rectangle keys reuse their slot while comparing the hundred
variant, so each new crossing can be captured without accumulating keys.

Bar draw/restore records use five-second intervals while continuously present.
Other named points use one second, with first/reentry capture. Continuously
present roots use five seconds. The first packet of each sampled root is paired
through allocation, submission, and restoration. A fixed 256-event ring drains
at most 16 records per rendered frame.

Stage/frame/song_ms describe capture time; emission may follow a few frames
later. The stage number is an inferred CTune lifecycle sequence, not a song ID.
Slots are decimal in logs. Values for counters are current count, previous count,
countdown, displayed number, x, y; rectangle values end with the first vertex
x/y/z. Introduction values are first/last/current frame with case in detail.
Staff values are x/y/width/height. Root/packet values are root position and scale.

device_ok=127 means all queried device fields succeeded. native_ok,
slot_lookup_ok, and batches_ok each indicate their own read validity.
batches refers only to the four general batches. packet_world is the allocation
source matrix and initialized packet matrix at submission.

draw_scope is 0 none, 1 bar, 2 counter, 3 direct selected effect, 4 selected
packet. draw_site names the explicit bar call/restoration contract.
judgement_scope is true only for a selected judgment sample currently inside
its draw scope. group6_scope marks traversal and does not imply movement.

Totals separate incidental_throttled and key_evictions from lost evidence.
Check buffer_dropped, stale_pairs, root_overflow, read_failures,
packet_collisions, and unowned_allocations; investigate nonzero values.
Absence of an unsampled incidental root is not evidence that it did not draw.

## Removed diagnostic-only hooks

The diagnostic build had 91 hooks. The following eight contracts, their ABI
entries/bindings, and diagnostic-only native layout fields have been removed:

| Temporary contract | RVA |
| --- | --- |
| diagnostic_hud_body | 24A269 |
| diagnostic_intro | 23F0AF |
| diagnostic_staff_sprite | 1C6F90 |
| diagnostic_hundred_rectangle | 1E4ADD |
| diagnostic_fade | 24A57D |
| diagnostic_counter_end | 1E4B58 |
| diagnostic_group6_begin | 24A2D5 |
| diagnostic_group6_end | 24A2DA |

Bar, individual counter, root-ownership, packet-allocation, packet-submission,
and packet-restoration hooks are permanent parts of the correction. Former
diagnostic sites that now implement placement were renamed accordingly.
They remain installed after removing logging.

## Closeout verification

IDA-CLI rechecked the supported input:
FEAD3BD4D0E0985F101965EDC417DD2B96522F8716FF789D84618FEB0D7A2522.
All 86 retained byte contracts matched; mutation spans ended on whole
instructions, had no interior code references, and did not overlap. The nine
pointer contracts remain in the existing global preflight. Evidence:
.codex-tmp/widescreen-closeout-20260906/retained_contracts.py and
.codex-tmp/widescreen-diagnostics-20260906/retained_contracts.json.

Both complete Debug and Release preset builds passed with no compiler/linker
warnings or errors. CMake regeneration emitted dependency deprecation warnings
from Zydis/Zycore. CLion error inspections completed for the changed gameplay,
profile, ABI header, startup, compositor, device, render, and runtime files
without errors. git diff --check passed. No synthetic tests were added or run.

The operator's observations establish acceptance of the reported visual fixes.
Compilation and native-contract checks establish the cleanup build's static
validity; they do not imply a new runtime run.

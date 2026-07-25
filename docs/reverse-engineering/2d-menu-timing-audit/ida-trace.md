# IDA Trace: 2D Menu Timeline Reach and Alternate Paths

- Status: binary audit complete; runtime probes remain acceptance work
- Audit date: 2026-07-25
- Owner: IDA evidence analyst
- Write scope: this file only; the executable, IDB, source, tests, and sibling audit files remain read-only.

## Fixed baseline and daemon probe

- Executable: `H:\gc\game471.exe`
- Executable SHA-256: `FEAD3BD4D0E0985F101965EDC417DD2B96522F8716FF789D84618FEB0D7A2522`
- IDB: `H:\gc\game471.exe.i64`
- IDB SHA-256: `55D119762B0706549AB5AA9C7D5D2DDF3C902AE322462D025D570C8181C50C1F`
- Source baseline: `H:\gc\artifacts\GCLoader\.worktrees\ctune-effect-timing`, commit `2354d0f`
- IDA daemon directory: `C:\Users\10614\.ida-cli\daemons`
- Connection contract: `AgentSession.connect(target, request_timeout_s=180)`
- First daemon request: `probe_backend(require_ida=True)`
- Probe result: `database_opened=True`, `ida_available=True`, backend `idalib`, target `H:\gc\game471.exe.i64`
- IDB policy: read-only IDAPython/Hex-Rays; no names, comments, types, bytes, or database-save operations are permitted.

## Current source contracts to prove in the binary

The current deployed-source baseline installs three direct 2D-related inline
hooks and derives their gate from one outer-frame hook:

| Source hook | VA / RVA | Expected entry bytes | Source policy |
|---|---:|---|---|
| `MovieClipGoto` | `0x004DEA30` / `0x000DEA30` | `6A FF 68 C9 38 67 00` | Increment a thread-local goto depth for the entire original call. |
| `MovieClipAdvance` | `0x004DF940` / `0x000DF940` | `56 8B F1 8B 06 8B 90 4C 01 00 00` | Skip ordinary calls on non-authored ticks; bypass the gate whenever goto depth is nonzero. |
| `NavigatorAdvance` | `0x005B6310` / `0x001B6310` | `55 8B EC 83 EC 08 89 4D FC 8B 45 FC 8B 48 60` | Skip the whole function on non-authored ticks. |
| `OuterFrame` | `0x00458B70` / `0x00058B70` | `56 8B F1 8B 06 8B 50 24` | Advance the shared deterministic authored-60 phase once per hooked outer-frame call. |

`NewsUpdate` (`0x00618A50`) and `NoticeUpdate` (`0x006544D0`) were deliberately
removed from the current hook plan and execute natively according to the source
design. Binary reach and whether they contain separate 2D clocks remain to be
proven here.

## Hook-site validation

All four current contracts match the fixed IDB at their exact entry addresses.
The bytes below are raw database bytes; function bounds, xrefs, and pseudocode
were then checked separately.

| VA | IDA function bounds | Raw entry bytes | Direct references and classification | Confidence |
|---:|---:|---|---|---|
| `0x00458B70` | `0x00458B70..0x00458BAF` | `56 8B F1 8B 06 8B 50 24` | Called twice from the central outer loop at `0x00458C8E` and `0x00458CAD`, one call in each update/present ordering branch. It is run before the corresponding render/present work. | High |
| `0x004DEA30` | `0x004DEA30..0x004DED77` | `6A FF 68 C9 38 67 00` | Direct code xrefs at `0x004DF81B`, `0x004DF8BF`, `0x004DF91F`, `0x004DF9C9`, and `0x005F6E73`: label resolution, frame goto-and-play, frame goto-and-stop, automatic wraparound, and `common.rvb` bootstrap respectively. | High |
| `0x004DF940` | `0x004DF940..0x004DFA41` | `56 8B F1 8B 06 8B 90 4C 01 00 00` | No direct code xrefs. Five data xrefs install it at virtual slot `+0x150` in one Movie/MovieClip interface family, not five independent clocks. Dynamic virtual callers are classified below. | High |
| `0x005B6310` | `0x005B6310..0x005B65FB` | `55 8B EC 83 EC 08 89 4D FC 8B 45 FC 8B 48 60` | Exactly one code caller, `0x005B77CD` in the registered navigator draw callback `0x005B77B0`. | High |

The IDB contains historical descriptive names/comments at some of these
addresses. They were treated only as search aids; the conclusions above come
from raw bytes, xrefs, disassembly, RTTI/vtable layout, and pseudocode in the
fixed database.

## MovieClip clock and object contract

Five vtable bases contain the same relevant interface layout:

- `0x006BD61C`: `Anim::IInstanceHasChild`
- `0x006BDC9C`: reference-count adaptor
- `0x006BDDF4`: smart-pointer adaptor
- `0x006BDF5C`: `Anim::Movie`
- `0x006BE0CC`: `Anim::MovieClipInstance`

In each table, slot `+0x150` points to `0x004DF940`. The adjacent contract is
also identical where it matters:

| Slot | Target | Observed role |
|---:|---:|---|
| `+0x90` | `0x004D1720` | Play: clear the stopped flag at object offset `+0x11C`. |
| `+0x94` | `0x004D1730` | Stop: set the stopped flag at `+0x11C`. |
| `+0x108` | `0x004DF8B0` | Goto-and-play by frame. |
| `+0x10C` | `0x004DF830` | Goto-and-play by label. |
| `+0x110` | `0x004DF910` | Goto-and-stop by frame. |
| `+0x114` | `0x004DF880` | Goto-and-stop by label. |
| `+0x118` | `0x004D1540` | Raw current-frame setter. |
| `+0x130` | `0x004D7670` | Timeline/frame lookup. |
| `+0x134` | `0x004D1580` | Forward wrapper; calls slot `+0x150`. |
| `+0x138` | `0x004D15A0` | Backward wrapper; calls slot `+0x150`. |
| `+0x150` | `0x004DF940` | Universal one-frame playhead primitive. |

`0x004DF940` reads and writes the signed 64-bit current-frame value at
MovieClip offset `+0x178`. A forward call adds one, then either clamps or wraps
to frame 1 according to the loop argument. Backward traversal uses the same
primitive. Wraparound can enter `0x004DEA30` so frame actions execute while
the playhead moves to frame 1.

The five data xrefs therefore represent inheritance/adaptor exposure of one
clock contract. They do not establish five callers or five timing domains.

## Normal draw traversal: root and nested clips are covered together

`Anim::DrawTraverse` is constructed at `0x004D0450` (vtable
`0x006BB74C`) and used by the renderer entry `0x004CE270`. Its MovieClip
visitor at `0x004CEC70` first traverses the current clip's child/timeline
container. After visiting that clip it:

1. checks the MovieClip stopped/playing state;
2. checks traversal mode at `DrawTraverse+0xFC`;
3. in normal forward mode (`1`), invokes MovieClip slot `+0x134` with looping
   enabled; or
4. in reverse mode (`2`), invokes slot `+0x138`.

Because nested MovieClips are recursively visited, the shared `0x004DF940`
hook reaches both root and child automatic advances. The authored-tick flag is
read, not consumed, by each invocation: on one authored outer iteration every
visited clip advances once; on a non-authored iteration all of those ordinary
advances are skipped. This is coherent for independently playing clips that
are each visited once per render.

The central loop at `0x00458C10` executes the `0x00458B70` update helper before
the render/present helpers in both ordering branches. Thus the current
OuterFrame hook publishes one shared phase for the MovieClip advances made
during that outer iteration; it no longer suppresses the rest of the update
phase.

## Goto, labels, and ActionScript paths

`0x004DEA30` is an internal goto walker. It normalizes the target frame, creates
an `Anim::GotoProcess` visitor, executes frame action lists, and can repeatedly
call the MovieClip forward wrapper (`+0x134`) to reach the destination. Its
direct wrappers are:

- `0x004DF830` / `0x004DF880`: label goto-and-play / goto-and-stop;
- `0x004DF8B0` / `0x004DF910`: frame goto-and-play / goto-and-stop;
- `0x004DF7D0`: label resolution;
- `0x005F6D50`: `common.rvb` bootstrap.

The ActionScript action executors also resolve to that same virtual interface:
Play and Stop call slots `+0x90`/`+0x94`; GotoAndPlay calls
`+0x108`/`+0x10C`; GotoAndStop calls `+0x110`/`+0x114`. Root and
find-instance label helpers at `0x004DAF20` and `0x004DAF60` call `+0x10C`.

Consequently the source hook's thread-local goto depth covers direct root
gotos, child/path-resolved gotos, intermediate traversal inside the goto
walker, label variants, frame variants, and frame actions triggered by those
walks. A goto-triggered advance is deliberately allowed on a non-authored
outer tick. Play/Stop do not move the playhead themselves; a later ordinary
draw traversal reaches the shared gate.

### Whole-image virtual-call sweep

A whole-image disassembly search for virtual-slot loads at `+0x134`, `+0x138`,
and `+0x150` found no second MovieClip advance wrapper or direct sink caller:

- MovieClip-typed `+0x134` calls occur in `0x004CEC70` (draw traversal),
  `0x004DEA30` (goto walker), and `0x004EFB90` (preprocessor).
- MovieClip-typed `+0x138` occurs only in `0x004CEC70`.
- MovieClip-typed `+0x150` occurs only in the forward and backward wrappers
  `0x004D1580` and `0x004D15A0`.
- Numeric `+0x134` matches in `0x004CF540` and `0x004DD9C0` are different
  interfaces: the former invokes a string/path callback on a separate object
  held at `this+6`, while the latter invokes a deserializer object's own slot
  308. Their object layouts and arguments do not match the MovieClip contract.

This makes the normal-render/goto/preprocessor classification exhaustive for
the shared MovieClip advance virtual slot in this image, subject to the usual
limitation that purely hand-written indirect calls need not use an immediately
recognizable vtable-load sequence.

### Alternate current-frame writer sweep

A separate whole-image scan covered the raw current-frame setter at virtual
slot `+0x118` and direct writes to the MovieClip current-frame qword at
`+0x178/+0x17C`:

- `0x004D1540` is the raw setter exposed by the MovieClip vtables. Its material
  dynamic use is inside the classified goto process; no second recurring
  playback scheduler calls it independently.
- `0x004DC3F0` initializes the frame state during MovieClip construction.
- `0x004E0940` resets the frame state to frame 1/subframe 0 while rebuilding or
  resetting an instance.
- Remaining numeric-offset matches operate on other object classes and do not
  carry a MovieClip vtable or timeline argument.

No alternate runtime playhead writer was found. Normal forward/reverse
playback, root and nested clips, and goto traversal therefore converge on the
already-classified `+0x150` primitive; constructor/reset writes do not form a
second timing domain.

## Distinct non-render caller: `Anim::PreProcessor`

There is a high-confidence context collision at the universal MovieClip sink.
`Anim::PreProcessor` (vtable `0x006BF204`) uses the same forward wrapper outside
normal once-per-render playback:

- `0x004F0720` repeatedly traverses a loaded movie until a preprocessing flag
  at `PreProcessor+0x79` clears.
- For each playing MovieClip, visitor `0x004EFB90` records its current frame,
  invokes MovieClip slot `+0x134` with looping disabled, compares the frame
  before and after, and calls Stop (`+0x94`) if the frame did not change.
- PreProcessor construction/use is reachable from `0x004DA3D0` and the
  `'PREP'` chunk parser at `0x004EEEF0`; `0x004DA3D0` is called during the
  asset-load iteration at `0x004E18D0`.

This is a tight preprocessing walk, not a 60 Hz display clock. It is not inside
`0x004DEA30`, so goto depth does not bypass the current gate. The current
non-authored-tick hook returns success without calling the original
`0x004DF940`; the preprocessor ignores that return value, sees that the saved
frame did not change, and stops the clip. On an authored tick, by contrast,
the shared phase remains true through the tight loop and all preprocessing
steps run.

Static verdict: intercepting all calls to `0x004DF940` is semantically too
broad unless the preprocessor is proven to execute only before hook install or
only under a safe phase. Runtime frequency is not proved by the IDB. The first
runtime probe should count `0x004EFB90` / `0x004F0720` entries after hook
installation, record the authored-phase value, count forward calls per
preprocessing traversal, and flag every Stop caused by an unchanged frame
after a skipped sink call.

## Navigator is a separate manual frame-cell clock

`0x005B6310` maintains several integer frame counters directly, without an
elapsed-time argument:

- state `+0x60` uses counter `+0x64` up to 10 for transition interpolation;
- counter `+0x70` counts down and resets to 2, 30, or a random value in the
  range 180 through 307, toggling `+0x74`;
- counter `+0x7C` counts down and resets to 5, 11, or 17, toggling `+0x80`
  and updating state at `+0x84`.

Its sole caller is the draw callback `0x005B77B0`, registered at priority 1200
by `0x005B77F0`. Current-IDB xrefs show exactly ten callers of that registration
helper:

`0x005ACE80`, `0x005B3CF0`, `0x005B8270`, `0x005BD3D0`, `0x005C3EE0`,
`0x005F76E0`, `0x00601140`, `0x00605800`, `0x006069D0`, and `0x0060BF30`.

This is not a duplicate MovieClip clock. It is a shared manual DDS/frame-cell
animation used across multiple menu/result scenes, and the current hook reaches
its sole advance sink globally.

## News and Notice are task-state owners, not additional playhead sinks

Current-IDB pseudocode for News (`0x00618A50`), Notice (`0x006544D0`), and the
small-news loader (`0x0060DB60`) contains setup/load-state increments but no
MovieClip forward call, sprite-cell index, or continuous transform clock:

- News loads the `news_big_*.png` resources, waits for resource readiness, then
  creates its sequence/movie owner.
- Small-news does the same for `news_small_*.png`.
- Notice creates its sequence/movie owner from configuration.
- All three end in the common sequence-task state machine at `0x0062FC60`.
  When active, that base dispatches the IFBL state wrapper. The IFBL timer
  engine at `0x006304B0` consumes the global elapsed-seconds delta for float
  waits; authored integer waits are a separate descriptor contract already
  represented by the source `IfblWait` transform.

The raw News/Notice state increments select one-time asynchronous setup phases;
they are not an animation cadence. Any MovieClip created by those phases later
advances through the shared MovieClip sink. Therefore the removed News/Notice
whole-task gates were aliases of broader task scheduling, not unique 2D
playhead gates. Static confidence is high; wall-time acceptance for the actual
news/legal sequences remains a runtime check.

## Native 2D draw-callback sweep

The central native 2D draw-callback registrar at `0x00425610` has 19 direct
registration callsites in the image. The registrations include delta-scaled
sprite transforms, static composition callbacks, the already-classified
Navigator callback, and two additional raw per-draw animation domains.

### Uncovered Ranking entry animation

RTTI/vtable evidence identifies `0x00616ED0` as `CRankingTask`'s start method.
It registers callback `0x00616C60` at draw priority 1400. XFL tag handler
`0x00617070` responds to `tg_rank01` through `tg_rank30` by setting the
corresponding entry counter to zero (and uses `-1` for inactive entries).
On every draw callback:

- each nonnegative entry counter is converted to slide offset and alpha;
- counts below 10 interpolate from offset 80 and alpha 0 to the settled state;
- `0x00616C60` then increments that entry counter unconditionally.

There is no elapsed delta and no call to MovieClip or Navigator advance. The
callback is registered independently of the XFL renderer even though
`CRankingTask` also addresses `ranking_xfl` children and labels such as
`imc_ran/imc_rank%02d` and `jf_base_1st`.

Static verdict: this 10-draw entry fade/slide is an uncovered authored-frame
clock. At a higher render cadence its wall-time duration scales inversely with
FPS unless another runtime layer throttles the draw callback. Confidence:
high for the call graph and counter semantics; runtime occurrence/frequency
should be measured.

### Uncovered HitChart entry animation

RTTI/vtable evidence identifies `0x00665A10` as `CHitChartTask`'s start method.
It also registers a priority-1400 draw callback, `0x00665230`. Tag handler
`0x00665B30` handles `tg_01` through `tg_30` by zeroing each selected entry's
counter at offset `+8`. On every callback:

- `0x00665230` derives entry position and alpha directly from that integer
  counter, using factors `0.04` for the first three entries and `0.125` for
  later entries;
- it draws the transformed entry with `0x005B2C30`; and
- it increments the entry counter once.

The same task writes rank text into XFL child paths such as
`imc_10/imc_rank04/imc_rank_text/tex_rank`, so this native overlay choreography
is materially coupled to the HitChart XFL screen but bypasses the MovieClip
playhead sink.

Static verdict: this is a second uncovered per-draw authored-frame clock, with
the same inverse-FPS wall-time risk. Confidence: high.

### Mixed-domain UnlockReward controller

The `unlock_reward` controller constructed at `0x00430750` combines correctly
elapsed-time-based fields with raw invocation counters. It owns
`data/2d_boost/menu/unlock_reward.dds`, a coin texture owner constructed at
`0x0042DDD0`, and many `unlock_reward_xfl` paths/labels. Its recurrent update
`0x00430C00` is reached through IFBL callback `0x005F7D20`.

Static initializer `0x00690A20` writes the descriptor at `0x007FC238` with
opcode 1 and callback pointer `0x005F7D20`. In the IFBL interpreter at
`0x006304B0`, an opcode-1 callback that returns false remains current and is
invoked again on the next interpreter update. Thus the raw fields below count
IFBL update invocations, rather than representing a one-shot setup loop.

Inside one invocation, seconds-based fields at `+0x3770` and `+0x37D0` are
reduced with the global delta. However:

- the countdown at `this+0x376C` is decremented once per call and fires
  `0x004303B0` when it reaches zero;
- the state counter at `this+0x37D4` increments through 1..31, then through
  33..43, with transition work at the boundaries.

These raw counters do not reach either current 2D gate. Static evidence proves
mixed timing domains and an uncovered invocation-count dependency; Hex-Rays'
imperfect field naming prevents assigning every visual effect to a counter
without a runtime trace. Confidence: high for the raw-count path, medium for
its exact visible symptom. Probe `0x005F7D20 -> 0x00430C00` call rate and log
the two fields alongside XFL labels and DDS draw activity.

### Recurrent IFBL callback sweep

The opcode-1 descriptor initializers provide a bounded inventory of recurrent
native callbacks. A whole-image initializer-pattern scan found 218 opcode-1
descriptors resolving to 197 unique direct callback targets. Each direct target
and its direct callees were checked for raw increment/decrement state that can
survive across callback returns.

Only two paths combine a recurrent opcode-1 callback with both elapsed-delta
state and raw per-invocation mutation:

1. gameplay tune processing at `0x006630B0`, outside the 2D menu audit; and
2. UnlockReward's `0x005F7D20 -> 0x00430C00` path described above.

The remaining apparent raw mutations are one-shot setup, calculation loops, or
delta-driven state machines rather than independent visual clocks. Examples
checked explicitly include:

- `0x00667470`, whose enclosing `0x00667CF0` path first accumulates
  `GlobalFrameDeltaSeconds` and compares the accumulated time against absolute
  thresholds;
- `0x005AE770`, whose enclosing `0x005AEF00` transitions are driven by global
  elapsed-delta fields;
- `0x00606F50`, a nested result-ranking calculation that completes and returns
  success in one invocation;
- `0x00616B80` and `0x006184A0`, single-step setup callbacks that increment
  once and return success; and
- `0x0060AAE0`, which belongs to opcode 4 and iterates numeric-field setup, not
  a retry-until-complete opcode-1 clock.

This inventory makes UnlockReward the only additional recurrent IFBL menu
clock found outside the existing MovieClip/IFBL-wait transforms. It does not
prove the exact visible symptom of each UnlockReward raw field; that remains
the medium-confidence portion of the finding.

### Static DDS consumers checked

- `sort_anim(_eng).dds` is drawn by `0x005AFF60`, but its transition fraction
  is accumulated in `0x005AC2D0` with the global elapsed-seconds delta.
- `title_s`, the `s_*` menu sheets, news PNGs, `coin.dds`,
  `balloon.dds`, and generic avatar/item/skin/title/message/SE resources are
  static files. Their load functions do not themselves create clocks.
- Navigator remains the confirmed exception where a static texture consumer
  owns a dedicated manual frame-cell state machine.

### Callback-sweep boundary and gameplay exclusion

The native draw registrar at `0x00425610` has exactly 19 direct registration
callsites. One registration selects among three callbacks, yielding 21 possible
callback targets. Every target was disassembled or decompiled:

- static composition callback `0x005A7D00` did not decompile, but its complete
  disassembly contains draw/setup work and no persistent counter mutation;
- delta-scaled callbacks use the existing global elapsed-seconds value;
- Navigator, Ranking, and HitChart are the only raw recurrent draw clocks in
  this target set.

One broader raw-counter search found a 30-invocation score/power visual in
`0x005E3EC0`. Its sole callsite, `0x0064A269`, lies inside
`GC120FPS_GameplayRender_Effects_FrameDomainTiming` (`0x00648D40..0x0064A598`)
and reads gameplay simulation/frame state. It is a gameplay-render effect, not
a menu, news, ranking, result-menu, or Flash/XFL scheduler, so it is explicitly
outside this audit rather than an unclassified coverage gap.

## Binary coverage verdict

The current implementation is **not complete for all 2D menu animation**, and
the universal MovieClip sink is not context-safe as currently gated.

| Domain | Binary reach by current source | Verdict | Confidence |
|---|---|---|---|
| Normal root and nested MovieClip playback | Shared `+0x150` sink | Reached coherently by one shared authored phase | High |
| Forward/reverse playback and constructor/reset alternatives | Shared sink; no second recurring writer found | Reached | High |
| Frame/label goto, ActionScript goto, and goto-triggered actions | Goto-depth bypass around `0x004DEA30` | Reached without suppressing semantic gotos | High |
| Movie asset preprocessing | Same sink from `0x004EFB90`, outside goto depth | **Incorrectly gated; a skipped call can make PreProcessor stop a clip** | High static semantics; runtime frequency unknown |
| Navigator DDS/frame-cell animation | Dedicated `0x005B6310` gate | Reached at its sole sink | High |
| News/Notice task updates | No independent playhead or native frame-cell clock | Correctly left native; later MovieClips converge on shared sink | High static; runtime wall-time acceptance pending |
| Ranking XFL-coupled entry choreography | Native callback `0x00616C60` | **Uncovered raw per-draw counter** | High |
| HitChart XFL-coupled entry choreography | Native callback `0x00665230` | **Uncovered raw per-draw counter** | High |
| UnlockReward native/XFL choreography | IFBL callback `0x005F7D20 -> 0x00430C00` | **Uncovered mixed domain with two raw invocation counters** | High path, medium exact symptom |
| Other registered native 2D draw callbacks | 21-target registrar sweep | No additional raw recurring clock found | High |
| Other recurrent IFBL opcode-1 callbacks | 197-target callback sweep | No additional in-scope raw recurring clock found | High within direct-descriptor inventory |

### Ranked findings

1. **High correctness risk - PreProcessor context collision.** The hook at
   `0x004DF940` is a universal playhead primitive, not exclusively the normal
   render scheduler. When the shared phase is false, its success-without-motion
   behavior violates `Anim::PreProcessor`'s before/after-frame contract and can
   stop a clip. Static semantics are conclusive; whether menu assets exercise
   the path after hook installation must be measured.
2. **High coverage gap - Ranking and HitChart entry transitions.** Both are
   XFL-coupled menu/result-screen choreography but increment native counters
   once per registered draw callback. Neither current 2D gate reaches them.
   Their authored-count transition durations therefore have inverse-FPS
   wall-time risk.
3. **Medium visible-risk coverage gap - UnlockReward.** Its recurrent IFBL
   update correctly uses elapsed seconds for some fields but also advances a
   countdown and state sequence once per callback invocation. The bypass is
   proven; its exact visible delta needs a runtime trace.

No binary evidence supports restoring broad News/Notice whole-task gates.
Likewise, no evidence supports treating `Anim::DrawTraverse` as a generic 3D
clock. Any later correction must be scoped to the proven caller/domain and
validated at multiple render rates; this audit makes no code recommendation or
mutation.

## Runtime probe matrix

Static tracing establishes reach and semantics, but cannot prove post-install
frequency, user-visible duration, or scheduler throttling. The minimum
acceptance trace is:

| Probe | Addresses / fields | Required observation |
|---|---|---|
| PreProcessor collision | `0x004F0720`, `0x004EFB90`, wrappers `0x004D1580/0x004D15A0`, sink `0x004DF940`, Stop `0x004D1730` | After hook installation, record asset/object, call stack, authored phase, goto depth, frame before/after, and every Stop caused by unchanged frame. Distinguish a tight preprocessing traversal from render traversal. |
| MovieClip callsite and multiplicity | `0x004CEC70`, `0x004DEA30`, `0x004EFB90`, `0x004DF940`; object `+0x11C`, `+0x178/+0x17C` | At 60/120/240 FPS, key calls by outer-frame serial, caller chain, object pointer, loop flag, stopped state, frame, phase, and goto depth. Confirm nested independent objects advance once each on authored ticks and semantic gotos always execute. |
| Ranking raw clock | tag handler `0x00617070`, draw `0x00616C60` | Measure callback frequency and wall time from counter reset to settled entry state at 60/120/240 FPS. |
| HitChart raw clock | tag handler `0x00665B30`, draw `0x00665230` | Measure callback frequency, entry counter, alpha/position, and reset-to-settle wall time at 60/120/240 FPS. |
| UnlockReward mixed clock | IFBL `0x005F7D20`, update `0x00430C00`, fields `+0x376C`, `+0x3770`, `+0x37D0`, `+0x37D4` | Log calls per wall second, raw and seconds-based fields, active XFL label/state, and DDS/coin draw activity. Identify the visible event at each raw-counter boundary. |
| Navigator control | `0x005B6310` fields `+0x60/+0x64`, `+0x70/+0x74`, `+0x7C/+0x80/+0x84` | Confirm the existing approximately 1:3 run/skip aggregate at 240 FPS also yields 60 authored updates per wall second and correct transition duration. |
| Native News/Notice control | `0x00618A50`, `0x006544D0`, `0x0060DB60`, state base `0x0062FC60` | Compare legal/news sequence wall time and input responsiveness at 60/120/240 FPS; do not infer correctness merely from task-update counts. |

Runtime logs should include the fixed executable/DLL identity and actual
present/update cadence. Aggregate run/skip counters alone cannot distinguish
root versus nested clips, preprocessing, goto bypass, or the three native
uncovered clocks.

## Checkpoint ledger

| Checkpoint | Status |
|---|---|
| Baseline and daemon probe recorded | Complete |
| Current hook sites validated in IDA | Complete |
| MovieClip/goto/script/child call graph classified | Complete, with PreProcessor context collision |
| Navigator/news/notice and sibling clocks searched | Complete; Ranking, HitChart, and UnlockReward gaps classified |
| Alternate MovieClip writers, native draw callbacks, and recurrent IFBL callbacks swept | Complete |
| Coverage verdict and runtime probe plan | Complete |

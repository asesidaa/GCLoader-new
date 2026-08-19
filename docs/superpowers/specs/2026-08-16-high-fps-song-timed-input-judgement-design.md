> **ARCHIVED FAILED DESIGN — NOT AUTHORITATIVE.** Retain it only as negative
> evidence for the abandoned query-composition approach.

# High-FPS Song-Timed Input Judgement Bridge

**Date:** 2026-08-16

**Status:** Superseded by [High-FPS Authoritative Input Judgement
Correction](2026-08-16-high-fps-authoritative-input-judgement-correction-design.md)

This document records the design implemented through commit `4df4e09`. It is
retained for historical evidence only and is not a current implementation
contract.

**Binary evidence target:** `H:\gc\game471.exe.i64`

**Runtime evidence target:** `H:\gc\loader-log.txt`

## Supersession

This historical document formerly superseded the behavior and architecture
in:

- [High-FPS Input Transition Bridge](2026-08-10-high-fps-input-transition-bridge-design.md);
- [High-FPS Input Judgement Transactions](2026-08-15-high-fps-input-judgement-transactions-design.md);
- [High-FPS Late-Gate Preview Correction](2026-08-15-high-fps-late-gate-preview-correction-design.md);
- [High-FPS One-Shot Input Lifetime Correction](2026-08-15-high-fps-one-shot-input-lifetime-correction-design.md); and
- [High-FPS Input Judgement Decision Record](../../reverse-engineering/high-fps-input-judgement-decisions.md), except as restated here.

The corresponding older implementation plans are obsolete and must not be
resumed. The current implementation's physical transition capture, fixed
storage, guarded hook transaction, and bounded diagnostic infrastructure may
be retained only after their contracts are changed to match this document.

The following rejected mechanisms must be removed rather than layered under
the replacement:

- exact-game-frame assignment;
- a `1 / 60` pending-edge lifetime;
- per-note or per-free-tap input ownership;
- query-driven edge consumption or retirement;
- native handler return values as input-consumption signals;
- query-order direction arming;
- event-time estimation from the QPC captured at judgement-core entry; and
- an outer-frame reset caused only by a frame containing no judgement call.

## Locked Terminology and Gameplay Model

Groove Coaster gameplay has one logical current note and one current note type.
The native implementation uses generic containers and may evaluate the current
note once for each booster component, but those component indices are not
gameplay lanes and do not represent concurrent notes.

This design therefore uses these terms:

- **current note:** the one logical note offered to judgement;
- **booster component:** the left or right booster requirement evaluated for
  the current note;
- **input cohort:** all control changes published in one physical polling
  snapshot; and
- **judgement sample:** the immutable effective input view shared by every
  native query during one judgement-core call.

Do not use `lane`, `paired note`, `simultaneous note`, or `chord` to describe
the current-note model. CRITICAL and DUAL HOLD are one logical note whose
native handler evaluates requirements for both booster components. A note
that accepts multiple directions still remains one logical note.

## Problem and Root Cause

The high-FPS patch currently advances two coupled systems on different
schedules:

1. input state and native input history advance with rendered-frame polling;
2. judgement advances from the gameplay audio clock.

The caller at `0x6401E0` reads the audio-derived step count and calls judgement
core `0x5D68E0` once for each crossed target tick. One rendered frame may
therefore execute:

- zero judgement calls when the audio cursor has not crossed a target tick;
- one ordinary judgement call; or
- multiple historical catch-up calls when several target ticks were crossed.

A native pressed edge is a sampled state transition. If the rendered frame
containing that edge has a zero audio-clock step, no note handler queries it.
The next input sample can already have lost the edge even though a held state
continues. This matches the observed pattern: short inputs drop, while an
input already held inside a long note remains available.

The current bridge adds two independent defects:

- `ObserveGameplayOuterFrame` treats a normal zero-step frame as gameplay
  inactivity and clears valid pending input; and
- it derives event time from `event_qpc - recognition_qpc`, where
  `recognition_qpc` is sampled separately at each core entry. Multiple
  historical catch-up ticks execute at nearly one wall-clock QPC, so a fresh
  event can be attached to an older song tick and the wrong current note.

The per-note transaction and retirement policy then changes native semantics.
Native pressed and held queries observe input state; they do not transfer
ownership of an event to a note. Every booster-component evaluation of the
current note must see one coherent sample. Direction matching is additionally
broken because the current synthetic held result is armed only after the
native matcher has already issued its first current-held query.

The correction is therefore not a larger judgement window and not a fixed
60 FPS judgement island. It is a clock-domain correction: preserve physical
input transitions until the corresponding song time is judged, then present
one immutable native-style input sample.

## Goals

1. Eliminate short-input loss caused by zero-step rendered frames.
2. Prevent a physical event from entering an older historical catch-up tick.
3. Use the physical input's song-timeline timestamp for edge-triggered note
   eligibility and timing grade.
4. Preserve the native current-note handlers, state machines, scoring
   aggregation, sounds, effects, holds, repeats, and duration mechanics.
5. Give every query and booster-component evaluation in one core call an
   immutable, idempotent input view.
6. Preserve original 60 FPS input-history forgiveness at 144, 165, 240, and
   other supported target rates without rounding to target-rate frames.
7. Preserve the locked Arcade and Switch behavior.
8. Keep target FPS 60 a correction no-op apart from passive validation logs.
9. Keep the hot path bounded, allocation-free, and independent of audio
   callbacks.

## Non-Goals

- Rewriting the chart scheduler or native judgement system.
- Processing more than the one logical current note selected by native code.
- Running note judgement at a synthetic fixed 60 Hz.
- Increasing, decreasing, or otherwise redefining judgement windows.
- Searching ahead through future notes.
- Patching the generic composite logical-input IDs `10..19`; audited
  judgement handlers do not query them.
- Scaling millisecond mechanics such as scratch or beat intervals.
- Reading, watching, reloading, or exposing new timing configuration.
- Changing menu, test-mode, binding, raw FastIO, or input-backend behavior.
- Changing the WASAPI or ASIO audio callback.
- Elaborate recovery from impossible transition rates or corrupted clocks.

## Native Binary Model

### Judgement scheduling

The audio-clock hook determines a target-tick step. The caller at `0x6401E0`
then invokes `0x5D68E0` for each crossed target tick and supplies that tick's
recognition milliseconds and gameplay frame. Zero is a normal step value.
Several calls in one rendered frame are historical catch-up ticks, not several
observations of current wall-clock time.

### Current-note dispatch and free tap

Core `0x5D68E0` obtains the current note descriptor and dispatches it through
`0x5D5720` only when recognition time is strictly greater than the descriptor
field at `+152`. That field is the already-baked earliest-eligible timestamp.
The loader does not recalculate it from `system.cfg`.

After current-note processing, the native core calls free-tap handler
`0x5D2040` at `0x5D76E4` when its suppression state permits it. For a normal
unmuted current note, recognition time greater than descriptor `+152`
suppresses free tap. A muted note retains native free-tap permission.

Free tap queries booster buttons `4` and `9`, then applies the native input
eligibility, selectable hit-sound, suppression, and effect behavior. The
replacement must continue through this native function.

### Audited input primitives

The complete judgement-handler xrefs use:

- pressed wrapper `0x659640`;
- held wrapper `0x659570`;
- held-age wrapper `0x6594D0`; and
- direction wrapper `0x659390`.

All pressed-wrapper calls made by note judgement and free tap use base logical
controls `0..9`. The generic input-device implementation supports composite
IDs `10..19`, but no audited judgement call site supplies them. CRITICAL and
DUAL HOLD are implemented by native booster-component evaluation using the
ordinary base controls. No composite-ID correction is required.

### Recognition and event time

This design distinguishes:

- **recognition time `R`:** the native song time of the current judgement
  tick; and
- **event time `T`:** the physical transition mapped into the same song-time
  domain.

Recognition time remains authoritative for:

- current-note selection and native iteration;
- note state transitions and miss processing;
- hold, slide, scratch, and beat lifecycle;
- repeat and duration calculations;
- native flags, effects, and sound execution; and
- current-note booster-component aggregation.

Event time may affect only:

- whether an edge belongs to free tap or the current note;
- the selected edge's shared late-gate argument; and
- the selected edge's timing-grade argument.

Already-held or recent-history acceptance with no qualifying new edge stays on
recognition time. Sustaining a hold or slide is not a new physical hit.

## Selected Architecture

The replacement is a song-timed transition journal feeding one immutable
judgement sample. It has five cooperating pieces:

1. the existing polling producer records every published logical transition;
2. the gameplay song-clock seam publishes a QPC-associated song-time anchor;
3. a fixed-capacity timeline maps transitions into song time and retains them
   until that time is reached by judgement;
4. core entry freezes all ready transitions into one immutable judgement
   sample; and
5. scoped query overrides compose that sample with native Arcade and existing
   Switch results.

There is no note-owned queue, no free-tap-owned queue, and no event lifetime
measured from wall-clock expiry. An edge is exposed for exactly one eligible
judgement sample and then ceases to be a pressed pulse. Its resulting held
state remains available normally.

## Data Contracts

### Physical transition record

Each record contains:

- monotonically increasing sequence number;
- input cohort identifier;
- QPC timestamp captured after the input mapper publishes the snapshot;
- complete pre-transition and post-transition held masks;
- rising mask;
- falling mask; and
- gameplay/input epoch.

Every changed control in one polling publication shares the cohort and QPC.
Separate publications retain their real order. The producer records only
changes and performs no judgement or note lookup.

### Song-time anchor

An anchor contains:

- anchor QPC;
- exact unwrapped audio source frame and source sample rate when available;
- otherwise the native rounded group cursor in milliseconds;
- playback generation; and
- exact, rounded, inactive, or failed source state.

The existing gameplay song-clock query brackets cursor acquisition with QPC
samples. The exact observation is associated with the midpoint of that short
query interval. This work occurs on the existing game-thread clock seam, not
inside the audio callback.

For an exact anchor, event time is derived from the source-frame time plus the
QPC delta between the event and anchor. Arithmetic uses checked integer
quotient/remainder or an equivalent rational representation. It does not use
floating target-frame ratios. Rounded group-cursor observations provide a
millisecond fallback with the same QPC association.

Here, **physical event time** means the time at which the polling and mapping
pipeline published the logical transition. It is not a hardware-interrupt
timestamp. Its accuracy is bounded by the device poll interval, thread
scheduling, mapper publication, and the exact or rounded audio-cursor source.
The contract is sub-render and independent of target-frame quantization, not
nanosecond-perfect knowledge of switch closure. Controls published in one
cohort intentionally retain one timestamp because their order was not
observable to the loader.

`JudgTimeOffset` and `GameTimeOffset` are fixed at the supported cabinet's
current values for this design. The current zero values add no calculation.
The bridge does not read, watch, cache generations for, or support live changes
to either value. Other `system.cfg` timing values are likewise not read by the
bridge.

### Immutable judgement sample

At core entry, the sample freezes:

- recognition time `R` and gameplay frame metadata;
- playback generation and epoch;
- all transition records with mapped `T <= R`;
- a rising pulse mask;
- the final effective held mask;
- the selected newest rise time and cohort for each logical control;
- authored-60 historical state used by direction forgiveness; and
- separate current-note and free-tap pulse classifications as they become
  known from the current descriptor.

Input query functions are pure observations of this sample. They may record a
component-local selected edge for late-gate, grade, and diagnostics, but they
never remove or hide a pulse from another query or booster component.

## Processing Algorithm

### Transition capture and readiness

1. The polling thread publishes a changed FastIO-derived logical snapshot and
   appends one timestamped transition record.
2. Zero-step rendered frames do not consume, expire, or reset that record.
3. On core entry for recognition time `R`, the consumer maps pending records
   through the current playback-generation anchor.
4. Records with `T > R` remain pending. This prevents a fresh event from
   appearing in an older catch-up tick.
5. All records with `T <= R` are included in the current sample and are no
   longer pending after the core call completes.

This adds no intentional input delay. Each transition is presented at the
first native judgement tick whose song time reaches it.

### Pulse and held-state construction

All distinct ready rising controls are ORed into the sample's pressed mask.
The final post-transition mask supplies the held state. A control that rose and
fell entirely between two judgement calls is forced held for the current
sample so its pressed pulse remains usable; the following sample observes the
final released state.

Several different controls remain independently visible. This preserves a
single physical cohort across both booster-component evaluations of the
current note. If the same logical control rises more than once before one core
call, the native boolean sample can represent only one pressed pulse. The
newest rise supplies its timestamp, older rises for that control coalesce, and
a bounded diagnostic counter records the event. No controls coalesce with a
different logical control.

Every ready pulse expires after this one core call regardless of whether a
handler queried it, accepted it, rejected it, or returned true. This mirrors a
native sampled edge and removes all loader-side ownership decisions.

### Catch-up behavior

When one rendered frame runs several historical core calls, each call receives
its own recognition time `R`:

- an event stays pending while `T > R`;
- it enters exactly the first catch-up call with `T <= R`; and
- later catch-up calls see only its held result, not another pressed pulse.

Core-entry wall-clock QPC is diagnostic only and never defines recognition
time or event association.

## Current-Note and Free-Tap Routing

Routing is linear and uses only the one current note descriptor supplied by
native dispatch. It does not scan future notes.

For each ready physical rise:

1. If native code has no current note, expose it only to free tap.
2. If the current note is muted under the native predicate, preserve native
   free-tap permission.
3. If `T <= descriptor.earliest_eligible_ms`, expose it only to free tap.
4. If `T > descriptor.earliest_eligible_ms`, expose it only to the current
   note, even when it is the wrong control for that note.

The fourth rule prevents an incorrect input inside the current-note region
from becoming an unintended selectable hit sound.

Normally the native free-tap branch already agrees with this classification.
One boundary-crossing case needs a narrow correction:

```text
T <= earliest_eligible_ms < R
```

Native code would suppress free tap because it compares `R`. A guarded
mid-hook at the verified branch beginning at `0x5D76CE` may force the existing
call path to `0x5D2040` only when the immutable sample contains an event
classified as free tap. Note-query overrides hide that event from the current
note. Free-tap-query overrides then expose it to the unmodified native
function.

The correction must not call `0x5D2040` manually after core return, duplicate
the native free-tap implementation, bypass mute/input filters, or make a
current-note-classified wrong input audible.

If one sample contains distinct physical rises on opposite sides of the
boundary, the current-note and free-tap masks remain separate. Native note
processing observes only the note-classified rises; the native or forced
free-tap path observes only the free-tap-classified rises.

## Effective Input Queries

The effective gameplay result composes:

1. the native query result;
2. the immutable high-FPS sample while a correction scope is active; and
3. the already-approved Switch gameplay aliases when Switch style is active.

Native success is never changed to failure. Outside the scoped high-FPS
judgement call, wrappers return native behavior. The bridge does not write its
effective values back to FastIO or the game's global input history.

### Pressed

A current-note pressed query observes its native result OR the matching
current-note pulse. A free-tap pressed query observes its native result OR the
matching free-tap pulse. Repeated queries return the same result. The first
booster component, alias probe, or helper call cannot consume the result seen
by another.

### Held

Current held queries observe native held OR the sample's effective held state.
Long-form continuation remains native and recognition-driven. Synthetic held
is needed for a rise-and-fall that completed between judgement calls and lasts
only for the sample presenting that rise.

### Held age and historical held

Direction forgiveness is answered through the authored-60 history described
below. Other unexpected historical query shapes fall back to native behavior
and increment only a bounded anomaly counter.

### Direction

Direction is derived from the same immutable held state and validated native
normalization table used by the existing Switch patch. The direction query
does not depend on whether held age happened to be queried first.

## Original 60 FPS Forgiveness

The native direction matcher at `0x5D2E50` observes current held, held age,
held state at `current_frame - 2`, and current direction. Directly using two or
four high-FPS frames shrinks the original forgiveness intervals.

A small `Authored60History` reconstructs only these native input-history
semantics. It does not schedule or delay judgement:

- current pressed edges remain immediately available at the current high-FPS
  judgement tick;
- state transitions are projected onto the original rational 60 Hz sample
  boundaries for historical queries;
- the native two-sample lookup means exactly two authored 60 Hz samples, not
  two target-FPS frames;
- held age advances in the same authored-sample domain used at native 60 FPS;
- the native `<= 1` and `<= 4` comparisons therefore retain their original
  boundary inclusivity and duration; and
- boundary behavior is verified against independent 60 FPS reference cases,
  not inferred from rounded 144/165/240 frame counts.

The current snapshot is prepared before the matcher begins, so its native
query order is irrelevant to synthetic visibility. Continuation matching
continues to use current held state as native code expects.

The supported cabinet keeps `HoldSafeFrame` and `SlideHoldSafeFrame` at zero;
the bridge does not read, scale, or emulate them. `ScratchEnableTime` and
`BeatEnableTime` are native millisecond mechanics and remain unchanged.

## Locked Switch Compatibility

The existing gameplay-only Switch policy remains authoritative:

1. A newly pressed direction may act as the center-button pressed edge on the
   same booster.
2. A booster button remains held while its real button or any direction on
   that booster is held.
3. Either adjacent cardinal may satisfy a diagonal target initially and
   continuously.
4. Real buttons, exact diagonals, native cardinal matches, and every unrelated
   native success remain valid.
5. Arcade behavior and menu, test-mode, binding, raw FastIO, and backend
   behavior remain unchanged.

The transformations apply independently to each booster component while all
components see the same immutable judgement sample. They do not create extra
logical notes or require both physical cardinals for a diagonal.

## Complete Note-Type and Free-Tap Audit

Every binary note ID and free tap is an explicit implementation and
verification obligation. The `booster_component` parameter selects the
ordinary base control group used by the current logical note; it is not a
lane.

| ID | Native path and input order | Song-timed correction | Native behavior retained |
|---:|---|---|---|
| 0 | Dispatcher default; no input handler | No invented note input; route free tap through the native current-descriptor rules | Marker/lifecycle behavior |
| 1 | NORMAL family through `0x5D1D50`; late gate before button `4/9` pressed | Preview only a compatible component pulse for the late gate; actual selected edge supplies grade delta | Candidate state, effects, grade encoding |
| 2 | FLICK `0x5D3320`; late gate before direction matcher | Preview a compatible direction rise from the immutable sample; selected contributing rise supplies grade delta | Native target angle and result handling |
| 3 | HOLD `0x5D41B0`; head late gate and pressed, then held body | Physical `T` applies to the head edge only; current held feeds continuation | Duration, release, completion |
| 4 | SCRATCH `0x5D3C60`; four direction pressed queries before late gate | Native query order selects the actual head rise before the gate; later pulses are each one judgement sample | Scratch direction sequence and duration |
| 5 | BEAT `0x5D3920`; button pressed before late gate and repeated during the mechanic | Each physical repeat edge is presented once; the head's selected edge may supply the gate time | Repeat interval, duration, completion |
| 6 | MERRY GO ROUND `0x5D5660` to normal handler with segment offset | Apply `T - R` to the native already-offset late-gate and grade arguments | Segment selection and offset |
| 7 | NORMAL-family variant | Same as ID 1 | Variant lifecycle |
| 8 | NORMAL-family variant | Same as ID 1 | Variant lifecycle |
| 9 | CRITICAL `0x5D1F70` to normal handler for each booster component | One logical note; every component sees one sample and uses its own selected control timestamp | Native cross-component aggregation |
| 10 | SLIDE HOLD `0x5D35C0`; head late gate before direction matcher, then held continuation | Physical `T` applies to the direction head; authored-60 history and current held support matching | Slide duration, release, completion |
| 11 | Dispatcher default; no independent input handler | No input correction beyond shared core/free-tap routing | Native lifecycle |
| 12 | Dispatcher default; no independent input handler | No input correction beyond shared core/free-tap routing | Native lifecycle |
| 13 | Dispatcher default; no independent input handler | No input correction beyond shared core/free-tap routing | Native lifecycle |
| 14 | Dispatcher default; no independent input handler | No input correction beyond shared core/free-tap routing | Native lifecycle |
| 15 | DUAL HOLD `0x5D5540` to HOLD for each booster component | One logical note; component heads share the sample without consumption and retain individual `T` values | Native wait-for-other-component and long-form lifecycle |
| - | Free tap `0x5D2040`; buttons `4/9` after note processing | Route from physical `T`; force only the verified boundary case; preserve Switch aliases | Native eligibility, suppression, selectable sound, flags, effect |

### Late gate and grade selection

NORMAL, FLICK, HOLD, SLIDE HOLD, and their shared families call the late gate
before querying input. Dispatcher entry may therefore create a non-consuming,
type-compatible preview from the immutable current-note pulse mask. SCRATCH
and BEAT query pressed first, so their actual native query selection supplies
the gate edge. A preview alone never grades an input or changes native state.

Once an actual edge-triggered match is selected, the late-gate or grade hook
applies the mapped song-time delta to the native argument:

```text
forwarded_argument = native_argument + round_to_native_ms(T - R)
```

Applying a delta rather than replacing the argument preserves MERRY GO ROUND's
segment adjustment and other native caller-local arithmetic. Event mapping
uses rational song time; rounding occurs only at the native integer-millisecond
ABI. A native result with no qualifying new edge receives the original
argument unchanged.

Event time never makes the acceptance window stricter. Because a presented
event always has `T <= R`, substituting it at the late-only gate can preserve
or rescue a physically valid edge but cannot make a recognition-time success
late. Current-note/free-tap routing separately prevents a pre-eligibility
event from becoming a note hit.

Long-form calls to duration helper `0x5D04F0` remain entirely on recognition
time.

## Lifecycle, Capacity, and Failure Behavior

### Epoch resets

Clear and reseed the journal only for actual discontinuities:

- bridge activation or deactivation;
- focus loss;
- input-device disconnect;
- explicit gameplay playback transition to inactive;
- playback-generation change; and
- shutdown.

Seed currently held controls as pre-held state without generating pressed
edges. Do not reset because:

- one outer rendered frame had no judgement call;
- the audio step was zero;
- several catch-up calls occurred; or
- no note handler queried input in a core call.

An exact and rounded cursor observation are both valid anchors. If both clock
sources fail, clear and reseed the correction epoch, continue with native
input for that scope, and record one rate-limited anchor failure. Do not guess
an event time from core-entry wall-clock QPC.

### Fixed capacity

The transition journal and diagnostic ring remain fixed capacity and perform
no hot-path allocation. If the transition journal is full:

1. append the newest complete transition;
2. discard the oldest transition;
3. increment the cumulative eviction counter; and
4. emit only a rate-limited anomaly.

The newest full post-transition snapshot remains sufficient to recover current
held state. Lost historical forgiveness or a visible missed input is the
acceptable abnormal symptom of overflow. The bridge does not disable itself,
delay new input, or attempt rollback.

### Installation and exception safety

All executable sites require named RVAs, expected original bytes, readable
memory, and transactional hook installation. The free-tap branch site is a new
guarded site and participates in the same preflight and rollback contract.

Runtime state is initialized through the ordinary framerate-patch startup
after normal process runtime initialization. No new pre-CRT global constructor,
`DllMain` callback work, audio callback work, or cross-thread native judgement
call is permitted. Exceptions must not cross a hook boundary.

At target FPS 60, do not install or activate the song-timed correction hooks,
query overrides, free-tap branch correction, history reconstruction, late-gate
adjustment, or grade adjustment. Passive validation logging may remain. The
independently selected existing Switch gameplay patch retains its native
60 FPS behavior.

## Diagnostics

Diagnostics are bounded records and summaries, not per-query logs. Replace
the misleading `lane` field with `booster_component` everywhere in this
feature.

Useful per-event fields are:

- epoch, playback generation, sequence, and cohort;
- source input and effective requested input;
- physical event time `T`, recognition time `R`, and delivery delay `R - T`;
- anchor source (`exact` or `rounded`) and cursor-query span;
- current note type and booster component;
- descriptor earliest-eligible time;
- route (`current_note` or `free_tap`);
- native versus forced free-tap branch;
- native, edge, held, or authored-history match reason;
- late-gate and grade deltas; and
- native handler result as observation only.

Summary counters include:

- captured, mapped, deferred, delivered, and coalesced transitions;
- native and forced free-tap presentations;
- catch-up deferrals;
- exact and rounded anchors plus anchor failures;
- queue depth, maximum depth, and oldest-record evictions;
- resets separated by real reason;
- hook callback or invariant anomalies; and
- bounded diagnostic overwrites.

Do not emit one log line per judgement core, input query, held continuation, or
ordinary zero-step rendered frame.

## Verification Contract

### Automated behavioral coverage

Tests must derive expected behavior independently and cover:

1. a rise during a zero-step rendered frame appearing exactly once at the
   first `R >= T` core call;
2. an event remaining hidden from historical catch-up ticks where `T > R`;
3. a press and release between core calls producing one pressed/held sample
   and a released following sample;
4. several controls in one cohort retaining one timestamp and remaining
   visible to every booster-component evaluation;
5. repeated queries being idempotent and the first query not consuming input;
6. newest-wins coalescence only for multiple rises of the same control before
   one core call;
7. exact and rounded song-time anchors at 144, 165, and 240 FPS without target
   frame-ratio assumptions;
8. catch-up steps of zero, one, and more than one;
9. free tap when `T` equals or precedes the earliest-eligible boundary;
10. current-note routing immediately after that boundary;
11. the boundary-crossing forced native free-tap path;
12. muted-note free-tap behavior;
13. a wrong input inside the current-note region not becoming free tap;
14. current held, authored frame-minus-two history, and held-age boundaries
    matching native 60 FPS reference behavior;
15. all locked Switch button, held, exact-direction, and adjacent-cardinal
    rules;
16. every note ID `0..15` plus free tap using the matrix above;
17. CRITICAL and DUAL HOLD component calls sharing a sample without invented
    concurrent-note semantics;
18. no judgement use of generic composite input IDs `10..19`;
19. oldest-record eviction with continued newest-state operation;
20. actual epoch resets and no reset for an ordinary zero-step frame;
21. invalid clock-anchor native fallback; and
22. target FPS 60 installing no correction behavior.

Hook tests must independently cover signature rejection, partial-install
rollback, the free-tap branch site, x86 calling conventions, and exception
containment. Tests that only grep source or duplicate production RVA tables are
not acceptable evidence.

### Static verification

Before deployment:

- run focused correction tests while implementing;
- build and test the complete x86 Debug preset graph;
- build and test the complete x86 Release preset graph;
- run `git diff --check`;
- inspect the production hook plan and guarded bytes; and
- confirm the deployed artifact has not been claimed from build output alone.

### Runtime acceptance

Build and static evidence cannot establish gameplay correctness. Runtime
acceptance is deliberately ordered:

1. run a 240 FPS Switch session first;
2. inspect `H:\gc\loader-log.txt` before making any success claim;
3. if 240 FPS still drops or misassociates input, stop and diagnose that run;
4. only after acceptable 240 FPS behavior, run the 60 FPS validation session.

The 240 FPS session must exercise:

- short alternating button notes;
- short direction notes and diagonal/cardinal Switch acceptance;
- CRITICAL and DUAL HOLD booster-component behavior;
- HOLD and SLIDE HOLD heads and continuation;
- SCRATCH and BEAT repeated edges; and
- selectable free tap both far from and immediately around the next note's
  earliest-eligible boundary.

Acceptance requires evidence of captured rises being delivered exactly once,
no ordinary zero-step resets, no future event entering an older catch-up tick,
correct current-note/free-tap routes, no unexpected overflow or anomaly, and
subjectively stable judgement. A passing unit suite or clean log does not by
itself establish gameplay feel.

## Planning Boundary

Implementation planning may begin only from this document and current source
and binary evidence. The plan must remove the superseded transaction/lifetime
behavior before adding the song-timed sample, must preserve the existing audio
pipeline, and must keep the 60 FPS correction path inactive. No runtime
deployment is part of design or planning; deployment occurs only when a later
execution request explicitly includes it.

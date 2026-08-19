> **ARCHIVED FAILED DESIGN — NOT AUTHORITATIVE.** The title is historical; this
> design failed to establish correct game behavior.

# High-FPS Authoritative Input Judgement Correction

**Date:** 2026-08-16

**Status:** Implemented; corrected after 240 FPS runtime findings; awaiting cabinet acceptance

**Binary evidence target:** `H:\gc\game471.exe.i64`

**Runtime evidence target:** `H:\gc\loader-log.txt`

## Authority and Supersession

This document is the sole authoritative design for correcting high-FPS
gameplay input and judgement. It supersedes:

- [High-FPS Song-Timed Input Judgement Bridge](2026-08-16-high-fps-song-timed-input-judgement-design.md);
- [High-FPS Input Judgement Decision Record](../../reverse-engineering/high-fps-input-judgement-decisions.md);
- the corresponding implementation plan; and
- the deployed implementation through commit `4df4e09` where its behavior
  conflicts with this document.

The earlier design chose the right clock-domain foundation but did not define
an enforceable native-query contract. The correction retains the physical
transition journal, song-time anchors, fixed-capacity storage, immutable core
sample, guarded hooks, and native note handlers. It replaces the entire
query-composition and edge-association layer rather than adding another
fallback or narrow note-type exception.

No production code is authorized by this document alone. Implementation
requires a separately reviewed plan.

## Locked Gameplay Model

Groove Coaster has one logical current note and one current note type. A native
handler may evaluate that note for booster component zero and booster component
one, but those component indices are not lanes or concurrent notes.

Use these terms:

- **recognition time `R`:** the native song time of one judgement-core call;
- **event time `T`:** the physical transition publication time mapped into the
  same song-time domain;
- **input cohort:** all logical changes published by one physical snapshot;
- **judgement sample:** one immutable input view shared by all native queries
  in one judgement-core call;
- **booster component:** the left or right control group evaluated for the one
  current note; and
- **authored sample:** one rational `1 / 60`-second input-history sample.

Do not introduce lane, chord, paired-note, or simultaneous-note ownership into
the design. Multiple controls may be visible in one immutable sample without
representing multiple chart notes.

## Confirmed Failures in the Current Implementation

The latest 240 FPS Switch-input run captured and delivered physical
transitions, but direction notes repeatedly returned `handler=0` with no
selected event time. Static and native review found these contract failures:

1. `SongTimedInputTimeline` computes
   `held_two_authored_samples_ago`, but production never consumes it.
2. `HighFpsInputBridge::QueryHeld` discards the requested gameplay frame.
3. `JudgementInputScope::QueryHeld` answers every held query from current held
   state.
4. `ComposeGameplayQuery` unconditionally lets a raw native `true` override
   the song-timed sample. This defeats catch-up deferral and current-note versus
   free-tap routing.
5. Native direction matcher `0x5D2E50` uses held at `frame - 2` as a negative
   freshness guard. Returning current held there makes a fresh direction look
   pre-held and breaks FLICK and SLIDE HOLD heads.
6. SCRATCH query observation overwrites the first native-priority direction
   with the last successful query. This is only a narrow head late-boundary
   association defect, not continuous scratch grading.
7. Ordinary per-frame note observations overwrite meaningful diagnostic
   events, and `oldest_evictions` mixes real input loss with normal authored
   history rotation.
8. Descriptor offset `+152` was misnamed as the playable boundary. Native
   bake and handler code prove `+152` is the mute/outer-dispatch boundary,
   `+156` is the unmute/playable boundary, and `+160` is the late limit.
9. A muted descriptor was incorrectly routed only to free tap. Native core
   `0x5D68E0` still dispatches the note handler and separately leaves the
   post-note free-tap path enabled, so HIDDEN/AD-LIB input must be visible to
   both native paths.
10. A direction rise lost its physical timestamp after its one-shot pressed
    delivery even though native FLICK/SLIDE HOLD forgiveness may accept the
    still-held direction during a later high-FPS core. That later acceptance
    therefore fell back to recognition time and made direction timing depend
    on the 240 FPS phase.

Passing unit tests did not catch these failures because the tests proved that
history was produced without executing the native consumer predicate.

## Goals

1. Present every physical rise exactly once to the first judgement call whose
   recognition time reaches it.
2. Prevent a current rendered-frame native bit from leaking into an older
   catch-up tick or across the free-tap/current-note boundary.
3. Reproduce the original authored-60 current-held, held-age, and
   frame-minus-two freshness semantics at every supported target FPS.
4. Keep stable GREAT, GOOD, and MISS outcomes unchanged.
5. Resolve sampling-sensitive boundary cases deterministically from physical
   event time. GREAT-to-GOOD and GOOD-to-GREAT changes are both acceptable in
   this narrow class and require no shadow comparison.
6. Preserve all native note handlers, lifecycle state, effects, sounds,
   aggregation, and long-note scoring.
7. Preserve every locked Arcade and Switch rule.
8. Cover note IDs `0..15` and free tap explicitly. No known gameplay query may
   rely on a generic fallback.
9. Remain bounded, allocation-free on the hot path, and independent of audio
   callbacks.
10. Keep target FPS 60 a correction no-op apart from passive validation.

## Non-Goals

- Replacing the chart scheduler or running judgement on a separate 60 Hz
  island.
- Searching future notes or inventing note ownership.
- Reimplementing the native direction matcher or long-note state machines.
- Retiming long-note duration, release, scratch, or beat mechanics.
- Reading or scaling fixed judgement configuration.
- Changing menu, test mode, input binding, raw FastIO, WASAPI, or ASIO
  behavior.
- Comparing native and event-time grade results at runtime.
- Guaranteeing hardware-interrupt timestamp precision. `T` remains bounded by
  device polling, mapper publication, and thread scheduling.

## Configuration Boundary

`GameTimeOffset` and `JudgTimeOffset` are the only variable timing settings in
scope. They cannot change during a song and retain the game's existing
activation behavior.

- `GameTimeOffset` remains an input to the existing gameplay song clock. Both
  `T` and `R` therefore occupy the same adjusted song-time domain.
- `JudgTimeOffset` remains embedded in the native late-limit and grade
  arguments.
- The correction applies only `round(T - R)` to an existing native argument;
  it never replaces caller-local arithmetic or caches an offset.
- Values saved between songs become active through the existing game and tune
  initialization paths.

Every other relevant value is a fixed supported-cabinet constant and is not a
bridge input:

- `HoldSafeFrame = 0`;
- `SlideHoldSafeFrame = 0`;
- `ScratchEnableTime = 250` milliseconds; and
- `BeatEnableTime = 200` milliseconds.

The bridge adds no file reads, watchers, generations, callbacks, controls, or
derived calculations for these constants.

## Selected Architecture

The retained data flow is:

```text
published physical input snapshot
    -> timestamped transition journal
    -> QPC-associated gameplay song-time anchor
    -> mapped pending transitions
    -> immutable judgement sample at R
    -> authoritative native-query contracts
    -> unchanged native note handler
```

The high-FPS correction owns only the time-corrected input view. Native code
continues to own the current descriptor, recognition time, note iteration,
state transitions, result encoding, sounds, effects, and long-note lifecycle.

### Authority boundary

While a valid high-FPS judgement scope is active:

| Concern | Authority |
|---|---|
| Current note and component | Native game |
| Recognition time and lifecycle | Native game |
| Pressed and held state at `R` | Immutable judgement sample |
| Held age and historical freshness | Authored-60 history |
| Target direction and note rules | Native game |
| Switch transformations | Existing gameplay-only Switch policy applied to the sample |
| Short-note late boundary and grade time | Associated event `T` composed into native arguments |
| Long-note duration and intervals | Native game at recognition time |

The raw native query may be observed for bounded diagnostics, but it cannot
unconditionally override the authoritative sample. Outside an active valid
scope, wrappers retain native behavior.

This refines the phrase "preserve native success": preserve the native
judgement outcome and all native acceptance rules for the correct song-time
sample. It does not permit a raw bit from the wrong catch-up tick to bypass
event routing.

## Transition Delivery and Immutable Samples

Each complete published snapshot records QPC, pre-state, post-state, rises,
falls, sequence, cohort, epoch, and playback generation.

At judgement-core entry for recognition time `R`:

1. map pending records through a valid song-time anchor;
2. leave records with `T > R` pending;
3. include all records with `T <= R` in this immutable sample;
4. expose every distinct ready rise for this core call only;
5. derive held from the final ready post-state;
6. if a control rose and fell entirely between calls, force it held only in the
   sample carrying its rise; and
7. update authored-60 history from the same ordered transitions; and
8. retain the most recent rise backing each currently held control until its
   physical release, as timestamp metadata only.

During catch-up:

```text
R1 < T  -> transition remains pending
R2 >= T -> pressed pulse appears exactly once
R3 > T  -> resulting held/history plus held-rise metadata remain
release -> held-rise metadata is cleared
```

Different logical controls remain independently visible. If the same control
rises more than once before one judgement opportunity, the newest rise supplies
the one boolean pulse and timestamp; older same-control rises coalesce with a
bounded counter. This limitation matches the native boolean query surface.

The immutable sample is shared across all booster-component evaluations in
the core call. Queries are idempotent and never consume or transfer ownership
of an edge.

Held-rise retention does not replay `PressedCurrentNote` or `PressedFreeTap`.
It exists only so a native direction head accepted through the original
authored-60 held-age/freshness rules can bind the same physical `T`. Seeded
already-held controls have no synthetic rise.

## Explicit Native-Query Contracts

Replace the generic held and native-OR-synthetic APIs with typed roles:

```text
PressedCurrentNote
PressedFreeTap
HeldCurrent
HeldAuthoredMinusTwo
HeldAgeAuthored60
DirectionCurrent
```

Selection uses the verified caller RVA, requested gameplay frame, current note
family, booster component, and direction-matcher head/continuation state.

| Role | Effective result |
|---|---|
| `PressedCurrentNote` | Only the current-note-routed pulse for the requested control or approved Switch alias |
| `PressedFreeTap` | Only the free-tap-routed pulse for the requested control or approved Switch alias |
| `HeldCurrent` | Held state in the immutable sample at `R`, including approved Switch held aliases |
| `HeldAuthoredMinusTwo` | Held state exactly two rational authored samples before the current authored sample |
| `HeldAgeAuthored60` | Held age in authored-60 sample units |
| `DirectionCurrent` | Direction derived from the same immutable held snapshot and normalized by native tables |

Verified pressed callers include normal, free tap, beat, scratch, and hold
queries. Verified held callers distinguish direction current-held,
direction `frame - 2`, and hold continuation. Caller and frame shape must both
agree. A runtime shape not present in the audited matrix is a contract anomaly
and cannot count as supported coverage.

### Authored-60 direction forgiveness

Direction matcher `0x5D2E50` observes:

- current held;
- held age with native `<= 1` and `<= 4` comparisons;
- held at `current_frame - 2`; and
- current normalized direction.

The correction projects transitions onto rational 60 Hz sample boundaries.
It does not interpret two or four target-FPS frames as authored history. A new
direction head therefore sees current held true, authored age within the native
limits, and frame-minus-two held false. A pre-held direction retains true
history and does not become a new head. SLIDE HOLD continuation uses only the
current held view required by native code.

The retained held-rise timestamp follows the same native forgiveness: one
fresh in-window contributor may combine with another direction held for up to
the native four-authored-sample limit. The full native held direction is not
reduced to only the fresh contributor. For a non-muted descriptor, if every
fresh contributor belongs at or before its mute boundary, the head is not
exposed as an in-window direction. Muted HIDDEN variants retain their verified
dual-presentation exception.

## Current-Note and Free-Tap Routing

Routing remains linear and uses the one descriptor supplied by native code.
For each ready rise:

1. no current note: expose only to native free tap;
2. muted note: expose the pulse to both the current native note handler and
   the later native free-tap path, exactly as `0x5D68E0` does;
3. `T <= mute_time` (`descriptor + 152`): expose only to free tap;
4. `T > mute_time`: expose only to the current note, including a wrong
   control that must not become an unintended selectable hit sound.

The authoritative pressed contracts enforce this separation for non-muted
notes. Muted notes are the verified native exception: dual presentation is
intentional, but each path still sees the pulse only once.

The verified boundary case `T <= mute_time < R` may force only the
existing native free-tap branch at `0x5D76CE`. It must not call the free-tap
implementation manually or duplicate sounds, effects, mute rules, or flags.

## Gate Candidate and Accepted-Edge Association

A physical timestamp may affect native timing only when the same physical
edge supplies the accepted input.

For families whose late gate precedes their input query:

1. choose one non-consuming compatible candidate using native source priority;
2. bind note identity, component, logical source, cohort, sequence, and `T`;
3. pass that candidate's event-adjusted argument to the native late gate;
4. expose the authoritative sample to the subsequent native query or matcher;
5. confirm that the accepted source is the bound candidate; and
6. allow only that confirmed edge to supply a short-note grade adjustment.

Native source priority is:

- requested center button before Switch direction aliases;
- existing fixed Switch alias order for a button;
- the rise contributing to the accepted effective direction for FLICK and
  SLIDE HOLD, including a rise retained from an earlier high-FPS core while
  the control remains held; and
- first successful native query order for SCRATCH.

A rejected query discards the candidate. An already-held continuation or
long-note body action has no new selected edge. A direction head accepted by
the native authored-60 freshness window retains and selects its original held
rise; it does not substitute recognition time.

Direction matcher output distinguishes a fresh head from authored-history and
continuation acceptance. Only a fresh head can confirm a physical rise.

## Judgement Timing Policy

Recognition time `R` remains authoritative for:

- current-note selection and iteration;
- note state transitions and miss processing;
- long-note stored start/end times;
- hold and slide continuation;
- scratch and beat maintenance intervals;
- duration scoring;
- effects, sounds, and native aggregation.

Event time `T` may affect only:

- current-note versus free-tap routing;
- a confirmed edge's shared late-gate argument; and
- a confirmed edge's short-note grade argument.

For the two native short-note grade callers of `0x5D0E00`:

```text
forwarded_argument = native_argument + round_to_native_ms(T - R)
result = native_grade(forwarded_argument)
```

The helper is invoked once. There is no native-versus-event shadow result or
transition matrix.

Stable hits away from a native grade boundary produce the same GREAT, GOOD, or
MISS result. A hit whose result depended on the original sampling phase may
resolve differently. Because delayed sampling can move toward or away from the
target, boundary changes are bidirectional and are accepted as the consequence
of deterministic physical-event timing. At high FPS these changes should be
rare; abnormal `abs(T - R)` remains diagnosable.

At the late-only gate, `T <= R` means event time can preserve an eligible
physical edge but cannot make a recognition-time acceptance stricter.

## Native Long-Note Semantics

Long-note results remain in native helper `0x5D04F0`. It grades the maintained
coverage interval, effectively clipping actual maintained start/end to chart
start/end and applying the native coverage thresholds and forgiveness.
Intermediate actions are not separately timing-graded.

- **HOLD:** the head uses one button edge for late eligibility. Stored start,
  held continuation, release, and final coverage remain recognition-driven.
- **SLIDE HOLD:** the head uses the full direction freshness contract.
  Continuation uses current held direction; duration and release remain native.
- **SCRATCH:** the first successful direction follows native priority. Each
  later accepted direction must differ from the previous accepted direction
  on that booster and arrive within the fixed 250 ms interval. It refreshes
  the maintained endpoint but receives no individual grade.
- **BEAT:** each ready button pulse refreshes the maintained endpoint under the
  fixed 200 ms interval. It receives no individual grade.
- **DUAL HOLD:** each booster component runs native HOLD logic against the same
  immutable sample. Native coupling finalizes the components coherently; the
  bridge creates no second note or independent lifecycle.

## Locked Switch Compatibility

Within gameplay judgement only:

1. a newly pressed direction may act as the center-button pressed edge on the
   same booster;
2. a real button or any held direction may sustain the same-booster button;
3. either adjacent cardinal may satisfy a diagonal target initially and
   continuously;
4. real buttons, exact diagonals, native cardinal matches, and Arcade behavior
   remain valid; and
5. menu, test mode, binding, raw FastIO, and backend behavior remain unchanged.

Aliases operate independently for the two booster components while every
component observes the same immutable sample. They do not create lanes or
require both physical cardinals for a diagonal.

## Complete Note-Type and Free-Tap Matrix

| ID | Native family | Corrected input/timing contract | Native behavior retained |
|---:|---|---|---|
| 0 | NONE/default | No invented note input; free-tap routing only | Marker and lifecycle |
| 1 | NORMAL | Bound component button/Switch pulse; event late gate and short grade | Candidate state, effects, result encoding |
| 2 | FLICK | Current held, authored age, authored frame-minus-two freshness, effective direction; fresh contributing rise supplies late gate and grade | Target angle, history rules, result handling |
| 3 | HOLD | Bound head button/Switch pulse; current held body | Start/end storage, release, duration grade |
| 4 | SCRATCH | Four authoritative direction pulses; first native-priority head edge | Direction-change state, 250 ms interval, duration grade |
| 5 | BEAT | One authoritative button/Switch pulse per judgement opportunity | 200 ms repeat interval, duration grade |
| 6 | MERRY GO ROUND | NORMAL contract with `T - R` added to the already adjusted native argument | Segment selection and offset |
| 7 | HIDDEN | NORMAL handler plus verified muted dual presentation to native free tap | Variant lifecycle and AD-LIB accounting |
| 8 | HIDDEN2 | NORMAL handler plus verified muted dual presentation to native free tap | Variant lifecycle and AD-LIB accounting |
| 9 | CRITICAL | Both component evaluations share one sample and retain component-local selected edges | Native aggregation |
| 10 | SLIDE HOLD | FLICK-style head contract; current held-direction continuation | Slide lifecycle, release, duration grade |
| 11 | SLIDE COUNTER | No independent input query | Native lifecycle marker |
| 12 | TURN | No independent input query | Native lifecycle marker |
| 13 | SPIN | No independent input query | Native lifecycle marker |
| 14 | FINISH | No independent input query | Native lifecycle marker |
| 15 | DUAL HOLD | Both HOLD component heads share one sample without consumption | Coupled completion and duration grades |
| - | Free tap | Free-tap-routed button/Switch pulse; verified boundary branch only | Eligibility, selectable sound, mute, effects, flags |

Every matrix row is an implementation and verification obligation. Rows with
no independent input query are supported by verified dispatcher and whole-
binary wrapper-xref evidence, not by omission.

## Lifecycle, Reset, and Failure Handling

Activate only for gameplay judgement above 60 FPS with a valid song-time
anchor. Reset and reseed on:

- gameplay activation;
- playback-generation change;
- focus loss;
- device disconnect;
- leaving gameplay; and
- shutdown.

Seed currently held controls as pre-held without producing pressed pulses.
Do not reset for zero audio step, no judgement call in one rendered frame,
multiple catch-up calls, or a core call whose note does not query input.

If transition storage fills, append the newest complete transition, discard
the oldest retained transition, preserve the newest full post-state, increment
an actual transport-loss counter, and emit one rate-limited anomaly. Do not
disable the bridge, delay new input, or attempt rollback.

All hooks require validated RVAs and original bytes, transactional
installation, exception containment, and ordinary post-CRT startup. No work is
added to `DllMain` or an audio callback.

At target FPS 60, do not activate authoritative samples, authored history,
query overrides, free-tap correction, late-gate adjustment, or grade
adjustment. Passive validation logging may remain. The existing Switch patch
retains its independent native 60 FPS behavior.

## Diagnostics

Diagnostics retain meaningful events and summaries, not ordinary per-frame
note observations.

Event records are limited to:

- transition delivery;
- current-note/free-tap routing;
- gate candidate and accepted-edge association;
- selected short-note grade edge;
- recovered input;
- clock or query-contract anomaly; and
- actual transition loss.

Each current-note record includes descriptor `mute_time` (`+152`),
`unmute_time` (`+156`), and `late_limit` (`+160`) so an observed FAST/MISS can
be distinguished from transport loss or an edge-association failure without
per-query logging.

Split counters into:

```text
transport_evictions
mapped_pending_evictions
authored_history_rotations
same_control_coalesces
future_head_observations
contract_anomalies
```

Only transport and mapped-pending eviction imply possible input loss.
Authored-history rotation is expected maintenance and never warns.

Summaries include captured/delivered transitions, exact/rounded anchors,
current queue depths, gate rescues, timing-grade adjustments, Switch aliases,
diagonal acceptance, maximum/average `abs(T - R)`, and counts by note family.
There is no per-hit native/event grade comparison.

## Verification Strategy

Tests must execute an independent model of the native consumer contracts. A
test that only feeds production policy helpers is insufficient.

### Direction reference harness

Reproduce native matcher `0x5D2E50` inputs and predicates independently:

- current held;
- held age `<= 1` and aggregate `<= 4`;
- frame-minus-two negative freshness;
- normalized direction and target acceptance;
- head versus continuation; and
- Switch exact and both adjacent-cardinal diagonal cases.

The current implementation must fail the fresh-head reference cases. The
corrected implementation must match the authored-60 result at target FPS 60,
120, 144, 165, 240, and 360.

### Required behavioral tests

- all note IDs `0..15` and free tap;
- stable GREAT, GOOD, and MISS fixtures away from grade boundaries;
- deterministic event-time results at early and late sampling boundaries;
- nonzero positive and negative `GameTimeOffset` and `JudgTimeOffset`;
- no bridge read of fixed judgement constants;
- exact and rounded song-time anchors;
- multiple catch-up calls with one pulse delivered at first `R >= T`;
- distinct controls in one cohort and same-control coalescing;
- press and release entirely between calls;
- current-note/free-tap boundary separation;
- CRITICAL and DUAL HOLD component visibility without consumption;
- HOLD and SLIDE HOLD head, continuation, release, and completion;
- SCRATCH native priority, any-different-direction continuation, timeout, and
  final duration result;
- BEAT repeated pulses, timeout, and final duration result;
- MERRY segment adjustment;
- activation, focus, disconnect, generation, and shutdown resets;
- real overflow separated from authored-history rotation; and
- diagnostic soak proving meaningful records survive ordinary 240 FPS play.

### Static and runtime gates

1. Add red tests that expose the current frame-discard and native-override
   defects.
2. Pass focused query-contract and native-reference tests.
3. Pass complete Debug and Release builds and test suites.
4. Recheck optimized x86 hook signatures, receivers, cleanup, and caller RVAs.
5. Deploy the verified 32-bit DLL.
6. Run 240 FPS first and inspect the new bounded diagnostics.
7. Exercise short directions, Switch aliases/diagonals, available long-note
   families, CRITICAL, DUAL HOLD, and free tap.
8. Run 60 FPS afterward and confirm the correction is inactive.

Static/build success is not cabinet acceptance.

## Runtime Acceptance Criteria

- No observed short-input loss at 240 FPS.
- FLICK and SLIDE HOLD heads register under Arcade and locked Switch rules.
- Stable judgement feel remains unchanged.
- Sampling-sensitive edge results are deterministic from `T`.
- Long-note start/end and interval behavior remain native.
- No known note path produces a query-contract anomaly.
- No actual transport or mapped-pending eviction occurs during normal play.
- Diagnostic records are not flooded by ordinary note observations.
- `abs(T - R)` remains bounded consistently with target-FPS judgement cadence;
  abnormal hitches remain visible.
- Target FPS 60 produces no authoritative high-FPS correction behavior.

## Rejected Alternatives

- **Patch only the unused frame-minus-two field:** leaves native override,
  routing leakage, edge association, and test gaps intact.
- **Run the direction matcher twice:** risks duplicated query/output side
  effects and does not solve pressed/free-tap temporal authority.
- **Reimplement the direction matcher:** duplicates native target, history,
  continuation, and Switch rules unnecessarily.
- **Run all judgement at synthetic 60 Hz:** adds intentional latency and
  discards the high-FPS native scheduler.
- **Keep unconditional native-true priority:** allows wrong-tick input to bypass
  the song-timed journal.
- **Retime long-note maintenance:** changes fixed scratch, beat, hold, slide,
  and duration mechanics.
- **Compare native and event grades at runtime:** doubles work without changing
  the selected deterministic policy.

## Implementation Boundary

The implementation plan may modify the high-FPS judgement bridge, immutable
sample policy, Switch gameplay query composition, hook context, authored
history, diagnostics, and tests necessary to enforce this contract. It must
not modify native chart scheduling, long-note scoring, fixed judgement
constants, audio callbacks, or unrelated input paths.

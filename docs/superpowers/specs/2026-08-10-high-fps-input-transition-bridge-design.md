> **ARCHIVED FAILED DESIGN — NOT AUTHORITATIVE.** Preserve it only to avoid
> repeating its disproved exact-frame and carry assumptions.

# High-FPS Gameplay Input Transition Bridge Design

**Date:** 2026-08-10
**Status:** Superseded by
[High-FPS Input Judgement Transactions](2026-08-15-high-fps-input-judgement-transactions-design.md)

The runtime-disproved exact-frame mapping and query-driven carry in this
document must be removed or replaced, not retained underneath the successor
design. Physical transition capture, bounded diagnostics, and transactional
hook installation may be reused where their behavior still satisfies the new
contract.

## Context

Groove Coaster represents a gameplay press as a rising edge in a
frame-indexed input history. The native pressed predicate is true when the
requested logical input is held at frame `N` and was not held at frame
`N - 1`. At the native 60 FPS rate, that edge belongs to one 16.67 ms gameplay
frame.

GCLoader now separates physical input capture from the game's register-read
rate. The input worker handles Raw Input and controller changes, combines
physical sources into logical controls, and publishes the current FastIO held
snapshot independently of the game. Runtime diagnostics at 240 FPS establish
that observed physical rises reach the published snapshot, iDmac read path,
GW XIO edge construction, and the game's frame history. Exact-frame native
pressed queries return true. Aggregate telemetry initially suggested that
some history edges occur on a frame for which a relevant gameplay pressed
query is not made, but it did not establish that relationship per edge.

The high-FPS gameplay clock can select a zero, one, or multi-frame step from
the audio-owned song timeline. A leading explanation is that an input change
can be stored against a repeated or already-processed gameplay frame when the
selected step is zero. When gameplay later advances, the held state remains
true but the one-frame rising edge is behind the consumer. The initial trace
motivated the frame bridge, but did not prove that the input-to-gameplay-frame
handoff accounts for each observed miss. The bridge records the step
classification needed to test this mechanism at runtime.

Two 240 FPS runtime candidates disproved that explanation as sufficient. In
the latest run, all 428 post-seed rises became effective frame edges, with no
overflow, fallback, missing-frame lookup, or invariant failure. Allowing an
edge to move to the first query in a four-frame window carried only 36 edges
and improved the symptom only slightly. It also made grading visibly
unstable, because the variable zero-to-three-frame shift changes judgement
time by up to 12.5 ms.

The earlier skipped-query counter measures gaps between outer frames that
contain final pressed calls. It does not prove that an individual edge was
skipped by its relevant native consumer. Query-aware carry is therefore
removed. The diagnostic candidate preserves the committed frame exactly and
accounts each edge as exact-frame delivery, expiry before a relevant query,
or simultaneous-alias coalescing. This distinguishes the wrapper boundary
from downstream note eligibility and grading without changing judgement.

## Binary-backed behavior boundary

The current `game471.exe.i64` decompilation establishes the relevant native
flow:

| Address | Behavior |
|---|---|
| `0x006630B0` | Tune gameplay state machine. The high-FPS song-clock hook selects `Tune+0x14` step, input alignment receives `current + step`, gameplay update runs, then `Tune+0x10` current frame advances by step. |
| `0x00659860` | Aligns the native input manager/history to a requested gameplay frame. |
| `0x0062D980` | Fills newer history frames from the currently sampled state; skipped frames receive the same held mask. |
| `0x0062DFB0` | Native rising-edge predicate for logical inputs `0..9`. |
| `0x00659640` | Gameplay pressed wrapper used by note-specific input logic. |
| `0x00659570` | Gameplay held wrapper used by continuation logic. |
| `0x006401E0` | Runs the gameplay core for every positive step frame, then reads tap/arrange flags and triggers the corresponding sounds. |
| `0x005D68E0` | Millisecond-domain gameplay core. It stores the current gameplay frame and dispatches both note and free-input processing. |
| `0x005D1D50` | Normal-note path. Eligibility gates run before the pressed query; accepted presses are then graded from current milliseconds and note timing fields. |
| `0x005D2040` | Free-input tap routine. It queries inputs 4 and 9 through `0x00659640`, sets tap flags at object offsets 237 and 238, and runs even when no note consumes the input. |
| `0x005D0E00` | Computes the grade from absolute millisecond distance and the configured note thresholds. |

This convergence is the selected integration boundary: a corrected gameplay
edge exposed through the pressed/held wrappers is visible to both free tap
sound behavior and note judgement. The bridge must not deliver an event only
to a note-specific consumer.

## Goals

- Prevent a captured logical rising edge from being stranded on a repeated or
  already-processed gameplay frame at rates above 60 FPS.
- Expose each rising edge only on its assigned target-rate gameplay frame,
  while allowing every native consumer in that frame to observe it.
- Preserve native note matching, millisecond judgement windows, grades,
  long-note state, repeat rules, Switch aliases, hit effects, and selectable
  free tap sounds.
- Keep gameplay presentation and input resolution at the configured target
  rate, including 240 FPS.
- Add no locks, allocation, waits, or per-event logging to the input worker or
  gameplay query paths.
- Fail transactionally at startup and recover without stuck or fabricated
  input from bounded runtime faults.
- Make native 60 FPS the unmodified behavioral reference.

## Non-goals

- Replacing native note judgement with a new timestamp-to-note scorer.
- Emulating exact 60 Hz input quantization at targets above 60 FPS.
- Widening judgement windows or retaining an input until a later gameplay
  frame or future note becomes eligible.
- Stretching `Pressed` across several target-rate frames.
- Changing physical bindings, thresholds, chattering, input poll rate, ASIO or
  WASAPI behavior, or the authoritative song clock.
- Changing menu input semantics.

## Terms

- **Outer frame:** one Tune/render invocation at the configured presentation
  rate.
- **Gameplay frame:** one target-rate frame selected for processing by the
  authoritative song-clock step.
- **Transition:** a combined logical held-state change captured by the input
  worker.
- **Commit:** assigning a transition to a gameplay frame in the loader-owned
  frame history. It removes the transition from the transport journal; it
  does not mean that a note consumed the input.
- **Broadcast edge:** a pressed bit visible to every gameplay query made for
  its committed frame. Repeated queries in the same frame see the same value,
  matching the native pure predicate.

## Activation contract

### Native 60 FPS

At `target_fps = 60`, the high-FPS transition journal, target-frame mapper,
and corrected gameplay query behavior are not activated. No input return
value, frame assignment, held state, or timing is changed. Existing unrelated
features such as the selected audio backend and optional Switch input style
remain independent.

Passive cadence and input diagnostics may be installed for validation. They
must call the native functions, preserve every return value, and perform no
behavioral correction.

### Targets above 60 FPS

The complete transition bridge is required. Its producer, frame timeline,
query integration, and required hook contracts install as one transaction.
Signature mismatch, invalid derived state, or hook creation failure rolls back
the complete bridge and reports a fatal startup error. The process never runs
with only part of the correction active.

## Selected architecture

### Combined logical transition journal

The existing input worker is the single producer. After keyboard, controller,
axis/button overlap, hysteresis, foreground, and disconnect rules have been
resolved, it compares the new ten-input logical held mask with the previously
published mask. A change appends one record containing:

- monotonically increasing sequence;
- QPC timestamp;
- input epoch;
- resulting ten-bit held mask;
- rising and falling masks for the change.

The current 32-bit FastIO snapshot publication remains unchanged for native
register reads and menus.

The journal is a fixed-capacity 1024-entry SPSC ring. Producer and consumer
indices use acquire/release atomics. Records, storage, and counters have
process lifetime; the hot path performs no allocation, locking, formatting,
or logging. Only changes to the combined logical mask create records, so
controller reports that do not cross a logical threshold do not create load.

### Target-frame mapper

The gameplay thread is the single consumer. A required Tune call-site hook runs
after the active clock plan has selected `Tune+0x14` step and immediately
before native input alignment and gameplay judgement. It reads the current
gameplay frame and final selected step. This boundary works with an exact
ASIO/WASAPI-owned song clock and with clock plans whose normal step remains
one; input delivery is not coupled to a particular audio backend.

- For `step == 0`, no gameplay frame will be processed. The mapper commits no
  transition and leaves pending records in chronological order.
- For `step == 1`, pending records are committed to the one newly processed
  frame.
- For `step > 1`, the mapper creates every intermediate frame and distributes
  records chronologically across the selected range.

QPC orders events inside a step range but never changes song time or the
number of processed frames. The mapper keeps the QPC boundary of the previous
processed frame and partitions the elapsed interval monotonically over the
positive step count. An older pending record clamps to the first new frame; a
record observed at the consumer boundary clamps to the last new frame. This
keeps event placement deterministic for zero-step deferral and catch-up while
leaving the audio clock authoritative.

Each committed frame stores:

- gameplay frame number and epoch;
- normalized held mask;
- rising mask derived as `held[N] & ~held[N - 1]`;
- falling mask derived from adjacent normalized held masks;
- source sequence range.

A down and up assigned to the same target frame is normalized into a one-frame
held pulse, with release applied on the next processed frame. This preserves
the native invariant that a pressed input is also held in that frame while
retaining a captured short tap. It extends such an exceptional sub-frame tap
by at most one target frame, which is no longer than native 60 FPS sampling.
Multiple native queries for the same input and frame all see the same derived
rising bit; the bit disappears on the next frame unless another rise is
committed.

### Loader-owned gameplay frame history

The bridge owns a fixed 64-entry ring of committed target frames. Startup
verifies that the configured rate and clock catch-up policy cannot request a
positive step larger than this retained range. It does not rewrite the
executable's native input-history memory. This isolates the correction from
private object layout and makes frame assignment independently testable.

The already-owned gameplay pressed and held wrapper hooks become the query
boundary:

- at 60 FPS or outside an active high-FPS gameplay epoch, they preserve the
  existing native/Switch behavior;
- for the verified local gameplay input device and a committed target frame,
  pressed and held values come from the loader history;
- Switch direction aliases operate on these effective values, so Arcade and
  Switch styles share the same corrected timeline;
- unsupported device IDs or non-gameplay calls retain native behavior.

The effective pressed query is intentionally frame-visible, not
consumer-destructive. The free tap routine and note-specific logic can both
observe the same edge during the same native gameplay-core invocation. The
event is never moved to a later frame, so the query layer adds no further
millisecond shift and cannot activate a later note.

When gameplay advances past a committed edge that was never queried for its
logical input, the bridge records one expiry. It also records the affected
logical input in a ten-counter vector. Exact-frame delivery and intentional
simultaneous-direction alias coalescing have separate counters, so their sum
can be reconciled against effective edges without per-event logging. This
accounting is observational and does not alter any return value.

### Gameplay epoch

The bridge begins a new epoch on gameplay initialization and on any
authoritative audio playback-generation change. Focus loss, device disconnect,
gameplay inactivity, song restart, or clock reset invalidates pending records
from the old epoch. The new epoch seeds its held mask from the current atomic
snapshot without creating rising bits. A key already held while a song starts
therefore remains held but cannot create an automatic hit.

## Failure and recovery policy

### Queue overflow

Overflow should be unreachable during normal human input, but it is handled
without blocking or corrupting indices. The producer increments an overflow
generation and continues publishing the latest atomic held snapshot. At the
next gameplay consumer call, the bridge:

1. discards untrusted queued transitions;
2. begins a new input epoch;
3. seeds held state from the current snapshot without rising bits; and
4. emits one rate-limited diagnostic summary.

This may lose the transition occurring during the fault, but it cannot leave a
key stuck, duplicate an edge, or fabricate a hit.

### Focus, device, and gameplay resets

- Focus loss clears pending rising edges and releases keyboard-derived held
  state through the existing input-worker rules.
- Controller disconnect clears controller-derived state without releasing an
  overlapping keyboard source.
- Gameplay inactivity or playback-generation changes discard transitions that
  must not replay after a pause, menu, restart, or backend reset.
- Frame lookup outside the active committed range falls back to native behavior
  and increments a bounded invariant counter. Expected non-gameplay fallbacks
  are distinguished from active-gameplay misses in diagnostics.

### Exceptions and logging

Hook entry points are `noexcept` and preserve native fallback behavior after
unexpected internal exceptions. Hot paths update atomics only. Startup logs
the active mode and capacities; periodic existing framerate telemetry emits a
compact bridge summary. No transition is logged individually.

## Diagnostics

The periodic summary includes interval and cumulative values for:

- captured logical rises and falls;
- journal enqueues and committed records;
- `step == 0`, `step == 1`, and `step > 1` consumer calls;
- edges deferred across zero-step calls;
- committed frame count and maximum positive step;
- native history edges and effective bridge edges;
- exact-frame deliveries, unqueried expiries by logical input, and intentional
  simultaneous-alias coalescing;
- native and final pressed-query successes;
- free-tap flag activations for both sides;
- held-query successes;
- maximum queue depth and event age;
- epoch resets, overflows, and active-gameplay native fallbacks; and
- duplicate-frame or missing-frame invariant failures.

The detailed diagnostic ring remains bounded. A complete song must be
diagnosable without per-input log spam.

## Automated verification

### Pure journal and mapper tests

- ordered enqueue/dequeue and ring wraparound;
- simultaneous logical changes and chords;
- overlapping physical sources producing only combined logical transitions;
- press, hold, release, and repeat-free steady held state;
- a down/up pair inside one target frame producing one held/pressed frame and
  a release on the next processed frame;
- zero-step deferral followed by one positive frame;
- repeated zero steps without duplicate commits;
- multi-frame catch-up with deterministic chronological placement;
- query repetition inside one frame and disappearance on the next frame;
- skipped pressed-query frames never moving an edge to a later frame;
- exact accounting of an edge that expires before any matching query;
- a short tap never being retimed to a later judgement frame;
- simultaneous Switch direction aliases coalescing into one booster tap;
- epoch reset seeding held state without a synthetic rise; and
- overflow resynchronization without stuck, duplicated, or fabricated input.

### Policy and hook tests

- `target_fps = 60` builds no behavioral bridge plan;
- targets above 60 require every bridge contract;
- signature or hook-creation failure rolls back all bridge hooks;
- Arcade and Switch aliases consume the same effective frame history;
- free tap and note queries can both see one committed edge;
- unknown device and non-gameplay calls preserve native behavior;
- active-gameplay missing-frame lookup is counted and falls back safely; and
- query hooks allocate nothing and preserve exception boundaries.

### Timing and performance tests

- QPC partition arithmetic uses checked 64-bit operations;
- event ordering remains stable at 120, 144, 165, 240, and 360 FPS;
- the bridge adds no extra gameplay step: a pending event is exposed on the
  first positive step selected after it is available to the consumer;
- synthetic high-rate producer stress does not corrupt the ring; and
- the full x86 Debug and Release test suites pass with diagnostics both enabled
  and disabled where supported.

Automated verification proves the bridge contract and hot-path behavior. It
does not claim subjective gameplay acceptance.

## Runtime validation order

The operator will run the deployed candidate in this order:

1. Configure and run 240 FPS first.
2. Exercise empty chart sections/free tap sounds, short notes, long-note heads
   and bodies, chords, and dense passages.
3. If any 240 FPS issue is observed, stop the comparison and report the issue
   with the current logs. Analyze and correct that run before requesting the
   60 FPS reference.
4. Only after the 240 FPS run is acceptable, configure 60 FPS and run the
   native no-op reference with passive diagnostics.
5. Compare the independently identifiable 240 and 60 log sessions.

The absence of a 60 FPS run after a failed 240 FPS attempt is expected and must
not be treated as incomplete operator validation.

## Manual acceptance

### 240 FPS candidate

- Every free press produces exactly one selected tap sound.
- Short notes and long-note heads do not exhibit unexplained missing edges.
- An active long note remains held until the actual release.
- Chords and dense passages produce no duplicate or stuck inputs.
- Native millisecond judgement windows and grade behavior remain unchanged.
- Logs show no overflow, active-gameplay fallback, missing committed frame, or
  duplicate-frame invariant failure. Carry remains zero; delivery, expiry,
  and coalescing account the effective edges at the query boundary.
- Queue depth and event age remain bounded with no visible frame-pacing or
  audio-deadline regression.

### Native 60 FPS reference

- The bridge reports inactive/no-op behavior.
- Passive hooks preserve native query results.
- Free tap sounds, notes, holds, and judgement retain native behavior.
- The log provides a comparison baseline without claiming that the high-FPS
  correction ran at 60 FPS.

Runtime acceptance belongs to the operator. Build success and automated tests
must be reported separately from the user's observed gameplay result.

## Source and deployment boundary

Source, tests, documentation, commits, and build artifacts belong in
`H:\gc\artifacts\GCLoader`. `H:\gc` is the runtime/deployment tree. Deployment
occurs only when explicitly requested and preserves operator-owned
configuration. The user launches the game and supplies runtime observations
and logs.

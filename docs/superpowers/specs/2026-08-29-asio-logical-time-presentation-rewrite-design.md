# ASIO Logical-Time Presentation Rewrite Design

**Date:** 2026-08-29

**Status:** Approved for implementation

**Scope:** Absolute judgement time, logical DirectSound playback, ASIO physical
presentation, focus suspension, and physical-session recovery

## Authority and supersession

This document is the sole design authority for the ASIO timing and recovery
rewrite. It supersedes the ASIO-specific timing and recovery decisions in:

- `2026-08-22-asio-absolute-time-judgement-design.md`;
- `2026-08-25-asio-focus-recovery-design.md`;
- `2026-08-28-asio-persistent-timeline-recovery-design.md`;
- `2026-08-22-asio-absolute-time-judgement.md`;
- `2026-08-25-asio-focus-recovery.md`;
- `2026-08-27-asio-session-recovery.md`; and
- `2026-08-28-asio-persistent-timeline-recovery.md`.

Those documents remain historical evidence. They are not implementation
authority and must not be used to restore callback-derived judgement,
synthetic detached anchors, or one-time physical-to-logical attachment.

The generic absolute-input capture, native recognition/score hooks, guarded
patch rules, and accepted WASAPI behavior remain authoritative unless this
document explicitly changes their dependency on ASIO physical output.

No production implementation begins until this written specification is
reviewed. After review, a separate implementation plan will identify exact
file changes and commits.

## Problem statement

The original ASIO path could sound and judge consistently during an
uninterrupted physical session, but it made absolute judgement depend on ASIO
callback anchors, callback history, and submitted output progress. Focus loss
therefore removed information required to finalize judgement and exposed
endpoint-generation, history-loss, backlog, song-transition, and lifetime
failures.

Successive recovery designs addressed those failures separately:

1. callback-anchor history projected input through whichever physical
   callbacks were available;
2. detached rendering published synthetic anchors while no physical device
   existed;
3. callback interpolation or newest-anchor extrapolation allowed query timing
   to affect the resolved event coordinate; and
4. the persistent logical-timeline design removed callback dependence from
   judgement, but attached the host timeline to the ASIO sample timeline only
   once and never reconciled their independent rates afterward.

The fourth design currently computes judgement from a host-derived logical
timeline while rendering ASIO from session-local sample-position deltas. A
one-time affine origin does not make the Windows multimedia clock and the
audio-interface oscillator the same clock. Constant attachment error can
change perceived offset, and rate error can accumulate across a song or
credit. Later callback timestamps are diagnostic only, so the design has no
mechanism that can restore physical alignment.

This is not an ASIO-driver defect and cannot be repaired by another judgement
offset, callback tolerance, history window, or recovery retry. The clock-domain
boundary itself must be rewritten.

## Why the cited projects do not solve this problem

The experimental osu-framework ASIO pull request lets each ASIO callback pull
the next frames directly from one BASS decode mixer. Device consumption and
mixer progression are one stream during normal operation. Its discussion
explicitly leaves alt-tab mode switching as an unresolved concern; it does not
provide continuous judgement while the physical ASIO session is absent.

KeyASIO is external middleware. Its hardware-mix mode owns hitsounds while osu
owns music on another device; its full mode replaces osu audio externally. It
does not own osu's native judgement clock, and its documentation warns that
exclusive-device ownership can desynchronize osu's internal clock.

GCLoader must satisfy a different combination of contracts: preserve a closed
source game's DirectSound behavior and song sequence, capture high-rate input
outside game-frame cadence, keep judgement time alive while audio is absent,
and replace an exclusive physical session without falling back or pausing the
game. Borrowing another project's normal callback loop does not solve those
additional contracts.

## Non-negotiable requirements

1. Judgement is based on the captured absolute input timestamp.
2. The resolved hit time and authored note time are expressed in the same
   logical song/source timeline before native recognition and grading compare
   them.
3. Once an input transition has been mapped into logical song time, later
   callback delivery, focus changes, rendering, recovery, or query timing can
   never change it.
4. ASIO callbacks, sample positions, submitted tails, physical-session
   generations, buffer indices, and device availability never participate in
   judgement resolution or judgement finality.
5. No callback bracketing, interpolation, newest-anchor extrapolation, or
   synthetic detached callback anchor is permitted in the judgement path.
6. The logical song timeline and game-facing DirectSound cursor continue while
   focus is lost. Audio may be silent during that interval.
7. Physical audio must be made to follow the logical song timeline. A
   one-time offset between independent clocks is insufficient.
8. The ASIO driver owns the nominal physical sample rate. The implementation
   must support the driver's accepted integral rate, including 44.1 and 48 kHz,
   without a fixed-rate assumption.
9. Driver output latency and any presentation-bridge group delay are accounted
   exactly once in physical presentation. They are not judgement offsets.
10. `GameTimeOffset` retains its existing game/audio alignment semantics.
    `JudgTimeOffset` remains native grade-only correction. Neither setting may
    hide an ASIO clock-domain error.
11. Focus state, not elapsed time or callback silence, authorizes suspension
    and focus recovery.
12. ASIO never falls back to WASAPI or DirectSound.
13. Recovery permits one immediate attempt and at most two delayed retries.
    Unexpected instability after a physical session is committed as running is
    fatal.
14. The game may not continue in the foreground after ASIO has failed or the
    presentation bridge has lost its timing contract.
15. No new per-callback logging, allocation, blocking lock, or unbounded work
    is introduced.
16. Automated verification must have an independently derived behavioral
    oracle. Source-grep, copied tables, and test-local restatements are not
    accepted as tests.

## Clock domains and canonical coordinates

The rewrite names four different coordinates and prevents accidental
substitution between them.

### Absolute host time `H`

`H` is captured at input observation and semantic-stage events. It preserves
the existing paired host timestamp contract and wrap-safe multimedia-clock
handling. It is available independently of every audio callback.

For the ASIO logical timeline, `H` uses the captured multimedia-clock member,
which is the Windows domain required for ASIO `systemTime`. The input value is
captured at observation; it is never reconstructed later from a callback. The
paired QPC member remains available to the accepted WASAPI route and transport
diagnostics, but the two members are not mixed within one stage projection.

### Logical output coordinate `L`

`L(H)` is a persistent, monotonic rational output-frame coordinate derived
only from absolute host time and the logical output rate. It belongs to the
logical audio engine, not to a WASAPI or ASIO physical session.

For a fixed logical epoch:

```text
L(H) = logical_origin
     + logical_rate * (H - host_origin)
```

The calculation uses checked rational arithmetic. Integer frames are obtained
only at APIs that explicitly require a whole render or DirectSound cursor
coordinate.

### Authored source/song coordinate `C`

Logical playback epochs map `L` into the source buffer's authored coordinate:

```text
C(H) = source_origin
     + (L(H) - logical_output_origin) * source_rate / logical_rate
```

Play, seek, loop, and natural-end transitions create or close logical playback
epochs. They are logical DirectSound/mixer events, not physical endpoint
events.

After applying the existing `GameTimeOffset` ownership exactly once, `C(H)` is
the time passed to native recognition. Authored note target time is already in
that same source/song domain. `JudgTimeOffset` remains inside the native grade
calculation and is not part of this projection.

### Physical ASIO coordinate `S`

`S` is the session-local ASIO `samplePosition`. It may restart or change origin
whenever a physical session is recreated. ASIO `systemTime` identifies the
absolute host time associated with `S`, and `ASIOGetLatencies()` identifies
when the buffer currently being filled will begin to sound.

`S` is used only inside the physical presentation adapter for structural
continuity and audio clock-domain tracking. It is never converted into a
judgement timestamp.

## Required data flow

### Absolute judgement

```text
captured absolute input timestamp H
              |
              v
     persistent logical frame L(H)
              |
              v
 logical playback epoch: L -> source/song C
              |
              v
 native recognition and grade against authored note Cnote
```

This path has no ASIO callback edge. A physical session can disappear between
capture and resolution without changing the result.

The projection from `H` through a fixed logical epoch is exact rational clock
conversion. It is not interpolation between game frames or audio callbacks.

### ASIO physical presentation

```text
logical mixer stream tagged in L
              |
              v
 ASIO presentation bridge / audio rate matcher
              |
              v
 physical ASIO buffers identified by S and systemTime
```

At a valid callback with absolute `systemTime = A`, driver output latency
`D`, and the rate matcher's known source-phase compensation `G`, the first
audible sample being prepared must correspond to the logical coordinate for
its predicted presentation time:

```text
target_logical_phase = L(A) + D + G
```

`D` and `G` are expressed in logical frames using checked rate conversion.
`G` is taken from the selected production rate matcher's documented latency;
its sign is fixed by that API's source/output phase convention and is verified
by an impulse-position contract test rather than guessed. These values affect
which audio content is placed in a physical buffer; they never alter `L(H)` or
`C(H)` for judgement.

The callback thread's wall-clock entry time is not presentation evidence. A
late thread dispatch or game-frame stall must not shift the requested song
interval when the driver supplied valid `systemTime` and `samplePosition`.

## Architecture

### 1. Persistent logical presentation clock

A backend-lifetime logical clock replaces ASIO-specific exact-clock ownership.
It:

- unwraps the selected absolute host-time domain;
- projects any captured timestamp into `L` without consulting render progress;
- exposes one stable logical generation for the backend lifetime;
- survives every physical ASIO session replacement; and
- has no IASIO, callback, buffer, submitted-tail, focus, or retry dependency.

The current host-derived projection logic in `AsioLogicalTimeline` may be
adapted, but the resulting component is common logical-audio infrastructure,
not an ASIO clock. `ExactAsioClock` must not remain the conceptual owner of
judgement time.

### 2. Logical playback history

The useful part of `AudioCursorTimeline` is retained: it records exact logical
output-to-source epochs for Play, Seek, Loop, and natural end. Its exact
history is rewritten so that:

- epochs are keyed by the persistent logical generation rather than a physical
  endpoint generation;
- mapped tails describe logical mixer/source coverage, not physical submitted
  output;
- focus loss and physical-session replacement do not close an epoch;
- a game Play or Seek can change playback generation; ASIO recovery cannot;
- history readiness cannot depend on an ASIO callback; and
- stage binding can fail only for real logical history loss or contradictory
  game playback transitions.

This preserves the information needed to express both input and notes in
source time without preserving physical endpoint coupling.

### 3. Backend-independent judgement resolver boundary

`JudgementClockResolver` is changed from a concrete physical-output-endpoint
consumer into a consumer of an exact judgement-timeline interface. The ASIO
implementation of that interface is the persistent logical clock and has no
physical ASIO dependency. The accepted WASAPI implementation is adapted to the
interface without changing its existing clock projection or pacing behavior.

For ASIO, the bound stage anchor contains:

- semantic stage generation;
- persistent judgement-timeline generation;
- selected buffer instance and playback generation;
- logical output origin and source origin;
- logical and source rates; and
- the existing configured `GameTimeOffset`.

It does not contain an ASIO endpoint object or physical-session generation.
After binding, ASIO `Resolve(H)` is a pure checked logical projection. The only
normal pending state is that no eligible logical playback epoch exists yet at
stage entry. Physical ASIO output progress cannot create pending work.

Absolute input history, semantic-stage boundaries, native recognition, held
state, sound dispatch, scoring, and grade behavior remain otherwise unchanged.

### 4. Sequential logical render stream

The mixer exposes one sequential logical stream in `L`. Exactly one render
owner may advance it at a time:

- the ASIO presentation bridge while a physical session is committed; or
- the suspension pump while no physical session exists.

The stream may internally render fixed miniaudio periods and retain a bounded,
preallocated remainder for the presentation rate matcher. It may not be
rewound, rendered concurrently, or silently skip/duplicate a logical interval
after the session is running.

Ownership transfer is an explicit transaction. The new owner starts at the
exact logical tail committed by the previous owner. A failed transfer leaves
the physical session silent and uncommitted.

### 5. ASIO presentation bridge

The bridge is physical-session-owned and is the only component allowed to
combine logical time with ASIO timing. It contains:

- ASIO callback validation;
- session-local sample-position tracking;
- driver `systemTime` validation;
- driver output latency;
- a preallocated final-output audio rate matcher;
- logical source phase for the next physical output sample; and
- bounded phase/rate controller state.

The bridge has two timing modes.

#### Priming mode

Startup and recovery callbacks output silence while callback structure and
time information are validated. Large phase correction is allowed only here:
the rate matcher is reset and its logical source phase is aligned directly to
the predicted presentation coordinate. Priming does not advance judgement or
publish a synthetic judgement anchor.

The physical session is committed only after the existing finite callback
proof and a successful logical-render ownership handoff. The first audible
buffer starts at the exact next logical stream coordinate chosen for its
predicted presentation time.

Recovery selects a future physical presentation boundary whose target logical
coordinate is not earlier than the suspension pump's committed tail. ASIO
continues to emit priming silence while the suspension pump advances to that
coordinate. The pump then commits that exact tail, the bridge accepts ownership
at the same coordinate, and only the corresponding future ASIO buffer becomes
audible. Recovery never rewinds the mixer or plays missed background audio at
an accelerated rate.

#### Tracking mode

Once running, each callback:

1. validates buffer alternation, sample-position delta, time flags, and
   session generation;
2. determines the logical phase that should be audible for that physical
   buffer;
3. compares it with the bridge's current logical source phase;
4. applies a bounded, smooth rate-ratio correction to the final mixed audio;
5. pulls only the next sequential logical frames; and
6. fills exactly the driver-requested physical frame count.

This is asynchronous audio clock-domain conversion. It changes only the
sampling of the final audio waveform so that a host-timed logical stream is
presented by a separately clocked device. It never changes a judgement
timestamp, logical playback epoch, note target, or game cursor.

The implementation may use the existing miniaudio dependency's runtime
resampler-rate support, but the rate matcher must be a final-output component.
Changing every source voice's converter would mix physical clock correction
into source/playback history and is prohibited.

Tracking has a documented finite phase envelope and rate-ratio envelope based
on the accepted driver rate, ASIO period, bridge capacity, and observed normal
oscillator error. The implementation plan will derive the concrete constants
from the existing Xonar callback evidence and verify them with independent
clock-rate simulations. They are compile-time policy, not user timing knobs.

No hard phase reset, block skip, block repeat, or source seek is allowed in
tracking mode. Exceeding the envelope, exhausting logical input, overrunning
the bounded bridge, or failing audio conversion is a fatal runtime contract
failure.

### 6. Game-facing DirectSound cursor

For ASIO, `CurrentOutputFrame()` becomes the whole logical coordinate
`floor(L(now))`, independent of physical presentation, submitted ASIO buffers,
and an asynchronously sampled logical-render tail. `GetCurrentPosition`,
`GetStatus`, drain completion, and song-end behavior resolve from the logical
playback history at that coordinate.

The active render owner remains required to advance the logical stream
sequentially. An actual planning, render, commit, ownership, bridge-underflow,
or pump failure is fatal. A game-thread cursor read cannot infer such a failure
from a momentary `committed_tail <= floor(L(now))` snapshot because that read is
not synchronized with the callback or suspension pump. This keeps normal
ranking/demo and second-song transitions independent of both physical output
and callback scheduling races.

Physical presented position remains available only to the ASIO bridge and
aggregate diagnostics. It must not be returned through the DirectSound facade
or used to decide game sequence.

### 7. ASIO lifecycle controller

The logical engine and the physical session have separate lifetimes:

| State | Logical clock/history | Render owner | Physical output |
|---|---|---|---|
| `Starting` | Constructing | None | Silent/uncommitted |
| `Running` | Advancing | ASIO bridge | Audible |
| `Suspended` | Advancing | Suspension pump | Absent |
| `Recovering` | Advancing | Suspension pump until handoff | Priming silence |
| `Fatal` | No longer usable | None | Stopped |
| `Stopping` | Quiescing | None after transfer | Released |

Focus loss can arrive during initial acquisition, priming, running, or
recovery. The serialized control state records the desired foreground state
and completes or unwinds the current physical transaction without changing
logical time.

#### Initial startup

- Initial startup has no alternate backend.
- Driver capability discovery adopts the driver's accepted integral sample
  rate before the logical audio contract is committed.
- Initial physical acquisition proceeds through its first `Running` commit even
  when the initial foreground snapshot is false or a focus-loss edge arrives
  during acquisition. The serialized controller records the desired background
  state but may not park the attempt before synchronous backend startup has
  completed. Immediately after startup commits, a still-background request
  enters `Suspended` through the normal quiesce, lease-transfer, and release
  transaction. This both validates the driver-owned rate and prevents startup
  from waiting on a foreground transition that the blocked game thread cannot
  complete.
- A real initial acquisition failure fails backend startup. The game does not
  continue under a silently broken ASIO selection.

#### Focus suspension

On an explicit focus-loss edge:

1. the physical session stops accepting new audible render work;
2. callback quiescence is proven;
3. logical render ownership transfers to the suspension pump at the committed
   logical tail;
4. IASIO buffers and the physical session are released; and
5. the logical clock, mixer voices, logical playback history, DirectSound
   cursor, and judgement binding continue.

The suspension pump advances/discards logical audio according to `L(now)`. It
does not fabricate callback timestamps or physical presentation.

#### Focus recovery

On an explicit foreground-regained edge:

1. create a completely fresh physical session and callback generation;
2. require the same committed logical output-rate contract;
3. prime and validate callbacks with silence;
4. align a fresh bridge to the current logical presentation coordinate;
5. transactionally transfer the sequential logical render stream from the
   suspension pump to the bridge; and
6. commit `Running` only after the first audible logical interval is ready.

No old `samplePosition`, physical origin, bridge phase, or callback anchor is
retained. The only continuity is the logical stream coordinate.

Recovery allows one immediate acquisition attempt, one retry after one second,
and one final retry after two additional seconds. Retry waits are interruptible
by a new focus edge or shutdown. Only complete, clean failures before the new
session is committed are retryable. Cleanup must prove that no callback,
buffer, driver object, or render ownership remains from the failed attempt.

## Error policy

### Expected lifecycle events

An explicit focus-loss edge permits silent suspension and physical-session
release. Missing audio while backgrounded is expected. Logical time and game
sequence continue.

### Retryable recovery acquisition failures

Only failures before `Running` may consume the bounded focus-recovery retry
schedule. Each attempt starts from a fully released physical state.

### Fatal failures

The following fail immediately after a session is committed:

- callback overlap or use after quiescence;
- invalid buffer index or alternation;
- repeated, regressed, or incorrectly stepped sample position;
- invalid required ASIO time information;
- unexpected sample-rate, latency, reset, resync, or buffer-size change;
- bridge phase/rate envelope violation;
- logical render underrun or bridge overrun;
- conversion failure, non-finite output, or invalid render contract;
- loss of sequential logical render ownership;
- logical clock/history contradiction; or
- failure to preserve the DirectSound ABI or initialized caller outputs.

A foreground fatal failure cannot degrade into silence while the game
continues, cannot trigger fallback, and cannot be disguised as focus loss.

## Threading, ownership, and real-time constraints

- The logical clock and logical playback history outlive every physical ASIO
  session.
- IASIO, driver buffers, callback state, bridge state, and physical generation
  are owned by one physical session.
- Callback function tables and actions are owned by value for at least the
  complete callback lifetime.
- The control thread owns physical construction, `Start`, stop, cleanup, focus
  transitions, and retries.
- Mixer/render ownership is explicit and exclusive between the bridge and
  suspension pump.
- The callback performs only bounded validation, rate matching, conversion,
  counter updates, and buffer publication using preallocated storage.
- No callback path allocates, logs, waits on the game thread, opens files,
  touches configuration, or performs device lifecycle work.
- Aggregate counters may be sampled by the existing observer outside the hot
  path.

## Diagnostics required for verification

Diagnostics support verification but are not the fix. The hot path records
only counters and bounded extrema. A startup, recovery, stage-end, fatal, or
shutdown summary may report:

- logical clock generation and rate;
- physical session generation, driver rate, period, and output latency;
- priming callbacks and handoff logical coordinate;
- initial, maximum, and final physical-to-logical phase error;
- minimum and maximum audio rate-match ratio;
- bridge underflow/overflow counts, which must remain zero;
- suspension advancement and recovery count;
- unexpected ASIO messages and structural violations; and
- judgement resolver counts proving that physical callback state is absent
  from resolved/pending/failure reasons.

No timestamp residual is allowed to re-anchor either logical time or a
previously mapped input.

## Rejected approaches

### Restore callback-derived judgement

Rejected. It can reproduce the old uninterrupted-session feel, but callback
loss again makes judgement pending and makes physical session lifetime part of
stage continuity.

### Interpolate or extrapolate judgement from callback anchors

Rejected. The answer can depend on which callbacks exist when an event is
queried, and focus loss removes the required evidence. Absolute input must be
projected from its captured timestamp through a fixed logical epoch instead.

### Publish detached or synthetic physical anchors

Rejected. No physical presentation exists while ASIO is absent. Inventing one
conflates logical continuation with hardware evidence and recreates endpoint
lifecycle failures.

### Attach physical sample position once to host time

Rejected. Independent clock frequencies are not made equal by sharing an
origin. Diagnostic residuals do not correct audible phase or drift.

### Correct drift by skipping or repeating complete blocks

Rejected. It creates audible discontinuities and can advance mixer/source
state inconsistently. Normal clock disagreement is handled only by the bounded
final-output audio rate matcher.

### Pause logical song or judgement time while ASIO is absent

Rejected. The game cannot reliably recover its stage, note, and sequence state
if the judgement timer is lost.

### Use focus duration or callback timeout as focus evidence

Rejected. Time may schedule work and retries, but only explicit focus state
authorizes lifecycle transitions.

### Fall back to WASAPI or DirectSound

Rejected. Configured ASIO either runs correctly, remains explicitly suspended
for focus loss, or fails.

## Implementation boundary

The rewrite is expected to replace or substantially refactor:

- ASIO-specific ownership in `ExactAsioClock`;
- physical endpoint dependency in `JudgementClockResolver`;
- physical endpoint generation in exact logical playback history;
- the one-time mapping in `AsioLogicalRenderSequencer`;
- ASIO's physical `CurrentOutputFrame()` implementation; and
- callback rendering that assumes one physical frame always equals one
  independently derived logical frame.

The rewrite should retain where their contracts remain valid:

- absolute input timestamp capture and bounded history;
- semantic-stage and native recognition/score hooks;
- DirectSound COM surface and caller-visible ABI;
- source snapshots and miniaudio voice graph;
- exact Play/Seek/source epoch information after moving it to logical
  generation ownership;
- ASIO driver discovery, capability validation, sample conversion, and
  callback safety infrastructure; and
- foreground publication and bounded retry policy after removing clock
  ownership from it.

No compatibility shim may feed old callback anchors into the new judgement
resolver. Intermediate commits may keep both implementations compiling, but
only one complete clock model may be selectable in a runtime artifact.

The accepted WASAPI runtime path is the behavioral comparison baseline and is
not redesigned as part of this ASIO task. Shared judgement interfaces may be
clarified only when required to remove ASIO physical state from the resolver.
The WASAPI provider's projection, output clock ownership, cursor behavior, and
pacing must otherwise remain unchanged and require focused compatibility
verification.

## Verification strategy

### Behavioral contract tests

Tests are added only where they have an independent oracle and protect a real
failure class.

1. **Callback-independent judgement projection**

   Construct a logical clock and playback epoch from independently calculated
   rational values. Resolve the same captured timestamps before and after
   arbitrary focus/session/callback activity. The exact source coordinates
   must remain identical, and no callback count or submitted tail may be an
   input.

2. **Logical Play/Seek/source mapping**

   Exercise real production playback-history publication across Play, Seek,
   Loop, natural end, and a physical-session replacement. Expected source
   positions are derived from authored source rate and logical elapsed time.
   Recovery must not create a playback generation or close an epoch.

3. **Independent-clock convergence**

   Drive the production presentation bridge with deterministic logical and
   physical clocks whose rates intentionally differ in both directions. Over a
   long simulated interval, the bridge must keep phase and bounded-buffer state
   inside policy while consuming sequential logical audio. The judgement
   results for the same host timestamps must be bit-identical with and without
   physical drift. A tagged impulse at a known logical coordinate independently
   verifies the rate matcher's source/output phase convention and that driver
   plus bridge latency is applied once.

4. **Priming and recovery handoff**

   Simulate loss during startup, running, and recovery. Priming may realign
   audio while silent; the first audible recovered sample must correspond to
   the independently calculated current logical presentation coordinate. No
   old session-local sample origin may survive.

5. **Failure classification**

   Verify that clean pre-commit acquisition failures consume only the bounded
   retry schedule, whereas committed-session structural errors and bridge
   underrun/overrun fail immediately. Verify that no failure selects another
   backend.

6. **Game-facing cursor continuity**

   Through the production DirectSound facade, verify that cursor, status,
   drain, and song-end progression continue on logical time during suspension
   and are unchanged by physical session generation.

Tests must call production components. They may not grep source, duplicate
implementation constants as their oracle, or validate a test-only clock helper
that production does not use.

### Static verification

Before runtime handoff:

- build and run focused tests in x86 Debug;
- build and run the full suite in x86 Debug and Release using the VS 18
  Insiders `vcvars32.bat` environment and
  `GC_ASIO_SDK_DIR=H:\gc\artifacts\ASIOSDK`;
- run `git diff --check` and inspect `git status`;
- verify no ASIO callback types or physical-session fields remain in the
  judgement dependency graph;
- verify the callback hot path has no allocation, logging, blocking lifecycle
  action, or unbounded loop;
- verify the configured ASIO path contains no alternate-backend selection;
- inspect the built x86 artifact; and
- hash-compare the candidate and deployed DLL before attributing runtime logs
  to this implementation.

### Runtime acceptance

Static tests cannot prove audio feel, driver behavior, frame pacing, or game
sequence. Runtime acceptance remains with the user and uses the exact deployed
candidate.

The minimum runtime matrix is:

1. foreground startup and a complete multi-song credit;
2. startup beginning while the game is backgrounded, then foreground recovery;
3. startup followed shortly by backgrounding, then foreground recovery;
4. focus loss and regain after startup but before gameplay;
5. focus loss and regain in menus;
6. focus loss and regain during gameplay, accepting temporary silence while
   logical judgement time continues;
7. a full two-song session with no second-song timing drift, loader-caused
   frame drop, end-of-song crash, or ranking/demo sequence stall; and
8. comparison with the accepted WASAPI path on the same physical listening
   chain and unchanged offsets.

Acceptance requires:

- no fallback and no unexpected fatal record;
- no bridge underrun, overrun, hard running-phase reset, skipped logical
  interval, or repeated logical interval;
- recovery only on explicit focus lifecycle;
- stable judgement across songs without a backend-specific compensating
  offset;
- no callback/recovery work visible as a game-frame stall; and
- normal post-credit ranking/demo progression.

Logs may confirm these contracts, but the user's gameplay observation remains
the authority for timing feel, audible continuity after recovery, and frame
pacing.

## Completion criteria

The rewrite is complete only when all of the following are true:

1. judgement has no physical ASIO dependency;
2. captured input and authored note time meet in one logical source timeline;
3. physical ASIO output continuously follows that logical timeline through a
   bounded final-output clock-domain converter;
4. focus loss preserves logical clock, playback history, cursor, and judgement;
5. recovery replaces only physical-session and bridge state;
6. normal running has no block skip/repeat, hidden phase reset, fallback, or
   nonfatal structural failure;
7. meaningful Debug and Release verification passes;
8. the candidate/deployed artifact identity is proven; and
9. the full runtime matrix is accepted by the user.

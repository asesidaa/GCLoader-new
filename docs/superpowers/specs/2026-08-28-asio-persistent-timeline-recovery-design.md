# ASIO Persistent Timeline and Replaceable Session Design

> **Superseded:** This document is historical evidence only. Its one-time
> physical-to-logical attachment and judgement/recovery authority are replaced
> by `2026-08-29-asio-logical-time-presentation-rewrite-design.md`.

**Date:** 2026-08-28

**Status:** Architecture approved in conversation; written review pending

**Scope:** ASIO sample-rate ownership, logical audio and judgement time,
physical-session replacement, focus suspension, and fatal runtime behavior

## Authority

This document supersedes
`2026-08-25-asio-focus-recovery-design.md` as the implementation authority for
ASIO lifecycle and recovery. It also supersedes the callback-anchor history,
physical-endpoint-generation, and fixed-48-kHz assumptions in
`2026-08-22-asio-absolute-time-judgement-design.md`.

The following established behavior remains authoritative and is not reopened:

- input transitions retain their absolute host timestamps and sequence order;
- judgement consumes checked rational time rather than render-frame time;
- native recognition, authored chart cadence, judgement windows, score
  behavior, and stage semantics remain unchanged;
- an active judgement stage retains its already-established stage binding and
  does not repeatedly resolve its old entry timestamp;
- the WASAPI exact provider and WASAPI-exclusive backend are unchanged;
- configured ASIO never falls back to WASAPI or DirectSound; and
- build and static checks do not replace actual-game acceptance.

The uncommitted change that resolves an ASIO timestamp from physical callback
brackets is not an architectural foundation. It makes callback scheduling and
future observations authoritative over an input timestamp that already has an
absolute host time. This design removes that authority rather than committing
the partial change.

## Problem

The current implementation gives physical ASIO callbacks too much authority.
`ExactAsioClock` retains callback anchors and derives an event coordinate from
whichever anchors are available when the event is queried. A later callback
can therefore change the evidence used for an already-final timestamp, and a
finalized query may wait for a callback that did not exist when the input was
captured.

The observed driver-versus-logical residual and DirectSound cursor failures do
not prove that callback interpolation is a valid judgement clock. The residual
is aggregate diagnostic evidence without per-input sign or causation, while
the cursor failures are a separate presentation-boundary problem. Neither may
rewrite the absolute judgement contract.

The recovery implementation then compounds the problem:

- initial backend construction and several callback calculations assume
  48,000 Hz before asking the driver which rate is active;
- callback timestamps are used both to validate a physical session and to
  establish the logical judgement coordinate;
- detached rendering advances through repeated wakeups instead of one stable
  process-lifetime time equation;
- physical-session replacement can alter the apparent endpoint clock even
  though the song and playback lifetime did not change; and
- attempts to repair a clock symptom locally can change the result produced
  for later songs without correcting the ownership model.

The intended product behavior is simpler. Focus loss may temporarily remove
audio, but it must not remove or pause the logical game/audio timeline supplied
as the scheduler's raw time. A replacement ASIO session supplies buffers and a
raw device position; it does not create a new game/audio clock. Native judgement
applies `JudgTimeOffset` afterward in its existing result calculation; ASIO does
not read or apply it. Recovery is an exceptional physical ownership transition,
not a general clock-correction mechanism.

## Decision summary

The backend is split into two explicit lifetimes:

1. A **persistent logical backend** owns the mixer, voices, logical output
   timeline, presented coordinate, exact judgement provider, render
   sequencer, and endpoint generation. It is created once and survives focus
   suspension and physical recovery.
2. A **replaceable physical session** owns IASIO, driver buffers, callback
   runtime state, the raw sample-position epoch, and a physical-session
   generation. It is replaced only after an observed focus loss.

The logical backend adopts the ASIO driver's current valid sample rate during
initial acquisition. That rate and the rest of the output contract are frozen
for the logical backend's lifetime. Recovery must restore a newly opened
driver to the frozen rate before `Start`; it may not change the logical rate.

Judgement time is a process-lifetime rational function of the multimedia
millisecond clock. A physical session attaches to that function exactly once.
Later callback timestamps are diagnostics and physical-presentation evidence
only. They can never interpolate, extrapolate, re-anchor, correct, slew, pause,
or rewrite judgement time.

`GameTimeOffset` is applied later when the logical audio coordinate is mapped
to game time. `JudgTimeOffset` remains solely inside native judgement-result
calculation. Neither offset participates in ASIO recovery or compensates for
clock evidence.

```text
multimedia-millisecond observations
              |
              v
     persistent logical timeline --------------------+
              |                                      |
              v                                      v
   exact judgement provider                absolute render targets
                                                     |
                                      +--------------+--------------+
                                      |                             |
                               detached discard          physical ASIO render
                                                                    ^
                                                                    |
new IASIO sample-position epoch -- one-time session attachment -----+
```

## Alternatives rejected

### Keep callback bracketing or newest-past-anchor projection

Both forms leave physical callbacks as the evidence for logical judgement.
Bracketing makes finalization depend on a future callback and allows later
evidence to change the coordinate. Newest-past projection avoids waiting only
by extrapolating independently from whichever callback happened to be newest.
Neither preserves one absolute result across callback cadence and physical
session replacement.

### Create a new endpoint/exact provider on every recovery

This accurately describes physical IASIO ownership but gives focus changes
authority over song and judgement identity. Active stages would either become
fatal on every legitimate recovery or require a fallback/rebinding rule that
can shift judgement. Physical generation must therefore remain separate from
logical endpoint generation.

### Pause logical time while audio is unavailable

This keeps mixer and hardware positions superficially aligned by deleting the
elapsed interval. It also loses judgement time, prevents clean recovery to the
live song position, and contradicts the approved requirement that brief audio
loss must not pause judgement.

### Force 48 kHz or adopt a new rate on each recovery

Forcing 48 kHz mutates a valid driver-owned initial rate and makes 44.1-kHz
operation impossible. Adopting a different recovery rate would require
recreating or retiming the mixer, voices, playback histories, exact provider,
and every frame coordinate mid-stage. The initial driver rate is therefore
adopted once and restored on every recovery.

## Terminology and immutable contract

Let:

- `R` be the initial driver sample rate, in whole frames per second;
- `B` be the configured and validated ASIO period in frames;
- `L` be the driver's reported output latency in frames after buffer creation;
- `T0` be the process-lifetime unwrapped multimedia-millisecond origin;
- `F0` be the logical presented-frame coordinate at `T0`; and
- `E` be the persistent logical endpoint generation.

The initial physical acquisition freezes this contract:

- driver registration identity;
- `R`;
- `B` and the selected stereo output channel indices;
- the selected channels' ASIO sample types;
- `L`;
- `outputReady` capability; and
- logical endpoint generation `E`.

The driver name remains configuration. Sample rate does not become a new
configuration field. Output sample rate is a property of the initially opened
driver session.

Every successful recovery must reproduce the immutable contract. It receives
a new nonzero physical-session generation, but it reuses `R`, `B`, `L`, `E`,
the mixer, all voices, and the exact provider.

## Sample-rate ownership

### Initial acquisition

After resolving and initializing the configured ASIO driver, the backend calls
`GetSampleRate`. The returned `ASIOSampleRate` is accepted only when it is:

- finite;
- positive;
- an exact whole-Hz value; and
- representable by the existing unsigned 32-bit mixer sample-rate contract.

Values such as `44100.0` and `48000.0` are valid. The initial acquisition does
not call `CanSampleRate` or `SetSampleRate` merely to force 48 kHz. An active
rate returned by the driver is the rate the logical backend adopts.

The startup sequence is staged because output latency is available only after
driver buffers exist:

1. Start foreground publication so no loss edge can be missed.
2. Resolve and initialize the driver.
3. Read and validate the current rate `R`, buffer limits, output channels, and
   requested period `B`.
4. Construct rate-dependent mixer and callback state using `R`.
5. Create driver buffers and read output latency `L`.
6. Freeze the full logical contract and create/register the logical timeline
   and exact provider.
7. Call `Start`, prime with silence, and prove the finite callback-stability
   contract.
8. Enter `Running`, then consume any focus loss or current background state
   observed during startup.

Any initial acquisition, construction, `Start`, or stability failure is fatal.
Initial startup has no retry and no alternate backend.

### Recovery acquisition

Recovery opens a fresh physical driver session and reads its current rate `C`.

- If `C == R`, no sample-rate mutation is made.
- If `C != R`, the attempt calls `CanSampleRate(R)`, calls `SetSampleRate(R)`,
  and then calls `GetSampleRate` again. The verified result must equal the
  frozen integer `R` exactly before buffers are created.
- The attempt remembers `C` when it changes the driver. On release or failed
  acquisition it restores `C`, after stopping and disposing buffers, so the
  game does not permanently overwrite the rate owned by another application.

The newly created buffers must reproduce `B`, the selected channel types,
`L`, and the frozen capability contract. A mismatch never updates the logical
backend. It is a clean pre-`Start` recovery failure only when complete cleanup,
including restoration of `C` when necessary, succeeds. Otherwise it is fatal.

ConfigGUI inspection and startup preflight use the initial adopt-current
policy. They report and validate the current rate without changing it.

## Persistent logical timeline

The logical timeline is independent of callback cadence. At construction it
captures the raw 32-bit `timeGetTime()` value associated with `T0`, establishes
unwrapped elapsed milliseconds `U0 = 0`, and establishes `F0 = 0`.

A single logical-timeline writer maintains a versioned stable snapshot:

```text
{ observed_raw_ms, observed_unwrapped_ms }
```

For each newer observation `n`, it advances the unwrapped value by the unsigned
32-bit forward difference from the previous raw observation. Observations occur
far more often than the signed half-range, so a normal `UINT32_MAX -> 0` wrap
is exact. Failure to observe a representable forward interval is an arithmetic
contract fault; it is not treated as focus evidence.

Readers map an event tick `t` against one stable snapshot:

```text
U(t) = observed_unwrapped_ms
     + signed_mod32(t - observed_raw_ms)

P(t) = F0 + (U(t) - U0) * R / 1000
```

`P(t)` is a checked rational presented-frame coordinate. It is not rounded
inside the exact provider. Because the raw and unwrapped members advance
together, reading the same retained event against a later snapshot produces
the same `U(t)`; updating wrap bookkeeping cannot re-anchor the event.

The exact provider resolves only timestamps retained by the input/stage
contracts, which are unambiguously within the signed half-range of the current
snapshot. An ancient timestamp outside that domain is a deterministic
`Discontinuous` result rather than a guessed epoch.

For an ASIO `systemTime` value in nanoseconds, the same domain is evaluated
without discarding the sub-millisecond remainder:

```text
whole_ms = uint32(floor(systemTime_ns / 1,000,000))
remainder_ns = systemTime_ns mod 1,000,000
U_ns = U(whole_ms) * 1,000,000 + remainder_ns
P(systemTime_ns) = F0 + (U_ns - U0 * 1,000,000) * R
                 / 1,000,000,000
```

All multiplication, addition, conversion, and modular-domain validation uses
checked arithmetic. Whole render coordinates use mathematical floor only
after a nonnegative coordinate has been established:

```text
presented_whole(t) = floor(P(t))
render_begin(t)    = presented_whole(t) + L
```

The implementation always derives a coordinate from the immutable origin.
It never advances the timeline by repeatedly adding `R / 1000`. At 44.1 kHz,
one millisecond is exactly `441 / 10` frames; the fractional tenth is retained
and cannot accumulate into drift.

Time advances the logical coordinate in every lifecycle state. It does not
select a lifecycle state. Focus state alone decides whether a physical session
may remain attached.

## Exact judgement provider

`ExactAsioClock` remains the backend-neutral provider registered as
`asio_multimedia_ms`, but it owns no callback anchors or physical-session
state. It is a small adapter over:

- the immutable logical origin `{T0, U0, F0, R}` and its exact wrap snapshot;
- persistent endpoint generation `E`;
- immutable period and output-latency information.

Resolving either an input timestamp or a provisional horizon computes `P(t)`
directly. Resolve intent never selects different time evidence.

The result is:

- `Resolved` whenever the logical projection is representable;
- `TemporarilyUnavailable` when a concurrent logical-timeline publication
  prevents a stable wrap-snapshot read; and
- `Discontinuous` after explicit invalidation, arithmetic failure, impossible
  domain ambiguity, or logical contract failure.

Normal ASIO operation cannot produce `HistoryLost` because there is no bounded
callback history to outlive. A future callback is never required to finalize
an older event, no callback can rewrite the event coordinate, and physical
render progress cannot delay logical judgement.

The submitted tail remains publication evidence and an exclusive bound for
rendering and DirectSound presentation. Physical and detached renders update
it only after the mixer transition and required presentation publications
commit. `ExactAsioClock` never reads it. A failed or abandoned render plan
makes no logical state change.

Endpoint identity remains stable across suspension and recovery. An active
judgement stage therefore keeps the same provider and endpoint generation.
The already-implemented rule that stops re-resolving an active stage's entry
timestamp remains in force.

## Physical-session attachment

ASIO sample position is a session-local coordinate and may restart at any
value after recovery. Each session receives one affine mapping into the
persistent logical render coordinate.

For the first valid callback observation in physical generation `G`, let:

- `S0` be the raw ASIO sample position;
- `A0` be the callback's valid ASIO `systemTime`; and
- `L` be the frozen output latency.

The candidate attachment is:

```text
logical_render_origin  = floor(P(A0)) + L
physical_render_origin = S0 + L
```

For a later sample position `S` in the same physical generation:

```text
logical_render(S) = logical_render_origin
                  + ((S + L) - physical_render_origin)
```

The latency appears on both sides to preserve the existing definition that
`samplePosition` is the callback's presented coordinate and the output buffer
is rendered for the future latency-adjusted span. It is applied once; no
second judgement or presentation offset is introduced.

The first valid callback establishes the candidate mapping. The existing
finite sequence of valid, continuous priming callbacks proves that mapping
before the physical session is committed as `Running`. Priming callbacks emit
silence. The mapping is not recomputed during the proof.

After attachment:

- raw sample-position deltas and `B` are authoritative for physical buffer
  continuity;
- later `systemTime` values may publish a physical anchor for the separate
  DirectSound presented cursor;
- the difference between the mapped physical coordinate and `P(systemTime)`
  remains an aggregate residual diagnostic;
- no later timestamp may correct or slew the physical mapping; and
- no callback observation may change the logical exact-judgement mapping or
  endpoint identity.

A slow callback or large residual is diagnostic by itself. A repeated or
skipped sample position, invalid buffer alternation, callback overlap, or other
structural callback violation remains fatal.

## Rendering and handoff

`AsioLogicalRenderSequencer` is the only owner of mixer-render authorization.
The physical callback path and detached path use one exclusive claim and
cannot render concurrently.

Every plan is based on an absolute logical target, never on the number of
timer wakes or callbacks observed:

- while running, the one-time physical mapping supplies the target;
- while suspended or performing pre-`Start` recovery work, `render_begin(now)`
  supplies the target;
- a late detached wake catches up to the absolute target exactly once, using
  fixed-size chunks when required by the mixer; and
- an abandoned plan does not increment any cursor or voice state.

Suspended rendering discards samples but commits the same logical effects as a
physical render: voices advance, playback histories advance, the presented
coordinate remains usable, and the submitted tail advances. This is what
allows judgement time and current song position to survive lost audio.

Before recovery calls `Start`, detached production is quiesced at a committed
boundary. The callback runtime is then allowed to prime with silence. At the
first committed physical render:

- if the mapped target equals the next logical tail, the callback renders the
  block normally;
- if the mapped target is behind the tail because detached rendering already
  covered that time, callbacks remain silent until the physical target catches
  the tail; already-discarded audio is never replayed;
- if the mapped target is ahead of the tail, the sequencer advances and
  discards the missing interval once before rendering the current block; and
- the attachment gap is recorded once, not treated as recurring drift.

Recovery therefore resumes the live song position. It never pauses judgement,
replays the period without audio, double-advances a voice, or offsets all later
callbacks to hide an initial mismatch.

## Foreground publication

The foreground monitor publishes one coherent snapshot:

```text
{ is_foreground, loss_generation }
```

`loss_generation` increments on every foreground-to-background transition.
The control thread tracks the last consumed generation. A wake event may
coalesce; the snapshot carries correctness.

Consequences:

- loss followed by regain before one control-thread read still invalidates the
  old physical session;
- an initially background process still completes initial ASIO `Start` and
  stability proof, then suspends immediately;
- loss during initial `Start` or stability is recorded and consumed after the
  non-cancellable proof completes;
- loss during recovery before `Start` cancels that clean attempt and returns
  to `Suspended` without consuming a retry failure;
- loss after recovery has invoked `Start` cannot turn an unstable session into
  a retryable attempt; the proof completes or fails fatally, after which
  pending focus state is consumed; and
- regain during release waits for proven release before a replacement session
  is opened.

No timeout, callback interval, silence duration, driver timestamp, frame drop,
window movement, or audio-clock residual can increment `loss_generation` or
select recovery.

## Lifecycle state machine

```text
Starting
  initial acquisition/Start/stability succeeds -> Running
  any failure                                -> Fatal

Running
  background or unconsumed loss generation  -> Suspending
  shutdown                                   -> Stopping
  any physical/logical contract fault        -> Fatal

Suspending
  callback quiescence and safe close succeed -> Suspended
  unsafe stop/dispose/rate restoration        -> Fatal

Suspended
  detached absolute rendering                -> Suspended
  foreground with no pending loss            -> Recovering
  logical/monitor fault                       -> Fatal
  shutdown                                    -> Stopping

Recovering
  clean pre-Start failure                     -> bounded retry or Fatal
  focus loss before Start                     -> Suspended
  Start/stability succeeds                    -> Running
  Start/stability/runtime contract failure    -> Fatal

Fatal
  controlled final teardown                  -> stopped with fatal result

Stopping
  controlled final teardown                  -> stopped
```

Recovery permits one immediate attempt, one retry after one second, and one
final retry after two additional seconds. Retry waits are interruptible by
focus publication and shutdown. These delays schedule an already-authorized
recovery; they are not evidence about focus or driver health.

The delayed retries are defensive. A normal installed driver is expected to
succeed on the immediate acquisition. A runtime acceptance run that routinely
uses retries is evidence of a remaining design or ownership defect, even if a
later attempt eventually produces sound.

## Failure and thread semantics

All IASIO lifecycle calls run on the owning control thread. The callback path
never opens, stops, disposes, retries, changes sample rate, queries focus, or
invokes the process fatal path directly.

When a callback observes a fatal contract violation, it:

1. zero-fills both output channels for the current buffer;
2. atomically latches the first typed fault;
3. signals the control thread; and
4. returns without throwing.

The control thread consumes the fault, enters `Fatal`, quiesces callback
ownership, performs controlled final teardown, reports the typed failure, and
uses the existing fatal result path. The silence is only callback-safe fault
containment; gameplay is not allowed to continue on an uncertain clock.

Only clean `Recovering` failures before invoking `Start` are retryable:

- driver resolution or creation failure;
- initialization or immutable-contract negotiation failure;
- inability to restore the frozen rate before buffer creation;
- buffer or callback installation failure; or
- immutable latency/channel/capability mismatch.

They are retryable only after complete cleanup proves that no callback,
buffer, driver object, or temporary sample-rate mutation remains.

The following are fatal:

- every initial startup failure;
- every failure after `Start` has been invoked;
- callback-stability failure;
- ASIO reset, resync, sample-rate, buffer-size, or latency-change request after
  `Start`;
- invalid buffer index or alternation, callback overlap, or sample-position
  discontinuity;
- required `outputReady` failure;
- mixer, conversion, sequencer, submitted-tail, or presented-clock publication
  failure;
- logical coordinate regression or checked-arithmetic failure;
- foreground-monitor failure or impossible focus generation;
- inability to quiesce callbacks or prove safe session cleanup; and
- inability to restore a sample rate changed by a failed recovery attempt.

One slow callback, one slow game frame, or an overload observation remains
diagnostic unless a separate structural invariant is violated. Conversely, a
structural fault is not made recoverable merely because focus changed nearby.

No exception may cross an ASIO callback, WinEvent callback, COM boundary,
DirectSound-facing method, or thread entry point.

## Component responsibilities

### `AsioSession`

- Owns one IASIO object and its creating-thread lifecycle.
- Implements explicit `AdoptCurrentRate` and `RequireFrozenRate(R)` policies.
- Reports the rate before and after any recovery adjustment.
- Creates/disposes buffers and reports immutable physical capabilities.
- Restores only a rate that the same attempt changed.
- Does not own logical time, mixer state, focus policy, or retries.

### `AsioLogicalTimeline`

- Owns `{T0, U0, F0, R}`, exact wrap bookkeeping, and checked multimedia-time
  projection.
- Accepts wrap-snapshot advancement from the control thread only; callback,
  input, and game threads are readers.
- Converts multimedia milliseconds and ASIO nanoseconds into one rational
  presented-frame domain.
- Has no IASIO, callback, mixer, focus, or retry dependency.

### `ExactAsioClock`

- Adapts `AsioLogicalTimeline` to the backend-neutral exact-clock interface.
- Reports persistent logical provider information only.
- Contains no callback anchor ring and performs no callback interpolation.
- Contains no submitted-tail or physical-presentation dependency.
- Retains one provider identity and endpoint generation until final shutdown.

### `AsioLogicalRenderSequencer`

- Owns the exclusive render claim, next committed logical tail, and one-time
  physical-session mapping.
- Plans physical and detached work from absolute logical targets.
- Commits state only after render and publication succeed.
- Makes attachment catch-up or wait happen exactly once.

### `AsioCallbackRuntime`

- Validates real-time callback structure and raw sample continuity.
- Uses the session generation and sequencer plan supplied by the logical
  backend.
- Zero-fills and latches faults without lifecycle work or exceptions.

### ASIO output-backend control state

- Owns the foreground snapshot, lifecycle state machine, retry schedule,
  persistent logical components, and replaceable physical session.
- Creates the initial logical contract in the staged order above.
- Is the only component allowed to initiate suspension, recovery, or fatal
  teardown.

## Configuration and preflight

No configuration key is added. Existing ASIO driver name, exact buffer frames,
and output base channel remain the complete user-selected stream request.

Runtime startup and ConfigGUI environmental validation both inspect the
driver's current rate and validate it as a supported whole-Hz mixer rate.
Preflight does not set or restore a rate because it never changes one. Runtime
recovery's frozen-rate restoration is an internal lifecycle rule, not a saved
configuration value.

Configured ASIO remains strict. Failure cannot instantiate WASAPI, DirectSound,
or a second logical backend. Logs must explicitly identify the selected backend
and confirm that no alternate backend was selected.

## Diagnostics

Diagnostics are transition-oriented and bounded.

One logical-backend record contains:

- driver registration and `sample_rate_source=driver_current`;
- `R`, `B`, `L`, selected channel types, and capability contract;
- raw `T0`, the unwrapped logical epoch, `F0`, and endpoint generation `E`;
- exact provider domain and timer quantum; and
- configured backend plus `alternate_backend_selected=false`.

Each physical-session generation records:

- generation and lifecycle reason (`startup` or `focus_recovery`);
- rate observed on open, whether `R` was requested, verification result, and
  restoration result when applicable;
- raw sample origin, logical render origin, and physical render origin;
- number of silent priming callbacks and attachment result;
- any one-time catch-up or wait interval; and
- stop, callback quiescence, buffer disposal, and close result.

Aggregate runtime summaries retain:

- callback cadence and slow/overload observations;
- sample-position and buffer-sequencing faults;
- logical rendered/discarded frames and submitted tail;
- driver-time residual relative to the immutable logical timeline, clearly
  labelled diagnostic-only;
- physical-session and focus-loss generations;
- exact resolved/temporary/discontinuous counts; and
- typed fatal state, when present.

Exact-clock physical publication and pending counts stay zero. Resolved,
unavailable, and discontinuity counts remain visible; `history_lost` stays
zero because ASIO judgement retains no callback history.
Deferred absolute-judgement summaries retain the maximum gap between outer
calls and the maximum time spent inside judgement dispatch for each five-second
window. These values are captured in fixed storage and formatted only after the
stage ends.
There is no per-callback, per-input, or per-note logging.

## Automated verification boundary

Tests are added or changed only where an independently derived oracle protects
a production contract.

1. `ExactAsioClockTests` covers 44.1 and 48 kHz absolute rational projection.
   The oracle is hand-derived: at 44.1 kHz, 1 ms is `441/10` frames, while at
   48 kHz it is 48 frames. Finalized and provisional intents must resolve the
   same coordinate; later wrap-snapshot observations may not rewrite it. The
   physical submitted tail and presentation anchor are tested separately and
   may not change or delay the logical judgement coordinate.
2. `AsioLogicalRenderSequencerTests` covers one physical attachment followed
   by continuous sample-position deltas and deliberately irregular later
   callback timestamps. The expected logical frames come from the first
   mapping plus raw sample deltas; later timestamps must not alter them. The
   test also distinguishes one-time catch-up from replay or double advance.
3. Existing `AsioForegroundStateTests` remains the authority for loss followed
   by regain before one consumer read. It is extended only if a required state
   transition is not already covered.

The callback-interpolation tests are replaced by this absolute-timeline oracle;
they are not retained as additional coverage. The combined exact/DirectSound
cursor test verifies that a physical presentation anchor moves only the cursor
and leaves the exact judgement result unchanged.

No fake IASIO suite, sleep-dependent timing test, source-text assertion,
log-string test, test-only production hook, or simulated gameplay oracle is
added. Driver acquisition, actual focus transfer, audible recovery, and
judgement feel remain runtime acceptance.

## Implementation migration order

1. Add the pure generic-rate `AsioLogicalTimeline` and its one justified
   rational/wrap test without switching production behavior.
2. Add explicit initial-adopt and recovery-require sample-rate policies to
   `AsioSession`; remove forced 48-kHz negotiation from runtime and preflight.
3. Reorder initial backend construction so `R` is known before every
   rate-dependent component and `L` is known before freezing the endpoint
   contract.
4. Thread `R` through the render core, callback timing, raw clock validation,
   presented clock, sequencer, exact provider, and duration diagnostics.
5. Remove `ExactAsioClock` callback history and make it a logical-timeline
   adapter with no submitted-tail dependency. Keep the submitted tail and
   physical anchors only in rendering and the separate DirectSound
   presentation clock.
6. Refactor the logical render sequencer around absolute targets and one-time
   physical-session attachment.
7. Integrate detached-to-physical handoff, silent priming, and the strict
   post-`Start` fatal boundary into the lifecycle controller.
8. Replace obsolete anchor diagnostics, review callback safety and ownership,
   then perform focused and complete static verification.

Each step must preserve compilation or be kept within one coherent commit.
Unrelated audio, judgement, input, configuration, and formatting changes are
out of scope.

## Static verification

CLion source diagnostics are loaded one changed file at a time: open the file,
allow analysis, request diagnostics, and leave the file open. Diagnostics are
not batched and CLion's process or project lifetime is never altered.

Command-line verification uses
`C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat`
and `GC_ASIO_SDK_DIR=H:\gc\artifacts\ASIOSDK` with the checked-in x86 Debug and
Release presets. It includes:

- focused changed-contract tests;
- complete Debug and Release builds and test suites;
- `git diff --check` and focused diff review;
- review that callbacks contain no allocation, formatting, blocking lifecycle
  work, or exception escape; and
- candidate/deployed DLL hash comparison before interpreting a runtime log.

These checks prove only source, build, and automated contracts. They do not
prove driver ownership transfer, audible stability, or judgement behavior.

## Ordered runtime acceptance

The user runs the actual game and controls focus. Runtime mutation or process
lifecycle actions are not performed by the implementation workflow.

### Run 1: lose focus shortly after startup

1. Start the game and move it to the background shortly afterward.
2. Initial acquisition must still adopt the driver's current `R`, call
   `Start`, and complete stable silent priming.
3. Pending background state must cause exactly one safe physical release while
   the logical endpoint and exact provider remain alive.
4. Logical time, voices, submitted tail, and judgement time must continue while
   audio is unavailable.
5. On regain, the immediate recovery attempt should normally succeed, preserve
   `R` and `E`, attach one new physical generation, and resume at the current
   song/menu position.
6. No alternate backend, repeated old audio, logical-time pause, re-anchor,
   unbounded retry, or runtime fault may appear.

The two delayed retries are allowed defensively but are not expected in this
normal run. Exercising them repeatedly is not accepted as healthy behavior.

### Run 2: all-foreground multi-song session

After Run 1 succeeds, restart and keep the game foreground for at least two
complete songs. The run must show:

- exactly one physical session and no recovery transition;
- the driver-current rate and immutable contract unchanged;
- no exact history loss, discontinuity, coordinate regression, or later
  callback re-anchor;
- no sample-position, render-gap, reset, resync, rate, buffer, or latency
  fault;
- no audio fluctuation or timing movement between the first and second song;
- no judgement degradation attributable to a preceding frame drop;
- the native timing trace preserves the established relations
  `applied_offset_ms = recognition_ms - scheduler_native_ms` and
  `signed_error_ms = recognition_ms - note_target_ms =
  (scheduler_native_ms - note_target_ms) + applied_offset_ms`;
- `applied_offset_ms` equals the configured `JudgTimeOffset` (`-9 ms` for the
  current user configuration), but the per-note `signed_error_ms` remains
  variable and is never expected to equal `-9 ms`; `GameTimeOffset` remains
  solely in the game/audio coordinate;
- ASIO-versus-WASAPI judgement comparison uses the per-note error distribution
  and the existing judgement-offset advisor, not equality to one fixed error;
- the DirectSound-facing cursor remains mapped rather than accumulating
  exclusive-tail lookup failures;
- the completed credit advances through unlock/reward handling into the normal
  ranking/demo sequence; and
- stable human-observed timing relative to the known WASAPI behavior.

### Run 3: post-start focus transfer and menu activity

After stable foreground startup, move out of focus, return, enter the menu, and
scroll long enough to reproduce the previous deterministic silence sequence.
There must be one release/recovery generation, immediate normal acquisition,
continued logical time, and sustained audio after menu activity.

### Run 4: window movement or transient game-frame drop

Moving the game window or causing a transient main-thread frame drop must not
change endpoint identity, physical attachment, or the scheduler's raw
game/audio-time projection. The main thread may temporarily stop presenting
frames, but callback audio and the logical timeline remain independent. When
rendering resumes, native/high-FPS song-clock catch-up may consume elapsed time;
ASIO does not pause or re-anchor the logical game/audio timeline to hide the
gap.

### Alternate supported driver rate

When the local driver can be placed at another supported current rate before
launch, repeat initial startup and the all-foreground session. Startup must
adopt that rate, such as 44.1 kHz, without a config change or forced switch to
48 kHz. Recovery within that backend lifetime must retain the adopted rate.

## Completion criteria

The redesign is complete only when all of the following hold:

- one persistent logical coordinate and endpoint identity define the ASIO
  game/audio timeline supplied to the scheduler for the entire backend lifetime;
- `P(t)` is the only ASIO judgement-time authority, and both resolve intents
  use it directly;
- callbacks and physical-session generations never participate in judgement
  interpolation or extrapolation;
- initial sample rate comes from the driver and recovery preserves it;
- focus loss may remove audio but cannot pause or lose the logical game/audio
  timeline supplied to the scheduler;
- recovery is focus-driven, bounded, and normally succeeds immediately;
- every post-`Start` instability fails closed instead of continuing or
  recovering silently;
- ASIO never falls back to WASAPI or DirectSound;
- the configured `GameTimeOffset` and `JudgTimeOffset` retain their separate
  native meanings, and post-credit ranking/demo progression is unchanged;
- meaningful pure-contract tests, x86 Debug/Release verification, and diff
  review pass; and
- the ordered actual-game runs demonstrate startup/background recovery,
  all-foreground multi-song stability, sustained menu audio, and unchanged
  judgement behavior.

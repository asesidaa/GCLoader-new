# ASIO Focus Suspension and Recovery Design

**Status:** Approved design, 2026-08-27

**Scope:** ASIO lifecycle, logical audio continuity, and exact judgement time

**Supersedes:** The focus-recovery behavior introduced by `fe0c54a` and extended
by `8619113`

## Problem

The current backend turns one ASIO endpoint into two incompletely separated
lifetimes: a persistent logical audio engine and a replaceable physical driver
session. The separation is presently inconsistent in four ways:

1. Startup can interpret foreground loss as successful ASIO startup without
   ever starting the driver or receiving a callback.
2. A loss followed quickly by a regain can collapse into one final foreground
   value before the control thread observes the loss.
3. Detached rendering has no initial logical anchor in that startup path and
   does not publish exact continuity anchors. Voices, presented audio time, and
   judgement time can therefore stop while the process is backgrounded.
4. The judgement scheduler resolves the immutable stage-entry timestamp on
   every outer loop after activation. Once that timestamp ages out of bounded
   exact history, every frame performs a full history scan and reports
   `HistoryLost` even though the active stage clock is already bound.

The current recovery loop also retries physical-session faults indefinitely.
That hides violations of callback, clock, render, and cleanup contracts and
allows the game to continue with an audio backend whose correctness is no
longer known.

The observed all-foreground runtime log does not show an ASIO driver failure:
the driver reported no reset, resync, rate, buffer-size, or latency request;
sample positions remained continuous; and rendered-frame gaps remained zero.
The redesign must therefore keep the ordinary foreground callback path small
and stable. Recovery is an explicit focus lifecycle, not a general mechanism
for masking audio faults.

## Required behavior

- Configured ASIO always remains ASIO. Neither startup nor recovery may create
  or select a WASAPI backend.
- Startup performs one complete physical ASIO acquisition, calls `Start`, and
  proves stable callbacks even if the process loses foreground during startup.
- After startup stability is proven, an observed background state or any
  unconsumed focus-loss generation suspends and releases the physical session.
- A running focus loss cleanly quiesces callbacks, stops and releases IASIO,
  but preserves the logical engine and exact-clock identity.
- While suspended, discarded mixer rendering advances voices, the logical
  output cursor, the presented clock, and exact judgement time.
- Foreground regain acquires a fresh physical session and maps its raw sample
  position into the existing logical output-frame domain.
- A focus loss cannot be erased by a later regain, even when both occur before
  the control thread wakes.
- Ordinary foreground operation never polls, retries, reopens, or performs
  detached rendering.
- Once the backend has invoked `Start`, any failure to prove or preserve the
  physical-session contract is fatal. Continuing with uncertain audio state is
  forbidden.
- Judgement is continuous across a confirmed suspension. Elapsed time may
  advance the logical timeline only after focus state independently places the
  backend in the suspended lifecycle; elapsed time is never evidence of focus
  loss.

## Non-goals

- Recovering arbitrary ASIO driver resets or broken callback streams in place.
- Guessing focus from silence, driver timestamps, callback cadence, timeouts,
  frame drops, or audio-clock drift.
- Falling back to another backend.
- Recreating the DirectSound facade, mixer, voices, endpoint generation, or
  exact-clock provider during focus recovery.
- Treating one late callback or one long game frame as proof of session loss.

## Ownership model

The backend owns two explicit lifetimes.

### Logical backend lifetime

Created once after configuration and destroyed only during final shutdown:

- DirectSound-facing `IAudioEngineServices`;
- `AudioRenderCore`, miniaudio mixer, and all live `MixerVoice` objects;
- monotonically increasing logical 48 kHz output-frame cursor;
- presented-clock publication;
- registered `ExactAsioClock` provider and endpoint generation;
- logical render sequencer and its exclusive render claim;
- foreground publication and the ASIO control thread.

### Physical session lifetime

Created at startup or recovery and destroyed at focus suspension or final
shutdown:

- IASIO driver instance and registration resolution;
- `AsioSession`, buffers, and channel views;
- callback table, callback runtime, and callback worker;
- per-session raw sample-position tracker;
- raw-driver-to-logical-frame mapping;
- diagnostic physical-session generation.

Only the logical render sequencer may authorize mixer rendering. The physical
callback path and suspended path are two producers competing for that single
production claim; they may never enter the mixer concurrently.

## Lifecycle state machine

```text
Starting
  acquisition/Start/stability succeeds -> Running
  any failure                         -> Fatal

Running
  unconsumed focus loss/background    -> Suspending
  shutdown                            -> Stopping
  any physical/logical contract fault -> Fatal

Suspending
  callbacks quiesced and IASIO closed -> Suspended
  unsafe cleanup                      -> Fatal

Suspended
  detached logical deadlines          -> Suspended
  foreground with no pending loss      -> Recovering
  logical/monitor fault                -> Fatal
  shutdown                             -> Stopping

Recovering
  pre-Start acquisition succeeds       -> Start/stability -> Running
  clean pre-Start acquisition failure  -> bounded retry or Fatal
  focus loss before Start              -> Suspended
  Start/stability/contract fault        -> Fatal

Fatal
  controlled final teardown            -> stopped with fatal result

Stopping
  controlled final teardown            -> stopped
```

`Running` is committed only after the callback stability proof completes. A
focus loss during `Starting` does not abort that proof. Immediately after the
proof, the control thread consumes the foreground snapshot; if the process is
background or a loss occurred during startup, it transitions through
`Suspending` before doing anything else.

## Foreground publication

Foreground publication is a coherent snapshot containing at least:

- `is_foreground`: the latest observed state; and
- `loss_generation`: a monotonic counter incremented on every foreground to
  background transition.

The WinEvent callback is the single publisher. The control thread records the
last consumed loss generation. Its wake event is only a scheduling hint and
may coalesce; correctness comes from the snapshot.

If loss and regain occur before one control-thread read, that read sees the
latest state as foreground **and** a newer loss generation. `Running` must
therefore suspend the old physical session first, consume the loss, and only
then recover. A quick regain cannot retroactively prove that the old IASIO
ownership was continuously valid.

An initially background process still performs normal startup. Once stable,
the current background state drives immediate suspension even if no transition
was observed after monitor initialization.

No foreground query is derived from elapsed time. Timers are used only to wake
an already suspended state for detached rendering or an already recovering
state for a scheduled retry.

## Startup and physical-session stability

Startup has exactly one acquisition attempt and no backend fallback:

1. Create the persistent logical backend and foreground monitor.
2. Resolve and open the configured IASIO driver.
3. Establish the immutable physical format contract.
4. Create buffers and install the process-lifetime callback table.
5. Invoke `Start`.
6. Require the existing finite sequence of valid, continuous callbacks that
   proves the physical session and raw clock mapping are usable.
7. Enter `Running`, then immediately process any pending focus state/loss.

Foreground changes do not short-circuit steps 2 through 6. Failure in any
startup step is fatal because no known-good ASIO backend exists. WASAPI is not
consulted.

The normal steady-state callback path remains unchanged except for the minimum
publication needed by the persistent logical timeline. It contains no focus
query, reopen decision, retry decision, or blocking lifecycle operation.

## Suspension and detached logical rendering

Suspension order is strict:

1. Stop authorizing new callback renders.
2. Stop IASIO.
3. Join/quiesce the callback worker and prove no callback owns the render claim.
4. Dispose physical buffers and close the IASIO session.
5. Enter `Suspended` with the logical engine intact.

Failure to prove safe quiescence or cleanup is fatal; the backend may not open
a replacement session while the previous callback/session ownership is
uncertain.

The last accepted physical callback supplies the starting logical frame and
monotonic-time anchor. While `Suspended`, the logical sequencer uses explicit
deadlines derived from that anchor to claim mixer renders. It discards samples
but commits all of the same logical consequences as a hardware render:

- active voices advance;
- the logical output-frame cursor advances without rounding away a partial
  interval;
- the normal presented clock advances;
- an exact continuity anchor is published for the same provider and endpoint
  generation.

The detached path may use monotonic elapsed time to calculate how many logical
frames passed because confirmed focus state has already selected the
`Suspended` lifecycle. That calculation cannot change lifecycle state.

If shutdown or regain wins a render race, an uncommitted render plan makes no
state change. Sequencer state advances only after the mixer render and all
required clock publications succeed.

## Recovery and handoff

Recovery begins only while the latest coherent snapshot is foreground and no
unconsumed loss must first invalidate a prior session. It creates a new
physical-session generation but reuses the persistent logical endpoint and
exact provider.

Before `Start`, a recovery attempt may fail cleanly while opening the driver,
validating the unchanged format contract, or creating buffers. Such a failure
is retryable only if complete cleanup proves that no callback and no physical
resource survived the attempt.

Recovery permits:

- one immediate acquisition attempt;
- one retry after 1 second; and
- one final retry after 2 additional seconds.

The waits are interruptible by foreground publication and shutdown. Focus loss
during a wait or before `Start` cleans the partial attempt, returns to
`Suspended`, and prevents further attempts until another regain. A failed
attempt already made remains diagnostic evidence, but suspension itself is not
a retry failure.

After `Start` is invoked, retry is forbidden. Start failure, callback-stability
failure, invalid callback/clock/render behavior, or unsafe cleanup is fatal.
If all three clean pre-Start attempts fail while foreground, the backend is
fatal. There is no indefinite recovery loop.

During acquisition waits, detached logical rendering continues. At handoff:

1. Detached production is quiesced at a committed logical boundary.
2. The first accepted physical callback establishes the new raw-driver sample
   epoch relative to the persistent logical frame.
3. Any elapsed interval not yet rendered is represented exactly once before
   the hardware block is committed.
4. Subsequent callbacks must agree with that mapping and advance continuously.

The mapping does not require a replacement driver to reuse the previous raw
sample position. It requires only that the persistent logical timeline never
move backward, double-count, or omit elapsed logical frames.

## Failure classification

Only these failures are retryable, and only during `Recovering` before
`Start`:

- driver creation/open failure;
- format/latency/buffer negotiation failure that leaves the persistent
  configured contract unchanged; and
- buffer-creation or callback-installation failure with proven complete
  cleanup.

These conditions are immediately fatal:

- any startup failure;
- `Start` failure or callback-stability failure;
- ASIO reset, resync, sample-rate, buffer-size, or latency-change request while
  running;
- invalid buffer index, repeated buffer, callback overlap, missed complete
  period, invalid driver clock, sample-position discontinuity, render gap, or
  required `outputReady` failure;
- mixer/render/sequencer/exact-clock publication failure;
- changed persistent format or endpoint contract;
- foreground-monitor failure or impossible focus generation;
- inability to quiesce callbacks or safely close a physical session;
- checked-arithmetic, allocation, or thread-entry failure that invalidates
  logical state.

An overload notification or one slow callback is diagnostic data only. It is
not fatal without a separate violated contract. Conversely, a real contract
violation is not made recoverable merely because a focus event happened near
it.

No exception may cross an ASIO callback, COM method, exported DirectSound
method, WinEvent callback, or thread entry point.

## Judgement-clock behavior and workload

`ExactAsioClock` retains one provider identity and endpoint generation for the
logical backend lifetime. Accepted physical callbacks and detached logical
renders both add anchors to that same logical frame domain. Recovery changes
only the physical-session generation.

Stage activation resolves its immutable entry timestamp only until an active
stage binding is established. After activation, each outer loop validates that
the current provider identity/generation still matches the bound provider and
uses the bound stage anchor directly. It must not repeatedly resolve the old
entry timestamp.

This preserves the fatal identity contract while removing the age-dependent
full-history scan. A song lasting longer than the exact-history window must not
produce `HistoryLost` merely because its already-consumed entry timestamp has
aged out.

Focus suspension does not reset the binding. Exact anchors from detached
logical rendering allow input timestamps during suspension to remain in the
same judgement domain; recovery resumes physical anchors without a new stage
clock.

## Diagnostics

Logs are transition-oriented rather than callback-oriented. They record:

- startup acquisition, Start, and stability result;
- every published focus loss generation and current foreground state;
- lifecycle transitions with physical-session generation;
- completed callback quiescence and physical release;
- detached logical frame totals and exact-anchor totals;
- each recovery attempt, typed pre-Start failure, scheduled delay, and outcome;
- physical-to-logical handoff coordinates;
- final typed fatal reason;
- confirmation that no alternate backend was selected.

Runtime summaries retain callback cadence, driver-time error, sample-position
discontinuities, render gaps, driver requests, and exact-clock history results.
Elapsed duration may describe an interval, but never justify a focus or fault
classification.

## Automated verification boundary

Automated coverage is limited to behavior with an independent local oracle:

1. A focused concurrency test blocks the consumer, publishes loss then regain,
   and reads once afterward. It must observe foreground true and a loss
   generation increment. The expected result comes from the two published
   transitions, not from production-derived expected data.
2. Existing logical-sequencer and exact-clock tests remain authoritative for
   hand-derived 48 kHz/frame arithmetic. They are extended only if the changed
   contract exposes a currently failing assertion.
3. No fake IASIO suite, source-text assertion, sleep-based timing test, or
   test-only production hook is added merely to increase test count.

CLion diagnostics are loaded one changed file at a time by opening that file
before requesting diagnostics. Static verification also includes focused diff
review, `git diff --check`, and complete x86 Debug and Release builds using the
local ASIO SDK. These checks do not claim that a real driver transferred
ownership correctly.

## Ordered runtime acceptance

Runtime acceptance uses the deployed artifact identity and loader log as the
authority. It proceeds in the user-selected order.

### Run 1: lose focus shortly after startup

1. Start normally and move the game to background shortly afterward.
2. Startup must still acquire ASIO, call `Start`, and prove stable callbacks.
3. The pending loss/background state must then cause exactly one safe release.
4. While backgrounded, logical, presented, and exact time must continue.
5. On regain, ASIO must recover through the immediate normal attempt, preserve
   endpoint/exact generations, and resume audible output at the current logical
   position.
6. No WASAPI record, unbounded retry, duplicated voice interval, judgement-time
   loss, or fatal contract error may appear.

The two delayed retries exist defensively but are not expected in this run. If
they are routinely exercised, the design or implementation is still wrong.

### Run 2: all-foreground full session

Only after Run 1 passes, play at least two songs while remaining foreground,
including one song longer than 60 seconds. The log must show:

- no focus recovery or physical-session replacement;
- no exact `HistoryLost` or discontinuous result;
- no render gap or sample-position discontinuity;
- no ASIO reset/resync/rate/buffer/latency request;
- stable callback and logical-frame progression;
- no workload that grows with stage age; and
- judgement behavior unaffected by a transient game-frame drop.

Static/build success remains separate from both runtime acceptance runs.

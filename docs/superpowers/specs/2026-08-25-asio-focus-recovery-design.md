# ASIO Session Loss and Recovery Design

## Context

The ASIO backend currently models focus loss as a special recovery path while
treating a driver or callback fault in the foreground as terminal. It also
lets physical callbacks and background advancement manipulate the persistent
mixer through separate, implicit ownership paths.

The reproduced failure proves that reopening IASIO is not sufficient recovery.
After a successful focus-driven reacquisition, the driver continued delivering
healthy callbacks while one candidate buffer's exact-playback publication
failed. A mixer-global sticky latch then substituted silence for every output
block. Physical-session lifetime, logical rendering, and judgement continuity
are therefore coupled at the wrong ownership boundaries.

ASIO focus handling remains an ownership lifecycle, not a clock-health
inference. Foreground state decides whether the loader should voluntarily own
IASIO. Explicit driver notifications and callback/session failures are separate
authorities that can invalidate a physical session even while the game remains
foreground. Driver time, callback cadence, and elapsed time must never be used
to infer foreground state.

## Approved behavior

- Losing foreground ownership fully stops and releases the current IASIO
  session so another ASIO client may acquire the driver.
- The DirectSound facade, mixer, active voices, logical output-frame domain,
  and exact judgement-clock provider survive the release.
- While no IASIO session exists, active voices advance through silent,
  discarded mixer renders.
- Regaining foreground ownership starts acquisition of a fresh IASIO session.
  Temporary acquisition failures are retried while the game remains
  foreground. A recovery delay is acceptable.
- A reset, resync, sample-rate change, buffer-size change, latency change,
  callback-contract failure, invalid driver clock, or output-ready failure
  invalidates only the physical session. The loader releases that session and
  retries ASIO while the game remains foreground.
- An overload notification is diagnostic evidence, not by itself proof that
  the session is lost. It does not stop rendering or trigger recovery unless a
  separate callback/clock contract actually fails.
- Losing foreground during initial ASIO acquisition or callback stabilization
  commits the logical ASIO backend in the same background-silent state. It
  never changes the selected backend to WASAPI.
- A fresh physical IASIO session does not create a fresh logical endpoint or
  exact-clock generation. Mid-song judgement state is bound to the existing
  provider and generation and must remain recoverable.
- Configured ASIO never starts WASAPI. Initial foreground acquisition may fail
  startup; after the logical ASIO backend commits, physical acquisition
  failures remain in ASIO recovery.
- Logical-engine corruption, checked-arithmetic failure, foreground-monitor
  failure, or inability to preserve the persistent endpoint contract remains
  fatal. A physical-session failure does not.

## Foreground authority

An audio-owned monitor installs an out-of-context
`EVENT_SYSTEM_FOREGROUND` WinEvent hook on a dedicated message-loop thread.
The callback compares the foreground window's process ID with the current game
process, publishes only state changes, and signals the ASIO control thread.

The WinEvent transition drives normal release and reacquisition. If a driver
fault races the notification, the control thread directly reads the current
foreground window and process ID before classifying the fault. This is an
explicit foreground-state query, not a timing heuristic.

The monitor is owned entirely by the ASIO backend. It does not reuse or depend
on the input polling foreground policy.

## Ownership boundary

The backend is split conceptually into two lifetimes:

1. **Logical audio engine lifetime**
   - DirectSound-facing `IAudioEngineServices`
   - `AudioRenderCore`, miniaudio mixer, and every existing `MixerVoice`
   - logical 48 kHz output-frame cursor
   - presented-clock publication
   - registered `ExactAsioClock` provider and endpoint generation
   - one logical render sequencer that owns the next render frame, render-time
     anchor, and the exclusive render claim
   - foreground monitor and control thread

2. **Physical ASIO session lifetime**
   - driver registration resolution and a newly created IASIO COM object
   - `AsioSession`
   - callback runtime and worker
   - ASIO buffers and channel views
   - per-session `AsioClockTracker`
   - mapping from raw driver sample positions to the persistent logical
     output-frame domain
   - a monotonically increasing physical-session generation used for
     diagnostics and handoff validation, not as a new logical endpoint

Focus loss destroys only the physical-session lifetime. Final backend shutdown
destroys both lifetimes.

## State machine

```text
startup --foreground retained + stable callbacks--> active
startup --foreground lost--> releasing -> detached
active --foreground lost--> releasing -> detached
active --physical-session fault--> releasing -> detached
detached --logical deadline--> discard logical render -> detached
detached --foreground gained/retained--> reacquiring
reacquiring --success + stable callbacks--> active
reacquiring --temporary failure + still foreground--> detached
reacquiring --foreground lost--> detached
any state --logical-engine fatal--> final teardown
any state --backend shutdown--> final teardown
```

The retry delay schedules another acquisition attempt only while the explicit
state is foreground. Expiration of a timer is never evidence that ownership
changed.

## Logical render sequencer and handoff

The logical render cursor is monotonically increasing for the lifetime of the
backend. Every mixer render or discarded interval goes through one sequencer.
The sequencer has one non-blocking exclusive render claim, so an active ASIO
callback and detached advancement cannot enter miniaudio concurrently.

Active ASIO callbacks and detached logical renders use an explicit ownership
handoff:

- Before releasing IASIO, callback rendering is disabled, the driver is
  stopped, and the callback worker is joined.
- Detached rendering advances the mixer at block-sized render points and
  discards the samples. Its discontinuity represents every elapsed logical
  frame, including a sub-period remainder.
- Physical acquisition may block, but it does not own the logical clock. The
  first callback of the replacement session computes the frame-accurate logical
  gap from the last committed logical render to its explicit driver timestamp.
  The current hardware block renders at that logical position with the
  intervening frames represented as a discontinuity. The gap is not rounded to
  a hardware period because that would permanently discard judgement time.
- The first callback establishes a new raw-driver-to-logical mapping. Raw ASIO
  sample positions may restart from zero or an unrelated driver coordinate.
  Subsequent callbacks must agree with that mapping.
- A render plan changes sequencer state only after `AudioRenderCore::Render`
  completes. Failed or competing plans cannot partially advance the logical
  cursor.

The normal DirectSound presented cursor advances during silent rendering. The
exact judgement provider remains registered with the same generation, but no
synthetic hardware anchors are published while IASIO is absent. Exact queries
may therefore remain pending during recovery; once stable ASIO anchors resume,
the same provider can resolve new input without losing the semantic-stage
binding.

## Voice and judgement ownership

Exact playback history belongs to one `AudioCursorTimeline`; it is not a mixer
health signal. Failure to publish one candidate buffer's exact mapping marks
that timeline discontinuous and records bounded diagnostics, but audio mixing
continues for every voice. In particular, a menu or preview buffer cannot mute
the endpoint.

The native gameplay cursor selection is the authority that chooses the
timeline used for judgement. Before binding, a discontinuous selected history
is rejected. Once the resolver has established its stage anchor, that anchor
remains the gameplay clock authority. Candidate-buffer publication failures do
not alter the bound anchor and are not promoted to mixer-wide failures.

## Failure classification

- Focus loss, driver reset/resync/change notifications, and physical
  clock/callback/output failures all invalidate the current physical session.
- Stop, buffer-disposal, COM-release, acquisition, and stabilization failures
  are retained as typed recovery diagnostics. They do not destroy the logical
  engine after commit.
- Foreground-monitor, allocation, logical-render ownership, endpoint-contract,
  exact-provider, and checked-arithmetic failures remain typed logical fatal
  failures.
- Reacquisition failures are retained for diagnostics and retried while
  foreground; they do not switch to WASAPI and do not destroy logical state.
- Configured ASIO is strict. Initial acquisition failures while foreground are
  fatal, and neither startup nor recovery may instantiate WASAPI.
- Foreground loss during initial acquisition or callback stabilization is a
  suspended ASIO state, not an ASIO startup failure.
- No exception may cross a callback, COM method, exported DirectSound method,
  WinEvent callback, or thread entry point.

## Diagnostics

Logs record state transitions rather than per-callback activity:

- foreground loss and completed IASIO release;
- foreground gain and reacquisition start;
- the first failed reacquisition attempt;
- successful restoration with attempt count;
- physical-session loss reason and physical-session generation;
- final fatal failures only for logical-engine failures.

Runtime counters include focus losses, physical-session losses and generations,
releases, discarded logical frames, reacquisition attempts, failures, and
successes. Mixer counters distinguish per-timeline exact-history failures from
endpoint render failures. No elapsed duration is presented as proof of
foreground loss or recovery.

## Verification boundary

The repository has no automated oracle for real IASIO ownership transfer.
Automated tests cover only the production logical-render sequencer and the
observed per-timeline mixer-isolation regression, using hand-derived
frame/timestamp and rendered-audio expectations. They do not mock an ASIO
driver or claim runtime recovery acceptance.

Static verification consists of CLion diagnostics, focused diff review,
`git diff --check`, and complete x86 Debug and Release builds with the local
ASIO SDK. Runtime acceptance requires a deployed game run that demonstrates:

1. active gameplay audio before focus loss;
2. explicit foreground-loss logging followed by IASIO release;
3. silent logical advancement while backgrounded;
4. fresh IASIO acquisition after focus regain;
5. audio resuming near the current game position;
6. the same exact-clock generation before and after recovery;
7. no absolute-judgement fatal assertion during the transition.
8. a foreground ASIO reset/session fault releases and reacquires ASIO without
   starting WASAPI or replacing the logical endpoint generation;
9. an unrelated candidate timeline failure cannot silence the mixer.

An initial-background run additionally requires ASIO to commit suspended,
report foreground loss and physical-session release, then reacquire ASIO after
foreground regain without any WASAPI startup record.

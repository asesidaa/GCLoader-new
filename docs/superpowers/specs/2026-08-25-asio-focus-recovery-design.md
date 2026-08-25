# ASIO Focus Recovery Design

## Context

The ASIO backend currently treats any committed runtime-clock fault as a
terminal backend failure. Runtime evidence shows the Xonar callback stream can
remain otherwise continuous immediately after the game loses foreground
ownership, while the driver-provided time information becomes unusable. The
backend then tears down permanently and has no path to reacquire the driver.

ASIO focus handling is an ownership lifecycle, not a clock-health inference.
The game becoming or ceasing to be the foreground process is the authority.
Driver time, callback cadence, and elapsed time must never be used to infer
foreground state.

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
- A fresh physical IASIO session does not create a fresh logical endpoint or
  exact-clock generation. Mid-song judgement state is bound to the existing
  provider and generation and must remain recoverable.
- A real runtime fault while the game is confirmed foreground remains fatal.

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
   - foreground monitor and control thread

2. **Physical ASIO session lifetime**
   - driver registration resolution and a newly created IASIO COM object
   - `AsioSession`
   - callback runtime and worker
   - ASIO buffers and channel views
   - per-session `AsioClockTracker`
   - mapping from raw driver sample positions to the persistent logical
     output-frame domain

Focus loss destroys only the physical-session lifetime. Final backend shutdown
destroys both lifetimes.

## State machine

```text
startup -> active
active --foreground lost--> releasing -> background_silent
background_silent --foreground gained--> reacquiring
reacquiring --success + stable callbacks--> active
reacquiring --temporary failure + still foreground--> background_silent
reacquiring --foreground lost--> background_silent
active --foreground-confirmed runtime fault--> fatal teardown
any state --backend shutdown--> final teardown
```

The retry delay schedules another acquisition attempt only while the explicit
state is foreground. Expiration of a timer is never evidence that ownership
changed.

## Mixer and cursor continuity

The logical render cursor is monotonically increasing for the lifetime of the
backend. Active ASIO callbacks and background silent renders are mutually
exclusive writers:

- Before releasing IASIO, callback rendering is disabled, the driver is
  stopped, and the callback worker is joined.
- Background silent rendering then advances the mixer in complete configured
  ASIO periods and discards the samples.
- Before starting a replacement session, silent rendering catches up to the
  current logical target and stops.
- Stable callbacks from the replacement session are rebased onto the next
  logical output frame. Raw ASIO sample positions may restart from zero or an
  unrelated driver coordinate.

The normal DirectSound presented cursor advances during silent rendering. The
exact judgement provider remains registered with the same generation, but no
synthetic hardware anchors are published while IASIO is absent. Exact queries
may therefore remain pending during recovery; once stable ASIO anchors resume,
the same provider can resolve new input without losing the semantic-stage
binding.

## Failure classification

- A clock/callback fault is an expected session-loss signal only when an
  explicit foreground query says the game is not foreground.
- Stop, buffer-disposal, COM-release, monitor, allocation, arithmetic, and
  synchronization failures remain typed failures.
- Reacquisition failures are retained for diagnostics and retried while
  foreground; they do not switch to WASAPI and do not destroy logical state.
- Initial ASIO startup retains the existing startup fallback policy.
- No exception may cross a callback, COM method, exported DirectSound method,
  WinEvent callback, or thread entry point.

## Diagnostics

Logs record state transitions rather than per-callback activity:

- foreground loss and completed IASIO release;
- foreground gain and reacquisition start;
- the first failed reacquisition attempt;
- successful restoration with attempt count;
- final fatal failures only when foreground-confirmed.

Runtime counters include focus losses, releases, silent frames, reacquisition
attempts, failures, and successes. No elapsed duration is presented as proof of
foreground loss or recovery.

## Verification boundary

The repository currently has no `tests/` tree. This change will not recreate
a test target or add implementation-mirroring tests without an independent
oracle.

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

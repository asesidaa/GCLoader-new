# ASIO Absolute-Time Judgement Design

**Date:** 2026-08-22  
**Status:** Architecture approved in conversation; written review pending

## Purpose

Extend the existing absolute-time judgement patch from WASAPI exclusive to the
already implemented ASIO output backend. The change exists first to make a
controlled WASAPI-versus-ASIO runtime comparison possible on the local Xonar
setup. It must also remain a clear, production-usable ASIO clock path rather
than a one-off diagnostic hack.

The latest 240-FPS run confirms that the Test Mode lifecycle correction works
and that the timing bias remains similar to the earlier runs:

| Run | Configured `JudgTimeOffset` | Reconstructed raw median | MAD |
|---|---:|---:|---:|
| Earlier full play | `0 ms` | `+17 ms` | `18 ms` |
| Earlier full play | `-12 ms` | `+17 ms` | `17 ms` |
| 2026-08-22 full play | `-18 ms` | `+13 ms` | `12 ms` |

The latest full play recorded 543 timing results: 493 GREAT, 48 COOL, and 2
GOOD. Its judged median was `-5 ms`. A separate short play before entering Test
Mode had a reconstructed raw median of `+14 ms`. The bias therefore moved by
about four milliseconds but remains in the same persistent `+13..+17 ms`
region. That is large enough to justify an audio-backend comparison.

## Authority and Preservation Rule

This document supplements the current implementation authority,
`2026-08-22-absolute-time-judgement-correction-design.md`, which was assembled
from the authoritative full audit and the completed binary evidence. It
supersedes only that document's "WASAPI exclusive first" backend limit and its
ASIO non-goal. The older August 19 design remains failed/historical evidence
and is not implementation authority.

All other established goals remain authoritative:

- Every input transition is judged from an absolute timestamp, never from the
  render frame in which the game happens to consume it.
- Render FPS does not change transition time, held age, recognition ordering,
  judgement windows, or score behavior.
- Calculations after timestamp capture remain checked rational calculations;
  144, 165, 240, and other frame rates cannot accumulate frame-to-millisecond
  rounding error.
- Loader-owned scheduling, full transition history, native recognition logic,
  held-state behavior, judgement windows, scoring, and native authored cadence
  remain unchanged.
- There is no mixed native/absolute judgement fallback once absolute judgement
  is enabled for gameplay.
- The corrected semantic stage lifecycle, one continuous stage clock, ordered
  history, scheduler, native query compatibility, and failure classifications
  remain unchanged.
- The existing completed binary/input-pipeline audit and the August 21
  authoritative full audit remain evidence authority. This work does not
  reopen or replace either audit.

## Existing ASIO Facts

The ASIO audio backend is already implemented and is not being redesigned. It
already:

- opens the registered 32-bit `XONAR SOUND CARD` driver;
- renders stereo at 48 kHz into the selected planar driver buffers;
- accepts the configured 192-frame buffer, a four-millisecond callback period;
- receives `samplePosition` and `systemTime` for every callback;
- treats `samplePosition` as the presented output frame and adds the reported
  384-frame output latency only when selecting the future render span;
- publishes the existing DirectSound-compatible presented clock; and
- detects callback, sample-position, rate, render, and conversion failures.

The missing capability is historical event-time resolution. The existing
absolute judgement resolver holds a concrete `ExactWasapiClock`, and the ASIO
presented-clock publication rounds the driver's Windows timestamp to
`timeGetTime` milliseconds for current-cursor projection. It cannot currently
resolve an older input QPC timestamp.

## Approaches Considered

### Chosen: capture the native timestamp of each backend clock domain

Every actual input transition carries two adjacent observations:

- the existing raw QPC tick for WASAPI and all existing queue-age diagnostics;
- one `timeGetTime()` millisecond tick for ASIO.

WASAPI continues to resolve QPC exactly as it does now. ASIO resolves the
multimedia tick against retained driver `systemTime + samplePosition` anchors.
Both routes produce the same checked rational output-frame result consumed by
the existing song-time resolver.

This uses the Windows clock domain that the ASIO SDK explicitly requires for
`systemTime`. It introduces at most one millisecond of per-transition ASIO
quantization, which the user explicitly accepted. It does not introduce drift
or cumulative rounding.

### Rejected: callback-entry QPC as the ASIO presentation timestamp

The ASIO callback runtime already reads QPC for cadence diagnostics. That QPC
records when the host thread entered the callback, not the physical time to
which the driver's `samplePosition` refers. Callback scheduling latency would
therefore become an unknown constant and variable judgement offset. Such an
offset could masquerade as the audio latency this comparison is intended to
measure.

### Rejected: current presented clock or stock judgement

The current ASIO `CurrentOutputFrame()` path has no retained event history and
uses last-value/monotonic cursor semantics. Stock judgement at 240 FPS is
already known to be unsuitable. Neither is an acceptable fallback or
comparison route.

## Clock Resolution Terminology

Three different ASIO quantities must not be conflated:

- **Input timestamp quantum:** 1 ms through `timeGetTime()` while absolute ASIO
  judgement is active.
- **ASIO callback period:** 192 frames / 48 kHz = 4 ms on the current setup.
- **Reported ASIO output latency:** 384 frames / 48 kHz = 8 ms on the current
  setup.

The one-millisecond decision concerns only the absolute timestamp attached to
an input transition. It does not make the ASIO callback one millisecond and it
does not change any judgement window.

For example, transitions observed at real times `100.1234 s` and `100.1239 s`
may both carry multimedia tick `100123`. Their journal sequence still preserves
which transition happened first, but ASIO assigns both the same absolute
millisecond coordinate. A later transition at `100.1261 s` carries `100126`.
No earlier transition's rounding affects any later one.

## Architecture

```text
1000-Hz input publication
    -> {QPC tick, multimedia millisecond, sequence, held masks}
    -> retained transition journal
    -> backend-neutral exact output clock
         WASAPI: event QPC -> endpoint/QPC history -> rational output frame
         ASIO: event multimedia tick -> systemTime/samplePosition history
               -> rational output frame
    -> existing output-frame/source-frame playback history
    -> existing rational judgement seconds
    -> existing history, held-age, recognition, grade, and score logic
```

### Backend-neutral provider contract

Introduce a small abstract exact-output-clock contract owned by `Audio`, not by
the judgement patch. It accepts an input timestamp containing both clock-domain
observations and returns the current endpoint-projection result states:
`Pending`, `Resolved`, `TemporarilyUnavailable`, `HistoryLost`, or
`Discontinuous`.

The provider maps absolute host time to the continuous physical output-frame
timeline. It does not classify playback and cannot delete an input event.
`NoPlayback` remains a native/playback-history observation used only while
binding the stage origin. The obsolete `OutsidePlayback` disposition remains
removed exactly as required by the correction design; a pre-origin transition
maps to a signed pre-origin judgement coordinate.

The provider also exposes:

- its backend/domain identity (`wasapi_qpc` or `asio_multimedia_ms`);
- the existing nonzero endpoint generation;
- the QPC frequency retained for queue-age and lifecycle diagnostics; and
- its publication count.

A backend-neutral registry owns only a weak reference to the active provider.
The audio backend owns the provider's lifetime. Registration, acquisition,
invalidation, and generation matching retain the current no-hot-swap rules.
The judgement scheduler no longer includes or names `ExactWasapiClock`.

The existing `endpoint_generation` terminology remains. In this codebase it
already identifies the active physical audio-output generation and is valid for
both WASAPI endpoints and the ASIO device stream. Renaming the complete audio
cursor timeline would add unrelated risk without improving this design.

### WASAPI provider

`ExactWasapiClock` implements the new interface. Its anchor format,
`IAudioClock` QPC mapping, history capacity, checked arithmetic, publication
timing, and runtime behavior do not change. Its resolver reads only the QPC
member of the input timestamp.

This preservation is important: enabling ASIO support must not change the
already runtime-accepted WASAPI result.

### ASIO provider

Add a dedicated `ExactAsioClock`. It is created only when absolute judgement is
enabled and ASIO is the active backend. It preallocates a bounded anchor ring at
startup and performs no allocation, locking, formatting, or logging from an
ASIO callback.

Each stable callback publishes:

- endpoint generation;
- monotonically increasing publication sequence;
- driver `samplePosition`, which is the presented output frame;
- the full driver `systemTime` value;
- the submitted output tail after the block has been rendered successfully.

The provider does not substitute callback-entry QPC. The existing ASIO clock
tracker remains responsible for rejecting invalid, regressing, skipped, or
rate-changing callback observations before publication.

For a retained input multimedia tick, the provider chooses a valid historical
anchor and computes the signed modular multimedia-time delta. At 48 kHz:

```text
event_time_ns = multimedia_time_ms * 1,000,000
output_frame  = samplePosition
              + (event_time_ns - systemTime_ns) * 48,000 / 1,000,000,000
```

The calculation remains a checked rational. It is not rounded to a whole
output frame inside the provider. Existing submitted-tail checks decide whether
endpoint projection is resolved or pending. Playback history then binds the
stage's first qualifying group-2 `Play` origin. It never turns a successfully
retained transition into an outside-playback loss.

`timeGetTime` wraps after 2^32 milliseconds. Resolution uses signed modular
deltas and accepts only the unambiguous half-range around a retained anchor.
The ring retains approximately 60 seconds, as the WASAPI provider does, so a
49.7-day wrap cannot create cumulative error or ambiguity in a normal stage.
The raw multimedia value `0` is valid at wrap and is never asserted as a
failure.

### Multimedia timer-period ownership

When absolute judgement and ASIO are both selected, the ASIO exact-clock owner
requests a one-millisecond multimedia timer period before creating callbacks or
calling `ASIOStart`. It then creates and registers the exact provider before
`ASIOStart`, allowing priming/stable callbacks to publish anchors. The request
stays alive through `ASIOStop`, provider invalidation, and provider
unregistration, and is then paired exactly once with its corresponding release.

This owner is process/backend lifecycle state, not stage state. Card scans,
arbitrary numbers of songs, Test Mode entry, results, and transitions between
non-stage and stage do not reacquire or release the timer period.

No judgement, stage-activation, or provider-data timeout is introduced.
`Pending` waits without a deadline. This work does not alter the ASIO audio
backend's existing pre-commit startup behavior and never uses an audio-startup
deadline to infer a game lifecycle transition. The provider becomes available
through the existing ASIO commit lifecycle and is invalidated through the
existing ASIO teardown path.

### Input transport and stage-entry watermark

`GameplayTransitionRecord` gains one `std::uint32_t` multimedia millisecond
field. The input polling thread reads QPC and `timeGetTime()` adjacently at the
existing publication observation point. Only changed held masks create journal
records, exactly as today.

Semantic stage entry also captures the same two-member timestamp immediately
before acquiring the synchronized journal cutoff and before native frame-zero
input initialization. WASAPI continues to project `stage_entry_qpc`; ASIO
projects `stage_entry_multimedia_time_ms`. This preserves the corrected
watermark rule that an old retained audio epoch cannot bind a new loader stage.
The cutoff continues to count the already accepted rare handoff window; ASIO
does not add another stage-entry loss policy.

Sequence numbers remain the authoritative order when multiple transitions have
the same millisecond timestamp. The additional timestamp does not change
capacity, overflow policy, baseline materialization, raw-message queue-age
diagnostics, or the 1000-Hz polling requirement.

### Playback binding and voice creation

When the ASIO backend creates the gameplay-native candidate voice under
absolute judgement, it configures exact playback history with the ASIO
provider's endpoint generation using the same `AudioCursorTimeline` contract
already used by WASAPI.

Stage activation still requires one coherent tuple:

- input transport epoch and cutoff;
- active exact provider and endpoint generation;
- gameplay playback generation and exact history;
- stage-entry dual-domain timestamp.

For either backend, let `O(T)` be the provider's exact rational output-frame
projection of an input or stage-entry timestamp `T`. The corrected continuous
stage formula remains:

```text
J(T) = S0/Fs + GameTimeOffset/1000 + (O(T) - O0)/Fo
```

For WASAPI, `T` selects the QPC member. For ASIO, `T` selects the multimedia
millisecond member. `O(T) < O0` remains valid and produces signed pre-origin
judgement time.

The clock domain differs by backend, but lifecycle ownership does not. The
normal sequence remains non-stage -> stage -> non-stage for every song, with
Test Mode entry treated as an explicit stage termination as already fixed.

## Configuration and Route Selection

Configuration validation changes from "WASAPI exclusive only" to:

- the feature remains opt-in and disabled by default;
- `enable_absolute_time_judgement = true` accepts
  `audio_backend = 'wasapi_exclusive'` or `audio_backend = 'asio'`;
- DirectSound remains rejected because it supplies no absolute historical
  provider;
- `input_poll_hz = 1000` remains mandatory.

With absolute judgement disabled, DirectSound, WASAPI, and ASIO retain their
existing behavior. ASIO does not request the one-millisecond timer period or
create an exact judgement provider in that mode.

Startup validates the actual provider, not merely the configured string:

- configured WASAPI requires an active `wasapi_qpc` provider;
- configured ASIO requires an active `asio_multimedia_ms` provider;
- a missing provider or backend/domain mismatch stops before gameplay with a
  clear fatal diagnostic.

The existing ASIO audio controller may attempt its pre-commit WASAPI fallback
for ordinary audio behavior. Absolute judgement must not silently accept that
fallback when ASIO was configured, because the resulting run would not be an
ASIO comparison. The provider mismatch therefore stops the process before
gameplay and logs both the configured backend and active provider domain.

There is never a mid-stage ASIO/WASAPI clock switch.

## Failure Surface

Fatal conditions are limited to directly observed contract failures:

| Condition | Deterministic evidence | Action |
|---|---|---|
| DirectSound selected with absolute judgement | Parsed configuration value | Reject configuration before runtime |
| One-millisecond timer-period request fails | Non-success API result | Log API result and stop ASIO absolute startup |
| Exact provider missing | Audio hook committed but registry acquisition is empty | Log configured backend and stop before gameplay |
| Provider domain differs from configured backend | Both enum values are directly observed | Log both and stop before gameplay |
| Invalid ASIO sample/system timestamp | Existing ASIO SDK flags/conversion/clock tracker rejects it | Latch existing ASIO runtime-clock failure and invalidate provider |
| Provider generation changes during an active stage | Bound and currently observed generations differ | Use the existing discontinuity fatal path |
| Required event history has already been overwritten | Event timestamp precedes retained publication history | Use the existing history-lost fatal path |
| Checked rational or modular-delta operation cannot represent the result | Checked operation reports failure | Log operands and use the existing arithmetic fatal path |

No lifecycle state is inferred from elapsed time. No valid non-stage -> stage ->
non-stage transition is considered an error. No fallback is invented for a
failure after gameplay activation.

Success-only platform calls remain assertion/fatal contracts with clear log
records. New code does not add exception recovery. Standard-library exceptions,
if any, are allowed to reach the existing process dump boundary.

## Diagnostics

Startup emits one bounded record containing at least:

- configured audio backend;
- active exact provider domain;
- endpoint generation;
- input QPC frequency;
- ASIO multimedia timestamp quantum (`1 ms`) when applicable;
- ASIO buffer frames, callback period, and reported output latency;
- installed judgement-hook count.

Existing ASIO cumulative runtime summaries remain authoritative for callback
cadence, host/driver interval skew, render work, silence, conversion, and
runtime faults. Add only provider-level cumulative counts needed to distinguish:

- published ASIO exact anchors;
- resolved, pending, history-lost, and discontinuous event-time queries;
- maximum absolute driver-time versus expected-period error already observed by
  the callback diagnostics.

Do not add per-callback, per-input, or per-note logging. Existing bounded
judgement timing records remain sufficient for offset analysis.

All new formatting uses `std::format`. New code contains no `try`/`catch`, no
callback allocation, and no dynamic allocation per transition or judgement.
The exact-provider ring may allocate once with `std::nothrow` before the ASIO
stream commits.

## Runtime Acceptance

No emulated judgement tests are added. Previous development established that
arbitrary expected-value tests are not a product oracle for this binary-owned
game pipeline. Verification consists of:

1. x86 MSVC Release build using the persisted local build script;
2. static inspection of imports, exports, hook count, and the absence of new
   exception handlers in the changed path;
3. deployment after build verification;
4. one 240-FPS ASIO runtime session using the existing local settings:
   `XONAR SOUND CARD`, 192 frames, output channels 0/1;
5. confirmation that startup reports `configured_backend=asio` and
   `active_exact_provider=asio_multimedia_ms`, with no fallback;
6. confirmation that lifecycle, transition, exact-clock, ASIO cadence, mixer,
   and judgement drop/fatal counters remain sane;
7. a comparable full-song play while keeping `JudgTimeOffset = -18` unchanged;
8. reconstruction of raw errors by subtracting the configured offset and
   comparison against the WASAPI `+13..+17 ms` median region.

Interpretation of the comparison:

- a substantial stable movement toward zero under ASIO supports an audio-path
  contribution;
- a similar raw median under ASIO argues against WASAPI buffering as the main
  source of the bias;
- an isolated grade distribution without the raw timing records is not enough
  to decide;
- the accepted one-millisecond ASIO timestamp quantum cannot explain a stable
  thirteen-to-seventeen-millisecond difference.

## Non-Goals

- Rewriting the ASIO output backend, mixer, callback scheduler, or conversion
  path.
- Modifying the independent high-FPS framerate hooks, including `0x664DB2`, or
  changing the game's render/update frame unit.
- Changing ASIO buffer size, output channels, driver control-panel settings, or
  thread priority.
- Changing judgement windows, `JudgTimeOffset` semantics, held-age rules,
  recognition, grading, score, or authored cadence.
- Supporting DirectSound absolute judgement.
- Using callback arrival QPC as presentation time.
- Building a general timer-synchronization framework or calibrating clocks
  across sleep/resume; the game process is not expected to remain in an active
  stage across system sleep.
- Adding replay, synthetic chart emulation, arbitrary expected-value tests, or
  a new timeout.
- Removing unrelated legacy exception handling from the existing audio backend.

## Completion Criteria

The implementation is complete only when:

- existing WASAPI absolute judgement builds and retains its current provider
  behavior;
- ASIO absolute judgement passes configuration and startup with an actual ASIO
  provider;
- every ASIO transition is resolved from its captured absolute multimedia
  timestamp rather than render frame or callback arrival;
- no arithmetic accumulates frame-rate rounding;
- provider mismatch/failure is explicit and cannot silently produce a WASAPI
  run labeled as ASIO;
- build/static checks pass; and
- the user performs the ASIO 240-FPS runtime acceptance and supplies the log for
  the offset comparison.

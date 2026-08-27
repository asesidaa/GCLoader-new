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

Configured ASIO is strict for ordinary audio and absolute judgement alike.
Neither initial failure nor foreground recovery may instantiate WASAPI. A
backend/provider mismatch remains fatal as defense in depth and logs both the
configured backend and active provider domain.

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

## Post-Implementation Runtime Finding: Output Amplitude And Cabinet Volume

This section records the read-only binary and runtime audit performed after the
first ASIO gameplay session. The correction selected from this evidence is
recorded below.

The current loader returns `0xFF` for `FIO_NODE0_ANALOG2` (`0x4128`). The
`game_decrypted.exe.i64` control flow is deterministic:

1. `0x4B4EA0` reads register `0x4128` into the node-0 analog-2 field.
2. `0x633200(0)` obtains that eight-bit analog value for the audio-volume
   controller.
3. `0x6336A0` converts it to an unsigned Windows mixer value with
   `analog << 8`; `0xFF` therefore becomes `0xFF00` (`65280`).
4. `0x57BAC0` clamps every submitted channel value to `[0, 0xFFFF]` and calls
   `mixerSetControlDetails` through `0x57B410`.

Therefore the emulated `0xFF` value does control a Windows speaker-destination
volume control, and zero explains the observed native Windows mute. It does not
overflow or request amplification: `0xFF00 / 0xFFFF` is approximately
`0.9961`, or `-0.034 dB`.

The game's DirectSound voice-volume path is separate. `0x614730` multiplies
three per-voice factors and maps the result to DirectSound hundredths of a
decibel. A product at or above `1.0` becomes `DSBVOLUME_MAX` (`0 dB`), a
non-positive product becomes `DSBVOLUME_MIN`, and intermediate products are
attenuation only. The replacement facade preserves this no-amplification
contract through `DirectSoundVolumeToLinearGain` and per-voice miniaudio node
gain.

The first ASIO runtime log nevertheless proves that the post-voice float sum
can exceed full scale: 29 blocks and 108 samples clipped, with a maximum
absolute sample of `1.29017`. Callback deadline misses, render gaps, active
short reads, non-finite samples, and buffer-alternation violations were all
zero. The `Int24LSB` three-byte layout also matches the official ASIO SDK host
sample. This makes post-mix amplitude clipping a concrete defect and a strong
candidate for the reported sharp free-tap splatter; it does not yet prove that
every perceived ASIO quality difference has the same cause.

The current replacement audio path has per-voice gain but no backend-neutral
master/session gain or mastering stage. The game still issues the legacy
Windows mixer call, but the loader does not consume that value in its PCM
pipeline. Any effect on ASIO is consequently driver or hardware behavior
outside the replacement mixer. Microsoft also documents that shared-session
volume controls do not affect WASAPI exclusive streams, which require endpoint
volume or client-side gain instead.

`loader-log-1.txt` cannot settle whether WASAPI clipped during its generally
acceptable session because the WASAPI summary did not record pre-conversion
peak or clipping counters. Its occasional audible roughness is compatible with
the shared post-mix path but is not proof.

### Trial Fixed Headroom Correction

The replacement mixer applies one fixed `-3 dB` gain (`0.7079458`) to the
completed floating-point mix in `AudioRenderCore`, after render-contract
handling and before either WASAPI PCM16 or ASIO sample conversion. The
observed `1.29017` peak therefore becomes approximately `0.91337`.

This is a loader-owned output policy, not a claim that the native DirectSound
path used the same headroom. It preserves relative voice levels and applies
the same deterministic attenuation to every backend and every rendered
sample. It does not add a limiter, dynamic gain state, allocation, exception
handling, backend branch, or timing dependency. The expected cost is a uniform
`3 dB` reduction in digital output level.

Build success proves only that the shared output path compiles. Runtime
acceptance requires confirming that sharp free-tap splatter is gone or reduced
and that ASIO diagnostics report sane post-headroom peaks and clipping counts.

### Runtime Rejection of Fixed Headroom

The first `-3 dB` ASIO runtime trial on 2026-08-23 rejected clipping as the
root cause of the reported free-tap sound-quality defect. The user reported
that free taps sounded worse, not better. During the same run,
`loader-log.txt` recorded 23 free-tap transient publications in the first five
seconds of the active stage, zero clipped blocks, zero clipped samples, and a
maximum post-headroom absolute sample of `0.835736`.

The ASIO callback and render path remained structurally healthy: deadline
misses, render gaps, active short reads, mixer errors, render-contract errors,
non-finite samples, buffer-alternation violations, driver overload messages,
and sample-position discontinuities were all zero. The reusable mixer buffer
is overwritten by `ma_engine_read_pcm_frames` on every render and explicitly
zero-filled after a short read, so post-render attenuation does not feed prior
samples into later callbacks.

Therefore the trial successfully removed measured output clipping but worsened
the reported artifact. Fixed output headroom is not an accepted correction for
the free-tap defect and must not be used as evidence that the defect was
understood. The trial gain remains in the currently deployed build pending an
explicit rollback; no replacement audio fix is selected by this result.

### Aligned Runtime Capture Before Selecting The Replacement Processing

The clipping defect still requires a correction. The rejected auditory result
means only that a fixed `-3 dB` multiplier is not evidence for the processing
used by the original DirectSound path. It does not make the measured
above-full-scale sum valid and it does not authorize removing clipping
protection without a replacement.

The next ASIO runtime build therefore retains the trial gain only as a labeled
diagnostic boundary and records the first five seconds after absolute-stage
activation. The first rendered ASIO block establishes one output-frame origin
for every artifact:

- each unique voice after source-format conversion and resampling but before
  its miniaudio bus gain;
- the completed floating-point mix before the trial gain;
- the same mix after the trial gain;
- both exact non-interleaved ASIO channel buffers after sample conversion and
  immediately before submission to the driver;
- bounded CSV records for DirectSound `Play`, seek, stop, and volume calls; and
- bounded per-render records mapping output frames, source frames, playback
  generations, voice gain, and loop state.

Each voice retains its immutable `AudioSnapshot` owner during the capture. The
writer emits up to five seconds from that raw PCM source after capture
completion and records the snapshot generation both at first render and at
write time. This also includes a BGM voice whose `Play` call occurred before
stage activation.

All repeating callback work is bounded and uses memory preallocated on stage
activation. The callback performs no allocation, file I/O, logging, mutex
acquisition, or wait. A separate writer waits without a timeout and writes the
WAV, raw ASIO, metadata, and CSV files only after the full window or after a
semantic stage exit requests a partial flush. Capacity exhaustion is recorded
in metadata instead of aborting gameplay.

The capture root is
`H:\gc\audio-diagnostics\<timestamp>-stage-<generation>`. Its purpose is to
identify the first corrupt boundary, not to emulate or assert expected sound:

- clean source but corrupt voice isolates conversion, resampling, or
  retrigger/cursor handling;
- clean voices but corrupt pre-gain mix isolates overlap or mixing;
- clean post-gain mix but corrupt submitted bytes isolates ASIO conversion;
- clean submitted bytes with a live-only artifact moves the remaining fault
  boundary to the ASIO driver, selected channels, or hardware; and
- an artifact already present in the cleanly encoded pre-gain mix requires
  comparing the captured signal with the native DirectSound output before
  choosing the original-like saturation, limiting, or gain policy.

No final mastering policy is selected until the actual-game capture is
available. In particular, the capture does not assume that native DirectSound
used fixed headroom, hard clipping, a limiter, or any other guessed operation.

### Aligned Capture Result: Repeated Native Transient Consumption

The completed capture is
`H:\gc\audio-diagnostics\20260823-043732-758-stage-1`. It is complete and
continuous: all three 48 kHz boundaries contain exactly 1,250 calls and
240,000 frames, with zero gaps or overlaps, and neither the voice-render nor
control-event bounded store overflowed.

The raw assets are not the corrupt boundary. Decoded PCM hashes prove that
captured buffers `76` and `77` are exact copies of
`H:\gc\data\sound\TAP_SE1.wav`, while buffers `78` and `79` are exact copies
of `TAP_SE2.wav`. The downstream arithmetic is also exact for every captured
sample:

- the pre-gain mix equals the sum of all captured voice outputs multiplied by
  their captured per-block gains;
- the post-gain mix equals the pre-gain mix multiplied by the single
  float32 `0.7079458` trial gain; and
- both submitted `Int24LSB` ASIO channels equal the exact quantization of the
  post-gain float samples.

The first corrupt behavior is playback multiplicity. The log contains exactly
24 free-tap transient publications in the capture window: 21 right and three
left. Those map exactly by order and sound type to 24 audible tap clusters, but
the clusters contain 50 `Play` calls:

- eight clusters contain one `Play`;
- six clusters contain two `Play` calls; and
- ten clusters contain three `Play` calls.

The repeated calls alternate/reuse the native double-buffered tap instances
and restart the same clean source at frame zero roughly one 240-FPS outer call
apart. This is the captured splatter/retrigger behavior. Every pre-gain sample
above full scale occurs in a two- or three-`Play` cluster; no single-`Play` tap
cluster exceeds full scale. The pre-gain peak is `1.3511286` with 115 samples
above `1.0`. The exact trial gain reduces that peak to `0.95652586`, but it
cannot remove the temporally separated copies and restarts.

The static control flow explains the runtime counts deterministically.
`GameplayJudgementState_ProcessRecognitionStep` (`0x5D68E0`) begins a native
recognition step by clearing judgement-state bytes `+0xA9`, `+0xAA`, `+0xEC`,
`+0xED`, and `+0xEE`, clearing the byte vector at `+0xAC`, and setting the byte
vector at `+0xCC` to one. The owned hook at RVA `0x240239` instead runs zero or
more scheduled recognition scopes and redirects to the original tail at RVA
`0x2402D0` on every rendered outer call. That tail reads `+0xED`, `+0xEE`, and
`+0xAA` and issues the tap/arrange sound calls.

During the first five seconds, the hook ran 1,195 outer calls but only 347
recognition scopes (48 event scopes and 299 heartbeats). Therefore an outer
call with no scope reaches the sound-consuming tail without executing the
native clear prelude. A free-tap byte published by one event remains set and
is consumed again on each following 240-FPS outer call until the next event or
heartbeat recognition step clears it. The observed one-, two-, and three-play
cluster sizes follow that exact lifetime.

This corrects the prior diagnosis. Above-full-scale mixing is real in this
capture, but it is downstream of repeated playback rather than proof that the
replacement mixer lacks an original mastering operation. Fixed `-3 dB`
headroom is not the defect correction. The correction must first restore
one-shot native transient consumption; the trial gain should then be removed
and amplitude remeasured before selecting any separate clipping policy.

The live `game471.exe.i64` IDA daemon closes the field boundary more narrowly.
The one-shot sound-publication set is exactly the three bytes already observed
by diagnostics:

- `+0xAA`: arrange/hidden-note sound request; `sub_43BDE0` returns it only when
  the native suppression byte at `+0x189` is clear;
- `+0xED`: component/control `4` free-tap sound request, returned directly by
  `sub_43BE60`; and
- `+0xEE`: component/control `9` free-tap sound request, returned directly by
  `sub_43BE80`.

The nearby prelude state is not an interchangeable flag block. `+0xA9` is a
separate descriptor predicate publication. `+0xEC` records the aggregate
free-input result. The vector at `+0xAC` participates in native component-grade
aggregation, while the vector at `+0xCC` is initialized and refined as row
free-input/continuous state and is read by both recognition and the original
tail through `sub_43BE20`. Clearing any of those four merely because they are
near the sound bytes would change native behavior.

The correction is therefore an exact three-byte lifetime repair. At the start
of the next owned outer call, after resolving the same validated judgement
state and before running any optional scope, expire the previous outer call's
`+0xAA`, `+0xED`, and `+0xEE` publications. A new recognition scope performs
its own native clear and may republish them; the original tail then observes
that new value once. A no-scope outer call reaches the tail with zeros instead
of replaying the previous request. Clearing on the next outer call, rather
than immediately after the tail, preserves the publication for every native
consumer in the outer frame in which it was created.

The existing event-isolation rule remains necessary: an event batch contains
one recognition scope, so a physical tap or hidden-note publication reaches
its tail before any later recognition can clear it. Heartbeat catch-up retains
the original native topology of multiple recognition/score steps followed by
one tail; it does not fabricate a physical pressed edge and is not redesigned
into loader-owned sound replay.

This conclusion is recorded by the read-only IDA artifacts
`transient-publication-lifecycle-20260823-pass3.json` (SHA-256
`46682efb8b40725510c455a14333791d63fe8ef1770dda472811a613b27e74de`)
and `transient-field-xrefs-20260823.json` (SHA-256
`430e0c5fbd6afd78f231fac7dab41c6e3a8d11f37353fd8f382ce52cb89fc380`)
under the existing `20260816T210335Z-a3aabe78` run.

#### Implemented Repair (2026-08-23)

`AbsoluteJudgementRuntime::DispatchOuterCall` now expires exactly the previous
outer call's `+0xAA`, `+0xED`, and `+0xEE` publications immediately after
`ResolveNativeIdentityOrFatal` returns the validated current judgement state.
The compiled Release object contains exactly those three byte-zero stores and
calls the helper before the cursor query and any scheduled recognition scope.
No neighboring judgement state, scheduler rule, native score call, input
transport, or framerate source was changed by this repair.

The diagnostic validation build removed the rejected fixed `-3 dB`
multiplication from `AudioRenderCore::Render` and retained pre/post mix capture
with a unity (`1.0`) boundary. The subsequent 240-FPS actual-game session was
accepted by the user: the repeated/splattered free-tap and hidden-note sound
behavior was fixed.

The x86 Debug DLL, a fresh x86 Release DLL, and the matching Release ConfigGUI
all built successfully. Persisted ABI inspection passed PE32/x86/subsystem,
all exports, hook stack cleanup, required WinMM imports, source-policy checks,
and zero framerate-source drift. The Release DLL SHA-256 is
`8F28DF59454CC80EF651BAFA41266E6B81E76F1EA9A66F072EA56E0175E12625`.
That instrumented build received actual-game acceptance at 240 FPS.

The verified Release DLL and matching ConfigGUI were deployed to `H:\gc` on
2026-08-23. Candidate and deployed hashes matched. The replaced runtime pair is
recoverable from
`H:\gc\deploy-backups\asio-absolute-judgement-20260823-042705522`.

#### Accepted Diagnostic Cleanup (2026-08-23)

After runtime acceptance, the temporary aligned audio-capture source pair,
CMake entry, ASIO submission capture, mixer/voice capture, DirectSound control
capture, and judgement-stage arm/finish calls were removed. The accepted raw
capture and IDA artifacts remain as historical evidence; the production source
contains no audio-dump recorder or publication path. The remaining source diff
against the pre-diagnostic ASIO implementation is only the exact three-byte
transient-publication repair above.

The recorder-free x86 Debug DLL, fresh x86 Release DLL, and matching Release
ConfigGUI built successfully. Persisted ABI inspection again passed all x86,
PE32, export, hook cleanup, WinMM import, source-policy, and framerate-isolation
checks. The recorder-free Release DLL SHA-256 is
`2D6FD97C62B97077CE0BAAE14AE79BEA78EDFAE52141B90AE10641D0DC2530DE`.
This cleanup artifact has build/static proof; runtime acceptance belongs to the
behaviorally equivalent instrumented build immediately preceding it.

The recorder-free Release DLL and matching ConfigGUI were deployed to `H:\gc`
on 2026-08-23 with candidate/deployed hashes equal. The accepted instrumented
runtime pair is preserved at
`H:\gc\deploy-backups\asio-absolute-judgement-20260823-043911057`.

#### Multi-stage A/V Mismatch Diagnosis (2026-08-23; Code Unchanged)

The latest two-song ASIO session does not show a cumulative ASIO output-clock
error. Between the second and third absolute-stage activations, the elapsed QPC
time and elapsed ASIO output frames differ by approximately `1.3471 ms`, within
the provider's logged `1 ms` timestamp quantum. The ASIO summaries report zero
callback deadline misses, render gaps, sample-position discontinuities,
resyncs, resets, or latency-change requests. Judgement's central signed-error
bias moved only about `4-5 ms`, which cannot account for the visibly large A/V
mismatch.

The deterministic defect is in the pre-existing high-FPS shared gameplay song
clock. `GameplaySongClock` persists one `last_exact_source_frame_` across the
process and treats `playback_generation` alone as the playback epoch identity.
That generation is not global: it is a member of each `SecondarySoundBuffer`.
The latest log proves three distinct gameplay buffers (`30`, `104`, and `169`)
all reported `playback_generation=2` when their source cursors started again at
zero. `SelectGameplaySongClockInput` receives `buffer_instance_id` but drops it
when constructing `SongClockObservation`.

Consequently, the first exact observation from a later buffer is compared with
the prior buffer's high-water source frame. `GameplaySongClock::Observe`
deterministically returns `BackwardsObservation`; the hook then returns without
writing an audio-derived catch-up step. A later song can recover only after its
source cursor exceeds the preceding buffer's terminal cursor. The short first
stage ended around 13 seconds, so the first full song could resume exact clock
ownership after that point. That full song ended around 114 seconds, while the
following song lasted only about 76 seconds, so the following song could not
recover at all.

The runtime cadence corroborates the visible result. Over the logged interior
of the final stage, outer rendering averaged approximately `239.34 FPS` for
`75.039 s`. A stale/default one-tick-per-render path at a nominal `240 Hz`
therefore loses about `49.36` target ticks, or `205.7 ms`, matching a plainly
visible late visual timeline. The preceding full stage would have lost about
`490.2 ms` without audio catch-up, yet was reported visually sound; this is
consistent with exact ownership resuming after its cursor crossed the short
stage's high-water mark.

The required repair is to identify an exact playback epoch by the already
published composite `(buffer_instance_id, playback_generation)`. Buffer
instance identifiers are process-wide and non-reusing; playback generation
distinguishes play/seek epochs within one buffer. A change in either component
must start a new epoch and accept the restarted source cursor. No explicit
song-count limit or stage-number special case is permitted. This repair belongs
to the high-FPS visual song-clock path; it must not alter the absolute judgement
clock, ASIO rendering, input transport, or judgement windows.

#### Implemented Multi-stage Song-clock Repair (2026-08-23)

`SongClockObservation` now carries the published `buffer_instance_id` together
with `playback_generation`. `GameplaySongClock` retains both values as its
accepted exact playback epoch. A change in either component starts a new epoch,
accepts the restarted source cursor, and replaces the prior epoch's high-water
frame. A decreasing unwrapped source frame is still rejected when both identity
components remain unchanged. No stage counter, elapsed-time heuristic, rounded
fallback, or song-count special case was added.

The high-FPS hook emits one `gameplay_song_clock_epoch` information record when
an exact epoch changes. Existing five-second framerate summaries now also expose
cumulative `gameplay_song_clock=<epoch_changes>/reject=<observation_rejections>`
counters. These diagnostics prove whether each gameplay buffer acquires exact
clock ownership without logging every cursor observation.

Fresh x86 Debug and Release builds rebuilt `GameplaySongClock.cpp` and
`FrameratePatch.cpp` successfully. Both touched translation units compiled with
no MSVC warnings. The persisted source-policy/ABI inspection passed PE32/x86,
DLL subsystem, exports, required WinMM imports, absolute-judgement hook stack
cleanup, the no-new-`try`/`catch` rule, and the no-new-stream-formatter rule.
The Release DLL SHA-256 is
`B880D88B374E5BC3B0C2DC4E23EAF355F2E757BEF94F6388CD77782D5819BD84`.

The Release DLL and matching ConfigGUI were deployed to `H:\gc`; candidate and
deployed hashes match. The replaced runtime pair is recoverable from
`H:\gc\deploy-backups\asio-absolute-judgement-20260823-213046404`. Static and
build verification is complete; multi-song 240-FPS behavior remains the runtime
acceptance gate.

#### Multi-song Runtime Acceptance (2026-08-23)

The deployed repair completed a two-song 240-FPS ASIO session recorded in
`H:\gc\loader-log.txt` through `21:42:04`. The run reproduced the exact former
collision: stage 1 used buffer `77` with `playback_generation=2`; stage 2 used
buffer `145`, also with `playback_generation=2`, and restarted at source frame
zero. The hook published one new exact epoch at each stage activation. Stage 2
was accepted immediately with `source_frame=0`, `current_tick=0`,
`desired_tick=0`, and `step=0`. Every five-second summary through session end
reported zero song-clock observation rejections.

The render cadence makes this a discriminating runtime test. Logged outer-frame
cadence averaged about `238.79 FPS` over the interior of stage 1 and `239.09 FPS`
over stage 2. A remaining one-tick-per-render fallback would therefore have
accumulated approximately `656.8 ms` and `396.5 ms` of visual delay. The user
reported the session mostly fine, while exact ownership remained active with no
rejections; the former cross-buffer lifecycle defect is runtime-accepted.

Lifecycle accounting was balanced at two semantic opens, two absolute
activations, and two semantic ends, with no forced termination, fatal, or error
record. ASIO reported zero callback deadline misses, render gaps,
sample-position discontinuities, resync/reset requests, unmapped cursor
failures, mixer errors, or render-contract errors. The run used
`game_time_offset_ms=0` and native `JudgTimeOffset=-12`; every one of the
1,003 timing records had `recognition_ms - native_ms = -12`. The post-offset
signed-error medians of `-6 ms` in stage 1 and `0 ms` in stage 2 therefore
correspond to pre-`JudgTimeOffset` raw medians of `+6 ms` and `+12 ms`.
The twenty-second buckets did not show the former monotonic loss of
synchronization.

Residual input diagnostics were small and non-systemic: stage 1 recorded seven
late records out of `1237`, and stage 2 recorded four out of `1142`. None were
dropped; sequence errors, overload drops, cleanup drops, clock-unavailable
reads, and final-accounting mismatches were all zero. These isolated records do
not reopen the song-clock defect, but remain relevant if a specific judgement
anomaly is later reported.

#### Judgement-offset Attribution Investigation (2026-08-23; Code Unchanged)

The timing records distinguish native calibration from the underlying phase
error. For each record:

```text
signed_error_ms = recognition_ms - note_target_ms
JudgTimeOffset   = recognition_ms - native_ms
raw_error_ms     = native_ms - note_target_ms
                 = signed_error_ms - JudgTimeOffset
```

A positive raw error means the captured input transition occurred after the
authored note target. Medians over all records are used because they remain
stable in the presence of isolated human misses; the median absolute deviation
(MAD) records the natural spread without pretending that every play was
perfect.

| Log | Output path | Native offset | Records | Raw median | Raw MAD | 10% trimmed raw mean |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| `loader-log-240.txt`, stage 1 | WASAPI exclusive, Arctis Nova Pro Wireless | `0 ms` | 534 | `+16 ms` | `17 ms` | `+15.01 ms` |
| `loader-log-1.txt`, stage 1 | WASAPI exclusive, Arctis Nova Pro Wireless | `-18 ms` | 527 | `+14 ms` | `13 ms` | `+14.96 ms` |
| `loader-log.txt`, stage 1 | ASIO, XONAR SOUND CARD | `-12 ms` | 551 | `+6 ms` | `11 ms` | `+7.24 ms` |
| `loader-log.txt`, stage 2 | ASIO, XONAR SOUND CARD | `-12 ms` | 452 | `+12 ms` | `12 ms` | `+12.24 ms` |
| `loader-log.txt`, both stages | ASIO, XONAR SOUND CARD | `-12 ms` | 1,003 | `+8 ms` | `12 ms` | not used |

The two WASAPI runs retain essentially the same approximately `+15 ms` raw
center despite using different native offsets. The bias is therefore not an
artifact of reading the already-adjusted judgement result. The ASIO run moves
toward zero, but the sessions used different charts and also changed the output
device from a wireless Arctis endpoint to the Xonar. This is evidence for an
audio/output-path contribution, not a controlled measurement of either the
WASAPI API or the ASIO API in isolation.

The latest ASIO run's combined `+8 ms` raw median numerically equals the
driver's 384-frame output latency at 48 kHz, but that equality is not evidence
that the implementation omitted the latency. The Steinberg ASIO 2.3
specification defines the timestamped sample position as the first frame of the
buffer passed to the callback and defines output latency as the delay from that
buffer switch until the buffer reaches output. The implementation therefore
uses `sample_position` as the presented timeline at callback time and renders
the future span beginning at `sample_position + output_latency_frames`.

For example, at callback position `P` and time `T`, the current code renders
content for `P + 384`. The driver reports that this buffer starts sounding
384 frames, or 8 ms, later. At `T + 8 ms`, the exact clock has also advanced
from `P` to `P + 384`, so the rendered content and clock agree. Adding or
subtracting the same 384 frames a second time would create an 8 ms error. A
driver may still report an inaccurate physical latency, but software logs
cannot infer that merely because the player's median has the same numeric
value.

Loader-side transport does not track the raw error consistently. Splitting
each of the four stage populations at its median loader delivery delay changed
the raw-error median by `+5`, `-0.5`, `0`, and `+2 ms`, respectively.
Comparing zero-age raw-input messages with the sparse `15..16 ms` queue-age
messages changed it by `+2`, `-1.5`, `+8`, and `-6 ms`. The direction
reverses between sessions. The latest two-stage run also had zero sequence
errors or transition drops. These results do not support loader delivery or
the Windows message-queue residence time as the stable offset source.

The current instrumentation begins after Windows has delivered `WM_INPUT`.
`OnRawInput` measures only `GetTickCount() - GetMessageTime()`; after packet
decode and mapping, `Publish` samples QPC and multimedia time for the
transition. It therefore cannot observe switch travel, keyboard scan/debounce,
USB report scheduling, or any earlier device-to-Windows delay. On the other
side, the exact audio providers follow the position reported by the audio
driver. Any device, wireless/DSP, analog, or acoustic delay that is not
represented in that reported position is also outside the log boundary.

Consequently, the present evidence does not identify a single `12 ms`
keyboard or human offset. It identifies a small, stable end-to-end phase error
made from some combination of player response, pre-`WM_INPUT` input latency,
display timing, and unreported output latency. A `12 ms` center is not large
relative to the same sessions' `11..17 ms` MAD and is entirely plausible as a
calibration value.

The latest ASIO run is also not consistent with one fixed hardware delay being
the entire result. With the same keyboard, output device, backend, and native
offset, consecutive 20-second chart buckets had these raw medians:

| Stage | Consecutive 20-second raw medians |
| --- | --- |
| 1 | `+9`, `+16`, `+4`, `+6`, `+8.5`, `+1`, `+3.5 ms` |
| 2 | `+8`, `+6`, `+4.5`, `+21`, `+19.5`, `+13 ms` |

The values neither remain constant nor drift monotonically. A fixed input or
output latency could still shift every bucket by a common baseline, but it
cannot by itself produce the section-dependent movement. The calibrated offset
is therefore an end-to-end player/setup value, not a direct measurement of one
device's latency.

A read-only Windows device enumeration on 2026-08-23 identified the present
non-Razer keyboard HID parent as `BKB02 Wireless Dongle`
(`VID_369B/PID_F1F4`). The historical timing records do not retain the Raw
Input `hDevice`, so this does not formally prove which device generated those
taps. If the BKB02 was used, however, its wired-versus-2.4 GHz modes provide a
cleaner input-path comparison than swapping to a different keyboard.

The next software-only isolation must keep the chart, keyboard, display,
physical output, and native offset fixed:

1. if the BKB02 generated the recorded taps, compare its wired and 2.4 GHz
   modes while retaining Xonar ASIO;
2. compare the Xonar Windows endpoint in WASAPI exclusive mode with the Xonar
   ASIO driver;
3. alternate multiple runs of the same full song and compare the pre-offset raw
   median and MAD, not grade counts alone;
4. if the center follows the backend while the physical device is fixed,
   investigate provider/driver timeline semantics; if it does not, the current
   Arctis-versus-Xonar difference belongs to the output devices;
5. only then swap the keyboard to measure a model-dependent delta.

Separating the final common offset into human intent versus physical
switch-to-sound latency requires an external physical reference such as a
contact/photodiode and audio-loopback measurement. Software timestamps cannot
observe human intent or the physical endpoints.

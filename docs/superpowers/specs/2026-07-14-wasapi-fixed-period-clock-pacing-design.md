# WASAPI Fixed-Period Clock Pacing and Cursor Hardening Design

Date: 2026-07-14

## Status and relationship to the original design

This document is a focused follow-up to
[`2026-07-12-wasapi-exclusive-low-latency-audio-design.md`](2026-07-12-wasapi-exclusive-low-latency-audio-design.md).
It does not replace the selected DirectSound facade, miniaudio mixer, or
exclusive event-driven WASAPI architecture. It hardens the relationship between
the endpoint presentation clock, mixer source time, and DirectSound cursors.

The first implementation reached the endpoint successfully with the advertised
minimum buffer of 132 frames, approximately 3 ms at 44.1 kHz, but runtime event
cadence was only approximately 200 callbacks per second. Submitting 132 frames
per callback at that cadence advances only 26,400 frames per second, which
matches the observed slowed and distorted playback.

The deployed 10 ms build instead reported:

- 441 endpoint frames;
- 100 callbacks per second;
- 44,100 submitted frames per second;
- no endpoint HRESULT failure;
- no audible cracking in the operator's retest.

This proves that 10 ms is a stable default for the tested XONAR endpoint. It
does not make callback count the authority for elapsed audio time, and it does
not prove a numeric end-to-end latency.

## Decisions

- The exclusive buffer remains a user-tunable, fixed duration in milliseconds.
- The distributed default remains 10 ms.
- Zero is invalid; it no longer means "use the endpoint minimum."
- A configured duration below the endpoint-reported minimum is rejected rather
  than clamped.
- There is no period probing, candidate negotiation, automatic buffer increase,
  runtime adaptation, or backend fallback.
- The documented WASAPI alignment retry is the only allowed adjustment to the
  configured duration.
- The normal render path remains one complete fixed-size endpoint buffer per
  event.
- `IAudioClock` is the authority for presentation progress.
- A confirmed lost output interval advances already-playing source timelines so
  tempo and synchronization remain correct, even though the loss can cause a
  brief audible discontinuity.
- An explicit game `Play` or `SetCurrentPosition` request takes precedence over
  an older automatic gap correction for that voice.
- The binary's `SkipMargin` and `SkipInterval` behavior remains unchanged.
- Repeated confirmed output gaps fail with guidance to increase the configured
  buffer; the hook never silently changes it.
- Automated tests cover deterministic, non-game behavior only. Operator-run
  in-game testing is the final acceptance evidence.
- Physical loopback measurement is not required. Low latency is judged by the
  operator in gameplay, and the project does not claim a numeric end-to-end
  latency.

## Goals

- Preserve correct pitch, tempo, and logical audio time when the hardware clock
  proves that an output interval was lost.
- Preserve the low-overhead fixed-buffer fast path when endpoint pacing is
  healthy.
- Make DirectSound play cursors reliable immediately after game-driven play and
  seek operations.
- Separate expected post-seek cursor states from genuine timeline failures.
- Distinguish QPC scheduling lateness from confirmed endpoint starvation.
- Fail clearly when a user-selected duration is chronically unsafe on the
  active endpoint.
- Add enough software observability to explain pacing and cursor behavior
  without adding render-thread logging.

## Non-goals

- Selecting a buffer duration on behalf of the user.
- Trying multiple exclusive periods during startup.
- Changing the buffer duration while the process is running.
- Adding a software staging ring or a second mixer thread.
- Switching to shared WASAPI, `IAudioClient3`, DirectSound, or another backend.
- Modifying the game's `SkipMargin`, `SkipInterval`, `GameTimeOffset`, judgement
  windows, or 120 FPS timing policy.
- Time-stretching audio to conceal an underrun.
- Rendering skipped samples into a throwaway buffer merely to advance time.
- Claiming a physical input-to-speaker or sample-to-speaker latency.

## Game-level BGM synchronization

The binary function `GC120FPS_GameplayAudioSync_CheckAndSeek` at `0x00640070`
implements a BGM synchronization watchdog:

1. It derives expected song time from the gameplay frame counter.
2. It subtracts `GameTimeOffset`.
3. It obtains DirectSound group 2's play cursor in milliseconds.
4. It seeks the stage-BGM group when the absolute difference exceeds
   `SkipMargin`.
5. It can also seek periodically according to `SkipInterval`, even when the
   difference is inside the margin.

The existing 120 FPS patch clamps a positive `SkipMargin` to at least 48 ms and
scales `SkipInterval` so its real-time frequency is preserved at 120 Hz. The
resync hook in `FrameratePatch.cpp` only observes and logs the final seek site.

This watchdog is not an endpoint buffer policy. It is a game-level, BGM-only
hard resynchronization mechanism. The new endpoint pacing correction instead
protects every voice that was continuously playing across a confirmed output
gap. In healthy operation it does nothing. If the game issues a BGM seek, the
new game epoch supersedes the automatic correction so the same elapsed time is
never applied twice.

## Fixed-period configuration contract

`experimental.wasapi_exclusive_buffer_ms` remains an unsigned integer and is
read once during audio-hook initialization.

Validation is strict:

- `0` is invalid and fails initialization with an actionable configuration
  message.
- The requested milliseconds must convert safely to `REFERENCE_TIME`.
- After querying `GetDevicePeriod`, a request below the reported minimum fails
  and logs both durations.
- The implementation passes the requested duration unchanged as both
  `hnsBufferDuration` and `hnsPeriodicity`.
- If initialization returns `AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED`, the existing
  documented alignment dance obtains the required frame count, reactivates the
  audio client, converts that frame count back to a duration, and retries once.
- After a successful retry, the returned aligned frame count must match the
  final `GetBufferSize` result.
- Without an alignment retry, the actual frame count must be either the floor
  or ceiling of `requested_duration * 44100 / 10000000`. Any other result is an
  unexplained endpoint adjustment and a startup failure.
- The final `GetBufferSize` result is the one fixed packet size used for the
  process lifetime.

There is no `max(configured, minimum)` clamp. There is no hidden safety period.
The startup error tells the operator to select a supported positive value, with
10 ms remaining the recommended default.

## Selected architecture

The existing units keep their responsibilities:

- `WasapiEndpoint` owns exclusive initialization, full-buffer access,
  `IAudioClock`, MMCSS, and endpoint failure reporting.
- `ExclusiveAudioEngine` owns the audio thread, packet loop, submitted output
  timeline, pacing policy, and non-real-time failure handoff.
- `MiniaudioMixer` owns voices, source conversion, looping, seeking, and source
  timeline advancement.
- `AudioCursorTimeline` maps endpoint output frames back to each source's
  original frame and byte domain.
- `SecondarySoundBuffer` owns DirectSound control serialization and the
  game-visible cursor contract.

Add one small deterministic component, `OutputPacingTracker`, between the
mapped endpoint clock and the mixer. It owns only integer timeline state. It
performs no allocation, logging, COM calls, or synchronization.

No new producer thread, staging queue, or software buffer is introduced.

## Output-frame model

Let:

- `B` be the actual endpoint buffer size in frames;
- `P` be the current `IAudioClock` position mapped into the engine's 44.1 kHz
  output-frame domain;
- `T` be `submitted_tail`, the exclusive end of all source material already
  released to WASAPI.

After prefill and before `IAudioClient::Start`, the audio thread reads
`IAudioClock` and maps that still-stopped stream position to logical output
frame 0. Microsoft defines a newly created stream's device position as zero and
starts uniform advancement only after `Start`. This makes the prefilled silent
buffer logical output slot `[0, B)`, so startup sets `T = B`. Every subsequent
block boundary is an integer multiple of `B` in this logical domain.

The first successful clock sample after `Start` seeds QPC wake diagnostics; it
is not compared with the pre-start QPC value. This prevents endpoint startup
propagation time from being classified as a late render wake.

For each render event, the pacing tracker computes:

```text
if P <= T:
    block_begin = T
    discontinuity_frames = 0
else:
    block_begin = align_up(P, B)
    discontinuity_frames = block_begin - T

block_end = block_begin + B
```

All arithmetic is overflow checked. The tracker also remembers the previous
mapped presentation position and rejects regression.

The ordinary case is therefore unchanged: render `[T, T + B)` and advance the
tail once. QPC wake jitter that leaves presentation inside the submitted region
does not alter source time.

When `P > T`, hardware presentation has crossed the end of every frame the
engine had submitted. The next available packet is aligned to the next logical
packet boundary. The interval `[T, block_begin)` is a confirmed discontinuity,
not merely a late thread wake.

`submitted_tail` advances to `block_end` only after
`IAudioRenderClient::ReleaseBuffer` succeeds. A failed submission never
publishes unsubmitted output as queued.

## Render event data flow

Each successful render iteration performs this order:

1. Wait for the exclusive render event.
2. Read `IAudioClock` and its QPC-correlated position.
3. Map the device position to output frames.
4. Ask `OutputPacingTracker` for the next block and any confirmed gap.
5. Check the sustained-gap policy.
6. Render the mixer block, applying any recoverable gap.
7. Convert the preallocated float output to PCM16.
8. Obtain and release exactly one complete endpoint buffer.
9. Commit the new submitted tail and real-time counters.

QPC delta continues to feed the late-wake diagnostic. It does not drive source
advancement and cannot independently cause a discontinuity.

## Mixer discontinuity behavior

Extend the render context with:

- `output_frame_begin`;
- `frame_count`;
- `discontinuity_output_begin`;
- `discontinuity_frames`.

At the first processing call for each voice in a render block:

1. Read the latest stable play/seek mailbox state.
2. If it represents a new game playback generation, apply the requested source
   position and epoch. This voice does not consume the older gap.
3. Otherwise, if the voice was playing continuously across the gap, advance it
   by the source-frame duration represented by `discontinuity_frames`.
4. Render the normal block from the resulting source position.

Gap advancement uses the existing cumulative output-to-source rate mapping so
22.05 kHz and 48 kHz exceptional sources do not accumulate rounding drift.

- A looping source wraps modulo its source length.
- A non-looping source that ends inside the gap transitions through the existing
  end/drain state machine at its mapped output boundary.
- A converted source resets its miniaudio converter after jumping to the new
  source position.
- A native 44.1 kHz source performs a direct frame jump.
- Skipped samples are never rendered into a discard buffer.

The mixer publishes source/output mapping segments for the skipped interval and
for the newly rendered block. A discontinuity marker distinguishes dropped
logical time from submitted audio for diagnostics, while cursor interpolation
continues to advance through that interval.

## Play, seek, and epoch precedence

Every discontinuous game control transition has a unique playback generation.
This includes a new `Play` run and every accepted `SetCurrentPosition`.
Timeline segments carry that generation so a stale queued span cannot satisfy a
new cursor query.

`SetCurrentPosition`:

- validates and stores the requested source frame immediately;
- creates the new generation;
- updates the game-visible pending cursor anchor;
- publishes the request through the existing mailbox;
- does not write into the render thread's single-writer timeline ring.

If a clock gap and a new game generation are both visible at the next mixer
pass, the new game generation wins. The requested source position becomes the
new origin and no older automatic gap is added to it.

This rule prevents the endpoint correction and `SkipMargin`-driven BGM seek
from double-advancing the same voice.

## Cursor resolution contract

Replace the timeline's undifferentiated optional result with a status-bearing
result:

- `Resolved`: the current presentation frame is covered by a stable segment for
  the current playback generation.
- `PendingGeneration`: a game play/seek has been accepted, but the first segment
  for that generation has not reached presentation. The facade returns the
  stored requested source position without recording a fault.
- `Unmapped`: a playing or audibly draining current generation should cover the
  presentation frame, but no valid segment does. The facade returns the last
  cursor and records a genuine timeline failure.

An inactive stopped voice continues to return its stored last position without
querying the endpoint clock. Endpoint clock API failures remain endpoint
HRESULT failures rather than timeline failures.

The cursor timeline remains single-writer on the audio thread. Game threads
only read it. The existing bounded sequence-protected ring remains suitable.

The DirectSound write cursor remains one actual endpoint packet ahead of the
resolved play cursor, converted into the source's frame domain.

## Sustained-gap policy

The first two confirmed gap events within a rolling one-second hardware-output
window are recoverable. The third confirmed gap event in that window is a fatal
pacing failure.

The tracker stores only the fixed number of recent presentation-frame values
needed to evaluate this rule. The window is measured in the 44.1 kHz hardware
output-frame domain, not wall-clock time and not callback count.

On a fatal pacing failure, the real-time thread records the failure and signals
the existing monitor. It submits silence only if the endpoint API state still
allows that operation. The non-real-time error report includes:

- configured duration;
- endpoint default and minimum periods;
- actual packet frames and duration;
- confirmed gap-event count;
- total and maximum skipped frames;
- instruction to raise `wasapi_exclusive_buffer_ms` and restart.

There is no automatic reopen or buffer adjustment. A user who deliberately
selects an unsafe period receives a bounded, explanatory failure instead of
indefinite slowed or repeatedly discontinuous playback.

Existing endpoint HRESULT failures, render-event timeout, device invalidation,
and invalid clock mapping remain immediately fatal.

## Diagnostics

### Startup

Retain the current configuration-handoff diagnostics and add:

- strict positive-value validation result;
- requested-versus-minimum validation result;
- actual aligned packet frames and duration;
- `IAudioClient::GetStreamLatency` when available.

`GetStreamLatency` is a software-reported maximum stream delay, not a physical
end-to-end measurement. Failure to query it is logged as unavailable but does
not prevent otherwise valid playback.

### Runtime

The render thread updates counters only. The existing non-real-time monitor
formats periodic summaries containing:

- render callbacks;
- QPC late wakes;
- current and minimum signed submitted lead over presentation, measured before
  the next packet is committed;
- confirmed gap events;
- total and maximum skipped output frames;
- chronic pacing failures;
- expected pending-generation cursor queries;
- genuine unmapped cursor failures;
- silence fallbacks;
- endpoint HRESULT failures;
- existing mixer/voice diagnostics.

The old `cursor_timeline_failures` aggregate is split so periodic game resync
seeks cannot masquerade as render faults.

A healthy 10 ms run is expected to have zero confirmed gaps, skipped frames,
chronic pacing failures, genuine unmapped cursor failures, and endpoint
failures. It may have isolated QPC late wakes and expected pending-generation
queries.

## Error handling

The fixed-period configuration adds these startup failures:

- zero configured milliseconds;
- configured duration below the endpoint minimum;
- unexplained actual-buffer difference outside ordinary one-frame rounding;
- mismatch between the alignment-retry frame count and final buffer size.

All messages identify the failed stage and the requested, minimum, and actual
values available at that stage.

Runtime pacing failure is distinct from endpoint HRESULT failure. It receives a
dedicated failure stage so the operator is told to raise the buffer rather than
being told only that a generic WASAPI call failed.

## Automated verification

Automated verification proves deterministic component and integration behavior
outside the game process. It is not gameplay evidence.

### `OutputPacingTracker`

Cover:

- prefilled startup state;
- sequential packet progression;
- presentation jitter that remains before the submitted tail;
- presentation exactly at a packet boundary;
- one and multiple missed packet slots;
- alignment and overflow boundaries;
- presentation regression;
- two recoverable gaps in one hardware second;
- the third gap becoming fatal;
- expiry of old gap events from the rolling window.

### Mixer

Cover:

- native-rate source advancement over a gap;
- 22.05 kHz and 48 kHz cumulative advancement;
- converter reset after a gap;
- looping wrap;
- non-looping end inside a gap;
- a new `Play` starting at its requested anchor instead of consuming an old
  gap;
- `SetCurrentPosition` overriding the gap;
- normal zero-gap rendering remaining byte/sample equivalent to the existing
  path.

### Cursor timeline and DirectSound facade

Cover:

- pending generation returns the requested cursor without a real failure;
- first presented segment transitions to `Resolved`;
- discontinuity segments advance cursor time;
- stale generations are ignored;
- an actually missing active segment produces `Unmapped`;
- stopped, ending, and draining status remains correct;
- write-cursor projection still uses the actual endpoint packet.

### Endpoint, engine, and configuration

Cover:

- distributed 10 ms default;
- zero rejection;
- below-minimum rejection without clamping;
- equal exclusive duration and periodicity;
- documented alignment retry only;
- software stream-latency reporting and unavailable diagnostics;
- exactly one full endpoint buffer per callback;
- QPC lateness not causing a gap correction;
- gap counters and fatal failure handoff;
- all existing audio and configuration test targets.

## Build environment contract

Every CMake configure and build runs inside the x86 MSVC environment loaded by:

```text
C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat
```

CMake must discover or fetch every project dependency through the repository's
normal configuration. No hand-written dependency path is permitted as a cache
workaround.

Use a normal reconfigure/build while the cache is healthy. If a configure
outside `vcvars32.bat` or another invalid invocation contaminates the cache,
recover with a fresh configure before building:

```powershell
& $env:ComSpec /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --fresh -S . -B build-msvc32-latest -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl'
```

The successful configure must populate the compiler and Ninja entries in
`CMakeCache.txt` and obtain dependencies through CMake itself.

## Manual in-game acceptance

Automated success and a successful DLL build do not accept this change. The
operator deploys the DLL and performs the final test in `game471.exe`.

With the 10 ms default, the operator verifies:

1. Startup reports exclusive 44.1 kHz stereo PCM16 and 441 actual frames on the
   tested endpoint.
2. Menus and attract flows play representative voices and effects correctly.
3. Multiple stages exercise paired BGM/SHOT streams, fades, transitions,
   arrangement effects, and both tap channels.
4. Dense gameplay has normal pitch and tempo, no cracking, no persistent gap,
   and no perceptible BGM drift.
5. Periodic and margin-driven game resync seeks remain functional.
6. Runtime summaries show zero confirmed gaps, skipped output frames, chronic
   pacing failures, genuine unmapped cursor failures, and endpoint failures in
   the healthy run.
7. Expected pending-generation cursor queries may increase around game seeks
   without failing acceptance.
8. Enabled audio feels lower latency than the original DirectSound path to the
   operator.

Only this in-game result is final behavior evidence. If the game is silent,
distorted, slowed, unstable, incorrectly synchronized, or does not start, the
change is not accepted regardless of automated test results.

## References

- Microsoft, [Exclusive-Mode Streams](https://learn.microsoft.com/en-us/windows/win32/coreaudio/exclusive-mode-streams).
- Microsoft, [`IAudioClient::Initialize`](https://learn.microsoft.com/en-us/previous-versions/ms678736%28v%3Dvs.85%29).
- Microsoft, [`IAudioClient::GetStreamLatency`](https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudioclient-getstreamlatency).
- Microsoft, [`IAudioClock::GetPosition`](https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudioclock-getposition).
- Microsoft, [`IAudioClient::GetCurrentPadding`](https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudioclient-getcurrentpadding).
- [Hypersonik](https://github.com/decafcode/hypersonik), as a reference for the
  whole-buffer exclusive event loop and render-thread command separation.

# WASAPI Exclusive 48 kHz Output Design

Date: 2026-07-17

## Context

GCLoader's optional low-latency audio backend currently replaces the game's
DirectSound renderer with an event-driven WASAPI exclusive stream. The game,
its primary DirectSound format, and almost all gameplay-critical WAV assets use
stereo or mono PCM at 44,100 Hz. The backend therefore deliberately opens the
endpoint as stereo PCM16 at 44,100 Hz and uses 44,100 output frames per second
as the common mixer, endpoint-clock, pacing, and cursor-timeline domain.

That strict format works on the endpoint used during the original development,
but many USB and headphone endpoints expose only 48,000 Hz PCM formats in
exclusive mode. WASAPI exclusive mode does not perform sample-rate conversion
for the application. Those endpoints reject the current exact-format probe and
the enabled backend fails at startup even though the game audio can be safely
converted in software.

This design extends the existing backend to negotiate stereo PCM16 at either
44,100 or 48,000 Hz. It does not change the game-facing DirectSound format or
the files on disk.

This document amends the following earlier decisions:

- `docs/superpowers/specs/2026-07-12-wasapi-exclusive-low-latency-audio-design.md`
  required a 44,100 Hz endpoint and made 48,000 Hz fallback a non-goal.
- `docs/superpowers/specs/2026-07-14-wasapi-fixed-period-clock-pacing-design.md`
  described the output-frame domain as always 44,100 Hz.

All other lifecycle, strict-period, pacing, failure, and manual-acceptance
contracts from those designs remain in force.

## Current implementation constraints

The implementation currently uses `kOutputSampleRate = 44100` for several
different concepts:

- the game's primary DirectSound format;
- the miniaudio engine's output rate;
- each voice converter's destination rate;
- WASAPI exclusive format probing and initialization;
- endpoint buffer-duration validation;
- `IAudioClock` mapping;
- missed-packet pacing and mixer discontinuity spans;
- DirectSound play/write cursor projection;
- diagnostics that classify native-rate and converted buffers.

The mixer already creates one `ma_data_converter` per voice. It accepts source
rates of 22,050, 44,100, and 48,000 Hz and maps converted source positions back
to the game-visible source-frame domain with cumulative integer arithmetic.
The fixed-period pacing work also deliberately keeps the endpoint, mixer, and
cursor timeline in one output-frame domain.

These facts make an endpoint-rate mixer the smallest correctness-preserving
extension. A post-mix 44.1-to-48 kHz converter would reduce the number of
resamplers, but it would introduce a second frame domain, variable input-frame
counts per fixed endpoint packet, master-resampler phase recovery after output
gaps, and another endpoint-to-mix cursor timeline. That architecture is not
selected for this change.

## Goals

- Continue to prefer exact 44,100 Hz stereo PCM16 exclusive output when the
  endpoint supports it.
- Automatically use exact 48,000 Hz stereo PCM16 exclusive output when the
  endpoint cannot use 44,100 Hz but can use 48,000 Hz.
- Keep the game-facing primary DirectSound format and all source buffers in
  their original sample-rate and byte domains.
- Use the selected endpoint rate as the single runtime output-frame domain for
  WASAPI, miniaudio, pacing, render spans, and endpoint-clock mapping.
- Reuse the existing per-voice miniaudio conversion path for 44.1-to-48 kHz
  playback.
- Preserve byte/sample-equivalent behavior on a 44,100 Hz endpoint.
- Preserve the existing fixed positive exclusive-buffer duration, alignment
  retry, clock-based cursor, gap recovery, and fatal-failure policies at both
  supported rates.
- Make the selected endpoint format and conversion use visible in startup and
  runtime diagnostics.
- Verify pitch, tempo, cursor timing, loop/seek behavior, and fixed-period
  pacing at both output rates with automated tests, then require in-game
  acceptance on a 48,000 Hz-only endpoint.

## Non-goals

- Converting, replacing, or rewriting WAV assets on disk.
- Reporting 48,000 Hz to the game through the emulated DirectSound primary
  buffer.
- Supporting sample rates other than 44,100 and 48,000 Hz.
- Supporting endpoint channel counts other than stereo.
- Supporting endpoint sample formats other than PCM16 in this change.
- Adding shared WASAPI, original DirectSound, or automatic backend fallback
  after exclusive audio has been explicitly enabled.
- Adding a user-selectable output-rate setting. Format selection is automatic
  and deterministic.
- Following default-device or format changes during a running game session.
- Replacing miniaudio's existing linear voice resampler or adding SpeexDSP,
  SoXR, or another conversion dependency.
- Treating automated tests as proof of acceptable gameplay audio quality or
  latency.

## Selected architecture

### Separate the game-native and runtime-output concepts

Replace the ambiguous compile-time output-rate concept with two explicit
concepts:

- `kGamePrimarySampleRate = 44100` describes the game-facing DirectSound
  primary format and the native rate used to classify gameplay PCM assets.
- `output_sample_rate` is selected during WASAPI endpoint initialization and is
  either 44,100 or 48,000 Hz for the process lifetime.

Stereo channel count, PCM16 endpoint sample size, and game-primary block
alignment remain fixed. Derived endpoint values such as average bytes per
second and frames per configured duration use `output_sample_rate` rather than
a global 44,100 Hz constant.

The old `IsExactOutputFormat` helper becomes explicitly game-facing, such as
`IsExactGamePrimaryFormat`. `PrimarySoundBuffer::GetFormat` and
`PrimarySoundBuffer::SetFormat` continue to expose and accept only the game's
44,100 Hz stereo PCM16 contract regardless of the selected endpoint rate.

### Deterministic exclusive-format negotiation

`WasapiEndpoint` constructs a fixed, ordered candidate list:

1. 44,100 Hz stereo PCM16 as a legacy `WAVEFORMATEX` PCM descriptor;
2. 44,100 Hz stereo PCM16 as `WAVEFORMATEXTENSIBLE` PCM with the canonical
   front-left/front-right channel mask;
3. 48,000 Hz stereo PCM16 as a legacy `WAVEFORMATEX` PCM descriptor;
4. 48,000 Hz stereo PCM16 as the equivalent `WAVEFORMATEXTENSIBLE` descriptor.

For each candidate, the endpoint calls `IAudioClient::IsFormatSupported` in
exclusive mode. Only exact `S_OK` selects a candidate. An unsupported-format
result or a non-exact success advances to the next candidate. Device
invalidation, service failure, or another operational HRESULT aborts startup
at the existing format-probe failure stage instead of being misclassified as a
format mismatch.

If all candidates are unsupported, startup fails with
`AUDCLNT_E_UNSUPPORTED_FORMAT`. There is no closest-match request in exclusive
mode and `GetMixFormat` is not used as authoritative format negotiation.

The selected descriptor remains alive through both the first exclusive
initialization attempt and any documented buffer-alignment retry. The endpoint
records at least:

- sample rate;
- channel count;
- bits per sample;
- block alignment and average bytes per second;
- legacy PCM versus extensible PCM descriptor form.

### Rate-aware endpoint initialization

Every frame/duration calculation in `WasapiEndpoint` uses the selected sample
rate:

- aligned frame count to `REFERENCE_TIME` conversion;
- ordinary floor/ceiling validation of the actual endpoint buffer;
- actual buffer duration diagnostics;
- addressability validation for stereo sample storage.

For the default 10 ms request, an ordinary endpoint is expected to return 441
frames at 44,100 Hz or 480 frames at 48,000 Hz. Driver alignment remains
authoritative, so neither number is hard-coded as the only legal result.

The render client still receives exactly one complete stereo PCM16 endpoint
packet per event. No endpoint-side format conversion or additional software
queue is introduced.

### Endpoint-rate miniaudio mixer

`MiniaudioMixer::Create` receives both the endpoint period in frames and the
selected output sample rate. The no-device `ma_engine` and every voice's
`ma_data_converter` use that rate as their output rate.

At 44,100 Hz:

- native gameplay PCM16 buffers retain their existing no-rate-conversion path;
- output samples, render spans, and cursor behavior remain equivalent to the
  current implementation.

At 48,000 Hz:

- 44,100 Hz gameplay sources are converted to 48,000 Hz by their existing
  persistent voice converters;
- 22,050 and 48,000 Hz exceptional sources continue through the same generic
  source-normalization and voice path;
- 48,000 Hz sources become native-rate relative to the mixer;
- source snapshots and DirectSound byte cursors remain in each buffer's
  original rate and block alignment.

The current miniaudio linear resampling algorithm and its current low-latency
filter configuration remain unchanged for this first compatibility extension.
The converter is persistent across render packets; it is reset only where the
existing play, seek, loop, or recovered-discontinuity rules already require a
reset. A later quality change requires separate evidence and design because it
can change fixed resampler delay.

Diagnostics distinguish two independent facts:

- whether a source is game-native 44,100 Hz PCM16;
- whether that source requires sample-rate conversion for the selected runtime
  output rate.

This prevents a 44,100 Hz gameplay buffer on a 48,000 Hz endpoint from being
misreported as non-gameplay while still accurately counting its conversion.

### One runtime output-frame domain

The selected endpoint rate is the sole output-frame rate used by:

- `EndpointClockMapper`;
- `OutputPacingTracker`;
- mixer render block begin/end positions;
- discontinuity frame counts;
- `AudioRenderSpan` output positions;
- submitted lead and skipped-output diagnostics;
- voice drain boundaries;
- DirectSound write-cursor lead projection.

`EndpointClockMapper` therefore receives the selected output sample rate when
it is reset. `ProjectWriteCursorFrame` also receives the selected output rate
and computes:

```text
source_frames_ahead = ceil(
    endpoint_buffer_frames * source_rate / output_sample_rate)
```

`IAudioEngineServices` exposes the selected output sample rate alongside the
endpoint buffer frame count so the DirectSound facade never assumes that an
endpoint frame is one forty-four-thousand-one-hundredth of a second.

All existing source/output cumulative mappings in `MiniaudioMixer` use the
mixer state's runtime output rate. No per-render floating-point duration
conversion is introduced. Integer scaling remains overflow checked and
cumulative so 44.1-to-48 kHz voices do not accumulate rounding drift.

### Render and cursor data flow

For each successful render event:

1. Read the endpoint clock.
2. Map it to the selected endpoint-rate output-frame domain.
3. Ask `OutputPacingTracker` for the next fixed endpoint packet and any
   confirmed gap.
4. Render exactly one endpoint packet from `MiniaudioMixer` at the selected
   rate, applying the existing discontinuity and generation-precedence rules.
5. Convert the resulting stereo float block to PCM16.
6. Submit exactly one full endpoint buffer.
7. Commit the pacing decision and publish endpoint-rate diagnostics.

The game-facing cursor flow remains:

1. Resolve the current endpoint-rate output frame from `IAudioClock`.
2. Resolve that frame through the voice's source/output render spans.
3. Return a source frame and byte offset in the original DirectSound buffer
   format.
4. Project the write cursor one actual endpoint packet ahead using both the
   endpoint buffer frame count and selected output sample rate.

The existing pending-generation behavior, stopped/draining behavior, and
unmapped-timeline failure classification remain unchanged.

## Component changes

### `WasapiAudioTypes`

- Rename the fixed rate to express the game-primary contract.
- Add helpers that build or validate supported endpoint candidates by explicit
  sample rate and descriptor form.
- Keep source normalization independent of the selected output rate.
- Move runtime conversion classification to the mixer/voice boundary where the
  selected output rate is known.
- Keep checked frame/time conversion helpers rate-parameterized.

### `WasapiEndpoint`

- Probe the ordered format candidates and retain the exact selected descriptor.
- Store the selected endpoint format fields in `EndpointInitialization`.
- Use the selected rate for initialization, alignment retry, and buffer-size
  validation.
- Preserve all current COM, event, render-service, clock, MMCSS, prefill, and
  owner-thread teardown behavior.

### `MiniaudioMixer`

- Accept and validate a nonzero output sample rate at creation.
- Store it in mixer state and use it for the engine, voice converters, source
  mapping, loop/end boundaries, gap advancement, and diagnostics.
- Preserve the fixed endpoint-period render size and all allocation behavior.
- Preserve the existing linear conversion algorithm.

### `AudioCursorTimeline`

- Parameterize endpoint-clock mapping by the selected output rate.
- Parameterize write-cursor projection by the selected output rate.
- Preserve the render-span representation and single-writer bounded ring.

### `OutputPacingTracker`

- Accept the selected output sample rate alongside the fixed endpoint packet
  frame count.
- Measure the chronic-gap rolling window as exactly one second at that runtime
  rate instead of a hard-coded 44,100 frames.
- Preserve packet alignment, submitted-tail, recoverable-gap, and third-gap
  fatal semantics in the selected-rate domain.

### `ExclusiveAudioEngine`

- Initialize the mixer, clock mapper, actual-period calculation, and pacing
  diagnostics from the selected endpoint rate.
- Expose the selected rate through `IAudioEngineServices`.
- Keep endpoint frames, mixer frames, submitted frames, and render-span frames
  in the same selected-rate domain.
- Preserve the render loop's one-packet, no-allocation behavior.

### `DirectSoundFacade`

- Keep the primary buffer fixed at 44,100 Hz stereo PCM16.
- Keep secondary source formats and byte storage unchanged.
- Use the engine's selected output rate only for hardware-time and write-cursor
  projection.

### `WasapiAudioPatch` diagnostics

Startup success reports:

- selected rate and complete PCM format;
- legacy versus extensible descriptor;
- whether the preferred 44,100 Hz candidate was selected or 48,000 Hz fallback
  was required;
- configured, requested, minimum, and actual endpoint periods;
- actual endpoint frames and duration;
- mixer output rate;
- endpoint-rate conversion status.

Runtime summaries retain existing counters. Counts whose unit is output frames
are interpreted at the logged selected rate.

## Error handling

The following remain fatal when exclusive audio is enabled:

- neither 44,100 nor 48,000 Hz stereo PCM16 is supported;
- an operational format-probe failure;
- configured period below the endpoint minimum;
- alignment retry or actual buffer validation failure at the selected rate;
- mixer creation with an invalid or unsupported output rate;
- existing clock, render, device invalidation, and chronic-gap failures.

There is no silent rate reinterpretation. A 44,100 Hz buffer is always passed
through a real sample-rate converter before entering a 48,000 Hz output mix, so
pitch and tempo remain unchanged.

Startup failure reporting includes every candidate attempted and the final
selected-rate state, if any, so an operator can distinguish unsupported format
from period, service, or device failures.

## Automated verification

Automated tests prove static and deterministic behavior, not perceived audio
quality.

### Audio format and DirectSound contract

- The game primary format remains exactly 44,100 Hz stereo PCM16.
- 48,000 Hz is not accepted by `PrimarySoundBuffer::SetFormat`.
- Source normalization continues to accept the observed 22,050, 44,100, and
  48,000 Hz PCM16/PCM24 envelope.
- Game-native classification remains tied to 44,100 Hz PCM16, independent of
  selected endpoint rate.

### Endpoint negotiation

- First-candidate 44,100 Hz legacy PCM success stops probing.
- 44,100 Hz extensible PCM is accepted when the legacy descriptor is rejected.
- Both 44,100 Hz descriptors rejected followed by 48,000 Hz legacy success
  selects 48,000 Hz.
- 48,000 Hz extensible PCM is accepted when its legacy descriptor is rejected.
- All candidates unsupported produces the exact unsupported-format failure.
- An operational failure aborts immediately instead of probing later rates.
- Initialize and alignment retry receive the exact selected descriptor.
- Requested/actual frame validation uses 441 frames for an ordinary 10 ms
  44,100 Hz case and 480 frames for an ordinary 10 ms 48,000 Hz case.

### Mixer and source mapping

- A 44,100 Hz mixer preserves the current native gameplay output.
- A 48,000 Hz mixer renders 44,100 Hz PCM at unchanged duration and expected
  pitch sample progression.
- Cumulative 44.1-to-48 kHz mapping remains stable across many periods without
  source cursor drift.
- Loop, seek, stop/drain, new-play generation, and recoverable-gap behavior are
  covered at 48,000 Hz.
- Native-rate and converted-rate diagnostics are correct at both selected
  rates.
- Existing 22,050 and 48,000 Hz exceptional-source tests remain green.

### Clock, pacing, and DirectSound cursors

- Endpoint clock mapping produces one second as 44,100 or 48,000 output frames
  according to the selected rate.
- A 10 ms packet projects the same source-time write-cursor lead from 441
  frames at 44,100 Hz and 480 frames at 48,000 Hz.
- Fixed packet progression, recoverable gaps, rolling-window chronic-gap
  policy, and submitted-lead counters remain correct in the 48,000 Hz domain;
  the rolling window expires at 48,000 rather than 44,100 output frames.
- Render spans resolve 44,100 Hz source cursors from 48,000 Hz output positions
  without cumulative drift.

### Engine and diagnostics

- The endpoint-selected rate is passed to the mixer and clock mapper.
- Float and PCM16 buffers contain exactly `endpoint_frames * 2` samples at both
  rates.
- One complete endpoint packet is submitted per render event.
- Startup success and failure reports identify selected/attempted rates and
  descriptor forms.
- All existing audio tests and the complete CTest suite remain green.

## Build environment contract

Configure and build under the existing x86 MSVC environment:

```text
C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat
```

Use the repository's existing `build-msvc32-latest` CMake/Ninja build. Build the
production DLL and every affected audio test, then run the complete CTest
suite. Build and automated-test success remain separate from runtime gameplay
acceptance.

## Manual in-game acceptance

The operator performs two enabled-backend runs when suitable endpoints are
available.

### 44,100 Hz endpoint regression

- Startup selects 44,100 Hz stereo PCM16.
- Existing menu, attract, stage BGM/SHOT, taps, arrangement effects, fades,
  seeks, loops, and transitions remain normal.
- Pitch, tempo, cursor behavior, and perceived latency do not regress.
- Healthy runtime summaries retain zero confirmed gaps, skipped frames,
  chronic pacing failures, genuine unmapped cursor failures, and endpoint
  failures.

### 48,000 Hz-only endpoint acceptance

- Startup rejects or skips 44,100 Hz candidates and selects 48,000 Hz stereo
  PCM16.
- A 10 ms configuration reports an ordinary 480-frame endpoint packet unless
  the driver documents an alignment-adjusted result.
- Menus and attract flows play representative music, voices, and effects.
- Multiple stages exercise paired BGM/SHOT streams, tap channels, arrangement
  effects, fades, loops, transitions, and game-driven resync seeks.
- Audio has normal pitch and tempo with no crackling, chopping, persistent
  gap, resampler artifact, or BGM drift.
- DirectSound play cursors continue to drive streaming refill and game timing
  correctly.
- Healthy runtime summaries show zero confirmed gaps, skipped frames, chronic
  pacing failures, genuine unmapped cursor failures, and endpoint failures.
- Enabled audio still feels lower latency than the original DirectSound path.

Only the operator's in-game result accepts the 48,000 Hz path. If audio is
silent, distorted, incorrectly pitched, unstable, desynchronized, or fails to
start, the feature is not accepted regardless of build and CTest results.

## References

- Microsoft, `IAudioClient::IsFormatSupported`.
- Microsoft, `Device Formats`.
- Microsoft, `Exclusive-Mode Streams`.
- miniaudio 0.11.25 data-conversion and engine documentation.

# Temporary WASAPI Audio Replay Diagnostics Design

Date: 2026-07-27

## Status

This design is approved in principle and incorporates the user's correction
that the diagnostic machinery is completely temporary.

The diagnostic DLL is always recording. It has no enable switch, no public or
private configuration key, and no disabled-by-default state. A separately
hashed final production DLL removes the recorder and every associated runtime
diagnostic after the cause is identified, the correction is accepted, and the
capture evidence is no longer needed.

This design adds observation only. It does not change the accepted WASAPI
resync policy, cursor projection, mixer conversion, endpoint period, or audio
content.

## Problem

The earlier periodic in-margin BGM seek was corrected, and audio now plays
normally most of the time. A shorter intermittent artifact remains several
times per song. Listening samples suggest that it may resemble either:

- a real source rewind, represented by sample `07`; or
- replay or concealment of already-rendered audio, represented by sample
  `13`.

The current aggregate counters cannot distinguish these cases. The latest
runtime capture has healthy 10 ms WASAPI pacing, with no late event wakes,
confirmed output gaps, skipped output frames, chronic pacing failures, or
endpoint HRESULT failures. Those counters make ordinary render starvation a
poor explanation, but they do not establish which samples reached the
endpoint.

The remaining causal boundaries are:

1. the game requests or the mixer applies a backward seek;
2. source progression stays monotonic but the application submits repeated
   PCM;
3. the application submits correct PCM and repetition occurs in the endpoint
   driver, wireless path, DAC, or headset.

The artifact is too brief for a manual trigger. The diagnostic must preserve
the entire run automatically.

## Goals

- Record the exact PCM16 stereo stream successfully submitted to WASAPI.
- Correlate that stream with game resync decisions, DirectSound seek requests,
  mixer seek application, source cursor progression, converter resets,
  endpoint clock samples, and output-frame submission.
- Detect and extract likely 33 to 100 ms replay regions after the run without
  performing waveform analysis on the real-time audio thread.
- Distinguish a sample-`07` source rewind from an application-side
  sample-`13` replay.
- Establish when the submitted stream is clean and the remaining fault is
  downstream of GCLoader.
- Avoid locks, allocation, formatting, logging, file I/O, or waiting on the
  audio render thread.
- Remove every new runtime diagnostic after the corrected behavior is
  accepted.

## Non-goals

- Fixing the artifact before the capture establishes its causal boundary.
- Adding a diagnostic configuration option or Config GUI control.
- Changing sample-rate conversion, resampler quality, endpoint format, buffer
  duration, pacing, gain, or seek behavior.
- Adding real-time replay concealment, crossfading, time stretching, or
  automatic seek suppression.
- Capturing the signal after the exclusive-mode endpoint, wireless
  transmitter, DAC, or headset.
- Treating waveform similarity by itself as proof of a defect; music can
  contain legitimate repeated material.
- Retaining a permanent flight recorder, analyzer, counter, trace line, or
  output directory contract.

## Selected architecture

### Diagnostic-only build

The implementation produces a separately hashed diagnostic DLL. When its
WASAPI engine starts successfully, recording starts automatically before the
first ordinary render block and continues until shutdown or a fixed 30-minute
safety limit. At 48 kHz, stereo, PCM16, the limit is approximately 346 MB of
PCM and comfortably covers startup plus multiple plays of the supplied
136-second song.

There is no runtime branch that disables recording. A failure to create the
capture files marks the diagnostic session unusable but does not change audio
behavior or cause the production audio path to fall back.

The process working directory receives one session directory:

```text
audio-diagnostics/
  YYYYMMDD-HHMMSS/
    session.json
    submitted.wav
    timeline.jsonl
```

After the process exits, the offline analyzer adds:

```text
    report.md
    candidates/
      candidate-001.wav
      candidate-002.wav
      ...
```

`submitted.wav` contains only blocks for which
`WasapiEndpoint::SubmitPcm16` succeeded. It is the exact interleaved PCM16
payload copied from `pcm16_mix_`, not a second render or a reconstruction.

### Real-time handoff

Add a focused temporary `AudioFlightRecorder` component with:

- one fixed-capacity single-producer/single-consumer queue for submitted PCM
  blocks;
- one fixed-capacity multi-producer/single-consumer queue for causal event
  records;
- one ordinary-priority writer thread; and
- aggregate overflow, file-error, and completion state.

Every producer submits fixed-size trivially copyable records. Queue storage is
allocated before the render loop begins. The audio thread only copies the
1,920-byte endpoint block and publishes queue indices with atomics. It never
opens, seeks, flushes, formats, or writes a file.

The writer thread drains both queues, writes PCM in submission order, formats
timeline records as JSON Lines, and periodically updates the WAV header so a
forced process exit leaves most of the capture playable. On orderly shutdown,
it drains all published records and finalizes the header and `session.json`.

If a PCM sequence number is missing because the queue overflowed, the writer
inserts an equal-duration silent placeholder to preserve wall-time alignment,
records the missing range, and marks the session incomplete. An incomplete
region cannot establish that the submitted stream was clean.

### Voice identity and source progression

Each mixer voice receives a monotonically increasing diagnostic ID at
creation. The trace records:

- voice ID and usage;
- source sample format, channels, rate, block alignment, and length;
- play, stop, looping, and epoch transitions;
- seek request and seek application;
- cursor immediately before and after an applied seek;
- render output-frame begin and end;
- source-frame begin and end;
- input frames required, copied, and consumed;
- output frames requested and produced;
- snapshot generation;
- normal loop wrap, output-discontinuity advance, and converter reset reason.

For an uninterrupted non-looping epoch, the next source begin must equal the
previous source end. A normal loop wrap, accepted new seek epoch, or explicit
output-discontinuity advance explains a non-contiguous source position and is
tagged separately. Any other overlap, backward movement, or gap is emitted as
a source-progression anomaly.

The trace records all voices rather than guessing which voice is the BGM. The
long 44.1 kHz stage voice can be identified afterward from format, length,
looping state, and lifespan.

### Game resync and DirectSound seek boundaries

The existing `AudioResyncPolicy` hook remains behaviorally unchanged. The
temporary build publishes an event containing:

- QPC timestamp;
- signed drift in milliseconds;
- configured margin;
- classification as suppressed in-margin or allowed out-of-margin; and
- whether the hook state was readable.

`SecondarySoundBuffer::SetCurrentPosition` publishes the buffer/voice ID,
requested byte position, derived source frame, previous reported frame, and
new playback generation. `MixerVoice` separately records when the audio thread
actually consumes that mailbox generation and resets the converter.

These two boundaries distinguish a game request from its eventual audio-thread
effect and reveal overwritten, delayed, duplicated, or unexpected seek
generations.

### Endpoint block timeline

For each successful render/submission cycle, record:

- PCM block sequence;
- endpoint clock position and correlated QPC;
- mapped presentation frame;
- pacing decision and output block begin;
- discontinuity frames;
- mixer result and frames read;
- submitted tail after pacing commit;
- `SubmitPcm16` result; and
- PCM queue publication result.

Existing runtime counters remain the authority for late wakes, confirmed
gaps, skipped frames, silence fallbacks, and endpoint failures. The temporary
timeline supplies exact local context but does not add another periodic
summary.

### Offline analyzer

The analyzer runs only after the game exits. It performs two passes.

The causal pass reports and clusters:

- applied backward seeks;
- repeated or overlapping source spans without an explanatory loop;
- converter resets by reason;
- resync attempts and their suppress/allow decisions;
- endpoint clock stalls or jumps;
- output discontinuities;
- missing PCM or event records; and
- failed or incomplete capture state.

The waveform pass scans `submitted.wav` for a current 33, 40, 50, 67, or
100 ms window that is substantially closer to an earlier window 10 to 250 ms
behind it than to ordinary adjacent audio. It uses normalized correlation and
sample error so that both an exact rewind and an edge-crossfaded replay can be
ranked. It clusters nearby matches, prioritizes matches near causal events,
and extracts bounded listening clips rather than declaring every musical
repeat a defect.

The standard report lists the highest-confidence candidates and links each
one to its source, seek, converter, and endpoint timeline context. Candidate
ranking is automatic; the user's listening verdict remains the authority for
whether a clip contains the reported runtime artifact.

## Diagnostic verdicts

| Evidence | Verdict |
|---|---|
| Backward seek is applied and the submitted waveform repeats at the same output frame | Sample `07`: application-side source rewind |
| No seek is applied, source spans overlap or repeat, and submitted PCM repeats | Mixer cursor/input reuse |
| Source spans remain monotonic but submitted PCM repeats | Post-voice application replay or stale final mix, sample `13`-like |
| Endpoint clock stalls or jumps and submitted PCM contains the artifact | Application/endpoint scheduling boundary needs focused follow-up |
| Submitted WAV contains the artifact but no timeline anomaly exists | Investigate final mixing and PCM handoff with the captured block identity |
| User hears the artifact live but it is absent throughout the submitted WAV | Fault is after `ReleaseBuffer`: driver, wireless link, DAC, or headset |
| Any relevant PCM queue range is missing | No clean/downstream verdict for that range; repeat the diagnostic run |

The last verdict cannot identify which downstream component repeated the
audio. It establishes that changing the game cursor or miniaudio resampler
cannot correct that occurrence. A wired endpoint or physical output recording
would be the next isolation step.

## Failure handling and performance constraints

- Recorder initialization failure: emit one temporary startup error, mark the
  session unavailable, and leave audio behavior unchanged.
- PCM queue overflow: never block; preserve time with a writer-side silent
  placeholder and mark the interval incomplete.
- Event queue overflow: never block; increment a lost-event counter and mark
  the corresponding report evidence incomplete.
- Disk full or writer failure: stop recording, publish aggregate failure
  state, and leave audio rendering active.
- WAV header updates and JSON formatting occur only on the writer thread.
- Shutdown stops rendering first, then asks the writer to drain and join.
- The 30-minute limit stops only diagnostics. It does not stop or alter audio.
- No diagnostic condition invokes the fatal audio path or changes resync,
  cursor, mixer, or endpoint decisions.

The diagnostic run is invalid as a timing baseline if it introduces late
wakes, confirmed gaps, skipped frames, chronic pacing failures, or endpoint
errors that were absent from the current baseline.

## Verification

### Automated

- Queue tests prove ordered delivery, wraparound, overflow accounting, and
  non-blocking producer behavior.
- WAV tests prove exact PCM16 stereo 48 kHz output, correct periodic/final
  headers, sequence-gap alignment, and orderly shutdown.
- Timeline tests cover monotonic conversion, legal loop wrap, explicit seek,
  overwritten mailbox generation, backward seek, discontinuity advance,
  converter-reset classification, and multiple simultaneous voices.
- Analyzer tests synthesize clean audio, a 40 ms source rewind, and a 50 ms
  edge-crossfaded replay. The clean fixture must not be ranked with either
  injected anomaly.
- Local verification also runs the analyzer against the previously generated
  `00`, `07`, and `13` listening samples without committing those WAV files to
  the repository.
- Existing mixer, cursor timeline, DirectSound facade, pacing, WASAPI engine,
  framerate policy, transaction, and runtime tests continue to pass.
- Build the normal 32-bit production target and review the owned diff for
  forbidden render-thread file I/O, formatting, allocation, mutex acquisition,
  or waits.

### Runtime capture

1. Back up and hash the currently deployed DLL.
2. Build, hash, and deploy the temporary diagnostic DLL.
3. Verify the temporary startup line names the session directory and that the
   endpoint remains PCM16 stereo 48 kHz with a 480-frame period.
4. Play the supplied song for its full duration under the same device,
   framerate, and timing settings as the report.
5. Exit normally, run the analyzer, and verify the session is complete.
6. Inspect causal classifications and listen to the ranked clips.
7. The user reports whether the submitted recording contains the same
   artifact heard during the live run.

One run may identify the boundary. If evidence is missing or the session is
incomplete, repeat without changing another variable.

## Temporary delivery lifecycle

### Stage A: always-on diagnostic build

Implement and deploy the recorder and causal trace without changing audio
behavior. Capture one or more complete songs until the root-cause boundary is
established.

### Stage B: corrected diagnostic build

Implement only the correction supported by Stage A evidence. Retain the same
always-on recorder for the corrected run so the absence of the identified
event is observable rather than inferred.

The user owns the live auditory acceptance. Static tests, a clean submitted
recording, and analyzer output do not replace that verdict.

### Stage C: complete diagnostic removal

After the user accepts the correction:

- remove `AudioFlightRecorder` and its queues and writer thread;
- remove all voice, seek, resync, converter, and endpoint trace publication;
- remove the offline analyzer and temporary analyzer fixtures;
- remove temporary startup/error/completion lines and aggregate state;
- remove diagnostic CMake entries and tests;
- remove the runtime `audio-diagnostics` capture directories after their
  conclusion is recorded in a concise validation document; and
- build, hash, deploy, and smoke-test a clean production DLL.

The final product has no diagnostic configuration, dormant branch, retained
counter, recorder thread, file output, trace format, or periodic log.

## Expected temporary source scope

The implementation plan should keep temporary changes within:

- `src/Audio/Diagnostics/AudioFlightRecorder.h`;
- `src/Audio/Diagnostics/AudioFlightRecorder.cpp`;
- `src/Audio/CMakeLists.txt`;
- `src/Audio/Mixer/MiniaudioMixer.h`;
- `src/Audio/Mixer/MiniaudioMixer.cpp`;
- `src/Audio/DirectSound/DirectSoundFacade.h`;
- `src/Audio/DirectSound/DirectSoundFacade.cpp`;
- `src/Audio/Wasapi/ExclusiveAudioEngine.h`;
- `src/Audio/Wasapi/ExclusiveAudioEngine.cpp`;
- `src/Audio/Wasapi/WasapiAudioPatch.cpp`;
- `src/Patches/Framerate/FrameratePatch.cpp`;
- focused audio and framerate tests;
- one temporary offline analyzer under `tools/`; and
- one append-only runtime-validation document.

The plan may narrow this list if a boundary does not require a source edit. It
must not modify game data, executable bytes beyond the already-owned resync
hook, IDBs, endpoint negotiation, public configuration, Config GUI, unrelated
timing patches, or other loader subsystems.

## Rejected alternatives

### Manual hotkey or marker

Rejected because the artifact is too brief for the user to mark reliably.

### Real-time waveform detector

Rejected because music legitimately repeats and cross-correlation would add
avoidable work to the audio thread. Continuous capture permits richer offline
analysis without affecting deadline behavior.

### Metadata-only instrumentation

Rejected because it cannot prove whether the PCM submitted to WASAPI already
contains a sample-`13`-like replay.

### Permanent or configurable diagnostics

Rejected by user direction. A public key would expand the strict configuration
contract, and a dormant internal switch would leave unnecessary investigation
code in the product.

### ETW-only investigation

Rejected as the first step because ETW may reveal scheduling or driver stalls
but cannot prove which PCM samples the application submitted or whether its
source cursor moved backward.

## Completion definition

The diagnostic effort is complete only when:

1. an always-on temporary build captures a complete session without introducing
   pacing failures;
2. the submitted stream and timeline establish the defect boundary, or
   establish that it lies after application submission;
3. the root-cause evidence selects one narrow correction;
4. the corrected diagnostic build no longer contains the identified event and
   the user accepts live audio;
5. all temporary recorder, trace, analyzer, test, and log machinery is removed;
6. runtime capture files are deleted after their conclusion is preserved
   concisely;
7. the clean final production build passes focused regression verification;
   and
8. the user completes a final runtime smoke test with the diagnostic-free DLL.


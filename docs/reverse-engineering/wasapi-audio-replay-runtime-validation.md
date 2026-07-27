# WASAPI Audio Replay Runtime Validation

## Scope and evidence rule

This record separates static/build evidence, captured PCM/timeline evidence,
and the user's live auditory verdict. Stage A observes only and changes no
audio decision.

## Baseline

- Endpoint: PCM16 stereo 48,000 Hz
- Period: 480 frames / 10 ms
- Source under test:
  `H:\gc\data\stage\sound\bgm_b-516_happysyn2_BGM.wav`
- Previous runtime counters: zero late wakes, confirmed gaps, skipped output
  frames, chronic pacing failures, and endpoint HRESULT failures.
- Listening controls: sample `07` source rewind and sample `13` crossfaded
  replay were somewhat similar but not accepted as exact matches.

## Stage A build

Implementation commits:

- `f2595dc` - define temporary diagnostic queues
- `7278375` - record checkpointed WASAPI diagnostics
- `bdd4143` - capture successfully submitted WASAPI PCM
- `b936964` - trace source progression, seeks, and converter resets
- `b6f97da` - trace the existing game audio-resync decisions
- `29d96c5` - analyze replay captures and extract listening clips

Static verification:

- The focused analyzer command passed all 9 tests.
- The exact focused x86 build/test slice passed all 11 tests in 10.21 seconds.
- The real listening-control sweep rejected clean sample `00` and ranked the
  injected events in samples `07` and `13` near 3, 7, and 11 seconds.
- The real-time boundary audit found no new formatting, logging, file I/O,
  allocation, lock, wait, or sleep in the owned render/callback diff.
- No diagnostic configuration key or Config GUI change exists.
- The miniaudio linear resampler with `lpfOrder = 0`, the resync in-margin
  comparison, and the resync epilogue target remain unchanged.
- `git diff --check` passed.

Verified candidate:

- Path:
  `H:\gc\artifacts\GCLoader\.worktrees\audio-replay-diagnostics-stage-a\build-msvc32-release\dist\iDmacDrv32.dll`
- PE machine: x86 (`14C`)
- Length: 5,672,960 bytes
- Last write: 2026-07-27 06:14:54 +08:00
  (2026-07-26 22:14:54 UTC)
- SHA-256:
  `F0C35C3673859BF8294B3391F03607ADE4C302F8D2B50CF4C5DF6084A521A1BC`

This identity is the Stage A observation build, not the corrected shared-clock
candidate.

## Stage A deployment

Deployed at 2026-07-27 06:18:52 +08:00 while `game471.exe` was not running.

- Immutable archive:
  `H:\gc\artifacts\runtime-builds\wasapi-audio-replay\stage-a-diagnostic\F0C35C3673859BF8294B3391F03607ADE4C302F8D2B50CF4C5DF6084A521A1BC\iDmacDrv32.dll`
- Runtime destination: `H:\gc\iDmacDrv32.dll`
- Candidate/archive/runtime SHA-256:
  `F0C35C3673859BF8294B3391F03607ADE4C302F8D2B50CF4C5DF6084A521A1BC`
- Rollback copy:
  `H:\gc\deploy-backups\wasapi-audio-replay-stage-a-20260727-061852\iDmacDrv32.pre-diagnostic.dll`
- Rollback SHA-256:
  `FE490E13D535AA7F1077561676F399A4026F5159040B1150FE1F946BCC3472AB`

No configuration file was altered.

## Stage A capture

The complete analyzed session is:

- Directory: `H:\gc\audio-diagnostics\20260727-213445`
- Conclusive duration: 233.95 seconds
- Incomplete ranges: none
- First discontinuity: capture time 104.430 seconds, source voices 86 and 87
- Second discontinuity: capture time 160.220 seconds
- Backward source movement: 3,043 frames at 44,100 Hz, or 69.002 ms
- Submitted-PCM correlation with the region presented 69 ms earlier:
  approximately 0.9993 for the first event and 0.9995 for the second
- BGM plus `_SHOT` reconstruction correlation: 0.999995864

Both long-form voices received the same group seek. `_SHOT` can make a
repeated transient more audible, but it did not create an independent
discontinuity.

The confirmed listening file is:

`H:\gc\tmp\audio-issue-identification\confirmed-capture-20260727-213445\01_first_captured_tight.wav`

## User verdict

The user confirmed that `01_first_captured_tight.wav`, the first extracted
event, is the exact runtime issue.

## Root-cause classification

The game's gameplay-audio watchdog compared an integer nominal gameplay clock
against the endpoint-backed DirectSound cursor, then issued a backward
`SetCurrentPosition` for sound group 2. The mixer and resampler followed that
request correctly.

Every 480 endpoint frames consumed exactly 441 source frames before the seek,
so ordinary 44.1-to-48 kHz resampling was not drifting. Reducing the resync
margin to 10 ms would create smaller but more frequent rewinds; it is not the
selected correction.

The authorized correction is the WASAPI shared gameplay song clock specified
in:

`docs/superpowers/specs/2026-07-28-wasapi-shared-gameplay-song-clock-design.md`

## Shared-clock diagnostic build

Built and deployed on 2026-07-28:

- Source commit:
  `5758796fec17afbefb7f7120772c6a28b03d0e96`
- Verification:
  x86 `iDmacDrv32` build passed, 60/60 CTests passed, and 11/11 audio
  replay analyzer tests passed
- PE identity:
  `14C machine (x86)`
- Candidate:
  `H:\gc\artifacts\GCLoader\.worktrees\audio-replay-diagnostics-stage-a\build-msvc32-release\dist\iDmacDrv32.dll`
- Candidate length:
  `5672960` bytes
- Candidate last-write time:
  `2026-07-28T04:38:42.3167036+08:00`
- Candidate/archive/runtime SHA-256:
  `E18F09A3FC8A9A001CDEB53D7C94A9B92EAA605ECB2109EFFC8AC7B3CACE2331`
- Immutable archive:
  `H:\gc\artifacts\runtime-builds\wasapi-shared-clock\diagnostic\E18F09A3FC8A9A001CDEB53D7C94A9B92EAA605ECB2109EFFC8AC7B3CACE2331\iDmacDrv32.dll`
- Runtime destination:
  `H:\gc\iDmacDrv32.dll`
- Rollback copy:
  `H:\gc\deploy-backups\wasapi-shared-clock-diagnostic-20260728-044207\iDmacDrv32.pre-shared-clock.dll`

## Shared-clock runtime matrix

Not exercised yet.

## Diagnostic removal and production build

Not started. Removal is gated on user acceptance of the corrected diagnostic
run.

## Mandatory future cleanup

The recorder, event publications, analyzer, temporary tests, capture
directories, and diagnostic log lines must remain through the corrected
diagnostic run and be removed from the final production DLL.

The mechanical Stage C source-removal manifest is:

- `src/Audio/CMakeLists.txt`
- `src/Audio/Diagnostics/AudioFlightRecorder.cpp`
- `src/Audio/Diagnostics/AudioFlightRecorder.h`
- `src/Audio/DirectSound/DirectSoundFacade.cpp`
- `src/Audio/Mixer/MiniaudioMixer.cpp`
- `src/Audio/Mixer/MiniaudioMixer.h`
- `src/Audio/Wasapi/ExclusiveAudioEngine.cpp`
- `src/Audio/Wasapi/ExclusiveAudioEngine.h`
- `src/Audio/Wasapi/WasapiAudioPatch.cpp`
- `src/Audio/Wasapi/WasapiAudioPatchInternal.h`
- `src/Patches/Framerate/FrameratePatch.cpp`
- `src/Patches/Framerate/FrameratePatch.h`
- `tests/Audio/AudioFlightRecorderTests.cpp`
- `tests/Audio/CMakeLists.txt`
- `tests/Audio/ExclusiveAudioEngineTests.cpp`
- `tests/Audio/MiniaudioMixerTests.cpp`
- `tests/Audio/SecondarySoundBufferTests.cpp`
- `tests/Audio/WasapiAudioPatchTests.cpp`
- `tests/Patches/Framerate/FramerateRuntimeTests.cpp`
- `tools/analysis/audio_replay_analyzer.py`
- `tools/analysis/tests/test_audio_replay_analyzer.py`

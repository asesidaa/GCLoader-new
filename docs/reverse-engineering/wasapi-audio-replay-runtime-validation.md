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

Runtime deployment and capture remain unexercised at this point.

## Stage A capture

No Stage A capture has been analyzed yet.

## User verdict

No Stage A live auditory verdict has been supplied yet.

## Root-cause classification

Unclassified. No correction is authorized by Stage A static evidence.

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

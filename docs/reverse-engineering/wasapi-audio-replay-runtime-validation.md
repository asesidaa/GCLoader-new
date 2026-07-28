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

### End-of-song correction build

The first 240 FPS run exposed an end-of-buffer selection error. An explicit
inactive cursor publication was classified as rounded whenever the game getter
also returned the buffer's final nonnegative millisecond cursor. That selected
step zero indefinitely and prevented the post-song state transition.

The corrected diagnostic build makes the explicit inactive publication
authoritative and therefore preserves the binary's initialized step of one:

- Source commit:
  `5b7f5c04b922b3b5aab4b61b4491a6285d06554c`
- Verification:
  x86 `iDmacDrv32` build passed, 60/60 CTests passed, 11/11 audio replay
  analyzer tests passed, and the post-commit focused suite passed 4/4
- PE identity:
  `14C machine (x86)`
- Candidate:
  `H:\gc\artifacts\GCLoader\.worktrees\audio-replay-diagnostics-stage-a\build-msvc32-release\dist\iDmacDrv32.dll`
- Candidate length:
  `5672960` bytes
- Candidate last-write time:
  `2026-07-28T21:19:13.5731397+08:00`
- Candidate/archive/runtime SHA-256:
  `970A76157950F556446EE0FBF70CE7961062615C2ED23D49CEDCEEF1DF5F161B`
- Immutable archive:
  `H:\gc\artifacts\runtime-builds\wasapi-shared-clock\diagnostic\970A76157950F556446EE0FBF70CE7961062615C2ED23D49CEDCEEF1DF5F161B\iDmacDrv32.dll`
- Runtime destination:
  `H:\gc\iDmacDrv32.dll`
- Rollback copy:
  `H:\gc\deploy-backups\wasapi-shared-clock-end-state-fix-20260728-212023\iDmacDrv32.pre-end-state-fix.dll`
- Rollback SHA-256:
  `E18F09A3FC8A9A001CDEB53D7C94A9B92EAA605ECB2109EFFC8AC7B3CACE2331`

## Shared-clock runtime matrix

### Target 240, initial diagnostic build

- Configured target:
  `240`
- Measured external rate:
  `240.113 FPS`
- Session:
  `H:\gc\audio-diagnostics\20260728-205539`
- Conclusive capture:
  `203.740 seconds`
- Shared-clock observations:
  exact `32626`, rounded `3204`, inactive `0`, failed `0`, invalid `0`
- Step counts:
  zero `3361`, one `32287`, multi `182`
- Maximum absolute tick error/backlog:
  `4 / 0`
- Audio evidence:
  the BGM begins at capture `47.840 seconds`; whole-song low-band normalized
  correlation is `0.967`; every tested ten-second window retains the same
  alignment; there are no PCM gaps, confirmed replay candidates, or
  same-generation BGM cursor rewinds
- `_SHOT` qualification:
  the analyzer's 69 possible-watchdog labels are short rotating one-shot
  buffer resets, not BGM rewinds
- User audio verdict:
  audio feels fine
- Gameplay verdict:
  rejected because the chart remained active instead of proceeding to the
  result scene
- Failure boundary:
  the last exact event advances current tick `32653` toward desired tick
  `32654`; the next and all remaining rounded events hold both at `32654` with
  step zero
- Root cause:
  an inactive observation lost precedence to the nonnegative final
  whole-millisecond cursor
- Follow-up:
  repeat target 240 with corrected diagnostic SHA-256
  `970A76157950F556446EE0FBF70CE7961062615C2ED23D49CEDCEEF1DF5F161B`

### Target 240, corrected end-state build

- Measured external rate:
  `240.108 FPS`
- Session:
  `H:\gc\audio-diagnostics\20260729-025330`
- Conclusive capture:
  `229.910 seconds`, with no incomplete ranges
- Shared-clock observations:
  exact `32628`, rounded `3`, inactive `694`, failed `0`, invalid `0`
- Active-cursor step counts:
  zero `85`, one `32437`, multi `109`
- Maximum absolute tick error/backlog:
  `2 / 0`
- End-state boundary:
  all `694` inactive observations advanced by exactly one tick before the
  result transition
- Audio evidence:
  no same-generation BGM cursor rewind and no confirmed replay candidate;
  whole-song correlation `0.9676` at capture offset `56.370 seconds`, with
  zero relative drift in all 13 sampled windows
- User verdict:
  audio remained clean and the song completed normally into results

### Target 60, native path

- Measured external rate:
  `60.0035 FPS`
- Startup plan:
  native bypass, direct writes `0`, hooks `9`
- Session:
  `H:\gc\audio-diagnostics\20260729-044429`
- Conclusive capture:
  `234.060 seconds`, with no incomplete ranges
- Shared-clock observations:
  exact `8164`, rounded `0`, inactive `175`, failed `0`, invalid `0`
- Active-cursor step counts:
  zero `1`, one `8163`, multi `0`
- Maximum absolute tick error/backlog:
  `1 / 0`
- End-state boundary:
  all `175` inactive observations advanced by exactly one tick
- Audio evidence:
  no same-generation BGM cursor rewind and no confirmed replay candidate;
  whole-song correlation `0.974154` at capture offset `59.850 seconds`, with
  zero relative drift in all 13 sampled windows
- Qualification:
  one generic 250 ms waveform candidate overlapped dense rotating `_SHOT`
  activity and did not reproduce the accepted defect signature
- User verdict:
  the full run, audio, and result transition were normal

### Target 165, transformed path

- Measured external rate:
  `165.202 FPS`
- Startup plan:
  transformed deterministic phase, direct writes `17`, hooks `50`
- Session:
  `H:\gc\audio-diagnostics\20260729-045806`
- Conclusive capture:
  `246.950 seconds`, with no incomplete ranges
- Shared-clock observations:
  exact `22442`, rounded `2`, inactive `479`, failed `0`, invalid `0`
- Active-cursor step counts:
  zero `45`, one `22349`, multi `50`
- Maximum absolute tick error/backlog:
  `2 / 0`
- End-state boundary:
  all `479` inactive observations advanced by exactly one tick
- Audio evidence:
  no same-generation BGM cursor rewind and no ranked or confirmed replay
  candidate; whole-song correlation `0.974110` at capture offset
  `53.320 seconds`, with zero relative drift in all 13 sampled windows
- User verdict:
  the full run was normal

### Matrix closeout

The user accepted the 60, 165, and corrected 240 FPS runs and directed this
stage to finish and clean up. A separately exact 59.94 FPS limiter case and a
separate 144 FPS capture are not required for this stage; normal limiter drift
around each configured target is expected.

Across all accepted captures, the WASAPI endpoint reported zero late wakes,
confirmed gaps, skipped output frames, chronic pacing failures, and endpoint
HRESULT failures.

## Diagnostic removal and production build

The temporary source path was removed after runtime acceptance. The permanent
unwrapped cursor resolution, scoped gameplay cursor publication, rational
shared clock, root hook, mode-specific hook plans, range-aware consumers, and
end-of-song inactive-cursor behavior remain.

The Stage C source-removal manifest is:

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

Removal verification:

- `rg` found no remaining `AudioFlightRecorder`, `AudioDiagnostic`,
  `audio-diagnostics`, `GameplaySongClockCursorSource`,
  `PublishAudioResyncDiagnostic`, diagnostic sink, or diagnostic voice-ID
  reference under `src`, `tests`, or `tools`.
- `git diff --check` passed.
- The stripped x86 production target linked successfully.
- The complete post-removal suite passed `59/59` CTests in 10.65 seconds.

Production identity:

- Last code-affecting commit:
  `9bb61b46786bb87e8d89e3ff938525e249c4091f`
- Candidate:
  `H:\gc\artifacts\GCLoader\.worktrees\audio-replay-diagnostics-stage-a\build-msvc32-release\dist\iDmacDrv32.dll`
- PE identity:
  `14C machine (x86)`
- Length:
  `5636096` bytes
- Candidate/archive/runtime SHA-256:
  `D5728147B61554F3F2047A1476BFD8EFE2F6B47CCE17B672C3CEEC8486F05CB5`
- Immutable archive:
  `H:\gc\artifacts\runtime-builds\wasapi-shared-clock\production\D5728147B61554F3F2047A1476BFD8EFE2F6B47CCE17B672C3CEEC8486F05CB5\iDmacDrv32.dll`
- Runtime destination:
  `H:\gc\iDmacDrv32.dll`
- Rollback copy:
  `H:\gc\deploy-backups\wasapi-shared-clock-production-20260729-052242\iDmacDrv32.pre-production.dll`
- Rollback SHA-256:
  `970A76157950F556446EE0FBF70CE7961062615C2ED23D49CEDCEEF1DF5F161B`

Deployment occurred while `game471.exe` was not running. Candidate, archive,
and runtime hashes matched exactly. No configuration file was altered.
Binary-string verification found no recorder session, submitted-WAV,
timeline, or shared-clock telemetry marker in the production DLL.

The accepted capture directories removed explicitly after preserving their
numeric evidence were:

- `H:\gc\audio-diagnostics\20260727-213445`
- `H:\gc\audio-diagnostics\20260728-205539`
- `H:\gc\audio-diagnostics\20260729-025330`
- `H:\gc\audio-diagnostics\20260729-044429`
- `H:\gc\audio-diagnostics\20260729-045806`

These five directories occupied `386924951` bytes in total and were removed
permanently. `H:\gc\audio-diagnostics` remains as an empty root with zero
children.

The user directed immediate stage closeout and cleanup after accepting the
60, 165, and corrected 240 FPS runs. No additional diagnostic capture or
separate post-removal gameplay capture is part of this closeout; production
identity is instead tied to the tested code commit and the mechanical removal
proof above.

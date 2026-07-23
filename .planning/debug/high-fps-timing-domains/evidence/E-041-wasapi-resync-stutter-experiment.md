# E-041: WASAPI resync-stutter correction experiment

## Candidate identity

The experimental production candidate consists of:

- `d3360c3` — publish the process-lifetime committed WASAPI hook state;
- `cb04c04` — suppress harmless in-margin watchdog seeks only for committed
  WASAPI;
- `a817318` — publish endpoint presentation time for lock-free game-thread
  cursor reads.

The normal release candidate and deployed runtime DLL are:

- source:
  `H:\gc\artifacts\GCLoader\build-msvc32-release\dist\iDmacDrv32.dll`;
- target: `H:\gc\iDmacDrv32.dll`;
- SHA256:
  `9384763CE02DA110D89F53C70E614FA3A2CB30CE7219EF3A0E4B3FC1CAA78DB2`.

The prior runtime DLL is preserved at:

`H:\gc\deploy-backups\wasapi-resync-experiment-8ca8607\iDmacDrv32.pre-experiment.dll`

Its SHA256 is
`B726D347B5F6B8432BA75E1E7A9B63C3B7075BF445131D7B5B2C5F9FA3323150`.

## Implementation boundary

The resync-policy hook is selected only when the existing WASAPI
`DirectSoundCreate8` detour has actually committed. Passthrough DirectSound
does not install it.

The hook preflights the complete supported seek block and epilogue:

`8B 55 F8 52 E8 33 02 FD FF 8B C8 E8 2C 12 FD FF 5E 8B E5 5D C3`

At RVA `0x002401C4`, unreadable stack state, an invalid margin, or
`abs(drift_ms) > margin_ms` leaves `eip` unchanged and executes the original
stage-BGM setter. An in-margin arrival sets `eip` to RVA `0x002401D4`, the
original `pop esi; mov esp, ebp; pop ebp; ret` epilogue. The policy performs no
logging, allocation, counter update, endpoint query, or backend query.

The audio thread now publishes a coherent mapped presentation frame,
endpoint-correlated QPC sample, and committed submitted tail after successful
submission and pacing commit. `CurrentOutputFrame` reads QPC and that atomic
publication, clamps projection to the last submitted frame, and publishes a
monotonic maximum. It contains no endpoint dereference, `IAudioClock` call,
project mutex, logging, allocation, or failure publication. The only two live
`ReadClock` calls are the audio-thread initialization and render-event reads.

The investigation-only render, cursor, buffer-copy, and resync diagnostics
were removed. No replacement telemetry or debug test suite was added.

## Build and focused verification

The production DLL and five existing focused targets were configured, built,
and run through the checked-in release preset:

```powershell
& $env:ComSpec /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --preset msvc32-release && cmake --build --preset msvc32-release --target iDmacDrv32 FrameratePatchPlanTests FrameratePatchTransactionTests FramerateRuntimeTests AudioCursorTimelineTests ExclusiveAudioEngineTests && ctest --preset msvc32-release -R "^(FrameratePatchPlanTests|FrameratePatchTransactionTests|FramerateRuntimeTests|AudioCursorTimelineTests|ExclusiveAudioEngineTests)$"'
```

CTest reported 5/5 passed and zero failures. Static review also established:

- no temporary diagnostic, old resync hook, old resync counter, or
  `endpoint_service_mutex_` symbol remains;
- `CurrentOutputFrame` is publication-only;
- exactly two endpoint `ReadClock` calls remain, both audio-thread-owned;
- the owned worktree was clean before deployment.

## Focused 240 FPS runtime

The operator ran the deployed candidate under the same relevant conditions as
E-040:

- endpoint: Arctis Nova Pro Wireless headphones;
- requested and actual fixed buffer: 10 ms / 480 frames;
- selected endpoint/mixer format: PCM16, 48 kHz, stereo;
- external cap: 240 FPS;
- run start: 2026-07-24 05:31:19;
- last process log record: 2026-07-24 05:34:08.

The existing production log confirms:

- committed WASAPI hook state and active WASAPI-exclusive backend;
- framerate transaction committed with 17 direct writes and 42 hooks;
- external-cap validation at 240.041 FPS;
- at least 150 seconds of audio runtime summaries;
- zero late event wakes, confirmed gaps, skipped output frames, chronic pacing
  failures, or endpoint HRESULT failures;
- no WASAPI startup/runtime fatal and no framerate fail-closed record.

No silent-policy counter was added, so the runtime log is not used to infer how
many interval arrivals were suppressed.

## Operator observation and verdict

After initially describing the result as better, the operator supplied the
separate acceptance answers:

- approximately three-second repeating/skipping audio: **gone**;
- visual micro-freezes: **gone**;
- BGM/chart synchronization and regressions: **clean run**, with no new
  audible or gameplay regression noticed.

Verdict: **accepted** for this focused 240 FPS WASAPI run.

The before/after result, the exact prior three-second seek correlation, and the
narrow in-margin suppression support accepting the periodic hard seek as the
audible stutter root cause. The visual result accepts the published-clock
candidate at runtime, but this combined A/B does not independently isolate the
clock-path change from the simultaneous seek correction. It therefore supports
the removed synchronous endpoint boundary as a contributor without claiming
that static plausibility alone proves sole causality.

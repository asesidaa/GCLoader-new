# WASAPI Exclusive Low-Latency Audio Design

Date: 2026-07-12

## Context

`game471.exe` is an arcade rhythm game, so audio output latency directly affects the perceived relationship between an input and its hit sound. The current game uses the legacy DirectSound 8 API. GCLoader already injects into the game process, provides MinHook and SafetyHook, parses a strict reflect-cpp TOML configuration, and builds as a 32-bit CMake/MSVC project.

The target platform for this feature is Windows 10 or newer. The design may therefore use Core Audio, event-driven WASAPI exclusive mode, MMCSS, and other Windows 10-era facilities without supporting older Windows versions.

## Binary Evidence

The analysis target is `H:\gc\game471.exe.i64`, with image base `0x00400000`. Evidence was collected through one reusable daemon-backed `ida-cli` session.

### DirectSound initialization

- `DSOUND!DirectSoundCreate8` is imported at `0x006AD080`; its thunk is at `0x00504144`.
- `sub_615410` at `0x00615410` calls `DirectSoundCreate8(NULL, ...)`.
- The game calls `IDirectSound8::SetCooperativeLevel(hwnd, DSSCL_PRIORITY)`.
- It creates a primary buffer with `DSBCAPS_PRIMARYBUFFER`.
- It sets the primary format to PCM, stereo, 44,100 Hz, 16-bit, block alignment 4, and 176,400 bytes per second.
- RTTI identifies the game-side manager as `CDirectSoundManager`.

### Secondary buffers

- Static resources use flags `0x50082`: `DSBCAPS_STATIC | DSBCAPS_CTRLVOLUME | DSBCAPS_GETCURRENTPOSITION2 | DSBCAPS_LOCDEFER`.
- Streaming resources use `0x50182`, adding `DSBCAPS_CTRLPOSITIONNOTIFY` as a capability flag.
- `sub_615E60` creates secondary buffers through `IDirectSound8::CreateSoundBuffer`.
- `sub_6142E0` writes source data through `Lock`, memory copy, and `Unlock`.
- `sub_615BA0` optionally calls `SetCurrentPosition(0)` and then `Play`.
- `sub_6146A0` calls `Stop`.
- `GC120FPS_DSoundChannel_GetPlayCursorMs` calls `GetCurrentPosition` and converts the source-byte cursor to milliseconds.
- `GC120FPS_DSoundChannel_SetPlayCursorMs` converts milliseconds back to a source-byte position and calls `SetCurrentPosition`.
- `sub_614730` maps the game's volume factors to DirectSound hundredths-of-decibels and calls `SetVolume`.
- `sub_6159D0` checks `GetStatus`, calls `Restore` for a lost buffer, and reloads its data.
- `sub_615D80` calls `GetCaps` to obtain the streaming buffer length.
- Destruction calls `Release` on buffers and the DirectSound device.

Although streaming buffers request `DSBCAPS_CTRLPOSITIONNOTIFY`, the game does not obtain `IDirectSoundNotify` or call `SetNotificationPositions`. `sub_614B30` polls `GetCurrentPosition` and refills with `Lock` and `Unlock`. The compatibility layer therefore does not need a notification subsystem.

### Streaming behavior

`sub_6163C0` sizes a streaming buffer to three seconds of source PCM using `nAvgBytesPerSec * 3.0`. The worker fills that circular buffer ahead of the play cursor and later overwrites consumed regions. The three-second buffer is refill headroom; it is not three seconds of endpoint render latency.

### Gameplay sounds

- `GC120FPS_CSoundManager_PlayTapSE_DoubleBuffered` at `0x00611610` immediately plays one of two tap channels.
- `GC120FPS_CSoundManager_PlayArrangeSE_DoubleBuffered` at `0x00611760` immediately plays one of two arrangement-effect channels.
- `GC120FPS_CSoundManager_PlayStageBgmPair_FadeMs` at `0x006126D0` plays the paired stage streams.
- The 120 Hz gameplay update calls the tap or arrangement function directly after detecting a gameplay input edge.

`PlaySoundA` is present only in a separate asynchronous helper for `sound/%s.wav`. It is not the main gameplay renderer. WINMM mixer imports control volume or device state but do not render the main game audio. The binary does not import XAudio2, WASAPI, AudioGraph, OpenAL, or a middleware audio engine.

## Asset Evidence

The gameplay-critical files already match the game's primary sample rate:

| Asset family | Count | Format |
|---|---:|---|
| `data\sound\TAP_SE1.wav` and `TAP_SE2.wav` | 2 | PCM16, mono, 44,100 Hz |
| `data\sound\SE_ARRANGE.wav` | 1 | PCM16, stereo, 44,100 Hz |
| `data\stage\sound\*.wav` | 2,718 | PCM16, stereo, 44,100 Hz |
| Stage `*_BGM.wav` subset | 936 | PCM16, stereo, 44,100 Hz |
| Stage `*_SHOT.wav` subset | 1,781 | PCM16, stereo, 44,100 Hz |

The wider scanned WAV set contains PCM16 and PCM24, mono and stereo, at 22,050, 44,100, and 48,000 Hz. These exceptional formats occur outside the native gameplay set and still need functional playback, but their conversion latency is not gameplay-critical.

## Endpoint Evidence

After the operator changed the Windows audio configuration, the current default console and multimedia endpoint is:

```text
{0.0.0.00000000}.{7c7578a2-cb53-4888-82bc-977f18875721}
```

The diagnostic probe reported:

- Default device period: 10.0000 ms.
- Minimum device period: 3.0000 ms.
- Shared mix format: 44,100 Hz, stereo, 32-bit float.
- Shared default, fundamental, minimum, and maximum engine period: 441 frames, or 10.0000 ms.
- Shared support for the game's 44,100 Hz stereo PCM16 format: success.
- Exclusive support for the game's 44,100 Hz stereo PCM16 format: success.
- Exclusive support for 48,000 Hz stereo PCM16: success.
- Exclusive support for the shared 32-bit-float mix format: unsupported.

The current diagnostic actively initializes its 48,000 Hz test format. It checks 44,100 Hz through `IsFormatSupported` but does not print the aligned 44,100 Hz exclusive buffer returned by `Initialize` and `GetBufferSize`. The production backend must perform and log that initialization. Because three milliseconds is 132.3 frames at 44,100 Hz, the implementation must use the driver-aligned result rather than hard-code 132 or 133 frames.

### First runtime acceptance finding

The first enabled gameplay launch on 2026-07-14 initialized the XONAR endpoint successfully at 132 frames, or 2.993 ms, but produced continuously crackling or chopped audio while the game ran at 120 FPS. The run reported no endpoint HRESULT failure and ended before the first 30-second runtime summary. A source comparison with Spice2x found that its exclusive WASAPI wrapper likewise submits one complete endpoint buffer per event, but explicitly offers a larger fixed exclusive-buffer setting because approximately 3 ms can underrun and crackle on some endpoints.

The production policy is therefore revised from an unconditional device-minimum request to a fixed, operator-configurable buffer duration. The default is 10 ms. The selected duration remains fixed for the process lifetime; there is no runtime adaptation. A configured value of zero explicitly requests the device minimum for diagnosis or operator tuning.

## Goals

- Replace all of the game's DirectSound-rendered audio with one event-driven WASAPI exclusive stream when explicitly enabled.
- Require the game-native output format: 44,100 Hz, stereo, PCM16.
- Default to a fixed 10 ms exclusive period, allow an operator-selected fixed duration, and verify the actual aligned result.
- Keep gameplay BGM, SHOT, tap, and arrangement audio on a zero-sample-rate-conversion path.
- Keep the exclusive stream running continuously so a newly played hit sound begins in the next render period without stream-start cost.
- Preserve the DirectSound buffer, volume, loop, seek, status, lock, and cursor behavior observed in `game471.exe`.
- Report play cursors in each buffer's original source-byte domain and tie them to the endpoint play clock.
- Continue to play exceptional non-gameplay formats using a simple conversion path.
- Make endpoint mode, format, period, conversion use, timing failures, and late render wakes observable in `loader-log.txt`.
- Fail clearly instead of silently downgrading latency when the explicitly selected exclusive backend is unavailable.

## Non-Goals

- Supporting Windows versions older than Windows 10.
- Supporting an endpoint that cannot initialize 44,100 Hz stereo PCM16 in exclusive mode.
- Falling back to 48,000 Hz, shared WASAPI, AudioGraph, XAudio2, or the original DirectSound backend after the exclusive hook has been selected.
- Following a Windows default-device change during a running game session.
- Replacing or modifying WAV assets on disk.
- Implementing unused DirectSound 3D, pan, frequency-shift, effects, duplication, capture, or notification behavior.
- Supporting unknown executables that use a broader DirectSound contract than `game471.exe`.
- Treating automated tests as proof that perceived gameplay latency is acceptable.
- Claiming a measured physical input-to-speaker latency without a future loopback or microphone measurement.

## Configuration

Add two required fields to `ExperimentalConfig`:

```cpp
rfl::Rename<"enable_wasapi_exclusive_audio", bool>
    enable_wasapi_exclusive_audio = false;
rfl::Rename<
    "wasapi_exclusive_buffer_ms",
    WasapiBufferMillisecondsConfigValue>
    wasapi_exclusive_buffer_ms = 10;
```

`WasapiBufferMillisecondsConfigValue` is the Windows 32-bit unsigned numeric
storage type used for reflect-cpp. It remains distinct from `std::uint32_t`
because SDL aliases `SDL_Keycode` to `std::uint32_t` and the existing key-name
reflector would otherwise parse this numeric TOML field as a string. The
`ConfigManager` getter exposes the value as `std::uint32_t`.

The distributed configuration contains:

```toml
[experimental]
enable_wasapi_exclusive_audio = false
wasapi_exclusive_buffer_ms = 10
```

Both keys are required under the repository's strict configuration-upgrade contract. An older or partial configuration missing either key fails parsing instead of receiving a backward-compatible default.

`ConfigGUI` exposes the enable field as an experimental checkbox and the buffer duration as an unsigned integer, then round-trips both through reflect-cpp TOML serialization.

When the enable field is false, GCLoader installs no audio hook and the game uses its original DirectSound implementation. When true, failure to install or initialize the new backend is fatal and the error message tells the operator to set the field back to false. The buffer field is read once during initialization. Zero selects the endpoint minimum; a positive value selects that many milliseconds. The endpoint request is the larger of the selected duration and the endpoint minimum.

## Selected Architecture

Use a hybrid architecture:

- Raw Windows Core Audio owns endpoint selection, strict format negotiation, exclusive initialization, event-driven rendering, `IAudioClock`, and MMCSS.
- A no-device miniaudio engine owns voice mixing, source-format conversion, mono expansion, volume, looping, and seeking.
- A narrow custom DirectSound COM facade exposes the game-observed API to `game471.exe`.
- Existing MinHook support detours `dsound!DirectSoundCreate8`.

SpeexDSP is not required. All gameplay sources and the output engine run at 44,100 Hz, so gameplay does not need a sample-rate converter. Miniaudio's built-in linear conversion is sufficient for exceptional non-gameplay buffers.

### Why raw WASAPI remains outside miniaudio

Miniaudio supports WASAPI and exclusive mode, but the low-level Windows layer remains explicit so GCLoader can:

- query and log `GetDevicePeriod` directly;
- require the exact game-native exclusive format;
- implement the documented buffer-alignment retry;
- reject an unexpectedly large actual buffer;
- retain direct, stable access to `IAudioClock`;
- distinguish endpoint play position from mixer submission position;
- enforce the no-fallback contract without relying on library internals.

### Why miniaudio remains inside the engine

The no-device engine avoids reimplementing:

- multi-voice float mixing;
- PCM16 and packed PCM24 conversion;
- mono-to-stereo expansion;
- per-voice volume, loop, seek, and lifecycle behavior;
- exceptional 22,050/48,000 Hz conversion;
- deterministic mixer tests independent of physical hardware.

Pin miniaudio to a reviewed release in CMake rather than tracking its moving default branch. The implementation should use its permissive MIT-0 option.

## Components

### `WasapiAudioPatch`

This unit owns configuration gating and the `DirectSoundCreate8` MinHook detour.

`WasapiAudioPatchInit()` runs only in the game-process initialization branch. When disabled it logs the selected backend and returns without touching DirectSound. When enabled it resolves and installs only the `DirectSoundCreate8` target. It does not use a global MinHook enable or disable operation that could disturb RFID or NESYS hooks.

Installing the detour is lightweight. COM activation, device enumeration, thread creation, and WASAPI initialization do not occur under `DllMain`'s loader lock. They are deferred until the game's first intercepted `DirectSoundCreate8` call.

The detour accepts the game's observed default-device request and rejects COM aggregation. It lazily creates the process-wide exclusive engine, returns a custom `IDirectSound8`, and never calls the original function in enabled mode.

### `ExclusiveAudioEngine`

This process-lifetime object owns:

- one dedicated audio thread;
- COM initialization on that thread;
- `IMMDeviceEnumerator` and the startup default `eConsole` endpoint;
- `IAudioClient`, `IAudioRenderClient`, and `IAudioClock`;
- the WASAPI event and a fatal-control event;
- the actual endpoint format and period metadata;
- a no-device miniaudio engine configured for 44,100 Hz and two output channels;
- preallocated float mix and PCM16 endpoint buffers;
- global submitted-frame and endpoint-play timelines;
- real-time-safe counters.

The first intercepted `DirectSoundCreate8` starts the audio thread and waits for a bounded initialization result. The audio thread activates the default endpoint, confirms the exact PCM16 format with `IsFormatSupported`, queries the default and minimum periods, and initializes an event-driven exclusive client at the configured fixed duration.

Initialization uses `AUDCLNT_SHAREMODE_EXCLUSIVE` and `AUDCLNT_STREAMFLAGS_EVENTCALLBACK`. Buffer duration and periodicity are equal and use `max(device minimum, configured milliseconds)`, where zero configured milliseconds means the device minimum. If Windows returns `AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED`, the engine obtains the aligned frame count, releases and reacquires the audio client as required, recalculates the duration, and retries once.

After initialization, `GetBufferSize` is authoritative. The driver-aligned result produced by the documented alignment retry is accepted as the realizable fixed buffer for this endpoint and exact format. GCLoader does not multiply that result or add another software period. The log reports the configured duration, device minimum, requested duration, and actual aligned duration so any driver-level difference remains visible.

The thread obtains the render and clock services, sets the event, prefills the complete endpoint buffer with silence, starts the stream, and registers with MMCSS using the `Pro Audio` profile. Failure to establish the requested MMCSS scheduling is fatal in enabled mode.

The endpoint is fixed for the session. Default-device changes are ignored. Device removal, invalidation, or an unrecoverable render-client failure signals fatal control handling and requires a game restart.

The engine intentionally remains alive and continuously submits silence when no voice is active. Releasing the last DirectSound facade does not stop or reopen the endpoint. Process detach performs no complex teardown under the loader lock.

### `DirectSoundDevice`

This COM object implements the full ABI shape required by `IDirectSound8`, but only the game-observed behavior is functional.

Functional methods:

- `IUnknown::QueryInterface`, `AddRef`, and `Release`;
- `CreateSoundBuffer`;
- `SetCooperativeLevel`.

`QueryInterface` supports `IUnknown`, `IDirectSound`, and `IDirectSound8`. `SetCooperativeLevel` validates the request and accepts the observed `DSSCL_PRIORITY` call without changing endpoint ownership. Unused device methods return a consistent unsupported result.

`CreateSoundBuffer` distinguishes `DSBCAPS_PRIMARYBUFFER` from secondary buffers. Primary creation returns a `PrimarySoundBuffer`. Secondary creation validates the descriptor and returns a `SecondarySoundBuffer` backed by the process-wide engine.

### `PrimarySoundBuffer`

The primary buffer exists to satisfy the game's initialization contract. It accepts `SetFormat` only for 44,100 Hz stereo PCM16 and returns format or capability information consistently. It contains no playable source data because the WASAPI endpoint itself is the actual primary output.

The object implements the complete `IDirectSoundBuffer8` ABI with functional COM lifetime, format, and capability methods. Irrelevant playback and control methods return a documented no-op success or unsupported result according to their DirectSound role.

### `SecondarySoundBuffer`

Each secondary buffer owns:

- the accepted `DSBUFFERDESC` flags;
- a normalized copy of the original source `WAVEFORMATEX` or `WAVEFORMATEXTENSIBLE`;
- the original game-facing byte length;
- immutable published PCM snapshots plus one outstanding writable lock state;
- DirectSound volume, status, loop, and source-cursor state;
- a custom miniaudio data source;
- one miniaudio sound voice;
- a bounded render-span timeline for hardware-clock cursor reporting;
- COM reference state and diagnostics.

Supported source formats are the observed asset envelope: PCM or extensible PCM, one or two channels, 22,050/44,100/48,000 Hz, and packed 16- or 24-bit samples. Buffer lengths must be block-aligned.

The object implements the complete `IDirectSoundBuffer8` ABI so its vtable is valid. The methods proven necessary for the game are functional:

- `GetCaps`;
- `GetCurrentPosition`;
- `GetFormat`;
- `GetVolume`;
- `GetStatus`;
- `Lock` and `Unlock`;
- `Play` and `Stop`;
- `SetCurrentPosition`;
- `SetVolume`;
- `Restore`;
- `IUnknown::QueryInterface`, `AddRef`, and `Release`.

`QueryInterface` supports `IUnknown`, `IDirectSoundBuffer`, and `IDirectSoundBuffer8`. Unused pan, frequency, 3D, effects, duplication, and notification controls return the appropriate unsupported result. `Restore` succeeds because snapshots remain process memory and cannot become a DirectSound-lost hardware buffer.

## Buffer Publication

The game writes static and streaming data through DirectSound `Lock` and `Unlock`. The WASAPI render thread must never block behind file I/O or a large game-side copy.

Each buffer therefore uses copy-on-write immutable publication:

1. `Lock` validates the byte offset and length, clones the latest published snapshot on the calling game or streaming thread, and records one outstanding lock.
2. It returns one contiguous region or two wraparound regions into that private writable snapshot.
3. `Unlock` validates the returned pointers and lengths, completes the write, and atomically publishes the new immutable snapshot.
4. A miniaudio data-source read atomically captures one snapshot for its entire read operation.
5. A later game write cannot mutate memory currently being read by the audio thread.

The initial streaming fill may clone roughly three seconds of PCM, and later refills may clone that ring again. This work occurs on the game's streaming thread, not the render thread. It is preferred over a render-thread mutex or torn audio. A later implementation plan may optimize copying only after profiling proves it necessary; the correctness contract remains immutable publication.

Only one `Lock` may be outstanding per buffer. Invalid ranges, unaligned lengths, mismatched `Unlock` arguments, or unsupported lock flags return DirectSound errors without publishing partial data.

## Playback and Conversion

`Play` applies the requested loop flag and starts the miniaudio voice at the current source position. `Stop` stops future rendering and preserves the reported source cursor. `SetCurrentPosition` creates a new playback epoch, seeks the data source, and resets any per-source conversion state before the next rendered span. Repeated `Play` calls follow the DirectSound behavior needed by the game's double-buffered sound channels.

DirectSound volume values are hundredths of a decibel from `DSBVOLUME_MIN` through `DSBVOLUME_MAX`. The facade converts them to miniaudio gain without inventing another volume curve. No additional gain smoothing is used because smoothing would delay a requested hit-sound level change.

The miniaudio engine runs at 44,100 Hz and outputs two float channels:

- 44,100 Hz stereo PCM16 gameplay data requires only PCM16-to-float conversion before mixing.
- 44,100 Hz mono tap data requires PCM16-to-float conversion plus equal left/right expansion.
- Neither path performs sample-rate conversion or adds a resampler lookahead.
- 44,100 Hz PCM24 requires only sample-format conversion.
- 22,050 and 48,000 Hz exceptional buffers may use miniaudio's built-in linear sample-rate conversion.

The render thread asks miniaudio for exactly the endpoint frame count into preallocated float storage, converts the mixed block to interleaved PCM16 at the same 44,100 Hz rate, and submits it. Float-to-PCM16 conversion is not resampling and adds no buffering stage.

Startup and runtime diagnostics count native-rate buffers, sample-format conversions, and sample-rate conversions separately. A gameplay stage is expected to create zero sample-rate-converted BGM, SHOT, tap, or arrangement buffers.

## Endpoint-Clock Cursor Semantics

Miniaudio renders ahead into the endpoint buffer, while DirectSound `GetCurrentPosition` is expected to expose a play cursor. Returning the raw miniaudio data-source cursor would lead the actual speaker position by the queued endpoint frames.

The engine therefore owns one monotonically increasing output-frame timeline. Each render submission has a global 44,100 Hz output-frame begin and end. Every active secondary buffer publishes a small fixed-size ring of render spans containing:

- output-frame begin and end;
- original unwrapped source-frame begin and end;
- playback or seek epoch;
- loop-wrap metadata;
- whether the source ended inside the span.

The render thread writes these spans through a sequence counter. It performs no allocation and takes no game-thread lock. `GetCurrentPosition` obtains the current `IAudioClock` position, converts it to the engine output-frame timeline, selects the corresponding source span, and interpolates the source position. The result is wrapped to the original source length and converted to original source bytes.

This model handles:

- a sound that started inside the currently queued endpoint buffer;
- a voice that ended before the end of a render block;
- BGM loop boundaries;
- a seek or resynchronization while old audio is still queued;
- exceptional source rates without reporting output-format bytes to the game.

The second DirectSound write-cursor result is returned as a conservative source-domain position one actual endpoint buffer ahead of the play cursor. The observed game streaming worker does not consume that value, but the result remains internally consistent.

## Real-Time Rules

After successful initialization, the audio thread may only:

- wait for its WASAPI event;
- obtain and release endpoint buffers;
- read already-published immutable source snapshots;
- run bounded miniaudio mixing and same-rate PCM conversion;
- update preallocated render spans and atomic counters;
- record, but not format or log, an error code.

It may not:

- allocate or free memory;
- clone a DirectSound buffer;
- open or read files;
- log through plog;
- show UI;
- initialize COM objects or reconfigure the endpoint;
- wait for a game-thread mutex.

## Error Handling

### Hook installation

If the opt-in hook cannot be installed, GCLoader logs the MinHook stage and status, displays an actionable message, and fails game startup. It must not leave a partially active hook.

### Initial endpoint setup

Any of the following is fatal:

- no default console render endpoint;
- exact 44,100 Hz stereo PCM16 exclusive format unsupported;
- endpoint already held by another exclusive client;
- configured fixed-period initialization or alignment retry failure;
- actual endpoint buffer materially larger than the reported minimum;
- render or clock service unavailable;
- event registration failure;
- MMCSS `Pro Audio` registration failure;
- endpoint start failure.

The error log contains the endpoint ID, requested format, API stage, and HRESULT. A message tells the operator that the explicit low-latency backend failed and that `enable_wasapi_exclusive_audio` must be set to false to restore original DirectSound behavior. There is no automatic fallback.

### Runtime endpoint failure

The real-time thread atomically records the failing stage and HRESULT, outputs silence if the API still permits it, and signals a fatal-control event. A non-real-time monitor formats the log, displays the restart or disable-hook instruction, and terminates the process. It does not attempt to follow another default endpoint or rebuild audio state mid-song.

### Late rendering

A late event wake or mixer short-read increments counters and fills any required endpoint frames with silence. One late callback does not terminate the game. Repeated lateness remains visible in logs and is a failed manual acceptance result even if the endpoint itself stays valid.

## Diagnostics

Successful startup logs:

- requested and active audio backend;
- endpoint name and ID;
- exact exclusive format;
- default and minimum device periods;
- configured fixed duration and requested duration;
- actual aligned endpoint frames and milliseconds;
- exclusive event-driven initialization success;
- MMCSS profile and priority result;
- miniaudio mixer rate and channels.

Non-real-time runtime summaries log:

- render callback count;
- maximum simultaneous voices;
- native-rate buffer count;
- sample-format-converted buffer count;
- sample-rate-converted buffer count;
- native gameplay buffer count;
- late event wakes;
- silence fallbacks;
- cursor-timeline read failures;
- endpoint HRESULT failures.

Per-buffer filenames are not available at the DirectSound boundary, so gameplay-native classification is based on format and observed creation/use patterns rather than fragile filename hooks. Manual gameplay validation remains authoritative for proving that the expected stage buffers used the native path.

## Verification Strategy

Automated verification supports the implementation but does not replace playing the game.

### Automated build and component checks

Focused CMake/CTest targets cover deterministic behavior:

- strict configuration parsing and failure when either audio key is missing;
- ConfigGUI round-trip, the distributed default-off value, and the 10 ms buffer default;
- source-format validation and native-versus-converted classification;
- COM identity, supported interface IDs, and reference counting;
- primary-buffer format validation;
- secondary `GetCaps`, `GetFormat`, status, volume, play, stop, seek, loop, and restore behavior;
- wrapped `Lock`, invalid lock rejection, matching `Unlock`, and immutable snapshot publication;
- deterministic no-device miniaudio rendering for mono expansion, multiple-voice mixing, clipping, native-rate bypass, and exceptional linear conversion;
- output-clock render spans with simulated first-period starts, queued audio, loop wrap, stop, seek, and resynchronization;
- source-frame and source-byte cursor conversion;
- strict WASAPI call order and failure behavior through a mockable endpoint abstraction;
- configured-duration selection, explicit zero-to-device-minimum behavior, and driver-aligned fixed-period acceptance;
- fatal-error handoff from the real-time thread to non-real-time reporting;
- proof that render-path helpers do not allocate or log in their steady-state test path.

These checks show that the implementation's deterministic mechanics behave as designed. They do not establish that all game audio works or that latency feels improved.

### Primary manual acceptance

The primary acceptance test is performed by the operator launching and playing `game471.exe`.

The required manual sequence is:

1. Disable the hook and confirm the original DirectSound game still starts and plays audio.
2. Enable the hook and confirm startup completes without an audio error dialog.
3. Confirm the log reports exclusive 44,100 Hz stereo PCM16, configured 10 ms duration, the actual aligned fixed-period buffer, successful MMCSS registration, and no backend fallback.
4. Exercise attract mode and menus long enough to hear representative navigation, voice, and menu audio, including exceptional formats.
5. Play multiple stages and confirm both BGM and SHOT streams start, stay synchronized, seek or resynchronize correctly, fade correctly, and transition cleanly.
6. Exercise both tap sounds and the arrangement effect repeatedly, including dense gameplay.
7. Confirm logs show zero sample-rate conversion for gameplay buffers and no sustained late-wakeup, silence-fallback, or endpoint-error condition.
8. Judge whether hit-sound response feels materially less delayed during actual play.

The feature is not accepted until the operator completes this play test. Automated success alone is insufficient. The operator's gameplay judgment is the authoritative latency acceptance result for this phase.

## Open-Source and Platform References

- Microsoft, [Low Latency Audio](https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/low-latency-audio).
- Microsoft, [Exclusive-Mode Streams](https://learn.microsoft.com/en-us/windows/win32/coreaudio/exclusive-mode-streams).
- Microsoft, [`IAudioClient`](https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nn-audioclient-iaudioclient).
- Microsoft, [`IAudioClock`](https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nn-audioclient-iaudioclock).
- [miniaudio](https://github.com/mackron/miniaudio), used under its MIT-0 option for no-device mixing and exceptional conversion.
- [DSOAL](https://github.com/kcat/dsoal) and Wine's DirectSound implementation may be consulted as semantic references only. Their implementation code is not copied into this feature.

## Acceptance Summary

The design is successful when the opt-in hook provides all game audio through one strict 44,100 Hz event-driven WASAPI exclusive stream at the configured fixed period (10 ms by default), gameplay audio uses no sample-rate conversion, logs prove the intended backend stayed active without sustained render faults, and the operator can launch and play the game with correct audio while perceiving materially reduced hit-sound latency.

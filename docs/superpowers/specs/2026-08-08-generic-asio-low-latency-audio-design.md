# Generic ASIO® Low-Latency Audio Backend Design

**Date:** 2026-08-08

**Status:** Approved

ASIO is a registered trademark of Steinberg Media Technologies GmbH.

## Summary

Add a generic, user-selected ASIO output backend beside the existing WASAPI
exclusive and native DirectSound paths.

The game continues to enter GCLoader through `DirectSoundCreate8`. The existing
DirectSound facade, miniaudio mixer, source-cursor timelines, and shared gameplay
song clock remain authoritative. ASIO adds a second output scheduler below that
common render core; it does not replace the game-facing DirectSound contract and
does not add vendor-specific Xonar behavior.

The user chooses the backend and its backend-native buffer size:

- WASAPI uses milliseconds;
- ASIO uses an exact frame count;
- an explicit ASIO registry driver name selects the driver;
- a base output channel selects one stereo pair.

ConfigGUI offers installed drivers and common names as editable suggestions. A
32-bit helper validates the exact selection before saving without loading an
arbitrary vendor DLL into ConfigGUI. Runtime performs the authoritative probe
again. Failure before ASIO becomes active falls back to WASAPI; a committed ASIO
stream is not hot-switched mid-song.

The Steinberg ASIO SDK remains an external CMake dependency. GCLoader uses the
SDK under GPLv3 for public ASIO-enabled builds and publishes corresponding source
without vendoring the SDK into the git repository.

## Related Work

This design builds on these accepted contracts:

- `docs/superpowers/specs/2026-07-12-wasapi-exclusive-low-latency-audio-design.md`
- `docs/superpowers/specs/2026-07-14-wasapi-fixed-period-clock-pacing-design.md`
- `docs/superpowers/specs/2026-07-17-wasapi-exclusive-48khz-output-design.md`
- `docs/superpowers/specs/2026-07-28-wasapi-shared-gameplay-song-clock-design.md`
- `docs/reverse-engineering/wasapi-audio-replay-runtime-validation.md`

Those documents remain authoritative for DirectSound behavior, mixer snapshots,
fixed-period WASAPI rendering, endpoint-backed play cursors, and the exact BGM
source cursor consumed by the gameplay clock.

External references used during investigation:

- [Steinberg ASIO open-source SDK](https://www.steinberg.net/developers/asiosdk-open/)
- [Microsoft low-latency audio guidance](https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/low-latency-audio)
- [rs_asio](https://github.com/mdias/rs_asio)
- [ASIO4ALL](https://www.asio4all.eu/)
- [FlexASIO](https://github.com/dechamps/FlexASIO)

## Confirmed Feasibility

### Game interception boundary

The current `game471.exe.i64` imports only `DirectSoundCreate8` from DSOUND.
The call at `0x0061543E` belongs to `sub_615410`, which performs this sequence:

1. `DirectSoundCreate8`;
2. `SetCooperativeLevel`;
3. primary-buffer creation;
4. primary format selection for stereo PCM16 at 44.1 kHz.

No XAudio import or second DirectSound creation path needs an ASIO-specific
binary patch. The existing exported-function hook remains sufficient.

### Existing GCLoader architecture

The current pipeline is:

```text
game DirectSound calls
        |
DirectSound facade and secondary buffers
        |
miniaudio mixer + AudioCursorTimeline
        |
ExclusiveAudioEngine
        |
WASAPI exclusive endpoint + presentation clock
```

`ExclusiveAudioEngine` currently owns WASAPI-specific endpoint and scheduling
objects directly. The reusable seam is therefore below the mixer and above the
output scheduler, not at the game's device-enumeration layer.

### Installed Xonar ASIO driver

The machine has a native 32-bit registration:

| Property | Observed value |
|---|---|
| Registry key | `HKLM\SOFTWARE\ASIO\XONAR SOUND CARD` in the 32-bit view |
| CLSID | `{30D54986-2A72-4827-8A89-E0B096EABE69}` |
| DLL | `...\Win10\x86\AsusSC40.dll` |
| Version | `10.0.0016.9163` |
| Signature | Valid Microsoft Windows Hardware Compatibility Publisher signature |
| Input channels | 0 |
| Output channels | 8 |
| Output sample type | `ASIOSTInt24LSB` on all observed outputs |
| Current sample rate | 48,000 Hz |
| Supported relevant rates | 44,100 and 48,000 Hz |
| Buffer range | 192 through 2,400 frames |
| Preferred buffer | 192 frames |
| Granularity | 1 frame |
| Reported output latency | 384 frames at 48 kHz, or 8 ms |

The game and loader are Win32. The native 32-bit driver removes the principal
ABI blocker. The 64-bit registration named `XONAR SOUND CARD(64)` is not a valid
choice for the game process and must not be offered as an installed selection.

### WASAPI comparison

The Xonar speaker endpoint advertises:

- default period: 10 ms;
- minimum period: 3 ms;
- exact exclusive stereo PCM16 support at 44.1 and 48 kHz.

Earlier runtime evidence already initialized this endpoint at about 2.993 ms,
but audio crackled continuously at 120 FPS. The accepted production setting is
10 ms. ASIO's preferred 192-frame callback is 4 ms at 48 kHz and therefore
deserves a gameplay stability test, but its API name alone does not prove lower
end-to-end latency.

### rs_asio applicability

rs_asio is useful reference material for registry enumeration, driver lifecycle,
buffer negotiation, planar sample conversion, and compatibility pitfalls. It is
not a drop-in component. Rocksmith exposes a WASAPI enumeration boundary, so
rs_asio injects fake WASAPI devices backed by ASIO. Groove Coaster exposes a
DirectSound creation boundary that GCLoader already owns; copying the fake-device
layer would add an unnecessary translation and duplicate the accepted mixer and
cursor model.

## Goals

- Support any usable registered 32-bit ASIO output driver through the generic
  Steinberg interface.
- Keep the user in control of DirectSound, WASAPI exclusive, or ASIO selection.
- Let the user select an exact ASIO buffer frame count and stereo output pair.
- Offer useful installed and common driver names without creating a whitelist.
- Block ConfigGUI save when the current ASIO selection cannot satisfy the output
  contract.
- Repeat validation at runtime and fall back to WASAPI before ASIO commits.
- Preserve the existing game-facing DirectSound contract and shared song clock.
- Keep all callback work bounded, preallocated, and suitable for a driver-owned
  real-time thread.
- Keep the ASIO SDK outside the repository while making public binary releases
  GPLv3-compliant and reproducible.
- Produce machine-backed evidence for the Xonar before claiming runtime success.

## Non-Goals

- Selecting an ASIO driver automatically because Windows selected a similarly
  named WASAPI endpoint.
- Choosing ASIO merely because an ASIO registration exists.
- Adding Xonar-specific code paths.
- Supporting ASIO input, recording, monitoring, DSD, aggregate devices, or more
  than one simultaneous ASIO driver.
- Replacing miniaudio, changing the source-buffer contract, or changing song
  tempo, pitch, judgement windows, or gameplay offsets.
- Opening or configuring third-party driver control panels from GCLoader.
- Silently rounding an unsupported user buffer request.
- Attempting an unverified mid-song ASIO-to-WASAPI clock handoff.
- Claiming ASIO4ALL, FlexASIO, or another uninstalled driver has been tested.

## Terminology

### ASIO registry name

The exact subkey name under the 32-bit `HKLM\SOFTWARE\ASIO` registry view.
Lookup follows Windows case-insensitive registry semantics. The driver's internal
`getDriverName` text is diagnostic data and need not equal the registry name.

### ASIO buffer frame count

The exact `bufferSize` passed to `IASIO::createBuffers`. The driver advertises a
minimum, maximum, preferred size, and granularity. GCLoader validates rather than
rounds the configured value.

### Presentation frame

The output-frame position currently anchored by the ASIO sample position and
system timestamp after startup timestamps become stable. It is not the tail of
the future block most recently rendered into a driver buffer.

### Committed backend

An output backend that has completed initialization, prepared buffers, started
its stream, and established a valid presentation clock. ASIO failure before this
point may fall back to WASAPI. Failure after it requires restart guidance.

## Configuration Decision

Replace the ambiguous WASAPI enable boolean with an explicit backend:

```toml
[experimental]
audio_backend = "wasapi_exclusive" # directsound | wasapi_exclusive | asio
wasapi_exclusive_buffer_ms = 10
asio_driver_name = "XONAR SOUND CARD"
asio_buffer_frames = 192
asio_output_base_channel = 0
```

### Defaults and validation

The distributed defaults are:

```toml
audio_backend = "directsound"
wasapi_exclusive_buffer_ms = 10
asio_driver_name = ""
asio_buffer_frames = 0
asio_output_base_channel = 0
```

Inactive backend-specific values remain serializable. When `audio_backend` is
`asio`, semantic validation requires:

- a nonempty driver name;
- a positive frame count;
- a base channel whose adjacent `base + 1` channel exists;
- successful save-time validation before ConfigGUI writes.

A hand-edited file may still become dynamically unusable after it was saved.
Runtime owns the final device and stream decision and falls back as specified
below.

### Legacy migration

Configuration document migration maps:

- `enable_wasapi_exclusive_audio = false` to `audio_backend = "directsound"`;
- `enable_wasapi_exclusive_audio = true` to
  `audio_backend = "wasapi_exclusive"`.

The legacy key is removed when the migrated document is persisted. A document
containing both keys fails as ambiguous. The three ASIO fields are added with
their inactive defaults during migration.

## ConfigGUI Decision

The Experimental section replaces the WASAPI checkbox with a three-way backend
choice. WASAPI keeps its millisecond control. ASIO shows:

- an editable driver-name combo;
- an exact unsigned frame-count input;
- the read-only duration represented by that count at 48 kHz;
- a base-channel combo populated with output channel names from the last probe;
- the driver's minimum, maximum, preferred size, and granularity;
- an explicit Validate state and the most recent error.

Driver-name choices are assembled in this order:

1. currently registered 32-bit driver names;
2. common convenience suggestions not already present;
3. the user's arbitrary typed value.

Initial convenience suggestions are:

- `XONAR SOUND CARD`;
- `ASIO4ALL v2`;
- `FlexASIO`;
- `KoordASIO`;
- `FL Studio ASIO`;
- `Generic Low Latency ASIO Driver`.

Suggestions are never treated as installed or compatible merely because they
are listed. ASIO cannot be selected when the 32-bit registry view contains no
ASIO driver. A selected universal driver such as ASIO4ALL or FlexASIO remains
responsible for its own underlying Windows endpoint configuration.

The first product-facing label uses `ASIO®`. The panel displays the official,
unaltered Compatible logo and the required Steinberg attribution. The asset is
copied from the external SDK into the built distribution; no SDK header or
source is committed for this purpose.

## Save-Time Probe

ConfigGUI must not load arbitrary vendor code in-process. It launches a Win32
`AsioProbe` helper hidden, captures a structured result through an anonymous
pipe, and enforces a bounded timeout. Crash, timeout, malformed output, or a
failed capability check prevents Save and leaves the existing file untouched.

The helper and runtime share a project-owned `gc_asio` library so the probe does
not become a second compatibility implementation.

For the selected registry name, sample rate, buffer, and base channel, the probe
performs this sequence:

1. enumerate the 32-bit ASIO registry view and resolve the exact key and CLSID;
2. instantiate `IASIO` through COM in the helper process;
3. create a hidden helper window and pass its `HWND` to `init`;
4. query driver identity, channel counts, current rate, and buffer limits;
5. require 48 kHz through `canSampleRate`;
6. validate the exact frame count against minimum, maximum, and granularity;
7. query both selected output channels and require supported PCM sample types;
8. prepare exactly those two output channels with `createBuffers`;
9. query input/output latency after buffer creation;
10. dispose buffers, restore any temporarily changed sample rate, release the
    driver, and uninitialize COM.

The probe never calls `start` and never intentionally emits audio. It may briefly
claim the device or change and restore the driver rate; the GUI explains this
before validation. The runtime check remains authoritative because device
availability and driver state can change after Save.

## Build and Licensing Decision

### External SDK

The SDK is not fetched and is not vendored. CMake defines a required cache path:

```cmake
GC_ASIO_SDK_DIR
```

If the cache value is absent, it is initialized from the environment variable of
the same name. The command-line cache value wins. Configuration fails unless the
root contains, at minimum:

- `README.md` and `LICENSE.txt` selecting the dual-licensed 2025+ SDK;
- `changes.txt` identifying SDK 2.3.4 or newer;
- `common/asio.h`;
- `common/asiosys.h`;
- `common/iasiodrv.h`;
- the official Compatible logo asset used by ConfigGUI.

The locally approved path is `H:\gc\artifacts\ASIOSDK`, but no source or
runtime behavior depends on that absolute path.

Only the interface headers are included. GCLoader implements its own narrow
registry, COM lifetime, buffer, conversion, and callback layers. Steinberg sample
host sources and rs_asio sources are not compiled or copied.

### License scope

The repository formalizes the author's public-domain intent for project-owned
code with CC0-1.0. The ASIO-enabled combined program is conveyed under
GPL-3.0-only because it incorporates the GPLv3 ASIO SDK interface. Third-party
components keep their own licenses.

The implementation adds:

- complete CC0-1.0 and GPL-3.0-only texts;
- a short root license-scope document;
- SPDX identifiers on new project-owned ASIO files;
- a third-party notice document containing the pinned dependency notices and
  Steinberg attribution;
- copies of the applicable notices in the distribution directory.

The current git history has one author. The pinned compiled dependencies were
audited as MIT, BSD, Boost Software License, or public-domain/MIT alternatives;
no immediate GPLv3 compatibility conflict was found. This audit is recorded as
engineering evidence, not represented as legal advice.

### Public source obligation

The SDK remains outside git, but every public ASIO-enabled binary release must
offer corresponding source for the exact binary. The release process produces a
matching source archive containing:

- the exact GCLoader revision and build instructions;
- the exact ASIO SDK files used to build it, including license text;
- the pinned third-party source revisions or compliant source-access material;
- the configuration needed to reproduce the Win32 build.

Merely linking to a mutable upstream SDK download is not the release strategy.
The repository may document how to acquire the SDK for ordinary development,
while the release archive preserves the exact corresponding source.

## Runtime Architecture

The selected architecture is:

```text
DirectSoundCreate8 hook
        |
DirectSoundDevice + primary/secondary facades
        |
backend-neutral AudioRenderCore
        |-------------------------------|
WasapiOutputBackend                AsioOutputBackend
event/MMCSS worker                 driver buffer callbacks
        |                               |
interleaved PCM16 endpoint         planar driver-owned channel buffers
```

`AudioRenderCore` owns the accepted mixer, voice creation, output-span planning,
cursor timelines, observers, and current presentation-frame publication.
Backends own only device discovery, format/buffer negotiation, scheduling,
submission, and physical-clock observations.

`DirectSoundDevice::SetCooperativeLevel` passes the game's real `HWND` to the
backend controller. Binary evidence shows the game calls it immediately after
`DirectSoundCreate8` and before buffer creation. This lets ASIO receive a valid
Windows system reference without guessing a foreground window. Creating a sound
buffer before successful priority cooperative level continues to fail according
to the facade contract.

WASAPI retains its current event-driven render worker. ASIO has no GCLoader
render thread: the driver owns the callback schedule and requests one configured
block at a time.

## Generic ASIO Host Contract

### Discovery and instantiation

- Read only the 32-bit `HKLM\SOFTWARE\ASIO` registry view.
- Convert UTF-8 config text and registry names through checked UTF-16 paths.
- Resolve a registered CLSID; never accept a configured DLL path.
- Use COM instantiation and hold the returned `IASIO` interface through an RAII
  owner on a non-callback control thread.
- Allow one active ASIO host per process. ASIO callbacks have no user-context
  pointer, so one atomic callback router targets the committed host. Probe work
  runs in a different process and does not conflict.

### Initialization order

Runtime performs:

1. COM initialization and registered-driver creation;
2. `init(game_hwnd)`;
3. capability and exact configuration validation;
4. `canSampleRate(48000)` and `setSampleRate(48000)` when necessary;
5. output-channel inspection;
6. callback-router publication;
7. `createBuffers` for the selected stereo pair and exact frame count;
8. `getLatencies` after creation;
9. callback-safe preallocation and clock reset;
10. `start`;
11. stable presentation-clock establishment;
12. backend commit.

Teardown calls `stop`, waits for the SDK guarantee that callbacks have ceased,
clears the callback router, disposes buffers, releases the driver, and then
uninitializes COM. Driver code is never unloaded from `DllMain`.

### Buffer validation

For driver values `(minimum, maximum, preferred, granularity)`:

- the configured value must be within the inclusive range;
- granularity `-1` permits powers of two only;
- granularity greater than `1` requires an exact multiple;
- granularity `0` or `1` accepts every integer in range;
- no value is silently clamped or rounded.

The preferred value is displayed and used to populate a newly selected driver's
empty field, but it does not override an explicit user value.

### Channels and sample formats

The configured base channel selects `base` as left and `base + 1` as right.
Only those output channels are activated. Both must use a supported PCM type.

The first implementation supports common little-endian Windows ASIO types:

- signed 16-, packed 24-, and signed 32-bit integer;
- 16-, 18-, 20-, or 24-bit valid data in a 32-bit integer container;
- 32- and 64-bit IEEE floating point.

MSB and DSD types are rejected with the exact channel index and type in the
diagnostic. The render core produces preallocated stereo float frames. The ASIO
adapter clips and deinterleaves them directly into the two driver-owned planar
buffers. No intermediate interleaved PCM16 block is added for ASIO.

## Real-Time Callback Contract

The preferred callback is `bufferSwitchTimeInfo`. The legacy `bufferSwitch`
path calls `getSamplePosition` from the callback and otherwise shares the same
render function.

Each callback may only:

1. validate the buffer index and committed callback generation;
2. read the supplied or queried sample position and timestamp;
3. plan the future output span using the reported output latency;
4. render exactly the configured frame count;
5. convert/deinterleave into the selected planar buffers;
6. publish atomic counters and presentation-clock observations;
7. call `outputReady` when the driver reported support.

The callback performs no allocation, mutex acquisition, driver discovery,
configuration lookup, file or console logging, COM lifetime change, blocking
wait, or last-owner destruction. Diagnostic formatting and fatal handling occur
on a control path that consumes atomically published state.

`directProcess` is advisory. On Windows the SDK permits processing on the
thread-based callback path; adding a second queued worker would add a block of
latency and a second deadline. The callback therefore processes directly and
records deadline/overload evidence.

## Presentation Clock and Shared Song Time

ASIO sample position resets to zero at `start` and identifies the first sample
of the current block. Output latency, queried after buffer creation, specifies
how far ahead the buffer being filled must be placed in the output timeline.

The mapper therefore maintains two distinct positions:

- the callback's stable current-block sample position is the presentation
  anchor published through `CurrentOutputFrame`;
- the buffer being filled is rendered for the future span selected from that
  anchor and `outputLatency`.

The DirectSound play cursor resolves the exact source position corresponding to
the presentation anchor through the existing `AudioCursorTimeline`. It never
reports the mixer's future render tail.

The SDK documents pre-start priming callbacks. The first two callbacks may share
one system timestamp even while sample positions advance. They fill silence as
needed but do not establish or advance the public clock. The mapper requires the
constant streaming sequence, normally observable from the third callback,
before publishing a nonzero presentation frame.

Invalid, regressing, non-block-aligned, non-advancing, or rate-changing ASIO time
information raises a backend fault. Time-info mode is preferred; the legacy
sample-position call is accepted only when it satisfies the same invariants.

This preserves the July 28 shared gameplay-song-clock rule: chart, judgement,
and audio consume one physical presentation timeline. An ASIO label is not
permission to return a submitted or decoded cursor.

## Failure and Fallback Policy

### Before backend commit

Any ASIO discovery, initialization, format, channel, buffer, callback, start, or
clock-establishment failure tears down the partial ASIO host and starts WASAPI
with the existing WASAPI configuration.

The log records at minimum:

```text
requested_backend=asio
active_backend=wasapi_exclusive
asio_driver_name=...
asio_failure_stage=...
asio_result=...
fallback_reason=...
```

The config file is not rewritten. If WASAPI also fails, its existing fatal
behavior and recovery guidance remain authoritative. Original DirectSound is
used only when the user explicitly selected `directsound`.

### After backend commit

Driver reset, buffer-size change, sample-rate change, resync request, device
loss, invalid clock, or callback failure sets a one-way fault flag. A control
path stops audio and displays guidance to restart. It does not silently switch
to WASAPI after song playback has begun because the two physical clocks have no
runtime-validated continuity handoff.

The next launch repeats ASIO validation and falls back to WASAPI if the driver
remains unavailable.

## Diagnostics

Startup logs include:

- requested and active backend;
- registry name, driver-reported name, CLSID, and version;
- sample rate and selected output channel indices/names/types;
- requested, minimum, maximum, preferred, and granular buffer sizes;
- reported input and output latencies;
- time-info, output-ready, and overload-report support;
- fallback stage and result when applicable.

Low-frequency monitoring consumes callback-published counters and reports:

- callback count and maximum callback duration;
- deadline misses;
- driver overload messages;
- reset, resync, latency-change, and rate-change messages;
- sample-position discontinuities;
- render gaps or skipped output frames;
- exact BGM cursor generations and unexpected seeks.

The callback itself never formats these records.

## Verification Strategy

### Automated tests

Use fake registry, COM, `IASIO`, process-launch, and render-core seams to cover:

- 32-bit driver enumeration, arbitrary Unicode names, missing registrations,
  malformed CLSIDs, and wrong-bitness-only registrations;
- exact buffer range and all granularity rules without rounding;
- base-channel bounds, channel-name reporting, mixed/unsupported channel types,
  and every supported conversion including clipping and silence;
- callback indices, double-buffer alternation, output-ready behavior, callback
  generation invalidation, and teardown ordering;
- the first two priming callbacks, stable third callback, output-latency future
  placement, legacy sample-position fallback, regression, and discontinuity;
- helper success, structured failure, timeout, crash, and no-write-on-failure;
- ASIO runtime failure before commit followed by one WASAPI startup;
- no automatic hot fallback after commit;
- config defaults, legacy migration, round-trip serialization, GUI edit model,
  and backend-specific validation;
- missing, old, or incomplete external SDK configure failures;
- retention of all existing WASAPI, DirectSound, mixer, cursor, and shared-clock
  tests.

### Hardware probe acceptance

On the current machine, save-time and runtime probes must report:

```text
driver=XONAR SOUND CARD
sample_rate=48000
output_channels=0,1
sample_type=ASIOSTInt24LSB
buffer_frames=192
output_latency_frames=384
```

Any difference is treated as new evidence and investigated rather than adjusted
by a Xonar-specific workaround.

### Gameplay acceptance

After focused and full automated tests pass, runtime deployment still requires
explicit operator authorization and a recoverable backup. Compare:

1. accepted WASAPI exclusive at 48 kHz and 10 ms;
2. Xonar ASIO at 48 kHz and 192 frames;
3. larger ASIO frame counts only if the preferred count cracks or overloads.

At 120 FPS, exercise menu audio, song load, normal play, looping stems, pause,
retry, result transition, repeated song starts, and a sustained session. Record:

- deployed binary identity and exact config;
- driver capability and latency report;
- callback and overload metrics;
- presentation-clock continuity;
- all explicit and unexpected BGM seeks;
- the user's audible verdict;
- any external latency measurement that can be reproduced.

The ASIO backend is not declared lower latency merely because 192 frames is
smaller than the current 10 ms WASAPI buffer. A final latency claim requires the
driver report plus stable gameplay evidence, and preferably a physical loopback
or equivalent measured output test.

### Common-driver claims

ASIO4ALL, FlexASIO, KoordASIO, FL Studio ASIO, and Steinberg's generic driver
remain convenience suggestions until their 32-bit versions are installed and
pass the same probe and runtime suite. No installer is downloaded or executed as
part of this design.

## Risks and Mitigations

| Risk | Mitigation |
|---|---|
| Vendor DLL crashes or hangs during Save | Run it in bounded Win32 `AsioProbe`, never ConfigGUI |
| API name is mistaken for latency proof | Compare actual stable configurations and record measured evidence |
| Render-ahead cursor reaches gameplay | Publish stable presentation anchor, never render tail |
| Initial priming timestamp advances clock | Ignore priming timestamps until constant streaming is established |
| Unsupported user buffer is silently changed | Validate exact range/granularity and reject |
| Multi-output driver routes to wrong speakers | Persist and display the selected stereo base channel and names |
| Callback misses deadlines | Preallocate, avoid locks/logging, collect atomic deadline and overload metrics |
| ASIO disappears after Save | Repeat the authoritative runtime probe and fall back before commit |
| Mid-song device loss corrupts timing | Stop with restart guidance; do not hot-switch clocks |
| Wrong-bitness driver is selected | Enumerate and instantiate only the 32-bit registry view |
| Universal wrapper adds hidden latency | Treat its reported latency and runtime results like any other driver |
| SDK is accidentally vendored or silently fetched | Require explicit `GC_ASIO_SDK_DIR` and fail configuration |
| GPL source obligations are missed | Produce a matching source archive with every public ASIO-enabled binary |
| Trademark use drifts | Copy the official logo unmodified and retain the required attribution |

## Acceptance Boundary

This design establishes technical feasibility and an approved implementation
contract. It does not claim an implemented backend, successful deployment,
stable Xonar gameplay, or lower measured latency. Those claims require the
automated, hardware, and gameplay evidence above.

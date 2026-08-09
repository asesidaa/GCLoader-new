<!-- SPDX-License-Identifier: CC0-1.0 -->

# ASIO Runtime Validation

**Date:** 2026-08-09
**Status:** Control-panel and runtime-diagnostic candidate builds accepted;
deployment and audible gameplay acceptance remain pending

## Acceptance Boundary

This record separates isolated-driver and build evidence from what still
requires an explicitly authorized game deployment and an audible hardware run.

The historical release probe was driven in a separate Win32 process with the
same bounded binary protocol now used by ConfigGUI's self-hosted ASIO mode.
The parent
closed the request pipe, drained the bounded response, enforced a five-second
timeout, and would have killed the helper on timeout. No ConfigGUI Save was
performed and no game or loader configuration was changed.

The helper performs `init`, capability queries, selected-channel inspection,
the documented pre-start `outputReady` probe, `createBuffers`, `getLatencies`,
`disposeBuffers`, driver release, and sample-rate restoration when needed. It
never calls `ASIOStart`, so that evidence does not claim audible output or
callback stability. The newer automated tests exercise callback and render
behavior with deterministic clocks and a fake Xonar-shaped driver; they still
cannot accept real Xonar timing or audible behavior.

## Historical Probe Build Identity

The binaries were built as x86 RelWithDebInfo from project revision
`0c5532356211ca37cafed852d867b4e7e71bdf81` with
`GC_ASIO_SDK_DIR=H:\gc\artifacts\ASIOSDK`.

| Artifact | SHA-256 |
|---|---|
| `iDmacDrv32.dll` | `60a2425f4294eafcf64181303203d03f7169b449219d6102e2a712e7b2b896f7` |
| `ConfigGUI.exe` | `abb28f35d4ba3ad18797a272a2d8f631ba4beba495019722bb3a737236e0e8b1` |
| `AsioProbe.exe` | `291af7f8cbebcbf50802f0d8f18d59a5310f23ee688a77cd413eef30c8b9fa8b` |

Both complete CTest configurations passed 88 of 88 tests. Debug and
RelWithDebInfo `iDmacDrv32.dll`, `ConfigGUI.exe`, and `AsioProbe.exe` report PE
machine `0x14c` (x86). The loader retains its existing 15 named exports and
ordinals.

The clean-revision corresponding-source package was also extracted and built
with `FETCHCONTENT_FULLY_DISCONNECTED=ON` while HTTP, HTTPS, and all-protocol
proxies pointed at an unreachable local endpoint. It produced all three x86
artifacts without creating a fetched `*-src` directory.

## Current Diagnostic Candidate

The control-panel and diagnostic implementation through revision `eea82ad`
was freshly configured and built as x86 Debug and RelWithDebInfo with
`GC_ASIO_SDK_DIR=H:\gc\artifacts\ASIOSDK`. Both complete suites passed 95 of
95 tests. These suites include the distribution and corresponding-source
checks, isolated ASIO process/control-panel tests, deterministic callback
cadence tests, exact paired sample conversion, render-cause accumulation, and
the fake-Xonar streaming backend test.

The RelWithDebInfo distribution contains only the intended runtime files:
`iDmacDrv32.dll`, `ConfigGUI.exe`, `config.toml`, and `card.txt`. It contains no
ASIO SDK source archive, SDK license/logo payload, separate probe executable,
or generated `imgui.ini`.

| Artifact | SHA-256 |
|---|---|
| `iDmacDrv32.dll` | `6c36b7316e2fb94cc42265a2631496eecffb88c85f8f88f87ab6cd5faeb4944f` |
| `ConfigGUI.exe` | `502fe5fe1f4382ade939001bdd4952c75fae27c4a7ec8e2a402eaae2073bb2c1` |

These binaries were not copied into `H:\gc` as part of this verification. The
checks establish build and deterministic behavior only; they do not establish
that the reported crackle or intermittent missing audio is fixed.

## Current 32-bit Registration

Only the 32-bit registry view was read.

| Field | Current value |
|---|---|
| Registered drivers | `XONAR SOUND CARD` |
| Selected registry name | `XONAR SOUND CARD` |
| CLSID | `{30D54986-2A72-4827-8A89-E0B096EABE69}` |
| Driver DLL | `c:\program files\asustekcomputer.inc\nhasussc40\driverasussc40\win10\x86\asussc40.dll` |
| Driver DLL SHA-256 | `98a097253c0e80ce53563d3a3b7d860ede46925ee3580822ea033be84e586b0d` |
| Signature | Valid, Microsoft Windows Hardware Compatibility Publisher |

The 64-bit registry view was not used for discovery or instantiation.

## Probe Requests

Two isolated requests were run in order:

1. inspection: driver `XONAR SOUND CARD`, frame count `0` (adopt preferred),
   output base channel `0`;
2. exact validation: driver `XONAR SOUND CARD`, frame count `192`, output base
   channel `0`.

Both helpers exited with code zero and returned a valid capability response.
The exact-validation result was:

| Capability | Current value |
|---|---|
| Registry name | `XONAR SOUND CARD` |
| Driver-reported name | `XONAR SOUND CAR` |
| Driver version | `1` |
| Original sample rate | `48000` Hz |
| Verified sample rate | `48000` Hz |
| Buffer minimum / maximum | `192` / `2400` frames |
| Buffer preferred / granularity | `192` / `1` frame |
| Effective exact buffer | `192` frames |
| Input channels | `0` |
| Reported output channels | `2` |
| Selected pair | `0`, `1` |
| Input / output latency | `192` / `384` frames |
| `outputReady` support | no |
| Overload-notification support | no |

The selected channels were:

| Index | Driver name | Sample type |
|---:|---|---|
| 0 | `HPOut00_00 ch` | `ASIOSTInt24LSB` |
| 1 | `HPOut00_01 ch` | `ASIOSTInt24LSB` |

Inspection and exact validation returned the same capability values. The
configured 192-frame request was accepted exactly; it was not rounded or
clamped.

## Difference From the Earlier Baseline

The earlier feasibility probe recorded eight outputs. The current driver state
reports two outputs with `HPOut` channel names. Repeating the isolated request
in inspection and validation modes reproduced the two-channel result.

This may reflect a current Xonar routing/control-panel mode, but that is an
inference only. No driver setting was changed during this validation. The
current two-channel report is treated as authoritative, and no Xonar-specific
exception was added to manufacture the older eight-channel result. Channel
pair `0/1` remains valid for the requested stereo output contract.

The driver-reported identity also omits the final `D` in `XONAR SOUND CAR`.
Selection correctly remains based on the exact registry name rather than this
diagnostic string.

## Observed Streaming Baselines Before Expanded Diagnostics

The following values are retained as baselines, not labeled as audio dropouts:

| Requested buffer | Expected period | Observed cumulative evidence |
|---:|---:|---|
| 384 frames | 8 ms | Maximum callback/render work was about 0.622 ms. The old undifferentiated silence count was 12 at 60 seconds and then grew around lifecycle/shutdown activity. |
| 192 frames | 4 ms | Maximum callback/render work was about 0.482 ms. The old undifferentiated silence count was 26 at 30 seconds and later grew around lifecycle/shutdown activity. |

Both runs reported zero deadline misses, overload notifications, ASIO
reset/resync/latency/buffer/rate changes, sample-position discontinuities, and
render-gap frames. The 192-frame log currently retained in `H:\gc` records a
maximum of 0.4823 ms and 26 silence substitutions in its first 30-second
summary.

Those facts did not prove that 192 frames was stable or unstable. In
particular, the old `silence_substitutions` total merged normal no-voice
startup/shutdown blocks with active short reads, mixer errors, and render
contract failures. It therefore could not prove that an audible in-song event
was missing audio at the mixer boundary.

## New Cumulative Diagnostic Fields

The candidate keeps logging bounded to the existing sites: one startup record,
one cumulative summary every 30 seconds, a fatal summary if needed, and one
final shutdown summary. There is no callback-, block-, voice-, or sample-level
log line.

| Field family | Meaning |
|---|---|
| `asio_expected_callback_us` | Exact configured buffer period. It is 4,000 microseconds for 192 frames at 48 kHz. |
| `callback_interval_samples`, `average_callback_interval_us`, `maximum_callback_interval_us` | Host time between adjacent valid callback-entry samples. |
| `early_callback_intervals` | Arrival intervals below one half of the configured period; this can expose catch-up bursts after a stall. |
| `late_callback_intervals`, `severe_callback_intervals` | Arrivals above 1.5 periods and above 2 periods. Severe is intentionally also counted as late. |
| `timed_callback_work_samples`, `average_callback_us`, `maximum_callback_us` | Time spent dispatching the callback, distinct from time between callback arrivals. |
| `timed_render_work_samples`, `average_render_us`, `maximum_render_us` | Mixer/render duration for both inline and deferred work. |
| `driver_interval_samples`, `maximum_driver_period_error_us`, `maximum_host_driver_interval_skew_us` | Driver `systemTime` duration versus the expected period and the matching host-QPC duration. Epochs are never compared. |
| `buffer_alternation_violations` | Driver repeated a double-buffer index instead of alternating it. |
| `no_active_voice_silence_blocks` | A successful short/zero mixer read when no voice was playing at that callback. This is factual, not automatically expected. |
| `active_short_read_blocks`, `short_read_missing_frames` | A playing voice existed but the mixer did not fill the block; missing frames cover all successful short reads. |
| `mixer_error_blocks`, `first_mixer_error` | Miniaudio returned an error; the first result is retained. |
| `render_contract_error_blocks` | Invalid render-buffer geometry or an impossible returned frame count. |
| `clipped_output_blocks`, `clipped_output_samples`, `maximum_absolute_output_sample` | Pre-clamp sample integrity across both stereo channels. Clipped samples are sample values, not stereo frames. |
| `zero_output_blocks_with_active_voice`, `zero_output_blocks_without_active_voice` | A complete successful, nonsubstituted block was exactly zero, classified using the callback-local active-voice count. |
| `non_finite_output_blocks` | NaN or infinity was detected before either driver channel was written; both channels are then cleared and the existing conversion fault is latched. |

`silence_substitutions` remains for comparison with older logs and is now the
saturating sum of the four mutually exclusive silence-reason counters.

### Interpreting an Audible Event

Compare cumulative deltas across the summaries surrounding the noted event;
do not diagnose from a lifetime total alone.

| Correlated observation | Boundary indicated |
|---|---|
| Late/severe host arrival while driver duration remains normal | Callback delivery or OS scheduling. |
| Driver-period error or a sample-position discontinuity | Driver/device clock or transport. |
| Normal arrival but high callback/render work | GCLoader callback or render path. |
| Active short read, mixer error, or active-voice zero block | Mixer/source-data path. |
| No-active-voice silence during a sound that should be playing | Upstream voice-start or game/control path. |
| Clean, nonzero converted blocks while the audible gap occurs | Below GCLoader's callback boundary, such as the ASIO driver/device/output path. |
| Clipping or non-finite input | Rendered sample integrity. |

## Operator-Only Audible Acceptance

The ASIO interface provides a generic `controlPanel()` entry point, but it does
not standardize a universal host-side buffer-size setter. ConfigGUI therefore
opens the selected driver's genuine panel. The vendor panel owns its displayed
device buffer setting; `asio_buffer_frames` remains GCLoader's exact requested
frame count and is revalidated after the panel closes.

Do not treat the automated build as audible acceptance. The operator sequence
is:

1. Manually deploy the final Release `iDmacDrv32.dll` and matching
   `ConfigGUI.exe`.
2. In ConfigGUI, open **ASIO Driver Settings**, select the driver's smallest
   supported setting, close the vendor panel, and let ConfigGUI re-inspect it.
3. Keep `asio_buffer_frames = 192` and output base pair `0`; Save only after
   exact isolated validation succeeds.
4. Run the game for 60-90 seconds and note the approximate second of every
   missing or crackling event.
5. Preserve the startup record, the 30-second summaries surrounding each event,
   and the final summary.
6. Compare cumulative deltas in the timing, silence-reason, and sample-integrity
   fields before choosing a code change or a LatencyMon/ETW capture.

Deployment is intentionally not part of this build procedure.

## Proven and Not Yet Proven

This run proves current 32-bit registration, COM instantiation, driver `init`,
exact 48 kHz support, buffer metadata, supported packed-24-bit channel types,
exact 192-frame buffer creation, the 384-frame reported output latency, and
clean non-streaming teardown.

It does not prove:

- `ASIOStart` or callback delivery;
- audible correctness, crackle-free playback, or overload behavior;
- presentation-clock continuity during gameplay;
- safe menu, song, retry, result, or sustained-session transitions;
- lower end-to-end latency than the accepted 10 ms WASAPI configuration.

Those claims require explicit deployment authorization followed by the
gameplay matrix in the ASIO design. The next runtime candidate should retain
the validated values:

```toml
[experimental]
audio_backend = "asio"
asio_driver_name = "XONAR SOUND CARD"
asio_buffer_frames = 192
asio_output_base_channel = 0
```

Runtime validation remains authoritative and may fall back to WASAPI only
before ASIO commits. No deployment was performed as part of this record.

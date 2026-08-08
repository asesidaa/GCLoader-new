<!-- SPDX-License-Identifier: CC0-1.0 -->

# ASIO Runtime Validation

**Date:** 2026-08-08
**Status:** Build and non-streaming Xonar probe accepted; deployment and
gameplay acceptance remain pending

## Acceptance Boundary

This record separates what was proven without starting an audio stream from
what still requires an explicitly authorized game deployment.

The release `AsioProbe.exe` was driven in a separate Win32 process with the
same bounded binary protocol used by ConfigGUI's `AsioProbeClient`. The parent
closed the request pipe, drained the bounded response, enforced a five-second
timeout, and would have killed the helper on timeout. No ConfigGUI Save was
performed and no game or loader configuration was changed.

The helper performs `init`, capability queries, selected-channel inspection,
the documented pre-start `outputReady` probe, `createBuffers`, `getLatencies`,
`disposeBuffers`, driver release, and sample-rate restoration when needed. It
never calls `ASIOStart`, so this evidence does not claim audible output or
callback stability.

## Build Identity

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
gameplay matrix in the ASIO design. The first runtime candidate should retain
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

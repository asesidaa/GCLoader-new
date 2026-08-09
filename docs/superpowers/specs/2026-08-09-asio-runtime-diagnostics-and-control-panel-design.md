# ASIO Runtime Diagnostics and Control Panel Design

**Date:** 2026-08-09
**Status:** Approved in conversation

ASIO is a registered trademark of Steinberg Media Technologies GmbH.

## Context

The first Xonar AE runtime tests established that GCLoader commits the native
32-bit `XONAR SOUND CARD` ASIO driver, requests the configured buffer size, and
renders stereo into the two headphone output channels. The driver accepted both
384 and 192 frames at 48 kHz. At 192 frames, its callback period is 4 ms while
its reported output latency remains 384 frames, or 8 ms.

The operator nevertheless hears intermittent missing audio and crackling at
192 frames. Existing cumulative diagnostics show no callback overlap, ASIO
change notification, sample-position discontinuity, render gap, or fatal
conversion failure. Maximum observed callback work remains far below 4 ms.
Those counters do not measure callback arrival cadence, driver-clock cadence,
clipping, or the reason that the common render core substituted silence.

Other ASIO hosts also expose the Xonar driver's own settings panel. ASIO does
not display that panel automatically: a host must initialize the driver and
call `IASIO::controlPanel()`. GCLoader's driver abstraction currently omits that
method. Consequently ConfigGUI hides driver-side buffering or safety settings
that may be distinct from the exact frame count passed to
`IASIO::createBuffers`.

This design adds targeted evidence and access to the genuine vendor panel. It
does not assume that 192 frames is unstable, change audio scheduling, or claim
to fix the audible symptom before the new evidence identifies its boundary.

This document supersedes only the earlier generic ASIO design's control-panel
non-goal. Its generic-driver, exact-buffer, isolated validation, runtime
revalidation, stereo-pair, fallback, and shared-song-clock decisions remain
authoritative.

## Goals

- Expose the selected ASIO driver's genuine control panel from ConfigGUI.
- Keep arbitrary vendor driver code and its UI isolated from the ConfigGUI
  process.
- Show no helper console, wrapper window, duplicate ConfigGUI, or taskbar item.
- Refresh capability information after the vendor panel closes without
  silently changing the operator's configured frame count.
- Distinguish late callback delivery from expensive callback work.
- Compare host callback cadence with the ASIO timestamps and sample positions
  supplied by the driver.
- Distinguish expected idle silence from mixer failures or short reads while a
  voice is active.
- Detect output clipping and non-finite render samples before ASIO sample-format
  conversion hides their source.
- Keep runtime reporting cumulative and infrequent, with no formatting,
  allocation, locking, or logging on the real-time callback path.
- Preserve a clean deployment containing no additional helper executable.

## Non-Goals

- Changing buffer size, sample rate, channel selection, song-clock math,
  gameplay timing, miniaudio behavior, or thread priority as part of this
  diagnostic change.
- Automatically accepting the driver's preferred size in place of the exact
  configured `asio_buffer_frames` value.
- Reimplementing any vendor control panel or adding Xonar-specific settings.
- Opening the control panel from the injected game process.
- Treating the existence of a control panel as proof that a driver or buffer
  size will run without dropouts.
- Adding per-callback, per-voice, or per-sample log messages.
- Adding general input-path instrumentation before the audio boundary is
  resolved.

## Control Panel Architecture

### Operator-facing behavior

When `audio_backend = "asio"` and the editable driver name is nonempty,
ConfigGUI offers an **Open ASIO Control Panel** action beside driver inspection.
The action uses the exact registry driver name currently in the editor. It does
not require the current frame count or stereo-pair selection because no stream
is created.

The normal ConfigGUI remains responsive. The only new visible window is the
genuine window created by the ASIO driver. There is no child console, helper
dialog, wrapper GUI, duplicate ConfigGUI, or taskbar entry.

While a panel request is active, ConfigGUI prevents another panel, ASIO
inspection, or Save operation from starting. Other harmless editor interaction
may continue. The UI reports that the driver panel is open and reports a typed
error if launch, registry lookup, COM initialization, driver initialization, or
`controlPanel()` fails.

When the panel host finishes successfully, ConfigGUI automatically performs a
fresh capability inspection. The refreshed report may show changed
minimum/maximum/preferred frames or latency. It does not silently overwrite the
editable `asio_buffer_frames` value. The existing exact validation remains the
save-time gate, and runtime validation remains authoritative.

### Background self-host mode

ConfigGUI recognizes a second exact internal mode before normal GUI startup:

```text
ConfigGUI.exe --asio-control-panel
```

The parent launches its own absolute executable path. The fixed internal mode
is the only command-line value. The user-controlled registry driver name travels
through a bounded binary stdin request and never through shell parsing or the
command line.

The host is an isolated background process, not a separately distributed
executable. It initializes a single-threaded COM apartment, creates an unshown
tool owner window suitable for `IASIO::init`, resolves and instantiates the
selected 32-bit driver, initializes it, and calls `IASIO::controlPanel()`.
It never creates buffers or starts audio.

Most drivers keep `controlPanel()` active until their modal window closes. To
support a driver that returns after creating an in-process modeless panel, the
host also pumps its STA messages while it owns any visible top-level window
other than the unshown owner. A panel delegated entirely to a vendor-owned
external process cannot be lifetime-controlled generically; in that case the
host reports the driver's return result and exits.

After the panel closes, the child writes one bounded structured result and
exits. The parent waits asynchronously without a normal five-second timeout;
the operator controls how long the settings window remains open. The process is
still placed in a kill-on-close Job Object. ConfigGUI shutdown closes the job
before joining its worker, so a hung vendor panel cannot block application
shutdown.

The launch retains the probe path's suspended creation, restricted inherited
handle list, stdin/stdout isolation, no-shell rule, and bounded protocol. The
panel host has no visible application window of its own. Driver crashes,
abnormal exits, malformed responses, and broken pipes are contained and become
typed UI errors.

### Driver abstraction

`IAsioDriver` gains a direct `ControlPanel()` forwarding operation matching
`IASIO::controlPanel()`. Production and fake drivers implement the same method.
The operation is used only by the ConfigGUI panel host; capability probing and
the game runtime do not call it.

## Runtime Diagnostic Model

### Reporting cadence

The existing startup report, cumulative 30-second runtime summary, fatal
summary, and final shutdown summary remain the only normal log sites. New
fields are cumulative from stream start. The control thread snapshots atomics
and formats human-readable microseconds, counts, sample magnitudes, and result
codes. The callback and deferred render worker perform no logging.

The startup report explicitly includes the expected callback period derived
from exact buffer frames and 48 kHz. For 192 frames it is 4,000 microseconds.

### Callback arrival and work

The callback runtime records QueryPerformanceCounter data for two separate
questions:

1. **Arrival cadence:** elapsed host time between successive ASIO callbacks.
2. **Callback work:** elapsed time spent inside the callback dispatch itself.

The cumulative snapshot includes the number and total duration of valid
samples, their average, and their maximum. Arrival intervals are additionally
classified relative to the configured period:

- early: less than one half of the expected period;
- late: greater than one and one half of the expected period;
- severe: greater than two expected periods.

Late and severe counts overlap intentionally: `severe` is the immediately
actionable subset of `late`. Early intervals expose burst/catch-up delivery
after a stall. Integer frame/rate arithmetic defines the thresholds; no
floating-point work is added to the callback.

Inline and deferred rendering both contribute to a dedicated render-duration
sample count, total, average, and maximum. This avoids treating a short
driver-callback dispatch as proof that deferred rendering also completed on
time. The snapshot also carries the already-recorded buffer-alternation
violations through to the formatted runtime summary.

### Driver clock correlation

For time-info and valid legacy callbacks, diagnostics retain the interval
between successive driver `systemTime` values and compare it with:

- the expected buffer period; and
- the matching host-QPC callback-arrival interval.

The summary reports the maximum absolute driver-period error and maximum
absolute host-versus-driver interval skew. These are duration comparisons, so
they do not assume that QPC and ASIO `systemTime` share an epoch. Existing exact
sample-position continuity and fatal clock validation remain unchanged and
continue to count discontinuities and missing render frames.

This separates three cases that the current maximum callback-work counter
cannot distinguish:

- the driver called the host late;
- the driver timestamp advanced normally but delivery to the host was late;
- callback delivery was normal but GCLoader spent too long rendering.

### Silence classification

`AudioRenderBlock` preserves the mixer's `frames_read` and a callback-local
active-voice snapshot instead of reducing all incomplete renders to one
boolean. The existing safety behavior remains: an incomplete or failed render
clears the complete output block before conversion.

Every substituted block falls into exactly one cumulative reason:

- `idle_silence_blocks`: successful short/zero read with no active voice;
- `active_short_read_blocks`: successful short read while at least one voice
  was active;
- `mixer_error_blocks`: non-success miniaudio result.

The snapshot also includes total missing frames from successful short reads and
the first non-success mixer result. The old total remains derivable as the sum
of the three reason counters and may remain in the log during transition for
easy comparison with earlier runs.

The active-voice value is captured by the mixer as part of the same render
result; the summary does not infer historical state from a later control-thread
snapshot. This is what lets startup/shutdown idle silence be separated from an
actual dropout while sound should be playing.

### Render sample integrity

The ASIO conversion path reports aggregate statistics while it already walks
each channel's float samples:

- blocks and samples whose magnitude exceeded 1.0 before clamping;
- maximum finite absolute sample magnitude;
- blocks containing a non-finite sample.

The statistics use fixed-size values and lock-free atomics. They do not add a
third traversal of the interleaved block. Existing behavior remains unchanged:
finite out-of-range values are clamped to the destination format, while a
non-finite value clears the driver block and latches a conversion fault.

Clipped counts are sample counts across both left and right output channels,
not stereo frame counts. This makes sustained clipping distinguishable from an
isolated peak without logging individual samples.

## Runtime Interpretation

The added fields provide this evidence boundary:

| Observation | Most likely boundary for the next investigation |
|---|---|
| Late/severe host intervals with matching driver-time progression | Driver callback delivery or system scheduling |
| Large driver-period error or sample-position discontinuity | ASIO driver/device clock behavior |
| High render duration relative to the period | GCLoader render path |
| Active short reads or mixer errors | Common miniaudio mixer/source path |
| Clipped or non-finite render samples | Mix level, source data, or sample conversion input |
| All host metrics clean while crackling remains audible | Driver/device/DPC path below the ASIO callback boundary |

The last case justifies a separate LatencyMon or ETW/WPR capture. This change
does not add those system-wide tools to GCLoader.

## Failure and Shutdown Behavior

- A missing driver or `ASE_NotPresent` control panel produces a clear ConfigGUI
  error and does not modify configuration.
- A child crash or malformed response cannot crash ConfigGUI and does not
  authorize Save.
- A panel may remain open indefinitely during normal use; ConfigGUI exit closes
  the Job Object and then joins the operation worker.
- Failed post-panel inspection leaves the prior editable values intact, marks
  the inspection stale, and prevents ASIO Save until validation succeeds.
- Diagnostic counter overflow uses saturating arithmetic where totals could
  otherwise wrap during a long-running cabinet session.
- Failure to obtain a QPC sample omits that timing sample rather than inventing
  a zero interval or changing audio behavior.
- New nonfatal diagnostic anomalies are reported only in the next cumulative
  summary. Existing fatal runtime conditions retain their immediate control-
  thread failure report.

## Verification

Behavioral tests cover:

1. `IAsioDriver::ControlPanel()` forwards the exact driver result.
2. The panel runner initializes the selected driver, passes a valid unshown
   owner window, calls the panel without creating buffers, and releases all
   resources on success and every failure stage.
3. The client launches the current ConfigGUI executable with only the fixed
   internal argument; Unicode driver names remain in the bounded stdin
   protocol.
4. The panel child has no helper window or console, retains restricted handles
   and kill-on-close containment, permits an operator-length lifetime, and can
   be terminated during ConfigGUI shutdown without blocking the join.
5. Panel completion invalidates the old inspection and triggers a fresh probe;
   changed capability data is displayed without rewriting configured frames.
6. Deterministic QPC sequences produce the expected interval averages, maxima,
   early/late/severe buckets, callback-work totals, and inline/deferred render
   totals.
7. Deterministic ASIO timestamps produce the expected driver-period error and
   host-versus-driver skew without relying on a shared clock epoch.
8. Full reads, idle zero reads, active short reads, and mixer errors populate
   mutually exclusive silence counters and preserve full-block clearing.
9. Every supported ASIO output format reports clipping and non-finite input
   consistently while preserving its existing conversion bytes and failure
   behavior.
10. Runtime summary formatting exposes every new counter with stable units and
    retains the existing startup, 30-second, fatal, and final cadence.
11. Relevant x86 Debug and Release builds and test suites pass, the DLL export
    surface remains unchanged, and `dist` gains no helper executable or other
    panel artifact.

After deployment, runtime acceptance uses the real Xonar panel to confirm the
driver-side setting, closes it so ConfigGUI refreshes the capability report,
validates the exact configured 192-frame request, and captures at least one
summary spanning an audible failure. Build and synthetic-test success are not
reported as proof that the cabinet audio symptom is resolved.

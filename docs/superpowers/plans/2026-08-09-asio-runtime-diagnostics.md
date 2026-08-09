# ASIO Runtime Diagnostics Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add cumulative, callback-safe evidence that separates ASIO callback delivery, driver-clock cadence, render work, mixer silence causes, zero output, clipping, and conversion faults without changing audio scheduling or gameplay behavior.

**Architecture:** Preserve callback-local facts at their source: `AudioRenderCore` returns exact mixer provenance, `AsioCallbackRuntime` records host/driver interval timing with lock-free integers, and a paired ASIO converter returns block integrity statistics. `AsioOutputBackend` only accumulates fixed-size counters; the existing control-thread observer formats them at startup, every 30 seconds, on failure, and at shutdown.

**Tech Stack:** C++23, Win32/x86, QueryPerformanceCounter, Steinberg ASIO SDK 2.3.4+, miniaudio, lock-free atomics, CMake 3.31+, Ninja/MSVC x86, CTest.

## Global Constraints

- The approved design is `docs/superpowers/specs/2026-08-09-asio-runtime-diagnostics-and-control-panel-design.md`.
- Work only in `H:\gc\artifacts\GCLoader\.worktrees\asio-audio-backend`; do not deploy to or mutate runtime `H:\gc`.
- Require `GC_ASIO_SDK_DIR=H:\gc\artifacts\ASIOSDK`; do not fetch or vendor the SDK.
- This is instrumentation, not an audio fix. Do not change ASIO/WASAPI buffer selection, sample rate, output pair, clock placement, callback inline/deferred policy, miniaudio mixing, thread priority, fallback, or gameplay timing.
- Keep startup, cumulative 30-second, failure, and final summaries as the only normal log sites. Do not add callback-, voice-, block-, or sample-level log messages.
- The callback and deferred render worker remain allocation-free, lock-free, exception-free, and free of formatting, file/console I/O, sleeps, waits, and mutexes.
- Use lock-free fixed-width atomics and saturating totals. Floating-point formatting happens only on the control thread.
- Expected period is exact `buffer_frames / 48000` seconds. Early means below one half, late above one and one half, and severe above two periods; severe is a subset of late.
- Compare host-QPC and driver `systemTime` durations, never absolute epochs. A missing QPC sample invalidates only the affected interval and does not create a zero sample or change audio.
- Preserve full-block clearing for mixer short/error/contract failures and non-finite conversion input.
- `no_active_voice_silence` means no playing mixer voice at that callback; it must not be labeled automatically expected.
- Clipped counts are left/right sample counts before clamp, not stereo-frame counts. Zero-output classification excludes substituted blocks.
- Keep all types generic; do not add Xonar- or vendor-specific thresholds or branches.
- New project-owned files use `SPDX-License-Identifier: CC0-1.0`; combined ASIO distribution licensing remains unchanged.
- Use deterministic behavior tests and injected clocks/actions. Do not add source-text, regex, mirrored-production, tautological, or nominal-coverage tests.
- Build and test Debug and RelWithDebInfo from the Visual Studio x86 environment before completion. Separate build proof from the operator's audible runtime acceptance.

---

## File and Responsibility Map

| File | Responsibility |
|---|---|
| `src/Audio/Mixer/MiniaudioMixer.h/.cpp` | Attach callback-local playing-voice count to each render result. |
| `src/Audio/Mixer/AudioRenderCore.h` | Publish exact frame count, active voices, missing frames, and silence reason. |
| `src/Audio/Mixer/AudioRenderCoreInternal.h` | Pure mutually exclusive silence classification and unchanged full-block clearing. |
| `src/Audio/Asio/AsioCallbackRuntime.h/.cpp` | Host arrival/work/render timing, period buckets, driver-time error, and host/driver skew. |
| `src/Audio/Asio/AsioSampleConverter.h/.cpp` | Validate/analyze/convert a complete stereo block and return clipping/peak/zero/non-finite facts. |
| `src/Audio/Asio/AsioOutputBackend.h/.cpp` | Accumulate render provenance and conversion facts, carry every callback counter, and snapshot atomics. |
| `src/Audio/Asio/AsioOutputBackendInternal.h` | Focused lock-free render-diagnostic accumulator used by production and direct behavior tests. |
| `src/Audio/AudioPatch.cpp` | Format expected period, averages, maxima, reason counters, and sample integrity only on the control thread. |
| `tests/Audio/AudioRenderCoreTests.cpp` | Pure render-result classification and clearing. |
| `tests/Audio/MiniaudioMixerTests.cpp` | Playing-voice snapshot at render time. |
| `tests/Audio/AsioCallbackRuntimeTests.cpp` | Deterministic QPC/driver timestamp timing and missing-QPC behavior. |
| `tests/Audio/AsioSampleConverterTests.cpp` | Byte preservation plus clipping, peak, zero, and non-finite facts for every supported format. |
| `tests/Audio/AsioOutputBackendTests.cpp` | End-to-end counter propagation and reason/sample aggregation. |
| `tests/Audio/AudioPatchTests.cpp` | Stable startup and cumulative summary field names/units. |
| `docs/reverse-engineering/asio-runtime-validation.md` | Interpret old runs and prescribe the next 192-frame evidence capture. |

---

### Task 1: Preserve mixer render provenance

**Files:**
- Modify: `src/Audio/Mixer/MiniaudioMixer.h`
- Modify: `src/Audio/Mixer/MiniaudioMixer.cpp`
- Modify: `src/Audio/Mixer/AudioRenderCore.h`
- Modify: `src/Audio/Mixer/AudioRenderCoreInternal.h`
- Modify: `tests/Audio/MiniaudioMixerTests.cpp`
- Modify: `tests/Audio/AudioRenderCoreTests.cpp`

**Interfaces:**
- Consumes: miniaudio result, returned frame count, fixed output span, expected frames, and callback-local `active_voices`.
- Produces: one exhaustive `AudioRenderSilenceReason` and complete `AudioRenderBlock` provenance while retaining current zero-on-incomplete behavior.

- [ ] **Step 1: Write failing render-provenance tests**

Extend the types under test:

```cpp
struct MixerRenderResult {
    ma_result result{MA_ERROR};
    std::uint64_t frames_read{};
    std::uint32_t active_voices{};
};

enum class AudioRenderSilenceReason : std::uint8_t {
    none,
    no_active_voice,
    active_short_read,
    mixer_error,
    render_contract_error,
};

struct AudioRenderBlock {
    std::span<const float> interleaved_stereo;
    ma_result mixer_result{MA_ERROR};
    std::uint64_t frames_read{};
    std::uint32_t active_voices{};
    std::uint32_t missing_frames{};
    AudioRenderSilenceReason silence_reason{
        AudioRenderSilenceReason::none};
    bool silence_substituted{};
};
```

In `AudioRenderCoreTests`, cover:

- exact successful full read with zero and nonzero samples: reason `none`, no clearing;
- successful zero/short read with zero active voices: `no_active_voice`;
- successful short read with one active voice: `active_short_read`;
- non-success result: `mixer_error` regardless of active count;
- wrong stereo span or `frames_read > expected_frames`: `render_contract_error`;
- exact `missing_frames` for valid short reads and zero otherwise.

Every non-`none` case must clear the entire supplied span. In
`MiniaudioMixerTests`, start a voice, render, and require
`result.active_voices == 1`; after stop/end, require zero on the next render.

- [ ] **Step 2: Build and verify RED**

```powershell
cmake --build --preset msvc32-debug --target AudioRenderCoreTests MiniaudioMixerTests
```

Expected: compilation fails because the result fields/reason enum do not exist.

- [ ] **Step 3: Implement exhaustive classification without changing samples**

Load `state_->active_voices` into each `MixerRenderResult` after
`ma_engine_read_pcm_frames`; early invalid/reentrant returns use zero. In
`FinalizeAudioRenderBlock`, use this precedence:

```cpp
contract invalid or frames_read > expected -> render_contract_error
result != MA_SUCCESS                    -> mixer_error
frames_read < expected && active != 0   -> active_short_read
frames_read < expected && active == 0   -> no_active_voice
otherwise                               -> none
```

Use checked subtraction for `missing_frames`. Retain the current complete-span
zero fill for every substituted reason and preserve the successful block
unchanged.

- [ ] **Step 4: Run and verify GREEN**

```powershell
cmake --build --preset msvc32-debug --target AudioRenderCoreTests MiniaudioMixerTests ExclusiveAudioEngineTests
ctest --test-dir build-msvc32-debug --output-on-failure -R '^(AudioRenderCoreTests|MiniaudioMixerTests|ExclusiveAudioEngineTests)$'
```

Expected: all pass; the WASAPI consumer remains behaviorally unchanged.

- [ ] **Step 5: Commit**

```powershell
git add -- src/Audio/Mixer/MiniaudioMixer.h src/Audio/Mixer/MiniaudioMixer.cpp src/Audio/Mixer/AudioRenderCore.h src/Audio/Mixer/AudioRenderCoreInternal.h tests/Audio/MiniaudioMixerTests.cpp tests/Audio/AudioRenderCoreTests.cpp
git commit -m "Preserve mixer render provenance"
```

---

### Task 2: Measure callback arrival, work, and driver-clock cadence

**Files:**
- Modify: `src/Audio/Asio/AsioCallbackRuntime.h`
- Modify: `src/Audio/Asio/AsioCallbackRuntime.cpp`
- Modify: `src/Audio/Asio/AsioOutputBackend.cpp`
- Modify: `tests/Audio/AsioCallbackRuntimeTests.cpp`
- Modify: `tests/Audio/AsioOutputBackendTests.cpp`

**Interfaces:**
- Consumes: exact buffer frames/rate, callback-entry/exit QPC, valid driver `systemTime`, inline/deferred render duration, and callback ordinal.
- Produces: lock-free totals/counts/maxima, early/late/severe buckets, driver-period error, and host/driver interval skew.

- [ ] **Step 1: Write deterministic timing tests**

Add the preparation contract:

```cpp
struct AsioCallbackTimingConfig {
    std::uint32_t buffer_frames{};
    std::uint32_t sample_rate{};
};

static std::expected<std::unique_ptr<AsioCallbackRuntime>, AsioFailure>
Prepare(IAsioBlockRenderer&,
        AsioLegacyPositionActions,
        AsioCallbackTimingConfig,
        AsioCallbackRuntimeActions =
            ProductionAsioCallbackRuntimeActions()) noexcept;
```

Extend `AsioCallbackRuntimeSnapshot` with:

```cpp
std::uint64_t callback_interval_samples{};
std::uint64_t total_callback_interval_ticks{};
std::uint64_t maximum_callback_interval_ticks{};
std::uint64_t early_callback_intervals{};
std::uint64_t late_callback_intervals{};
std::uint64_t severe_callback_intervals{};
std::uint64_t timed_callback_work_samples{};
std::uint64_t total_callback_ticks{};
std::uint64_t timed_render_work_samples{};
std::uint64_t total_render_ticks{};
std::uint64_t driver_interval_samples{};
std::uint64_t maximum_driver_period_error_ns{};
std::uint64_t maximum_host_driver_interval_skew_ns{};
std::uint64_t expected_period_ns{};
```

Retain existing maximum callback/render ticks, QPC frequency, and fault/change
counters.

Replace the fake QPC incrementer with a deterministic queued sequence. For
192/48,000, feed callback-entry intervals of 4 ms, 1 ms, 7 ms, and 9 ms at a
10 MHz frequency. Assert four interval samples with early=1, late=2,
severe=1, exact total/max, and severe included in late. Feed matching driver
times for zero error, then a 5 ms driver interval paired with a 7 ms host
interval and assert 1 ms maximum driver-period error and 2 ms maximum skew.

Test inline and deferred work counts/totals separately. Make one QPC query fail
and assert only the affected host interval/work sample is omitted; the next
unpaired callback must not bridge across the missing sample.

Also feed the accepted startup sequence `(0, t0)`, `(192, t0)`, `(384, t1)`.
The repeated initial driver timestamp is legal during clock priming and must not
increment driver interval, period-error, or host/driver-skew counters. The first
positive driver timestamp advance establishes the diagnostic baseline; the
following callback produces the first driver-correlation interval.

- [ ] **Step 2: Build and verify RED**

```powershell
cmake --build --preset msvc32-debug --target AsioCallbackRuntimeTests AsioOutputBackendTests
```

Expected: compilation fails because timing config and snapshot fields do not exist.

- [ ] **Step 3: Implement integer-only callback instrumentation**

Validate nonzero frames/rate and compute expected nanoseconds with checked
integer quotient/remainder arithmetic. Use lock-free atomics for previous
callback entry, previous driver time, validity/ordinal, totals, buckets, and
maxima. Saturating CAS addition prevents long-session wrap.

An arrival interval exists only for adjacent callbacks with valid entry QPC.
Driver correlation remains unprimed across permitted repeated initial
timestamps; the first positive timestamp advance establishes its baseline, and
only subsequent adjacent valid timestamps become samples. Host/driver skew
exists only when both intervals cover the same callback pair. Convert the host
interval duration to nanoseconds with checked integer math before subtracting
driver duration; never compare epochs.

Record callback work for every valid outer entry/exit QPC pair. Measure inline
render work with a separate QPC pair immediately around
`renderer_->RenderAsioBlock`, matching the deferred worker's existing dedicated
pair; do not reuse total callback duration as render duration. A failed timing
query omits only its own sample. Do not add allocations, locks, logs,
floating-point calculations, or new waits.

Pass `{request_.buffer_frames, 48'000}` from `AsioOutputBackend` and update the
test-only incumbent runtime setup accordingly.

- [ ] **Step 4: Run and verify GREEN**

```powershell
cmake --build --preset msvc32-debug --target AsioCallbackRuntimeTests AsioOutputBackendTests
ctest --test-dir build-msvc32-debug --output-on-failure -R '^(AsioCallbackRuntimeTests|AsioOutputBackendTests)$'
```

Expected: all existing callback safety/fault tests and new cadence tests pass.

- [ ] **Step 5: Commit**

```powershell
git add -- src/Audio/Asio/AsioCallbackRuntime.h src/Audio/Asio/AsioCallbackRuntime.cpp src/Audio/Asio/AsioOutputBackend.cpp tests/Audio/AsioCallbackRuntimeTests.cpp tests/Audio/AsioOutputBackendTests.cpp
git commit -m "Measure ASIO callback and driver cadence"
```

---

### Task 3: Analyze and convert complete stereo blocks atomically

**Files:**
- Modify: `src/Audio/Asio/AsioSampleConverter.h`
- Modify: `src/Audio/Asio/AsioSampleConverter.cpp`
- Modify: `tests/Audio/AsioSampleConverterTests.cpp`

**Interfaces:**
- Consumes: one finite interleaved stereo float block, two destination formats, and both planar driver spans.
- Produces: byte-exact conversion plus clipped-sample count, maximum absolute input, all-zero flag, and non-finite failure without partial writes.

- [ ] **Step 1: Write paired-conversion integrity tests**

Add:

```cpp
struct AsioStereoConversionStats {
    std::uint64_t clipped_samples{};
    float maximum_absolute_sample{};
    bool all_zero{};
    bool non_finite{};
};

struct AsioStereoConversionResult {
    bool converted{};
    AsioStereoConversionStats stats;
};

AsioStereoConversionResult ConvertFloatStereoToAsio(
    std::span<const float> interleaved_stereo,
    const std::array<ASIOSampleType, 2>& types,
    const std::array<std::span<std::byte>, 2>& destinations) noexcept;
```

For every supported ASIO output type, convert the existing `kStereo` fixture
into left/right destinations and assert the existing exact bytes, two clipped
samples, maximum `1.25F`, `all_zero == false`, and `non_finite == false`.
Convert a zero block and require no clipping, zero maximum, and `all_zero`.

For NaN and infinity in either channel, require `converted == false`,
`non_finite == true`, and both destination spans byte-identical to sentinels.
For invalid type, odd input, undersized destination, or overlapping destination
spans, require failure with `non_finite == false` and no writes.

- [ ] **Step 2: Build and verify RED**

```powershell
cmake --build --preset msvc32-debug --target AsioSampleConverterTests
```

Expected: compilation fails because paired conversion/stats do not exist.

- [ ] **Step 3: Implement one validation/analysis pass and one stereo write pass**

Validate formats, sizes, non-overlap, and all finite samples before writing.
During that validation pass compute maximum absolute magnitude, exact all-zero,
and count `abs(sample) > 1.0F`. Then convert both channel samples in one frame
loop using the existing quantization/storage helpers.

This replaces the backend's two calls, each of which currently scans the whole
interleaved block for finiteness. Retain `ConvertFloatStereoChannelToAsio` for
its existing callers/tests, but make the ASIO backend use only the paired API.

- [ ] **Step 4: Run and verify GREEN**

```powershell
cmake --build --preset msvc32-debug --target AsioSampleConverterTests
ctest --test-dir build-msvc32-debug --output-on-failure -R '^AsioSampleConverterTests$'
```

Expected: all supported byte fixtures and new integrity facts pass.

- [ ] **Step 5: Commit**

```powershell
git add -- src/Audio/Asio/AsioSampleConverter.h src/Audio/Asio/AsioSampleConverter.cpp tests/Audio/AsioSampleConverterTests.cpp
git commit -m "Report ASIO sample conversion integrity"
```

---

### Task 4: Aggregate and format the complete runtime evidence

**Files:**
- Modify: `src/Audio/Asio/AsioOutputBackend.h`
- Modify: `src/Audio/Asio/AsioOutputBackend.cpp`
- Modify: `src/Audio/Asio/AsioOutputBackendInternal.h`
- Modify: `src/Audio/AudioPatch.cpp`
- Modify: `tests/Audio/AsioOutputBackendTests.cpp`
- Modify: `tests/Audio/AudioPatchTests.cpp`

**Interfaces:**
- Consumes: Task 1 silence reason/provenance, Task 2 callback snapshot, Task 3 stereo conversion result, and existing mixer/cursor counters.
- Produces: one exhaustive cumulative `AsioRuntimeCountersSnapshot` and stable control-thread log fields with microsecond averages/maxima.

- [ ] **Step 1: Write failing aggregation and formatting tests**

Extend `AsioRuntimeCountersSnapshot` with every Task 2 timing field plus:

```cpp
std::uint64_t buffer_alternation_violations{};
std::uint64_t no_active_voice_silence_blocks{};
std::uint64_t active_short_read_blocks{};
std::uint64_t mixer_error_blocks{};
std::uint64_t render_contract_error_blocks{};
std::uint64_t short_read_missing_frames{};
ma_result first_mixer_error{MA_SUCCESS};
std::uint64_t clipped_output_blocks{};
std::uint64_t clipped_output_samples{};
std::uint64_t zero_output_blocks_with_active_voice{};
std::uint64_t zero_output_blocks_without_active_voice{};
std::uint64_t non_finite_output_blocks{};
float maximum_absolute_output_sample{};
```

Retain `silence_substitutions` as the sum-compatible historical total. In the
fake-Xonar backend test, assert the existing pre-voice stable render increments
`no_active_voice_silence_blocks` and not an active/error/contract reason. Start
a normal voice and require nonzero output. Start two full-scale voices to force
clipping and require clipped block/sample counters plus maximum magnitude above
1.0. Render a complete exact-zero source while its voice is playing and require
only `zero_output_blocks_with_active_voice`.

Define a focused accumulator in `AsioOutputBackendInternal.h`:

```cpp
struct AsioRenderDiagnosticsSnapshot {
    std::uint64_t no_active_voice_silence_blocks{};
    std::uint64_t active_short_read_blocks{};
    std::uint64_t mixer_error_blocks{};
    std::uint64_t render_contract_error_blocks{};
    std::uint64_t short_read_missing_frames{};
    ma_result first_mixer_error{MA_SUCCESS};
    std::uint64_t clipped_output_blocks{};
    std::uint64_t clipped_output_samples{};
    std::uint64_t zero_output_blocks_with_active_voice{};
    std::uint64_t zero_output_blocks_without_active_voice{};
    std::uint64_t non_finite_output_blocks{};
    float maximum_absolute_output_sample{};
};

class AsioRenderDiagnostics final {
public:
    void RecordRender(const AudioRenderBlock&) noexcept;
    void RecordConversion(
        const AudioRenderBlock&,
        const AsioStereoConversionResult&) noexcept;
    AsioRenderDiagnosticsSnapshot Snapshot() const noexcept;
};
```

Directly feed each Task 1 silence reason to `RecordRender` and require mutually
exclusive counters, exact saturated missing-frame total, and first mixer-error
latching. Feed clipped, zero, and non-finite Task 3 results to
`RecordConversion`; verify substituted blocks never enter zero-output counts.
Then assert the production backend carries this snapshot plus callback
arrival/work/render, driver error/skew, and buffer alternation into its public
snapshot.

In `AudioPatchTests`, populate every field with distinct values and require
these stable names:

```text
asio_expected_callback_us
callback_interval_samples
average_callback_interval_us
maximum_callback_interval_us
early_callback_intervals
late_callback_intervals
severe_callback_intervals
timed_callback_work_samples
average_callback_us
maximum_callback_us
timed_render_work_samples
average_render_us
maximum_render_us
driver_interval_samples
maximum_driver_period_error_us
maximum_host_driver_interval_skew_us
buffer_alternation_violations
no_active_voice_silence_blocks
active_short_read_blocks
mixer_error_blocks
render_contract_error_blocks
short_read_missing_frames
first_mixer_error
clipped_output_blocks
clipped_output_samples
zero_output_blocks_with_active_voice
zero_output_blocks_without_active_voice
non_finite_output_blocks
maximum_absolute_output_sample
```

Assert a zero count/frequency formats averages as zero without division or
`nan`/`inf`. Existing fields and summary prefix remain present.

- [ ] **Step 2: Build and verify RED**

```powershell
cmake --build --preset msvc32-debug --target AsioOutputBackendTests AudioPatchTests
```

Expected: compilation/assertion failure because aggregate fields and formatter output do not exist.

- [ ] **Step 3: Implement callback-safe accumulation**

In `RenderAsioBlock`, switch on `block.silence_reason` exactly once and
saturating-add missing frames. Latch the first non-success mixer result with a
lock-free integer CAS. The historical `silence_substitutions` either increments
beside the selected reason or is computed from the four snapshot counters; it
must equal their sum.

Use the paired converter for both driver spans. Before latching a non-finite
conversion fault, increment `non_finite_output_blocks`; the existing failure
path still clears both driver buffers. For successful, nonsubstituted blocks,
accumulate clipped blocks/samples and atomically maximize the nonnegative float
bit pattern. If `all_zero`, increment exactly one active/no-active zero counter.

Carry all callback fields, including the previously dropped
`buffer_alternation_violations`, through `SnapshotCounters`. Keep all callback-
side state fixed-size and lock-free.

- [ ] **Step 4: Format only on the control thread**

Add expected period to the startup line from effective frames and 48 kHz.
Compute QPC-derived averages/maxima as `double` only in `asio_counters_text`.
Convert nanosecond driver fields to microseconds there. Format cumulative
counts and `maximum_absolute_output_sample` without issuing any new log line.

Retain the current sites:

```text
Audio startup ...
ASIO audio runtime summary ...          (every 30 seconds)
ASIO audio runtime failure ...          (fatal control-thread report)
ASIO audio runtime summary ...          (final shutdown snapshot)
```

- [ ] **Step 5: Run and verify GREEN**

```powershell
cmake --build --preset msvc32-debug --target AsioCallbackRuntimeTests AsioSampleConverterTests AudioRenderCoreTests AsioOutputBackendTests AudioPatchTests
ctest --test-dir build-msvc32-debug --output-on-failure -R '^(AsioCallbackRuntimeTests|AsioSampleConverterTests|AudioRenderCoreTests|AsioOutputBackendTests|AudioPatchTests)$'
```

Expected: all pass and no test observes an added per-callback log.

- [ ] **Step 6: Commit**

```powershell
git add -- src/Audio/Asio/AsioOutputBackend.h src/Audio/Asio/AsioOutputBackend.cpp src/Audio/Asio/AsioOutputBackendInternal.h src/Audio/AudioPatch.cpp tests/Audio/AsioOutputBackendTests.cpp tests/Audio/AudioPatchTests.cpp
git commit -m "Report actionable ASIO runtime diagnostics"
```

---

### Task 5: Verify both builds and prepare the real 192-frame capture

**Files:**
- Modify: `docs/reverse-engineering/asio-runtime-validation.md`

**Interfaces:**
- Consumes: complete diagnostics, prior 384/192 runtime logs, current build artifacts, and the operator-controlled deployment boundary.
- Produces: verified binaries plus an interpretation guide for one audible real-hardware run; it does not deploy or claim the symptom fixed.

- [ ] **Step 1: Run focused Debug tests**

```powershell
$env:GC_ASIO_SDK_DIR='H:\gc\artifacts\ASIOSDK'
cmake --preset msvc32-debug
cmake --build --preset msvc32-debug --target AudioRenderCoreTests MiniaudioMixerTests AsioCallbackRuntimeTests AsioSampleConverterTests AsioOutputBackendTests AudioPatchTests
ctest --test-dir build-msvc32-debug --output-on-failure -R '^(AudioRenderCoreTests|MiniaudioMixerTests|AsioCallbackRuntimeTests|AsioSampleConverterTests|AsioOutputBackendTests|AudioPatchTests)$'
```

Expected: every focused test passes.

- [ ] **Step 2: Run complete Debug and RelWithDebInfo verification**

```powershell
cmake --build --preset msvc32-debug
ctest --preset msvc32-debug -j 4
cmake --preset msvc32-release
cmake --build --preset msvc32-release
ctest --preset msvc32-release -j 4
```

Expected: both complete builds and suites pass; distribution and export tests remain green.

- [ ] **Step 3: Verify callback-code constraints and branch scope**

```powershell
git diff --check
git status --short
git diff --stat d214bdf..HEAD
```

Inspect the callback/render diffs and confirm that they contain no logging,
dynamic allocation, mutex, sleep, file/console I/O, or wait added to the normal
callback path. Confirm no runtime `H:\gc` file changed.

- [ ] **Step 4: Update the runtime validation guide with current evidence**

Record the already-observed baselines without calling them dropouts:

```text
384 frames: max callback work about 0.622 ms inside an 8 ms period;
            silence count 12 at 60 s, then startup/shutdown growth.
192 frames: max callback work about 0.482 ms inside a 4 ms period;
            silence count 26 at 30 s, then shutdown growth.
Both:       zero deadline/overload/change/gap/discontinuity counters.
```

Explain that the old total lacked cause and therefore could not prove in-song
missing audio. Add a field interpretation table matching the approved design:

- late/severe arrival with normal driver duration: delivery/scheduling;
- driver-period error/discontinuity: driver/device clock;
- high render work: GCLoader render path;
- active short/error/active-zero: mixer/source path;
- no-active silence during an expected sound: upstream voice-start/control;
- nonzero clean blocks during an audible gap: below GCLoader's callback boundary;
- clipping/non-finite: rendered sample integrity.

- [ ] **Step 5: Write the operator-only acceptance procedure**

Document, but do not execute or deploy, this sequence:

1. deploy the final Release `iDmacDrv32.dll` and matching ConfigGUI manually;
2. open the genuine driver panel, select its smallest supported setting, close
   it, and let ConfigGUI re-inspect;
3. keep exact `asio_buffer_frames = 192`, base pair `0`, and Save only after
   validation succeeds;
4. run for 60-90 seconds and note the approximate second of each missing/crackling event;
5. preserve startup, the surrounding 30-second summaries, and the final summary;
6. compare cumulative deltas and the new reason/timing/sample fields before
   selecting a code fix or a LatencyMon/ETW capture.

State explicitly that build tests cannot accept audible runtime behavior.

- [ ] **Step 6: Commit documentation**

```powershell
git add -- docs/reverse-engineering/asio-runtime-validation.md
git commit -m "Document ASIO runtime diagnostic interpretation"
```

Expected: the implementation is ready for operator deployment, but no claim of
crackle-free or lower-latency gameplay is made.

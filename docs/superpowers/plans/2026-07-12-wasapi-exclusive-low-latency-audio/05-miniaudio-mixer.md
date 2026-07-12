# No-Device Miniaudio Mixer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Mix all secondary-buffer voices into preallocated 44,100 Hz stereo float blocks without a miniaudio device, then convert those blocks to saturating PCM16 for WASAPI.

**Architecture:** Each voice owns a custom miniaudio data source backed by `AudioSnapshot`; the callback copies immutable source frames, self-manages loop/seek state, and publishes cursor spans. A no-device `ma_engine` performs PCM16/PCM24 conversion, mono duplication, mixing, and linear conversion only for 22.05/48 kHz sources.

**Tech Stack:** C++23, miniaudio 0.11.25 no-device engine, Tasks 02-04 types/storage/timeline, CTest.

## Global Constraints

- Engine output is 44,100 Hz, 2-channel float; endpoint conversion is float→interleaved PCM16 at the same rate.
- Set `noDevice = MA_TRUE`, `periodSizeInFrames = actual endpoint frames`, `defaultVolumeSmoothTimeInPCMFrames = 0`, and mono mode `duplicate`.
- Create sounds with `MA_SOUND_FLAG_NO_PITCH | MA_SOUND_FLAG_NO_SPATIALIZATION`.
- Native 44.1 kHz voices must bypass sample-rate conversion.
- Exceptional 22.05/48 kHz voices use miniaudio's linear converter.
- Render makes no allocator callback, snapshot clone, log call, or mutex wait.
- Source callbacks use only the snapshot view captured for that read.
- All allocations and node attachment/detachment occur on non-render threads.

---

## Prerequisites

- Plans 01-04 are committed.

## File Structure

- Create `MiniaudioMixer.h` / `MiniaudioMixer.cpp`.
- Create `tests/MiniaudioMixerTests.cpp`.
- Modify `CMakeLists.txt`.

### Task 1: Snapshot Data Source and Deterministic Voice Mixing

**Interfaces:**

```cpp
struct MixerDiagnosticsSnapshot {
    std::uint64_t native_rate_buffers{};
    std::uint64_t sample_format_converted_buffers{};
    std::uint64_t sample_rate_converted_buffers{};
    std::uint64_t native_gameplay_buffers{};
    std::uint32_t active_voices{};
    std::uint32_t maximum_simultaneous_voices{};
};

struct MixerRenderResult { ma_result result; std::uint64_t frames_read; };

enum class VoiceUsage : std::uint8_t {
    General,
    GameplayNativeCandidate,
};

class MixerVoice {
public:
    HRESULT Play(bool looping, std::uint64_t epoch) noexcept;
    void Stop() noexcept;
    HRESULT Seek(std::uint64_t source_frame, std::uint64_t epoch) noexcept;
    void SetGain(float) noexcept;
    bool playing() const noexcept;
    bool looping() const noexcept;
    bool at_end() const noexcept;
};

class MiniaudioMixer {
public:
    static std::unique_ptr<MiniaudioMixer> Create(
        std::uint32_t period_frames,
        const ma_allocation_callbacks*,
        ma_result*) noexcept;
    std::unique_ptr<MixerVoice> CreateVoice(
        const NormalizedSourceFormat&,
        AudioSnapshot&,
        AudioCursorTimeline&,
        VoiceUsage,
        ma_result*) noexcept;
    MixerRenderResult Render(
        std::span<float> stereo,
        std::uint64_t output_frame_begin) noexcept;
    MixerDiagnosticsSnapshot diagnostics() const noexcept;
};

void ConvertFloatToPcm16(
    std::span<const float>,
    std::span<std::int16_t>) noexcept;
```

- [ ] **Step 1: Write failing mixer tests with allocation callbacks**

Create `tests/MiniaudioMixerTests.cpp`. Provide `ma_allocation_callbacks` whose malloc/realloc/free functions increment a probe only while `probe.enabled` is true.

Use synthetic snapshots to assert:

1. Four mono PCM16 frames `{16384,-16384,8192,-8192}` render to equal L/R and first sample approximately `0.5`.
2. `SetGain(0.5F)` applies on the next block with no ramp.
3. Two stereo voices at `0.75` mix above `1.0F`; `ConvertFloatToPcm16` clips them to `32767`.
4. A `-1.0F` sample converts to `-32768`.
5. Native PCM24 renders without a sample-rate conversion count.
6. 22.05 kHz and 48 kHz sources produce monotonic linear-conversion samples and increment only the sample-rate-converted count.
7. A looping four-frame source publishes a span whose unwrapped end crosses the source length.
8. Seek changes the published epoch, resets the source position, and clears exceptional-rate converter history so the first post-seek samples do not contain pre-seek state.
9. After one warm render, enable the allocator probe around `Render`; the callback count remains zero.

Pass `GameplayNativeCandidate` for the mono native PCM16 and two stereo native PCM16 voices, and `General` for PCM24 and both exceptional-rate voices. Use this exact diagnostics expectation:

```cpp
const auto diagnostics = mixer->diagnostics();
failures += expect(diagnostics.native_rate_buffers == 4, "native rate count");
failures += expect(
    diagnostics.sample_format_converted_buffers == 6,
    "integer to float count");
failures += expect(
    diagnostics.sample_rate_converted_buffers == 2,
    "exceptional rate count");
failures += expect(
    diagnostics.native_gameplay_buffers == 3,
    "44.1 kHz PCM16 gameplay count");
```

- [ ] **Step 2: Register and verify red**

```cmake
add_executable(MiniaudioMixerTests
        WasapiAudioTypes.cpp
        AudioSnapshot.cpp
        AudioCursorTimeline.cpp
        MiniaudioMixer.cpp
        tests/MiniaudioMixerTests.cpp
)
target_include_directories(MiniaudioMixerTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(MiniaudioMixerTests PRIVATE miniaudio)
add_test(NAME MiniaudioMixerTests COMMAND MiniaudioMixerTests)
```

Append `MiniaudioMixer.cpp` to `SOURCES`. Reconfigure and build; expect missing `MiniaudioMixer.h`.

- [ ] **Step 3: Implement the custom data-source state**

Use one namespace-scope implementation struct whose first member is `ma_data_source_base`:

```cpp
struct SnapshotDataSource {
    ma_data_source_base base{};
    NormalizedSourceFormat format{};
    AudioSnapshot* snapshot{};
    AudioCursorTimeline* timeline{};
    std::atomic_uint64_t cursor{};
    std::atomic_uint64_t epoch{1};
    std::atomic_bool looping{};
    std::atomic_bool ended{};
    std::uint64_t unwrapped_cursor{};  // render thread only
    std::uint64_t last_render_id{};    // render thread only
    std::uint64_t render_output_offset{};
};
```

The vtable implements read, seek, format, cursor, length, and looping; set `MA_DATA_SOURCE_SELF_MANAGED_RANGE_AND_LOOP_POINT`.

- [ ] **Step 4: Implement bounded snapshot reads and span publication**

The read callback must:

```cpp
const auto view = source.snapshot->AcquireForRender();
while (*frames_read < requested_frames) {
    if (position == source_length) {
        if (!source.looping.load(std::memory_order_relaxed)) {
            reached_end = true;
            break;
        }
        position = 0;
        wrapped = true;
    }
    const auto chunk = std::min<std::uint64_t>(
        source_length - position,
        requested_frames - *frames_read);
    std::memcpy(
        destination + *frames_read * source.format.block_align,
        view.bytes().data() + position * source.format.block_align,
        static_cast<std::size_t>(chunk * source.format.block_align));
    position += chunk;
    *frames_read += chunk;
    source.unwrapped_cursor += chunk;
}
```

Within the current render context, convert consumed source frames to represented output frames with ceiling division, clamp to the remaining output block, and publish `AudioRenderSpan`. Return `MA_AT_END` only after the final nonlooping data has been exposed; otherwise return `MA_SUCCESS`.

The seek callback rejects `frame >= length`, then resets cursor, unwrapped cursor, `ended`, render id, and render offset.

- [ ] **Step 5: Initialize the no-device engine and voices**

Use:

```cpp
auto config = ma_engine_config_init();
config.noDevice = MA_TRUE;
config.channels = kOutputChannels;
config.sampleRate = kOutputSampleRate;
config.periodSizeInFrames = period_frames;
config.defaultVolumeSmoothTimeInPCMFrames = 0;
config.monoExpansionMode = ma_mono_expansion_mode_duplicate;
if (callbacks != nullptr) {
    config.allocationCallbacks = *callbacks;
}
```

Initialize a voice with `ma_data_source_init`, then `ma_sound_config_init_2`, the two no-pitch/no-spatialization flags, zero smoothing, and an end callback that changes only atomics/counters.

`Play`, `Stop`, `Seek`, and `SetGain` call only miniaudio's atomic/synchronized control APIs and maintain `active_voices` without underflow.

`CreateVoice` increments `native_gameplay_buffers` only when `usage == VoiceUsage::GameplayNativeCandidate` and `format.native_rate_pcm16` is true. This count is a format/use-pattern diagnostic candidate; filenames are unavailable and Plan 11 remains authoritative.

- [ ] **Step 6: Implement fixed-block render and saturation**

Reject a span whose sample count is not `period_frames * 2`. Set a thread-local render context, call `ma_engine_read_pcm_frames`, clear the context, and zero any short remainder.

Use exact saturation:

```cpp
const auto sample = std::clamp(input[index], -1.0F, 1.0F);
output[index] = sample <= -1.0F
    ? static_cast<std::int16_t>(-32768)
    : static_cast<std::int16_t>(std::lround(sample * 32767.0F));
```

- [ ] **Step 7: Verify focused behavior**

```powershell
& $env:ComSpec /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build build-msvc32-latest --target MiniaudioMixerTests iDmacDrv32 && ctest --test-dir build-msvc32-latest -R "^MiniaudioMixerTests$" --output-on-failure'
```

Expected: mixing, conversion, loop/seek spans, diagnostics, clipping, and zero steady-state allocator calls pass.

- [ ] **Step 8: Commit**

```powershell
git add -- CMakeLists.txt MiniaudioMixer.h MiniaudioMixer.cpp tests/MiniaudioMixerTests.cpp
git commit -m "feat: add no-device low-latency audio mixer"
```

## Completion Gate

The test must distinguish sample-format conversion from sample-rate conversion. A gameplay-native buffer can increment the former but never the latter.

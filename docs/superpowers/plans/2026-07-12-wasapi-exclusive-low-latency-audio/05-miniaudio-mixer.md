# No-Device Miniaudio Mixer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Mix all secondary-buffer voices into preallocated 44,100 Hz stereo float blocks without a miniaudio device, then convert those blocks to saturating PCM16 for WASAPI.

**Architecture:** Each voice owns a custom zero-input/one-output miniaudio node attached to a no-device engine endpoint plus a preinitialized `ma_data_converter`. Mixer and voices share the engine-state owner, while each voice retains shared snapshot/timeline owners. The node callback uses raw pointers from those stable owners, captures one immutable `AudioSnapshot` view, fills fixed input scratch, converts PCM16/PCM24 mono/stereo to 44.1 kHz stereo float, and publishes epoch-cumulative cursor spans. Native-rate voices take miniaudio's format/channels-only path; only 22.05/48 kHz sources use the linear resampler.

**Tech Stack:** C++23, miniaudio 0.11.25 no-device engine, Tasks 02-04 types/storage/timeline, CTest.

## Global Constraints

- Engine output is 44,100 Hz, 2-channel float; endpoint conversion is float→interleaved PCM16 at the same rate.
- Set `noDevice = MA_TRUE`, `periodSizeInFrames = actual endpoint frames`, `defaultVolumeSmoothTimeInPCMFrames = 0`, and mono mode `duplicate`.
- Do not create `ma_sound` objects or access their fields; voices are project custom `ma_node` objects.
- Native 44.1 kHz voices must bypass sample-rate conversion.
- Exceptional 22.05/48 kHz voices use miniaudio's linear converter.
- Render makes no allocator callback, snapshot clone, log call, or mutex wait.
- Node callbacks use one local lexical-scope, nonmoving snapshot view per process call.
- Converter/node heaps and fixed input scratch are allocated off-render. Size scratch from `ma_data_converter_get_required_input_frame_count(period_frames)` plus reported input latency so every linear-resampler phase fits.
- All node initialization, attachment, detachment, and uninitialization occur on non-render threads.
- Render accepts exactly one configured period so the graph and converter never retain an unreported short-block surplus.
- Seek uses a latest-wins mailbox with mutex-serialized control writers and a seq_cst atomic sequence/payload publication. Render never locks; an in-progress or incoherent publication makes that voice output silence for the block.
- Before producing a new seek epoch, the render callback resets cursor/unwrapped/end/span context and calls `ma_data_converter_reset()`. The pinned linear resampler's bounded cleared-zero startup transient is expected; no pre-seek sample may survive it.
- `MiniaudioMixer` and every voice share ownership of engine state. Voices also own `shared_ptr<AudioSnapshot>` and `shared_ptr<AudioCursorTimeline>`; caller release order cannot invalidate render or teardown.
- The render callback may use `.get()` from stable state-held shared owners but must never copy, move, reset, acquire, or release a `shared_ptr`.
- Cursor spans derive source positions from the overflow-safe epoch-cumulative mapping `epoch_source_start + floor(total_output_frames * source_rate / 44100)`. Publish bounded rational segments where one span cannot represent every output-frame phase exactly.
- Active accounting is controlled by a lock-free per-voice transitional state machine whose phase and monotonic playback-run token share one packed atomic. Control writers may serialize/wait off-render; natural end CASes the exact `Playing(run)` captured at callback entry, so a stale render cannot end a newer run. Render never waits or takes the control mutex.
- Logical mixer activity and hardware audibility are distinct at nonlooping end-of-source. After publishing the final span, a voice exposes its half-open queued output end through a lock-free observable before logical `playing()` becomes false. The active-voice counter still drops at logical end; it is not held open for endpoint drain. Looped voices never publish a terminal drain boundary.

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
    std::optional<std::uint64_t>
        audible_until_output_frame() const noexcept;
};

class MiniaudioMixer {
public:
    static std::unique_ptr<MiniaudioMixer> Create(
        std::uint32_t period_frames,
        const ma_allocation_callbacks*,
        ma_result*) noexcept;
    std::unique_ptr<MixerVoice> CreateVoice(
        const NormalizedSourceFormat&,
        std::shared_ptr<AudioSnapshot>,
        std::shared_ptr<AudioCursorTimeline>,
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
6. 22.05 kHz and 48 kHz sources produce the pinned linear converter's bounded reset transient followed by monotonic current-region samples, continue correctly across a second block, and increment only the sample-rate-converted count.
7. A looping four-frame source publishes a span whose unwrapped end crosses the source length.
8. For both exceptional rates, cover stopped seek then `Play`, seek-and-play reset, resynchronization while playing, and multiple seeks before one render. The latest request wins, the span starts at the requested source position/epoch, and the first post-seek block contains only the reset transient plus samples from the new region—never pre-seek converter state.
9. After one warm render, enable the allocator probe around `Render`; the callback count remains zero.
10. Destroy the public mixer before its voice, then stop/destroy the voice safely and prove all callback allocations are released exactly once by the last shared engine owner.
11. Drop caller snapshot/timeline owners, render and resolve the timeline through the voice-held owners, then prove both owners expire after voice destruction. No shared ownership operation occurs in render.
12. Assert exact multi-block cumulative phase (`22.05 kHz: 412→6, 415→7`; `48 kHz: 512→13, 515→16`) and stress concurrent `Play`/`Stop`/natural-end so a single voice never reports active/max above one and finishes stopped.
13. Capture an old render's playback-run token, stop and start a new run, then prove the stale token cannot win natural end or decrement the new run's active count.
14. A nonlooping source that ends in one period publishes its exact final queued output end even though `playing()` and `active_voices` become false/zero; replay and seek clear that boundary, explicit stop clears it, and a looped voice never publishes one.

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

- [ ] **Step 3: Implement the custom node, converter, and seek-mailbox state**

Use a namespace-scope node wrapper whose first member is `ma_node_base` and whose project state owns the converter and fixed scratch:

```cpp
struct VoiceNode {
    ma_node_base base{};
    MixerVoiceState* state{};
};

struct MixerVoiceState {
    VoiceNode node{};
    ma_data_converter converter{};
    std::shared_ptr<MiniaudioMixerState> mixer;
    NormalizedSourceFormat format{};
    std::shared_ptr<AudioSnapshot> snapshot;
    std::shared_ptr<AudioCursorTimeline> timeline;
    std::vector<std::byte> input_scratch;
    std::atomic_uint64_t cursor{};
    std::atomic_bool looping{};
    std::atomic_bool ended{};
    std::atomic_uint64_t audible_until_output_frame{};
    detail::VoicePlaybackStateMachine playback; // packed phase + run token
    std::mutex control_mutex; // control threads only; render never locks it
    // Mutex-serialized writers; seq_cst sequence/frame/epoch payload.
    SeekMailbox seek_mailbox{};
    std::uint64_t epoch_source_start{}; // render thread only
    std::uint64_t epoch_output_frames{}; // render thread only
    std::uint64_t last_render_id{};    // render thread only
    std::uint64_t render_output_offset{};
};
```

The node vtable has zero input buses and one stereo output bus. Initialize a linear `ma_data_converter` from the accepted source format/rate/channels to f32 stereo 44.1 kHz. Matching rates must report no resampler; exceptional rates use the stock linear resampler with no LPF. Allocate converter heap and scratch before attachment. Scratch capacity is the queried period requirement plus converter input latency, which covers phase-dependent 48 kHz requirements after the first block.

`MiniaudioMixer` stores `shared_ptr<MiniaudioMixerState>` and copies that owner into each voice. `CreateVoice` accepts and stores shared snapshot/timeline owners. The callback takes raw pointers with `.get()` only while the state-held shared owners remain unchanged. All last-owner destruction remains off-render. Plan 06 must allocate buffers/timelines as shared owners and pass them directly to this API.

- [ ] **Step 4: Implement bounded node processing, seek reset, and span publication**

At the start of node processing, read the seek mailbox with a stable seq_cst sequence/payload/sequence snapshot. If the sequence is odd or changed, emit silence without advancing the voice. For a new stable version, reset source cursor, epoch source start, cumulative represented output count, ended/render-span state, and `ma_data_converter_reset()` before reading any new-epoch frame.

The node callback acquires exactly one local nonmoving view, copies at most the converter's required input frames into fixed scratch, and never retains the view:

```cpp
const auto view = source.snapshot->AcquireForRender();
while (copied < required_input_frames) {
    if (position == source_length) {
        if (!source.looping.load(std::memory_order_relaxed)) {
            break;
        }
        position = 0;
        wrapped = true;
    }
    const auto chunk = std::min<std::uint64_t>(
        source_length - position,
        required_input_frames - copied);
    std::memcpy(
        input_scratch + copied * source.format.block_align,
        view.bytes().data() + position * source.format.block_align,
        static_cast<std::size_t>(chunk * source.format.block_align));
    position += chunk;
    copied += chunk;
}
```

Call `ma_data_converter_process_pcm_frames`, advance the physical input cursor only by frames actually consumed, and zero any short output remainder. Independently derive every published unwrapped source position from the overflow-safe cumulative rational mapping `epoch_source_start + floor(total_output_frames * source_rate / 44100)`. Split the block into a bounded number of exact rational segments when one `AudioRenderSpan` interpolation would lose phase. Clamp to the remaining fixed output block. Mark a nonlooping voice ended only after its final source frame has been exposed. When final span publication succeeds, store its exclusive output end in the lock-free audible-until observable before completing the logical-playing transition; do not publish this boundary for loops. `Play`, replay, and accepted `Seek` clear the prior boundary, and explicit `Stop` clears it after its caller has had an opportunity to sample the hardware cursor. Reject control seeks where `frame >= length` before mailbox publication.

- [ ] **Step 5: Initialize the no-device engine and attach voices**

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

Initialize each voice's converter, fixed scratch, and stopped custom node off-render. Attach the node's single stereo output bus to `ma_node_graph_get_endpoint(ma_engine_get_node_graph(&engine))`. Voice destruction detaches and uninitializes the node/converter, then releases source and engine shared owners; engine teardown happens exactly once after the final mixer/voice owner.

`Play`/`Stop` use supported atomic node-state APIs, and `SetGain` uses `ma_node_set_output_bus_volume`. `Seek` only validates and publishes to the coherent mailbox. Serialize control transitions outside render and use lock-free `Stopped/Starting/Playing/Stopping/Ending/Ended` states packed with a monotonic playback-run token. Only the transition owner updates the global active count. The callback captures its run token on entry, and natural end must win an exact `Playing(run)→Ending(run)` CAS before publishing the terminal boundary and decrementing; stop/end cannot double-decrement, race a delayed start increment, or let an old callback end a replayed voice. The short `Ending` transition remains logically playing until the boundary is visible; endpoint drain after `Ended` does not keep the mixer active counter elevated.

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

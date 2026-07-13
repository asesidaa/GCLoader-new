# Exclusive Audio Engine Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Run the no-device mixer continuously into the strict WASAPI endpoint, expose endpoint-clock services to DirectSound buffers, and hand runtime failures to a non-real-time monitor.

**Architecture:** A process-lifetime `ExclusiveAudioEngine` owns one audio thread, one monitor thread, the endpoint, mixer, preallocated float/PCM16 blocks, clock mapper, atomic counters, initialization event, and fatal event. An observer performs all formatting/logging/UI/termination outside the render thread.

**Tech Stack:** C++23 threads/atomics, Win32 events, Plans 02-08, miniaudio, WASAPI, CTest.

## Global Constraints

- The first caller waits at most 10,000 ms for initialization.
- Endpoint and mixer initialization occur on the audio thread, outside loader lock.
- Allocate both render vectors after receiving actual endpoint frames and before `WasapiEndpoint::Start`.
- Keep the endpoint running and submit silence with zero active voices.
- Render callbacks do not allocate/free, log, show UI, initialize COM, or wait on a game-thread mutex.
- A late wake or mixer short read increments counters and submits silence; it is not immediately fatal.
- Endpoint API failure records the first stage/HRESULT atomically, increments endpoint failures, signals fatal control, and exits the render loop.
- Before any audio-thread exit after endpoint creation, call `WasapiEndpoint::ShutdownOnInitializingThread()` and reset the endpoint on that same thread. This applies to startup failure, runtime failure/exit, and explicit test shutdown.
- Monitor thread alone emits periodic summaries and runtime fatal reports.
- No endpoint rebuild, fallback, or default-device follow occurs.
- Normal successful production operation remains process-lifetime; releasing DirectSound facades and process detach do not initiate teardown.

---

## Prerequisites

- Plans 01-08 are committed.

## File Structure

- Create `ExclusiveAudioEngine.h` / `ExclusiveAudioEngine.cpp`.
- Create `tests/ExclusiveAudioEngineTests.cpp`.
- Modify `CMakeLists.txt`.

### Task 1: Render Loop, Clock Services, Counters, and Fatal Handoff

**Interfaces:**

```cpp
struct AudioRuntimeCountersSnapshot {
    std::uint64_t render_callbacks{};
    std::uint64_t late_event_wakes{};
    std::uint64_t silence_fallbacks{};
    std::uint64_t cursor_timeline_failures{};
    std::uint64_t endpoint_hresult_failures{};
    MixerDiagnosticsSnapshot mixer{};
};

class IAudioEngineObserver {
public:
    virtual ~IAudioEngineObserver() = default;
    virtual void StartupSucceeded(
        const EndpointInitialization&) noexcept = 0;
    virtual void RuntimeSummary(
        const AudioRuntimeCountersSnapshot&) noexcept = 0;
    virtual void RuntimeFailed(
        const AudioFailure&,
        const AudioRuntimeCountersSnapshot&) noexcept = 0;
};

class ExclusiveAudioEngine final : public IAudioEngineServices {
public:
    static std::unique_ptr<ExclusiveAudioEngine> StartAndWait(
        std::unique_ptr<IWasapiApi>,
        std::shared_ptr<IAudioEngineObserver>,
        DWORD timeout_ms,
        const ma_allocation_callbacks* mixer_allocations,
        AudioStartupFailure*) noexcept;
    std::unique_ptr<MixerVoice> CreateVoice(
        const NormalizedSourceFormat&, std::shared_ptr<AudioSnapshot>,
        std::shared_ptr<AudioCursorTimeline>, VoiceUsage,
        ma_result*) noexcept override;
    std::optional<std::uint64_t>
        CurrentOutputFrame() noexcept override;
    std::uint32_t endpoint_buffer_frames() const noexcept override;
    void CountCursorTimelineFailure() noexcept override;
};
```

- [ ] **Step 1: Write failing engine tests with fake endpoint/observer**

Create `tests/ExclusiveAudioEngineTests.cpp`. Its fake implements the complete `IWasapiApi` surface: `InitializeComMta`, `OpenDefaultConsoleEndpoint`, `ActivateAudioClient`, `IsExactFormatSupported`, `GetDevicePeriod`, `InitializeExclusiveEvent`, `GetBufferSize`, `ReleaseAudioClient`, `CreateRenderEvent`, `SetEventHandle`, `GetRenderService`, `GetClockService`, `GetClockFrequency`, `GetRenderBuffer`, `ReleaseRenderBuffer`, `RegisterMmcssProAudio`, `SetMmcssCriticalPriority`, `Start`, `WaitForRender`, `GetClockPosition`, and `ShutdownOnInitializingThread`. It stores calls in a fixed-capacity array, returns endpoint name/ID and a frame count derived from the configured period, makes `WaitForRender` consume a controlled fixed queue, writes render buffers into preallocated storage, and returns controlled clock position/QPC values. It exposes submitted PCM blocks to assertions without allocating while the render-allocation probe is enabled.

Assert:

1. endpoint initialization that never signals readiness times out at the supplied short test timeout;
2. endpoint initialization failure returns its exact `AudioStartupFailure`, including the endpoint name/ID learned before failure, and no engine;
3. successful initialization creates mixer/render storage before the fake observes `Start`;
4. three render events with no voices submit three all-zero PCM16 blocks;
5. a snapshot-backed voice renders nonzero samples and advances the submitted-frame timeline by actual endpoint frames;
6. `CurrentOutputFrame` maps fake `IAudioClock` position/frequency through `EndpointClockMapper`;
7. a clock/timeline read failure increments only `cursor_timeline_failures`;
8. a QPC delta greater than 1.5 actual periods increments `late_event_wakes`;
9. a forced mixer short read zero-fills the remainder and increments `silence_fallbacks`;
10. fake `GetRenderBuffer`, `ReleaseRenderBuffer`, wait, and clock failures record the exact first stage/HRESULT;
11. `RuntimeFailed` runs on the monitor thread, never the recorded audio-thread ID;
12. a periodic summary includes render, mixer classification, maximum voice, late, silence, cursor, and endpoint counts;
13. an allocator probe around two warmed render events records zero allocation/free calls, and the observer receives no callback during those events.
14. startup failure after endpoint creation, runtime-loop failure/exit, and explicit test shutdown each invoke endpoint shutdown/reset exactly once on the recorded audio-thread ID before that thread exits; no monitor/caller-thread shutdown is accepted.

- [ ] **Step 2: Register and verify red**

```cmake
add_executable(ExclusiveAudioEngineTests
        WasapiAudioTypes.cpp AudioSnapshot.cpp AudioCursorTimeline.cpp
        MiniaudioMixer.cpp DirectSoundFacade.cpp WasapiEndpoint.cpp
        ExclusiveAudioEngine.cpp tests/ExclusiveAudioEngineTests.cpp
)
target_include_directories(ExclusiveAudioEngineTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ExclusiveAudioEngineTests PRIVATE
        miniaudio dsound dxguid ole32 uuid avrt propsys)
add_test(NAME ExclusiveAudioEngineTests COMMAND ExclusiveAudioEngineTests)
```

Append `ExclusiveAudioEngine.cpp` to `SOURCES`; expect missing declarations.

- [ ] **Step 3: Define state and bounded startup handshake**

The engine owns:

```cpp
std::unique_ptr<IWasapiApi> pending_api_;
std::unique_ptr<WasapiEndpoint> endpoint_;
std::unique_ptr<MiniaudioMixer> mixer_;
const ma_allocation_callbacks* mixer_allocations_{};
std::vector<float> float_mix_;
std::vector<std::int16_t> pcm16_mix_;
EndpointClockMapper clock_mapper_;
std::thread audio_thread_;
std::thread monitor_thread_;
HANDLE initialization_event_{};
HANDLE fatal_event_{};
std::atomic_bool initialization_succeeded_{};
std::atomic_bool monitor_exit_{};
std::atomic_uint64_t submitted_frames_{};
```

Create both events before starting threads. `StartAndWait` waits `timeout_ms`; `WAIT_TIMEOUT` returns `AudioFailureStage::InitializationTimeout` and no usable engine. Under the fail-fast startup contract, the timed-out internal object is intentionally abandoned to process cleanup instead of blocking indefinitely on a COM call; the caller immediately invokes fatal startup reporting.

The audio thread performs:

```cpp
AudioFailure failure{};
EndpointInitialization attempted{};
ma_result mixer_result = MA_ERROR;
endpoint_ = WasapiEndpoint::Create(
    std::move(pending_api_), &attempted, &failure);
if (!endpoint_) {
    signal_init_failure({failure, attempted});
    return;
}
const auto frames = endpoint_->initialization().actual_buffer_frames;
mixer_ = MiniaudioMixer::Create(
    frames, mixer_allocations_, &mixer_result);
if (!mixer_) {
    failure = {AudioFailureStage::InitializeMixer, E_OUTOFMEMORY};
    endpoint_->ShutdownOnInitializingThread();
    endpoint_.reset();
    signal_init_failure({failure, attempted});
    return;
}
float_mix_.resize(static_cast<std::size_t>(frames) * 2);
pcm16_mix_.resize(static_cast<std::size_t>(frames) * 2);
if (FAILED(endpoint_->Start(&failure))) {
    const auto failed_initialization = endpoint_->initialization();
    endpoint_->ShutdownOnInitializingThread();
    endpoint_.reset();
    signal_init_failure({failure, failed_initialization});
    return;
}
```

Read initial clock position/frequency, reset the mapper to output frame zero, store success, signal initialization, then enter render.

Any failure after endpoint creation, including initial clock setup, first captures the attempted metadata, then calls `ShutdownOnInitializingThread()` and resets `endpoint_` before signaling startup failure and returning from the audio thread. A failure inside `WasapiEndpoint::Create` is already cleaned by its same-thread destructor fallback. The bounded caller-timeout path remains deliberate fail-fast abandonment because the audio thread may be stuck inside a system call; it must never destroy or release the endpoint from the waiting caller thread.

Wrap mixer creation and both vector resizes in `try/catch (const std::bad_alloc&)`; report `InitializeMixer/E_OUTOFMEMORY` with the attempted endpoint metadata. No allocation exception may escape the audio-thread entry point.

- [ ] **Step 4: Implement the steady-state render cycle**

For each event:

```cpp
AudioFailure failure{};
if (FAILED(endpoint_->WaitForRender(2000, &failure))) {
    RecordRuntimeFailure(failure);
    break;
}

EndpointClockPosition clock{};
if (FAILED(endpoint_->ReadClock(&clock, &failure))) {
    endpoint_->TrySubmitSilence();
    RecordRuntimeFailure(failure);
    break;
}
CountLateWake(clock.qpc_100ns);

const auto begin = submitted_frames_.load(std::memory_order_relaxed);
const auto rendered = mixer_->Render(float_mix_, begin);
if (rendered.result != MA_SUCCESS) {
    std::fill(float_mix_.begin(), float_mix_.end(), 0.0F);
    silence_fallbacks_.fetch_add(1, std::memory_order_relaxed);
} else if (rendered.frames_read != frames) {
    const auto bounded_frames = std::min<std::uint64_t>(
        rendered.frames_read, frames);
    const auto first_missing = static_cast<std::size_t>(bounded_frames) * 2;
    std::fill(float_mix_.begin() + first_missing, float_mix_.end(), 0.0F);
    silence_fallbacks_.fetch_add(1, std::memory_order_relaxed);
}
ConvertFloatToPcm16(float_mix_, pcm16_mix_);
if (FAILED(endpoint_->SubmitPcm16(pcm16_mix_, &failure))) {
    RecordRuntimeFailure(failure);
    break;
}
submitted_frames_.fetch_add(frames, std::memory_order_release);
render_callbacks_.fetch_add(1, std::memory_order_relaxed);
```

After leaving the render loop for any reason, the audio thread calls `endpoint_->ShutdownOnInitializingThread()` and resets `endpoint_` before it returns. The same sequence is used for an explicit test-shutdown request. The engine destructor/test harness signals the audio thread, waits for that owner-thread cleanup to complete, and only then joins it; neither the monitor thread nor the controlling caller resets the endpoint.

No function reachable from this block may include plog or allocate.

- [ ] **Step 5: Implement late detection and atomic first-failure recording**

Use clock QPC values already expressed in 100-ns units. Count a late wake when:

```cpp
qpc_delta > actual_period_100ns + actual_period_100ns / 2
```

Store failure stage and HRESULT in separate atomics, but publish the stage last with release ordering. Only the first `None → failing stage` transition wins. Then increment endpoint failures and call `SetEvent(fatal_event_)`.

- [ ] **Step 6: Implement monitor-only reporting**

The monitor waits on `fatal_event_` with a 30,000 ms timeout:

- timeout: snapshot counters and call `RuntimeSummary`;
- fatal event: acquire-load stage/HRESULT, snapshot counters, call `RuntimeFailed`, then exit;
- test/process shutdown event: exit without a fatal report.

The test-shutdown event only requests audio-loop exit. It does not own endpoint cleanup. The audio thread performs shutdown/reset before publishing its exited state; the test harness joins only after that state is visible. Normal production success does not signal this path because the engine and endpoint are process-lifetime.

`StartupSucceeded` is called by the non-render caller after the initialization event, not by the audio thread.

- [ ] **Step 7: Implement `IAudioEngineServices`**

- `CreateVoice` accepts shared snapshot/timeline owners and forwards those owners, the format, `VoiceUsage`, and result pointer to initialized `mixer_`. Voice state retains the owners; the render path does not copy `shared_ptr` values.
- `endpoint_buffer_frames` returns authoritative endpoint frames.
- `CountCursorTimelineFailure` atomically increments the counter.
- `CurrentOutputFrame` calls `endpoint_->ReadClock` and maps through `clock_mapper_`. A failed endpoint HRESULT calls `RecordRuntimeFailure` before returning `nullopt`; an unmappable but successful clock sample returns `nullopt` without an endpoint failure. The buffer caller then increments the cursor-timeline counter. It never logs.

- [ ] **Step 8: Verify**

```powershell
& $env:ComSpec /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build build-msvc32-latest --target ExclusiveAudioEngineTests iDmacDrv32 && ctest --test-dir build-msvc32-latest -R "^ExclusiveAudioEngineTests$" --output-on-failure'
```

- [ ] **Step 9: Commit**

```powershell
git add -- CMakeLists.txt ExclusiveAudioEngine.h ExclusiveAudioEngine.cpp tests/ExclusiveAudioEngineTests.cpp
git commit -m "feat: run exclusive low-latency audio engine"
```

## Completion Gate

The engine test must prove observer callbacks are absent from the warmed render scope. A logger-free source file alone is not sufficient evidence if callbacks can still escape to logging code.

# Strict WASAPI Exclusive Endpoint Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Initialize and service the startup default console endpoint in exact 44,100 Hz stereo PCM16 event-driven exclusive mode at its driver-aligned minimum period.

**Architecture:** `WasapiEndpoint` owns policy and error staging over a mockable `IWasapiApi`. `Win32WasapiApi` owns the real MMDevice/Core Audio COM objects, render event, and MMCSS handle; tests drive the policy without opening hardware.

**Tech Stack:** Windows 10+ MMDevice, `IAudioClient`, `IAudioRenderClient`, `IAudioClock`, MMCSS/AVRT, WRL `ComPtr`, C++23, CTest.

## Global Constraints

- Select `eRender`/`eConsole` once; never follow later default-device changes.
- Call `IsFormatSupported` only for exact 44,100 Hz stereo PCM16 exclusive mode; no closest format and no fallback.
- Request minimum `GetDevicePeriod` as both buffer duration and periodicity with `AUDCLNT_STREAMFLAGS_EVENTCALLBACK`.
- On `AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED`, call `GetBufferSize`, release the failed client, activate a new client, recalculate duration, and retry once.
- After retry, actual frames must equal the alignment frame count. Without retry, actual frames must not exceed `ReferenceTimeToFramesCeil(minimum_period, 44100)`.
- A successful aligned-size query, actual-size query, or clock-frequency query that returns zero is invalid at that query's existing failure stage. Zero aligned/actual frames use `AUDCLNT_E_BUFFER_SIZE_ERROR`; zero frequency uses `E_UNEXPECTED`.
- Register the event before `Start`, prefill the complete endpoint buffer with silence, and require render/clock services.
- Require MMCSS task `Pro Audio` and `AVRT_PRIORITY_CRITICAL`; failure is fatal.
- Runtime buffer/clock methods allocate nothing and log nothing.
- Core Audio/MMCSS teardown is owner-thread-only. The explicit shutdown surface is idempotent; an off-thread call returns `RPC_E_WRONG_THREAD` without releasing COM interfaces or reverting MMCSS.

---

## Prerequisites

- Plans 01-02 are mandatory; normal execution also has Plans 03-07 committed.

## File Structure

- Create `WasapiEndpoint.h` / `WasapiEndpoint.cpp`.
- Create `tests/WasapiEndpointTests.cpp`.
- Modify `CMakeLists.txt` to link Windows audio support libraries.

### Task 1: Mockable Strict Initialization Policy

**Interfaces:**

```cpp
enum class AudioFailureStage : std::uint32_t {
    None,
    InitializationTimeout,
    InitializeMixer,
    CoInitialize,
    OpenDefaultEndpoint,
    ActivateAudioClient,
    IsFormatSupported,
    GetDevicePeriod,
    InitializeExclusive,
    GetAlignedBufferSize,
    ReactivateAudioClient,
    RetryInitializeExclusive,
    GetActualBufferSize,
    CreateRenderEvent,
    SetEventHandle,
    GetRenderService,
    GetClockService,
    GetClockFrequency,
    PrefillGetBuffer,
    PrefillReleaseBuffer,
    RegisterMmcss,
    SetMmcssPriority,
    StartEndpoint,
    WaitRenderEvent,
    GetRenderBuffer,
    ReleaseRenderBuffer,
    GetClockPosition,
};

struct AudioFailure {
    AudioFailureStage stage{AudioFailureStage::None};
    HRESULT result{S_OK};
};

struct EndpointInitialization {
    std::wstring endpoint_name;
    std::wstring endpoint_id;
    REFERENCE_TIME default_period{};
    REFERENCE_TIME minimum_period{};
    REFERENCE_TIME requested_duration{};
    std::uint32_t actual_buffer_frames{};
    std::uint64_t clock_frequency{};
    bool alignment_retry{};
};

struct EndpointClockPosition {
    std::uint64_t position{};
    std::uint64_t qpc_100ns{};
};

struct AudioStartupFailure {
    AudioFailure failure{};
    EndpointInitialization attempted{};
};
```

`IWasapiApi` exposes exact steps rather than one opaque initialize call:

```cpp
class IWasapiApi {
public:
    virtual ~IWasapiApi() = default;
    virtual HRESULT InitializeComMta() noexcept = 0;
    virtual HRESULT OpenDefaultConsoleEndpoint(
        std::wstring*, std::wstring*) noexcept = 0;
    virtual HRESULT ActivateAudioClient() noexcept = 0;
    virtual HRESULT IsExactFormatSupported(
        const WAVEFORMATEX&) noexcept = 0;
    virtual HRESULT GetDevicePeriod(
        REFERENCE_TIME*, REFERENCE_TIME*) noexcept = 0;
    virtual HRESULT InitializeExclusiveEvent(
        REFERENCE_TIME, REFERENCE_TIME,
        const WAVEFORMATEX&) noexcept = 0;
    virtual HRESULT GetBufferSize(std::uint32_t*) noexcept = 0;
    virtual void ReleaseAudioClient() noexcept = 0;
    virtual HRESULT CreateRenderEvent() noexcept = 0;
    virtual HRESULT SetEventHandle() noexcept = 0;
    virtual HRESULT GetRenderService() noexcept = 0;
    virtual HRESULT GetClockService() noexcept = 0;
    virtual HRESULT GetClockFrequency(std::uint64_t*) noexcept = 0;
    virtual HRESULT GetRenderBuffer(std::uint32_t, BYTE**) noexcept = 0;
    virtual HRESULT ReleaseRenderBuffer(std::uint32_t, DWORD) noexcept = 0;
    virtual HRESULT RegisterMmcssProAudio() noexcept = 0;
    virtual HRESULT SetMmcssCriticalPriority() noexcept = 0;
    virtual HRESULT Start() noexcept = 0;
    virtual HRESULT WaitForRender(DWORD timeout_ms) noexcept = 0;
    virtual HRESULT GetClockPosition(
        std::uint64_t*, std::uint64_t*) noexcept = 0;
    virtual HRESULT ShutdownOnInitializingThread() noexcept = 0;
};
```

- [ ] **Step 1: Write a fake API and failing policy tests**

Create `tests/WasapiEndpointTests.cpp`. The fake records an enum call sequence, captures both initialize durations/formats, uses a fixed PCM byte array for render buffers, and can fail one selected call.

Assert the direct-success sequence:

```text
InitializeComMta
OpenDefaultConsoleEndpoint
ActivateAudioClient
IsExactFormatSupported
GetDevicePeriod
InitializeExclusiveEvent
GetBufferSize(actual)
CreateRenderEvent
SetEventHandle
GetRenderService
GetClockService
GetClockFrequency
GetRenderBuffer(prefill)
ReleaseRenderBuffer(SILENT)
RegisterMmcssProAudio
SetMmcssCriticalPriority
```

After creation succeeds, call `WasapiEndpoint::Start`; assert that it records one final `Start` call. Keeping `Start` separate lets Plan 09 allocate the mixer and render buffers after learning the authoritative frame count but before the stream runs.

The captured format must be PCM/2/44100/16/4/176400, and both initialize durations must equal the minimum period.

Add alignment behavior: first initialize returns `AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED`, aligned size is 144, then the fake must observe `ReleaseAudioClient`, second `ActivateAudioClient`, retry duration `FramesToReferenceTime(144,44100)`, and actual size exactly 144.

Assert exactly two `ActivateAudioClient` and two `InitializeExclusiveEvent` calls plus one `ReleaseAudioClient` call. No third activation or initialization attempt is permitted.

Add rejection cases:

- direct success with nominal minimum 133 frames but actual 266;
- retry actual size different from the reported aligned size;
- closest/unsupported format (`S_FALSE` or `AUDCLNT_E_UNSUPPORTED_FORMAT`);
- a second alignment error on retry;
- successful zero aligned frames, zero actual frames, and zero clock frequency at `GetAlignedBufferSize`, `GetActualBufferSize`, and `GetClockFrequency` respectively;
- failure at event, services, clock frequency, prefill, MMCSS registration, priority, and start.

For each failure assert both `AudioFailureStage` and HRESULT.

After a successful create/start, exercise runtime forwarding: an exact-size PCM16 block is copied and released with flags `0`; a wrong-size block returns `E_INVALIDARG` without acquiring a buffer; `TrySubmitSilence` releases one full buffer with `AUDCLNT_BUFFERFLAGS_SILENT`; wait failure records `WaitRenderEvent`; clock `S_FALSE` is accepted with returned position/QPC; failed clock records `GetClockPosition`.

Make shutdown fake-observable. An off-thread call must return `RPC_E_WRONG_THREAD` and remain retryable on the initializing thread. After owner-thread success, a repeated explicit call and later destruction must not delegate again. Same-thread destruction without a prior explicit call delegates exactly once.

- [ ] **Step 2: Register and verify red**

```cmake
add_executable(WasapiEndpointTests
        WasapiAudioTypes.cpp WasapiEndpoint.cpp
        tests/WasapiEndpointTests.cpp
)
target_include_directories(WasapiEndpointTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(WasapiEndpointTests PRIVATE
        miniaudio ole32 uuid avrt propsys)
add_test(NAME WasapiEndpointTests COMMAND WasapiEndpointTests)
```

Append `WasapiEndpoint.cpp` to `SOURCES`; expect missing endpoint declarations.

- [ ] **Step 3: Implement policy initialization**

`WasapiEndpoint::Create(std::unique_ptr<IWasapiApi>, EndpointInitialization* attempted, AudioFailure*)` calls a private `Initialize`. Populate `attempted` as soon as endpoint name/ID and each period/buffer field become known, even if a later step fails. Use a local helper that records the exact stage on failure.

The central alignment logic is:

```cpp
auto requested = minimum_period;
auto hr = api_->InitializeExclusiveEvent(
    requested, requested, output_format);
if (hr == AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED) {
    alignment_retry = true;
    hr = api_->GetBufferSize(&aligned_frames);
    if (FAILED(hr)) {
        return fail(AudioFailureStage::GetAlignedBufferSize, hr);
    }
    if (aligned_frames == 0) {
        return fail(AudioFailureStage::GetAlignedBufferSize,
                    AUDCLNT_E_BUFFER_SIZE_ERROR);
    }
    api_->ReleaseAudioClient();
    if (FAILED(hr = api_->ActivateAudioClient())) {
        return fail(AudioFailureStage::ReactivateAudioClient, hr);
    }
    requested = FramesToReferenceTime(aligned_frames, kOutputSampleRate);
    hr = api_->InitializeExclusiveEvent(
        requested, requested, output_format);
    if (FAILED(hr)) {
        return fail(AudioFailureStage::RetryInitializeExclusive, hr);
    }
} else if (FAILED(hr)) {
    return fail(AudioFailureStage::InitializeExclusive, hr);
}
```

After the successful initialize:

```cpp
if (alignment_retry && actual_frames != aligned_frames) {
    return fail(AudioFailureStage::GetActualBufferSize,
                AUDCLNT_E_BUFFER_SIZE_ERROR);
}
if (!alignment_retry && actual_frames >
        ReferenceTimeToFramesCeil(minimum_period, kOutputSampleRate)) {
    return fail(AudioFailureStage::GetActualBufferSize,
                AUDCLNT_E_BUFFER_SIZE_ERROR);
}
```

Reject `actual_frames == 0` at `GetActualBufferSize` with `AUDCLNT_E_BUFFER_SIZE_ERROR`. After successful `GetClockFrequency`, reject a zero result at `GetClockFrequency` with `E_UNEXPECTED`. These checks occur before any render-buffer acquisition.

Then perform event, services, clock, silent prefill, MMCSS, and priority in the tested order. Return the initialized-but-not-started endpoint.

- [ ] **Step 4: Implement runtime endpoint methods**

Expose:

```cpp
HRESULT Start(AudioFailure*) noexcept;
HRESULT WaitForRender(DWORD timeout_ms, AudioFailure*) noexcept;
HRESULT SubmitPcm16(
    std::span<const std::int16_t>, AudioFailure*) noexcept;
HRESULT TrySubmitSilence() noexcept;
HRESULT ReadClock(EndpointClockPosition*, AudioFailure*) noexcept;
HRESULT ShutdownOnInitializingThread() noexcept;
const EndpointInitialization& initialization() const noexcept;
```

`Start` delegates once to `IWasapiApi::Start` and records `StartEndpoint` on failure. `SubmitPcm16` requires exactly `actual_buffer_frames * 2` samples, calls `GetRenderBuffer`, `memcpy`, then `ReleaseRenderBuffer(frames,0)`. `TrySubmitSilence` requests one whole endpoint buffer and releases it with `AUDCLNT_BUFFERFLAGS_SILENT`, returning any HRESULT without formatting or logging. `ReadClock` accepts `S_OK` and `S_FALSE`; only `FAILED(hr)` is fatal.

`ShutdownOnInitializingThread` delegates until owner-thread shutdown succeeds, then returns `S_OK` without delegating again. `WasapiEndpoint` destruction invokes it as a same-thread fallback. An off-thread `RPC_E_WRONG_THREAD` does not mark shutdown complete, so the owning audio thread can still perform the required cleanup.

- [ ] **Step 5: Implement `Win32WasapiApi`**

Use WRL `ComPtr` fields for enumerator, device, client, render client, and clock. `OpenDefaultConsoleEndpoint` must call:

```cpp
CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, ...);
enumerator_->GetDefaultAudioEndpoint(eRender, eConsole, &device_);
device_->GetId(&id);
device_->OpenPropertyStore(STGM_READ, &properties);
properties->GetValue(PKEY_Device_FriendlyName, &name);
```

Free `GetId` with `CoTaskMemFree` and clear `PROPVARIANT` with `PropVariantClear`.

Initialize with:

```cpp
client_->Initialize(
    AUDCLNT_SHAREMODE_EXCLUSIVE,
    AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
    buffer_duration,
    periodicity,
    &format,
    nullptr);
```

On alignment retry, owner-thread `ReleaseAudioClient` resets render/clock/client `ComPtr`s but retains the selected `IMMDevice` so `ActivateAudioClient` creates a fresh client from the same endpoint.

`CreateRenderEvent` uses auto-reset/nonsignaled `CreateEventW`. MMCSS calls `AvSetMmThreadCharacteristicsW(L"Pro Audio", ...)` followed by `AvSetMmThreadPriority(..., AVRT_PRIORITY_CRITICAL)`.

Production `ShutdownOnInitializingThread` verifies the current thread, then reverts MMCSS, closes the event, releases clock/render/client/device/enumerator COM objects, and calls `CoUninitialize` in that order. It is idempotent. The production destructor invokes it only on the initializing thread. If destruction nevertheless occurs off-thread, the fail-safe does not close, signal, reuse, or otherwise touch the raw render-event handle because it cannot prove the owner thread is no longer waiting on it. It abandons that handle to process cleanup, `Detach()`es every remaining `ComPtr` without `Release`, and does not revert MMCSS or call `CoUninitialize`. These deliberate leaks are reserved for fatal process-cleanup paths. Plan 09 must explicitly shut down and reset the endpoint before the audio thread exits; normal success remains process-lifetime and deliberately leaves the endpoint alive.

- [ ] **Step 6: Verify focused policy and production compilation**

```powershell
& $env:ComSpec /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build build-msvc32-latest --target WasapiEndpointTests iDmacDrv32 && ctest --test-dir build-msvc32-latest -R "^WasapiEndpointTests$" --output-on-failure'
```

Expected: fake policy cases pass; no test opens a physical endpoint.

- [ ] **Step 7: Commit**

```powershell
git add -- CMakeLists.txt WasapiEndpoint.h WasapiEndpoint.cpp tests/WasapiEndpointTests.cpp
git commit -m "feat: add strict WASAPI exclusive endpoint"
```

## Completion Gate

The aligned frame count returned by the driver is the only accepted retry size. No constant 132/133-frame endpoint assumption may appear outside deterministic arithmetic tests.

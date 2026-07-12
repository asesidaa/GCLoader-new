# DirectSound Device and Primary Buffer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Complete the game-facing DirectSound compatibility boundary with an `IDirectSound8` device and a format-only primary `IDirectSoundBuffer8`.

**Architecture:** The device validates the observed cooperative-level call and routes primary versus secondary descriptors. The primary buffer exposes the exact endpoint format but owns no source data; secondary construction delegates to Plan 06.

**Tech Stack:** C++23, DirectSound 8 COM ABI, Plan 06 facade, CTest.

## Global Constraints

- Device `QueryInterface` supports only `IUnknown`, `IDirectSound`, and `IDirectSound8`.
- Reject COM aggregation.
- Accept the observed `DSSCL_PRIORITY` call; no cooperative-level call may reconfigure WASAPI.
- Primary `SetFormat` accepts only exact 44,100 Hz stereo PCM16.
- Primary playback methods needed during initialization are harmless successes; primary storage/control methods remain unavailable.
- Complete every `IDirectSound8` and `IDirectSoundBuffer8` vtable entry.

---

## Prerequisites

- Plans 01-06 are committed.

## File Structure

- Modify `DirectSoundFacade.h` / `DirectSoundFacade.cpp`.
- Create `tests/DirectSoundDeviceTests.cpp`.
- Modify `CMakeLists.txt`.

### Task 1: Device and Primary COM Facades

**Interfaces:**

```cpp
HRESULT CreateDirectSoundDevice(
    IAudioEngineServices&, IDirectSound8**) noexcept;

class PrimarySoundBuffer final : public IDirectSoundBuffer8 {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void**) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;
    HRESULT STDMETHODCALLTYPE GetCaps(LPDSBCAPS) override;
    HRESULT STDMETHODCALLTYPE GetCurrentPosition(LPDWORD, LPDWORD) override;
    HRESULT STDMETHODCALLTYPE GetFormat(LPWAVEFORMATEX, DWORD, LPDWORD) override;
    HRESULT STDMETHODCALLTYPE GetVolume(LPLONG) override;
    HRESULT STDMETHODCALLTYPE GetPan(LPLONG) override;
    HRESULT STDMETHODCALLTYPE GetFrequency(LPDWORD) override;
    HRESULT STDMETHODCALLTYPE GetStatus(LPDWORD) override;
    HRESULT STDMETHODCALLTYPE Initialize(LPDIRECTSOUND, LPCDSBUFFERDESC) override;
    HRESULT STDMETHODCALLTYPE Lock(
        DWORD, DWORD, LPVOID*, LPDWORD, LPVOID*, LPDWORD, DWORD) override;
    HRESULT STDMETHODCALLTYPE Play(DWORD, DWORD, DWORD) override;
    HRESULT STDMETHODCALLTYPE SetCurrentPosition(DWORD) override;
    HRESULT STDMETHODCALLTYPE SetFormat(LPCWAVEFORMATEX) override;
    HRESULT STDMETHODCALLTYPE SetVolume(LONG) override;
    HRESULT STDMETHODCALLTYPE SetPan(LONG) override;
    HRESULT STDMETHODCALLTYPE SetFrequency(DWORD) override;
    HRESULT STDMETHODCALLTYPE Stop() override;
    HRESULT STDMETHODCALLTYPE Unlock(LPVOID, DWORD, LPVOID, DWORD) override;
    HRESULT STDMETHODCALLTYPE Restore() override;
    HRESULT STDMETHODCALLTYPE SetFX(DWORD, LPDSEFFECTDESC, LPDWORD) override;
    HRESULT STDMETHODCALLTYPE AcquireResources(DWORD, DWORD, LPDWORD) override;
    HRESULT STDMETHODCALLTYPE GetObjectInPath(
        REFGUID, DWORD, REFGUID, LPVOID*) override;
};

class DirectSoundDevice final : public IDirectSound8 {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void**) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;
    HRESULT STDMETHODCALLTYPE CreateSoundBuffer(
        LPCDSBUFFERDESC, LPDIRECTSOUNDBUFFER*, LPUNKNOWN) override;
    HRESULT STDMETHODCALLTYPE GetCaps(LPDSCAPS) override;
    HRESULT STDMETHODCALLTYPE DuplicateSoundBuffer(
        LPDIRECTSOUNDBUFFER, LPDIRECTSOUNDBUFFER*) override;
    HRESULT STDMETHODCALLTYPE SetCooperativeLevel(HWND, DWORD) override;
    HRESULT STDMETHODCALLTYPE Compact() override;
    HRESULT STDMETHODCALLTYPE GetSpeakerConfig(LPDWORD) override;
    HRESULT STDMETHODCALLTYPE SetSpeakerConfig(DWORD) override;
    HRESULT STDMETHODCALLTYPE Initialize(LPCGUID) override;
    HRESULT STDMETHODCALLTYPE VerifyCertification(LPDWORD) override;
};
```

- [ ] **Step 1: Write failing device/primary tests**

Create `tests/DirectSoundDeviceTests.cpp` with a fake engine-services object that owns a four-frame `MiniaudioMixer` and implements these exact methods:

```cpp
std::unique_ptr<MixerVoice> CreateVoice(
    const NormalizedSourceFormat&, AudioSnapshot&,
    AudioCursorTimeline&, VoiceUsage, ma_result*) noexcept override;
std::optional<std::uint64_t> CurrentOutputFrame() noexcept override;
std::uint32_t endpoint_buffer_frames() const noexcept override;
void CountCursorTimelineFailure() noexcept override;
```

Assert:

- factory returns a reference-count-one `IDirectSound8`;
- `IUnknown`, `IDirectSound`, and `IDirectSound8` share identity;
- null HWND fails; `DSSCL_NORMAL` returns `DSERR_PRIOLEVELNEEDED`; nonnull `DSSCL_PRIORITY` succeeds;
- buffer creation before priority cooperative level returns `DSERR_PRIOLEVELNEEDED`;
- aggregation returns `DSERR_NOAGGREGATION`;
- a primary descriptor must have only `DSBCAPS_PRIMARYBUFFER`, zero bytes, and null source format;
- primary `QueryInterface` supports the three buffer identities;
- primary `SetFormat` accepts exact output and rejects 48 kHz/float/mono;
- primary `GetFormat` and `GetCaps` return exact output/capability data;
- primary `Play`, `Stop`, and `Restore` return `DS_OK`;
- primary `Lock`/`Unlock` return `DSERR_INVALIDCALL`;
- device routes a valid secondary descriptor to Plan 06;
- `GetCaps`, duplication, compact, speaker config, and certification return `DSERR_UNSUPPORTED`;
- final releases return zero.

- [ ] **Step 2: Register and verify red**

```cmake
add_executable(DirectSoundDeviceTests
        WasapiAudioTypes.cpp AudioSnapshot.cpp AudioCursorTimeline.cpp
        MiniaudioMixer.cpp DirectSoundFacade.cpp
        tests/DirectSoundDeviceTests.cpp
)
target_include_directories(DirectSoundDeviceTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(DirectSoundDeviceTests PRIVATE miniaudio dsound dxguid)
add_test(NAME DirectSoundDeviceTests COMMAND DirectSoundDeviceTests)
```

Expected: missing device/primary declarations.

- [ ] **Step 3: Declare complete device and primary vtables**

For `DirectSoundDevice`, override:

```cpp
QueryInterface; AddRef; Release; CreateSoundBuffer; GetCaps;
DuplicateSoundBuffer; SetCooperativeLevel; Compact;
GetSpeakerConfig; SetSpeakerConfig; Initialize; VerifyCertification;
```

Use the exact COM signatures from `IDirectSound8` in `dsound.h`, not simplified project signatures.

For `PrimarySoundBuffer`, use the complete method list in this plan's interface block verbatim.

- [ ] **Step 4: Implement primary format/capability behavior**

Use one helper:

```cpp
WAVEFORMATEX OutputWaveFormat() noexcept {
    return {
        .wFormatTag = WAVE_FORMAT_PCM,
        .nChannels = kOutputChannels,
        .nSamplesPerSec = kOutputSampleRate,
        .nAvgBytesPerSec = kOutputAverageBytesPerSecond,
        .nBlockAlign = kOutputBlockAlign,
        .wBitsPerSample = kOutputBitsPerSample,
        .cbSize = 0,
    };
}
```

`GetCaps` returns `DSBCAPS_PRIMARYBUFFER` and byte length zero. `GetCurrentPosition` returns zeros. `GetStatus` returns zero. `SetFormat` calls `IsExactOutputFormat` and returns `DSERR_BADFORMAT` on mismatch.

- [ ] **Step 5: Implement device routing and unsupported methods**

`CreateSoundBuffer` checks aggregation, descriptor size, and priority cooperative state. For primary, validate its strict descriptor and create `PrimarySoundBuffer`; otherwise delegate to `SecondarySoundBuffer::Create`.

Use:

| Device method | Result |
|---|---|
| `Initialize` | `DSERR_ALREADYINITIALIZED` |
| `GetCaps`, `DuplicateSoundBuffer`, `Compact`, speaker methods, `VerifyCertification` | `DSERR_UNSUPPORTED` |

- [ ] **Step 6: Verify**

```powershell
& $env:ComSpec /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build build-msvc32-latest --target DirectSoundDeviceTests SecondarySoundBufferTests iDmacDrv32 && ctest --test-dir build-msvc32-latest -R "^(DirectSoundDeviceTests|SecondarySoundBufferTests)$" --output-on-failure'
```

- [ ] **Step 7: Commit**

```powershell
git add -- CMakeLists.txt DirectSoundFacade.h DirectSoundFacade.cpp tests/DirectSoundDeviceTests.cpp
git commit -m "feat: emulate DirectSound device and primary buffer"
```

## Completion Gate

Creating a primary buffer must not allocate source PCM or open an endpoint; it only satisfies the game's DirectSound initialization contract.

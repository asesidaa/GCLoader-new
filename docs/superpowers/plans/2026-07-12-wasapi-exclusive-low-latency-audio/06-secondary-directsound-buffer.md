# Secondary DirectSound Buffer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement the complete `IDirectSoundBuffer8` ABI for game secondary buffers, with functional storage, playback, gain, looping, seeking, status, and endpoint-clock cursor behavior.

**Architecture:** `SecondarySoundBuffer` owns the normalized source format, shared `AudioSnapshot` and `AudioCursorTimeline` sources, and one custom-node `MixerVoice`. It passes those shared owners through the voice-creation boundary so voice state structurally retains its render sources without copying `shared_ptr` values on the render path. A narrow engine-services interface supplies voice creation, current hardware output frame, endpoint period, and cursor-failure counting without coupling COM code to the production engine.

**Tech Stack:** C++23, DirectSound 8 COM ABI, Plans 02-05, miniaudio, CTest.

## Global Constraints

- Accept only observed flags: `STATIC`, `CTRLVOLUME`, optional `CTRLPOSITIONNOTIFY`, `GETCURRENTPOSITION2`, and `LOCDEFER`.
- Preserve original source format and game-facing byte length.
- `QueryInterface` supports only `IUnknown`, `IDirectSoundBuffer`, and `IDirectSoundBuffer8`.
- Implement every vtable entry; pan, frequency, 3D, effects, and notification controls remain unsupported.
- `SetVolume` uses DirectSound hundredths-of-dB conversion with no smoothing.
- `Stop` preserves the last hardware-clock cursor.
- A naturally ended nonlooping voice remains game-visible as playing while the endpoint clock is inside its final queued half-open span and the coherent drain record matches the current playback run and latest accepted seek epoch; this hardware-drain status does not change mixer active accounting. Replay, seek, and stop make stale records unobservable.
- `SetCurrentPosition` increments the epoch, seeks source frames, and rejects unaligned/out-of-range bytes.
- `Restore` succeeds and performs non-RT retired-snapshot reclamation.

---

## Prerequisites

- Plans 01-05 are committed.

## File Structure

- Create `DirectSoundFacade.h` / `DirectSoundFacade.cpp` with engine seam and secondary class.
- Create `tests/SecondarySoundBufferTests.cpp`.
- Modify `CMakeLists.txt`.

### Task 1: Functional Secondary Buffer

**Interfaces:**

```cpp
class IAudioEngineServices {
public:
    virtual ~IAudioEngineServices() = default;
    virtual std::unique_ptr<MixerVoice> CreateVoice(
        const NormalizedSourceFormat&, std::shared_ptr<AudioSnapshot>,
        std::shared_ptr<AudioCursorTimeline>, VoiceUsage,
        ma_result*) noexcept = 0;
    virtual std::optional<std::uint64_t>
        CurrentOutputFrame() noexcept = 0;
    virtual std::uint32_t endpoint_buffer_frames() const noexcept = 0;
    virtual void CountCursorTimelineFailure() noexcept = 0;
};

class SecondarySoundBuffer final : public IDirectSoundBuffer8 {
public:
    static HRESULT Create(
        IAudioEngineServices&, const DSBUFFERDESC&,
        SecondarySoundBuffer**) noexcept;
};
```

- [ ] **Step 1: Write failing COM and behavior tests**

Create a fake `IAudioEngineServices` backed by a real four-frame `MiniaudioMixer`; expose a settable `std::optional<uint64_t> output_frame` and cursor-failure counter.

In `tests/SecondarySoundBufferTests.cpp`, assert:

- construction succeeds for static flags `0x50082` and streaming flags `0x50182`;
- malformed descriptor size, null format, zero/unaligned bytes, primary flag, and `CTRL3D` fail deterministically;
- all three supported interface IDs return the same identity and unsupported IDs return `E_NOINTERFACE`;
- `GetCaps` preserves flags/bytes and `GetFormat` preserves PCM/extensible source bytes;
- wrapped COM `Lock(12,8)` over a 16-byte buffer returns 4+4 and matching `Unlock` publishes;
- invalid/mismatched locks surface the `AudioSnapshot` HRESULTs;
- volume `-600` round-trips; values outside `[-10000,0]` fail;
- looping `Play` sets `DSBSTATUS_PLAYING | DSBSTATUS_LOOPING`;
- repeated `Play` while already playing does not rewind the cursor or double-count the active voice;
- after rendering output `[100,104)`, hardware frame 102 reports source byte 8;
- write cursor is one endpoint period ahead in original bytes;
- missing clock/timeline returns the last good cursor and increments the diagnostic counter;
- `Stop` clears playing status and preserves the last cursor;
- a nonlooping source ending inside one endpoint period publishes a final queued output end, resolves the hardware-mapped cursor without fallback while inside it, reports `PLAYING` until that exclusive end, preserves the cursor through `Stop`, and remains stopped with the last good cursor at/after the end;
- after replay advances the playback run or `SetCurrentPosition` accepts a new epoch, a stale terminal record cannot revive COM playing status, resolve an old timeline span, or increment cursor-failure diagnostics; only a matching new terminal record may drive drain behavior;
- seek byte 2 and byte 16 fail for a 16-byte/stereo buffer; byte 4 succeeds and changes epoch;
- `Restore` succeeds;
- `SetPan`, `SetFrequency`, `SetFX`, `AcquireResources`, and `GetObjectInPath` return `DSERR_CONTROLUNAVAIL`;
- final `Release` returns zero.

- [ ] **Step 2: Register and verify red**

```cmake
add_executable(SecondarySoundBufferTests
        WasapiAudioTypes.cpp AudioSnapshot.cpp AudioCursorTimeline.cpp
        MiniaudioMixer.cpp DirectSoundFacade.cpp
        tests/SecondarySoundBufferTests.cpp
)
target_include_directories(SecondarySoundBufferTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(SecondarySoundBufferTests PRIVATE miniaudio dsound dxguid)
add_test(NAME SecondarySoundBufferTests COMMAND SecondarySoundBufferTests)
```

Append `DirectSoundFacade.cpp` to `SOURCES`; expect missing facade declarations.

- [ ] **Step 3: Declare the complete vtable**

`SecondarySoundBuffer` must override these exact groups:

```cpp
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
```

- [ ] **Step 4: Implement creation and lifetime**

Validate flags against:

```cpp
constexpr DWORD kSupportedSecondaryFlags =
    DSBCAPS_STATIC | DSBCAPS_CTRLVOLUME |
    DSBCAPS_CTRLPOSITIONNOTIFY |
    DSBCAPS_GETCURRENTPOSITION2 | DSBCAPS_LOCDEFER;
```

Normalize the format, require `dwBufferBytes % block_align == 0`, allocate shared snapshot/timeline owners, then create the object and pass those owners through `CreateVoice`. The buffer and voice state retain shared ownership; render code continues to use one scoped, nonmoving `AudioSnapshot::RenderView` and does not copy `shared_ptr` values. Map allocation/miniaudio initialization failure to `DSERR_OUTOFMEMORY` and never return a half-created object.

Classify usage from both format and the two observed descriptor patterns:

```cpp
constexpr DWORD kObservedStaticFlags =
    DSBCAPS_STATIC | DSBCAPS_CTRLVOLUME |
    DSBCAPS_GETCURRENTPOSITION2 | DSBCAPS_LOCDEFER;
const bool observed_usage = descriptor.dwFlags == kObservedStaticFlags ||
    descriptor.dwFlags ==
        (kObservedStaticFlags | DSBCAPS_CTRLPOSITIONNOTIFY);
const auto usage = format.native_rate_pcm16 && observed_usage
    ? VoiceUsage::GameplayNativeCandidate
    : VoiceUsage::General;
```

This is a diagnostic candidate, not filename proof; manual gameplay validation remains authoritative.

Use atomic COM references beginning at one. Delete only when `fetch_sub(...) - 1 == 0`.

- [ ] **Step 5: Implement functional methods**

`GetCurrentPosition` resolves the current output frame through the timeline and current epoch while the voice is either logically mixing or the endpoint clock is strictly before its final queued output end. Once the endpoint reaches/passes that exclusive boundary, return the last good cursor without treating normal drain completion as a timeline failure. On a real clock/timeline failure:

```cpp
engine_.CountCursorTimelineFailure();
return last_reported_source_frame_.load(std::memory_order_acquire);
```

Convert play and projected write frames with `SourceFrameToByte`.

`Play` requires both reserved arguments zero and allows only `DSBPLAY_LOOPING`. `Stop` resolves/stores the cursor before transitioning the voice to a nondraining stopped state. `GetStatus` combines logical `voice.playing()` with `CurrentOutputFrame() < audible_until_output_frame`; the voice method returns a boundary only from a coherent record whose run matches the current draining playback state and whose epoch matches the latest accepted seek epoch. It never reports `LOOPING` for terminal drain because looped voices have no terminal record. Each operation reads the endpoint clock at most once. `SetCurrentPosition` performs:

```cpp
const auto source_frame = position / format_.block_align;
const auto epoch = epoch_.fetch_add(1, std::memory_order_acq_rel) + 1;
last_reported_source_frame_.store(source_frame, std::memory_order_release);
return voice_->Seek(source_frame, epoch);
```

`GetStatus` sets `LOOPING` only when the voice is also playing.

- [ ] **Step 6: Implement the unsupported ABI table**

Use this exact behavior:

| Method | Result |
|---|---|
| `Initialize` | `DSERR_ALREADYINITIALIZED` |
| `SetFormat` | `DSERR_CONTROLUNAVAIL` |
| pan/frequency getters/setters | `DSERR_CONTROLUNAVAIL` |
| `SetFX`, `AcquireResources`, `GetObjectInPath` | `DSERR_CONTROLUNAVAIL` |
| `Restore` | reclaim retired snapshots, then `DS_OK` |

- [ ] **Step 7: Verify**

```powershell
& $env:ComSpec /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build build-msvc32-latest --target SecondarySoundBufferTests iDmacDrv32 && ctest --test-dir build-msvc32-latest -R "^SecondarySoundBufferTests$" --output-on-failure'
```

- [ ] **Step 8: Commit**

```powershell
git add -- CMakeLists.txt DirectSoundFacade.h DirectSoundFacade.cpp tests/SecondarySoundBufferTests.cpp
git commit -m "feat: emulate DirectSound secondary buffers"
```

## Completion Gate

The test must call the object through `IDirectSoundBuffer8*`; testing only project-owned helper methods does not prove the COM ABI.

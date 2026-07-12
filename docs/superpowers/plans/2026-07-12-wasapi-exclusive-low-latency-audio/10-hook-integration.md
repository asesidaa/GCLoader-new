# DirectSound Hook and Lifecycle Integration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Install one owned `DirectSoundCreate8` detour when configured, lazily create the process-wide exclusive engine on first use, and fail startup clearly without falling back.

**Architecture:** A small hook transaction executes under the existing game-process `DllMain` branch and owns exactly one MinHook target. The detour validates the observed call, invokes a cached engine factory outside loader lock, and creates a fresh `DirectSoundDevice` over the process-lifetime engine.

**Tech Stack:** C++23, MinHook, Plans 01-09, DirectSound 8, plog, Win32 message/process APIs, CTest.

## Global Constraints

- Disabled mode resolves no DirectSound export, creates no hook, and creates no engine.
- Enabled hook installation may run under loader lock; COM, endpoints, miniaudio, and threads may not.
- Resolve only loaded `dsound.dll` and export `DirectSoundCreate8`; do not load another module in `DllMain`.
- Queue/enable/disable/remove only the owned target; never use `MH_ALL_HOOKS`.
- Roll back a partially created/queued hook before returning failure.
- The detour never calls the original function in enabled mode.
- Accept only `device_guid == nullptr`, `outer == nullptr`, and nonnull result pointer.
- Engine initialization failure is fatal and tells the operator to set `enable_wasapi_exclusive_audio = false`; no fallback.
- Process detach performs no audio teardown.

---

## Prerequisites

- Plans 01-09 are committed.

## File Structure

- Create `WasapiAudioPatch.h` / `WasapiAudioPatch.cpp`.
- Create `tests/WasapiAudioPatchTests.cpp`.
- Modify `dllmain.cpp` and `CMakeLists.txt`.

### Task 1: Exact-Target Hook Transaction

**Interfaces:**

```cpp
enum class AudioHookStage {
    None, ResolveModule, ResolveExport, InitializeMinHook,
    CreateHook, QueueEnable, ApplyQueued,
};

struct AudioHookFailure {
    AudioHookStage stage{AudioHookStage::None};
    MH_STATUS status{MH_OK};
    DWORD win32_error{ERROR_SUCCESS};
    void* target{};
};

struct AudioMinHookApi {
    decltype(&MH_Initialize) initialize;
    decltype(&MH_CreateHook) create;
    decltype(&MH_QueueEnableHook) queue_enable;
    decltype(&MH_ApplyQueued) apply;
    decltype(&MH_DisableHook) disable;
    decltype(&MH_RemoveHook) remove;
};

bool InstallWasapiAudioHook(
    bool enabled,
    AudioMinHookApi,
    AudioHookFailure*) noexcept;
bool WasapiAudioPatchInit() noexcept;
```

- [ ] **Step 1: Write failing hook transaction tests**

Create `tests/WasapiAudioPatchTests.cpp` with fake module/export resolution and fake MinHook functions. Assert:

- disabled mode performs zero resolution and MinHook calls;
- enabled mode resolves exactly `dsound.dll!DirectSoundCreate8`;
- `MH_ERROR_ALREADY_INITIALIZED` is accepted from initialization;
- create, queue, and apply reference only the resolved target;
- create failure removes nothing that was not created;
- queue/apply failure disables and removes exactly the created target;
- no fake call receives `MH_ALL_HOOKS`;
- every failure records exact stage/status/Win32 error;
- no engine-factory function is invoked during hook installation.

- [ ] **Step 2: Implement owned hook installation**

Use `GetModuleHandleW(L"dsound.dll")` and `GetProcAddress(...,"DirectSoundCreate8")`. Return `ERROR_MOD_NOT_FOUND`/`ERROR_PROC_NOT_FOUND` at the matching stages.

Install in this order:

```cpp
initialize();
create(target, DirectSoundCreate8Detour, &original);
queue_enable(target);
apply();
```

On any failure after create, call `disable(target)` then `remove(target)`. Retain the committed target for process lifetime; do not expose a detach unhook.

### Task 2: Lazy Engine and Detour Contract

**Interfaces:**

```cpp
using DirectSoundCreate8Fn = HRESULT (WINAPI*)(
    LPCGUID, LPDIRECTSOUND8*, LPUNKNOWN);

class IExclusiveEngineFactory {
public:
    virtual ~IExclusiveEngineFactory() = default;
    virtual ExclusiveAudioEngine* GetOrCreate(
        AudioStartupFailure*) noexcept = 0;
};

class IAudioStartupFailureReporter {
public:
    virtual ~IAudioStartupFailureReporter() = default;
    virtual void FatalStartupFailure(
        const AudioStartupFailure&) noexcept = 0;
};
```

- [ ] **Step 3: Add failing detour/lazy-init tests**

Using fake factory/reporter, assert:

- null output pointer returns `DSERR_INVALIDPARAM`;
- nonnull output is set to null before other work;
- nonnull device GUID returns `DSERR_NODRIVER`;
- nonnull outer returns `DSERR_NOAGGREGATION`;
- two valid detour calls initialize the engine once and return two independent device facade objects;
- engine failure invokes the startup reporter once and never calls the saved original trampoline;
- successful enabled calls never call the saved original trampoline;
- cached initialization failure is not retried by a later call.

- [ ] **Step 4: Implement production lazy state**

Use a mutex/condition variable with states `Uninitialized`, `Initializing`, `Succeeded`, and `Failed`; do not use `std::call_once`, because tests and diagnostics need the cached failure value.

The winning caller creates:

```cpp
auto api = CreateProductionWasapiApi();
auto observer = std::make_shared<ProductionAudioObserver>();
AudioStartupFailure startup_failure{};
auto engine = ExclusiveAudioEngine::StartAndWait(
    std::move(api), observer, 10'000, nullptr, &startup_failure);
```

On success, move the engine into process-lifetime global ownership and publish `Succeeded`. On failure or timeout, publish the exact failure and `Failed`; the production reporter logs and terminates the process. A timed-out engine object is intentionally abandoned to process cleanup because the next action is fatal process termination; do not block indefinitely joining an initialization thread.

For a valid call:

```cpp
AudioStartupFailure startup_failure{};
auto* engine = GetOrCreateExclusiveAudioEngine(&startup_failure);
if (engine == nullptr) {
    reporter.FatalStartupFailure(startup_failure);
    return DSERR_NODRIVER;
}
return CreateDirectSoundDevice(*engine, output);
```

### Task 3: Production Diagnostics and `DllMain` Gate

- [ ] **Step 5: Implement non-real-time observer/reporting**

`ProductionAudioObserver` logs successful startup fields from the design: requested/active backend, endpoint name/ID, exact format, default/minimum periods, requested duration, actual frames/ms, event-driven exclusive success, MMCSS profile/priority, and mixer rate/channels.

Runtime summaries log every counter in `AudioRuntimeCountersSnapshot`.

Runtime fatal and initial startup fatal logs include endpoint ID when available, `AudioFailureStage`, HRESULT in hex, and exact format. The message text must include:

```text
WASAPI exclusive low-latency audio failed.
Restart the game after setting enable_wasapi_exclusive_audio = false
to restore the original DirectSound backend.
```

Runtime fatal reports then call `TerminateProcess(GetCurrentProcess(), ERROR_DEVICE_NOT_AVAILABLE)`. Tests use the fake reporter and never terminate.

- [ ] **Step 6: Add the game-only attach gate**

In `dllmain.cpp`, include `WasapiAudioPatch.h` and call it only inside `ShouldRunGameOnlyInitialization(role)`, before RFID/framerate/input initialization:

```cpp
if (!gc::audio::WasapiAudioPatchInit()) {
    PLOG_ERROR << "WasapiAudioPatch: fail-closed DLL attach";
    return FALSE;
}
```

Disabled mode returns true. Enabled hook failure logs the `AudioHookFailure`, shows an actionable message, and returns false so attach fails. No engine work occurs in this function.

- [ ] **Step 7: Register sources, test target, and Windows libraries**

Append `WasapiAudioPatch.cpp` to `SOURCES`. Ensure `iDmacDrv32` links:

```cmake
miniaudio dsound dxguid ole32 uuid avrt propsys
```

Register `WasapiAudioPatchTests` with every audio source it consumes plus MinHook and the same Windows libraries.

- [ ] **Step 8: Verify focused and aggregate tests**

```powershell
& $env:ComSpec /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake -S . -B build-msvc32-latest -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl && cmake --build build-msvc32-latest --target WasapiAudioPatchTests iDmacDrv32 ConfigGUI && ctest --test-dir build-msvc32-latest -R "^(WasapiAudioPatchTests|ExclusiveAudioEngineTests|WasapiEndpointTests|DirectSoundDeviceTests|SecondarySoundBufferTests)$" --output-on-failure'
```

- [ ] **Step 9: Commit**

```powershell
git add -- CMakeLists.txt dllmain.cpp WasapiAudioPatch.h WasapiAudioPatch.cpp tests/WasapiAudioPatchTests.cpp
git commit -m "feat: hook DirectSound into exclusive audio engine"
```

## Completion Gate

`WasapiAudioPatchInit` must be provably lightweight: the fake engine factory remains untouched until the detour test executes the first valid `DirectSoundCreate8` call.

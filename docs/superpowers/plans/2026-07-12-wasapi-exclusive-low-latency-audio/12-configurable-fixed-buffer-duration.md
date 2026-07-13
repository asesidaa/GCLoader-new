# Configurable Fixed WASAPI Buffer Duration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:executing-plans` to implement this plan inline. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the unstable endpoint-minimum request with a fixed, configurable exclusive buffer that defaults to 10 ms while preserving `0 = endpoint minimum`.

**Architecture:** The strict TOML schema owns a process-lifetime millisecond value. The production hook converts it to `REFERENCE_TIME` once, the engine forwards it unchanged, and `WasapiEndpoint` requests `max(configured, driver minimum)` for both exclusive duration and periodicity. The existing driver-alignment retry remains authoritative.

**Tech Stack:** C++20, reflect-cpp TOML, Dear ImGui, WASAPI/Core Audio, MSVC x86/Ninja, CTest.

## Global Constraints

- Work in the isolated GCLoader worktree; `H:\gc` is deployment/runtime state only.
- Add the required `experimental.wasapi_exclusive_buffer_ms` key with default `10`; do not use `rfl::DefaultIfMissing`.
- Interpret `0` as an explicit endpoint-minimum request.
- Keep the selected value fixed for the process lifetime; do not add adaptive resizing or clock-timeline recovery.
- Pass the same selected duration as `hnsBufferDuration` and `hnsPeriodicity`.
- Preserve the current `AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED` retry and treat its aligned duration as authoritative.
- The render thread remains allocation-free and does not read configuration.
- Preserve exact 44,100 Hz stereo PCM16 and all existing fail-closed behavior.

---

### Task 1: Add the Strict Buffer Configuration Contract

**Files:**

- Modify: `config.h`
- Modify: `config.toml`
- Modify: `GUI_main.cpp`
- Test: `tests/ConfigFeatureTests.cpp`

- [x] **Step 1: Add failing schema and round-trip tests**

Add `wasapi_exclusive_buffer_ms = 10` to every default/valid experimental fixture and `wasapi_exclusive_buffer_ms = 20` to `kEnabledExperimentalConfig`. Add these assertions:

```cpp
failures += expect_u32(
    upgraded_defaults.experimental().wasapi_exclusive_buffer_ms(),
    10,
    "upgraded default wasapi_exclusive_buffer_ms");
failures += expect_bool(
    generated_toml.find("wasapi_exclusive_buffer_ms = 10") !=
        std::string::npos,
    true,
    "ConfigGUI default WASAPI buffer serialization");
failures += expect_u32(
    custom.experimental().wasapi_exclusive_buffer_ms(),
    20,
    "custom wasapi_exclusive_buffer_ms");
failures += expect_u32(
    reparsed_wasapi.experimental().wasapi_exclusive_buffer_ms(),
    20,
    "WASAPI buffer TOML round trip");
```

Add a partial `[experimental]` fixture containing every existing field but omitting `wasapi_exclusive_buffer_ms`, and require `expect_parse_failure(..., "missing wasapi_exclusive_buffer_ms")`.

- [x] **Step 2: Verify RED**

```powershell
& $env:ComSpec /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build build-msvc32-latest --target ConfigFeatureTests'
```

Expected: compilation fails because `ExperimentalConfig::wasapi_exclusive_buffer_ms` does not exist.

- [x] **Step 3: Implement schema, getter, distributed value, and GUI input**

Add the distinct 32-bit numeric storage alias, then add the field to
`ExperimentalConfig` and its public getter to `ConfigManager`. A raw
`std::uint32_t` cannot be used for reflect-cpp storage here because SDL aliases
`SDL_Keycode` to that type and its key-name reflector would parse the numeric
TOML value as a string.

```cpp
using WasapiBufferMillisecondsConfigValue = unsigned long;
static_assert(
    sizeof(WasapiBufferMillisecondsConfigValue) == sizeof(std::uint32_t));

rfl::Rename<
    "wasapi_exclusive_buffer_ms",
    WasapiBufferMillisecondsConfigValue>
    wasapi_exclusive_buffer_ms = 10;

std::uint32_t GetWasapiExclusiveBufferMs() const {
    return static_cast<std::uint32_t>(
        config.experimental.value().wasapi_exclusive_buffer_ms.value());
}
```

Add `wasapi_exclusive_buffer_ms = 10` beside the enable flag in `config.toml`. In `GUI_main.cpp`, add a `ImGuiDataType_U32` input bound directly to the field and a tooltip stating that `0` uses the endpoint minimum and changes take effect after restarting the game.

- [x] **Step 4: Verify GREEN and commit**

```powershell
& $env:ComSpec /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build build-msvc32-latest --target ConfigFeatureTests ConfigGUI'
ctest --test-dir build-msvc32-latest -R ConfigFeatureTests --output-on-failure
git add -- config.h config.toml GUI_main.cpp tests/ConfigFeatureTests.cpp
git commit -m "feat: add configurable WASAPI buffer duration"
```

Expected: the focused test passes, ConfigGUI builds, and missing-key parsing stays strict.

---

### Task 2: Apply the Fixed Duration Through Endpoint Startup

**Files:**

- Modify: `WasapiEndpoint.h`
- Modify: `WasapiEndpoint.cpp`
- Modify: `ExclusiveAudioEngine.h`
- Modify: `ExclusiveAudioEngine.cpp`
- Modify: `WasapiAudioPatchInternal.h`
- Modify: `WasapiAudioPatch.cpp`
- Test: `tests/WasapiEndpointTests.cpp`
- Test: `tests/ExclusiveAudioEngineTests.cpp`
- Test: `tests/WasapiAudioPatchTests.cpp`

- [x] **Step 1: Add failing endpoint-policy and propagation tests**

Extend `WasapiEndpointTests` so `Create` receives a configured duration and proves all three policies:

```cpp
// configured 10 ms, driver minimum 3 ms
auto endpoint = WasapiEndpoint::Create(
    std::move(api), 100'000, &attempted, &failure);
failures += Expect(
    raw->initialize_durations == std::vector<REFERENCE_TIME>{100'000} &&
        raw->initialize_periodicities ==
            std::vector<REFERENCE_TIME>{100'000} &&
        endpoint->initialization().configured_duration == 100'000 &&
        endpoint->initialization().requested_duration == 100'000,
    "configured 10 ms duration and periodicity");
```

Also prove `configured = 0` and a configured value below the driver minimum both request the minimum. Set the fake actual buffer to the matching frame count so the 10 ms case also proves post-initialize validation uses `requested_duration`, not `minimum_period`.

Extend the patch tests so the fake `StartExclusiveAudioEngineFn` captures `100'000`, and extend startup diagnostics expectations with:

```text
configured_duration_100ns=100000
configured_duration_ms=10.000
```

- [x] **Step 2: Verify RED**

```powershell
& $env:ComSpec /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build build-msvc32-latest --target WasapiEndpointTests ExclusiveAudioEngineTests WasapiAudioPatchTests'
```

Expected: compilation fails on the new duration parameter/field before production code is changed.

- [x] **Step 3: Implement the duration flow**

Add `configured_duration` to `EndpointInitialization`. Pass a `REFERENCE_TIME configured_duration` through `StartProductionExclusiveAudioEngine`, `ExclusiveAudioEngine::StartAndWait`, and `WasapiEndpoint::Create`.

In `WasapiEndpoint::Initialize`, select the initial request exactly once:

```cpp
auto requested = std::max(
    initialization_.minimum_period,
    initialization_.configured_duration);
initialization_.requested_duration = requested;
result = api_->InitializeExclusiveEvent(
    requested,
    requested,
    output_format);
```

Keep the existing alignment retry unchanged after that selection. For non-aligned success, validate `actual_buffer_frames` against `requested_duration`:

```cpp
ReferenceTimeToFramesCeil(
    initialization_.requested_duration,
    kOutputSampleRate)
```

Capture the config once when constructing the process-lifetime production detour state, convert milliseconds with `10'000` 100-ns units per millisecond, and forward it to startup. Add configured duration in both 100-ns and milliseconds to startup logs.

- [x] **Step 4: Verify focused GREEN and commit**

```powershell
& $env:ComSpec /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build build-msvc32-latest --target WasapiEndpointTests ExclusiveAudioEngineTests WasapiAudioPatchTests'
ctest --test-dir build-msvc32-latest -R "WasapiEndpointTests|ExclusiveAudioEngineTests|WasapiAudioPatchTests" --output-on-failure
git add -- WasapiEndpoint.h WasapiEndpoint.cpp ExclusiveAudioEngine.h ExclusiveAudioEngine.cpp WasapiAudioPatchInternal.h WasapiAudioPatch.cpp tests/WasapiEndpointTests.cpp tests/ExclusiveAudioEngineTests.cpp tests/WasapiAudioPatchTests.cpp
git commit -m "fix: use a stable WASAPI exclusive buffer"
```

Expected: fixed 10 ms, explicit minimum, below-minimum clamping, alignment retry, propagation, and diagnostics all pass.

---

### Task 3: Verify and Prepare the 10 ms Runtime Retest

- [ ] **Step 1: Run product and complete automated verification**

```powershell
& $env:ComSpec /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build build-msvc32-latest --target iDmacDrv32 ConfigGUI ConfigFeatureTests WasapiEndpointTests ExclusiveAudioEngineTests WasapiAudioPatchTests'
ctest --test-dir build-msvc32-latest --output-on-failure
git status --short
git diff HEAD~2 --check
```

Expected: product and GUI build, complete CTest is green, and the only uncommitted files are intentional plan checkbox/evidence updates.

- [ ] **Step 2: Back up and deploy without touching unrelated runtime settings**

Stop the game, create a timestamped directory under `H:\gc\deploy-backups`, copy the current DLL and config into it, deploy `build-msvc32-latest\iDmacDrv32.dll`, and add only this missing strict key under runtime `[experimental]`:

```toml
wasapi_exclusive_buffer_ms = 10
```

Keep `enable_wasapi_exclusive_audio = true`. Verify deployed/built SHA256 hashes match.

- [ ] **Step 3: Operator retest gate**

Launch `game471.exe` at 120 FPS and confirm the startup log reports configured/requested duration near 10 ms and an actual frame count near 441 (or the driver's aligned equivalent). The operator judges whether the prior crackling/chopped distortion is gone. Do not claim success from automated checks alone.

# Generic ASIO® Low-Latency Audio Backend Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a generic Win32 ASIO output backend with editable driver selection, exact frame-buffer control, isolated save-time validation, authoritative runtime fallback to WASAPI, and the existing physical-presentation song clock.

**Architecture:** The DirectSound facade becomes a lazy game-facing shell: `DirectSoundCreate8` creates it, and `SetCooperativeLevel` starts the configured output backend with the real game `HWND`. WASAPI and ASIO share a backend-neutral `AudioRenderCore`; ASIO hosts any registered 32-bit `IASIO` driver through project-owned registry, lifecycle, callback, clock, conversion, probe, and diagnostics layers. ConfigGUI never loads vendor code: it delegates inspection and final validation to a bounded Win32 helper before atomically saving.

**Tech Stack:** C++23, Win32/x86, DirectSound 8 facade, miniaudio, WASAPI exclusive mode, Steinberg ASIO SDK 2.3.4+, COM, ImGui, CMake 3.31+, Ninja/MSVC x86, CTest, CC0-1.0 project source, GPL-3.0-only ASIO-enabled distributions.

## Global Constraints

- The approved design is `docs/superpowers/specs/2026-08-08-generic-asio-low-latency-audio-design.md`.
- Work, tests, plans, and commits belong in `H:\gc\artifacts\GCLoader`. Do not deploy to or mutate the runtime tree `H:\gc` without a later explicit operator authorization.
- Target Windows 10+ and Win32/x86 only. Enumerate only `HKLM\SOFTWARE\ASIO` through the 32-bit registry view.
- Require `GC_ASIO_SDK_DIR` as a CMake cache path or environment variable. The cache value wins. Do not fetch or vendor the ASIO SDK.
- The approved local SDK is `H:\gc\artifacts\ASIOSDK` and must identify ASIO SDK 2.3.4 or newer.
- Compile only the external SDK interface headers `common/asio.h`, `common/asiosys.h`, and `common/iasiodrv.h`. Do not compile Steinberg sample-host code or rs_asio.
- Project-owned source stays CC0-1.0. The ASIO-enabled combined program and its matching source bundle are distributed under GPL-3.0-only; third-party components retain their own licenses.
- Keep the ASIO interface generic. Do not add Xonar branches, device-name conditionals, vendor DLL paths, automatic endpoint matching, or a driver whitelist.
- Driver selection is the exact user-supplied registry subkey name. Installed drivers and common names are editable suggestions only.
- Use 48,000 Hz, two adjacent output channels, and the exact configured ASIO frame count. Never clamp or round an unsupported value.
- Preserve WASAPI buffer configuration in milliseconds and ASIO buffer configuration in frames.
- ConfigGUI must prevent Save when selected ASIO validation fails. Runtime repeats the same capability validation and owns the final verdict.
- A pre-commit ASIO failure falls back once to the configured WASAPI-exclusive path. A committed ASIO stream never hot-switches clocks after playback starts.
- Preserve the DirectSound facade, miniaudio voice semantics, `AudioCursorTimeline`, and the July 28 physical-presentation shared song clock. Never publish the future render tail as the play cursor.
- Keep ASIO callbacks allocation-free, lock-free, exception-free, and free of file, console, or formatted logging. Defer `directProcess == ASIOFalse` through a pre-created real-time worker.
- Do not call driver control panels, add ASIO input, add recording/monitoring, or download/install ASIO4ALL, FlexASIO, or another driver.
- Runtime/static evidence and gameplay acceptance are separate. Do not claim lower latency until stable hardware playback and preferably a reproducible physical measurement exist.
- Run configure/build commands after `C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat` establishes the x86 environment.
- Prefix each new project-owned ASIO source/test file with `SPDX-License-Identifier: CC0-1.0`.
- Use behavior tests and injected interfaces; do not add source-text, regex, mirrored-production, tautological, or nominal-coverage tests.

---

## File and Responsibility Map

| File | Responsibility |
|---|---|
| `cmake/AsioSdk.cmake` | Resolve and validate the external SDK; define `gc_asio_sdk` and the official logo path. |
| `tests/CMake/AsioSdkFixture/CMakeLists.txt` | Minimal nested project for SDK configure-contract tests. |
| `tests/CMake/AsioSdkConfigureTests.cmake` | Prove missing, incomplete, old, environment, and cache-path behavior. |
| `LICENSE.md` and `LICENSES/*` | State CC0 file licensing and GPL scope for ASIO-enabled combined binaries. |
| `THIRD_PARTY_NOTICES.md` | Pin dependency revisions, licenses, Steinberg attribution, and ASIO trademark text. |
| `src/Config/AudioConfig.h/.cpp` | Backend enum/name and static backend-specific validation. |
| `src/Config/config.h/.cpp` | Strict schema fields and runtime getters. |
| `src/Config/ConfigDocument.h/.cpp` | Legacy WASAPI-boolean migration and canonical persistence flags. |
| `src/Audio/Asio/AsioTypes.h` | Stable project-owned ASIO request, capability, failure, channel, and diagnostics types. |
| `src/Audio/Asio/AsioBufferRules.h/.cpp` | Exact ASIO buffer metadata and frame-count validation. |
| `src/Audio/Asio/AsioSampleConverter.h/.cpp` | Stereo-float to supported planar little-endian ASIO formats. |
| `src/Audio/Asio/AsioDriver.h/.cpp` | Narrow mockable `IASIO` wrapper and COM factory. |
| `src/Audio/Asio/AsioDriverCatalog.h/.cpp` | Unicode-safe 32-bit registry enumeration and exact-name resolution. |
| `src/Audio/Asio/AsioSession.h/.cpp` | Ordered init/rate/channel/buffer/latency preparation and same-thread cleanup. |
| `src/Audio/Mixer/PresentedOutputClock.h` | Backend-neutral clock ownership interface held by `AudioRenderCore`. |
| `src/Audio/Asio/AsioClock.h/.cpp` | Priming, block/sample validation, future placement, wrap-safe presentation projection. |
| `src/Audio/Asio/AsioCallbackRuntime.h/.cpp` | Global callback router, ASIO messages, inline/deferred dispatch, and real-time worker. |
| `src/Audio/Asio/AsioProbeProtocol.h/.cpp` | Bounded binary request/result codec shared by helper and GUI. |
| `src/Audio/Asio/AsioProbeClient.h/.cpp` | No-shell Win32 process, pipe, timeout, Job Object, and result handling. |
| `tools/AsioProbe/Main.cpp` | Hidden-window out-of-process driver inspection/validation. |
| `tools/ConfigGUI/AudioBackendEditorModel.h/.cpp` | Installed/common/editable choices, probe state, channels, and save transaction. |
| `tools/ConfigGUI/AsioLogoTexture.h/.cpp` | WIC decode and D3D11 texture lifetime for the unmodified external compatible logo. |
| `tools/ConfigGUI/Main.cpp` | ImGui backend selector, ASIO® branding, controls, and final save gate. |
| `src/Audio/Mixer/AudioRenderCore.h/.cpp` | Shared preallocated mixer/render block consumed by WASAPI and ASIO. |
| `src/Audio/Mixer/AudioRenderCoreInternal.h` | Pure render-finalization seam used by the core and behavior tests. |
| `src/Audio/Wasapi/WasapiPresentedOutputClock.h/.cpp` | Existing QPC projection adapted to the core-owned clock interface. |
| `src/Audio/Wasapi/ExclusiveAudioEngine.h/.cpp` | WASAPI scheduling/clock adapter over `AudioRenderCore`. |
| `src/Audio/Asio/AsioOutputBackend.h/.cpp` | Runtime ASIO lifecycle, render conversion, clock publication, faults, and counters; compiled into `gc_audio`, not low-level `gc_asio`. |
| `src/Audio/AudioBackendController.h/.cpp` | Lazy backend selection and ASIO-to-WASAPI pre-commit fallback. |
| `src/Audio/AudioPatch.h/.cpp` | Generic `DirectSoundCreate8` hook, diagnostics, and process-lifetime controller. |
| `src/Audio/DirectSound/DirectSoundFacade.h/.cpp` | Start the controller from valid priority `SetCooperativeLevel(HWND, ...)`. |
| `cmake/PackageCorrespondingSource.cmake` | Produce the exact clean-revision GPL corresponding-source archive outside git. |
| `docs/reverse-engineering/asio-runtime-validation.md` | Hardware probe, build identity, clock/callback evidence, and user acceptance. |

---

### Task 1: Establish the external SDK and license boundary

**Files:**
- Create: `cmake/AsioSdk.cmake`
- Create: `tests/CMake/CMakeLists.txt`
- Create: `tests/CMake/AsioSdkFixture/CMakeLists.txt`
- Create: `tests/CMake/AsioSdkConfigureTests.cmake`
- Create: `LICENSE.md`
- Create: `LICENSES/CC0-1.0.txt`
- Create: `LICENSES/GPL-3.0-only.txt`
- Create: `THIRD_PARTY_NOTICES.md`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: the local dual-licensed ASIO SDK root and existing top-level CMake configuration.
- Produces: required cache variable `GC_ASIO_SDK_DIR`, interface target `gc_asio_sdk`, cache file `GC_ASIO_COMPATIBLE_LOGO`, configure-contract tests, and distribution license files.

- [ ] **Step 1: Write the nested configure-contract test first**

The fixture contains only:

```cmake
cmake_minimum_required(VERSION 3.31)
project(AsioSdkFixture LANGUAGES CXX)
list(APPEND CMAKE_MODULE_PATH "${GC_PROJECT_SOURCE_DIR}/cmake")
include(AsioSdk)
gc_require_asio_sdk()
```

`AsioSdkConfigureTests.cmake` creates isolated build-tree roots and invokes the fixture five times:

1. no cache value and an unset environment variable: configure must fail with `GC_ASIO_SDK_DIR is required`;
2. a root missing `common/iasiodrv.h`: fail naming that file;
3. a root whose `changes.txt` starts at 2.3.3: fail with `ASIO SDK 2.3.4 or newer`;
4. `GC_ASIO_SDK_DIR` supplied only through the environment: configure succeeds;
5. valid but different environment/cache roots: configure succeeds and the generated probe file records the cache root.

Register it in `tests/CMake/CMakeLists.txt`:

```cmake
add_test(
    NAME AsioSdkConfigureTests
    COMMAND ${CMAKE_COMMAND}
        -DGC_PROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}
        -DGC_TEST_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}/asio-sdk-contract
        -DGC_REAL_ASIO_SDK_DIR=${GC_ASIO_SDK_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/AsioSdkConfigureTests.cmake
)
```

Add `add_subdirectory(CMake)` to `tests/CMakeLists.txt`, then run the script directly.

```powershell
$env:GC_ASIO_SDK_DIR = 'H:\gc\artifacts\ASIOSDK'
cmake -DGC_PROJECT_SOURCE_DIR=H:\gc\artifacts\GCLoader -DGC_TEST_BINARY_DIR=H:\gc\artifacts\GCLoader\build-msvc32-debug\tests\CMake\asio-sdk-contract -DGC_REAL_ASIO_SDK_DIR=$env:GC_ASIO_SDK_DIR -P tests\CMake\AsioSdkConfigureTests.cmake
```

Expected RED: the fixture cannot include `AsioSdk.cmake`.

- [ ] **Step 2: Implement the required SDK resolver**

`cmake/AsioSdk.cmake` must:

```cmake
function(gc_require_asio_sdk)
    if(NOT DEFINED GC_ASIO_SDK_DIR)
        set(GC_ASIO_SDK_DIR "" CACHE PATH "Root of Steinberg ASIO SDK 2.3.4+")
    endif()
    if(GC_ASIO_SDK_DIR STREQUAL "" AND
            NOT "$ENV{GC_ASIO_SDK_DIR}" STREQUAL "")
        set(GC_ASIO_SDK_DIR "$ENV{GC_ASIO_SDK_DIR}"
            CACHE PATH "Root of Steinberg ASIO SDK 2.3.4+" FORCE)
    endif()
    if(GC_ASIO_SDK_DIR STREQUAL "")
        message(FATAL_ERROR
            "GC_ASIO_SDK_DIR is required as a cache path or environment variable")
    endif()

    cmake_path(ABSOLUTE_PATH GC_ASIO_SDK_DIR NORMALIZE
        OUTPUT_VARIABLE asio_root)
    foreach(required IN ITEMS
            README.md LICENSE.txt changes.txt
            common/asio.h common/asiosys.h common/iasiodrv.h
            "Steinberg ASIO Logo Artwork/ASIO-compatible-logo-Steinberg-TM-BW.png")
        if(NOT EXISTS "${asio_root}/${required}")
            message(FATAL_ERROR
                "GC_ASIO_SDK_DIR is incomplete; missing ${required}")
        endif()
    endforeach()
    file(READ "${asio_root}/changes.txt" asio_changes LIMIT 4096)
    string(REGEX MATCH
        "Changes in ASIO ([0-9]+)\\.([0-9]+)(\\.([0-9]+))?"
        asio_version_line "${asio_changes}")
    if(asio_version_line STREQUAL "")
        message(FATAL_ERROR "Cannot identify ASIO SDK version from changes.txt")
    endif()
    set(asio_version_major "${CMAKE_MATCH_1}")
    set(asio_version_minor "${CMAKE_MATCH_2}")
    if(CMAKE_MATCH_4 STREQUAL "")
        set(asio_version_patch 0)
    else()
        set(asio_version_patch "${CMAKE_MATCH_4}")
    endif()
    if(asio_version_major LESS 2 OR
            (asio_version_major EQUAL 2 AND asio_version_minor LESS 3) OR
            (asio_version_major EQUAL 2 AND asio_version_minor EQUAL 3 AND
             asio_version_patch LESS 4))
        message(FATAL_ERROR "ASIO SDK 2.3.4 or newer is required")
    endif()

    add_library(gc_asio_sdk INTERFACE)
    target_include_directories(gc_asio_sdk INTERFACE "${asio_root}/common")
    set(GC_ASIO_SDK_DIR "${asio_root}" CACHE PATH
        "Root of Steinberg ASIO SDK 2.3.4+" FORCE)
    set(GC_ASIO_COMPATIBLE_LOGO
        "${asio_root}/Steinberg ASIO Logo Artwork/ASIO-compatible-logo-Steinberg-TM-BW.png"
        CACHE FILEPATH "Official unmodified ASIO Compatible logo" FORCE)
endfunction()
```

Include and call this module immediately after `ProjectOptions` and before fetched dependencies. This ensures a missing SDK fails early.

- [ ] **Step 3: Formalize license scope and notices**

Add the unmodified canonical CC0 1.0 Universal legal code from `https://creativecommons.org/publicdomain/zero/1.0/legalcode.txt` as `LICENSES/CC0-1.0.txt` and GNU GPL version 3 from `https://www.gnu.org/licenses/gpl-3.0.txt` as `LICENSES/GPL-3.0-only.txt`.

`LICENSE.md` states exactly:

```text
Project-authored source files are dedicated under CC0-1.0 unless a file says
otherwise. External components retain their stated licenses.

When GCLoader is compiled with the Steinberg ASIO SDK under the SDK's GPLv3
option, the resulting ASIO-enabled combined program is distributed under
GPL-3.0-only. This does not replace the CC0 dedication on independently
authored project files.

The proprietary game, its data, and third-party ASIO drivers are not included
in this repository or licensed by this notice.
```

`THIRD_PARTY_NOTICES.md` records exact source URL, pin, and license for:

- ASIO SDK 2.3.4, GPLv3 option, plus “ASIO is a registered trademark of Steinberg Media Technologies GmbH.”;
- MinHook `c3fcafdc10146beb5919319d0683e44e3c30d537`, BSD-2-Clause;
- miniaudio `9634bedb5b5a2ca38c1ee7108a9358a4e233f14d`, public-domain/MIT option;
- toml++ `v3.4.0`, MIT;
- SafetyHook `v0.7.0`, Boost-1.0;
- Zydis at the revision resolved by the pinned SafetyHook tree, MIT;
- reflect-cpp `v0.25.0`, MIT;
- plog `1.1.11`, MIT;
- Dear ImGui `v1.92.8`, MIT.

Copy `LICENSE.md`, both complete license texts, `THIRD_PARTY_NOTICES.md`, and the SDK's `LICENSE.txt` into `${GC_DIST_DIR}/licenses` with `configure_file(COPYONLY)`. Do not copy an SDK header or source into the repository.

- [ ] **Step 4: Configure and run the CMake contract**

```powershell
$env:GC_ASIO_SDK_DIR = 'H:\gc\artifacts\ASIOSDK'
& $env:ComSpec /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --preset msvc32-debug'
ctest --test-dir build-msvc32-debug --output-on-failure -R '^AsioSdkConfigureTests$'
```

Expected: configure succeeds with `GC_ASIO_SDK_DIR` normalized to the local SDK, and all five nested cases pass.

- [ ] **Step 5: Commit the dependency and license boundary**

```powershell
git add -- CMakeLists.txt cmake/AsioSdk.cmake tests/CMakeLists.txt tests/CMake/CMakeLists.txt tests/CMake/AsioSdkFixture/CMakeLists.txt tests/CMake/AsioSdkConfigureTests.cmake LICENSE.md LICENSES/CC0-1.0.txt LICENSES/GPL-3.0-only.txt THIRD_PARTY_NOTICES.md
git commit -m "Require external ASIO SDK and record licenses"
```

---

### Task 2: Replace the WASAPI Boolean with a strict backend schema

**Files:**
- Create: `src/Config/AudioConfig.h`
- Create: `src/Config/AudioConfig.cpp`
- Modify: `src/Config/CMakeLists.txt`
- Modify: `src/Config/config.h`
- Modify: `src/Config/config.cpp`
- Modify: `src/Config/ConfigDocument.h`
- Modify: `src/Config/ConfigDocument.cpp`
- Modify: `tools/ConfigGUI/Main.cpp`
- Modify: `config.toml`
- Modify: `tests/Config/ConfigFeatureTests.cpp`
- Modify: `tests/Config/ConfigDocumentTests.cpp`

**Interfaces:**
- Consumes: strict reflect-cpp/TOML parsing and existing atomic configuration persistence.
- Produces: `gc::config::AudioBackend`, five backend/audio fields, runtime getters, and legacy document migration with a durable migration flag.

- [ ] **Step 1: Add failing schema and migration assertions**

Change the distributed required-key table to require:

```text
audio_backend
wasapi_exclusive_buffer_ms
asio_driver_name
asio_buffer_frames
asio_output_base_channel
```

Remove `enable_wasapi_exclusive_audio` from that new-schema table. Add exact parsing/round-trip cases for `directsound`, `wasapi_exclusive`, and `asio`; reject the unknown backend string `auto`. With ASIO selected, also reject empty or over-1,024-byte driver text, zero or above-`LONG_MAX` frames, and a base channel above `LONG_MAX - 1`.

In `ConfigDocumentTests.cpp`, preserve `wasapi_exclusive_buffer_ms` and replace only the four new backend/ASIO assignments with:

```toml
enable_wasapi_exclusive_audio = false
```

and then `true`. Assert false migrates to `AudioBackend::directsound`, true migrates to `AudioBackend::wasapi_exclusive`, the three ASIO fields receive inactive defaults, and `migrations.audio_backend` is true. A document containing both the legacy Boolean and `audio_backend` must fail with `both audio_backend and legacy`. A new-schema document missing one ASIO field remains a strict failure.

Run:

```powershell
cmake --build --preset msvc32-debug --target ConfigFeatureTests ConfigDocumentTests
```

Expected RED: `AudioBackend` and the new fields do not exist.

- [ ] **Step 2: Declare the audio configuration contract**

Create `AudioConfig.h`:

```cpp
#pragma once
// SPDX-License-Identifier: CC0-1.0

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

namespace gc::config {

enum class AudioBackend : std::uint8_t {
    directsound,
    wasapi_exclusive,
    asio,
};

const char* AudioBackendName(AudioBackend) noexcept;
std::expected<void, std::string> ValidateAudioBackendSettings(
    AudioBackend backend,
    std::string_view asio_driver_name,
    std::uint32_t asio_buffer_frames,
    std::uint32_t asio_output_base_channel) noexcept;

} // namespace gc::config
```

Validation accepts inactive ASIO defaults for DirectSound/WASAPI. For ASIO it requires a nonempty valid UTF-8 name of at most 1,024 encoded bytes, `0 < frames <= LONG_MAX`, and `base_channel <= LONG_MAX - 1`. Dynamic registration, channel count, format, and buffer acceptance remain outside config validation.

In `ExperimentalConfig` replace the Boolean and add:

```cpp
rfl::Rename<"audio_backend", gc::config::AudioBackend>
    audio_backend = gc::config::AudioBackend::directsound;
rfl::Rename<"wasapi_exclusive_buffer_ms", unsigned long>
    wasapi_exclusive_buffer_ms = 10;
rfl::Rename<"asio_driver_name", std::string>
    asio_driver_name;
rfl::Rename<"asio_buffer_frames", unsigned long>
    asio_buffer_frames = 0;
rfl::Rename<"asio_output_base_channel", unsigned long>
    asio_output_base_channel = 0;
```

Add getters `GetAudioBackend()`, `GetAsioDriverName()`, `GetAsioBufferFrames()`, and `GetAsioOutputBaseChannel()`; retain `GetWasapiExclusiveBufferMs()`. Until Task 13 generalizes the hook, retain a clearly marked transitional `GetEnableWasapiExclusiveAudio()` returning `backend != directsound`, so intermediate commits keep the current facade buildable; Task 13 must delete it.

- [ ] **Step 3: Implement syntax-level legacy migration**

Replace the single Boolean in `ParsedInputConfigDocument` with:

```cpp
struct ConfigDocumentMigrations {
    bool registry_paths{};
    bool audio_backend{};
    bool any() const noexcept {
        return registry_paths || audio_backend;
    }
};

struct ParsedInputConfigDocument {
    InputConfig config;
    ConfigDocumentMigrations migrations;
};
```

Add `MigrateLegacyAudioBackend(toml::table&)` before reflect-cpp parsing. It:

1. requires an `[experimental]` table;
2. rejects simultaneous `enable_wasapi_exclusive_audio` and `audio_backend`;
3. if the legacy key is present, requires a Boolean, inserts the exact lowercase backend string, erases the legacy key, and inserts absent ASIO fields with `""`, `0`, `0`;
4. leaves a new-schema document untouched;
5. reports whether migration occurred.

Rename `ConfigManager::registry_schema_migrated_` to `document_migrated_`. Pass `result->migrations.any()` into `PrepareAndPersistGameSystemPathConfiguration` and include that Boolean in `must_persist`, so the game process atomically canonicalizes a legacy audio document before installing the hook.

- [ ] **Step 4: Update the distributed configuration and verify GREEN**

Use:

```toml
[experimental]
audio_backend = 'directsound'
wasapi_exclusive_buffer_ms = 10
asio_driver_name = ''
asio_buffer_frames = 0
asio_output_base_channel = 0
```

Make the existing GUI checkbox an intermediate enum adapter so this commit also remains buildable: checked maps to `wasapi_exclusive`, unchecked maps to `directsound`, and an already loaded `asio` value is preserved unless the user changes the checkbox. Change GUI load to `ParseAndValidateInputConfigDocument`, initialize `dirty` from `migrations.any()`, and save through `WriteInputConfigAtomically`; a legacy file therefore opens and can be canonically saved without a truncate window. Task 10 replaces this adapter with the complete three-way editor and wraps the atomic writer with the dynamic ASIO gate.

Run:

```powershell
cmake --build --preset msvc32-debug --target ConfigFeatureTests ConfigDocumentTests ConfigGUI iDmacDrv32
ctest --test-dir build-msvc32-debug --output-on-failure -R '^(ConfigFeatureTests|ConfigDocumentTests)$'
```

Expected: both focused tests pass, legacy documents persist canonically through the existing atomic writer, and unknown/missing new fields remain strict.

- [ ] **Step 5: Commit the backend schema**

```powershell
git add -- src/Config/AudioConfig.h src/Config/AudioConfig.cpp src/Config/CMakeLists.txt src/Config/config.h src/Config/config.cpp src/Config/ConfigDocument.h src/Config/ConfigDocument.cpp tools/ConfigGUI/Main.cpp config.toml tests/Config/ConfigFeatureTests.cpp tests/Config/ConfigDocumentTests.cpp
git commit -m "Add strict audio backend configuration"
```

---

### Task 3: Implement exact ASIO buffer rules

**Files:**
- Create: `src/Audio/Asio/CMakeLists.txt`
- Create: `src/Audio/Asio/AsioTypes.h`
- Create: `src/Audio/Asio/AsioBufferRules.h`
- Create: `src/Audio/Asio/AsioBufferRules.cpp`
- Create: `tests/Audio/AsioBufferRulesTests.cpp`
- Modify: `src/Audio/CMakeLists.txt`
- Modify: `tests/Audio/CMakeLists.txt`

**Interfaces:**
- Consumes: `gc_asio_sdk` and Steinberg `ASIOError`/`ASIOSampleType` definitions.
- Produces: low-level static `gc_asio` library, project-owned `AsioBufferLimits`, `AsioBufferRuleError`, and `ValidateAsioBufferFrames`; the target never depends on `gc_audio` or mixer/facade types.

- [ ] **Step 1: Write the buffer-rule matrix**

Declare the expected API in the missing header:

```cpp
struct AsioBufferLimits {
    long minimum{};
    long maximum{};
    long preferred{};
    long granularity{};
};

enum class AsioBufferRuleError : std::uint8_t {
    invalid_metadata,
    below_minimum,
    above_maximum,
    not_power_of_two,
    not_granular,
};

std::expected<void, AsioBufferRuleError> ValidateAsioBufferFrames(
    const AsioBufferLimits&,
    std::uint32_t requested) noexcept;
```

The test table covers:

- Xonar `{192, 2400, 192, 1}` accepting 192, 193, and 2400 but rejecting 191 and 2401;
- fixed `{256, 256, 256, 0}` accepting only 256;
- power-of-two `{64, 1024, 256, -1}` accepting 64/128/1024 and rejecting 96;
- granular `{64, 1024, 256, 64}` accepting 64/128/1024 and rejecting 96;
- invalid zero/negative sizes, preferred outside range, minimum above maximum, fixed metadata with nonzero granularity, non-fixed granularity zero, and granularity below -1.

Build `AsioBufferRulesTests` and observe RED because the headers do not exist.

- [ ] **Step 2: Add the focused ASIO target and types**

Put `AsioBufferLimits` in `AsioTypes.h` so later reports can use it without a header cycle. `src/Audio/Asio/CMakeLists.txt` starts with:

```cmake
add_library(gc_asio STATIC
    AsioBufferRules.cpp
)
target_include_directories(gc_asio PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(gc_asio PUBLIC gc_asio_sdk ole32 advapi32 avrt winmm)
```

Add `add_subdirectory(Asio)` before `add_library(gc_audio ...)`. Register `AsioBufferRulesTests` separately and link it to `gc_asio`.

After `gc_audio` is declared, link it publicly to `gc_asio`. Keep low-level driver/session/protocol code in `gc_asio`; later `AsioOutputBackend.cpp`, which consumes `AudioRenderCore` and `IAudioEngineServices`, is added to `gc_audio` instead. This one-way edge prevents a static-library cycle.

`AsioTypes.h` owns project types only and includes `iasiodrv.h`. Add `AsioDriverRegistration { std::string registry_name; CLSID clsid; }`. Define a stable failure payload:

```cpp
enum class AsioFailureStage : std::uint8_t {
    none,
    registry,
    clsid,
    com,
    init,
    identity,
    channels,
    sample_rate,
    buffer_metadata,
    channel_info,
    output_ready_probe,
    callback_prepare,
    create_buffers,
    latency,
    render_core,
    start,
    startup_clock,
    callback,
    conversion,
    runtime_clock,
    output_ready,
    stop,
    dispose,
    restore_sample_rate,
    protocol,
    process_launch,
    process_job,
    probe_timeout,
    probe_crash,
};

enum class AsioResultDomain : std::uint8_t {
    none,
    asio,
    hresult,
    win32,
};

struct AsioFailure {
    AsioFailureStage stage{};
    AsioResultDomain domain{};
    std::int64_t result{};
    std::string driver_message;
    std::string detail;
};
```

Preserve signed ASIO/HRESULT values losslessly in `result`; never flatten a Win32 code into an ASIO result.

- [ ] **Step 3: Implement strict validation without adjustment**

Implement the rules in this order:

1. reject internally inconsistent metadata;
2. reject range violations;
3. fixed size accepts only the sole value;
4. `-1` requires a power of two;
5. `1` accepts every in-range integer;
6. values above `1` require `requested % granularity == 0`.

Return an error only; never return a replacement frame count.

- [ ] **Step 4: Verify and commit**

```powershell
cmake --build --preset msvc32-debug --target AsioBufferRulesTests
ctest --test-dir build-msvc32-debug --output-on-failure -R '^AsioBufferRulesTests$'
git add -- src/Audio/Asio/CMakeLists.txt src/Audio/Asio/AsioTypes.h src/Audio/Asio/AsioBufferRules.h src/Audio/Asio/AsioBufferRules.cpp src/Audio/CMakeLists.txt tests/Audio/AsioBufferRulesTests.cpp tests/Audio/CMakeLists.txt
git commit -m "Validate exact ASIO buffer sizes"
```

---

### Task 4: Convert stereo float into supported planar ASIO samples

**Files:**
- Create: `src/Audio/Asio/AsioSampleConverter.h`
- Create: `src/Audio/Asio/AsioSampleConverter.cpp`
- Create: `tests/Audio/AsioSampleConverterTests.cpp`
- Modify: `src/Audio/Asio/CMakeLists.txt`
- Modify: `tests/Audio/CMakeLists.txt`

**Interfaces:**
- Consumes: interleaved stereo float blocks from the future `AudioRenderCore`.
- Produces: `AsioBytesPerSample`, `IsSupportedAsioOutputType`, `ClearAsioChannel`, and `ConvertFloatStereoChannelToAsio`.

- [ ] **Step 1: Write byte-exact conversion tests**

Declare:

```cpp
std::optional<std::size_t> AsioBytesPerSample(
    ASIOSampleType) noexcept;
bool IsSupportedAsioOutputType(ASIOSampleType) noexcept;
bool ClearAsioChannel(
    ASIOSampleType,
    std::span<std::byte> destination,
    std::uint32_t frames) noexcept;
bool ConvertFloatStereoChannelToAsio(
    std::span<const float> interleaved_stereo,
    std::uint32_t channel,
    ASIOSampleType,
    std::span<std::byte> destination) noexcept;
```

Use the input `{-1.25F, 1.25F, -1.0F, 1.0F, 0.0F, 0.5F}` and assert:

- channel selection deinterleaves left/right without an intermediate buffer;
- out-of-range values clip;
- `ASIOSTInt16LSB`, packed `ASIOSTInt24LSB`, `ASIOSTInt32LSB`, `ASIOSTFloat32LSB`, and `ASIOSTFloat64LSB` have exact little-endian output;
- `ASIOSTInt32LSB16/18/20/24` are right-aligned signed samples with the unused most-significant bits sign-extended;
- packed 24-bit Xonar zero is exactly three zero bytes;
- undersized output, odd interleaved input, channel > 1, MSB, and DSD types fail without writing beyond the supplied span;
- `ClearAsioChannel` zeros exactly `frames * bytes_per_sample`.

- [ ] **Step 2: Implement the documented SDK representations**

Use the SDK specification rules:

- `Int16LSB` is signed 16-bit;
- `Int24LSB` is signed packed 24-bit;
- `Int32LSB` is a left-aligned 32-bit container, so normal full-scale conversion uses all 32 bits;
- `Int32LSB16/18/20/24` scale to the named signed width, right-align in a 32-bit word, and sign-extend;
- float output is clipped to `[-1.0, 1.0]` and stored as IEEE little-endian.

For an integer width `N`, compute finite in-range samples as `round(value * 2^(N-1))`, then clamp to `[-2^(N-1), 2^(N-1)-1]`; this fixes `-1.0`, `0.0`, `0.5`, and `1.0` byte expectations across all widths. Validate the entire input/destination shape and pre-scan for non-finite input before the first write. A caller that receives false clears both output channels.

Use `memcpy` for 32/64-bit stores so driver buffers need not be naturally aligned. The function is `noexcept`, contains no allocation, and returns false for every unsupported type.

- [ ] **Step 3: Verify every format and commit**

```powershell
cmake --build --preset msvc32-debug --target AsioSampleConverterTests
ctest --test-dir build-msvc32-debug --output-on-failure -R '^AsioSampleConverterTests$'
git add -- src/Audio/Asio/AsioSampleConverter.h src/Audio/Asio/AsioSampleConverter.cpp src/Audio/Asio/CMakeLists.txt tests/Audio/AsioSampleConverterTests.cpp tests/Audio/CMakeLists.txt
git commit -m "Convert mixer output to ASIO samples"
```

---

### Task 5: Discover and instantiate arbitrary 32-bit ASIO drivers

**Files:**
- Create: `src/Audio/Asio/AsioDriverCatalog.h`
- Create: `src/Audio/Asio/AsioDriverCatalog.cpp`
- Create: `src/Audio/Asio/AsioDriver.h`
- Create: `src/Audio/Asio/AsioDriver.cpp`
- Create: `tests/Audio/AsioDriverCatalogTests.cpp`
- Create: `tests/Audio/AsioDriverTests.cpp`
- Modify: `src/Audio/Asio/CMakeLists.txt`
- Modify: `tests/Audio/CMakeLists.txt`

**Interfaces:**
- Consumes: exact UTF-8 config driver name and Win32 registry/COM.
- Produces: `IAsioRegistrySource`, `EnumerateAsioDrivers`, `ResolveAsioDriver`, `IAsioDriver`, `IAsioDriverFactory`, and production implementations.

- [ ] **Step 1: Write catalog behavior tests**

Use a fake `IAsioRegistrySource`:

```cpp
class IAsioRegistrySource {
public:
    virtual ~IAsioRegistrySource() = default;
    virtual std::expected<std::vector<AsioRegistryValue>, AsioFailure>
        Read32BitRegistrations() noexcept = 0;
};

struct AsioRegistryValue {
    std::wstring subkey_name;
    std::wstring clsid_text;
};

std::expected<std::vector<AsioDriverRegistration>, AsioFailure>
EnumerateAsioDrivers(IAsioRegistrySource&) noexcept;
std::expected<AsioDriverRegistration, AsioFailure>
ResolveAsioDriver(IAsioRegistrySource&, std::string_view utf8_name) noexcept;
```

Assert Unicode round trips, registry-name order is stable, exact lookup uses Windows case-insensitive semantics, malformed UTF-8/CLSID fails, duplicate case-folded names fail, and missing names fail. The production source accepts injected registry actions; assert its open call contains `KEY_WOW64_32KEY | KEY_READ`, opens only `HKLM\SOFTWARE\ASIO`, and therefore cannot see a registration exposed by the fake actions only for the 64-bit view.

- [ ] **Step 2: Declare the narrow driver wrapper**

`IAsioDriver` mirrors only the methods GCLoader uses:

```cpp
class IAsioDriver {
public:
    virtual ~IAsioDriver() = default;
    virtual ASIOBool Init(HWND) noexcept = 0;
    virtual void GetDriverName(char (&)[32]) noexcept = 0;
    virtual long GetDriverVersion() noexcept = 0;
    virtual void GetErrorMessage(char (&)[124]) noexcept = 0;
    virtual ASIOError Start() noexcept = 0;
    virtual ASIOError Stop() noexcept = 0;
    virtual ASIOError GetChannels(long*, long*) noexcept = 0;
    virtual ASIOError GetLatencies(long*, long*) noexcept = 0;
    virtual ASIOError GetBufferSize(long*, long*, long*, long*) noexcept = 0;
    virtual ASIOError CanSampleRate(ASIOSampleRate) noexcept = 0;
    virtual ASIOError GetSampleRate(ASIOSampleRate*) noexcept = 0;
    virtual ASIOError SetSampleRate(ASIOSampleRate) noexcept = 0;
    virtual ASIOError GetSamplePosition(
        ASIOSamples*, ASIOTimeStamp*) noexcept = 0;
    virtual ASIOError GetChannelInfo(ASIOChannelInfo*) noexcept = 0;
    virtual ASIOError CreateBuffers(
        ASIOBufferInfo*, long, long, ASIOCallbacks*) noexcept = 0;
    virtual ASIOError DisposeBuffers() noexcept = 0;
    virtual ASIOError Future(long selector, void*) noexcept = 0;
    virtual ASIOError OutputReady() noexcept = 0;
};

class IAsioDriverFactory {
public:
    virtual ~IAsioDriverFactory() = default;
    virtual std::expected<std::unique_ptr<IAsioDriver>, AsioFailure>
        Create(const CLSID&) noexcept = 0;
};
```

The production COM action test asserts `CoCreateInstance` receives the registration CLSID as both class ID and interface ID, matching Steinberg's Windows host contract, uses `CLSCTX_INPROC_SERVER`, wraps the returned `IASIO*`, forwards every method once, and releases it exactly once.

- [ ] **Step 3: Implement production registry and COM adapters**

Use checked `MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, ...)` and `WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, ...)`. Parse CLSIDs with `CLSIDFromString`. Do not read, accept, or load a configured DLL path.

Registry/config identity stays Unicode and exact. Separately, ASIO's fixed `char` driver/error/channel buffers are treated as bounded Windows ANSI display text: zero them before the call, measure at most the SDK array size even if the driver omits a terminator, convert through `CP_ACP` to UTF-16 and then checked UTF-8, and fall back to an escaped byte diagnostic if conversion fails. Never use driver-reported display text to select or re-resolve the registration. Add terminated, fully filled, and non-ASCII fake cases.

`ProductionAsioRegistrySource` enumerates subkeys and reads each `CLSID` value from the explicitly opened 32-bit view. Sort registrations with `CompareStringOrdinal(..., TRUE)` plus an ordinal case-sensitive tie-breaker, and use that same case-insensitive ordinal comparison for lookup/deduplication; do not apply the process locale. `ProductionAsioDriverFactory` calls:

```cpp
void* raw{};
const HRESULT result = CoCreateInstance(
    clsid, nullptr, CLSCTX_INPROC_SERVER, clsid, &raw);
```

and wraps `static_cast<IASIO*>(raw)` only on success.

- [ ] **Step 4: Verify and commit**

```powershell
cmake --build --preset msvc32-debug --target AsioDriverCatalogTests AsioDriverTests
ctest --test-dir build-msvc32-debug --output-on-failure -R '^(AsioDriverCatalogTests|AsioDriverTests)$'
git add -- src/Audio/Asio/AsioDriverCatalog.h src/Audio/Asio/AsioDriverCatalog.cpp src/Audio/Asio/AsioDriver.h src/Audio/Asio/AsioDriver.cpp src/Audio/Asio/CMakeLists.txt tests/Audio/AsioDriverCatalogTests.cpp tests/Audio/AsioDriverTests.cpp tests/Audio/CMakeLists.txt
git commit -m "Discover generic 32-bit ASIO drivers"
```

---

### Task 6: Prepare and tear down an ASIO session in the SDK-defined order

**Files:**
- Create: `src/Audio/Asio/AsioSession.h`
- Create: `src/Audio/Asio/AsioSession.cpp`
- Create: `src/Audio/Asio/AsioCapabilityProbe.h`
- Create: `src/Audio/Asio/AsioCapabilityProbe.cpp`
- Create: `tests/Audio/AsioSessionTests.cpp`
- Create: `tests/Audio/AsioCapabilityProbeTests.cpp`
- Modify: `src/Audio/Asio/AsioTypes.h`
- Modify: `src/Audio/Asio/CMakeLists.txt`
- Modify: `tests/Audio/CMakeLists.txt`

**Interfaces:**
- Consumes: an already-resolved driver registration, `IAsioDriver`, the exact configured frame/channel request, and optional callbacks.
- Produces: `AsioSession`, `AsioCapabilityReport`, and a side-effect-bounded inspection/validation operation shared by the helper and runtime.

- [ ] **Step 1: Write the ordered lifecycle tests first**

Add the project-owned request/report contract to `AsioTypes.h`:

```cpp
enum class AsioProbeMode : std::uint8_t {
    inspect,
    validate,
};

struct AsioStreamRequest {
    std::string driver_name;
    std::uint32_t buffer_frames{};
    std::uint32_t output_base_channel{};
};

inline constexpr std::uint32_t kMaxAsioReportedChannels = 256;

struct AsioChannelDescriptor {
    std::uint32_t index{};
    std::string name;
    ASIOSampleType sample_type{};
};

struct AsioCapabilityReport {
    AsioDriverRegistration registration;
    std::string reported_driver_name;
    long driver_version{};
    double original_sample_rate{};
    double sample_rate{};
    AsioBufferLimits buffer_limits;
    std::uint32_t input_channels{};
    std::vector<AsioChannelDescriptor> output_channels;
    std::uint32_t selected_base_channel{};
    std::uint32_t effective_buffer_frames{};
    std::uint32_t input_latency_frames{};
    std::uint32_t output_latency_frames{};
    bool output_ready_supported{};
    bool overload_reporting_supported{};
};
```

Drive a fake `IAsioDriver` with a call ledger. The successful validation order must be exactly:

```text
resolve -> CoCreateInstance -> init
getDriverName -> getDriverVersion -> future(kAsioCanReportOverload)
-> getChannels
getSampleRate -> canSampleRate(48000) -> setSampleRate(48000 if needed)
-> getSampleRate and verify 48000
getBufferSize -> validate exact frames
getChannelInfo(all outputs) -> validate adjacent selected pair
outputReady capability probe -> createBuffers(exact selected pair, exact frames)
getLatencies
```

Assert `createBuffers` receives two output entries, `isInput == ASIOFalse`, channel numbers `base` and `base + 1`, and the exact requested frame count. Inspection mode uses the driver's preferred size only when `buffer_frames == 0`; validation mode rejects zero and never substitutes a preferred size.

Add failure cases at every stage. Negative counts, more than the shared 256-channel protocol bound, unsupported sample rate, fewer than two adjacent outputs, unsupported/MSB/DSD selected formats, inconsistent buffer metadata, unsupported exact frame count, failed buffer creation, or invalid/negative output latency must return a typed `AsioFailure` with stage, result domain/value, driver text, and actionable detail. A mixed pair of two independently supported formats is accepted, and an unsupported format on an unselected output is reported but does not block the selected pair.

- [ ] **Step 2: Implement same-thread RAII cleanup**

Declare:

```cpp
class AsioSession final {
public:
    static std::expected<std::unique_ptr<AsioSession>, AsioFailure> Prepare(
        AsioDriverRegistration registration,
        std::unique_ptr<IAsioDriver> driver,
        const AsioStreamRequest& request,
        HWND system_reference,
        AsioProbeMode mode,
        bool restore_sample_rate) noexcept;

    ~AsioSession();
    AsioSession(const AsioSession&) = delete;
    AsioSession& operator=(const AsioSession&) = delete;

    std::expected<void, AsioFailure> CreateOutputBuffers(
        ASIOCallbacks*) noexcept;
    std::expected<void, AsioFailure> Start() noexcept;
    std::expected<void, AsioFailure> Stop() noexcept;
    std::expected<void, AsioFailure> Close() noexcept;
    const AsioCapabilityReport& report() const noexcept;
    std::span<ASIOBufferInfo> buffers() noexcept;
    IAsioDriver& driver() noexcept;
};
```

Track `started`, `buffers_created`, `sample_rate_changed`, and the creating thread ID. `Stop`, `disposeBuffers`, sample-rate restoration, and final driver-interface release occur in reverse order on the creating/control thread. Expose explicit checked teardown for probe/runtime callers; if an otherwise successful probe cannot restore the original rate, Save fails at `restore_sample_rate`. The destructor is a final no-throw safety net, asserts the same-thread invariant in debug builds, and records but cannot return cleanup failures. The caller that initialized COM destroys the session first and calls `CoUninitialize` afterward.

Probe `future(kAsioCanReportOverload)` with a null option pointer: only the SDK-required `ASE_SUCCESS` means supported; `ASE_NotPresent` or `ASE_OK` records false, while other errors are diagnostic failures. `Prepare` stops after the output-ready capability probe so runtime can publish the callback router before buffer creation. Treat `ASE_OK` as supported and `ASE_NotPresent` as unsupported; any other result is diagnostic failure. `CreateOutputBuffers` passes the exact two output entries and configured frames to the driver, then calls `getLatencies` only after successful `createBuffers`, because that is the SDK-defined point at which latency values are valid. It may be called exactly once.

- [ ] **Step 3: Add the reusable capability entry point**

Declare:

```cpp
std::expected<AsioCapabilityReport, AsioFailure> ProbeAsioCapability(
    IAsioRegistrySource& registry,
    IAsioDriverFactory& factory,
    const AsioStreamRequest& request,
    HWND system_reference,
    AsioProbeMode mode) noexcept;
```

It resolves the exact configured registry name, creates and initializes the driver, prepares through the output-ready probe, calls `CreateOutputBuffers` with inert callbacks, copies the completed report, and destroys the session without starting audio. It must restore the original sample rate before returning if inspection changed it. Use a real hidden `HWND` in production; tests use a sentinel valid handle.

Add the observed Xonar fixture: registration `XONAR SOUND CARD`, zero inputs, eight `ASIOSTInt24LSB` outputs, current 48 kHz, buffer `{192, 2400, 192, 1}`, configured frames 192, base channel 0, and output latency 384. Assert the report is accepted without any vendor-name branch.

- [ ] **Step 4: Verify and commit**

```powershell
cmake --build --preset msvc32-debug --target AsioSessionTests AsioCapabilityProbeTests
ctest --test-dir build-msvc32-debug --output-on-failure -R '^(AsioSessionTests|AsioCapabilityProbeTests)$'
git add -- src/Audio/Asio/AsioTypes.h src/Audio/Asio/AsioSession.h src/Audio/Asio/AsioSession.cpp src/Audio/Asio/AsioCapabilityProbe.h src/Audio/Asio/AsioCapabilityProbe.cpp src/Audio/Asio/CMakeLists.txt tests/Audio/AsioSessionTests.cpp tests/Audio/AsioCapabilityProbeTests.cpp tests/Audio/CMakeLists.txt
git commit -m "Prepare generic ASIO sessions"
```

---

### Task 7: Derive the shared song clock from ASIO presentation position

**Files:**
- Create: `src/Audio/Mixer/PresentedOutputClock.h`
- Create: `src/Audio/Asio/AsioClock.h`
- Create: `src/Audio/Asio/AsioClock.cpp`
- Create: `tests/Audio/AsioClockTests.cpp`
- Modify: `src/Audio/Asio/CMakeLists.txt`
- Modify: `tests/Audio/CMakeLists.txt`

**Interfaces:**
- Consumes: callback `ASIOTime` sample/system timestamps, legacy `getSamplePosition`, buffer frames, output latency, and `timeGetTime` projection time.
- Produces: validated presented/render frame decisions and a monotonic `CurrentOutputFrame()` publication compatible with `AudioCursorTimeline`.

- [ ] **Step 1: Write clock acceptance and rejection tests**

Declare:

```cpp
class IPresentedOutputClock {
public:
    virtual ~IPresentedOutputClock() = default;
    virtual std::optional<std::uint64_t>
        CurrentOutputFrame() noexcept = 0;
    virtual void Invalidate() noexcept = 0;
};

enum class AsioClockDecisionKind : std::uint8_t {
    priming,
    stable,
    invalid,
};

struct AsioClockDecision {
    AsioClockDecisionKind kind{AsioClockDecisionKind::invalid};
    std::uint64_t presented_output_frame{};
    std::uint64_t render_output_frame_begin{};
    std::uint64_t system_time_ns{};
};

class AsioClockTracker final {
public:
    void Reset(std::uint32_t buffer_frames,
               std::uint32_t output_latency_frames) noexcept;
    AsioClockDecision Observe(std::uint64_t sample_position,
                              std::uint64_t system_time_ns) noexcept;
};

struct AsioClockNowActions {
    void* context{};
    std::uint32_t (*time_get_time_ms)(void*) noexcept{};
};

class AsioPresentedClockPublication final
    : public IPresentedOutputClock {
public:
    explicit AsioPresentedClockPublication(
        AsioClockNowActions) noexcept;
    void Publish(const AsioClockDecision&,
                 std::uint64_t submitted_output_tail) noexcept;
    std::optional<std::uint64_t>
        CurrentOutputFrame() noexcept override;
    void Invalidate() noexcept override;
};
```

For 192-frame buffers and 384-frame output latency, assert `(0, t0)`, `(192, t0)`, and `(384, t1)` make the first two callbacks priming despite the repeated initial timestamp and the third stable once time advances. A stable callback at sample position 384 publishes presented frame 384 and renders frame 768; it does not publish the future rendered tail as presented audio.

Reject non-buffer-aligned sample positions, position regressions, skipped/implausible block jumps, repeated timestamps after priming, invalid timestamps, and any buffer-size or sample-rate discontinuity. Compare Windows system time through the low 32-bit millisecond domain derived from the SDK nanoseconds, so the normal `timeGetTime` wrap is accepted while a zero elapsed delta after priming is not. Cover legacy callbacks by feeding the exact values returned from `getSamplePosition` through the same tracker.

For publication, inject a mutable fake `AsioClockNowActions` and cover `timeGetTime`'s 32-bit wrap, monotonic projection, projection capped at the submitted tail, invalidation, null before the first stable anchor, and retention of the last returned frame without further advancement after invalidation.

- [ ] **Step 2: Implement priming and future placement**

Use the driver's `ASIOTimeInfo.samplePosition` as the current block's physical placement and `systemTime` as nanoseconds on Windows. When time info is absent, fetch both through `getSamplePosition`; do not mix one source's sample position with the other's timestamp.

The tracker requires two consecutive deltas equal to the configured block size before declaring the third observation stable. Compute:

```text
presented_output_frame  = sample_position
render_output_frame_begin = sample_position + output_latency_frames
```

Use checked 64-bit arithmetic. A violation after stability is a runtime-clock fault, not a fresh priming sequence.

- [ ] **Step 3: Implement callback-to-game publication**

Publish one seqlock-style trivially copyable anchor containing ASIO presented frame, the wrapping millisecond value derived from `ASIOTimeInfo.systemTime / 1'000'000`, submitted output tail, and validity. `CurrentOutputFrame()` obtains `timeGetTime` through the injected production action only from the game/control reader, applies unsigned 32-bit wrap arithmetic, converts milliseconds at exactly 48 kHz, clamps to the submitted tail, and never returns below the last returned frame.

No callback path calls `timeBeginPeriod`, allocates, locks, or formats logs. The existing process-level timing policy remains the owner of timer resolution.

- [ ] **Step 4: Verify and commit**

```powershell
cmake --build --preset msvc32-debug --target AsioClockTests
ctest --test-dir build-msvc32-debug --output-on-failure -R '^AsioClockTests$'
git add -- src/Audio/Mixer/PresentedOutputClock.h src/Audio/Asio/AsioClock.h src/Audio/Asio/AsioClock.cpp src/Audio/Asio/CMakeLists.txt tests/Audio/AsioClockTests.cpp tests/Audio/CMakeLists.txt
git commit -m "Track ASIO physical presentation time"
```

---

### Task 8: Route ASIO callbacks without real-time hazards

**Files:**
- Create: `src/Audio/Asio/AsioCallbackRuntime.h`
- Create: `src/Audio/Asio/AsioCallbackRuntime.cpp`
- Create: `tests/Audio/AsioCallbackRuntimeTests.cpp`
- Modify: `src/Audio/Asio/CMakeLists.txt`
- Modify: `tests/Audio/CMakeLists.txt`

**Interfaces:**
- Consumes: the SDK's process-global callback function pointers, `directProcess`, buffer indexes, time information, and driver messages.
- Produces: one process-global atomic router, inline/deferred block delivery, pre-created MMCSS worker state, and latched runtime faults/counters.

- [ ] **Step 1: Specify callback behavior with a fake renderer**

Declare:

```cpp
struct AsioRenderRequest {
    long buffer_index{};
    ASIOBool direct_process{};
    bool has_time_info{};
    std::uint64_t sample_position{};
    std::uint64_t system_time_ns{};
};

class IAsioBlockRenderer {
public:
    virtual ~IAsioBlockRenderer() = default;
    virtual void RenderAsioBlock(const AsioRenderRequest&) noexcept = 0;
    virtual void ClearAsioBlock(long buffer_index) noexcept = 0;
    virtual void OnAsioRuntimeFault(AsioFailureStage) noexcept = 0;
};
```

Tests install a fake renderer into the sole router and invoke all four exported callback functions. Assert:

- `bufferSwitchTimeInfo(..., ASIOTrue)` renders synchronously and returns the incoming `ASIOTime*`;
- `bufferSwitch(..., ASIOTrue)` obtains both legacy position values and uses the identical render path;
- `ASIOFalse` signals a worker created before stream start and returns without rendering on the callback thread;
- the worker consumes exactly one bounded slot, renders once, and is promoted with `AvSetMmThreadCharacteristicsW(L"Pro Audio", ...)`;
- a second deferred callback before the first completes is never overlapped or silently replaced: it performs no buffer write, increments the deadline-miss counter, and latches a callback fault;
- an overlapping inline/inline or inline/deferred callback loses the same single-render atomic claim, performs no buffer dereference, faults, and never enters the renderer concurrently;
- the first legal buffer index may be 0 or 1 and subsequent callbacks alternate; a repeated valid index clears that block and faults, while an out-of-range index faults without dereferencing any driver buffer;
- callbacks before install and after uninstall return inertly and do not dereference stale state; callbacks after `BeginStopping` but before uninstall clear buffers through the still-live renderer.

For time-info callbacks, require a nonnull pointer plus `kSystemTimeValid` and `kSamplePositionValid`; reject negative signed values. A `kSampleRateChanged` flag, a valid sample rate other than 48 kHz, or a valid speed other than 1.0 latches a rate/clock fault. Legacy callbacks require successful `GetSamplePosition` and convert the SDK's native-or-hi/lo 64-bit values through one checked helper. Do not combine a time-info sample position with a legacy timestamp.

`asioMessage` tests return selector support only for `kAsioSelectorSupported`, `kAsioEngineVersion`, `kAsioSupportsTimeInfo`, and the runtime-notification selectors actually handled. Return engine version 2, time-info support 1, time-code support 0. `kAsioResetRequest`, `kAsioResyncRequest`, `kAsioLatenciesChanged`, and `kAsioOverload` latch typed counters/faults; `kAsioBufferSizeChange` returns 0 and latches a restart-required fault because this host cannot resize in place. Every `sampleRateDidChange` callback latches a restart-required fault and records the reported rate, including an apparently unchanged 48 kHz notification.

- [ ] **Step 2: Implement the process-global router and pre-created worker**

`AsioCallbackRuntime::Prepare` creates all events/thread state before `ASIOStart`. Installation uses a compare/exchange on one `std::atomic<IAsioBlockRenderer*>`; a second active session fails startup. The worker owns no ASIO driver or COM object and exits before the session disposes buffers.

Use injected `QueryPerformanceCounter` actions and a frequency cached before installation to publish maximum callback and deferred-render durations as atomic tick counts. The callback only reads the counter and updates a maximum; monitoring converts ticks to time and formats them later. Also count time-info versus legacy callbacks and double-buffer index alternation violations.

The SDK callbacks are `noexcept` static functions. Their successful path performs only atomic loads/stores, integer copies, buffer clearing through precomputed spans, bounded performance-counter reads, event signaling for deferred work, the renderer call, and the driver's optional `outputReady` call made by the renderer. No mutex, allocation, stream/file access, GUI call, message box, or formatted logging is allowed.

- [ ] **Step 3: Make teardown ordering testable**

Expose explicit `Install`, `BeginStopping`, `JoinWorker`, and `Uninstall`. Test the required control-thread order:

```text
ASIOStop -> BeginStopping -> JoinWorker -> Uninstall router
-> disposeBuffers -> restore sample rate -> release driver
```

After `BeginStopping`, callback buffers are zeroed and no new render is accepted. After `Uninstall`, late vendor callbacks see null state. Preserve the first runtime fault; later notifications only increment counters.

- [ ] **Step 4: Verify and commit**

```powershell
cmake --build --preset msvc32-debug --target AsioCallbackRuntimeTests
ctest --test-dir build-msvc32-debug --output-on-failure -R '^AsioCallbackRuntimeTests$'
git add -- src/Audio/Asio/AsioCallbackRuntime.h src/Audio/Asio/AsioCallbackRuntime.cpp src/Audio/Asio/CMakeLists.txt tests/Audio/AsioCallbackRuntimeTests.cpp tests/Audio/CMakeLists.txt
git commit -m "Route ASIO callbacks safely"
```

---

### Task 9: Isolate vendor probing behind a bounded helper process

**Files:**
- Create: `src/Audio/Asio/AsioProbeProtocol.h`
- Create: `src/Audio/Asio/AsioProbeProtocol.cpp`
- Create: `src/Audio/Asio/AsioProbeClient.h`
- Create: `src/Audio/Asio/AsioProbeClient.cpp`
- Create: `tools/AsioProbe/CMakeLists.txt`
- Create: `tools/AsioProbe/Main.cpp`
- Create: `tests/Audio/AsioProbeProtocolTests.cpp`
- Create: `tests/Audio/AsioProbeClientTests.cpp`
- Modify: `src/Audio/Asio/CMakeLists.txt`
- Modify: `tools/CMakeLists.txt`
- Modify: `tests/Audio/CMakeLists.txt`

**Interfaces:**
- Consumes: an inspection/validation request and sibling `AsioProbe.exe`.
- Produces: a versioned bounded pipe protocol, isolated x86 helper, and mockable `IAsioProbeClient` for ConfigGUI.

- [ ] **Step 1: Define and fuzz the bounded protocol**

Use a little-endian binary envelope with fixed-width integers:

```cpp
inline constexpr std::uint32_t kAsioProbeMagic = 0x4F495341; // ASIO
inline constexpr std::uint16_t kAsioProbeProtocolVersion = 1;
inline constexpr std::uint32_t kAsioProbeMaxPayloadBytes = 64 * 1024;
inline constexpr std::uint32_t kAsioProbeMaxDriverNameBytes = 1024;
inline constexpr std::uint32_t kAsioProbeMaxChannelNameBytes = 32;
inline constexpr std::uint32_t kAsioProbeMaxChannels =
    kMaxAsioReportedChannels;

struct AsioProbeRequest {
    AsioProbeMode mode{AsioProbeMode::inspect};
    std::string driver_name;
    std::uint32_t buffer_frames{};
    std::uint32_t output_base_channel{};
};

std::expected<std::vector<std::byte>, AsioProbeProtocolError>
EncodeAsioProbeRequest(const AsioProbeRequest&) noexcept;
std::expected<AsioProbeRequest, AsioProbeProtocolError>
DecodeAsioProbeRequest(std::span<const std::byte>) noexcept;
```

Add symmetric result functions. A result is either one `AsioCapabilityReport` or one structured `AsioFailure`, never an ambiguous mix. Encode a CLSID as its 16 fixed bytes. Never serialize an `HWND`; the helper creates and inserts its own local handle. Tests round-trip Unicode names/channel labels and every buffer/failure field, and reject wrong magic/version/kind, truncated and trailing data, invalid UTF-8, oversized lengths/counts, integer overflow, and unknown enums. Decoders must not reserve from untrusted counts until all bounds are proven.

- [ ] **Step 2: Build the x86 helper without a shell or driver start**

`AsioProbe.exe` reads exactly one framed request from inherited standard input and writes exactly one framed result to inherited standard output. Its main thread calls `CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE)`, creates a hidden real top-level Win32 window, converts the wire fields into `AsioStreamRequest`, and passes that local `HWND` separately to `ProbeAsioCapability`. It destroys the session/window, uninitializes COM, flushes the result, and exits. It never calls `ASIOStart`, opens a control panel, emits human text on stdout, or persists settings.

Give operational errors distinct nonzero exit codes, but make the structured response authoritative when one was successfully written. Link only the needed project ASIO/config libraries and Win32 dependencies. Copy the helper next to ConfigGUI in `${GC_DIST_DIR}`.

- [ ] **Step 3: Test and implement the parent process boundary**

Declare:

```cpp
class IAsioProbeClient {
public:
    virtual ~IAsioProbeClient() = default;
    virtual std::expected<AsioCapabilityReport, AsioFailure> Run(
        const AsioProbeRequest&,
        std::chrono::milliseconds timeout) noexcept = 0;
};
```

Inject a narrow `IAsioProbeProcessActions` in tests. Assert production behavior:

1. resolve an absolute `AsioProbe.exe` path from ConfigGUI's executable directory;
2. create private stdin/stdout anonymous pipes with only child ends inheritable;
3. call `CreateProcessW` directly with a quoted absolute executable and no driver-controlled command-line argument;
4. set `CREATE_SUSPENDED | CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT`, an explicit inherited-handle list, and no shell;
5. assign the suspended child to a kill-on-close Job Object, then resume its primary thread;
6. send the driver name only in the bounded pipe payload;
7. concurrently drain bounded stdout while waiting, so a full pipe cannot deadlock;
8. on timeout, close the job, wait for termination, close every handle, and return `probe_timeout`;
9. distinguish create failure, job-assignment failure, crash/nonzero exit, truncated/oversized/invalid output, and a valid negative driver result.

Use a 5-second default inspection timeout and 5-second final-validation timeout. Do not retry a hung/crashed driver automatically.

- [ ] **Step 4: Verify and commit**

```powershell
cmake --build --preset msvc32-debug --target AsioProbeProtocolTests AsioProbeClientTests AsioProbe
ctest --test-dir build-msvc32-debug --output-on-failure -R '^(AsioProbeProtocolTests|AsioProbeClientTests)$'
git add -- src/Audio/Asio/AsioProbeProtocol.h src/Audio/Asio/AsioProbeProtocol.cpp src/Audio/Asio/AsioProbeClient.h src/Audio/Asio/AsioProbeClient.cpp src/Audio/Asio/CMakeLists.txt tools/AsioProbe/CMakeLists.txt tools/AsioProbe/Main.cpp tools/CMakeLists.txt tests/Audio/AsioProbeProtocolTests.cpp tests/Audio/AsioProbeClientTests.cpp tests/Audio/CMakeLists.txt
git commit -m "Isolate ASIO driver validation"
```

---

### Task 10: Add editable ASIO choices and make Save transactional

**Files:**
- Create: `tools/ConfigGUI/AudioBackendEditorModel.h`
- Create: `tools/ConfigGUI/AudioBackendEditorModel.cpp`
- Create: `tools/ConfigGUI/AsioLogoTexture.h`
- Create: `tools/ConfigGUI/AsioLogoTexture.cpp`
- Create: `tests/Config/ConfigGuiAudioBackendModelTests.cpp`
- Modify: `tools/ConfigGUI/Main.cpp`
- Modify: `tools/ConfigGUI/Win32D3D11Host.h`
- Modify: `tools/ConfigGUI/Win32D3D11Host.cpp`
- Modify: `tools/ConfigGUI/CMakeLists.txt`
- Modify: `tests/Config/CMakeLists.txt`

**Interfaces:**
- Consumes: installed 32-bit registrations, common editable suggestions, current config, `IAsioProbeClient`, and the existing atomic config writer.
- Produces: deterministic editor/probe state and a Save transaction that performs zero writes on ASIO validation failure.

- [ ] **Step 1: Test editable choices and invalidation rules**

Use this exact common-suggestion order after installed entries:

```cpp
inline constexpr std::array<std::string_view, 6> kCommonAsioDriverNames{
    "XONAR SOUND CARD",
    "ASIO4ALL v2",
    "FlexASIO",
    "KoordASIO",
    "FL Studio ASIO",
    "Generic Low Latency ASIO Driver",
};
```

Assert installed registry names come first, common entries are appended case-insensitively without duplicates, and an arbitrary current/user-entered name remains editable and selected even when not present in either list. These strings are suggestions, never support claims or a whitelist.

An empty successful catalog disables the ASIO selector but preserves arbitrary text/suggestions. A registry enumeration failure is a distinct visible catalog error, also disables selection, and is never misreported as simply no drivers installed.

Model states are `idle`, `probing`, `valid`, and `failed`. Editing driver name, frames, base channel, or backend invalidates the previous result. A successful inspection stores the full capability report. If the configured frames are zero, inspection uses the driver's preferred count and the model writes that exact value into the in-memory config; nonzero input is never changed. Build adjacent channel-pair labels from the returned output descriptors and reject a base channel that is not an adjacent pair.

- [ ] **Step 2: Write the no-write-on-failure Save tests**

Extract the transaction:

```cpp
std::expected<void, std::string> ValidateAndWriteConfig(
    const std::filesystem::path& path,
    const InputConfig& config,
    IAsioProbeClient& asio_probe,
    const AtomicConfigWriteActions& write_actions) noexcept;
```

Test DirectSound and WASAPI call static validation then atomically write without a helper. For ASIO, test that final validation receives the exact name, exact frame count, exact base channel, and mode `validate`; `AsioProbe.exe` supplies its own real hidden reference window internally. Only then may the GUI call the existing atomic writer.

For missing registration, unsupported sample rate/buffer/channel/format, driver error, timeout, helper crash, malformed response, and atomic-write failure, assert the old config bytes remain identical. Count writes/renames explicitly; every probe failure must be zero writes.

- [ ] **Step 3: Replace the Boolean UI with backend-specific controls**

In `Main.cpp` render a three-way `DirectSound` / `WASAPI exclusive` / `ASIO®` selector. Show:

- WASAPI buffer duration in milliseconds only for WASAPI;
- an editable combo for the exact ASIO driver name;
- an Inspect button and visible `idle/probing/valid/failed` result;
- exact buffer frames plus computed milliseconds at 48 kHz;
- reported minimum, maximum, preferred, and granularity semantics;
- an adjacent output-pair combo showing indexes, names, and formats;
- the precise helper failure in a wrapped error panel;
- Steinberg-compatible ASIO® logo and the required trademark/attribution text.

Beside Inspect/Validate, explain that probing never starts audio but may briefly claim the device and change its sample rate to 48 kHz before restoring it.

If no 32-bit registrations exist, disable selecting ASIO and explain that a 32-bit ASIO driver must be installed; keep the editable field and common suggestions visible so the user understands expected registry names. If registrations exist, an arbitrary typed name is allowed. Do not infer success from the name or from a prior inspection.

Wrap the atomic Save path from Task 2 with `ValidateAndWriteConfig`. On ASIO Save, copy the path/config into one owned joinable worker, disable further edits/Saves, and render a modal progress state while the helper runs. Marshal only the final value/error back to the GUI thread; do not let the worker retain references to ImGui state. Failure keeps the window/config open and prevents Save. Success refreshes the model from the saved canonical document. Window shutdown closes the probe client's Job Object and joins the worker, bounded by the same five-second probe policy.

- [ ] **Step 4: Package GUI/helper assets and verify**

Resolve the compatible black/white ASIO logo from `GC_ASIO_COMPATIBLE_LOGO` and copy only the chosen asset to the ConfigGUI distribution directory. Do not rasterize or redraw the trademark. Add a configure error if no compatible logo was found in the required SDK.

Expose the host's borrowed `ID3D11Device*` and implement an RAII `AsioLogoTexture` that decodes the sibling PNG with Windows Imaging Component into RGBA, creates one immutable D3D11 texture/shader-resource view, and releases it before the host device closes. Initialize a GUI-thread STA for WIC only; this never loads an ASIO driver. Link `windowscodecs`, render the original aspect ratio with `ImGui::Image`, and show attribution plus an asset-load error if the deployed PNG is missing/corrupt.

Run:

```powershell
cmake --build --preset msvc32-debug --target ConfigGuiAudioBackendModelTests ConfigGUI AsioProbe
ctest --test-dir build-msvc32-debug --output-on-failure -R '^ConfigGuiAudioBackendModelTests$'
```

Manually launch the source-tree ConfigGUI only after the tests, point it at a temporary copied config, and verify arbitrary text survives selection changes and a deliberately invalid ASIO name cannot overwrite the file. Do not use or modify `H:\gc\config.toml`.

- [ ] **Step 5: Commit the editor and save gate**

```powershell
git add -- tools/ConfigGUI/AudioBackendEditorModel.h tools/ConfigGUI/AudioBackendEditorModel.cpp tools/ConfigGUI/AsioLogoTexture.h tools/ConfigGUI/AsioLogoTexture.cpp tools/ConfigGUI/Main.cpp tools/ConfigGUI/Win32D3D11Host.h tools/ConfigGUI/Win32D3D11Host.cpp tools/ConfigGUI/CMakeLists.txt tests/Config/ConfigGuiAudioBackendModelTests.cpp tests/Config/CMakeLists.txt
git commit -m "Validate ASIO settings before save"
```

---

### Task 11: Extract one preallocated render core for WASAPI and ASIO

**Files:**
- Create: `src/Audio/Mixer/AudioRenderCore.h`
- Create: `src/Audio/Mixer/AudioRenderCore.cpp`
- Create: `src/Audio/Mixer/AudioRenderCoreInternal.h`
- Create: `src/Audio/Wasapi/WasapiPresentedOutputClock.h`
- Create: `src/Audio/Wasapi/WasapiPresentedOutputClock.cpp`
- Create: `tests/Audio/AudioRenderCoreTests.cpp`
- Create: `tests/Audio/WasapiPresentedOutputClockTests.cpp`
- Modify: `src/Audio/Wasapi/ExclusiveAudioEngine.h`
- Modify: `src/Audio/Wasapi/ExclusiveAudioEngine.cpp`
- Modify: `src/Audio/Wasapi/ExclusiveAudioEngineInternal.h`
- Modify: `tests/Audio/ExclusiveAudioEngineTests.cpp`
- Modify: `src/Audio/CMakeLists.txt`
- Modify: `tests/Audio/CMakeLists.txt`

**Interfaces:**
- Consumes: the current `MiniaudioMixer`, allocation callbacks, period/rate, `MixerRenderTimeline`, and one owned `IPresentedOutputClock` supplied by the backend.
- Produces: the backend-neutral `AudioRenderCore`, one reusable interleaved stereo float block, voice/cursor ownership, and the common `CurrentOutputFrame()` publication boundary.

- [ ] **Step 1: Move the render-finalization behavior under focused tests**

Declare:

```cpp
struct AudioRenderBlock {
    std::span<const float> interleaved_stereo;
    ma_result mixer_result{MA_ERROR};
    bool silence_substituted{};
};

class AudioRenderCore final {
public:
    static std::unique_ptr<AudioRenderCore> Create(
        std::uint32_t period_frames,
        std::uint32_t output_sample_rate,
        std::shared_ptr<const ma_allocation_callbacks>,
        std::unique_ptr<IPresentedOutputClock>,
        ma_result*) noexcept;

    std::unique_ptr<MixerVoice> CreateVoice(
        const NormalizedSourceFormat&,
        std::shared_ptr<AudioSnapshot>,
        std::shared_ptr<AudioCursorTimeline>,
        VoiceUsage,
        ma_result*) noexcept;
    AudioRenderBlock Render(const MixerRenderTimeline&) noexcept;
    std::optional<std::uint64_t> CurrentOutputFrame() noexcept;
    void InvalidatePresentationClock() noexcept;
    MixerDiagnosticsSnapshot diagnostics() const noexcept;
    std::uint32_t period_frames() const noexcept;
    std::uint32_t output_sample_rate() const noexcept;
};
```

Move the pure behavior currently exercised through `detail::FinalizeMixerRenderBlock` into `detail::FinalizeAudioRenderBlock` in `AudioRenderCoreInternal.h`. `AudioRenderCoreTests` assert that an exact-frame successful render preserves samples, while a short read or error result zeroes the complete fixed block and marks `silence_substituted`. Also assert repeated production renders reuse the same float allocation, timelines reach the mixer unchanged, voice creation/diagnostic counters delegate unchanged, a fake owned clock receives current-frame/invalidate calls, and its lifetime ends with the core. Do not introduce new sample sanitization or otherwise change accepted WASAPI output.

- [ ] **Step 2: Implement the backend-neutral core**

Create the `MiniaudioMixer` and `period_frames * 2` float vector in `Create`, before any real-time callback begins, and take sole ownership of a nonnull presentation clock. Reject zero frames, non-48-kHz output, missing clock, allocation overflow, mixer creation failure, and vector allocation failure through the existing `ma_result` convention. `Render` is `noexcept`, never resizes, and always returns exactly one full-period span. `CurrentOutputFrame` and invalidation delegate to the owned clock; no backend publishes a render tail directly to the facade.

Keep `ConvertFloatToPcm16` in the mixer module; it remains a WASAPI adapter operation. Do not put ASIO SDK types or WASAPI endpoint types in `AudioRenderCore`.

- [ ] **Step 3: Make WASAPI delegate without changing its behavior**

Add `WasapiPresentedOutputClock`, a thin owner of the existing `PresentedClockPublication`, cached QPC frequency, and injected `QueryPerformanceCounter` action. Its tests reproduce the existing projection, cap, monotonic, failure, and invalidation behavior. Replace `ExclusiveAudioEngine::mixer_` and `float_mix_` with `render_core_`; retain a non-owning typed clock pointer only for publishing endpoint observations. Create the clock/core after the endpoint reports the fixed period/rate and before endpoint start. In `RenderLoop`, call:

```cpp
const auto block = render_core_->Render(MixerRenderTimeline{
    .output_frame_begin = decision.render_output_frame_begin,
    .discontinuity_frames = decision.discontinuity_frames,
});
if (block.silence_substituted) {
    silence_fallbacks_.fetch_add(1, std::memory_order_relaxed);
}
ConvertFloatToPcm16(block.interleaved_stereo, pcm16_mix_);
```

Delegate `CreateVoice`, `CurrentOutputFrame`, invalidation, and mixer diagnostics to the core. Keep WASAPI's PCM16 vector, endpoint event loop, pacing tracker, physical-presentation observations, startup/failure semantics, and counters otherwise byte-for-behavior equivalent.

- [ ] **Step 4: Verify the extraction and commit**

```powershell
cmake --build --preset msvc32-debug --target AudioRenderCoreTests WasapiPresentedOutputClockTests ExclusiveAudioEngineTests MiniaudioMixerTests
ctest --test-dir build-msvc32-debug --output-on-failure -R '^(AudioRenderCoreTests|WasapiPresentedOutputClockTests|ExclusiveAudioEngineTests|MiniaudioMixerTests)$'
git add -- src/Audio/Mixer/AudioRenderCore.h src/Audio/Mixer/AudioRenderCore.cpp src/Audio/Mixer/AudioRenderCoreInternal.h src/Audio/Wasapi/WasapiPresentedOutputClock.h src/Audio/Wasapi/WasapiPresentedOutputClock.cpp src/Audio/Wasapi/ExclusiveAudioEngine.h src/Audio/Wasapi/ExclusiveAudioEngine.cpp src/Audio/Wasapi/ExclusiveAudioEngineInternal.h src/Audio/CMakeLists.txt tests/Audio/AudioRenderCoreTests.cpp tests/Audio/WasapiPresentedOutputClockTests.cpp tests/Audio/ExclusiveAudioEngineTests.cpp tests/Audio/CMakeLists.txt
git commit -m "Share the audio render core"
```

---

### Task 12: Implement and commit the ASIO output backend

**Files:**
- Create: `src/Audio/Asio/AsioOutputBackend.h`
- Create: `src/Audio/Asio/AsioOutputBackend.cpp`
- Create: `src/Audio/Asio/AsioOutputBackendInternal.h`
- Create: `tests/Audio/AsioOutputBackendTests.cpp`
- Modify: `src/Audio/Asio/AsioTypes.h`
- Modify: `src/Audio/CMakeLists.txt`
- Modify: `tests/Audio/CMakeLists.txt`

**Interfaces:**
- Consumes: the runtime config snapshot, real game `HWND`, catalog/factory, callback runtime, clock mapper, sample converter, render core, and allocation callbacks.
- Produces: `AsioOutputBackend : IAudioEngineServices`, stable-start commit result, runtime diagnostics, and controlled stop/restart guidance.

- [ ] **Step 1: Build a fake-Xonar end-to-end backend test**

Declare the public startup surface:

```cpp
struct AsioRuntimeCountersSnapshot {
    std::uint64_t callbacks{};
    std::uint64_t time_info_callbacks{};
    std::uint64_t legacy_callbacks{};
    std::uint64_t deferred_callbacks{};
    std::uint64_t deadline_misses{};
    std::uint64_t silence_substitutions{};
    std::uint64_t overload_messages{};
    std::uint64_t reset_requests{};
    std::uint64_t resync_requests{};
    std::uint64_t latency_change_requests{};
    std::uint64_t buffer_size_change_requests{};
    std::uint64_t sample_rate_change_requests{};
    std::uint64_t sample_position_discontinuities{};
    std::uint64_t render_gap_frames{};
    std::uint64_t maximum_callback_ticks{};
    std::uint64_t maximum_render_ticks{};
    std::uint64_t pending_cursor_queries{};
    std::uint64_t unmapped_cursor_failures{};
    MixerDiagnosticsSnapshot mixer{};
};

class IAsioOutputObserver {
public:
    virtual ~IAsioOutputObserver() = default;
    virtual void StartupSucceeded(
        const AsioCapabilityReport&) noexcept = 0;
    virtual void RuntimeSummary(
        const AsioRuntimeCountersSnapshot&) noexcept = 0;
    virtual void RuntimeFailed(
        const AsioFailure&,
        const AsioRuntimeCountersSnapshot&) noexcept = 0;
};

class AsioOutputBackend final : public IAudioEngineServices {
public:
    static std::unique_ptr<AsioOutputBackend> StartAndWait(
        HWND game_window,
        const AsioStreamRequest&,
        std::unique_ptr<IAsioRegistrySource>,
        std::unique_ptr<IAsioDriverFactory>,
        std::shared_ptr<IAsioOutputObserver>,
        std::shared_ptr<const ma_allocation_callbacks>,
        DWORD startup_clock_timeout_ms,
        AsioFailure*) noexcept;
    ~AsioOutputBackend();

    AsioOutputBackend(const AsioOutputBackend&) = delete;
    AsioOutputBackend& operator=(const AsioOutputBackend&) = delete;

    std::unique_ptr<MixerVoice> CreateVoice(
        const NormalizedSourceFormat&,
        std::shared_ptr<AudioSnapshot>,
        std::shared_ptr<AudioCursorTimeline>,
        VoiceUsage,
        ma_result*) noexcept override;
    std::optional<std::uint64_t> CurrentOutputFrame() noexcept override;
    std::uint32_t endpoint_buffer_frames() const noexcept override;
    std::uint32_t output_sample_rate() const noexcept override;
    void CountPendingCursorQuery() noexcept override;
    void CountUnmappedCursorFailure() noexcept override;
};
```

Use injected thread/event/time/MMCSS actions and the Task 6 fake driver. Model the observed Xonar generically: eight `Int24LSB` outputs, exact 192-frame double buffers, base 0/1, and 384-frame output latency. Feed priming callbacks `(0, t0)`, `(192, t0)`, then stable `(384, t1)` and assert:

- startup does not commit before the third callback;
- both priming buffers are all zero and may call `outputReady` only when supported;
- the stable render timeline begins at frame 768 and has exactly 192 frames;
- both planar packed-24 buffers contain the byte-exact converted mixer result;
- `CurrentOutputFrame` exposes the presentation anchor around frame 384, never the render tail around 960;
- `CreateVoice`, period/rate, pending-cursor, unmapped-cursor, and mixer counters flow through the existing service contract.

Assert the low-frequency observer snapshot includes the complete counter structure above and that QPC ticks are converted with the cached frequency only on the control/monitor path. Retain the existing framerate diagnostics for exact gameplay cursor generation and seek observations.

- [ ] **Step 2: Prove every pre-commit failure remains recoverable**

Add table-driven failures for COM initialization, resolve/create/init, capability validation, callback-router collision, worker/MMCSS setup, buffer creation/latency, render-core allocation, `start`, no callback, only priming callbacks, invalid third clock, callback fault before stability, and the two-second deadline.

Each case must synchronously return null plus the first typed `AsioFailure`, stop if start succeeded, join the worker, clear the router, dispose buffers, restore the original sample rate, release the driver, and uninitialize COM on the control thread. Assert no observer reports startup success and no thread/handle remains live.

- [ ] **Step 3: Implement dedicated control-thread ownership**

The constructor copies the request and stores only owned pending dependencies; no caller stack reference crosses into the new thread. `StartAndWait` creates control events and one joinable control thread. That thread calls `CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE)`, performs exact catalog resolution/factory creation, and runs `AsioSession::Prepare` through the output-ready probe using the real game `HWND`. It then prepares callback state, installs the global router, calls `CreateOutputBuffers`, reads the resulting latency report, creates an `AsioPresentedClockPublication`, transfers its ownership into `AudioRenderCore` while retaining only a typed non-owning publisher pointer, makes the deferral worker ready, resets the tracker, and calls `ASIOStart`.

Because apartment-threaded vendor drivers may rely on their owning thread's message queue, the control/monitor loop waits with `MsgWaitForMultipleObjectsEx` and drains messages with `PeekMessageW`/`TranslateMessage`/`DispatchMessageW` while also servicing shutdown/fault/summary events. Tests assert COM mode, message dispatch, and that every `IASIO` lifecycle call and final release remains on this same thread.

The in-game host cannot safely cancel or abandon an in-process vendor method that never returns. Therefore `startup_clock_timeout_ms` starts only after `ASIOStart` itself returns successfully; earlier lifecycle calls are allowed to complete or fail normally. The out-of-process save probe is the bounded guard for a hung vendor method. Never detach the control thread to manufacture an overall timeout.

The stable callback publishes only a `stable_render_ready` event; it cannot commit while `ASIOStart` may still be on the stack. After `ASIOStart` returns `ASE_OK`, the control thread requires stable-render readiness and no latched fault, publishes one irreversible committed bit, then signals the caller's startup event. No waiter holds a callback resource. A fault after committed publication is post-commit even if the `StartAndWait` caller has not resumed yet. A returned `ASIOStart` followed by no stable callback within 2,000 ms is `startup_clock` failure.

On destructor, signal shutdown and join the control thread. The control thread performs the complete ordered teardown and only then reports its final summary. Never detach a thread or unload a driver from `DllMain`.

- [ ] **Step 4: Implement real-time render and post-commit failure policy**

`RenderAsioBlock` validates the generation/buffer index and clock decision. During priming, zero both selected channel buffers. When stable:

1. render `buffer_frames` at `sample_position + output_latency_frames`;
2. convert left and right directly into the two driver-owned planar buffers;
3. publish the presentation anchor with the submitted render tail cap;
4. increment callback/render/silence/deadline counters atomically;
5. call `OutputReady` only if the pre-start probe returned support.

Validate both channel spans before conversion. If either channel conversion fails, clear both selected buffers before latching the failure so a partially converted stereo block is never submitted.

An invalid clock, conversion failure, unexpected callback, deferred overlap, ASIO reset/resync/buffer/rate notification, or driver output-ready error after commit latches a post-commit fault. Wake the control thread, stop cleanly, invalidate the public clock, emit the observer's restart-required diagnostic, and keep returning silence to any late callback. Do not invoke the backend controller or start WASAPI after commit.

- [ ] **Step 5: Verify and commit**

```powershell
cmake --build --preset msvc32-debug --target AsioOutputBackendTests AsioCallbackRuntimeTests AsioClockTests
ctest --test-dir build-msvc32-debug --output-on-failure -R '^(AsioOutputBackendTests|AsioCallbackRuntimeTests|AsioClockTests)$'
git add -- src/Audio/Asio/AsioOutputBackend.h src/Audio/Asio/AsioOutputBackend.cpp src/Audio/Asio/AsioOutputBackendInternal.h src/Audio/Asio/AsioTypes.h src/Audio/CMakeLists.txt tests/Audio/AsioOutputBackendTests.cpp tests/Audio/CMakeLists.txt
git commit -m "Render game audio through ASIO"
```

---

### Task 13: Select the backend lazily and fall back only before ASIO commit

**Files:**
- Create: `src/Audio/AudioBackendController.h`
- Create: `src/Audio/AudioBackendController.cpp`
- Rename: `src/Audio/Wasapi/WasapiAudioPatch.h` -> `src/Audio/AudioPatch.h`
- Rename: `src/Audio/Wasapi/WasapiAudioPatch.cpp` -> `src/Audio/AudioPatch.cpp`
- Rename: `src/Audio/Wasapi/WasapiAudioPatchInternal.h` -> `src/Audio/AudioPatchInternal.h`
- Create: `tests/Audio/AudioBackendControllerTests.cpp`
- Rename: `tests/Audio/WasapiAudioPatchTests.cpp` -> `tests/Audio/AudioPatchTests.cpp`
- Modify: `src/Audio/DirectSound/DirectSoundFacade.h`
- Modify: `src/Audio/DirectSound/DirectSoundFacade.cpp`
- Modify: `tests/Audio/DirectSoundDeviceTests.cpp`
- Modify: `src/Config/config.h`
- Modify: `src/Patches/Framerate/FrameratePatch.cpp`
- Modify: `src/Loader/DllMain.cpp`
- Modify: `src/Audio/CMakeLists.txt`
- Modify: `tests/Audio/CMakeLists.txt`

**Interfaces:**
- Consumes: the configured backend snapshot, factories for WASAPI/ASIO engines, real game window, existing fatal reporter, and the `DirectSoundCreate8` detour transaction.
- Produces: one lazy `IAudioEngineController`, generic hook naming, requested/active diagnostics, and the exact ASIO-to-WASAPI pre-commit fallback policy.

- [ ] **Step 1: Write the controller state-machine tests**

Declare:

```cpp
class IAudioEngineController : public IAudioEngineServices {
public:
    virtual HRESULT StartForWindow(HWND game_window) noexcept = 0;
};

class IAudioBackendControllerFactory {
public:
    virtual ~IAudioBackendControllerFactory() = default;
    virtual IAudioEngineController* GetOrCreate() noexcept = 0;
};

class IWasapiOutputBackendFactory {
public:
    virtual ~IWasapiOutputBackendFactory() = default;
    virtual std::unique_ptr<IAudioEngineServices> Start(
        REFERENCE_TIME configured_duration,
        AudioStartupFailure*) noexcept = 0;
};

class IAsioOutputBackendFactory {
public:
    virtual ~IAsioOutputBackendFactory() = default;
    virtual std::unique_ptr<IAudioEngineServices> Start(
        HWND game_window,
        const AsioStreamRequest&,
        AsioFailure*) noexcept = 0;
};

enum class ActiveAudioBackend : std::uint8_t {
    none,
    wasapi_exclusive,
    asio,
    failed,
};
```

Production factories close over allocation callbacks, driver/endpoint adapters, observers, and the fixed startup timings; the controller supplies only the persisted request values and real `HWND`. Inject both factories and the existing fatal reporter. Test:

- WASAPI starts once with the first valid priority-level game `HWND`;
- ASIO repeats exact-name/capability validation and commits without touching WASAPI when its stable startup succeeds;
- every ASIO failure before stable commit tears it down, logs the typed failure, and attempts WASAPI exactly once with the configured millisecond duration;
- if fallback succeeds, the active backend is WASAPI but the in-memory/on-disk requested backend remains ASIO;
- if both fail, the WASAPI failure is fatal and the ASIO cause remains nested in diagnostics;
- concurrent/repeated `StartForWindow` calls observe one initialization, one stable result, and no retry after terminal failure;
- a post-commit ASIO runtime fault never calls the WASAPI factory;
- the commit/fault race remains post-commit once the backend's irreversible committed bit is visible, even if `StartForWindow` has not returned;
- service calls before successful start fail safely; after start they delegate to the active engine.

Use an explicit state sequence `not_started -> starting -> active_asio|active_wasapi|failed` guarded on the non-real-time control path. Never run a factory while holding the state mutex; other callers wait on a condition variable.

Keep `IAudioEngineController` in `AudioBackendController.h`. `DirectSoundFacade.h` forward-declares it and stores a reference; `DirectSoundFacade.cpp` includes the controller header before calling `StartForWindow`. This avoids moving or duplicating `IAudioEngineServices` and avoids an include cycle.

- [ ] **Step 2: Start from `SetCooperativeLevel`, not the hook callback**

Change `CreateDirectSoundDevice` to accept `IAudioEngineController&`. `DirectSoundDevice::SetCooperativeLevel` preserves current parameter validation, then calls `StartForWindow(window)` before publishing `priority_cooperative_level_`. Return `DSERR_NODRIVER` on controller startup failure and leave priority false. Existing `CreateSoundBuffer` calls before successful priority still return `DSERR_PRIOLEVELNEEDED`.

Extend `DirectSoundDeviceTests` to assert the exact game `HWND` is forwarded once, invalid window/non-priority levels never call start, a start failure does not unlock buffer creation, and repeated successful calls do not recreate the engine.

- [ ] **Step 3: Generalize the hook without weakening its transaction**

Move the existing patch files with `git mv`, then rename only the WASAPI-specific public/internal symbols:

```text
InstallWasapiAudioHook              -> InstallAudioHook
WasapiAudioPatchInit                -> AudioPatchInit
IsWasapiAudioHookCommitted          -> IsAudioHookCommitted
WasapiAudioPatchInitWithDependencies -> AudioPatchInitWithDependencies
```

Retain `DirectSoundCreate8Fn`, every MinHook stage, fail-closed rollback, saved-original signature, and existing behavior tests. The detour validates `device_guid`, output, and aggregation exactly as today, obtains/creates the process-lifetime controller through `IAudioBackendControllerFactory`, and returns a DirectSound facade without starting an output engine. A controller-allocation failure is reported on the existing non-callback fatal path and returns `DSERR_OUTOFMEMORY`; add tests proving no WASAPI/ASIO backend factory runs inside the hook.

`AudioPatchInit` installs the hook only for `wasapi_exclusive` or `asio`; explicit `directsound` is a no-op and the original system `DirectSoundCreate8` remains active. Rename log prefixes and test targets from WASAPI patch to generic audio patch.

Preserve the current intentionally allocated process-lifetime detour state: releasing the DirectSound facade does not stop the backend, and `DLL_PROCESS_DETACH` does not destroy the controller, join threads, release `IASIO`, or unload vendor code under the loader lock. Controlled startup failure and post-commit fault still execute the ordered teardown on the ASIO control thread; ordinary process termination is left to Windows as it is for the existing WASAPI engine.

- [ ] **Step 4: Wire startup and clock consumers**

In `DllMain.cpp`, replace `WasapiAudioPatchInit`/`IsWasapiAudioHookCommitted` with the generic names. Keep current attach ordering and fail-closed behavior. Update `FrameratePatch` only at the capability flag call site: hooked WASAPI and committed ASIO/fallback WASAPI all supply the same authoritative song clock; explicit DirectSound does not.

Delete the transitional `GetEnableWasapiExclusiveAudio()` added in Task 2. Generic hook enablement and backend selection now read `GetAudioBackend()` directly; no legacy Boolean-named code remains.

Emit one structured startup record containing:

```text
requested_backend
active_backend
asio registry name, driver-reported name, CLSID, and version (when requested)
sample rate and selected channel indexes/names/types
requested/minimum/maximum/preferred/granularity frames
input/output latency frames
time-info mode, host overload-notification support, and output-ready support
asio_failure_stage/result/detail (when fallback occurred)
wasapi_buffer_ms (when active/fallback)
asio_buffer_frames/base_channel/output_latency (when active)
fallback_reason
```

Periodic runtime summaries include maximum callback/render duration, callback/deadline/silence/overload counts, every reset/resync/latency/buffer/rate notification, clock discontinuities/render gaps, pending/unmapped cursor counts, mixer counters, and the existing BGM cursor-generation/seek diagnostics. Formatting remains off the callback path.

Do not write configuration during runtime fallback. Post-commit ASIO failure uses the existing fatal/restart presentation on a non-callback thread and states that the user may select WASAPI in ConfigGUI.

- [ ] **Step 5: Verify the integration and commit**

```powershell
cmake --build --preset msvc32-debug --target AudioBackendControllerTests AudioPatchTests DirectSoundDeviceTests FramerateRuntimeTests iDmacDrv32
ctest --test-dir build-msvc32-debug --output-on-failure -R '^(AudioBackendControllerTests|AudioPatchTests|DirectSoundDeviceTests|FramerateRuntimeTests)$'
git add -A -- src/Audio/AudioBackendController.h src/Audio/AudioBackendController.cpp src/Audio/AudioPatch.h src/Audio/AudioPatch.cpp src/Audio/AudioPatchInternal.h src/Audio/Wasapi/WasapiAudioPatch.h src/Audio/Wasapi/WasapiAudioPatch.cpp src/Audio/Wasapi/WasapiAudioPatchInternal.h src/Audio/DirectSound/DirectSoundFacade.h src/Audio/DirectSound/DirectSoundFacade.cpp src/Audio/CMakeLists.txt src/Config/config.h src/Patches/Framerate/FrameratePatch.cpp src/Loader/DllMain.cpp tests/Audio/AudioBackendControllerTests.cpp tests/Audio/AudioPatchTests.cpp tests/Audio/WasapiAudioPatchTests.cpp tests/Audio/DirectSoundDeviceTests.cpp tests/Audio/CMakeLists.txt
git commit -m "Select ASIO with pre-commit WASAPI fallback"
```

---

### Task 14: Package the GPL source boundary and run full static verification

**Files:**
- Create: `cmake/PackageCorrespondingSource.cmake`
- Create: `tests/CMake/CorrespondingSourcePackageTests.cmake`
- Create: `SOURCE-OFFER.md`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMake/CMakeLists.txt`
- Modify: `LICENSE.md`
- Modify: `THIRD_PARTY_NOTICES.md`

**Interfaces:**
- Consumes: one clean committed repository revision, the exact external SDK root, configured FetchContent source directories, license files, and build instructions.
- Produces: an ASIO-enabled binary distribution with notices and a reproducible matching-source ZIP tied to the exact revision.

- [ ] **Step 1: Test source-package completeness with fixture directories**

Drive `PackageCorrespondingSource.cmake` in script mode with a temporary fake project archive, fake ASIO SDK, and fake dependency source trees. Assert the staged archive contains:

```text
GCLoader source at the recorded commit
complete ASIOSDK tree including LICENSE.txt and PDFs
all configured FetchContent source trees and their license files
CMakePresets.json, cmake modules, config template, and build instructions
LICENSE.md, LICENSES/, THIRD_PARTY_NOTICES.md, SOURCE-OFFER.md
corresponding-source-manifest.txt with commit and SHA-256 inventory
configure-offline.ps1 with local SDK and FetchContent source overrides
```

Assert missing SDK/license/dependency input, dirty-tree marker, archive error, and hash error abort packaging without publishing a partial final ZIP. Staging lives only under the build directory.

- [ ] **Step 2: Implement a clean-revision package target**

At configure time, capture every direct and transitive `FetchContent` source directory actually used. Add `gc-package-corresponding-source`, which:

1. refuses to run unless `git status --porcelain` is empty;
2. records `git rev-parse HEAD`;
3. uses `git archive HEAD` for project-owned tracked sources;
4. copies the complete external `GC_ASIO_SDK_DIR` tree, not a mutable link;
5. copies every configured dependency source tree used to build the binaries;
6. writes dependency origin/tag/hash, toolchain, configure/build commands, and `FETCHCONTENT_SOURCE_DIR_<NAME>` overrides for every copied source;
7. hashes the staged files and emits `GCLoader-<commit>-corresponding-source.zip` under `${CMAKE_BINARY_DIR}/source-package` atomically;
8. copies that exact verified ZIP beside the ASIO-enabled binary distribution in `${GC_DIST_DIR}`.

Do not include vendor ASIO driver binaries, runtime game files, build outputs, credentials, local configs, or `H:\gc`. Explain in `SOURCE-OFFER.md` that project-authored files remain offered under CC0 individually, while the distributed ASIO-enabled combined work is GPL-3.0-only and third-party files keep their notices. Treat this as the approved distribution policy, not legal advice.

Copy `LICENSE.md`, `LICENSES/CC0-1.0.txt`, `LICENSES/GPL-3.0-only.txt`, `THIRD_PARTY_NOTICES.md`, `SOURCE-OFFER.md`, the ASIO SDK `LICENSE.txt`, and the compatible logo into every ASIO-enabled `${GC_DIST_DIR}`.

- [ ] **Step 3: Run focused and full Debug verification under x86 MSVC**

From a PowerShell prompt:

```powershell
$vcvars = 'C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat'
$env:GC_ASIO_SDK_DIR = 'H:\gc\artifacts\ASIOSDK'
cmd.exe /d /s /c "call `"$vcvars`" >nul && cmake --preset msvc32-debug && cmake --build --preset msvc32-debug && ctest --preset msvc32-debug -j 4"
```

Expected: configure identifies an external SDK 2.3.4+, every target including `iDmacDrv32`, `ConfigGUI`, and `AsioProbe` builds as x86, and the complete Debug CTest suite passes.

- [ ] **Step 4: Run full RelWithDebInfo verification and inspect artifacts**

```powershell
$vcvars = 'C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat'
$env:GC_ASIO_SDK_DIR = 'H:\gc\artifacts\ASIOSDK'
cmd.exe /d /s /c "call `"$vcvars`" >nul && cmake --preset msvc32-release && cmake --build --preset msvc32-release && ctest --preset msvc32-release -j 4"
cmd.exe /d /s /c "call `"$vcvars`" >nul && dumpbin /headers build-msvc32-release\dist\iDmacDrv32.dll | findstr /i machine && dumpbin /headers build-msvc32-release\dist\AsioProbe.exe | findstr /i machine"
```

Expected: both PE files report x86; `dumpbin /exports` shows the loader's existing export set unchanged; `dist` includes ConfigGUI, AsioProbe, the chosen compatible ASIO logo, and every required notice. Do not run the clean-tree-only source-package target until the packaging changes are committed in Step 5.

- [ ] **Step 5: Review the exact revision and commit packaging**

```powershell
git diff --check
git status --short
git diff --stat
git add -- CMakeLists.txt cmake/PackageCorrespondingSource.cmake tests/CMake/CMakeLists.txt tests/CMake/CorrespondingSourcePackageTests.cmake SOURCE-OFFER.md LICENSE.md THIRD_PARTY_NOTICES.md
git commit -m "Package ASIO corresponding source"
```

After the commit, generate and verify the archive against that exact clean revision:

```powershell
cmake --build --preset msvc32-release --target gc-package-corresponding-source
# Extract the emitted ZIP beneath build-msvc32-release/source-package/verify,
# then run its generated configure-offline.ps1 from vcvars32.
```

The generated script must set `GC_ASIO_SDK_DIR` to the copied `third_party/asiosdk`, pass every copied `FETCHCONTENT_SOURCE_DIR_<NAME>` override, and set `FETCHCONTENT_FULLY_DISCONNECTED=ON`. Configure and build `iDmacDrv32`, `ConfigGUI`, and `AsioProbe` from the extracted tree with network access disabled. Do not deploy any resulting binary to `H:\gc` in this task.

---

### Task 15: Validate the actual Xonar driver and record gameplay acceptance

**Files:**
- Create: `docs/reverse-engineering/asio-runtime-validation.md`

**Interfaces:**
- Consumes: the verified release build, local 32-bit `XONAR SOUND CARD` registration, source-tree helper output, explicit operator deployment authorization, and human audible/gameplay observation.
- Produces: reproducible hardware evidence separated into probe, runtime, latency, and acceptance claims.

- [ ] **Step 1: Capture a non-streaming probe baseline**

Use the release ConfigGUI, which is the protocol client for the sibling `AsioProbe.exe`, to run inspection and exact validation requests for:

```toml
audio_backend = 'asio'
asio_driver_name = 'XONAR SOUND CARD'
asio_buffer_frames = 192
asio_output_base_channel = 0
```

Record helper/build SHA-256 values and the complete structured report. The expected local baseline is 0 inputs, 8 outputs, 48 kHz support, limits 192/2400/preferred 192/granularity 1, selected `Int24LSB` channels 0/1, and 384 output-latency frames. Treat differences as current driver truth requiring investigation; do not add a Xonar exception to make the expected values pass.

Repeat the ConfigGUI save test against a temporary config: valid exact settings save; an arbitrary missing name, unsupported frames, and invalid channel pair visibly fail and leave bytes unchanged. Record other common driver names as untested suggestions unless they are actually installed and probed.

- [ ] **Step 2: Stop for explicit runtime-deployment authorization**

Present the clean release build identity, passing tests, probe result, planned backup paths, exact `H:\gc` files that would be replaced, and rollback steps. Do not copy, replace, launch, or edit runtime files until the operator explicitly authorizes deployment.

- [ ] **Step 3: After authorization, compare WASAPI and ASIO under the same game conditions**

Back up only the named runtime loader/config files, deploy the authorized release artifacts, and test these two explicit configurations without changing unrelated settings:

```toml
audio_backend = 'wasapi_exclusive'
wasapi_exclusive_buffer_ms = 10
```

```toml
audio_backend = 'asio'
asio_driver_name = 'XONAR SOUND CARD'
asio_buffer_frames = 192
asio_output_base_channel = 0
```

For each, run menu audio, song start, sustained 120 FPS gameplay, pause/resume, retry/seek, result transition, and repeated-song sessions. Record active/requested backend, callbacks, deadline misses, silence substitutions, clock validation, cursor failures, skipped/gap counters, post-commit faults, and audible crackle/dropout/desynchronization observations.

Verify a pre-commit invalid driver/buffer falls back once to WASAPI with no config rewrite, then restore the accepted config. The post-commit stop/restart policy is already mandatory in automated tests; exercise an actual hardware reset only if the operator separately approves a safe reproduction. Never disable or uninstall the sound device merely to satisfy this checklist.

- [ ] **Step 4: Make latency claims evidence-bound**

Report the theoretical block/driver figures separately from measured end-to-end latency. At 48 kHz, 192 frames is 4 ms per block and the observed 384-frame driver output latency is 8 ms, but neither alone proves game-to-speaker latency. Claim improvement over 10 ms WASAPI only after stable gameplay and preferably a reproducible loopback/photodiode measurement using identical conditions.

If 192 frames misses deadlines or crackles, record that result before testing a larger driver-accepted exact frame count. Do not silently change the saved value or describe a larger fallback buffer as 192-frame success.

- [ ] **Step 5: Record acceptance and commit the evidence**

Use four explicit verdicts in `asio-runtime-validation.md`:

```text
Static/build verification: PASS|FAIL
Xonar helper capability validation: PASS|FAIL
In-game stability and synchronization: ACCEPTED|REJECTED|NOT RUN
Measured latency improvement: PROVEN|NOT PROVEN
```

Include build commit/hash, SDK identification, driver-reported values, exact config, test duration/scenarios, logs/counter excerpts, human observations, and rollback status. Do not turn helper success into a gameplay-acceptance claim.

```powershell
git add -- docs/reverse-engineering/asio-runtime-validation.md
git commit -m "Record ASIO runtime validation"
```

---

## Final Definition of Done

- [ ] A clean x86 configure fails clearly without `GC_ASIO_SDK_DIR` and succeeds from either the cache path or environment variable with the approved SDK.
- [ ] DirectSound, WASAPI exclusive, and ASIO are strict persisted choices; legacy Boolean migration is canonical and conflict-safe.
- [ ] ConfigGUI accepts arbitrary names, offers installed/common choices, isolates vendor code, and performs zero writes on failed final validation.
- [ ] Runtime re-resolves and revalidates the exact 32-bit driver, frames, channel pair, sample formats, and 48-kHz rate.
- [ ] Xonar's 192-frame packed-24 path is covered generically, with no device-name conditional.
- [ ] Callback deferral, clock priming, physical presentation, buffer placement, and teardown ordering have behavioral tests.
- [ ] ASIO pre-commit failures fall back exactly once to WASAPI; post-commit failures stop and request restart without a hot clock switch.
- [ ] Existing WASAPI behavior and the DirectSound facade contract remain green.
- [ ] Debug and RelWithDebInfo builds and full CTest suites pass under MSVC x86.
- [ ] GPL/CC0/third-party notices ship with binaries, and the exact clean revision produces a complete matching-source archive.
- [ ] No runtime deployment or gameplay-success claim occurs without explicit authorization and recorded human acceptance.

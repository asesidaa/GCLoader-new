# GCLoader Source/Build Foundation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Move every DLL/runtime C++ source under the approved `src/` tree, move ConfigGUI and the decrypt archive under `tools/`, mirror tests by feature, and replace repeated production source lists with reusable CMake targets without changing runtime behavior.

**Architecture:** This is Subproject 1 from the approved architecture-modernization design. It is a mechanical ownership migration: existing Interfaces, namespaces, hook behavior, config behavior, exported ABI, and test behavior remain unchanged while sources and tests move feature-by-feature into static-library targets. The root build stays green after every task, then becomes a thin composition file only after all feature targets exist.

**Tech Stack:** C++23, CMake 3.31+, Ninja, 32-bit MSVC 18 Insiders, SDL3, MinHook, SafetyHook, miniaudio, reflect-cpp, toml++, plog, ImGui, PowerShell, CTest.

## Global Constraints

- Work only in `H:\gc\artifacts\GCLoader`; `H:\gc` is runtime/deploy scope and must not be modified.
- Execute this plan in an isolated worktree created with `superpowers:using-git-worktrees`.
- Build only x86. Keep the `_M_IX86` guard and use `C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat`.
- Keep `CMAKE_CXX_STANDARD 23`, the static MSVC runtime, UTF-8 compilation, warning policy, `NOMINMAX`, and the localized Ninja `/showIncludes` repair exactly active.
- Use a worktree-local `build-msvc32-latest` Ninja/`RelWithDebInfo` build directory. Task 1 creates it from the untouched baseline; reconfigure it after every CMake ownership change.
- Preserve the `iDmacDrv32` DLL name, all 15 export names, x86 calling conventions, and ordinals 1 through 15.
- Preserve all 28 existing executable and CTest names.
- Keep root `config.toml` unchanged. Do not change schema, defaults, validation, strictness, or ConfigGUI missing-file behavior in this subproject.
- Do not change namespaces, function signatures, hook inventories, RVAs, signatures, feature gates, error behavior, logging behavior, or process lifetime.
- Do not consolidate MinHook transactions, remove `ConfigManager`, split `FrameratePatch.cpp`, deepen FastIO, or add Loader rollback; those belong to subsequent approved subprojects.
- Do not add forwarding headers for old include paths. Update every caller to the new canonical include path in the same task that moves the header.
- Use `git mv` for tracked relocations and `apply_patch` for content changes.
- Tests must link the same static-library implementation used by `iDmacDrv32`; a test CMake file must not compile a production `.cpp` directly after its feature task is complete.
- No task may claim gameplay verification. Each task requires focused build/tests, all 28 CTests, `git diff --check`, and a clean intended diff before commit.
- Preserve `zero_decrypt.zip` byte-for-byte. Its SHA-256 must remain `725BB06CD4DB0EB4B4789DC76D3FF6CDDEF212D429E27224760DF1322217C175`.

## Baseline Contract

The plan was written against this verified 2026-07-18 baseline:

- `ctest --test-dir build-msvc32-latest --output-on-failure`: `100% tests passed, 0 tests failed out of 28`.
- `build-msvc32-latest\iDmacDrv32.dll`: 15 named exports.

```text
1 iDmacDrvOpen
2 iDmacDrvClose
3 iDmacDrvDmaRead
4 iDmacDrvDmaWrite
5 iDmacDrvRegisterRead
6 iDmacDrvRegisterWrite
7 iDmacDrvRegisterBufferRead
8 iDmacDrvRegisterBufferWrite
9 iDmacDrvMemoryRead
10 iDmacDrvMemoryWrite
11 iDmacDrvMemoryBufferRead
12 iDmacDrvMemoryBufferWrite
13 iDmacDrvMemoryReadExt
14 iDmacDrvMemoryWriteExt
15 iDmacDrvProgramDownload
```

- `zero_decrypt.zip` SHA-256: `725BB06CD4DB0EB4B4789DC76D3FF6CDDEF212D429E27224760DF1322217C175`.

---

### Task 1: Capture the Baseline and Extract Common CMake Policy

**Files:**
- Create: `cmake/ProjectOptions.cmake`
- Create: `cmake/Dependencies.cmake`
- Modify: `CMakeLists.txt`
- Reference: `docs/superpowers/specs/2026-07-17-gcloader-codebase-architecture-modernization-design.md`

**Interfaces:**
- Consumes: the current top-level CMake configuration and existing `build-msvc32-latest` artifacts.
- Produces: `ProjectOptions.cmake` for invariant compiler/build policy and `Dependencies.cmake` for pinned third-party targets; all existing target names remain available.

- [ ] **Step 1: Build the untouched worktree and capture normalized baseline evidence outside the repository**

Run from PowerShell:

```powershell
$vcvars = 'C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat'
$commandLine = 'call "' + $vcvars + '" >nul && cmake -S . -B build-msvc32-latest -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo && cmake --build build-msvc32-latest --config RelWithDebInfo'
cmd.exe /d /s /c $commandLine
if ($LASTEXITCODE -ne 0) { throw "baseline configure/build failed: $LASTEXITCODE" }

$evidence = Join-Path $env:TEMP 'gcloader-source-build-foundation'
New-Item -ItemType Directory -Force -Path $evidence | Out-Null

ctest --test-dir build-msvc32-latest -N |
    Set-Content (Join-Path $evidence 'ctest-before.txt')

$rawExports = Join-Path $evidence 'exports-before-raw.txt'
$commandLine = 'call "' + $vcvars + '" >nul && dumpbin /nologo /exports "build-msvc32-latest\iDmacDrv32.dll" > "' + $rawExports + '"'
cmd.exe /d /s /c $commandLine
if ($LASTEXITCODE -ne 0) { throw "dumpbin failed: $LASTEXITCODE" }

$normalized = Get-Content $rawExports | ForEach-Object {
    if ($_ -match '^\s*(\d+)\s+\S+\s+\S+\s+(iDmacDrv[A-Za-z0-9]+)') {
        [pscustomobject]@{
            Ordinal = [int]$Matches[1]
            Name = $Matches[2]
        }
    }
} | Sort-Object Ordinal | ForEach-Object { "$($_.Ordinal) $($_.Name)" }
$normalized | Set-Content (Join-Path $evidence 'exports-before.txt')

(Get-FileHash zero_decrypt.zip -Algorithm SHA256).Hash |
    Set-Content (Join-Path $evidence 'zero-decrypt-before.sha256')

Get-Content (Join-Path $evidence 'exports-before.txt')
Get-Content (Join-Path $evidence 'zero-decrypt-before.sha256')
```

Expected: the 15-line export list and hash from **Baseline Contract** exactly.

- [ ] **Step 2: Verify the complete baseline test suite**

Run:

```powershell
ctest --test-dir build-msvc32-latest --output-on-failure
```

Expected: `100% tests passed, 0 tests failed out of 28`.

- [ ] **Step 3: Create the common project-options file**

Create `cmake/ProjectOptions.cmake` with exactly:

```cmake
# CMake 4.2 can decode localized MSVC /showIncludes output twice on a
# Simplified Chinese host. Ninja then records zero header dependencies because
# its generated prefix is mojibake. Repair only that detected value; leave all
# other locales and generators untouched.
if(MSVC AND CMAKE_GENERATOR MATCHES "^Ninja" AND
        CMAKE_CL_SHOWINCLUDES_PREFIX MATCHES "^娉")
    set(CMAKE_C_CL_SHOWINCLUDES_PREFIX "注意: 包含文件:  ")
    set(CMAKE_CXX_CL_SHOWINCLUDES_PREFIX "注意: 包含文件:  ")
    set(CMAKE_CL_SHOWINCLUDES_PREFIX "注意: 包含文件:  ")
endif()

set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY
            "MultiThreaded$<$<CONFIG:Debug>:Debug>"
            CACHE INTERNAL "")
    add_compile_options(/W3 /utf-8)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

add_definitions(-DNOMINMAX)
```

- [ ] **Step 4: Create the pinned-dependencies file**

Move the existing `FetchContent` declarations and ImGui target unchanged into `cmake/Dependencies.cmake`. The complete file must be:

```cmake
include(FetchContent)

FetchContent_Declare(
        SDL3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG f87239e71e42da91ca317a12eefb82cfbf3393eb
)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(SDL3)

FetchContent_Declare(
        minhook
        GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
        GIT_TAG c3fcafdc10146beb5919319d0683e44e3c30d537
)
FetchContent_MakeAvailable(minhook)

set(MINIAUDIO_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(MINIAUDIO_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(MINIAUDIO_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(MINIAUDIO_INSTALL OFF CACHE BOOL "" FORCE)
set(MINIAUDIO_NO_EXTRA_NODES ON CACHE BOOL "" FORCE)
set(MINIAUDIO_NO_LIBVORBIS ON CACHE BOOL "" FORCE)
set(MINIAUDIO_NO_LIBOPUS ON CACHE BOOL "" FORCE)
set(MINIAUDIO_NO_DEVICEIO ON CACHE BOOL "" FORCE)
set(MINIAUDIO_NO_DECODING ON CACHE BOOL "" FORCE)
set(MINIAUDIO_NO_ENCODING ON CACHE BOOL "" FORCE)
set(MINIAUDIO_NO_RESOURCE_MANAGER ON CACHE BOOL "" FORCE)
set(MINIAUDIO_NO_GENERATION ON CACHE BOOL "" FORCE)
FetchContent_Declare(
        miniaudio
        GIT_REPOSITORY https://github.com/mackron/miniaudio.git
        GIT_TAG 9634bedb5b5a2ca38c1ee7108a9358a4e233f14d
)
FetchContent_MakeAvailable(miniaudio)
target_compile_definitions(miniaudio PUBLIC
        MA_NO_DEVICE_IO
        MA_NO_DECODING
        MA_NO_ENCODING
        MA_NO_RESOURCE_MANAGER
        MA_NO_GENERATION
)

FetchContent_Declare(
        tomlplusplus
        GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
        GIT_TAG v3.4.0
)
FetchContent_MakeAvailable(tomlplusplus)
export(TARGETS tomlplusplus_tomlplusplus
        FILE "${CMAKE_BINARY_DIR}/tomlplusplus-config.cmake")
set(tomlplusplus_DIR "${CMAKE_BINARY_DIR}")

set(SAFETYHOOK_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(SAFETYHOOK_BUILD_TEST OFF CACHE BOOL "" FORCE)
set(SAFETYHOOK_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SAFETYHOOK_FETCH_ZYDIS ON CACHE BOOL "" FORCE)
FetchContent_Declare(
        safetyhook
        GIT_REPOSITORY https://github.com/cursey/safetyhook.git
        GIT_TAG v0.7.0
)
FetchContent_MakeAvailable(safetyhook)

FetchContent_Declare(
        reflectcpp
        URL https://github.com/getml/reflect-cpp/archive/refs/tags/v0.25.0.zip
)
set(REFLECTCPP_TOML ON CACHE BOOL "" FORCE)
set(REFLECTCPP_XML OFF CACHE BOOL "" FORCE)
set(REFLECTCPP_USE_VCPKG OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(reflectcpp)

FetchContent_Declare(
        plog
        GIT_REPOSITORY https://github.com/SergiusTheBest/plog
        GIT_TAG 1.1.11
)
FetchContent_MakeAvailable(plog)

FetchContent_Declare(
        imgui_external
        URL https://github.com/ocornut/imgui/archive/refs/tags/v1.92.8.tar.gz
        EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(imgui_external)

add_library(imgui
        ${imgui_external_SOURCE_DIR}/imgui.cpp
        ${imgui_external_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_external_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_external_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_external_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
        ${imgui_external_SOURCE_DIR}/backends/imgui_impl_sdlrenderer3.cpp
        ${imgui_external_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
)
target_include_directories(imgui PUBLIC
        ${imgui_external_SOURCE_DIR}
        ${SDL3_SOURCE_DIR}/include
)
```

- [ ] **Step 5: Replace the duplicated top-level policy/dependency blocks**

The beginning of `CMakeLists.txt` must become:

```cmake
cmake_minimum_required(VERSION 3.31)
project(GCLoader VERSION 1.0.0 LANGUAGES C CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(ProjectOptions)

find_package(Git REQUIRED)
include(Dependencies)
```

Keep the existing `set(SOURCES ...)`, targets, tests, and `.def` setup below this point. Delete the duplicate final `if(MSVC) set(CMAKE_MSVC_RUNTIME_LIBRARY ...) endif()` block because `ProjectOptions.cmake` now owns it.

- [ ] **Step 6: Reconfigure and build the existing targets**

Run:

```powershell
$vcvars = 'C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat'
$commandLine = 'call "' + $vcvars + '" >nul && cmake -S . -B build-msvc32-latest -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo && cmake --build build-msvc32-latest --target iDmacDrv32 ConfigGUI --config RelWithDebInfo'
cmd.exe /d /s /c $commandLine
if ($LASTEXITCODE -ne 0) { throw "configure/build failed: $LASTEXITCODE" }
ctest --test-dir build-msvc32-latest --output-on-failure
```

Expected: configure/build exit `0`; `100% tests passed, 0 tests failed out of 28`.

- [ ] **Step 7: Review and commit the CMake extraction**

Run:

```powershell
git diff --check
git status --short
git diff -- CMakeLists.txt cmake/ProjectOptions.cmake cmake/Dependencies.cmake
```

Expected: only the three intended CMake files differ; no source/target behavior changes.

Commit:

```powershell
git add -- CMakeLists.txt cmake/ProjectOptions.cmake cmake/Dependencies.cmake
git commit -m "build: extract project options and dependencies"
```

---

### Task 2: Move Configuration, Key Mapping, and ConfigGUI

**Files:**
- Create: `src/CMakeLists.txt`
- Create: `src/Config/CMakeLists.txt`
- Create: `src/Nesys/Network/CMakeLists.txt`
- Create: `tools/CMakeLists.txt`
- Create: `tools/ConfigGUI/CMakeLists.txt`
- Create: `tests/CMakeLists.txt`
- Create: `tests/Config/CMakeLists.txt`
- Move: `config.h` → `src/Config/config.h`
- Move: `config.cpp` → `src/Config/config.cpp`
- Move: `RegistryConfig.h` → `src/Config/RegistryConfig.h`
- Move: `RegistryConfig.cpp` → `src/Config/RegistryConfig.cpp`
- Move: `SdlRflParsers.h` → `src/Config/SdlRflParsers.h`
- Move: `NesysNetworkConfig.h` → `src/Nesys/Network/NesysNetworkConfig.h`
- Move: `NesysNetworkConfig.cpp` → `src/Nesys/Network/NesysNetworkConfig.cpp`
- Move: `WinKeyMapping.h` → `src/Platform/Win32/KeyMapping.h`
- Move: `GUI_main.cpp` → `tools/ConfigGUI/Main.cpp`
- Move: `tests/ConfigFeatureTests.cpp` → `tests/Config/ConfigFeatureTests.cpp`
- Modify: all callers containing old configuration/key-mapping include paths
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: unchanged `InputConfig`, `ConfigManager`, registry validation, IPv4 validation, SDL/reflect-cpp codecs, and key-mapping functions.
- Produces: `gc_nesys_network_config` and `gc_config` static targets; canonical include paths rooted at `src`; unchanged `ConfigGUI` and `ConfigFeatureTests` targets.

- [ ] **Step 1: Confirm focused tests are green before moving files**

Run:

```powershell
ctest --test-dir build-msvc32-latest --output-on-failure -R '^(ConfigFeatureTests|WasapiAudioPatchTests|InputPollingRuntimeStartupTests|RegistryConfigOverrideTests|ServerAddressOverrideTests)$'
```

Expected: all five selected tests pass.

- [ ] **Step 2: Move the tracked files**

Run:

```powershell
New-Item -ItemType Directory -Force src/Config, src/Nesys/Network, src/Platform/Win32, tools/ConfigGUI, tests/Config | Out-Null
git mv config.h src/Config/config.h
git mv config.cpp src/Config/config.cpp
git mv RegistryConfig.h src/Config/RegistryConfig.h
git mv RegistryConfig.cpp src/Config/RegistryConfig.cpp
git mv SdlRflParsers.h src/Config/SdlRflParsers.h
git mv NesysNetworkConfig.h src/Nesys/Network/NesysNetworkConfig.h
git mv NesysNetworkConfig.cpp src/Nesys/Network/NesysNetworkConfig.cpp
git mv WinKeyMapping.h src/Platform/Win32/KeyMapping.h
git mv GUI_main.cpp tools/ConfigGUI/Main.cpp
git mv tests/ConfigFeatureTests.cpp tests/Config/ConfigFeatureTests.cpp
```

Expected: `git status --short` reports only renames before include/CMake edits.

- [ ] **Step 3: Update canonical include paths without changing declarations**

Apply these exact textual replacements to tracked `.cpp`/`.h` files:

```text
#include "config.h"              -> #include "Config/config.h"
#include "RegistryConfig.h"      -> #include "Config/RegistryConfig.h"
#include "SdlRflParsers.h"       -> #include "Config/SdlRflParsers.h"
#include "NesysNetworkConfig.h"  -> #include "Nesys/Network/NesysNetworkConfig.h"
#include "WinKeyMapping.h"       -> #include "Platform/Win32/KeyMapping.h"
```

Do not rename `ConfigManager`, schema types, namespaces, functions, or fields.

Verify:

```powershell
$oldIncludes = rg -n '#include "(config|RegistryConfig|SdlRflParsers|NesysNetworkConfig|WinKeyMapping)\.h"' -g '*.cpp' -g '*.h'
if ($LASTEXITCODE -eq 0) { $oldIncludes; throw 'old configuration include remains' }
if ($LASTEXITCODE -gt 1) { throw "rg failed: $LASTEXITCODE" }
```

Expected: no matches.

- [ ] **Step 4: Add the configuration and tool CMake targets**

Create `src/Nesys/Network/CMakeLists.txt`:

```cmake
add_library(gc_nesys_network_config STATIC
        NesysNetworkConfig.cpp
)
target_include_directories(gc_nesys_network_config PUBLIC
        ${PROJECT_SOURCE_DIR}/src
)
```

Create `src/Config/CMakeLists.txt`:

```cmake
add_library(gc_config STATIC
        config.cpp
        RegistryConfig.cpp
)
target_include_directories(gc_config PUBLIC
        ${PROJECT_SOURCE_DIR}/src
        ${SDL3_SOURCE_DIR}/include
        ${reflectcpp_SOURCE_DIR}/include
        ${plog_SOURCE_DIR}/include
)
target_link_libraries(gc_config PUBLIC
        gc_nesys_network_config
        SDL3-static
        tomlplusplus::tomlplusplus
        reflectcpp
)
```

Create `src/CMakeLists.txt`:

```cmake
add_subdirectory(Nesys/Network)
add_subdirectory(Config)
```

Create `tools/ConfigGUI/CMakeLists.txt`:

```cmake
add_executable(ConfigGUI Main.cpp)
target_include_directories(ConfigGUI PRIVATE
        ${PROJECT_SOURCE_DIR}/src
        ${SDL3_SOURCE_DIR}/include
        ${reflectcpp_SOURCE_DIR}/include
        ${plog_SOURCE_DIR}/include
)
target_link_libraries(ConfigGUI PRIVATE
        gc_config
        SDL3-static
        ntdll
        tomlplusplus::tomlplusplus
        reflectcpp
        imgui
)
```

Create `tools/CMakeLists.txt`:

```cmake
add_subdirectory(ConfigGUI)
```

Create `tests/Config/CMakeLists.txt`:

```cmake
add_executable(ConfigFeatureTests ConfigFeatureTests.cpp)
target_link_libraries(ConfigFeatureTests PRIVATE gc_config)
add_test(NAME ConfigFeatureTests COMMAND ConfigFeatureTests)
```

Create `tests/CMakeLists.txt`:

```cmake
add_subdirectory(Config)
```

- [ ] **Step 5: Switch existing targets from repeated config sources to `gc_config`**

In root `CMakeLists.txt`:

1. Add this immediately after `include(Dependencies)`:

```cmake
enable_testing()
add_subdirectory(src)
add_subdirectory(tools)
add_subdirectory(tests)
```

2. Remove `config.cpp`, `NesysNetworkConfig.cpp`, and `RegistryConfig.cpp` from `SOURCES`.
3. Delete `GUI_SOURCES` and the root `ConfigGUI` target block.
4. Add `${PROJECT_SOURCE_DIR}/src` to `iDmacDrv32` include directories and add `gc_config` to its link libraries.
5. Delete the old root `enable_testing()` and `ConfigFeatureTests` block.
6. Remove the three moved production `.cpp` files from `WasapiAudioPatchTests`, `InputPollingRuntimeStartupTests`, `RegistryConfigOverrideTests`, and `ServerAddressOverrideTests`.
7. Add these links to the existing test targets:

```cmake
target_link_libraries(WasapiAudioPatchTests PRIVATE gc_config)
target_link_libraries(InputPollingRuntimeStartupTests PRIVATE gc_config)
target_link_libraries(RegistryConfigOverrideTests PRIVATE gc_config)
target_link_libraries(ServerAddressOverrideTests PRIVATE
        gc_nesys_network_config)
```

Keep every other source and link dependency in those target blocks unchanged.

- [ ] **Step 6: Reconfigure, build affected targets, and run focused tests**

Run:

```powershell
$vcvars = 'C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat'
$targets = 'iDmacDrv32 ConfigGUI ConfigFeatureTests WasapiAudioPatchTests InputPollingRuntimeStartupTests RegistryConfigOverrideTests ServerAddressOverrideTests'
$commandLine = 'call "' + $vcvars + '" >nul && cmake -S . -B build-msvc32-latest -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo && cmake --build build-msvc32-latest --target ' + $targets + ' --config RelWithDebInfo'
cmd.exe /d /s /c $commandLine
if ($LASTEXITCODE -ne 0) { throw "configure/build failed: $LASTEXITCODE" }
ctest --test-dir build-msvc32-latest --output-on-failure -R '^(ConfigFeatureTests|WasapiAudioPatchTests|InputPollingRuntimeStartupTests|RegistryConfigOverrideTests|ServerAddressOverrideTests)$'
```

Expected: build exit `0`; all five focused tests pass.

- [ ] **Step 7: Run the complete regression suite and commit**

Run:

```powershell
ctest --test-dir build-msvc32-latest --output-on-failure
git diff --check
git status --short
```

Expected: `100% tests passed, 0 tests failed out of 28`; changes are limited to the declared moves, callers, and CMake files.

Commit:

```powershell
git add -- CMakeLists.txt src tools tests
git commit -m "refactor: organize configuration and config gui sources"
```

---

### Task 3: Organize Audio Sources Behind `gc_audio`

**Files:**
- Move: `DirectSoundFacade.*` -> `src/Audio/DirectSound/`
- Move: `AudioCursorTimeline.*`, `AudioSnapshot.*`, `MiniaudioMixer.*` -> `src/Audio/Mixer/`
- Move: `ExclusiveAudioEngine*`, `OutputPacingTracker.*`, `WasapiAudioPatch*`, `WasapiAudioTypes.*`, `WasapiEndpoint.*` -> `src/Audio/Wasapi/`
- Move: the ten root `tests/*Audio*Tests.cpp`, `tests/DirectSoundDeviceTests.cpp`, `tests/MiniaudioMixerTests.cpp`, `tests/OutputPacingTrackerTests.cpp`, and `tests/SecondarySoundBufferTests.cpp` listed below -> `tests/Audio/`
- Create: `src/Audio/CMakeLists.txt`
- Create: `tests/Audio/CMakeLists.txt`
- Modify: `src/CMakeLists.txt`, `tests/CMakeLists.txt`, root `CMakeLists.txt`, moved sources, and moved tests

**Interfaces:**
- Consumes: `gc_config` from Task 2 and the existing nine audio implementation translation units.
- Produces: one reusable `gc_audio` static library; all ten existing audio test executable/CTest names remain unchanged.

- [ ] **Step 1: Create the audio directories and move production files**

Run:

```powershell
New-Item -ItemType Directory -Force -Path `
    src/Audio/DirectSound, `
    src/Audio/Mixer, `
    src/Audio/Wasapi, `
    tests/Audio | Out-Null

git mv DirectSoundFacade.cpp src/Audio/DirectSound/DirectSoundFacade.cpp
git mv DirectSoundFacade.h src/Audio/DirectSound/DirectSoundFacade.h

git mv AudioCursorTimeline.cpp src/Audio/Mixer/AudioCursorTimeline.cpp
git mv AudioCursorTimeline.h src/Audio/Mixer/AudioCursorTimeline.h
git mv AudioSnapshot.cpp src/Audio/Mixer/AudioSnapshot.cpp
git mv AudioSnapshot.h src/Audio/Mixer/AudioSnapshot.h
git mv MiniaudioMixer.cpp src/Audio/Mixer/MiniaudioMixer.cpp
git mv MiniaudioMixer.h src/Audio/Mixer/MiniaudioMixer.h

git mv ExclusiveAudioEngine.cpp src/Audio/Wasapi/ExclusiveAudioEngine.cpp
git mv ExclusiveAudioEngine.h src/Audio/Wasapi/ExclusiveAudioEngine.h
git mv ExclusiveAudioEngineInternal.h src/Audio/Wasapi/ExclusiveAudioEngineInternal.h
git mv OutputPacingTracker.cpp src/Audio/Wasapi/OutputPacingTracker.cpp
git mv OutputPacingTracker.h src/Audio/Wasapi/OutputPacingTracker.h
git mv WasapiAudioPatch.cpp src/Audio/Wasapi/WasapiAudioPatch.cpp
git mv WasapiAudioPatch.h src/Audio/Wasapi/WasapiAudioPatch.h
git mv WasapiAudioPatchInternal.h src/Audio/Wasapi/WasapiAudioPatchInternal.h
git mv WasapiAudioTypes.cpp src/Audio/Wasapi/WasapiAudioTypes.cpp
git mv WasapiAudioTypes.h src/Audio/Wasapi/WasapiAudioTypes.h
git mv WasapiEndpoint.cpp src/Audio/Wasapi/WasapiEndpoint.cpp
git mv WasapiEndpoint.h src/Audio/Wasapi/WasapiEndpoint.h
```

Expected: every audio implementation/header is below `src/Audio`; no file content has changed yet.

- [ ] **Step 2: Move the ten audio tests**

Run:

```powershell
git mv tests/AudioCursorTimelineTests.cpp tests/Audio/AudioCursorTimelineTests.cpp
git mv tests/AudioFormatTests.cpp tests/Audio/AudioFormatTests.cpp
git mv tests/AudioSnapshotTests.cpp tests/Audio/AudioSnapshotTests.cpp
git mv tests/DirectSoundDeviceTests.cpp tests/Audio/DirectSoundDeviceTests.cpp
git mv tests/ExclusiveAudioEngineTests.cpp tests/Audio/ExclusiveAudioEngineTests.cpp
git mv tests/MiniaudioMixerTests.cpp tests/Audio/MiniaudioMixerTests.cpp
git mv tests/OutputPacingTrackerTests.cpp tests/Audio/OutputPacingTrackerTests.cpp
git mv tests/SecondarySoundBufferTests.cpp tests/Audio/SecondarySoundBufferTests.cpp
git mv tests/WasapiAudioPatchTests.cpp tests/Audio/WasapiAudioPatchTests.cpp
git mv tests/WasapiEndpointTests.cpp tests/Audio/WasapiEndpointTests.cpp
```

- [ ] **Step 3: Rewrite every audio include to its canonical feature path**

Apply these literal replacements across `src`, `tests`, and the still-root `dllmain.cpp`:

| Old include | New include |
|---|---|
| `"AudioCursorTimeline.h"` | `"Audio/Mixer/AudioCursorTimeline.h"` |
| `"AudioSnapshot.h"` | `"Audio/Mixer/AudioSnapshot.h"` |
| `"MiniaudioMixer.h"` | `"Audio/Mixer/MiniaudioMixer.h"` |
| `"DirectSoundFacade.h"` | `"Audio/DirectSound/DirectSoundFacade.h"` |
| `"ExclusiveAudioEngine.h"` | `"Audio/Wasapi/ExclusiveAudioEngine.h"` |
| `"ExclusiveAudioEngineInternal.h"` | `"Audio/Wasapi/ExclusiveAudioEngineInternal.h"` |
| `"OutputPacingTracker.h"` | `"Audio/Wasapi/OutputPacingTracker.h"` |
| `"WasapiAudioPatch.h"` | `"Audio/Wasapi/WasapiAudioPatch.h"` |
| `"WasapiAudioPatchInternal.h"` | `"Audio/Wasapi/WasapiAudioPatchInternal.h"` |
| `"WasapiAudioTypes.h"` | `"Audio/Wasapi/WasapiAudioTypes.h"` |
| `"WasapiEndpoint.h"` | `"Audio/Wasapi/WasapiEndpoint.h"` |

Run:

```powershell
rg -n '#include "(AudioCursorTimeline|AudioSnapshot|MiniaudioMixer|DirectSoundFacade|ExclusiveAudioEngine|ExclusiveAudioEngineInternal|OutputPacingTracker|WasapiAudioPatch|WasapiAudioPatchInternal|WasapiAudioTypes|WasapiEndpoint)\.h"' src tests dllmain.cpp
```

Expected: no matches.

- [ ] **Step 4: Create the reusable audio target**

Create `src/Audio/CMakeLists.txt` with exactly:

```cmake
add_library(gc_audio STATIC
        DirectSound/DirectSoundFacade.cpp
        Mixer/AudioCursorTimeline.cpp
        Mixer/AudioSnapshot.cpp
        Mixer/MiniaudioMixer.cpp
        Wasapi/ExclusiveAudioEngine.cpp
        Wasapi/OutputPacingTracker.cpp
        Wasapi/WasapiAudioPatch.cpp
        Wasapi/WasapiAudioTypes.cpp
        Wasapi/WasapiEndpoint.cpp
)
target_include_directories(gc_audio PUBLIC
        ${PROJECT_SOURCE_DIR}/src
        ${SDL3_SOURCE_DIR}/include
        ${minhook_SOURCE_DIR}/include
        ${plog_SOURCE_DIR}/include
)
target_link_libraries(gc_audio PUBLIC
        gc_config
        SDL3-static
        minhook
        miniaudio
        dsound
        dxguid
        ole32
        uuid
        avrt
        propsys
)
```

Append this line to `src/CMakeLists.txt`, after `add_subdirectory(Config)`:

```cmake
add_subdirectory(Audio)
```

- [ ] **Step 5: Define all audio tests as consumers of `gc_audio`**

Create `tests/Audio/CMakeLists.txt` with exactly:

```cmake
set(GC_AUDIO_TESTS
        AudioCursorTimelineTests
        AudioFormatTests
        AudioSnapshotTests
        DirectSoundDeviceTests
        ExclusiveAudioEngineTests
        MiniaudioMixerTests
        OutputPacingTrackerTests
        SecondarySoundBufferTests
        WasapiAudioPatchTests
        WasapiEndpointTests
)

foreach(test_target IN LISTS GC_AUDIO_TESTS)
    add_executable(${test_target} ${test_target}.cpp)
    target_link_libraries(${test_target} PRIVATE gc_audio)
    add_test(NAME ${test_target} COMMAND ${test_target})
endforeach()
```

Insert this line at the start of `tests/CMakeLists.txt`, before `add_subdirectory(Config)`:

```cmake
add_subdirectory(Audio)
```

- [ ] **Step 6: Remove duplicated audio ownership from the root build**

In root `CMakeLists.txt`:

1. Remove these nine entries from `SOURCES`: `AudioCursorTimeline.cpp`, `AudioSnapshot.cpp`, `DirectSoundFacade.cpp`, `ExclusiveAudioEngine.cpp`, `MiniaudioMixer.cpp`, `OutputPacingTracker.cpp`, `WasapiAudioPatch.cpp`, `WasapiAudioTypes.cpp`, and `WasapiEndpoint.cpp`.
2. Add `gc_audio` to `target_link_libraries(iDmacDrv32 PRIVATE ...)`.
3. Delete the complete root target blocks beginning with `add_executable(AudioFormatTests`, `AudioSnapshotTests`, `AudioCursorTimelineTests`, `OutputPacingTrackerTests`, `MiniaudioMixerTests`, `SecondarySoundBufferTests`, `DirectSoundDeviceTests`, `WasapiEndpointTests`, `ExclusiveAudioEngineTests`, and `WasapiAudioPatchTests`, through each target's corresponding `add_test(...)`.

Run:

```powershell
rg -n '^(add_executable\((Audio|DirectSound|Exclusive|Miniaudio|OutputPacing|Secondary|Wasapi)|\s+(AudioCursorTimeline|AudioSnapshot|DirectSoundFacade|ExclusiveAudioEngine|MiniaudioMixer|OutputPacingTracker|WasapiAudioPatch|WasapiAudioTypes|WasapiEndpoint)\.cpp)' CMakeLists.txt
```

Expected: no matches.

- [ ] **Step 7: Reconfigure, verify audio, run all tests, and commit**

Run:

```powershell
$vcvars = 'C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat'
$targets = 'iDmacDrv32 AudioCursorTimelineTests AudioFormatTests AudioSnapshotTests DirectSoundDeviceTests ExclusiveAudioEngineTests MiniaudioMixerTests OutputPacingTrackerTests SecondarySoundBufferTests WasapiAudioPatchTests WasapiEndpointTests'
$commandLine = 'call "' + $vcvars + '" >nul && cmake -S . -B build-msvc32-latest -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo && cmake --build build-msvc32-latest --target ' + $targets + ' --config RelWithDebInfo'
cmd.exe /d /s /c $commandLine
if ($LASTEXITCODE -ne 0) { throw "configure/build failed: $LASTEXITCODE" }

ctest --test-dir build-msvc32-latest --output-on-failure -R '^(AudioCursorTimelineTests|AudioFormatTests|AudioSnapshotTests|DirectSoundDeviceTests|ExclusiveAudioEngineTests|MiniaudioMixerTests|OutputPacingTrackerTests|SecondarySoundBufferTests|WasapiAudioPatchTests|WasapiEndpointTests)$'
ctest --test-dir build-msvc32-latest --output-on-failure
git diff --check
```

Expected: ten focused tests pass; then `100% tests passed, 0 tests failed out of 28`.

Commit:

```powershell
git add -- CMakeLists.txt src/Audio src/CMakeLists.txt tests/Audio tests/CMakeLists.txt dllmain.cpp
git commit -m "refactor: organize audio sources and tests"
```

---

### Task 4: Organize Polling and Switch Input Behind `gc_input`

**Files:**
- Move: `InputManager.*`, `InputPollingRuntime.*`, `InputSnapshotState.*` -> `src/Input/Polling/`
- Move: `SwitchInputPatch.*`, `SwitchInputPolicy.*` -> `src/Input/Switch/`
- Move: input tests -> mirrored `tests/Input/Polling/` and `tests/Input/Switch/`
- Create: `src/Input/CMakeLists.txt`, `tests/Input/CMakeLists.txt`
- Modify: `src/CMakeLists.txt`, `tests/CMakeLists.txt`, root `CMakeLists.txt`, `dllmain.cpp`, `iDmacDrv32.cpp`, moved sources, and moved tests

**Interfaces:**
- Consumes: `gc_config`, SDL3, `ntdll`, and SafetyHook.
- Produces: `gc_input`; preserves the five input functions/types and all four input test names and properties.

- [ ] **Step 1: Move input production and test files**

Run:

```powershell
New-Item -ItemType Directory -Force -Path `
    src/Input/Polling, `
    src/Input/Switch, `
    tests/Input/Polling, `
    tests/Input/Switch | Out-Null

git mv InputManager.cpp src/Input/Polling/InputManager.cpp
git mv InputManager.h src/Input/Polling/InputManager.h
git mv InputPollingRuntime.cpp src/Input/Polling/InputPollingRuntime.cpp
git mv InputPollingRuntime.h src/Input/Polling/InputPollingRuntime.h
git mv InputSnapshotState.cpp src/Input/Polling/InputSnapshotState.cpp
git mv InputSnapshotState.h src/Input/Polling/InputSnapshotState.h
git mv SwitchInputPatch.cpp src/Input/Switch/SwitchInputPatch.cpp
git mv SwitchInputPatch.h src/Input/Switch/SwitchInputPatch.h
git mv SwitchInputPolicy.cpp src/Input/Switch/SwitchInputPolicy.cpp
git mv SwitchInputPolicy.h src/Input/Switch/SwitchInputPolicy.h

git mv tests/InputPollingRuntimeStartupTests.cpp tests/Input/Polling/InputPollingRuntimeStartupTests.cpp
git mv tests/InputSnapshotStateTests.cpp tests/Input/Polling/InputSnapshotStateTests.cpp
git mv tests/SwitchInputPatchTests.cpp tests/Input/Switch/SwitchInputPatchTests.cpp
git mv tests/SwitchInputPolicyTests.cpp tests/Input/Switch/SwitchInputPolicyTests.cpp
```

- [ ] **Step 2: Rewrite all input includes to canonical paths**

Apply these literal replacements across `src`, `tests`, root `dllmain.cpp`, and root `iDmacDrv32.cpp`:

| Old include | New include |
|---|---|
| `"InputManager.h"` | `"Input/Polling/InputManager.h"` |
| `"InputPollingRuntime.h"` | `"Input/Polling/InputPollingRuntime.h"` |
| `"InputSnapshotState.h"` | `"Input/Polling/InputSnapshotState.h"` |
| `"SwitchInputPatch.h"` | `"Input/Switch/SwitchInputPatch.h"` |
| `"SwitchInputPolicy.h"` | `"Input/Switch/SwitchInputPolicy.h"` |

Run:

```powershell
rg -n '#include "(InputManager|InputPollingRuntime|InputSnapshotState|SwitchInputPatch|SwitchInputPolicy)\.h"' src tests dllmain.cpp iDmacDrv32.cpp
```

Expected: no matches.

- [ ] **Step 3: Create `gc_input` and its four tests**

Create `src/Input/CMakeLists.txt` with exactly:

```cmake
add_library(gc_input STATIC
        Polling/InputManager.cpp
        Polling/InputPollingRuntime.cpp
        Polling/InputSnapshotState.cpp
        Switch/SwitchInputPatch.cpp
        Switch/SwitchInputPolicy.cpp
)
target_include_directories(gc_input PUBLIC
        ${PROJECT_SOURCE_DIR}/src
        ${SDL3_SOURCE_DIR}/include
        ${plog_SOURCE_DIR}/include
)
target_link_libraries(gc_input PUBLIC
        gc_config
        SDL3-static
        ntdll
        safetyhook::safetyhook
)
```

Append this line to `src/CMakeLists.txt`, after the audio subdirectory:

```cmake
add_subdirectory(Input)
```

Create `tests/Input/CMakeLists.txt` with exactly:

```cmake
add_executable(InputSnapshotStateTests Polling/InputSnapshotStateTests.cpp)
target_link_libraries(InputSnapshotStateTests PRIVATE gc_input)
add_test(NAME InputSnapshotStateTests COMMAND InputSnapshotStateTests)

add_executable(InputPollingRuntimeStartupTests
        Polling/InputPollingRuntimeStartupTests.cpp)
target_link_libraries(InputPollingRuntimeStartupTests PRIVATE gc_input)
add_test(NAME InputPollingRuntimeStartupTests
        COMMAND InputPollingRuntimeStartupTests)
set_tests_properties(InputPollingRuntimeStartupTests PROPERTIES
        TIMEOUT 5
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
)

add_executable(SwitchInputPolicyTests Switch/SwitchInputPolicyTests.cpp)
target_link_libraries(SwitchInputPolicyTests PRIVATE gc_input)
add_test(NAME SwitchInputPolicyTests COMMAND SwitchInputPolicyTests)

add_executable(SwitchInputPatchTests Switch/SwitchInputPatchTests.cpp)
target_link_libraries(SwitchInputPatchTests PRIVATE gc_input)
add_test(NAME SwitchInputPatchTests COMMAND SwitchInputPatchTests)
```

Append to `tests/CMakeLists.txt`, after `add_subdirectory(Config)`:

```cmake
add_subdirectory(Input)
```

- [ ] **Step 4: Remove old input ownership from root CMake**

In root `CMakeLists.txt`:

1. Remove `InputManager.cpp`, `InputPollingRuntime.cpp`, `InputSnapshotState.cpp`, `SwitchInputPolicy.cpp`, and `SwitchInputPatch.cpp` from `SOURCES`.
2. Add `gc_input` to `iDmacDrv32`'s private links.
3. Delete the complete root blocks for `InputSnapshotStateTests`, `InputPollingRuntimeStartupTests`, `SwitchInputPolicyTests`, and `SwitchInputPatchTests`.

Run:

```powershell
rg -n '^(add_executable\((Input|Switch)|\s+(InputManager|InputPollingRuntime|InputSnapshotState|SwitchInputPolicy|SwitchInputPatch)\.cpp)' CMakeLists.txt
```

Expected: no matches.

- [ ] **Step 5: Reconfigure, verify input, run all tests, and commit**

Run:

```powershell
$vcvars = 'C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat'
$targets = 'iDmacDrv32 InputSnapshotStateTests InputPollingRuntimeStartupTests SwitchInputPolicyTests SwitchInputPatchTests'
$commandLine = 'call "' + $vcvars + '" >nul && cmake -S . -B build-msvc32-latest -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo && cmake --build build-msvc32-latest --target ' + $targets + ' --config RelWithDebInfo'
cmd.exe /d /s /c $commandLine
if ($LASTEXITCODE -ne 0) { throw "configure/build failed: $LASTEXITCODE" }

ctest --test-dir build-msvc32-latest --output-on-failure -R '^(InputSnapshotStateTests|InputPollingRuntimeStartupTests|SwitchInputPolicyTests|SwitchInputPatchTests)$'
ctest --test-dir build-msvc32-latest --output-on-failure
git diff --check
```

Expected: four focused tests pass; then all 28 tests pass.

Commit:

```powershell
git add -- CMakeLists.txt src/Input src/CMakeLists.txt tests/Input tests/CMakeLists.txt dllmain.cpp iDmacDrv32.cpp
git commit -m "refactor: organize input sources and tests"
```

---

### Task 5: Organize NESYS and Logging Behind Owned Targets

**Files:**
- Move: `NesysServiceProcess.*`, `NesysServicePatch.*`, `NesysHookTransaction.*` -> `src/Nesys/`
- Move: `NesysServiceLauncher.*` -> `src/Nesys/Launcher/`
- Move: `ServerAddressOverride.*`, `SyntheticNetworkAdapter.*` -> `src/Nesys/Network/`
- Move: `RegistryConfigOverride.*` -> `src/Nesys/Registry/`
- Move: `SessionLog.*` -> `src/Logging/`
- Move: five NESYS tests -> `tests/Nesys/` with mirrored `Network/` and `Registry/`; `SessionLogTests.cpp` -> `tests/Logging/`
- Create: `src/Nesys/CMakeLists.txt`, `src/Logging/CMakeLists.txt`, `tests/Nesys/CMakeLists.txt`, `tests/Logging/CMakeLists.txt`
- Modify: `src/CMakeLists.txt`, `tests/CMakeLists.txt`, root `CMakeLists.txt`, `dllmain.cpp`, moved sources, and moved tests

**Interfaces:**
- Consumes: `gc_nesys_network_config`, `gc_config`, MinHook, SafetyHook, and plog.
- Produces: `gc_nesys_process`, `gc_nesys`, and `gc_logging`, separating process-role primitives from hooks and log appenders without changing behavior.

- [ ] **Step 1: Move NESYS, logging, and mirrored tests**

Run:

```powershell
New-Item -ItemType Directory -Force -Path `
    src/Nesys/Launcher, `
    src/Nesys/Registry, `
    src/Logging, `
    tests/Nesys/Network, `
    tests/Nesys/Registry, `
    tests/Logging | Out-Null

git mv NesysHookTransaction.cpp src/Nesys/NesysHookTransaction.cpp
git mv NesysHookTransaction.h src/Nesys/NesysHookTransaction.h
git mv NesysServicePatch.cpp src/Nesys/NesysServicePatch.cpp
git mv NesysServicePatch.h src/Nesys/NesysServicePatch.h
git mv NesysServiceProcess.cpp src/Nesys/NesysServiceProcess.cpp
git mv NesysServiceProcess.h src/Nesys/NesysServiceProcess.h
git mv NesysServiceLauncher.cpp src/Nesys/Launcher/NesysServiceLauncher.cpp
git mv NesysServiceLauncher.h src/Nesys/Launcher/NesysServiceLauncher.h
git mv ServerAddressOverride.cpp src/Nesys/Network/ServerAddressOverride.cpp
git mv ServerAddressOverride.h src/Nesys/Network/ServerAddressOverride.h
git mv SyntheticNetworkAdapter.cpp src/Nesys/Network/SyntheticNetworkAdapter.cpp
git mv SyntheticNetworkAdapter.h src/Nesys/Network/SyntheticNetworkAdapter.h
git mv RegistryConfigOverride.cpp src/Nesys/Registry/RegistryConfigOverride.cpp
git mv RegistryConfigOverride.h src/Nesys/Registry/RegistryConfigOverride.h
git mv SessionLog.cpp src/Logging/SessionLog.cpp
git mv SessionLog.h src/Logging/SessionLog.h

git mv tests/NesysHookTransactionTests.cpp tests/Nesys/NesysHookTransactionTests.cpp
git mv tests/NesysServicePatchTests.cpp tests/Nesys/NesysServicePatchTests.cpp
git mv tests/ServerAddressOverrideTests.cpp tests/Nesys/Network/ServerAddressOverrideTests.cpp
git mv tests/SyntheticNetworkAdapterTests.cpp tests/Nesys/Network/SyntheticNetworkAdapterTests.cpp
git mv tests/RegistryConfigOverrideTests.cpp tests/Nesys/Registry/RegistryConfigOverrideTests.cpp
git mv tests/SessionLogTests.cpp tests/Logging/SessionLogTests.cpp
```

- [ ] **Step 2: Rewrite NESYS and logging includes**

Apply these literal replacements across `src`, `tests`, and root `dllmain.cpp`:

| Old include | New include |
|---|---|
| `"NesysHookTransaction.h"` | `"Nesys/NesysHookTransaction.h"` |
| `"NesysServicePatch.h"` | `"Nesys/NesysServicePatch.h"` |
| `"NesysServiceProcess.h"` | `"Nesys/NesysServiceProcess.h"` |
| `"NesysServiceLauncher.h"` | `"Nesys/Launcher/NesysServiceLauncher.h"` |
| `"NesysNetworkConfig.h"` | `"Nesys/Network/NesysNetworkConfig.h"` |
| `"ServerAddressOverride.h"` | `"Nesys/Network/ServerAddressOverride.h"` |
| `"SyntheticNetworkAdapter.h"` | `"Nesys/Network/SyntheticNetworkAdapter.h"` |
| `"RegistryConfigOverride.h"` | `"Nesys/Registry/RegistryConfigOverride.h"` |
| `"SessionLog.h"` | `"Logging/SessionLog.h"` |

The `NesysNetworkConfig.h` replacement also catches any bare include left over from Task 2. Run:

```powershell
rg -n '#include "(NesysHookTransaction|NesysServicePatch|NesysServiceProcess|NesysServiceLauncher|NesysNetworkConfig|ServerAddressOverride|SyntheticNetworkAdapter|RegistryConfigOverride|SessionLog)\.h"' src tests dllmain.cpp
```

Expected: no matches.

- [ ] **Step 3: Create the NESYS and logging library targets**

Create `src/Nesys/CMakeLists.txt` with exactly:

```cmake
add_library(gc_nesys_process STATIC NesysServiceProcess.cpp)
target_include_directories(gc_nesys_process PUBLIC
        ${PROJECT_SOURCE_DIR}/src)

add_library(gc_nesys STATIC
        NesysHookTransaction.cpp
        NesysServicePatch.cpp
        Launcher/NesysServiceLauncher.cpp
        Network/ServerAddressOverride.cpp
        Network/SyntheticNetworkAdapter.cpp
        Registry/RegistryConfigOverride.cpp
)
target_include_directories(gc_nesys PUBLIC
        ${PROJECT_SOURCE_DIR}/src
        ${minhook_SOURCE_DIR}/include
        ${reflectcpp_SOURCE_DIR}/include
        ${plog_SOURCE_DIR}/include
)
target_link_libraries(gc_nesys PUBLIC
        gc_nesys_process
        gc_nesys_network_config
        gc_config
        minhook
        safetyhook::safetyhook
)
```

Create `src/Logging/CMakeLists.txt` with exactly:

```cmake
add_library(gc_logging STATIC SessionLog.cpp)
target_include_directories(gc_logging PUBLIC
        ${PROJECT_SOURCE_DIR}/src
        ${plog_SOURCE_DIR}/include
)
target_link_libraries(gc_logging PUBLIC gc_nesys_process)
```

Append to `src/CMakeLists.txt`, in this order after `add_subdirectory(Input)`:

```cmake
add_subdirectory(Nesys)
add_subdirectory(Logging)
```

Do not add `add_subdirectory(Network)` inside `src/Nesys/CMakeLists.txt`; `src/CMakeLists.txt` already enters `Nesys/Network` first to create `gc_nesys_network_config`.

- [ ] **Step 4: Create the NESYS and logging test targets**

Create `tests/Nesys/CMakeLists.txt` with exactly:

```cmake
add_executable(NesysHookTransactionTests NesysHookTransactionTests.cpp)
target_link_libraries(NesysHookTransactionTests PRIVATE gc_nesys)
add_test(NAME NesysHookTransactionTests COMMAND NesysHookTransactionTests)

add_executable(NesysServicePatchTests NesysServicePatchTests.cpp)
target_link_libraries(NesysServicePatchTests PRIVATE gc_nesys)
add_test(NAME NesysServicePatchTests COMMAND NesysServicePatchTests)

add_executable(ServerAddressOverrideTests
        Network/ServerAddressOverrideTests.cpp)
target_link_libraries(ServerAddressOverrideTests PRIVATE gc_nesys)
add_test(NAME ServerAddressOverrideTests COMMAND ServerAddressOverrideTests)

add_executable(SyntheticNetworkAdapterTests
        Network/SyntheticNetworkAdapterTests.cpp)
target_link_libraries(SyntheticNetworkAdapterTests PRIVATE gc_nesys)
add_test(NAME SyntheticNetworkAdapterTests COMMAND SyntheticNetworkAdapterTests)

add_executable(RegistryConfigOverrideTests
        Registry/RegistryConfigOverrideTests.cpp)
target_link_libraries(RegistryConfigOverrideTests PRIVATE gc_nesys)
add_test(NAME RegistryConfigOverrideTests COMMAND RegistryConfigOverrideTests)
```

Create `tests/Logging/CMakeLists.txt` with exactly:

```cmake
add_executable(SessionLogTests SessionLogTests.cpp)
target_link_libraries(SessionLogTests PRIVATE gc_logging)
add_test(NAME SessionLogTests COMMAND SessionLogTests)
```

Append to `tests/CMakeLists.txt`, in this order after `add_subdirectory(Input)`:

```cmake
add_subdirectory(Logging)
add_subdirectory(Nesys)
```

- [ ] **Step 5: Remove root ownership and link the new modules**

In root `CMakeLists.txt`:

1. Remove `NesysHookTransaction.cpp`, `NesysServiceLauncher.cpp`, `NesysServicePatch.cpp`, `NesysServiceProcess.cpp`, `RegistryConfigOverride.cpp`, `ServerAddressOverride.cpp`, `SyntheticNetworkAdapter.cpp`, and `SessionLog.cpp` from `SOURCES`.
2. Add `gc_nesys` and `gc_logging` to `iDmacDrv32`'s private links.
3. Delete the complete root target blocks for `RegistryConfigOverrideTests`, `SessionLogTests`, `NesysHookTransactionTests`, `SyntheticNetworkAdapterTests`, `ServerAddressOverrideTests`, and `NesysServicePatchTests`.

Run:

```powershell
rg -n '^(add_executable\((RegistryConfigOverride|SessionLog|Nesys|SyntheticNetwork|ServerAddress)|\s+(NesysHookTransaction|NesysServiceLauncher|NesysServicePatch|NesysServiceProcess|RegistryConfigOverride|ServerAddressOverride|SessionLog|SyntheticNetworkAdapter)\.cpp)' CMakeLists.txt
```

Expected: no matches.

- [ ] **Step 6: Reconfigure, verify NESYS/logging, run all tests, and commit**

Run:

```powershell
$vcvars = 'C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat'
$targets = 'iDmacDrv32 NesysHookTransactionTests NesysServicePatchTests RegistryConfigOverrideTests ServerAddressOverrideTests SessionLogTests SyntheticNetworkAdapterTests'
$commandLine = 'call "' + $vcvars + '" >nul && cmake -S . -B build-msvc32-latest -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo && cmake --build build-msvc32-latest --target ' + $targets + ' --config RelWithDebInfo'
cmd.exe /d /s /c $commandLine
if ($LASTEXITCODE -ne 0) { throw "configure/build failed: $LASTEXITCODE" }

ctest --test-dir build-msvc32-latest --output-on-failure -R '^(NesysHookTransactionTests|NesysServicePatchTests|RegistryConfigOverrideTests|ServerAddressOverrideTests|SessionLogTests|SyntheticNetworkAdapterTests)$'
ctest --test-dir build-msvc32-latest --output-on-failure
git diff --check
```

Expected: six focused tests pass; then all 28 tests pass.

Commit:

```powershell
git add -- CMakeLists.txt src/Nesys src/Logging src/CMakeLists.txt tests/Nesys tests/Logging tests/CMakeLists.txt dllmain.cpp
git commit -m "refactor: organize nesys and logging sources"
```

---

### Task 6: Move RFID, Test-Mode Storage, and Win32 Hooking Under `src`

**Files:**
- Move: `Rfid/**` -> `src/Rfid/**`
- Move: `TestModeStorage/**` -> `src/TestModeStorage/**`
- Move: `Win32Hooks/Kernel32Hooks.*` -> `src/Win32Hooks/`
- Move: `Win32Hooks/MinHookTransaction.*` -> `src/Platform/Win32/Hooking/`
- Create: `src/Platform/CMakeLists.txt`, `src/Platform/Win32/Hooking/CMakeLists.txt`, `src/Rfid/CMakeLists.txt`, `src/TestModeStorage/CMakeLists.txt`, `src/Win32Hooks/CMakeLists.txt`
- Create: `tests/Rfid/CMakeLists.txt`, `tests/TestModeStorage/CMakeLists.txt`, `tests/Win32Hooks/CMakeLists.txt`
- Modify: `src/CMakeLists.txt`, `tests/CMakeLists.txt`, root `CMakeLists.txt`, moved hook callers, and `tests/Win32Hooks/Kernel32HookTests.cpp`

**Interfaces:**
- Consumes: the already-refactored RFID implementation, test-mode redirector, MinHook, plog, SDL key mapping, and `gc_config`.
- Produces: `gc_hooking`, `gc_rfid_core`, `gc_test_mode_storage`, `gc_win32_hooks`, and `gc_rfid_feature`; existing RFID namespaces, Windows hook signatures, and six test names remain unchanged.

- [ ] **Step 1: Move the three source families and the general transaction**

Run:

```powershell
New-Item -ItemType Directory -Force -Path `
    src/Platform/Win32/Hooking, `
    src/Win32Hooks | Out-Null

git mv Rfid src/Rfid
git mv TestModeStorage src/TestModeStorage
git mv Win32Hooks/Kernel32Hooks.cpp src/Win32Hooks/Kernel32Hooks.cpp
git mv Win32Hooks/Kernel32Hooks.h src/Win32Hooks/Kernel32Hooks.h
git mv Win32Hooks/MinHookTransaction.cpp src/Platform/Win32/Hooking/MinHookTransaction.cpp
git mv Win32Hooks/MinHookTransaction.h src/Platform/Win32/Hooking/MinHookTransaction.h
```

Expected: the old root `Rfid`, `TestModeStorage`, and `Win32Hooks` directories contain no tracked files and disappear from the worktree.

- [ ] **Step 2: Canonicalize the general transaction include**

Replace every occurrence of:

```cpp
#include "Win32Hooks/MinHookTransaction.h"
```

with:

```cpp
#include "Platform/Win32/Hooking/MinHookTransaction.h"
```

This must update `src/Platform/Win32/Hooking/MinHookTransaction.cpp`, `src/Win32Hooks/Kernel32Hooks.h`, `src/Rfid/Feature.h`, and `tests/Win32Hooks/Kernel32HookTests.cpp`.

Run:

```powershell
rg -n '#include "Win32Hooks/MinHookTransaction\.h"' src tests
```

Expected: no matches. Keep existing `Rfid/...`, `TestModeStorage/...`, and `Win32Hooks/Kernel32Hooks.h` includes as-is because `${PROJECT_SOURCE_DIR}/src` is their canonical include root.

- [ ] **Step 3: Create the five production library targets**

Create `src/Platform/CMakeLists.txt` with exactly:

```cmake
add_subdirectory(Win32/Hooking)
```

Create `src/Platform/Win32/Hooking/CMakeLists.txt` with exactly:

```cmake
add_library(gc_hooking STATIC MinHookTransaction.cpp)
target_include_directories(gc_hooking PUBLIC
        ${PROJECT_SOURCE_DIR}/src
        ${minhook_SOURCE_DIR}/include
        ${plog_SOURCE_DIR}/include
)
target_link_libraries(gc_hooking PUBLIC minhook)
```

Create `src/Rfid/CMakeLists.txt` with exactly:

```cmake
add_library(gc_rfid_core STATIC
        ComPortState.cpp
        Runtime.cpp
        State.cpp
        TaitoCommands.cpp
        Jvs/Decoder.cpp
        Jvs/Device.cpp
        Jvs/Encoder.cpp
)
target_include_directories(gc_rfid_core PUBLIC
        ${PROJECT_SOURCE_DIR}/src
        ${plog_SOURCE_DIR}/include
)
```

Create `src/TestModeStorage/CMakeLists.txt` with exactly:

```cmake
add_library(gc_test_mode_storage STATIC
        Redirector.cpp
        Hooks.cpp
)
target_include_directories(gc_test_mode_storage PUBLIC
        ${PROJECT_SOURCE_DIR}/src
        ${plog_SOURCE_DIR}/include
)
```

Create `src/Win32Hooks/CMakeLists.txt` with exactly:

```cmake
add_library(gc_win32_hooks STATIC Kernel32Hooks.cpp)
target_include_directories(gc_win32_hooks PUBLIC
        ${PROJECT_SOURCE_DIR}/src
        ${minhook_SOURCE_DIR}/include
        ${plog_SOURCE_DIR}/include
)
target_link_libraries(gc_win32_hooks PUBLIC
        gc_hooking
        gc_rfid_core
        gc_test_mode_storage
)
```

Append the following to `src/CMakeLists.txt`, after `add_subdirectory(Logging)`:

```cmake
add_subdirectory(Platform)
add_subdirectory(Rfid)
add_subdirectory(TestModeStorage)
add_subdirectory(Win32Hooks)

add_library(gc_rfid_feature STATIC Rfid/Feature.cpp)
target_include_directories(gc_rfid_feature PUBLIC
        ${PROJECT_SOURCE_DIR}/src
        ${minhook_SOURCE_DIR}/include
        ${plog_SOURCE_DIR}/include
)
target_link_libraries(gc_rfid_feature PUBLIC
        gc_config
        gc_hooking
        gc_rfid_core
        gc_test_mode_storage
        gc_win32_hooks
)
```

- [ ] **Step 4: Make the existing six tests consume the production targets**

Create `tests/Rfid/CMakeLists.txt` with exactly:

```cmake
add_executable(RfidRuntimeTests RfidRuntimeTests.cpp)
target_link_libraries(RfidRuntimeTests PRIVATE gc_rfid_core)
add_test(NAME RfidRuntimeTests COMMAND RfidRuntimeTests)

add_executable(JvsCodecTests JvsCodecTests.cpp)
target_link_libraries(JvsCodecTests PRIVATE gc_rfid_core)
add_test(NAME JvsCodecTests COMMAND JvsCodecTests)

add_executable(JvsDeviceTests JvsDeviceTests.cpp)
target_link_libraries(JvsDeviceTests PRIVATE gc_rfid_core)
add_test(NAME JvsDeviceTests COMMAND JvsDeviceTests)

add_executable(ComPortStateTests ComPortStateTests.cpp)
target_link_libraries(ComPortStateTests PRIVATE gc_rfid_core)
add_test(NAME ComPortStateTests COMMAND ComPortStateTests)
```

Create `tests/TestModeStorage/CMakeLists.txt` with exactly:

```cmake
add_executable(TestModeStorageRedirectTests
        TestModeStorageRedirectTests.cpp)
target_link_libraries(TestModeStorageRedirectTests PRIVATE
        gc_test_mode_storage)
add_test(NAME TestModeStorageRedirectTests
        COMMAND TestModeStorageRedirectTests)
```

Create `tests/Win32Hooks/CMakeLists.txt` with exactly:

```cmake
add_executable(Kernel32HookTests Kernel32HookTests.cpp)
target_link_libraries(Kernel32HookTests PRIVATE gc_win32_hooks)
add_test(NAME Kernel32HookTests COMMAND Kernel32HookTests)
```

Append to `tests/CMakeLists.txt`, after the NESYS subdirectory:

```cmake
add_subdirectory(Rfid)
add_subdirectory(TestModeStorage)
add_subdirectory(Win32Hooks)
```

- [ ] **Step 5: Remove duplicated RFID/storage/hook compilation from root CMake**

In root `CMakeLists.txt`:

1. Remove `Rfid/Feature.cpp`, `Rfid/ComPortState.cpp`, `Rfid/Runtime.cpp`, `Rfid/State.cpp`, `Rfid/TaitoCommands.cpp`, all three `Rfid/Jvs/*.cpp` entries, both `TestModeStorage/*.cpp` entries, and both `Win32Hooks/*.cpp` entries from `SOURCES`.
2. Add `gc_rfid_feature` to `iDmacDrv32`'s private links.
3. Delete the complete root target blocks for `RfidRuntimeTests`, `JvsCodecTests`, `JvsDeviceTests`, `ComPortStateTests`, `Kernel32HookTests`, and `TestModeStorageRedirectTests`.

Run:

```powershell
rg -n '(^|\s)(Rfid|TestModeStorage|Win32Hooks)/.*\.cpp|^add_executable\((RfidRuntime|Jvs|ComPortState|Kernel32Hook|TestModeStorage)' CMakeLists.txt
```

Expected: no matches.

- [ ] **Step 6: Reconfigure, verify the six focused tests, run all tests, and commit**

Run:

```powershell
$vcvars = 'C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat'
$targets = 'iDmacDrv32 RfidRuntimeTests JvsCodecTests JvsDeviceTests ComPortStateTests Kernel32HookTests TestModeStorageRedirectTests'
$commandLine = 'call "' + $vcvars + '" >nul && cmake -S . -B build-msvc32-latest -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo && cmake --build build-msvc32-latest --target ' + $targets + ' --config RelWithDebInfo'
cmd.exe /d /s /c $commandLine
if ($LASTEXITCODE -ne 0) { throw "configure/build failed: $LASTEXITCODE" }

ctest --test-dir build-msvc32-latest --output-on-failure -R '^(RfidRuntimeTests|JvsCodecTests|JvsDeviceTests|ComPortStateTests|Kernel32HookTests|TestModeStorageRedirectTests)$'
ctest --test-dir build-msvc32-latest --output-on-failure
git diff --check
```

Expected: six focused tests pass; then all 28 tests pass.

Commit:

```powershell
git add -- CMakeLists.txt src/Platform src/Rfid src/TestModeStorage src/Win32Hooks src/CMakeLists.txt tests/Rfid tests/TestModeStorage tests/Win32Hooks tests/CMakeLists.txt
git commit -m "refactor: organize rfid storage and win32 hook sources"
```

---

### Task 7: Organize Runtime Patches Behind `gc_runtime_patches`

**Files:**
- Move: `FrameratePatch.*` -> `src/Patches/Framerate/`
- Move: `CountdownTimerFreeze.*` -> `src/Patches/Countdown/`
- Move: `tests/CountdownTimerFreezeTests.cpp` -> `tests/Patches/Countdown/CountdownTimerFreezeTests.cpp`
- Create: `src/Patches/CMakeLists.txt`, `tests/Patches/CMakeLists.txt`
- Modify: `src/CMakeLists.txt`, `tests/CMakeLists.txt`, root `CMakeLists.txt`, `dllmain.cpp`, moved sources, and moved test

**Interfaces:**
- Consumes: `gc_config`, SafetyHook, and plog.
- Produces: `gc_runtime_patches`; countdown patch bytes, RVAs, signatures, and feature behavior remain identical.

- [ ] **Step 1: Move patch sources and their test**

Run:

```powershell
New-Item -ItemType Directory -Force -Path `
    src/Patches/Framerate, `
    src/Patches/Countdown, `
    tests/Patches/Countdown | Out-Null

git mv FrameratePatch.cpp src/Patches/Framerate/FrameratePatch.cpp
git mv FrameratePatch.h src/Patches/Framerate/FrameratePatch.h
git mv CountdownTimerFreeze.cpp src/Patches/Countdown/CountdownTimerFreeze.cpp
git mv CountdownTimerFreeze.h src/Patches/Countdown/CountdownTimerFreeze.h
git mv tests/CountdownTimerFreezeTests.cpp tests/Patches/Countdown/CountdownTimerFreezeTests.cpp
```

- [ ] **Step 2: Rewrite patch includes to canonical paths**

Apply these literal replacements across `src`, `tests`, and root `dllmain.cpp`:

| Old include | New include |
|---|---|
| `"FrameratePatch.h"` | `"Patches/Framerate/FrameratePatch.h"` |
| `"CountdownTimerFreeze.h"` | `"Patches/Countdown/CountdownTimerFreeze.h"` |

Run:

```powershell
rg -n '#include "(FrameratePatch|CountdownTimerFreeze)\.h"' src tests dllmain.cpp
```

Expected: no matches.

- [ ] **Step 3: Create the patch library and test target**

Create `src/Patches/CMakeLists.txt` with exactly:

```cmake
add_library(gc_runtime_patches STATIC
        Framerate/FrameratePatch.cpp
        Countdown/CountdownTimerFreeze.cpp
)
target_include_directories(gc_runtime_patches PUBLIC
        ${PROJECT_SOURCE_DIR}/src
        ${plog_SOURCE_DIR}/include
)
target_link_libraries(gc_runtime_patches PUBLIC
        gc_config
        safetyhook::safetyhook
)
```

Append to `src/CMakeLists.txt`, after the `gc_rfid_feature` block:

```cmake
add_subdirectory(Patches)
```

Create `tests/Patches/CMakeLists.txt` with exactly:

```cmake
add_executable(CountdownTimerFreezeTests
        Countdown/CountdownTimerFreezeTests.cpp)
target_link_libraries(CountdownTimerFreezeTests PRIVATE gc_runtime_patches)
add_test(NAME CountdownTimerFreezeTests COMMAND CountdownTimerFreezeTests)
```

Insert this line into `tests/CMakeLists.txt`, before `add_subdirectory(Rfid)`:

```cmake
add_subdirectory(Patches)
```

- [ ] **Step 4: Remove root ownership, verify, and commit**

In root `CMakeLists.txt`, remove `CountdownTimerFreeze.cpp` and `FrameratePatch.cpp` from `SOURCES`, add `gc_runtime_patches` to the DLL links, and delete the complete `CountdownTimerFreezeTests` target block.

Run:

```powershell
$vcvars = 'C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat'
$targets = 'iDmacDrv32 CountdownTimerFreezeTests'
$commandLine = 'call "' + $vcvars + '" >nul && cmake -S . -B build-msvc32-latest -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo && cmake --build build-msvc32-latest --target ' + $targets + ' --config RelWithDebInfo'
cmd.exe /d /s /c $commandLine
if ($LASTEXITCODE -ne 0) { throw "configure/build failed: $LASTEXITCODE" }

ctest --test-dir build-msvc32-latest --output-on-failure -R '^CountdownTimerFreezeTests$'
ctest --test-dir build-msvc32-latest --output-on-failure
git diff --check
```

Expected: the focused test passes; then all 28 tests pass.

Commit:

```powershell
git add -- CMakeLists.txt src/Patches src/CMakeLists.txt tests/Patches tests/CMakeLists.txt dllmain.cpp
git commit -m "refactor: organize runtime patch sources"
```

---

### Task 8: Complete DLL Composition, Tool Relocation, and Parity Verification

**Files:**
- Move: `dllmain.cpp` -> `src/Loader/DllMain.cpp`
- Move: `iDmacDrv32.cpp`, `iDmacDrv32.def`, `RegisterOpTypes.h`, `keycodes.h` -> `src/Driver/iDmac/`
- Move: `zero_decrypt.zip` -> `tools/zero_decrypt.zip`
- Replace: root `CMakeLists.txt`, `src/CMakeLists.txt`, `tests/CMakeLists.txt`
- Modify: moved loader/driver includes

**Interfaces:**
- Consumes: every static-library target created in Tasks 2-7 and the baseline evidence captured in Task 1.
- Produces: the same `iDmacDrv32.dll` external contract from a thin root build; every DLL/runtime source is below `src`, every test is mirrored by feature, and the decrypt archive is byte-identical below `tools`.

- [ ] **Step 1: Move the loader, iDmac driver contract, and archive**

Run:

```powershell
New-Item -ItemType Directory -Force -Path `
    src/Loader, `
    src/Driver/iDmac | Out-Null

git mv dllmain.cpp src/Loader/DllMain.cpp
git mv iDmacDrv32.cpp src/Driver/iDmac/iDmacDrv32.cpp
git mv iDmacDrv32.def src/Driver/iDmac/iDmacDrv32.def
git mv RegisterOpTypes.h src/Driver/iDmac/RegisterOpTypes.h
git mv keycodes.h src/Driver/iDmac/keycodes.h
git mv zero_decrypt.zip tools/zero_decrypt.zip
```

The capitalization `iDmac` is intentional and matches the repository/source contract.

- [ ] **Step 2: Canonicalize the driver include and check the archive immediately**

In `src/Driver/iDmac/iDmacDrv32.cpp`, replace:

```cpp
#include "RegisterOpTypes.h"
```

with:

```cpp
#include "Driver/iDmac/RegisterOpTypes.h"
```

Run:

```powershell
$expectedHash = '725BB06CD4DB0EB4B4789DC76D3FF6CDDEF212D429E27224760DF1322217C175'
$actualHash = (Get-FileHash tools/zero_decrypt.zip -Algorithm SHA256).Hash
if ($actualHash -ne $expectedHash) {
    throw "zero_decrypt.zip changed: $actualHash"
}
```

Expected: no output and no exception.

- [ ] **Step 3: Replace `src/CMakeLists.txt` with the final composition**

The complete file must be:

```cmake
add_subdirectory(Nesys/Network)
add_subdirectory(Config)
add_subdirectory(Audio)
add_subdirectory(Input)
add_subdirectory(Nesys)
add_subdirectory(Logging)
add_subdirectory(Platform)
add_subdirectory(Rfid)
add_subdirectory(TestModeStorage)
add_subdirectory(Win32Hooks)

add_library(gc_rfid_feature STATIC Rfid/Feature.cpp)
target_include_directories(gc_rfid_feature PUBLIC
        ${PROJECT_SOURCE_DIR}/src
        ${minhook_SOURCE_DIR}/include
        ${plog_SOURCE_DIR}/include
)
target_link_libraries(gc_rfid_feature PUBLIC
        gc_config
        gc_hooking
        gc_rfid_core
        gc_test_mode_storage
        gc_win32_hooks
)

add_subdirectory(Patches)

add_library(iDmacDrv32 SHARED
        Loader/DllMain.cpp
        Driver/iDmac/iDmacDrv32.cpp
)
target_include_directories(iDmacDrv32 PRIVATE
        ${PROJECT_SOURCE_DIR}/src
        ${SDL3_SOURCE_DIR}/include
        ${minhook_SOURCE_DIR}/include
        ${reflectcpp_SOURCE_DIR}/include
        ${plog_SOURCE_DIR}/include
)
target_link_libraries(iDmacDrv32 PRIVATE
        gc_audio
        gc_config
        gc_input
        gc_logging
        gc_nesys
        gc_rfid_feature
        gc_runtime_patches
        SDL3-static
        dsound
        dxguid
        ntdll
        ole32
        uuid
        avrt
        propsys
        minhook
        miniaudio
        safetyhook::safetyhook
        tomlplusplus::tomlplusplus
        reflectcpp
)

set(DEF_FILE
        "${CMAKE_CURRENT_SOURCE_DIR}/Driver/iDmac/iDmacDrv32.def")
if(MSVC)
    set_target_properties(iDmacDrv32 PROPERTIES
            LINK_FLAGS "/DEF:${DEF_FILE}")
endif()
```

- [ ] **Step 4: Replace the root and test composition files**

Replace root `CMakeLists.txt` with exactly:

```cmake
cmake_minimum_required(VERSION 3.31)
project(GCLoader VERSION 1.0.0 LANGUAGES C CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(ProjectOptions)
find_package(Git REQUIRED)
include(Dependencies)

enable_testing()
add_subdirectory(src)
add_subdirectory(tools)
add_subdirectory(tests)
```

Replace `tests/CMakeLists.txt` with exactly:

```cmake
add_subdirectory(Audio)
add_subdirectory(Config)
add_subdirectory(Input)
add_subdirectory(Logging)
add_subdirectory(Nesys)
add_subdirectory(Patches)
add_subdirectory(Rfid)
add_subdirectory(TestModeStorage)
add_subdirectory(Win32Hooks)
```

- [ ] **Step 5: Reconfigure and build every first-party target**

Run:

```powershell
$vcvars = 'C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat'
$commandLine = 'call "' + $vcvars + '" >nul && cmake -S . -B build-msvc32-latest -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo && cmake --build build-msvc32-latest --config RelWithDebInfo'
cmd.exe /d /s /c $commandLine
if ($LASTEXITCODE -ne 0) { throw "full build failed: $LASTEXITCODE" }
```

Expected: CMake configures cleanly and the complete Ninja build exits `0`, including `iDmacDrv32`, `ConfigGUI`, and all 28 tests.

- [ ] **Step 6: Compare the test inventory and run the complete suite**

Run:

```powershell
$evidence = Join-Path $env:TEMP 'gcloader-source-build-foundation'
$beforeTests = Get-Content (Join-Path $evidence 'ctest-before.txt') |
    ForEach-Object {
        if ($_ -match 'Test\s+#\d+:\s+(.+)$') { $Matches[1] }
    } | Sort-Object -Unique
$afterListing = ctest --test-dir build-msvc32-latest -N
$afterTests = $afterListing | ForEach-Object {
    if ($_ -match 'Test\s+#\d+:\s+(.+)$') { $Matches[1] }
} | Sort-Object -Unique
$testDiff = Compare-Object $beforeTests $afterTests
if ($testDiff -or $afterTests.Count -ne 28) {
    $testDiff | Format-Table | Out-String | Write-Host
    throw "CTest inventory changed or does not contain 28 tests"
}

ctest --test-dir build-msvc32-latest --output-on-failure
if ($LASTEXITCODE -ne 0) { throw "CTest failed: $LASTEXITCODE" }
```

Expected: inventory comparison is silent; `100% tests passed, 0 tests failed out of 28`.

- [ ] **Step 7: Compare the DLL export contract by name and ordinal**

Run:

```powershell
$evidence = Join-Path $env:TEMP 'gcloader-source-build-foundation'
$vcvars = 'C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat'
$rawExports = Join-Path $evidence 'exports-after-raw.txt'
$commandLine = 'call "' + $vcvars + '" >nul && dumpbin /nologo /exports "build-msvc32-latest\iDmacDrv32.dll" > "' + $rawExports + '"'
cmd.exe /d /s /c $commandLine
if ($LASTEXITCODE -ne 0) { throw "dumpbin failed: $LASTEXITCODE" }

$afterExports = Get-Content $rawExports | ForEach-Object {
    if ($_ -match '^\s*(\d+)\s+\S+\s+\S+\s+(iDmacDrv[A-Za-z0-9]+)') {
        [pscustomobject]@{
            Ordinal = [int]$Matches[1]
            Name = $Matches[2]
        }
    }
} | Sort-Object Ordinal | ForEach-Object { "$($_.Ordinal) $($_.Name)" }
$beforeExports = Get-Content (Join-Path $evidence 'exports-before.txt')
$exportDiff = Compare-Object $beforeExports $afterExports
if ($exportDiff -or $afterExports.Count -ne 15) {
    $exportDiff | Format-Table | Out-String | Write-Host
    throw "iDmacDrv32 export contract changed"
}
```

Expected: comparison is silent and finds exactly 15 exports.

- [ ] **Step 8: Audit the final physical layout and CMake ownership**

Run:

```powershell
$rootSources = Get-ChildItem -File | Where-Object {
    $_.Extension -in '.c', '.cc', '.cpp', '.cxx', '.h', '.hpp', '.def'
}
if ($rootSources) {
    $rootSources.FullName | Write-Host
    throw "runtime source remains at repository root"
}

$badTestSources = foreach ($cmakeFile in
        Get-ChildItem tests -Recurse -Filter CMakeLists.txt) {
    $text = Get-Content $cmakeFile.FullName -Raw
    foreach ($match in [regex]::Matches(
            $text, '[A-Za-z0-9_/.-]+\.cpp')) {
        if ($match.Value -notmatch 'Tests\.cpp$') {
            "$($cmakeFile.FullName): $($match.Value)"
        }
    }
}
if ($badTestSources) {
    $badTestSources | Write-Host
    throw "a test target still compiles production source directly"
}

$oldBareIncludes = rg -n '#include "(config|RegistryConfig|SdlRflParsers|WinKeyMapping|AudioCursorTimeline|AudioSnapshot|MiniaudioMixer|DirectSoundFacade|ExclusiveAudioEngine|ExclusiveAudioEngineInternal|OutputPacingTracker|WasapiAudioPatch|WasapiAudioPatchInternal|WasapiAudioTypes|WasapiEndpoint|InputManager|InputPollingRuntime|InputSnapshotState|SwitchInputPatch|SwitchInputPolicy|NesysHookTransaction|NesysServicePatch|NesysServiceProcess|NesysServiceLauncher|NesysNetworkConfig|ServerAddressOverride|SyntheticNetworkAdapter|RegistryConfigOverride|SessionLog|FrameratePatch|CountdownTimerFreeze|RegisterOpTypes)\.h"' src tests tools
if ($LASTEXITCODE -eq 0) {
    $oldBareIncludes | Write-Host
    throw "obsolete root-relative includes remain"
}
if ($LASTEXITCODE -ne 1) { throw "rg include audit failed: $LASTEXITCODE" }

$expectedHash = Get-Content (
    Join-Path $env:TEMP 'gcloader-source-build-foundation\zero-decrypt-before.sha256')
$actualHash = (Get-FileHash tools/zero_decrypt.zip -Algorithm SHA256).Hash
if ($actualHash -ne $expectedHash) {
    throw "zero_decrypt.zip content changed"
}

git diff --check
git status --short
```

Expected: no root C/C++/DEF files, no production `.cpp` references below `tests`, no obsolete bare includes, unchanged archive hash, no whitespace errors, and only the intended Task 8 relocations/composition edits in status.

- [ ] **Step 9: Commit the completed source/build foundation**

Run:

```powershell
git add -- CMakeLists.txt src tools tests
git commit -m "refactor: complete unified source layout"
git status --short
```

Expected: commit succeeds and final `git status --short` is empty. This closes build/static verification only; gameplay acceptance remains a separate manual gate.

---

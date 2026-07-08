# NesysService Adapter Stability Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a reversible GCLoader patch that injects the existing `iDmacDrv32.dll` into game-launched `NesysService.exe -app` children and suppresses service-side DHCP release/renew calls without replacing the service or named-pipe protocol.

**Architecture:** Keep one DLL with two process roles. The game role keeps existing RFID/input/timer initialization and installs a `CreateProcessA` hook that injects the current DLL into eligible `NesysService.exe -app` launches; the service role skips game-only RVA patches and installs only IP Helper no-op hooks for `IpReleaseAddress` and `IpRenewAddress`.

**Tech Stack:** C++23, Win32 x86 DLL, MinHook, reflect-cpp TOML, plog, CMake/CTest, existing `build-msvc32-latest` MSVC/Ninja build.

## Global Constraints

- Keep the real `NesysService.exe` and named-pipe protocol.
- Prevent the service from releasing or renewing real Windows adapter leases.
- Avoid forcing a preferred adapter as the first fix.
- Keep game-only RFID, input, and frame patches out of the service process.
- Make the behavior reversible by config and visible in logs.
- Non-goal: replacing `NesysService.exe` or emulating the `\\.\pipe\nesys_games` protocol.
- Non-goal: patching `NesysService.exe` on disk.
- Non-goal: reordering or synthesizing `GetAdaptersInfo()` results in the first implementation.
- Non-goal: finding and patching an already-running `NesysService.exe`; the first implementation only handles services launched by the game while GCLoader is active.
- Add `[experimental] enable_nesys_service_adapter_patch = true`.
- The planned default is `true`; setting it to `false` restores the old service launch behavior without rebuilding.
- The launcher hook is fail-open by default.
- If injection fails after the service process is created, log the exact failed step, resume the service thread, and return success to the game.
- If service-side MinHook setup fails, log the failure and let the service continue unpatched.
- Static verification must build `iDmacDrv32.dll` and `ConfigGUI.exe`.
- Runtime acceptance requires `loader-log.txt` evidence for game role, service launch interception, child DLL injection, service role, and both IP Helper suppressions.
- Work in `H:\gc\artifacts\GCLoader`; `H:\gc` is runtime/deploy evidence.
- Preserve the existing dirty worktree entries shown by `git status --short` before this plan was written: `CMakeLists.txt`, `GUI_main.cpp`, `RfidEmu.cpp`, `config.h`, `tests/ConfigFeatureTests.cpp`, `TestModeStorageRedirect.cpp`, `TestModeStorageRedirect.h`, and `tests/TestModeStorageRedirectTests.cpp`.

---

## Scope Check

This spec is one coherent feature, not several independent projects. The config key, process-role split, launcher hook, child injection, and service-side IP Helper hooks all need the same `NesysServicePatch` boundary so service role logic cannot accidentally run game-only RVA patches.

Binary analysis is not part of this implementation plan because the spec already records the IDA-backed facts for `NesysService.exe.i64`: `GetLowerMacAddrAdapter()` at `sub_406C00`, `GetIfTable()` at `sub_406A30`, and service-side calls to `IpReleaseAddress()` and `IpRenewAddress()`. Re-run `$ida-cli` only if implementation evidence shows the service binary has changed from the one used by the spec.

## File Structure

- Create `NesysServiceProcess.h`: pure process-role and launch-eligibility interface used by runtime code and tests.
- Create `NesysServiceProcess.cpp`: pure string parsing, process role classification, and creation-flag helpers. No config, no MinHook, no plog.
- Create `NesysServicePatch.h`: runtime init interface called from `DllMain`.
- Create `NesysServicePatch.cpp`: process-aware runtime patch module. Owns `CreateProcessA` hook, suspended-process DLL injection, service IP Helper hooks, MinHook status logging, and fail-open behavior.
- Create `tests/NesysServicePatchTests.cpp`: CTest executable for process-role, launch-eligibility, and creation-flag helper behavior.
- Modify `config.h`: add strict reflect-cpp key and `ConfigManager::GetEnableNesysServiceAdapterPatch()`.
- Modify `GUI_main.cpp`: expose the experimental checkbox.
- Modify `tests/ConfigFeatureTests.cpp`: require the new key, default it to `true`, test `false`, and fail old configs that omit it.
- Modify `dllmain.cpp`: detect process role before game-only initialization; service role runs only `NesysServicePatchInit()`.
- Modify `CMakeLists.txt`: compile the new runtime files into `iDmacDrv32`, add `NesysServicePatchTests`, and keep `ConfigGUI` building with the expanded config model.

### Task 1: Strict Config and GUI Surface

**Files:**
- Modify: `config.h`
- Modify: `GUI_main.cpp`
- Modify: `tests/ConfigFeatureTests.cpp`

**Interfaces:**
- Consumes: existing `InputConfig`, `ExperimentalConfig`, and `ConfigManager`.
- Produces: `bool ConfigManager::GetEnableNesysServiceAdapterPatch() const`, TOML key `[experimental] enable_nesys_service_adapter_patch`, and GUI checkbox label `NESYS service adapter patch`.

- [ ] **Step 1: Write the failing config test**

In `tests/ConfigFeatureTests.cpp`, update the experimental TOML snippets and assertions so the new key is required and defaults to `true`:

```cpp
constexpr const char* kDefaultExperimentalConfig = R"toml(
card_read = 'f4'

[experimental]
enable_120fps_timer_patches = false
enable_testmode_storage_redirect = false
enable_timer_freeze_patches = false
enable_nesys_service_adapter_patch = true
)toml";

constexpr const char* kDefaultExperimentalTable = R"toml(
[experimental]
enable_120fps_timer_patches = false
enable_testmode_storage_redirect = false
enable_timer_freeze_patches = false
enable_nesys_service_adapter_patch = true
)toml";

constexpr const char* kEnabledExperimentalConfig = R"toml(
card_read = 'f8'

[experimental]
enable_120fps_timer_patches = true
enable_testmode_storage_redirect = true
enable_timer_freeze_patches = true
enable_nesys_service_adapter_patch = false
)toml";
```

Add these assertions after the existing experimental assertions:

```cpp
failures += expect_bool(
    upgraded_defaults.experimental().enable_nesys_service_adapter_patch(),
    true,
    "upgraded default enable_nesys_service_adapter_patch");

failures += expect_bool(
    custom.experimental().enable_nesys_service_adapter_patch(),
    false,
    "custom enable_nesys_service_adapter_patch");
```

Add this parse-failure case after the existing missing experimental-key cases:

```cpp
failures += expect_parse_failure(
    std::string(kRequiredConfigPrefix) + kDefaultCardReadConfig + R"toml(
[experimental]
enable_120fps_timer_patches = false
enable_testmode_storage_redirect = false
enable_timer_freeze_patches = false
)toml",
    "missing enable_nesys_service_adapter_patch");
```

- [ ] **Step 2: Run the test to verify it fails**

Run:

```powershell
& $env:ComSpec /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build build-msvc32-latest --target ConfigFeatureTests'
```

Expected: FAIL at compile time with a message that `ExperimentalConfig` has no member or field named `enable_nesys_service_adapter_patch`.

- [ ] **Step 3: Add the config field and getter**

In `config.h`, change `ExperimentalConfig` to:

```cpp
struct ExperimentalConfig
{
    rfl::Rename<"enable_120fps_timer_patches", bool> enable_120fps_timer_patches = false;
    rfl::Rename<"enable_testmode_storage_redirect", bool> enable_testmode_storage_redirect = false;
    rfl::Rename<"enable_timer_freeze_patches", bool> enable_timer_freeze_patches = false;
    rfl::Rename<"enable_nesys_service_adapter_patch", bool> enable_nesys_service_adapter_patch = true;
};
```

Add this public getter next to the other experimental getters:

```cpp
bool GetEnableNesysServiceAdapterPatch() const { return config.experimental.value().enable_nesys_service_adapter_patch.value(); }
```

- [ ] **Step 4: Add the GUI checkbox**

In `GUI_main.cpp`, insert this block after the `Test-mode storage redirect` checkbox:

```cpp
bool enable_nesys_service_adapter_patch = g_config.experimental().enable_nesys_service_adapter_patch();
if (ImGui::Checkbox("NESYS service adapter patch", &enable_nesys_service_adapter_patch)) {
    g_config.experimental().enable_nesys_service_adapter_patch = enable_nesys_service_adapter_patch;
    g_config_dirty = true;
}
```

- [ ] **Step 5: Run the test to verify it passes**

Run:

```powershell
& $env:ComSpec /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build build-msvc32-latest --target ConfigFeatureTests && ctest --test-dir build-msvc32-latest -C RelWithDebInfo --output-on-failure -R ConfigFeatureTests'
```

Expected: PASS, with `ConfigFeatureTests` listed as passed.

- [ ] **Step 6: Commit**

```powershell
git add -- config.h GUI_main.cpp tests/ConfigFeatureTests.cpp
git commit -m "Add NESYS service adapter patch config"
```

### Task 2: Process Role and Launch Matching Helpers

**Files:**
- Create: `NesysServiceProcess.h`
- Create: `NesysServiceProcess.cpp`
- Create: `tests/NesysServicePatchTests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: Win32 `LPCSTR`, `LPSTR`, `DWORD`, and process image paths.
- Produces:
  - `enum class gc::nesys_service::ProcessRole { Game, Service }`
  - `bool gc::nesys_service::IsNesysServiceImagePathA(std::string_view image_path)`
  - `bool gc::nesys_service::CommandLineContainsAppArgumentA(std::string_view command_line)`
  - `bool gc::nesys_service::IsNesysServiceLaunchA(LPCSTR application_name, LPSTR command_line)`
  - `gc::nesys_service::ProcessRole gc::nesys_service::DetectProcessRoleFromImagePathA(std::string_view image_path)`
  - `gc::nesys_service::ProcessRole gc::nesys_service::DetectCurrentProcessRole()`
  - `bool gc::nesys_service::ShouldRunGameOnlyInitialization(ProcessRole role)`
  - `DWORD gc::nesys_service::AddCreateSuspendedFlag(DWORD creation_flags)`
  - `bool gc::nesys_service::WasCreateSuspendedRequested(DWORD creation_flags)`

- [ ] **Step 1: Write the failing helper tests**

Create `tests/NesysServicePatchTests.cpp`:

```cpp
#include "NesysServiceProcess.h"

#include <Windows.h>
#include <iostream>
#include <string>

namespace {

int expect_true(bool actual, const char* name) {
    if (actual) {
        return 0;
    }
    std::cerr << "Expected true for " << name << "\n";
    return 1;
}

int expect_false(bool actual, const char* name) {
    if (!actual) {
        return 0;
    }
    std::cerr << "Expected false for " << name << "\n";
    return 1;
}

int expect_dword(DWORD actual, DWORD expected, const char* name) {
    if (actual == expected) {
        return 0;
    }
    std::cerr << "Expected " << name << " to be 0x" << std::hex << expected
              << ", got 0x" << actual << std::dec << "\n";
    return 1;
}

LPSTR mutable_command_line(std::string& value) {
    return value.empty() ? nullptr : value.data();
}

} // namespace

int main() {
    int failures = 0;

    failures += expect_true(
        gc::nesys_service::IsNesysServiceImagePathA("NesysService.exe"),
        "bare service image");
    failures += expect_true(
        gc::nesys_service::IsNesysServiceImagePathA("C:\\Games\\GC\\NesysService.exe"),
        "absolute service image");
    failures += expect_true(
        gc::nesys_service::IsNesysServiceImagePathA("\"C:\\Games\\GC\\NesysService.exe\""),
        "quoted service image");
    failures += expect_false(
        gc::nesys_service::IsNesysServiceImagePathA("game.exe"),
        "game image is not service image");

    failures += expect_true(
        gc::nesys_service::CommandLineContainsAppArgumentA("\"NesysService.exe\" -app"),
        "quoted command line has -app");
    failures += expect_true(
        gc::nesys_service::CommandLineContainsAppArgumentA("NesysService.exe -APP"),
        "command line has uppercase -APP");
    failures += expect_false(
        gc::nesys_service::CommandLineContainsAppArgumentA("NesysService.exe -application"),
        "command line rejects -application");

    std::string service_cmd = "\"C:\\Games\\GC\\NesysService.exe\" -app";
    failures += expect_true(
        gc::nesys_service::IsNesysServiceLaunchA(nullptr, mutable_command_line(service_cmd)),
        "null application with service command");

    std::string args_only = "-app";
    failures += expect_true(
        gc::nesys_service::IsNesysServiceLaunchA("C:\\Games\\GC\\NesysService.exe", mutable_command_line(args_only)),
        "application service path with args-only command line");

    std::string wrong_image = "Other.exe -app";
    failures += expect_false(
        gc::nesys_service::IsNesysServiceLaunchA(nullptr, mutable_command_line(wrong_image)),
        "wrong image with app argument");

    std::string missing_app = "NesysService.exe";
    failures += expect_false(
        gc::nesys_service::IsNesysServiceLaunchA(nullptr, mutable_command_line(missing_app)),
        "service image without app argument");

    failures += expect_true(
        gc::nesys_service::DetectProcessRoleFromImagePathA("C:\\Games\\GC\\NesysService.exe")
            == gc::nesys_service::ProcessRole::Service,
        "service role from image path");
    failures += expect_true(
        gc::nesys_service::DetectProcessRoleFromImagePathA("C:\\Games\\GC\\game.exe")
            == gc::nesys_service::ProcessRole::Game,
        "game role from image path");
    failures += expect_true(
        gc::nesys_service::ShouldRunGameOnlyInitialization(gc::nesys_service::ProcessRole::Game),
        "game role runs game initialization");
    failures += expect_false(
        gc::nesys_service::ShouldRunGameOnlyInitialization(gc::nesys_service::ProcessRole::Service),
        "service role skips game initialization");

    failures += expect_dword(
        gc::nesys_service::AddCreateSuspendedFlag(0),
        CREATE_SUSPENDED,
        "empty flags become suspended");
    failures += expect_dword(
        gc::nesys_service::AddCreateSuspendedFlag(CREATE_NO_WINDOW),
        CREATE_NO_WINDOW | CREATE_SUSPENDED,
        "existing flags preserve create no window");
    failures += expect_true(
        gc::nesys_service::WasCreateSuspendedRequested(CREATE_SUSPENDED),
        "detect caller requested suspended");
    failures += expect_false(
        gc::nesys_service::WasCreateSuspendedRequested(CREATE_NO_WINDOW),
        "detect caller did not request suspended");

    return failures == 0 ? 0 : 1;
}
```

Add this CMake target after `ConfigFeatureTests`:

```cmake
add_executable(NesysServicePatchTests
        NesysServiceProcess.cpp
        tests/NesysServicePatchTests.cpp
)
target_include_directories(NesysServicePatchTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)
add_test(NAME NesysServicePatchTests COMMAND NesysServicePatchTests)
```

- [ ] **Step 2: Run the test to verify it fails**

Run:

```powershell
& $env:ComSpec /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build build-msvc32-latest --target NesysServicePatchTests'
```

Expected: FAIL because `NesysServiceProcess.h` and `NesysServiceProcess.cpp` do not exist.

- [ ] **Step 3: Implement the helper interface**

Create `NesysServiceProcess.h`:

```cpp
#pragma once

#include <Windows.h>

#include <string>
#include <string_view>

namespace gc::nesys_service {

enum class ProcessRole {
    Game,
    Service,
};

bool EqualsIgnoreCaseAscii(std::string_view left, std::string_view right);
std::string FileNameOfPathA(std::string_view path);
std::string FirstCommandLineTokenA(std::string_view command_line);
bool IsNesysServiceImagePathA(std::string_view image_path);
bool CommandLineContainsAppArgumentA(std::string_view command_line);
bool IsNesysServiceLaunchA(LPCSTR application_name, LPSTR command_line);
ProcessRole DetectProcessRoleFromImagePathA(std::string_view image_path);
ProcessRole DetectCurrentProcessRole();
bool ShouldRunGameOnlyInitialization(ProcessRole role);
const char* ProcessRoleName(ProcessRole role);
DWORD AddCreateSuspendedFlag(DWORD creation_flags);
bool WasCreateSuspendedRequested(DWORD creation_flags);

} // namespace gc::nesys_service
```

Create `NesysServiceProcess.cpp`:

```cpp
#include "NesysServiceProcess.h"

#include <algorithm>
#include <cctype>

namespace gc::nesys_service {
namespace {

std::string trim_ascii(std::string_view value) {
    std::size_t first = 0;
    while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first])) != 0) {
        ++first;
    }

    std::size_t last = value.size();
    while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1])) != 0) {
        --last;
    }

    return std::string{value.substr(first, last - first)};
}

std::string trim_token_quotes(std::string_view value) {
    auto trimmed = trim_ascii(value);
    if (trimmed.size() >= 2 && trimmed.front() == '"' && trimmed.back() == '"') {
        return trimmed.substr(1, trimmed.size() - 2);
    }
    return trimmed;
}

std::string next_token(std::string_view command_line, std::size_t& offset) {
    while (offset < command_line.size() &&
           std::isspace(static_cast<unsigned char>(command_line[offset])) != 0) {
        ++offset;
    }

    if (offset >= command_line.size()) {
        return {};
    }

    if (command_line[offset] == '"') {
        const std::size_t start = offset;
        ++offset;
        while (offset < command_line.size() && command_line[offset] != '"') {
            ++offset;
        }
        if (offset < command_line.size()) {
            ++offset;
        }
        return std::string{command_line.substr(start, offset - start)};
    }

    const std::size_t start = offset;
    while (offset < command_line.size() &&
           std::isspace(static_cast<unsigned char>(command_line[offset])) == 0) {
        ++offset;
    }
    return std::string{command_line.substr(start, offset - start)};
}

} // namespace

bool EqualsIgnoreCaseAscii(std::string_view left, std::string_view right) {
    if (left.size() != right.size()) {
        return false;
    }

    for (std::size_t i = 0; i < left.size(); ++i) {
        const auto l = static_cast<unsigned char>(left[i]);
        const auto r = static_cast<unsigned char>(right[i]);
        if (std::tolower(l) != std::tolower(r)) {
            return false;
        }
    }

    return true;
}

std::string FileNameOfPathA(std::string_view path) {
    auto trimmed = trim_token_quotes(path);
    const auto separator = trimmed.find_last_of("\\/");
    if (separator == std::string::npos) {
        return trimmed;
    }
    return trimmed.substr(separator + 1);
}

std::string FirstCommandLineTokenA(std::string_view command_line) {
    std::size_t offset = 0;
    return trim_token_quotes(next_token(command_line, offset));
}

bool IsNesysServiceImagePathA(std::string_view image_path) {
    return EqualsIgnoreCaseAscii(FileNameOfPathA(image_path), "NesysService.exe");
}

bool CommandLineContainsAppArgumentA(std::string_view command_line) {
    std::size_t offset = 0;
    while (offset < command_line.size()) {
        const auto token = trim_token_quotes(next_token(command_line, offset));
        if (token.empty()) {
            continue;
        }
        if (EqualsIgnoreCaseAscii(token, "-app")) {
            return true;
        }
    }
    return false;
}

bool IsNesysServiceLaunchA(LPCSTR application_name, LPSTR command_line) {
    const std::string_view app = application_name != nullptr ? std::string_view{application_name} : std::string_view{};
    const std::string_view cmd = command_line != nullptr ? std::string_view{command_line} : std::string_view{};

    if (!CommandLineContainsAppArgumentA(cmd)) {
        return false;
    }

    if (!app.empty()) {
        return IsNesysServiceImagePathA(app);
    }

    return IsNesysServiceImagePathA(FirstCommandLineTokenA(cmd));
}

ProcessRole DetectProcessRoleFromImagePathA(std::string_view image_path) {
    return IsNesysServiceImagePathA(image_path) ? ProcessRole::Service : ProcessRole::Game;
}

ProcessRole DetectCurrentProcessRole() {
    char image_path[MAX_PATH]{};
    const DWORD copied = GetModuleFileNameA(nullptr, image_path, static_cast<DWORD>(std::size(image_path)));
    if (copied == 0 || copied >= std::size(image_path)) {
        return ProcessRole::Game;
    }
    return DetectProcessRoleFromImagePathA(image_path);
}

bool ShouldRunGameOnlyInitialization(ProcessRole role) {
    return role == ProcessRole::Game;
}

const char* ProcessRoleName(ProcessRole role) {
    switch (role) {
    case ProcessRole::Game:
        return "game";
    case ProcessRole::Service:
        return "service";
    }
    return "unknown";
}

DWORD AddCreateSuspendedFlag(DWORD creation_flags) {
    return creation_flags | CREATE_SUSPENDED;
}

bool WasCreateSuspendedRequested(DWORD creation_flags) {
    return (creation_flags & CREATE_SUSPENDED) != 0;
}

} // namespace gc::nesys_service
```

- [ ] **Step 4: Run the tests to verify they pass**

Run:

```powershell
& $env:ComSpec /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build build-msvc32-latest --target NesysServicePatchTests && ctest --test-dir build-msvc32-latest -C RelWithDebInfo --output-on-failure -R NesysServicePatchTests'
```

Expected: PASS, with `NesysServicePatchTests` listed as passed.

- [ ] **Step 5: Commit**

```powershell
git add -- CMakeLists.txt NesysServiceProcess.h NesysServiceProcess.cpp tests/NesysServicePatchTests.cpp
git commit -m "Add NESYS service launch matching tests"
```

### Task 3: Role-Aware DLL Initialization Skeleton

**Files:**
- Create: `NesysServicePatch.h`
- Create: `NesysServicePatch.cpp`
- Modify: `CMakeLists.txt`
- Modify: `dllmain.cpp`

**Interfaces:**
- Consumes: `gc::nesys_service::DetectCurrentProcessRole()`, `gc::nesys_service::ShouldRunGameOnlyInitialization(ProcessRole)`, and `ConfigManager::GetEnableNesysServiceAdapterPatch()`.
- Produces: `void gc::nesys_service::NesysServicePatchInit(HMODULE loader_module)` and a `DllMain` branch where `ProcessRole::Service` never calls `RfidEmuInit()` or `FrameratePatchInit()`.

- [ ] **Step 1: Add compile references before the implementation exists**

In `CMakeLists.txt`, add the new runtime sources to `SOURCES`:

```cmake
set(SOURCES
        CountdownTimerFreeze.cpp
        config.cpp
        dllmain.cpp
        FrameratePatch.cpp
        iDmacDrv32.cpp
        InputManager.cpp
        NesysServicePatch.cpp
        NesysServiceProcess.cpp
        RfidEmu.cpp
        TestModeStorageRedirect.cpp
)
```

Replace `dllmain.cpp` with:

```cpp
#include <windows.h>
#include <filesystem>
#include "InputManager.h"
#include "plog/Log.h"
#include "plog/Initializers/RollingFileInitializer.h"
#include "RfidEmu.h"
#include "SDL3/SDL.h"
#include "FrameratePatch.h"
#include "NesysServicePatch.h"
#include "NesysServiceProcess.h"

#ifndef _M_IX86
 #error "Only Win32 version is supported!"
#endif

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        {
            DisableThreadLibraryCalls(hModule);
#ifdef _DEBUG
            // plog::init(plog::debug, "loader-log.txt");
#else
            plog::init(plog::info, "loader-log.txt");
#endif

            const auto role = gc::nesys_service::DetectCurrentProcessRole();
            PLOG_INFO << "NesysServicePatch: process role=" << gc::nesys_service::ProcessRoleName(role);

            if (gc::nesys_service::ShouldRunGameOnlyInitialization(role)) {
                RfidEmuInit();
                PLOG_DEBUG << "Rfid init complete!" << std::endl;

                FrameratePatchInit();
                PLOG_DEBUG << "120 FPS runtime patch init complete!" << std::endl;
            } else {
                PLOG_INFO << "NesysServicePatch: service role skipping game-only RFID/input/framerate initialization";
            }

            gc::nesys_service::NesysServicePatchInit(hModule);
            break;
        }
    case DLL_PROCESS_DETACH:
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
        break;
    }

    return TRUE;
}
```

- [ ] **Step 2: Run the build to verify it fails**

Run:

```powershell
& $env:ComSpec /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build build-msvc32-latest --target iDmacDrv32'
```

Expected: FAIL because `NesysServicePatch.h` and `NesysServicePatch.cpp` do not exist.

- [ ] **Step 3: Add the runtime module skeleton**

Create `NesysServicePatch.h`:

```cpp
#pragma once

#include <Windows.h>

namespace gc::nesys_service {

void NesysServicePatchInit(HMODULE loader_module);

} // namespace gc::nesys_service
```

Create `NesysServicePatch.cpp`:

```cpp
#include "NesysServicePatch.h"

#include "NesysServiceProcess.h"
#include "config.h"

#include <Windows.h>
#include <atomic>

#include "plog/Log.h"

namespace gc::nesys_service {
namespace {

std::atomic_bool g_initialized{false};
HMODULE g_loader_module = nullptr;

} // namespace

void NesysServicePatchInit(HMODULE loader_module) {
    bool expected = false;
    if (!g_initialized.compare_exchange_strong(expected, true)) {
        return;
    }

    g_loader_module = loader_module;
    const auto role = DetectCurrentProcessRole();
    const bool enabled = ConfigManager::instance().GetEnableNesysServiceAdapterPatch();

    PLOG_INFO << "NesysServicePatch: init role=" << ProcessRoleName(role)
              << " enable_nesys_service_adapter_patch=" << enabled
              << " loader_module=" << reinterpret_cast<void*>(g_loader_module);

    if (!enabled) {
        PLOG_INFO << "NesysServicePatch: disabled by config";
        return;
    }

    if (role == ProcessRole::Service) {
        PLOG_INFO << "NesysServicePatch: service role recognized";
        return;
    }

    PLOG_INFO << "NesysServicePatch: game role recognized";
}

} // namespace gc::nesys_service
```

- [ ] **Step 4: Run the build and helper tests**

Run:

```powershell
& $env:ComSpec /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build build-msvc32-latest --target iDmacDrv32 NesysServicePatchTests && ctest --test-dir build-msvc32-latest -C RelWithDebInfo --output-on-failure -R NesysServicePatchTests'
```

Expected: PASS; `iDmacDrv32.dll` links, and `NesysServicePatchTests` passes.

- [ ] **Step 5: Commit**

```powershell
git add -- CMakeLists.txt dllmain.cpp NesysServicePatch.h NesysServicePatch.cpp
git commit -m "Split GCLoader initialization by NESYS process role"
```

### Task 4: Game-Side CreateProcessA Hook and Child DLL Injection

**Files:**
- Modify: `NesysServicePatch.cpp`
- Modify: `NesysServiceProcess.h`
- Modify: `NesysServiceProcess.cpp`
- Modify: `tests/NesysServicePatchTests.cpp`

**Interfaces:**
- Consumes: `IsNesysServiceLaunchA(LPCSTR, LPSTR)`, `AddCreateSuspendedFlag(DWORD)`, and `WasCreateSuspendedRequested(DWORD)`.
- Produces: game-role `kernel32!CreateProcessA` hook that injects the current loader DLL with `VirtualAllocEx`, `WriteProcessMemory`, and `CreateRemoteThread(LoadLibraryW)`, waits up to 5000 ms, resumes the child main thread when the caller did not request suspension, and returns the original `CreateProcessA` result.

- [ ] **Step 1: Extend helper tests for suspended flag semantics**

The Task 2 test already includes:

```cpp
failures += expect_dword(
    gc::nesys_service::AddCreateSuspendedFlag(0),
    CREATE_SUSPENDED,
    "empty flags become suspended");
failures += expect_dword(
    gc::nesys_service::AddCreateSuspendedFlag(CREATE_NO_WINDOW),
    CREATE_NO_WINDOW | CREATE_SUSPENDED,
    "existing flags preserve create no window");
failures += expect_true(
    gc::nesys_service::WasCreateSuspendedRequested(CREATE_SUSPENDED),
    "detect caller requested suspended");
failures += expect_false(
    gc::nesys_service::WasCreateSuspendedRequested(CREATE_NO_WINDOW),
    "detect caller did not request suspended");
```

If these assertions were not present after Task 2 execution, add them before changing hook code.

- [ ] **Step 2: Run helper tests before runtime hook changes**

Run:

```powershell
& $env:ComSpec /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build build-msvc32-latest --target NesysServicePatchTests && ctest --test-dir build-msvc32-latest -C RelWithDebInfo --output-on-failure -R NesysServicePatchTests'
```

Expected: PASS. The tested helper behavior is the contract the hook must use.

- [ ] **Step 3: Implement game-side hook and injection**

Replace `NesysServicePatch.cpp` with:

```cpp
#include "NesysServicePatch.h"

#include "NesysServiceProcess.h"
#include "config.h"

#include <Windows.h>
#include <atomic>
#include <string>
#include <vector>

#include "MinHook.h"
#include "plog/Log.h"

namespace gc::nesys_service {
namespace {

using CreateProcessAFn = BOOL(WINAPI*)(
    LPCSTR,
    LPSTR,
    LPSECURITY_ATTRIBUTES,
    LPSECURITY_ATTRIBUTES,
    BOOL,
    DWORD,
    LPVOID,
    LPCSTR,
    LPSTARTUPINFOA,
    LPPROCESS_INFORMATION);

std::atomic_bool g_initialized{false};
HMODULE g_loader_module = nullptr;
CreateProcessAFn g_original_create_process_a = nullptr;

bool EnsureMinHookInitialized() {
    const auto status = MH_Initialize();
    if (status == MH_OK || status == MH_ERROR_ALREADY_INITIALIZED) {
        return true;
    }

    PLOG_ERROR << "NesysServicePatch: MH_Initialize failed status=" << static_cast<int>(status);
    return false;
}

std::wstring GetLoaderModulePath() {
    std::vector<wchar_t> buffer(MAX_PATH);
    while (true) {
        const DWORD copied = GetModuleFileNameW(g_loader_module, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (copied == 0) {
            PLOG_ERROR << "NesysServicePatch: GetModuleFileNameW failed gle=" << GetLastError();
            return {};
        }

        if (copied < buffer.size() - 1) {
            return std::wstring{buffer.data(), copied};
        }

        buffer.resize(buffer.size() * 2);
    }
}

void LogWin32Failure(const char* step) {
    PLOG_ERROR << "NesysServicePatch: " << step << " failed gle=" << GetLastError();
}

bool InjectCurrentDllIntoProcess(HANDLE process) {
    const auto dll_path = GetLoaderModulePath();
    if (dll_path.empty()) {
        return false;
    }

    const SIZE_T bytes = (dll_path.size() + 1) * sizeof(wchar_t);
    LPVOID remote_path = VirtualAllocEx(process, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (remote_path == nullptr) {
        LogWin32Failure("VirtualAllocEx");
        return false;
    }

    SIZE_T written = 0;
    if (WriteProcessMemory(process, remote_path, dll_path.c_str(), bytes, &written) == FALSE || written != bytes) {
        LogWin32Failure("WriteProcessMemory");
        VirtualFreeEx(process, remote_path, 0, MEM_RELEASE);
        return false;
    }

    HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
    if (kernel32 == nullptr) {
        LogWin32Failure("GetModuleHandleW(kernel32.dll)");
        VirtualFreeEx(process, remote_path, 0, MEM_RELEASE);
        return false;
    }

    auto load_library_w = reinterpret_cast<LPTHREAD_START_ROUTINE>(
        GetProcAddress(kernel32, "LoadLibraryW"));
    if (load_library_w == nullptr) {
        LogWin32Failure("GetProcAddress(LoadLibraryW)");
        VirtualFreeEx(process, remote_path, 0, MEM_RELEASE);
        return false;
    }

    HANDLE thread = CreateRemoteThread(process, nullptr, 0, load_library_w, remote_path, 0, nullptr);
    if (thread == nullptr) {
        LogWin32Failure("CreateRemoteThread");
        VirtualFreeEx(process, remote_path, 0, MEM_RELEASE);
        return false;
    }

    const DWORD wait_result = WaitForSingleObject(thread, 5000);
    if (wait_result != WAIT_OBJECT_0) {
        PLOG_ERROR << "NesysServicePatch: injection thread wait failed result=" << wait_result
                   << " gle=" << GetLastError();
        CloseHandle(thread);
        VirtualFreeEx(process, remote_path, 0, MEM_RELEASE);
        return false;
    }

    DWORD remote_result = 0;
    if (GetExitCodeThread(thread, &remote_result) == FALSE || remote_result == 0) {
        LogWin32Failure("GetExitCodeThread(LoadLibraryW)");
        CloseHandle(thread);
        VirtualFreeEx(process, remote_path, 0, MEM_RELEASE);
        return false;
    }

    CloseHandle(thread);
    VirtualFreeEx(process, remote_path, 0, MEM_RELEASE);
    PLOG_INFO << "NesysServicePatch: child DLL injection succeeded path=" << std::string(dll_path.begin(), dll_path.end());
    return true;
}

BOOL WINAPI CreateProcessAWrap(
    LPCSTR lpApplicationName,
    LPSTR lpCommandLine,
    LPSECURITY_ATTRIBUTES lpProcessAttributes,
    LPSECURITY_ATTRIBUTES lpThreadAttributes,
    BOOL bInheritHandles,
    DWORD dwCreationFlags,
    LPVOID lpEnvironment,
    LPCSTR lpCurrentDirectory,
    LPSTARTUPINFOA lpStartupInfo,
    LPPROCESS_INFORMATION lpProcessInformation) {

    if (!IsNesysServiceLaunchA(lpApplicationName, lpCommandLine)) {
        return g_original_create_process_a(
            lpApplicationName,
            lpCommandLine,
            lpProcessAttributes,
            lpThreadAttributes,
            bInheritHandles,
            dwCreationFlags,
            lpEnvironment,
            lpCurrentDirectory,
            lpStartupInfo,
            lpProcessInformation);
    }

    PLOG_INFO << "NesysServicePatch: intercepting NesysService.exe -app command="
              << (lpCommandLine != nullptr ? lpCommandLine : "<null>");

    const bool caller_requested_suspended = WasCreateSuspendedRequested(dwCreationFlags);
    const DWORD suspended_flags = AddCreateSuspendedFlag(dwCreationFlags);
    const BOOL result = g_original_create_process_a(
        lpApplicationName,
        lpCommandLine,
        lpProcessAttributes,
        lpThreadAttributes,
        bInheritHandles,
        suspended_flags,
        lpEnvironment,
        lpCurrentDirectory,
        lpStartupInfo,
        lpProcessInformation);
    const DWORD create_process_error = GetLastError();

    if (result == FALSE) {
        PLOG_WARNING << "NesysServicePatch: original CreateProcessA failed gle=" << create_process_error;
        SetLastError(create_process_error);
        return result;
    }

    bool injected = false;
    if (lpProcessInformation != nullptr && lpProcessInformation->hProcess != nullptr) {
        injected = InjectCurrentDllIntoProcess(lpProcessInformation->hProcess);
    } else {
        PLOG_ERROR << "NesysServicePatch: CreateProcessA returned success without process information";
    }

    if (!caller_requested_suspended &&
        lpProcessInformation != nullptr &&
        lpProcessInformation->hThread != nullptr) {
        const DWORD resume_result = ResumeThread(lpProcessInformation->hThread);
        if (resume_result == static_cast<DWORD>(-1)) {
            LogWin32Failure("ResumeThread");
        } else {
            PLOG_INFO << "NesysServicePatch: resumed NesysService.exe main thread";
        }
    }

    if (!injected) {
        PLOG_ERROR << "NesysServicePatch: child DLL injection failed; service resumed fail-open";
    }

    SetLastError(create_process_error);
    return result;
}

bool InstallGameCreateProcessHook() {
    if (!EnsureMinHookInitialized()) {
        return false;
    }

    const auto create_status = MH_CreateHookApi(
        L"kernel32.dll",
        "CreateProcessA",
        reinterpret_cast<LPVOID>(&CreateProcessAWrap),
        reinterpret_cast<LPVOID*>(&g_original_create_process_a));
    if (create_status != MH_OK && create_status != MH_ERROR_ALREADY_CREATED) {
        PLOG_ERROR << "NesysServicePatch: MH_CreateHookApi(CreateProcessA) failed status="
                   << static_cast<int>(create_status);
        return false;
    }

    const auto enable_status = MH_EnableHook(MH_ALL_HOOKS);
    if (enable_status != MH_OK && enable_status != MH_ERROR_ENABLED) {
        PLOG_ERROR << "NesysServicePatch: MH_EnableHook(CreateProcessA) failed status="
                   << static_cast<int>(enable_status);
        return false;
    }

    PLOG_INFO << "NesysServicePatch: CreateProcessA hook installed";
    return true;
}

} // namespace

void NesysServicePatchInit(HMODULE loader_module) {
    bool expected = false;
    if (!g_initialized.compare_exchange_strong(expected, true)) {
        return;
    }

    g_loader_module = loader_module;
    const auto role = DetectCurrentProcessRole();
    const bool enabled = ConfigManager::instance().GetEnableNesysServiceAdapterPatch();

    PLOG_INFO << "NesysServicePatch: init role=" << ProcessRoleName(role)
              << " enable_nesys_service_adapter_patch=" << enabled
              << " loader_module=" << reinterpret_cast<void*>(g_loader_module);

    if (!enabled) {
        PLOG_INFO << "NesysServicePatch: disabled by config";
        return;
    }

    if (role == ProcessRole::Service) {
        PLOG_INFO << "NesysServicePatch: service role recognized";
        return;
    }

    InstallGameCreateProcessHook();
}

} // namespace gc::nesys_service
```

- [ ] **Step 4: Build and run helper tests**

Run:

```powershell
& $env:ComSpec /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build build-msvc32-latest --target iDmacDrv32 NesysServicePatchTests && ctest --test-dir build-msvc32-latest -C RelWithDebInfo --output-on-failure -R NesysServicePatchTests'
```

Expected: PASS; `iDmacDrv32.dll` links with `NesysServicePatch.cpp`, and `NesysServicePatchTests` passes.

- [ ] **Step 5: Commit**

```powershell
git add -- NesysServicePatch.cpp NesysServiceProcess.h NesysServiceProcess.cpp tests/NesysServicePatchTests.cpp
git commit -m "Inject GCLoader into launched NESYS service"
```

### Task 5: Service-Side IP Helper Suppression Hooks

**Files:**
- Modify: `NesysServicePatch.cpp`

**Interfaces:**
- Consumes: service role from `DetectCurrentProcessRole()` and config getter from Task 1.
- Produces: service-role MinHook hooks for `IPHLPAPI!IpReleaseAddress` and `IPHLPAPI!IpRenewAddress`, each returning `NO_ERROR` immediately and logging the adapter index from `PIP_ADAPTER_INDEX_MAP`.

- [ ] **Step 1: Add service hook implementation**

In `NesysServicePatch.cpp`, add this include with the existing includes:

```cpp
#include <Iphlpapi.h>
```

Add these typedefs and globals after `CreateProcessAFn`:

```cpp
using IpReleaseAddressFn = DWORD(WINAPI*)(PIP_ADAPTER_INDEX_MAP);
using IpRenewAddressFn = DWORD(WINAPI*)(PIP_ADAPTER_INDEX_MAP);

IpReleaseAddressFn g_original_ip_release_address = nullptr;
IpRenewAddressFn g_original_ip_renew_address = nullptr;
```

Add these functions before `InstallGameCreateProcessHook()`:

```cpp
DWORD AdapterIndexOrZero(PIP_ADAPTER_INDEX_MAP adapter_info) {
    return adapter_info != nullptr ? adapter_info->Index : 0;
}

DWORD WINAPI IpReleaseAddressWrap(PIP_ADAPTER_INDEX_MAP adapter_info) {
    PLOG_INFO << "NesysServicePatch: suppressed IpReleaseAddress adapter_index="
              << AdapterIndexOrZero(adapter_info);
    return NO_ERROR;
}

DWORD WINAPI IpRenewAddressWrap(PIP_ADAPTER_INDEX_MAP adapter_info) {
    PLOG_INFO << "NesysServicePatch: suppressed IpRenewAddress adapter_index="
              << AdapterIndexOrZero(adapter_info);
    return NO_ERROR;
}

bool InstallServiceIpHelperHooks() {
    if (!EnsureMinHookInitialized()) {
        return false;
    }

    const auto release_status = MH_CreateHookApi(
        L"iphlpapi.dll",
        "IpReleaseAddress",
        reinterpret_cast<LPVOID>(&IpReleaseAddressWrap),
        reinterpret_cast<LPVOID*>(&g_original_ip_release_address));
    if (release_status != MH_OK && release_status != MH_ERROR_ALREADY_CREATED) {
        PLOG_ERROR << "NesysServicePatch: MH_CreateHookApi(IpReleaseAddress) failed status="
                   << static_cast<int>(release_status);
        return false;
    }

    const auto renew_status = MH_CreateHookApi(
        L"iphlpapi.dll",
        "IpRenewAddress",
        reinterpret_cast<LPVOID>(&IpRenewAddressWrap),
        reinterpret_cast<LPVOID*>(&g_original_ip_renew_address));
    if (renew_status != MH_OK && renew_status != MH_ERROR_ALREADY_CREATED) {
        PLOG_ERROR << "NesysServicePatch: MH_CreateHookApi(IpRenewAddress) failed status="
                   << static_cast<int>(renew_status);
        return false;
    }

    const auto enable_status = MH_EnableHook(MH_ALL_HOOKS);
    if (enable_status != MH_OK && enable_status != MH_ERROR_ENABLED) {
        PLOG_ERROR << "NesysServicePatch: MH_EnableHook(IP Helper hooks) failed status="
                   << static_cast<int>(enable_status);
        return false;
    }

    PLOG_INFO << "NesysServicePatch: service IP Helper hooks installed";
    return true;
}
```

Change the service branch in `NesysServicePatchInit()` to:

```cpp
if (role == ProcessRole::Service) {
    if (!InstallServiceIpHelperHooks()) {
        PLOG_ERROR << "NesysServicePatch: service IP Helper hook installation failed; continuing unpatched";
    }
    return;
}
```

- [ ] **Step 2: Build the DLL**

Run:

```powershell
& $env:ComSpec /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build build-msvc32-latest --target iDmacDrv32'
```

Expected: PASS; `iDmacDrv32.dll` links without needing a new on-disk service-hook DLL.

- [ ] **Step 3: Run focused tests**

Run:

```powershell
ctest --test-dir build-msvc32-latest -C RelWithDebInfo --output-on-failure -R "ConfigFeatureTests|NesysServicePatchTests"
```

Expected: PASS; config strictness and launch-helper behavior still pass after adding service hooks.

- [ ] **Step 4: Commit**

```powershell
git add -- NesysServicePatch.cpp
git commit -m "Suppress NESYS service DHCP lease mutation"
```

### Task 6: Full Build, Deploy, and Runtime Acceptance

**Files:**
- Runtime input: `H:\gc\config.toml`
- Runtime output: `H:\gc\loader-log.txt`
- Build output: `build-msvc32-latest\iDmacDrv32.dll`
- Build output: `build-msvc32-latest\ConfigGUI.exe`

**Interfaces:**
- Consumes: all prior task code.
- Produces: static proof that all targets build and runtime proof that game role, child injection, service role, and IP Helper suppression execute.

- [ ] **Step 1: Build all relevant targets**

Run:

```powershell
& $env:ComSpec /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build build-msvc32-latest --target iDmacDrv32 ConfigGUI ConfigFeatureTests CountdownTimerFreezeTests TestModeStorageRedirectTests NesysServicePatchTests'
```

Expected: PASS; `iDmacDrv32.dll`, `ConfigGUI.exe`, and all listed test executables build.

- [ ] **Step 2: Run the full CTest suite**

Run:

```powershell
ctest --test-dir build-msvc32-latest -C RelWithDebInfo --output-on-failure
```

Expected: PASS; CTest reports all configured tests passed.

- [ ] **Step 3: Update runtime config**

Open `H:\gc\config.toml` and make sure the experimental table contains the exact key:

```toml
[experimental]
enable_120fps_timer_patches = false
enable_testmode_storage_redirect = false
enable_timer_freeze_patches = false
enable_nesys_service_adapter_patch = true
```

If the other experimental keys are already intentionally set to `true`, keep their current values and add only:

```toml
enable_nesys_service_adapter_patch = true
```

- [ ] **Step 4: Deploy the built DLL**

Close the game and service processes first so the DLL is not locked. Then run:

```powershell
Copy-Item -LiteralPath 'H:\gc\artifacts\GCLoader\build-msvc32-latest\iDmacDrv32.dll' -Destination 'H:\gc\iDmacDrv32.dll' -Force
```

Expected: command succeeds and `H:\gc\iDmacDrv32.dll` has a newer write time than before the copy.

- [ ] **Step 5: Run enabled runtime acceptance**

Launch Groove Coaster from `H:\gc` with `enable_nesys_service_adapter_patch = true`, let startup reach the NESYS service path, then inspect `H:\gc\loader-log.txt`.

Expected log evidence:

```text
NesysServicePatch: process role=game
NesysServicePatch: init role=game enable_nesys_service_adapter_patch=1
NesysServicePatch: CreateProcessA hook installed
NesysServicePatch: intercepting NesysService.exe -app
NesysServicePatch: child DLL injection succeeded
NesysServicePatch: resumed NesysService.exe main thread
NesysServicePatch: process role=service
NesysServicePatch: service role skipping game-only RFID/input/framerate initialization
NesysServicePatch: init role=service enable_nesys_service_adapter_patch=1
NesysServicePatch: service IP Helper hooks installed
NesysServicePatch: suppressed IpReleaseAddress adapter_index=
NesysServicePatch: suppressed IpRenewAddress adapter_index=
```

Also confirm the game remains responsive on the previously problematic adapter setup and existing RFID/input/timer logs still appear only in the game process role.

- [ ] **Step 6: Run disabled runtime acceptance**

Set:

```toml
enable_nesys_service_adapter_patch = false
```

Launch again and inspect `H:\gc\loader-log.txt`.

Expected log evidence:

```text
NesysServicePatch: init role=game enable_nesys_service_adapter_patch=0
NesysServicePatch: disabled by config
```

Expected absence: no `NesysServicePatch: intercepting NesysService.exe -app`, no `child DLL injection succeeded`, and no service-role IP Helper suppression logs from the disabled run.

- [ ] **Step 7: Commit final verification notes only if a repo artifact was created**

If runtime evidence is recorded in a repo file, stage that file explicitly. Do not stage `H:\gc\loader-log.txt` or deployed runtime binaries.

```powershell
git status --short
git add -- <repo-verification-file>
git commit -m "Record NESYS service adapter patch verification"
```

Expected: commit contains only intentional repo documentation. If no repo artifact was created, skip this commit.

## Self-Review

- Spec coverage: config, game role, service role, `CreateProcessA` interception, suspended child injection, five-second injection wait, fail-open resume, `IpReleaseAddress` suppression, `IpRenewAddress` suppression, game-only patch isolation, static verification, and runtime acceptance are covered by Tasks 1-6.
- Deferred work coverage: `GetIfTable()` normalization and `GetAdaptersInfo()` virtualization remain out of scope by design.
- Placeholder scan: no task relies on unnamed files, unspecified functions, or hidden implementation choices.
- Type consistency: `ProcessRole`, `NesysServicePatchInit(HMODULE)`, `IsNesysServiceLaunchA(LPCSTR, LPSTR)`, `AddCreateSuspendedFlag(DWORD)`, `WasCreateSuspendedRequested(DWORD)`, and `GetEnableNesysServiceAdapterPatch()` are introduced before later tasks consume them.

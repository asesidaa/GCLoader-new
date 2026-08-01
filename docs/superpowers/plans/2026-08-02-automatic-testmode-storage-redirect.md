# Automatic Test-Mode Storage Redirect Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Probe native test-mode persistence on `D:` during every game startup and atomically persist `experimental.enable_testmode_storage_redirect = true` when that probe fails.

**Architecture:** `TestModeStorage` gains a Unicode-safe Win32 probe that creates, writes, flushes, closes, and deletes one temporary file beneath a supplied root. The game-only loader runs that probe independently of system-path selection, then passes the Boolean capability into the existing config preparation transaction so test-mode fallback and any system-root migration are serialized once.

**Tech Stack:** C++23, Win32 file APIs, `std::filesystem::path`, reflect-cpp TOML serialization, CMake/Ninja, CTest, MSVC x86 Debug and RelWithDebInfo.

## Global Constraints

- Execute inline without subagents on `feature/auto-testmode-storage-redirect`.
- Probe `D:\` on every game-process startup regardless of `registry.system_path`.
- Persist only the transition from disabled to enabled; never auto-disable the redirect.
- Combine the redirect mutation with any schema migration or system-root fallback in one atomic config write.
- Do not change existing test-mode path matching, Kernel32 routing, or the game's hashed storage tree.
- Do not deploy or modify the runtime directory as part of this plan.

---

### Task 1: Add the native test-mode persistence probe

**Files:**
- Create: `src/TestModeStorage/NativeStorageProbe.h`
- Create: `src/TestModeStorage/NativeStorageProbe.cpp`
- Modify: `src/TestModeStorage/CMakeLists.txt`
- Modify: `tests/TestModeStorage/TestModeStorageRedirectTests.cpp`

**Interfaces:**
- Produces: `gc::testmode_storage::NativeStorageProbeResult`, `NativeStorageProbeStageName`, `ProbeNativeStorage(const std::filesystem::path&)`, and the production overload `ProbeNativeStorage()` for `D:\`.
- Consumes: Win32 `GetTempFileNameW`, `CreateFileW`, `WriteFile`, `FlushFileBuffers`, `CloseHandle`, and `DeleteFileW`.

- [ ] **Step 1: Add failing behavioral probe tests**

Include `TestModeStorage/NativeStorageProbe.h` in `TestModeStorageRedirectTests.cpp`. Add one success case using `std::filesystem::current_path()` and one unavailable-root case using a unique nonexistent child of the current directory:

```cpp
const auto writable_probe =
    gc::testmode_storage::ProbeNativeStorage(
        std::filesystem::current_path());
failures += expect(
    writable_probe.available &&
        writable_probe.failed_stage ==
            gc::testmode_storage::NativeStorageProbeStage::none &&
        writable_probe.win32_error == ERROR_SUCCESS &&
        writable_probe.cleanup_error == ERROR_SUCCESS &&
        !writable_probe.probe_path.empty() &&
        !std::filesystem::exists(writable_probe.probe_path),
    "writable native storage probe succeeds and cleans up");

const auto missing_root =
    std::filesystem::current_path() /
    (L"missing-native-storage-root-" +
     std::to_wstring(GetCurrentProcessId()) + L"-" +
     std::to_wstring(GetTickCount64()));
failures += expect(
    !std::filesystem::exists(missing_root),
    "native storage missing-root fixture starts absent");
const auto unavailable_probe =
    gc::testmode_storage::ProbeNativeStorage(missing_root);
failures += expect(
    !unavailable_probe.available &&
        unavailable_probe.failed_stage ==
            gc::testmode_storage::NativeStorageProbeStage::create_file &&
        unavailable_probe.win32_error != ERROR_SUCCESS &&
        !std::filesystem::exists(missing_root),
    "unavailable native storage probe fails without creating its root");
```

The mutation caught by these tests is a probe that reports drive presence without proving a real write and flush, or that leaves its temporary file behind.

- [ ] **Step 2: Run the focused test target and verify RED**

Run:

```powershell
& $env:ComSpec /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build --preset msvc32-debug --target TestModeStorageRedirectTests'
```

Expected: compilation fails because `TestModeStorage/NativeStorageProbe.h` and its API do not exist yet. Existing redirect tests remain otherwise unchanged.

- [ ] **Step 3: Implement the minimal probe**

Declare this API in `NativeStorageProbe.h`:

```cpp
#pragma once

#include <Windows.h>

#include <filesystem>

namespace gc::testmode_storage {

enum class NativeStorageProbeStage {
    none,
    create_file,
    open_file,
    write_file,
    flush_file,
};

struct NativeStorageProbeResult {
    bool available{};
    NativeStorageProbeStage failed_stage{};
    DWORD win32_error{ERROR_SUCCESS};
    DWORD cleanup_error{ERROR_SUCCESS};
    std::filesystem::path probe_path;
};

[[nodiscard]] const char* NativeStorageProbeStageName(
    NativeStorageProbeStage stage) noexcept;

[[nodiscard]] NativeStorageProbeResult ProbeNativeStorage(
    const std::filesystem::path& root) noexcept;

[[nodiscard]] NativeStorageProbeResult ProbeNativeStorage() noexcept;

} // namespace gc::testmode_storage
```

Implement `ProbeNativeStorage(root)` with this exact lifecycle:

1. Use `GetTempFileNameW(root.c_str(), L"GCT", 0, buffer)` so the API both chooses a unique path and proves file creation beneath the supplied root.
2. Open the created file with `CreateFileW(..., GENERIC_WRITE, 0, nullptr, TRUNCATE_EXISTING, FILE_ATTRIBUTE_TEMPORARY, nullptr)`.
3. Write exactly one byte and require both `WriteFile == TRUE` and `bytes_written == 1`.
4. Require `FlushFileBuffers == TRUE`.
5. Close every valid handle and call `DeleteFileW` whenever the temporary file was created.
6. Preserve the first capability failure in `failed_stage` and `win32_error`; report close/delete failures separately through `cleanup_error`.
7. Set `available = true` only after create, open, write, and flush all succeed. Cleanup failure does not reverse that result.
8. Catch allocation/filesystem exceptions and return `available = false`, `failed_stage = create_file`, and a stable Win32 error (`ERROR_NOT_ENOUGH_MEMORY` for `std::bad_alloc`, otherwise `ERROR_INVALID_PARAMETER`).
9. Make the no-argument overload call the path overload with `std::filesystem::path{L"D:\\"}`.

Add `NativeStorageProbe.cpp` to `gc_test_mode_storage` in `src/TestModeStorage/CMakeLists.txt`.

- [ ] **Step 4: Build and run the probe tests to verify GREEN**

Run:

```powershell
& $env:ComSpec /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build --preset msvc32-debug --target TestModeStorageRedirectTests && ctest --preset msvc32-debug -R "^TestModeStorageRedirectTests$"'
```

Expected: the existing redirect cases and both new real-filesystem probe cases pass, and the success case leaves no `GCT*.tmp` file.

- [ ] **Step 5: Commit the probe**

```powershell
git add -- src/TestModeStorage/NativeStorageProbe.h src/TestModeStorage/NativeStorageProbe.cpp src/TestModeStorage/CMakeLists.txt tests/TestModeStorage/TestModeStorageRedirectTests.cpp
git commit -m "Add native test-mode storage probe"
```

### Task 2: Persist automatic redirection and wire game startup

**Files:**
- Modify: `src/Config/ConfigDocument.h:61`
- Modify: `src/Config/ConfigDocument.cpp:552`
- Modify: `src/Config/config.h:189`
- Modify: `src/Config/config.cpp:107`
- Modify: `src/Loader/DllMain.cpp:224`
- Modify: `tests/Config/SystemPathConfigTests.cpp:175`
- Modify: `tests/SystemPath/SystemPathIntegrationTests.cpp:232`

**Interfaces:**
- Consumes: `NativeStorageProbeResult::available` from Task 1.
- Produces: `ConfigManager::PrepareGameSystemPath(bool native_testmode_storage_available)` and `PrepareAndPersistGameSystemPathConfiguration(..., bool native_testmode_storage_available, ...)`.

- [ ] **Step 1: Add failing config-transaction cases**

Insert `bool native_testmode_storage_available` between `config_path` and `actions` in every test call to `PrepareAndPersistGameSystemPathConfiguration`. Pass `true` to existing cases that are not testing native storage.

Add a custom-system-path case proving the probe result is independent of system routing:

```cpp
DirectoryFake unavailable_storage_directories{};
WriterFake unavailable_storage_writer{};
const auto unavailable_storage =
    PrepareAndPersistGameSystemPathConfiguration(
        MakeInputConfig(true, ".\\custom"),
        false,
        config_path,
        false,
        MakePreparationActions(
            unavailable_storage_directories,
            unavailable_storage_writer));
failures += Expect(
    unavailable_storage && unavailable_storage->persisted &&
        unavailable_storage_writer.writes == 1 &&
        unavailable_storage_writer.replaces == 1 &&
        unavailable_storage->runtime.configured_path == ".\\custom" &&
        unavailable_storage->config.experimental()
            .enable_testmode_storage_redirect() &&
        unavailable_storage_writer.serialized.find(
            "enable_testmode_storage_redirect = true") !=
            std::string::npos,
    "unavailable native storage persists redirect for custom system path");
```

Add an already-enabled case that passes `false` availability and requires zero writes, and extend the existing fallback case to pass `false` and require one write containing both `.\system` and `enable_testmode_storage_redirect = true`. Keep the existing available/custom case passing `true`; its zero-write assertion proves availability does not rewrite or auto-enable the flag.

The mutations caught are: coupling the decision to `RuntimeRoot::redirect_enabled`, forgetting to persist the new flag, rewriting an already-enabled config, or issuing separate writes for simultaneous fallback and storage failure.

- [ ] **Step 2: Run the transaction test and verify RED**

Run:

```powershell
& $env:ComSpec /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build --preset msvc32-debug --target SystemPathConfigTests'
```

Expected: compilation fails because the production transaction does not yet accept the availability argument. After adding only the signature needed to compile, the new custom-path case must fail because the redirect remains disabled and no config write occurs.

- [ ] **Step 3: Implement the atomic config mutation**

Change the transaction declaration and definition to:

```cpp
PrepareAndPersistGameSystemPathConfiguration(
    InputConfig config,
    bool registry_schema_migrated,
    const std::filesystem::path& config_path,
    bool native_testmode_storage_available,
    GameSystemPathPreparationActions actions =
        ProductionGameSystemPathPreparationActions()) noexcept;
```

After `PrepareGameSystemRoot` succeeds and before computing `must_persist`, apply:

```cpp
const bool testmode_redirect_changed =
    !native_testmode_storage_available &&
    !config.experimental().enable_testmode_storage_redirect();
if (testmode_redirect_changed) {
    config.experimental().enable_testmode_storage_redirect = true;
}
```

Compute persistence as:

```cpp
const bool must_persist =
    testmode_redirect_changed ||
    (config.registry().enabled() &&
     (registry_schema_migrated ||
      prepared->configured_path_changed));
```

This preserves the existing atomic writer and ensures one serialized config contains every startup mutation.

- [ ] **Step 4: Wire the unconditional game-process probe**

Change `ConfigManager::PrepareGameSystemPath` to accept the Boolean capability and forward it to the transaction.

In the existing game-only branch of `Loader/DllMain.cpp`, include `TestModeStorage/NativeStorageProbe.h`, run `ProbeNativeStorage()` immediately before `config.PrepareGameSystemPath(...)`, and pass `probe.available`. Log:

- success at info level;
- capability failure at warning level with `NativeStorageProbeStageName(probe.failed_stage)` and `probe.win32_error`;
- nonzero `probe.cleanup_error` as a separate warning.

Do not run the probe in the NESYS service branch.

Update the two `SystemPathIntegrationTests.cpp` transaction calls to pass `true`, retaining their existing scope.

- [ ] **Step 5: Run focused configuration and integration tests**

Run:

```powershell
& $env:ComSpec /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build --preset msvc32-debug --target TestModeStorageRedirectTests SystemPathConfigTests SystemPathIntegrationTests ConfigDocumentTests ConfigFeatureTests iDmacDrv32 && ctest --preset msvc32-debug -R "^(TestModeStorageRedirectTests|SystemPathConfigTests|SystemPathIntegrationTests|ConfigDocumentTests|ConfigFeatureTests)$"'
```

Expected: all focused tests pass and the Debug x86 loader links with the production probe call.

- [ ] **Step 6: Commit the persisted fallback**

```powershell
git add -- src/Config/ConfigDocument.h src/Config/ConfigDocument.cpp src/Config/config.h src/Config/config.cpp src/Loader/DllMain.cpp tests/Config/SystemPathConfigTests.cpp tests/SystemPath/SystemPathIntegrationTests.cpp
git commit -m "Persist automatic test-mode storage redirect"
```

### Task 3: Verify both supported build configurations

**Files:**
- Verify only; do not modify production or runtime files unless a failing test exposes a defect in Tasks 1 or 2.

**Interfaces:**
- Consumes: the complete feature from Tasks 1 and 2.
- Produces: clean Debug and RelWithDebInfo x86 build/test evidence.

- [ ] **Step 1: Build and test the focused Debug slice from a clean dependency graph**

```powershell
& $env:ComSpec /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build --preset msvc32-debug --target TestModeStorageRedirectTests SystemPathConfigTests SystemPathIntegrationTests ConfigDocumentTests ConfigFeatureTests iDmacDrv32 && ctest --preset msvc32-debug -R "^(TestModeStorageRedirectTests|SystemPathConfigTests|SystemPathIntegrationTests|ConfigDocumentTests|ConfigFeatureTests)$" --output-on-failure'
```

Expected: build succeeds and five focused tests pass.

- [ ] **Step 2: Build and test the same RelWithDebInfo slice**

```powershell
& $env:ComSpec /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build --preset msvc32-release --target TestModeStorageRedirectTests SystemPathConfigTests SystemPathIntegrationTests ConfigDocumentTests ConfigFeatureTests iDmacDrv32 && ctest --preset msvc32-release -R "^(TestModeStorageRedirectTests|SystemPathConfigTests|SystemPathIntegrationTests|ConfigDocumentTests|ConfigFeatureTests)$" --output-on-failure'
```

Expected: build succeeds and the same five tests pass.

- [ ] **Step 3: Inspect repository state**

```powershell
git diff --check
git status --short --branch
git log -4 --oneline --decorate
```

Expected: no whitespace errors, no uncommitted implementation files, and the feature branch contains the design, plan, probe, and persistence commits. Do not deploy or merge in this task.

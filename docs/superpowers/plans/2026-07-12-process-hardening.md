# Process Logging and Registry Failure Hardening Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace unbounded cross-process logging with bounded per-process session logs, retain required static-CRT thread notifications, and close a physical registry handle when overlay tracking allocation fails.

**Architecture:** Add a focused `SessionLog` unit that truncates a role-specific file at process start and enforces a 100 MiB hard ceiling without backups. Detect the process role before initializing plog, keep `loader-log.txt` for the game, and write service diagnostics to `loader-service-log.txt`. Keep the existing registry overlay API and add best-effort close/null cleanup in the real open detour's allocation-failure boundary.

**Tech Stack:** C++23, Win32 x86 file/registry APIs, plog 1.1.10, CMake 3.31+, Ninja, CTest, MSVC x86 (`vcvars32.bat`).

## Global Constraints

- Preserve only the current process session; do not create numbered log backups.
- Use `loader-log.txt` for the game process and `loader-service-log.txt` for the NESYS service process.
- Open each session log with `CREATE_ALWAYS` and enforce exactly `100 * 1024 * 1024` bytes as the production ceiling.
- When the next record would exceed the ceiling, write one fixed limit marker only if it fits, then drop that record and every later record.
- Log open/write/format failure is non-fatal, emits at most one debugger diagnostic per failure boundary, and must not recurse through plog.
- Do not call `DisableThreadLibraryCalls`; this build uses the static CRT and non-trivial `thread_local` state.
- Do not change process-role detection, service injection, hook composition, log severity, or existing diagnostic messages.
- The configuration is immutable for the complete game/service run. Do not add a parent/child configuration bootstrap or digest protocol.
- On registry tracking allocation failure, call the original `RegCloseKey` for the freshly opened handle when available, clear the caller's result, and return `ERROR_NOT_ENOUGH_MEMORY`.
- Preserve normal registry ownership, native query semantics, close/reuse serialization, exact ANSI hook inventory, and every pass-through boundary.
- Preserve unrelated worktree changes and stage only each task's exact pathspec.
- Automated success remains build/static evidence; runtime acceptance remains user-owned.

## File Structure

### New files

- `SessionLog.h` — production limit/marker constants, role filename mapping, bounded session-file writer, and plog appender interface.
- `SessionLog.cpp` — Win32 truncate/write/cap/error behavior and plog formatting adapter.
- `tests/SessionLogTests.cpp` — temporary-file tests for truncation, ordering, strict cap, one marker, drop-after-cap, filenames, and invalid paths.

### Modified files

- `dllmain.cpp` — detect role before logging, initialize the role-specific appender, and retain thread notifications.
- `CMakeLists.txt` — compile `SessionLog.cpp` into `iDmacDrv32` and register `SessionLogTests`.
- `RegistryConfigOverride.cpp` — close/null the fresh HKEY when tracked-set insertion throws.
- `tests/RegistryConfigOverrideTests.cpp` — invoke the real detour with a deterministic fail-next allocation and fake original open/close trampolines.

---

### Task 1: Bounded Per-Process Session Logs and DLL Startup

**Files:**
- Create: `SessionLog.h`
- Create: `SessionLog.cpp`
- Create: `tests/SessionLogTests.cpp`
- Modify: `dllmain.cpp:1-81`
- Modify: `CMakeLists.txt:105-148,168-210`

**Interfaces:**
- Consumes: `gc::nesys_service::ProcessRole`, plog `IAppender`, `TxtFormatter`, `UTF8Converter`, and `NativeEOLConverter`.
- Produces: `gc::session_log::kMaxSessionLogBytes`, `kSessionLogLimitMarker`, `ProcessLogFileName(ProcessRole) -> const wchar_t*`, `BoundedSessionFile::Write(std::string_view) -> bool`, and `SessionLogAppender`.

- [ ] **Step 1: Add the focused session-log test target**

In `CMakeLists.txt`, add this target immediately after `RegistryConfigOverrideTests`:

```cmake
add_executable(SessionLogTests
        SessionLog.cpp
        tests/SessionLogTests.cpp
)
target_include_directories(SessionLogTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${plog_SOURCE_DIR}/include
)
add_test(NAME SessionLogTests COMMAND SessionLogTests)
```

- [ ] **Step 2: Write the complete failing session-log tests**

Create `tests/SessionLogTests.cpp`:

```cpp
#include "SessionLog.h"

#include <Windows.h>

#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>

namespace {

int expect(bool value, const char* name) {
    if (value) {
        return 0;
    }
    std::cerr << "Expectation failed: " << name << "\n";
    return 1;
}

std::wstring create_temp_file_path() {
    wchar_t directory[MAX_PATH]{};
    wchar_t path[MAX_PATH]{};
    const DWORD length = GetTempPathW(MAX_PATH, directory);
    if (length == 0 || length >= MAX_PATH ||
        GetTempFileNameW(directory, L"gcl", 0, path) == 0) {
        return {};
    }
    return path;
}

bool write_raw_file(const std::wstring& path, std::string_view bytes) {
    const HANDLE file = CreateFileW(
        path.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_DELETE,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD written = 0;
    const BOOL result = WriteFile(
        file,
        bytes.data(),
        static_cast<DWORD>(bytes.size()),
        &written,
        nullptr);
    CloseHandle(file);
    return result != FALSE && written == bytes.size();
}

std::string read_file(const std::wstring& path) {
    std::ifstream input(path, std::ios::binary);
    return {
        std::istreambuf_iterator<char>{input},
        std::istreambuf_iterator<char>{},
    };
}

} // namespace

int main() {
    using gc::nesys_service::ProcessRole;
    using namespace gc::session_log;

    int failures = 0;
    failures += expect(
        std::wstring_view{ProcessLogFileName(ProcessRole::Game)} ==
            L"loader-log.txt",
        "game log filename");
    failures += expect(
        std::wstring_view{ProcessLogFileName(ProcessRole::Service)} ==
            L"loader-service-log.txt",
        "service log filename");
    failures += expect(
        kMaxSessionLogBytes == 100ULL * 1024ULL * 1024ULL,
        "production 100 MiB limit");

    const auto path = create_temp_file_path();
    failures += expect(!path.empty(), "temporary log path");
    if (path.empty()) {
        return 1;
    }

    failures += expect(
        write_raw_file(path, "stale-session"),
        "seed stale session");
    {
        BoundedSessionFile file{path.c_str(), 1024};
        failures += expect(file.Write("first-"), "first ordered write");
        failures += expect(file.Write("second"), "second ordered write");
    }
    failures += expect(
        read_file(path) == "first-second",
        "startup truncates and writes in order");

    const std::string prefix = "prefix:";
    const std::uint64_t test_limit =
        prefix.size() + kSessionLogLimitMarker.size();
    {
        BoundedSessionFile file{path.c_str(), test_limit};
        failures += expect(file.Write(prefix), "prefix fits limit");
        failures += expect(
            !file.Write(std::string(kSessionLogLimitMarker.size() + 1, 'x')),
            "oversized record rejected");
        failures += expect(!file.Write("later"), "later record dropped");
    }
    const std::string capped = read_file(path);
    failures += expect(
        capped == prefix + std::string(kSessionLogLimitMarker),
        "one cap marker and no later record");
    failures += expect(
        capped.size() == test_limit,
        "strict byte limit not exceeded");
    failures += expect(
        capped.find(
            kSessionLogLimitMarker,
            prefix.size() + kSessionLogLimitMarker.size()) ==
            std::string::npos,
        "cap marker emitted once");

    {
        BoundedSessionFile invalid{L"", 64};
        failures += expect(
            !invalid.Write("ignored"),
            "invalid path disables writes without throwing");
    }

    DeleteFileW(path.c_str());
    return failures == 0 ? 0 : 1;
}
```

- [ ] **Step 3: Run RED and confirm the missing unit is the cause**

Run:

```powershell
& $env:ComSpec /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build build-msvc32-latest --target SessionLogTests'
```

Expected: CMake/build fails because `SessionLog.h` and `SessionLog.cpp` do not exist.

- [ ] **Step 4: Define the bounded session-log interface**

Create `SessionLog.h`:

```cpp
#pragma once

#include <Windows.h>

#include "NesysServiceProcess.h"

#include "plog/Appenders/IAppender.h"
#include "plog/Util.h"

#include <atomic>
#include <cstdint>
#include <string_view>

namespace gc::session_log {

inline constexpr std::uint64_t kMaxSessionLogBytes =
    100ULL * 1024ULL * 1024ULL;
inline constexpr std::string_view kSessionLogLimitMarker =
    "[GCLoader] session log limit reached; later records dropped.\r\n";

const wchar_t* ProcessLogFileName(
    nesys_service::ProcessRole role) noexcept;

class BoundedSessionFile final {
public:
    BoundedSessionFile(
        const wchar_t* file_name,
        std::uint64_t max_bytes) noexcept;
    ~BoundedSessionFile();

    BoundedSessionFile(const BoundedSessionFile&) = delete;
    BoundedSessionFile& operator=(const BoundedSessionFile&) = delete;

    bool Write(std::string_view bytes) noexcept;

private:
    bool WriteLocked(std::string_view bytes) noexcept;
    void DisableLocked(const wchar_t* message) noexcept;

    HANDLE file_{INVALID_HANDLE_VALUE};
    const std::uint64_t max_bytes_;
    std::uint64_t bytes_written_{0};
    bool capped_{false};
    bool failure_reported_{false};
    plog::util::Mutex mutex_;
};

class SessionLogAppender final : public plog::IAppender {
public:
    explicit SessionLogAppender(
        const wchar_t* file_name,
        std::uint64_t max_bytes = kMaxSessionLogBytes) noexcept;

    void write(const plog::Record& record) override;

private:
    BoundedSessionFile file_;
    std::atomic_bool formatting_failed_{false};
};

} // namespace gc::session_log
```

- [ ] **Step 5: Implement truncation, strict capping, and non-recursive failures**

Create `SessionLog.cpp`:

```cpp
#include "SessionLog.h"

#include "plog/Converters/NativeEOLConverter.h"
#include "plog/Converters/UTF8Converter.h"
#include "plog/Formatters/TxtFormatter.h"

#include <algorithm>
#include <limits>

namespace gc::session_log {

const wchar_t* ProcessLogFileName(
    nesys_service::ProcessRole role) noexcept {
    return role == nesys_service::ProcessRole::Service
        ? L"loader-service-log.txt"
        : L"loader-log.txt";
}

BoundedSessionFile::BoundedSessionFile(
    const wchar_t* file_name,
    std::uint64_t max_bytes) noexcept
    : max_bytes_(max_bytes) {
    file_ = CreateFileW(
        file_name,
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_DELETE,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file_ == INVALID_HANDLE_VALUE) {
        failure_reported_ = true;
        OutputDebugStringW(
            L"GCLoader: failed to open the process session log.\n");
    }
}

BoundedSessionFile::~BoundedSessionFile() {
    plog::util::MutexLock lock(mutex_);
    if (file_ != INVALID_HANDLE_VALUE) {
        CloseHandle(file_);
        file_ = INVALID_HANDLE_VALUE;
    }
}

void BoundedSessionFile::DisableLocked(
    const wchar_t* message) noexcept {
    if (file_ != INVALID_HANDLE_VALUE) {
        CloseHandle(file_);
        file_ = INVALID_HANDLE_VALUE;
    }
    if (!failure_reported_) {
        failure_reported_ = true;
        OutputDebugStringW(message);
    }
}

bool BoundedSessionFile::WriteLocked(
    std::string_view bytes) noexcept {
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const auto remaining = bytes.size() - offset;
        const DWORD request = static_cast<DWORD>((std::min)(
            remaining,
            static_cast<std::size_t>(
                std::numeric_limits<DWORD>::max())));
        DWORD written = 0;
        if (WriteFile(
                file_,
                bytes.data() + offset,
                request,
                &written,
                nullptr) == FALSE ||
            written == 0) {
            DisableLocked(
                L"GCLoader: failed to write the process session log.\n");
            return false;
        }
        offset += written;
        bytes_written_ += written;
    }
    return true;
}

bool BoundedSessionFile::Write(std::string_view bytes) noexcept {
    plog::util::MutexLock lock(mutex_);
    if (file_ == INVALID_HANDLE_VALUE || capped_) {
        return false;
    }
    if (bytes.empty()) {
        return true;
    }

    const std::uint64_t remaining = bytes_written_ < max_bytes_
        ? max_bytes_ - bytes_written_
        : 0;
    if (bytes.size() <= remaining) {
        return WriteLocked(bytes);
    }

    if (kSessionLogLimitMarker.size() <= remaining) {
        WriteLocked(kSessionLogLimitMarker);
    }
    capped_ = true;
    return false;
}

SessionLogAppender::SessionLogAppender(
    const wchar_t* file_name,
    std::uint64_t max_bytes) noexcept
    : file_(file_name, max_bytes) {
}

void SessionLogAppender::write(const plog::Record& record) {
    if (formatting_failed_.load(std::memory_order_relaxed)) {
        return;
    }
    try {
        const auto message =
            plog::NativeEOLConverter<plog::UTF8Converter>::convert(
                plog::TxtFormatter::format(record));
        file_.Write({message.data(), message.size()});
    } catch (...) {
        if (!formatting_failed_.exchange(
                true,
                std::memory_order_relaxed)) {
            OutputDebugStringW(
                L"GCLoader: failed to format the process session log.\n");
        }
    }
}

} // namespace gc::session_log
```

- [ ] **Step 6: Replace the shared appender and retain thread notifications**

In `dllmain.cpp`, remove the current `SharedWin32LogAppender` class and its converter/appender includes. Add:

```cpp
#include "SessionLog.h"
```

Replace `InitSharedLog()` with:

```cpp
void InitProcessLog(gc::nesys_service::ProcessRole role) {
    static gc::session_log::SessionLogAppender loader_log_appender(
        gc::session_log::ProcessLogFileName(role));
    plog::init(plog::info, &loader_log_appender);
}
```

Replace the start of `DLL_PROCESS_ATTACH` with this exact order, deleting the `DisableThreadLibraryCalls(hModule)` call:

```cpp
case DLL_PROCESS_ATTACH:
    {
        const auto role =
            gc::nesys_service::DetectCurrentProcessRole();
        InitProcessLog(role);

        PLOG_DEBUG << "DLL attach!" << std::endl;
        PLOG_INFO
            << "NesysServicePatch: process role="
            << gc::nesys_service::ProcessRoleName(role);
```

Keep `NesysServicePatchInit(hModule, role)`, the game-only initializer branch, the service branch, and the thread/detach cases unchanged after this block.

- [ ] **Step 7: Compile the session-log unit into the DLL**

Add `SessionLog.cpp` to `SOURCES` immediately after `ServerAddressOverride.cpp`:

```cmake
        ServerAddressOverride.cpp
        SessionLog.cpp
        SwitchInputPolicy.cpp
```

- [ ] **Step 8: Run focused GREEN**

Run:

```powershell
& $env:ComSpec /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build build-msvc32-latest --target SessionLogTests iDmacDrv32 && ctest --test-dir build-msvc32-latest --output-on-failure -R SessionLogTests'
```

Expected: both targets build and `SessionLogTests` passes 1/1. The temporary invalid-path case may emit one debugger diagnostic but no console/test failure.

- [ ] **Step 9: Prove the incompatible API call and backup names are absent**

Run:

```powershell
rg -n 'DisableThreadLibraryCalls|loader-log\.1|loader-service-log\.1' dllmain.cpp SessionLog.h SessionLog.cpp
```

Expected: no output and ripgrep exit code 1.

- [ ] **Step 10: Commit the logging slice**

```powershell
git add -- SessionLog.h SessionLog.cpp dllmain.cpp CMakeLists.txt tests/SessionLogTests.cpp
git commit -m "fix: bound process session logs"
```

---

### Task 2: Registry Tracking Allocation-Failure Cleanup

**Files:**
- Modify: `RegistryConfigOverride.cpp:162-183`
- Modify: `tests/RegistryConfigOverrideTests.cpp:1-80,884-907`

**Interfaces:**
- Consumes: the existing `AppendRegistryOverrideHookRequests()` detour/trampoline slots and `InitializeRegistryConfigOverride()` process-lifetime state.
- Produces: a fail-closed OOM path that closes/nulls the physical result without changing the normal `RegistryConfigOverride::Open` interface.

- [ ] **Step 1: Add deterministic fail-next allocation support to the test executable**

In `tests/RegistryConfigOverrideTests.cpp`, add these headers:

```cpp
#include <atomic>
#include <cstdlib>
#include <new>
```

Before the anonymous namespace, add the test executable's replacement allocation functions:

```cpp
std::atomic_bool g_fail_next_allocation{false};

void* operator new(std::size_t size) {
    if (g_fail_next_allocation.exchange(
            false,
            std::memory_order_relaxed)) {
        throw std::bad_alloc{};
    }
    if (void* memory = std::malloc(size == 0 ? 1 : size)) {
        return memory;
    }
    throw std::bad_alloc{};
}

void operator delete(void* memory) noexcept {
    std::free(memory);
}

void operator delete(void* memory, std::size_t) noexcept {
    std::free(memory);
}
```

- [ ] **Step 2: Add allocation-failure fake registry APIs**

Inside the anonymous namespace after `g_fake`, add:

```cpp
struct AllocationFailureState {
    HKEY opened_handle{reinterpret_cast<HKEY>(0x5001)};
    int open_calls{0};
    int close_calls{0};
    HKEY closed_handle{nullptr};
};

AllocationFailureState* g_allocation_failure = nullptr;

LSTATUS WINAPI fake_open_for_allocation_failure(
    HKEY,
    LPCSTR,
    DWORD,
    REGSAM,
    PHKEY result) {
    ++g_allocation_failure->open_calls;
    if (result != nullptr) {
        *result = g_allocation_failure->opened_handle;
    }
    return ERROR_SUCCESS;
}

LSTATUS WINAPI fake_close_for_allocation_failure(HKEY key) {
    ++g_allocation_failure->close_calls;
    g_allocation_failure->closed_handle = key;
    return ERROR_SUCCESS;
}
```

- [ ] **Step 3: Exercise the real detour and assert close/null/OOM**

After the existing hook-request inventory assertions and before the invalid snapshot case, add:

```cpp
    failures += expect(
        InitializeRegistryConfigOverride(ProcessRole::Game, config),
        "initialize allocation-failure detour state");
    std::vector<ApiHookRequest> allocation_requests;
    AppendRegistryOverrideHookRequests(allocation_requests);
    *allocation_requests[0].original = reinterpret_cast<LPVOID>(
        &fake_open_for_allocation_failure);
    *allocation_requests[2].original = reinterpret_cast<LPVOID>(
        &fake_close_for_allocation_failure);

    AllocationFailureState allocation_failure{};
    g_allocation_failure = &allocation_failure;
    HKEY allocation_result = nullptr;
    g_fail_next_allocation.store(true, std::memory_order_relaxed);
    const auto open_detour = reinterpret_cast<RegOpenKeyExAFn>(
        allocation_requests[0].detour);
    failures += expect_status(
        open_detour(
            HKEY_LOCAL_MACHINE,
            "SOFTWARE\\taito\\typex",
            0,
            KEY_READ,
            &allocation_result),
        ERROR_NOT_ENOUGH_MEMORY,
        "tracking allocation failure");
    failures += expect(
        allocation_failure.open_calls == 1,
        "allocation failure opened physical key once");
    failures += expect(
        allocation_failure.close_calls == 1 &&
            allocation_failure.closed_handle ==
                allocation_failure.opened_handle,
        "allocation failure closes exact physical handle");
    failures += expect(
        allocation_result == nullptr,
        "allocation failure clears caller handle");
    g_allocation_failure = nullptr;
```

- [ ] **Step 4: Run RED and confirm the cleanup assertions fail**

Run:

```powershell
& $env:ComSpec /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build build-msvc32-latest --target RegistryConfigOverrideTests && ctest --test-dir build-msvc32-latest --output-on-failure -R RegistryConfigOverrideTests'
```

Expected: the test executable builds, then fails because the detour returns OOM without calling the fake close and without clearing `allocation_result`.

- [ ] **Step 5: Close and clear the result inside the real detour catch boundary**

Replace the catch block in `reg_open_key_ex_a_detour` with:

```cpp
    } catch (...) {
        if (result != nullptr && *result != nullptr) {
            const HKEY opened_handle = *result;
            *result = nullptr;
            if (g_original_reg_close_key != nullptr) {
                g_original_reg_close_key(opened_handle);
            }
        }
        return ERROR_NOT_ENOUGH_MEMORY;
    }
```

The original close trampoline is populated before the transaction enables any registry detour. Do not call the detoured `RegCloseKey` export from this failure boundary.

- [ ] **Step 6: Run focused GREEN and the full configured suite**

Run:

```powershell
& $env:ComSpec /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build build-msvc32-latest --target RegistryConfigOverrideTests && ctest --test-dir build-msvc32-latest --output-on-failure -R RegistryConfigOverrideTests && ctest --test-dir build-msvc32-latest --output-on-failure'
```

Expected: the focused test passes 1/1 and the complete configured suite passes 11/11.

- [ ] **Step 7: Commit the registry cleanup slice**

```powershell
git add -- RegistryConfigOverride.cpp tests/RegistryConfigOverrideTests.cpp
git commit -m "fix: close untracked registry handles on OOM"
```

---

### Task 3: Complete Hardening Verification and Handoff

**Files:**
- Verify: `SessionLog.h`
- Verify: `SessionLog.cpp`
- Verify: `dllmain.cpp`
- Verify: `RegistryConfigOverride.cpp`
- Verify: `CMakeLists.txt`
- Verify: `tests/SessionLogTests.cpp`
- Verify: `tests/RegistryConfigOverrideTests.cpp`

**Interfaces:**
- Consumes: Tasks 1-2 and every configured CTest target.
- Produces: fresh x86 build/test/static evidence; runtime acceptance remains pending.

- [ ] **Step 1: Reconfigure and build every target under x86 MSVC**

Run:

```powershell
& $env:ComSpec /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake -S . -B build-msvc32-latest -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo && cmake --build build-msvc32-latest'
```

Expected: configure/generate and the full target graph complete with exit code 0.

- [ ] **Step 2: Run focused hardening tests and the full suite**

Run:

```powershell
ctest --test-dir build-msvc32-latest --output-on-failure -R "SessionLogTests|RegistryConfigOverrideTests"
ctest --test-dir build-msvc32-latest --output-on-failure
```

Expected: focused tests pass 2/2 and the full suite passes 11/11.

- [ ] **Step 3: Prove the logging and thread-notification boundaries**

Run:

```powershell
rg -n 'loader-log\.txt|loader-service-log\.txt|100ULL \* 1024ULL \* 1024ULL|CREATE_ALWAYS' SessionLog.h SessionLog.cpp
rg -n 'DisableThreadLibraryCalls|loader-log\.1|loader-service-log\.1' dllmain.cpp SessionLog.h SessionLog.cpp
```

Expected: the positive command shows both exact filenames, the exact production limit, and `CREATE_ALWAYS`. The forbidden command emits no output and exits 1.

- [ ] **Step 4: Repeat the registry hook/write/hash boundaries**

Run:

```powershell
rg -n '"Reg(OpenKeyExA|QueryValueExA|CloseKey)"' RegistryConfigOverride.cpp
rg -n --glob 'RegistryConfigOverride.*' --glob 'NesysServicePatch.*' 'RegOpenKeyExW|RegQueryValueExW|RegEnumKeyExA|RegEnumValueA|RegCreateKey|RegSetValue|RegDeleteKey|RegDeleteValue|country\.dat' .
rg -n --glob '*.cpp' --glob '*.h' --glob '!tests/**' 'RegCreateKey|RegSetValue|RegDeleteKey|RegDeleteValue' .
rg -n --glob '*.cpp' --glob '*.h' 'FEAD3BD4D0E0985F101965EDC417DD2B96522F8716FF789D84618FEB0D7A2522|487402D4ABDEF6A857A397CF25C9D681CB6F6052965C500361B0FD14D00913F2' .
```

Expected: the positive inventory contains only the three approved ANSI names. Each forbidden command emits no output and exits 1.

- [ ] **Step 5: Inspect the exact history and worktree state**

Run:

```powershell
git diff --check
git status --short
git log -8 --oneline
```

Expected: no whitespace error or tracked worktree change; only the two preserved unrelated untracked plan paths remain. The design, plan, logging, and registry-cleanup commits are visible.

- [ ] **Step 6: Report the updated automated/runtime boundary**

Report the observed command results, `loader-log.txt` as the game log, `loader-service-log.txt` as the service log, the one-session/100 MiB policy, and the registry OOM cleanup proof. Explicitly keep registry/gameplay runtime acceptance pending the user's existing manual checklist.

## Self-Review

- **Spec coverage:** Task 1 covers per-process names, truncate-on-start, 100 MiB ceiling, one marker, drop-after-cap, non-fatal error handling, DLL ordering, and retained thread notifications. Task 2 covers real-detour allocation failure, original-close cleanup, result clearing, and OOM. Task 3 covers full build, 11 tests, logging/static boundaries, registry boundaries, and runtime-pending handoff.
- **Placeholder scan:** Clean. Every code-producing step includes exact content and every verification step includes its command and expected result.
- **Type consistency:** `ProcessLogFileName`, `BoundedSessionFile`, `SessionLogAppender`, the 64-bit byte limits, plog record interface, hook request pointers, and fake Win32 signatures match their consumers.

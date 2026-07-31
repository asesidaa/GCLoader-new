# Game Crash Dump Handler Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Install a game-process-wide Windows exception handler that survives the game's CRT filter changes and writes a comprehensive crash dump beside the game executable.

**Architecture:** A focused `gc_crash_dump` Win32 diagnostics library owns fixed-storage dump naming, tiered `MiniDumpWriteDump` calls, top-level-filter chaining, and a MinHook detour for `SetUnhandledExceptionFilter`. `DllMain` enables it only for the game role before configuration and game-feature startup; one parent/child integration executable proves replacement protection, Unicode executable-path handling, and real dump contents.

**Tech Stack:** C++23, Win32 x86, DbgHelp `MiniDumpWriteDump`/`MiniDumpReadDumpStream`, MinHook through `gc_hooking`, CMake/Ninja presets, CTest, MSVC static runtime.

## Global Constraints

- Keep source, tests, plans, and commits in `H:\gc\artifacts\GCLoader`; do not deploy to or modify `H:\gc`.
- Install crash handling only for `gc::nesys_service::ProcessRole::Game`; never install it in the NESYS process.
- Preserve the existing iDmac exports, ordinals, x86 calling conventions, and normal runtime behavior.
- Keep installation and every exception boundary non-throwing; no exception may cross `DllMain` or a Win32 hook.
- Crash diagnostics are fail-open: handler or hook failure must not fail DLL attach.
- Use wide Win32 paths and write beside the executable, never relative to the current directory.
- The crash path must not log, allocate heap memory, acquire application locks, or use C++ streams.
- Preserve each later game/CRT top-level filter as downstream behavior while keeping GCLoader registered with Windows.
- Request full accessible memory and the comprehensive metadata flags defined in the approved spec, with full-memory and normal-dump fallback tiers.
- Do not add configuration fields, ConfigGUI controls, dump upload, rotation, compression, or runtime-tree cleanup.
- Automated verification is static/build evidence; a real game crash and debugger inspection remain separate runtime acceptance.

---

## File Structure

- Create `src/Diagnostics/CrashDumpHandler.h`: public install status and non-throwing game crash-handler entry point.
- Create `src/Diagnostics/CrashDumpHandler.cpp`: fixed-storage path preparation, dump attempts, top-level handler, downstream-filter detour, and installation state.
- Create `src/Diagnostics/CMakeLists.txt`: `gc_crash_dump` target and Win32/DbgHelp/MinHook linkage.
- Modify `src/CMakeLists.txt`: include Diagnostics and link `gc_crash_dump` into `iDmacDrv32`.
- Create `tests/Diagnostics/CrashDumpHandlerTests.cpp`: parent/child destructive-crash integration test and minidump inspection.
- Create `tests/Diagnostics/CMakeLists.txt`: test target and CTest registration.
- Modify `tests/CMakeLists.txt`: include Diagnostics tests.
- Modify `src/Loader/DllMain.cpp`: game-only fail-open installation and startup diagnostics.

### Task 1: Build the protected crash-dump component from a real failing crash test

**Files:**
- Create: `tests/Diagnostics/CrashDumpHandlerTests.cpp`
- Create: `tests/Diagnostics/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`
- Create: `src/Diagnostics/CrashDumpHandler.h`
- Create: `src/Diagnostics/CrashDumpHandler.cpp`
- Create: `src/Diagnostics/CMakeLists.txt`
- Modify: `src/CMakeLists.txt`

**Interfaces:**
- Consumes: `gc::win32_hooks::MinHookTransaction`, `gc::win32_hooks::HookRequest`, Win32 `SetUnhandledExceptionFilter`, and DbgHelp `MiniDumpWriteDump`.
- Produces: `gc::crash_dump::InstallStatus`, `gc::crash_dump::InstallStatusName(InstallStatus) noexcept`, and `gc::crash_dump::InstallGameCrashDumpHandler() noexcept` for loader startup.

- [ ] **Step 1: Read the test-quality rules before changing tests**

Read `superpowers:test-driven-development`'s `writing-good-tests.md`. Name the production regression before writing code: removing the setter detour, changing dump placement to the current directory, dropping full memory, or failing to pass the exception context must make this test fail.

- [ ] **Step 2: Add the integration test target and write the failing parent/child test**

Add `Diagnostics` to `tests/CMakeLists.txt` and create:

```cmake
# tests/Diagnostics/CMakeLists.txt
add_executable(CrashDumpHandlerTests CrashDumpHandlerTests.cpp)
target_link_libraries(CrashDumpHandlerTests PRIVATE gc_crash_dump dbghelp)
add_test(NAME CrashDumpHandlerTests COMMAND CrashDumpHandlerTests)
set_tests_properties(CrashDumpHandlerTests PROPERTIES TIMEOUT 120)
```

Write `CrashDumpHandlerTests.cpp` as one executable with `--crash-child` mode. The child contract is:

```cpp
LONG WINAPI ConsumingDownstreamFilter(EXCEPTION_POINTERS*) noexcept
{
    return EXCEPTION_EXECUTE_HANDLER;
}

int RunCrashChild()
{
    using gc::crash_dump::InstallGameCrashDumpHandler;
    using gc::crash_dump::InstallStatus;

    if (InstallGameCrashDumpHandler() == InstallStatus::unavailable) {
        return 100;
    }

    // This must update only GCLoader's downstream slot. If it replaces the
    // real top-level filter, the consuming callback prevents dump creation.
    SetUnhandledExceptionFilter(ConsumingDownstreamFilter);
    RaiseException(
        EXCEPTION_ACCESS_VIOLATION,
        EXCEPTION_NONCONTINUABLE,
        0,
        nullptr);
    return 101;
}
```

The parent must perform all of these observable checks:

1. Create one PID-qualified temporary directory whose name contains
   `崩壊-クラッシュ`, copy its own executable there, and use a different
   current directory for the child.
2. Launch the copied executable with `CreateProcessW` and `--crash-child`, keep
   the child PID, wait at most 90 seconds, and reject exit codes `100`, `101`,
   or `STILL_ACTIVE`.
3. Find exactly one `<copied-stem>-crash-*-p<PID>-t*.dmp` beside the copied
   executable and no matching dump in the unrelated current directory.
4. Map the dump read-only and use `MiniDumpReadDumpStream` to require a valid
   `ExceptionStream` with `EXCEPTION_ACCESS_VIOLATION`, a nonzero crashing
   thread ID, `Memory64ListStream`, `MemoryInfoListStream`,
   `ThreadInfoListStream`, and `HandleDataStream`.
5. Read `MINIDUMP_HEADER::Flags` and independently require at least
   `MiniDumpWithFullMemory`, `MiniDumpWithHandleData`,
   `MiniDumpWithProcessThreadData`, `MiniDumpWithFullMemoryInfo`, and
   `MiniDumpWithThreadInfo`.
6. Close mappings/handles and remove only the unique copied executable, dump,
   and PID-qualified test directory on every parent exit path.

Use the repository's existing `expect(bool, name)` style and print the relevant
Win32 error, child exit code, dump path, or missing stream when an expectation
fails. Do not inspect implementation source text.

- [ ] **Step 3: Run the target build and verify RED**

Run from an x86 MSVC developer environment:

```powershell
cmd.exe /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" >nul && cmake --preset msvc32-debug && cmake --build --preset msvc32-debug --target CrashDumpHandlerTests'
```

Expected: FAIL because `gc_crash_dump` and `Diagnostics/CrashDumpHandler.h` do
not exist. This is the required red result caused by the missing feature, not a
test typo.

- [ ] **Step 4: Add the public crash-dump interface and target**

Create the header with this exact public surface:

```cpp
#pragma once

namespace gc::crash_dump {

enum class InstallStatus {
    installed,
    filter_only,
    unavailable,
};

[[nodiscard]] constexpr const char* InstallStatusName(
    InstallStatus status) noexcept
{
    switch (status) {
    case InstallStatus::installed: return "installed";
    case InstallStatus::filter_only: return "filter_only";
    case InstallStatus::unavailable: return "unavailable";
    }
    return "unavailable";
}

[[nodiscard]] InstallStatus InstallGameCrashDumpHandler() noexcept;

} // namespace gc::crash_dump
```

Create `src/Diagnostics/CMakeLists.txt`:

```cmake
add_library(gc_crash_dump STATIC CrashDumpHandler.cpp)
target_include_directories(gc_crash_dump PUBLIC
        ${PROJECT_SOURCE_DIR}/src
        ${minhook_SOURCE_DIR}/include
        ${plog_SOURCE_DIR}/include
)
target_link_libraries(gc_crash_dump PUBLIC
        gc_hooking
        dbghelp
)
```

Add `add_subdirectory(Diagnostics)` to `src/CMakeLists.txt`. Do not link the
library into `iDmacDrv32` until Task 2, so the first green cycle proves the
component independently.

- [ ] **Step 5: Implement fixed-storage path preparation and dump writing**

In `CrashDumpHandler.cpp`, keep one process-lifetime state with:

```cpp
constexpr std::size_t kMaxDumpPathChars = 32768;

std::array<wchar_t, kMaxDumpPathChars> g_dump_path{};
std::size_t g_dump_prefix_length{};
std::atomic<LPTOP_LEVEL_EXCEPTION_FILTER> g_downstream_filter{};
std::atomic_flag g_dump_in_progress = ATOMIC_FLAG_INIT;
// 0=unattempted, 1=installing, 2=installed, 3=filter_only, 4=unavailable.
std::atomic<LONG> g_install_state{};
decltype(&SetUnhandledExceptionFilter) g_original_set_filter{};
```

`PrepareDumpPathPrefix()` must use `GetModuleFileNameW(nullptr, ...)`, find the
last path separator and final extension without narrowing, and cache
`<directory>\<stem>-crash-` plus its length. It returns `false` for truncation,
missing directory/stem, or insufficient suffix capacity.

Format the remaining name in the exception path from `GetSystemTime`,
`GetCurrentProcessId`, and `GetCurrentThreadId`:

```text
YYYYMMDDTHHMMSS.mmmZ-p<PID>-t<TID>.dmp
```

Append fixed-width and unsigned decimal fields with bounded, allocation-free
helpers that write digits directly into `g_dump_path`; do not use streams,
`std::format`, `std::filesystem`, locale-aware CRT formatting, or heap strings
inside the handler.

Define the primary dump type exactly as:

```cpp
constexpr MINIDUMP_TYPE kComprehensiveDumpType =
    static_cast<MINIDUMP_TYPE>(
        MiniDumpWithFullMemory |
        MiniDumpWithHandleData |
        MiniDumpWithUnloadedModules |
        MiniDumpWithProcessThreadData |
        MiniDumpWithFullMemoryInfo |
        MiniDumpWithThreadInfo |
        MiniDumpWithFullAuxiliaryState |
        MiniDumpWithPrivateWriteCopyMemory |
        MiniDumpIgnoreInaccessibleMemory |
        MiniDumpWithTokenInformation |
        MiniDumpWithModuleHeaders |
        MiniDumpWithAvxXStateContext);

constexpr MINIDUMP_TYPE kCompatibleFullDumpType =
    static_cast<MINIDUMP_TYPE>(
        MiniDumpWithFullMemory |
        MiniDumpWithHandleData |
        MiniDumpWithUnloadedModules |
        MiniDumpWithProcessThreadData |
        MiniDumpWithFullMemoryInfo |
        MiniDumpWithThreadInfo |
        MiniDumpIgnoreInaccessibleMemory);
```

Open the generated path with `CreateFileW(..., CREATE_NEW, ...)`. Supply the
received exception pointers and current thread ID in
`MINIDUMP_EXCEPTION_INFORMATION` with `ClientPointers = FALSE`. Attempt the
comprehensive type, truncate to offset zero with `SetFilePointerEx` plus
`SetEndOfFile` and retry the compatible full type, then similarly retry
`MiniDumpNormal`. Close the file on every path. Leave the last partial file if
all attempts fail; do not delete or rename existing operator dumps.

- [ ] **Step 6: Implement protected filter chaining and fail-open installation**

The setter detour must preserve the Win32 return contract without replacing the
actual OS filter:

```cpp
LPTOP_LEVEL_EXCEPTION_FILTER WINAPI SetUnhandledExceptionFilterDetour(
    LPTOP_LEVEL_EXCEPTION_FILTER requested) noexcept
{
    return g_downstream_filter.exchange(
        requested,
        std::memory_order_acq_rel);
}
```

The top-level handler must:

1. atomically reject recursive/concurrent entries with
   `EXCEPTION_CONTINUE_SEARCH` without calling downstream again;
2. attempt dump creation once;
3. load the current downstream filter;
4. invoke it inside an SEH boundary when it is non-null and not GCLoader's own
   filter, otherwise choose `EXCEPTION_CONTINUE_SEARCH`;
5. clear the gate only if control returns; and
6. never throw across the Win32 callback.

`InstallGameCrashDumpHandler()` must be idempotent. After successful prefix
preparation, call the real `SetUnhandledExceptionFilter` once to register the
GCLoader handler and retain the returned previous filter. Then install this
single shared hook request:

```cpp
const std::array requests{
    gc::win32_hooks::HookRequest{
        .module_name = L"kernel32.dll",
        .export_name = "SetUnhandledExceptionFilter",
        .detour = reinterpret_cast<LPVOID>(
            SetUnhandledExceptionFilterDetour),
        .original = reinterpret_cast<LPVOID*>(
            &g_original_set_filter),
    },
};
```

Use a process-lifetime `MinHookTransaction`. Return `installed` after the hook
commits, `filter_only` when the one-time filter is active but the hook fails,
and `unavailable` when the dump prefix cannot be prepared. Use
`g_install_state.compare_exchange_strong` to let exactly one caller transition
from unattempted to installing; publish the final stable state only after the
filter/hook attempt finishes, and map later calls to that same public result.
`SetUnhandledExceptionFilter` has no distinct failure return, so a null return
means "no previous filter" and is a valid initial downstream value. Catch all
internal C++ failures and convert them to a status; never undo or overwrite the
downstream game filter on a retry.

- [ ] **Step 7: Build and run the focused test to verify GREEN**

Run:

```powershell
cmd.exe /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" >nul && cmake --preset msvc32-debug && cmake --build --preset msvc32-debug --target CrashDumpHandlerTests && ctest --preset msvc32-debug -R ^CrashDumpHandlerTests$ --output-on-failure'
```

Expected: PASS. Confirm the parent reports a parsed exception/full-memory dump,
the consuming downstream filter did not suppress capture, and the unique
Unicode test directory and generated dump are gone afterward.

- [ ] **Step 8: Review the crash path and commit the component**

Inspect the diff specifically for allocations, logging, C++ streams, locks,
unbounded writes, missing handle closes, recursive downstream calls, or a path
derived from the current directory. Then run `git diff --check` and commit only
the Task 1 files:

```powershell
git add -- src/Diagnostics src/CMakeLists.txt tests/Diagnostics tests/CMakeLists.txt
git commit -m "feat: add protected game crash dump handler"
```

### Task 2: Wire game-only startup and verify the complete loader

**Files:**
- Modify: `src/Loader/DllMain.cpp`
- Modify: `src/CMakeLists.txt`

**Interfaces:**
- Consumes: `gc::crash_dump::InstallGameCrashDumpHandler() noexcept`,
  `gc::crash_dump::InstallStatusName(...) noexcept`, and
  `gc::nesys_service::ShouldRunGameOnlyInitialization(ProcessRole)`.
- Produces: a built `iDmacDrv32.dll` that registers crash capture only in the
  game process and imports `MiniDumpWriteDump` from the native DbgHelp DLL.

- [ ] **Step 1: Wire fail-open installation before configuration startup**

Include `Diagnostics/CrashDumpHandler.h`. In `DLL_PROCESS_ATTACH`, retain the
existing process-role detection and process-log initialization, then install
only for the game role before `ApplyConfiguredLogLevel()`:

```cpp
const auto role = gc::nesys_service::DetectCurrentProcessRole();
InitProcessLog(role);

if (gc::nesys_service::ShouldRunGameOnlyInitialization(role)) {
    const auto crash_dump_status =
        gc::crash_dump::InstallGameCrashDumpHandler();
    PLOG_INFO
        << "Game crash dump handler="
        << gc::crash_dump::InstallStatusName(crash_dump_status);
}

ApplyConfiguredLogLevel();
```

Do not branch DLL success on the returned status. Leave the existing later
game-only initialization block and NESYS skip behavior unchanged.

Link `gc_crash_dump` privately into `iDmacDrv32` in `src/CMakeLists.txt`.

- [ ] **Step 2: Build the complete Debug graph and run the full Debug suite**

Run:

```powershell
cmd.exe /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" >nul && cmake --preset msvc32-debug && cmake --build --preset msvc32-debug && ctest --preset msvc32-debug -j 4 --output-on-failure'
```

Expected: the entire Debug build succeeds and every CTest test, including
`CrashDumpHandlerTests`, passes with zero failures.

- [ ] **Step 3: Build the complete Release graph and run the full Release suite**

Run:

```powershell
cmd.exe /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" >nul && cmake --preset msvc32-release && cmake --build --preset msvc32-release && ctest --preset msvc32-release -j 4 --output-on-failure'
```

Expected: the entire Release build succeeds and every CTest test passes with
zero failures. This optimization-sensitive run is mandatory because the code
crosses Win32 callback, SEH, function-pointer, and x86 ABI boundaries.

- [ ] **Step 4: Inspect the built DLL and repository diff**

From the same x86 developer environment, run:

```powershell
dumpbin /imports build-msvc32-release\dist\iDmacDrv32.dll | Select-String -Pattern 'dbghelp.dll|MiniDumpWriteDump'
git diff --check
git status --short
git diff --stat HEAD~1
```

Expected: the DLL imports `MiniDumpWriteDump` from DbgHelp, `git diff --check`
is empty, and status contains only the two intended Task 2 source changes.

- [ ] **Step 5: Commit loader integration**

```powershell
git add -- src/Loader/DllMain.cpp src/CMakeLists.txt
git commit -m "feat: enable game crash dump capture"
```

- [ ] **Step 6: Record the runtime acceptance boundary**

Report the automated child crash as implementation evidence, not real-game
acceptance. Hand off this explicit runtime check without deploying:

1. deploy the newly built DLL only when the user requests deployment;
2. crash `game471.exe` deliberately or reproduce the natural failure;
3. confirm `game471-crash-*.dmp` appears beside `game471.exe`; and
4. open it in WinDbg or Visual Studio and confirm the exception, crashing
   thread, stacks, modules, and referenced/full memory are usable.

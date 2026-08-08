# Executable-Scoped Thread Priority Normalization Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the failed request-delay experiment with a MinHook policy that changes negative `SetThreadPriority` requests made directly by `game471.exe` or `NesysService.exe` to `THREAD_PRIORITY_NORMAL`.

**Architecture:** A focused NESYS component resolves the current main executable image range once, contributes `kernel32.dll!SetThreadPriority` to the existing role-aware MinHook transaction, and uses the detour return address to distinguish executable calls from DLL calls. A pure policy and injected forwarding seam make priority boundaries, handle forwarding, diagnostics, return values, and `LastError` behavior testable without changing live test-runner thread priorities.

**Tech Stack:** C++23, Windows x86 API, MinHook, CMake presets, CTest, daemon-backed IDA evidence from `game471.exe.i64` and `NesysService.exe.i64`.

## Global Constraints

- Apply the policy only when the existing NESYS network-virtualization feature is enabled.
- Clamp only negative priorities requested by the current main executable; calls originating in `iDmacDrv32.dll` or any other DLL pass through unchanged.
- Preserve normal, above-normal, highest, time-critical, and positive background-mode requests exactly.
- Preserve the original thread handle, return value, and `LastError` behavior.
- Install the NESYS-process hook before the suspended child resumes through the existing transactional hook path.
- Emit activation logging plus at most one first-clamp diagnostic per process; do not add per-call logging.
- Add no configuration or ConfigGUI field.
- Remove only `src/Nesys/Network/RequestDelayFix.cpp` and `src/Nesys/Network/RequestDelayFix.h`; do not touch unrelated working-tree content.
- Do not deploy to or mutate `H:\gc`; runtime acceptance belongs to the affected operator.
- Keep automated/static verification distinct from proof that nondeterministic card loading is fixed.
- Run every configure and build command after `C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat` establishes the x86 MSVC environment.

---

### Task 1: Remove the failed experiment and establish the failing policy test

**Files:**
- Delete: `src/Nesys/Network/RequestDelayFix.cpp`
- Delete: `src/Nesys/Network/RequestDelayFix.h`
- Create: `tests/Nesys/ThreadPriorityOverrideTests.cpp`
- Modify: `tests/Nesys/CMakeLists.txt`

**Interfaces:**
- Consumes: the approved design and existing `gc_nesys` test target conventions.
- Produces: a failing compile-time contract for `ExecutableImageRange`, `ReadExecutableImageRange`, `NormalizeExecutableThreadPriority`, `ForwardExecutableThreadPriority`, and `AppendThreadPriorityOverrideHookRequest`.

- [ ] **Step 1: Delete the two failed untracked files**

Delete exactly:

```text
src/Nesys/Network/RequestDelayFix.cpp
src/Nesys/Network/RequestDelayFix.h
```

Verify no live source or build file references remain:

```powershell
rg -n "RequestDelayFix|InitializeRequestDelayFix|ApplyRequestDelayFix" src tests CMakeLists.txt
```

Expected: no matches.

- [ ] **Step 2: Write the focused production-contract test before its header exists**

Create `tests/Nesys/ThreadPriorityOverrideTests.cpp` with a small assertion
harness matching the repository's existing executable tests. Its production
include and fake API state begin as follows:

```cpp
#include "Nesys/ThreadPriorityOverride.h"

#include <Windows.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <string_view>
#include <vector>

namespace {

int expect_true(bool actual, const char* name) {
    if (actual) {
        return 0;
    }
    std::cerr << "Expected true for " << name << "\n";
    return 1;
}

int expect_priority(int actual, int expected, const char* name) {
    if (actual == expected) {
        return 0;
    }
    std::cerr << "Expected " << name << " to be " << expected
              << ", got " << actual << "\n";
    return 1;
}

struct FakeSetPriorityState {
    HANDLE thread{};
    int priority{};
    int calls{};
    BOOL result{TRUE};
    DWORD last_error{ERROR_SUCCESS};
    int diagnostic_calls{};
    std::uintptr_t diagnostic_caller{};
    int diagnostic_requested{};
    int diagnostic_effective{};
};

FakeSetPriorityState* g_fake{};

BOOL WINAPI fake_set_thread_priority(HANDLE thread, int priority) {
    ++g_fake->calls;
    g_fake->thread = thread;
    g_fake->priority = priority;
    SetLastError(g_fake->last_error);
    return g_fake->result;
}

void fake_diagnostic(
    std::uintptr_t caller,
    int requested,
    int effective) noexcept {
    ++g_fake->diagnostic_calls;
    g_fake->diagnostic_caller = caller;
    g_fake->diagnostic_requested = requested;
    g_fake->diagnostic_effective = effective;
    SetLastError(ERROR_BUSY);
}

} // namespace
```

In `main()`, define `ExecutableImageRange image{0x1000, 0x2000}` and assert
these independently derived policy cases:

```cpp
using namespace gc::nesys_service;

failures += expect_priority(
    NormalizeExecutableThreadPriority(
        image, 0x1000, THREAD_PRIORITY_IDLE),
    THREAD_PRIORITY_NORMAL,
    "image-base idle priority");
failures += expect_priority(
    NormalizeExecutableThreadPriority(
        image, 0x1800, THREAD_PRIORITY_LOWEST),
    THREAD_PRIORITY_NORMAL,
    "image lowest priority");
failures += expect_priority(
    NormalizeExecutableThreadPriority(
        image, 0x1FFF, THREAD_PRIORITY_BELOW_NORMAL),
    THREAD_PRIORITY_NORMAL,
    "last image byte below-normal priority");
failures += expect_priority(
    NormalizeExecutableThreadPriority(
        image, 0x1800, THREAD_PRIORITY_NORMAL),
    THREAD_PRIORITY_NORMAL,
    "normal priority passes");
failures += expect_priority(
    NormalizeExecutableThreadPriority(
        image, 0x1800, THREAD_PRIORITY_ABOVE_NORMAL),
    THREAD_PRIORITY_ABOVE_NORMAL,
    "above-normal priority passes");
failures += expect_priority(
    NormalizeExecutableThreadPriority(
        image, 0x1800, THREAD_PRIORITY_TIME_CRITICAL),
    THREAD_PRIORITY_TIME_CRITICAL,
    "time-critical priority passes");
failures += expect_priority(
    NormalizeExecutableThreadPriority(
        image, 0x1800, THREAD_MODE_BACKGROUND_BEGIN),
    THREAD_MODE_BACKGROUND_BEGIN,
    "background begin passes");
failures += expect_priority(
    NormalizeExecutableThreadPriority(
        image, 0x0FFF, THREAD_PRIORITY_BELOW_NORMAL),
    THREAD_PRIORITY_BELOW_NORMAL,
    "caller below image passes");
failures += expect_priority(
    NormalizeExecutableThreadPriority(
        image, 0x2000, THREAD_PRIORITY_LOWEST),
    THREAD_PRIORITY_LOWEST,
    "exclusive image end passes");
```

Exercise forwarding with a sentinel handle. A clamped failure must call the
original once with normal priority, invoke the diagnostic once, return the
original result, and restore the original function's error after the fake
diagnostic changes it:

```cpp
FakeSetPriorityState clamped{};
clamped.result = FALSE;
clamped.last_error = ERROR_ACCESS_DENIED;
g_fake = &clamped;
const auto sentinel = reinterpret_cast<HANDLE>(0x1234);
const BOOL clamped_result = ForwardExecutableThreadPriority(
    image,
    0x1800,
    sentinel,
    THREAD_PRIORITY_LOWEST,
    &fake_set_thread_priority,
    &fake_diagnostic);
failures += expect_true(
    clamped_result == FALSE &&
        clamped.calls == 1 &&
        clamped.thread == sentinel &&
        clamped.priority == THREAD_PRIORITY_NORMAL &&
        clamped.diagnostic_calls == 1 &&
        clamped.diagnostic_caller == 0x1800 &&
        clamped.diagnostic_requested == THREAD_PRIORITY_LOWEST &&
        clamped.diagnostic_effective == THREAD_PRIORITY_NORMAL &&
        GetLastError() == ERROR_ACCESS_DENIED,
    "clamped request preserves API contract");
```

Add a pass-through case from `0x3000` and assert the negative value reaches the
fake unchanged with zero diagnostics. Add a null-original case that returns
`FALSE` and sets `ERROR_INVALID_FUNCTION`.

Build synthetic x86 PE headers in `std::array<std::byte, 0x400>` and exercise
`ReadExecutableImageRange` with:

- a valid DOS header, NT signature, `IMAGE_NT_OPTIONAL_HDR32_MAGIC`, and
  `SizeOfImage == 0x3000`;
- a null module;
- an invalid DOS signature;
- an invalid NT signature;
- a zero `SizeOfImage`;
- a `SizeOfImage` that overflows `uintptr_t` on the x86 test process.

Finally call `AppendThreadPriorityOverrideHookRequest(requests)` and assert one
request names `kernel32.dll` and `SetThreadPriority`, with non-null detour and
original-storage pointers.

- [ ] **Step 3: Register the test target**

Append to `tests/Nesys/CMakeLists.txt`:

```cmake
add_executable(ThreadPriorityOverrideTests ThreadPriorityOverrideTests.cpp)
target_link_libraries(ThreadPriorityOverrideTests PRIVATE gc_nesys)
add_test(NAME ThreadPriorityOverrideTests COMMAND ThreadPriorityOverrideTests)
```

- [ ] **Step 4: Run the target and observe the required RED result**

Run:

```powershell
cmake --preset msvc32-debug
cmake --build --preset msvc32-debug --target ThreadPriorityOverrideTests
```

Expected: compilation fails because
`Nesys/ThreadPriorityOverride.h` does not exist. This is the expected missing
production contract, not a test syntax or configuration failure.

---

### Task 2: Implement the executable-scoped priority policy and hook request

**Files:**
- Create: `src/Nesys/ThreadPriorityOverride.h`
- Create: `src/Nesys/ThreadPriorityOverride.cpp`
- Modify: `src/Nesys/CMakeLists.txt`
- Test: `tests/Nesys/ThreadPriorityOverrideTests.cpp`

**Interfaces:**
- Consumes: `ProcessRole`, `ApiHookRequest`, Win32 PE definitions, and MinHook's original-function storage convention.
- Produces: `InitializeThreadPriorityOverride(ProcessRole)`, `AppendThreadPriorityOverrideHookRequest(std::vector<ApiHookRequest>&)`, and the tested policy/forwarding functions required by Task 1.

- [ ] **Step 1: Declare the focused production and test-seam API**

Create `src/Nesys/ThreadPriorityOverride.h`:

```cpp
#pragma once

#include "Nesys/NesysHookTransaction.h"
#include "Nesys/NesysServiceProcess.h"

#include <Windows.h>

#include <cstdint>
#include <optional>
#include <vector>

namespace gc::nesys_service {

struct ExecutableImageRange {
    std::uintptr_t begin{};
    std::uintptr_t end{};

    bool Contains(std::uintptr_t address) const noexcept;
};

using SetThreadPriorityFn = BOOL(WINAPI*)(HANDLE, int);
using ThreadPriorityClampDiagnosticFn =
    void(*)(std::uintptr_t, int, int) noexcept;

std::optional<ExecutableImageRange> ReadExecutableImageRange(
    HMODULE module) noexcept;

int NormalizeExecutableThreadPriority(
    const ExecutableImageRange& image,
    std::uintptr_t caller,
    int requested_priority) noexcept;

BOOL ForwardExecutableThreadPriority(
    const ExecutableImageRange& image,
    std::uintptr_t caller,
    HANDLE thread,
    int requested_priority,
    SetThreadPriorityFn original,
    ThreadPriorityClampDiagnosticFn diagnostic) noexcept;

bool InitializeThreadPriorityOverride(ProcessRole role) noexcept;
void AppendThreadPriorityOverrideHookRequest(
    std::vector<ApiHookRequest>& requests);

} // namespace gc::nesys_service
```

- [ ] **Step 2: Implement guarded image-range parsing**

In `ThreadPriorityOverride.cpp`, keep SEH in a file-local Boolean helper with
no C++ objects requiring unwinding, then wrap its output in `std::optional`:

```cpp
bool try_read_executable_image_range(
    HMODULE module,
    ExecutableImageRange* output) noexcept {
    if (module == nullptr || output == nullptr) {
        return false;
    }
    __try {
        const auto begin = reinterpret_cast<std::uintptr_t>(module);
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0) {
            return false;
        }
        const auto nt_offset = static_cast<std::uintptr_t>(dos->e_lfanew);
        if (begin > std::numeric_limits<std::uintptr_t>::max() - nt_offset) {
            return false;
        }
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(
            begin + nt_offset);
        if (nt->Signature != IMAGE_NT_SIGNATURE ||
            nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC ||
            nt->OptionalHeader.SizeOfImage == 0) {
            return false;
        }
        const auto size = static_cast<std::uintptr_t>(
            nt->OptionalHeader.SizeOfImage);
        if (begin > std::numeric_limits<std::uintptr_t>::max() - size) {
            return false;
        }
        *output = ExecutableImageRange{begin, begin + size};
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

std::optional<ExecutableImageRange> ReadExecutableImageRange(
    HMODULE module) noexcept {
    ExecutableImageRange image{};
    if (!try_read_executable_image_range(module, &image)) {
        return std::nullopt;
    }
    return image;
}
```

`Contains` returns `begin <= address && address < end` and also requires
`begin < end`.

- [ ] **Step 3: Implement normalization and contract-preserving forwarding**

Implement the pure policy exactly:

```cpp
int NormalizeExecutableThreadPriority(
    const ExecutableImageRange& image,
    std::uintptr_t caller,
    int requested_priority) noexcept {
    if (image.Contains(caller) &&
        requested_priority < THREAD_PRIORITY_NORMAL) {
        return THREAD_PRIORITY_NORMAL;
    }
    return requested_priority;
}
```

`ForwardExecutableThreadPriority` rejects a null original with
`ERROR_INVALID_FUNCTION`. Otherwise it computes the effective priority, calls
the original exactly once, immediately captures `GetLastError()`, invokes the
diagnostic only when the value changed, restores the captured error, and
returns the original result.

- [ ] **Step 4: Implement the production detour and bounded diagnostic**

Store the executable range, `ProcessRole`, original trampoline, and an atomic
first-clamp flag in file-local state. `InitializeThreadPriorityOverride` reads
and stores the current main image from `GetModuleHandleW(nullptr)`.

The detour is:

```cpp
BOOL WINAPI set_thread_priority_detour(HANDLE thread, int priority) {
    return ForwardExecutableThreadPriority(
        g_executable_image,
        reinterpret_cast<std::uintptr_t>(_ReturnAddress()),
        thread,
        priority,
        g_original_set_thread_priority,
        &log_first_clamp);
}
```

`log_first_clamp` uses `exchange(true)` to log only once and reports role,
caller RVA, requested value, and effective value inside `try/catch (...)`.
`AppendThreadPriorityOverrideHookRequest` contributes:

```cpp
requests.push_back({
    L"kernel32.dll",
    "SetThreadPriority",
    reinterpret_cast<LPVOID>(&set_thread_priority_detour),
    reinterpret_cast<LPVOID*>(&g_original_set_thread_priority),
});
```

- [ ] **Step 5: Add the implementation to `gc_nesys` and verify GREEN**

Add `ThreadPriorityOverride.cpp` to `src/Nesys/CMakeLists.txt`, then run:

```powershell
cmake --build --preset msvc32-debug --target ThreadPriorityOverrideTests
ctest --test-dir build-msvc32-debug --output-on-failure -R "^ThreadPriorityOverrideTests$"
```

Expected: the focused test builds and passes with zero failures.

- [ ] **Step 6: Commit the focused policy and failed-attempt removal**

```powershell
git add -- src/Nesys/ThreadPriorityOverride.h src/Nesys/ThreadPriorityOverride.cpp src/Nesys/CMakeLists.txt tests/Nesys/ThreadPriorityOverrideTests.cpp tests/Nesys/CMakeLists.txt
git add -u -- src/Nesys/Network/RequestDelayFix.cpp src/Nesys/Network/RequestDelayFix.h
git commit -m "Normalize executable thread priority requests"
```

Because the deleted experiment was untracked, `git add -u` may have nothing to
stage for those two paths; verify they are absent from disk and status anyway.

---

### Task 3: Integrate the hook into both network-enabled process-role plans

**Files:**
- Modify: `src/Nesys/NesysServiceProcess.h`
- Modify: `src/Nesys/NesysServiceProcess.cpp`
- Modify: `src/Nesys/NesysServicePatch.cpp`
- Modify: `tests/Nesys/NesysServicePatchTests.cpp`

**Interfaces:**
- Consumes: `InitializeThreadPriorityOverride`, `AppendThreadPriorityOverrideHookRequest`, and the existing `NesysFeaturePlan` hook-count invariant.
- Produces: network-enabled game and NESYS plans that each own one executable-priority hook in their transactional installation.

- [ ] **Step 1: Extend role-plan assertions first**

Add `thread_priority_override` to the test's `expect_plan` diagnostic, reference
the new field in every expected aggregate, and change only network-enabled
counts:

| Plan | Old hooks | Expected hooks |
|---|---:|---:|
| Game, network only | 6 | 7 |
| Game, network and registry | 9 | 10 |
| NESYS, network only | 11 | 12 |
| NESYS, network and registry | 14 | 15 |

Network-disabled game and NESYS plans retain their prior counts and expect
`thread_priority_override == false`.

- [ ] **Step 2: Run the existing test target and observe RED**

```powershell
cmake --build --preset msvc32-debug --target NesysServicePatchTests
```

Expected: compilation fails because `NesysFeaturePlan` has no
`thread_priority_override` member. If the field is introduced while arranging
the test, the executable instead fails its network-enabled plan assertions.

- [ ] **Step 3: Extend `NesysFeaturePlan` and its resolver minimally**

Add:

```cpp
bool thread_priority_override{false};
```

between `registry_config_override` and `service_launcher`. Inside the existing
`if (network_enabled)` block, set it true and increment `api_hook_count` once
for both roles:

```cpp
plan.thread_priority_override = true;
++plan.api_hook_count;
```

Do not change `plan.enabled`, launcher, service-exit diagnostic, registry, or
network hook behavior.

- [ ] **Step 4: Add initialization and hook ownership to the transaction**

In `NesysServicePatch.cpp`:

1. Include `Nesys/ThreadPriorityOverride.h`.
2. Before building requests, call `InitializeThreadPriorityOverride(role)`
   when the component is planned; fail initialization with a focused error if
   image-range setup fails.
3. Append `AppendThreadPriorityOverrideHookRequest(requests)` when planned.
4. Keep the existing exact `requests.size() == plan.api_hook_count` invariant.
5. After commit, emit the component-active line from the approved design.

- [ ] **Step 5: Run both NESYS focused tests**

```powershell
cmake --build --preset msvc32-debug --target ThreadPriorityOverrideTests NesysServicePatchTests NesysHookTransactionTests
ctest --test-dir build-msvc32-debug --output-on-failure -R "^(ThreadPriorityOverrideTests|NesysServicePatchTests|NesysHookTransactionTests)$"
```

Expected: all three tests pass with zero failures.

- [ ] **Step 6: Commit role-plan integration**

```powershell
git add -- src/Nesys/NesysServiceProcess.h src/Nesys/NesysServiceProcess.cpp src/Nesys/NesysServicePatch.cpp tests/Nesys/NesysServicePatchTests.cpp
git commit -m "Enable priority normalization in NESYS roles"
```

---

### Task 4: Verify both configurations and prepare the operator handoff

**Files:**
- Verify only: complete source tree and generated build artifacts.

**Interfaces:**
- Consumes: the completed policy, hook transaction integration, and repository build presets.
- Produces: fresh static evidence and an exact DLL path for later operator testing, without deployment.

- [ ] **Step 1: Run focused Debug and Release verification**

```powershell
cmake --preset msvc32-debug
cmake --build --preset msvc32-debug --target ThreadPriorityOverrideTests NesysServicePatchTests NesysHookTransactionTests iDmacDrv32
ctest --test-dir build-msvc32-debug --output-on-failure -R "^(ThreadPriorityOverrideTests|NesysServicePatchTests|NesysHookTransactionTests)$"

cmake --preset msvc32-release
cmake --build --preset msvc32-release --target ThreadPriorityOverrideTests NesysServicePatchTests NesysHookTransactionTests iDmacDrv32
ctest --test-dir build-msvc32-release --output-on-failure -R "^(ThreadPriorityOverrideTests|NesysServicePatchTests|NesysHookTransactionTests)$"
```

Expected: both configurations build and all focused tests pass.

- [ ] **Step 2: Run complete Debug and Release verification**

```powershell
cmake --build --preset msvc32-debug
ctest --preset msvc32-debug -j 4
cmake --build --preset msvc32-release
ctest --preset msvc32-release -j 4
```

Expected: both complete build graphs and both full CTest suites finish with
zero failures.

- [ ] **Step 3: Inspect the final diff, status, and artifacts**

```powershell
git diff --check
git status --short
rg -n "RequestDelayFix|InitializeRequestDelayFix|ApplyRequestDelayFix" src tests CMakeLists.txt
```

Expected:

- `git diff --check` has no output;
- no failed-attempt file or symbol remains;
- status contains no uncommitted implementation file;
- the runtime tree `H:\gc` remains unchanged.

Locate the built Release `iDmacDrv32.dll`, calculate its SHA-256, and report
that source-build artifact path to the user. Do not copy it into `H:\gc`.

- [ ] **Step 4: State the runtime acceptance boundary**

Report static verification separately from operator acceptance. The affected
operator must confirm both process logs contain
`name=thread_priority_override`, observe the first-clamp diagnostic, repeat
card loading across multiple starts, compare request spacing with the previous
roughly 3.6-second cadence, and check gameplay, input, audio, and shutdown for
regressions.

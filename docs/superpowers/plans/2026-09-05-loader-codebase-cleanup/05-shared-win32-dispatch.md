# Shared Win32 Hook Dispatch Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace feature-coupled Kernel32 detours with one physical hook and one typed, deterministic handler chain per shared export while preserving every current argument, result, output, original-call, and `LastError` contract.

**Architecture:** `gc_win32_hooks` owns only Win32 signatures, call contexts, stable handler chains, original invocation, and detour adapters. RFID/JVS, system-path, test-mode-storage, and NESYS diagnostics own handlers in their feature modules. Game startup registers handlers in an explicit order before the SafetyHook registry installs the physical detours.

**Tech Stack:** C++23, Win32 x86 API signatures, SafetyHook-backed `gc_hooking`, fixed startup-built handler arrays, `std::expected`, `std::variant`, CMake/Ninja/MSVC.

**Spec:** `docs/superpowers/specs/2026-09-05-loader-codebase-cleanup-design.md`

## Global Constraints

- Complete Plans 01 through 04 first. Use the behavior-order tables from
  `docs/architecture/loader-cleanup-baseline.md` as the preservation oracle.
- The dispatcher calls an original function at most once. Feature handlers do
  not receive or call original trampolines.
- A pre-handler may continue, transform supported arguments, or complete with
  an explicit result and `LastError`. A post-observer cannot replace the
  result or error.
- Restore the captured final `LastError` immediately before returning to game
  code. Preserve incoming `LastError` around transformations that should not
  change it.
- Handler ordering is declared in Loader composition, never determined by
  static initialization, CMake order, pointer order, or feature-link order.
- Do not add callback-recorder tests or a fake original API table. This native
  hook migration uses code review, builds, and later real-process acceptance.
- Normal successful calls remain free of new per-call logging.

---

## Task 1: Add neutral dispatch contracts

**Files:**

- Create: `src/Win32Hooks/HookDecision.h`
- Create: `src/Win32Hooks/HandlerChain.h`
- Create: `src/Win32Hooks/Kernel32CallContexts.h`
- Create: `src/Win32Hooks/Kernel32Dispatcher.h`
- Create: `src/Win32Hooks/Kernel32Dispatcher.cpp`
- Modify: `src/Win32Hooks/CMakeLists.txt`

**Interfaces:**

```cpp
namespace gc::win32_hooks {

struct ContinueCall final {};

template <typename Result>
struct CompleteCall final {
    Result result{};
    DWORD last_error{};
};

template <typename Result>
using PreCallDecision = std::variant<ContinueCall, CompleteCall<Result>>;

template <typename Result>
struct CallOutcome final {
    Result result{};
    DWORD last_error{};
};

template <typename Context, typename Result>
using PreCallHandler =
    PreCallDecision<Result> (*)(void*, Context&) noexcept;

template <typename Context, typename Result>
using PostCallObserver =
    void (*)(void*, const Context&, const CallOutcome<Result>&) noexcept;

template <typename Context, typename Result, std::size_t Capacity>
class HandlerChain final {
public:
    [[nodiscard]] std::expected<void, RegistrationError>
    AddPre(HandlerIdentity, void*, PreCallHandler<Context, Result>) noexcept;
    [[nodiscard]] std::expected<void, RegistrationError>
    AddPost(HandlerIdentity, void*, PostCallObserver<Context, Result>) noexcept;
};

} // namespace gc::win32_hooks
```

- [ ] **Step 1: Use fixed, startup-populated storage**

The chain has a compile-time capacity equal to the deliberate handler count
for that export. Registration rejects overflow, duplicate feature/site
identity, null callback, and registration after publication. Dispatch performs
no handler-list allocation.

- [ ] **Step 2: Define concrete call contexts**

Create concrete types for:

- `CreateFileA/W`, `ReadFile`, `WriteFile`, `FlushFileBuffers`, `CloseHandle`;
- `FindFirstFileA/W`, `CreateDirectoryA/W`, `DeleteFileA/W`;
- `GetFileAttributesA/W`, `GetDiskFreeSpaceExA/W`, `MoveFileA/W`.

Each context contains all original arguments. Path contexts also own
`std::string` or `std::wstring` replacement storage plus a method that updates
the pointer to that storage for the full original call. Never retain a pointer
to a handler-local string.

- [ ] **Step 3: Keep the dispatcher feature-free**

No header or source in `src/Win32Hooks` may include `Rfid`, `SystemPath`,
`TestModeStorage`, or `Nesys` headers. `Kernel32Dispatcher` owns original
function slots and the typed chains only.

---

## Task 2: Implement one physical detour per shared export

**Files:**

- Create: `src/Win32Hooks/Kernel32Detours.h`
- Create: `src/Win32Hooks/Kernel32Detours.cpp`
- Modify: `src/Win32Hooks/Kernel32Hooks.h`
- Modify: `src/Win32Hooks/Kernel32Hooks.cpp`
- Modify: `src/Win32Hooks/CMakeLists.txt`

- [ ] **Step 1: Implement the common dispatch algorithm**

For each detour:

1. capture incoming `LastError`;
2. run pre-handlers in registered order;
3. stop at the first `CompleteCall`, using its result/error;
4. otherwise restore incoming `LastError` and call the original exactly once;
5. capture the original result and `GetLastError()` immediately;
6. run post-observers in registered order without allowing mutation;
7. restore the outcome's `LastError` and return its result.

Catch every exception at the detour boundary. On an unexpected exception,
return that API's established failure sentinel and set
`ERROR_UNHANDLED_EXCEPTION`; no exception crosses the Win32 ABI.

- [ ] **Step 2: Contribute shared targets to `HookPlan` once**

`AddSharedKernel32Hooks()` adds one named-dispatcher inline hook for every
published chain. Original storage belongs to `Kernel32Dispatcher`. Feature
registration never adds another physical request for the same target.

- [ ] **Step 3: Remove the coupled wrapper object**

Delete `Kernel32Hooks` references to `rfid::Runtime`,
`testmode_storage::Hooks`, `system_path::SystemPathRouter`, and NESYS
diagnostics. Retain the name only as a narrow compatibility facade if needed
during this task; delete it by task end when all callers use the dispatcher.

---

## Task 3: Move RFID/JVS Win32 policy into RFID

**Files:**

- Create: `src/Rfid/Win32FileHandlers.h`
- Create: `src/Rfid/Win32FileHandlers.cpp`
- Create: `src/Rfid/Win32ComHooks.h`
- Create: `src/Rfid/Win32ComHooks.cpp`
- Modify: `src/Rfid/Feature.h`
- Modify: `src/Rfid/Feature.cpp`
- Modify: `src/Rfid/CMakeLists.txt`

- [ ] **Step 1: Implement file/handle pre-handlers**

RFID registers first for `CreateFileA/W`, `ReadFile`, `WriteFile`, and
`CloseHandle`. Preserve:

- exact `COM2` matching;
- the emulated handle identity;
- zeroing `bytes_read`/`bytes_written` before validation;
- rejection of null count pointers, overlapped access, and null buffers with a
  nonzero length;
- exact RFID/JVS results and errors;
- short-circuiting so the original is not called for the emulated handle.

- [ ] **Step 2: Keep RFID-only communication exports exclusive**

Move `GetCommModemStatus`, `EscapeCommFunction`, `ClearCommError`,
`SetCommMask`, `SetupComm`, `GetCommState`, `SetCommState`,
`SetCommTimeouts`, and `GetCommTimeouts` into `Win32ComHooks`. They use the
same central HookPlan/HookRegistry but need no shared handler chain until a
second real consumer exists.

- [ ] **Step 3: Remove feature-wide hook-layer actions**

Delete `FeatureHookLayerActions` if the Plan 01 deletion ledger marks it as a
single-production forwarding seam. Keep RFID runtime construction and worker
lifetime independent of hook ownership.

---

## Task 4: Move system-path and test-mode-storage routing into handlers

**Files:**

- Create: `src/SystemPath/Win32PathHandlers.h`
- Create: `src/SystemPath/Win32PathHandlers.cpp`
- Modify: `src/SystemPath/CMakeLists.txt`
- Create: `src/TestModeStorage/Win32PathHandlers.h`
- Create: `src/TestModeStorage/Win32PathHandlers.cpp`
- Modify: `src/TestModeStorage/CMakeLists.txt`

- [ ] **Step 1: Register system-path before storage for CreateFile**

Preserve the baseline order. A successful system-path ANSI match may replace
the operation with its wide-path equivalent; model this explicitly in the
context with an `OriginalVariant` selector so exactly one original is called.
On routing error, complete with the API failure sentinel and routing error.

- [ ] **Step 2: Preserve every path API's actual ownership**

Use the baseline table rather than applying one blanket chain:

- system-path and storage both participate only where they currently do;
- storage owns its ANSI/wide test-mode mappings;
- system-path owns its configured-root mappings;
- no handler is registered when its feature is disabled;
- directory, delete, attributes, disk-space, move, and find operations retain
  their current A/W conversion and fallback behavior.

- [ ] **Step 3: Remove hook-layer dependencies from routers**

The feature handlers depend on neutral call contexts. The routers do not know
about SafetyHook or original trampolines.

---

## Task 5: Move NESYS request observation into post-observers

**Files:**

- Create: `src/Nesys/Diagnostics/GamePipeWin32Observers.h`
- Create: `src/Nesys/Diagnostics/GamePipeWin32Observers.cpp`
- Modify: `src/Nesys/Diagnostics/RequestPipelineDiagnostics.h`
- Modify: `src/Nesys/Diagnostics/RequestPipelineDiagnostics.cpp`
- Modify: `src/Nesys/CMakeLists.txt`

- [ ] **Step 1: Register observation after routing/original execution**

Preserve these observations:

- `CreateFileA/W`: original path identity, result, start/end monotonic time,
  final error;
- `WriteFile`: handle, input buffer/length, result, final error, timing;
- `FlushFileBuffers`: handle, result, final error, timing;
- `CloseHandle`: remove tracking only after the native close attempt.

The observer cannot alter result, output counts, or `LastError`.

- [ ] **Step 2: Keep diagnostics out of unrelated calls**

Perform pipe-name and tracked-handle tests before timestamp or formatting work.
Do not introduce logging on normal unrelated file calls.

---

## Task 6: Declare the complete handler order in Loader

**Files:**

- Create: `src/Loader/GameWin32HookComposition.h`
- Create: `src/Loader/GameWin32HookComposition.cpp`
- Modify: `src/Loader/NonVersionedHookPlan.cpp`
- Modify: `src/CMakeLists.txt`

**Produces:**

```cpp
std::expected<void, win32_hooks::RegistrationError>
ComposeGameWin32Handlers(
    win32_hooks::Kernel32Dispatcher&,
    rfid::Runtime&,
    system_path::SystemPathRouter&,
    testmode_storage::Hooks&,
    const nesys_service::diagnostics::GamePipeObserver&) noexcept;
```

- [ ] **Step 1: Register the core chains in exact order**

Use these orders:

```text
CreateFileA/W pre: RFID -> SystemPath -> TestModeStorage
CreateFileA/W post: NESYS diagnostics
WriteFile pre: RFID
WriteFile post: NESYS diagnostics
ReadFile pre: RFID
FlushFileBuffers post: NESYS diagnostics
CloseHandle pre: RFID
CloseHandle post: NESYS diagnostics
```

Then register the path-operation chains exactly as captured in Plan 01.

- [ ] **Step 2: Publish before hook installation**

Complete and freeze all chains before `HookRegistry::Install`. A registration
failure uses `AbortProcess`. No feature may register a handler after the
physical hook is enabled.

---

## Task 7: Remove old coupling and verify

**Files:**

- Delete: superseded `Kernel32Hooks` implementation after all callers move
- Modify: affected CMake targets and include dependencies
- Modify: `docs/architecture/loader-cleanup-baseline.md` only to append a
  dated `After shared dispatch` comparison; do not rewrite the frozen baseline

- [ ] **Step 1: Audit dependency direction**

Run:

```powershell
rg -n '#include "(Rfid|SystemPath|TestModeStorage|Nesys)/' src\Win32Hooks
rg -n 'originals_\.|OriginalKernel32Api|Kernel32Hooks::active_' src\Rfid src\SystemPath src\TestModeStorage src\Nesys src\Win32Hooks
```

Expected: no feature header enters `Win32Hooks`; original function storage and
physical detours exist only in the neutral dispatcher/hooking layer.

- [ ] **Step 2: Compare every behavior row manually**

For every export in the Plan 01 table, inspect old baseline and new handler
code side by side. Append a Markdown row with `preserved` or an exact unresolved
difference. Stop before commit if any difference is unexplained.

- [ ] **Step 3: Run full static verification**

```powershell
cmake --build --preset msvc32-debug
ctest --preset msvc32-debug -j 4
cmake --build --preset msvc32-release
ctest --preset msvc32-release -j 4
git diff --check
git status --short --branch
```

This does not prove RFID/JVS, storage, system-path, NESYS observation, or
`LastError` behavior in the game process.

- [ ] **Step 4: Commit**

```powershell
git add -- src\Win32Hooks src\Rfid src\SystemPath src\TestModeStorage src\Nesys\Diagnostics src\Nesys\CMakeLists.txt src\Loader\GameWin32HookComposition.h src\Loader\GameWin32HookComposition.cpp src\Loader\NonVersionedHookPlan.cpp src\CMakeLists.txt docs\architecture\loader-cleanup-baseline.md
git commit -m "Add shared Win32 hook dispatch"
```

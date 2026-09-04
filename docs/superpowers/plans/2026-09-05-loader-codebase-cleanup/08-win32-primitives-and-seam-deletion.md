# Win32 Primitives and Shallow-Seam Deletion Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace duplicated UTF conversion, captured-error formatting, and ordinary HANDLE lifetime code with one narrow Win32 module, then delete production adapters whose only purpose was indirection.

**Architecture:** `gc_platform_win32` provides strict mechanical primitives and no feature policy. Callers decide fallback text, logging, retry, optionality, and fatality. Ordinary CloseHandle-only resources use one move-only owner; specialized thread, pipe, registry, COM, MMCSS, hook, and process-lifetime ownership remains in its domain. The Plan 01 seam ledger controls deletion, with current single-production seams removed explicitly.

**Tech Stack:** C++23, Win32 Unicode and HANDLE APIs, `std::expected`, move-only RAII, CMake/Ninja/MSVC, real Win32 contract tests.

**Spec:** `docs/superpowers/specs/2026-09-05-loader-codebase-cleanup-design.md`

## Global Constraints

- Complete Plans 01 through 07 first.
- `gc_platform_win32` is a leaf platform target. It must not depend on Loader,
  Config, Audio, Input, RFID, NESYS, runtime patches, GUI, plog, SafetyHook, or
  reflect-cpp.
- Conversion helpers are strict mechanics. They do not silently substitute a
  code page, replacement character, empty value, fallback label, or modal
  text.
- Capture `GetLastError` immediately at the failing API. Formatting a stored
  code must never overwrite the code being reported.
- `UniqueHandle` owns only values released by `CloseHandle`. Do not use it for
  `HKEY`, COM pointers, SafetyHook objects, sockets, MMCSS handles requiring
  `AvRevertMmThreadCharacteristics`, or resources with extra shutdown steps.
- Removing an adapter must delete complexity. Do not copy its operation table
  into each caller or erase a meaningful platform/protocol boundary.
- Do not change process lifetime, launch a target process, or add fake Win32
  backends.

---

## Task 1: Add the narrow Win32 platform target

**Files:**

- Create: `src/Platform/Win32/CMakeLists.txt`
- Create: `src/Platform/Win32/Utf.h`
- Create: `src/Platform/Win32/Utf.cpp`
- Create: `src/Platform/Win32/Win32Error.h`
- Create: `src/Platform/Win32/Win32Error.cpp`
- Create: `src/Platform/Win32/UniqueHandle.h`
- Modify: `src/Platform/CMakeLists.txt`

**Interfaces:**

```cpp
namespace gc::platform::win32 {

enum class UtfDirection : std::uint8_t {
    utf8_to_utf16,
    utf16_to_utf8,
};

struct UtfError final {
    UtfDirection direction{};
    DWORD win32_error{ERROR_SUCCESS};
};

[[nodiscard]] std::expected<std::wstring, UtfError>
Utf8ToWide(std::string_view text) noexcept;

[[nodiscard]] std::expected<std::string, UtfError>
WideToUtf8(std::wstring_view text) noexcept;

struct Win32FormatError final {
    DWORD source_error{ERROR_SUCCESS};
    DWORD format_error{ERROR_SUCCESS};
};

[[nodiscard]] std::expected<std::wstring, Win32FormatError>
FormatWin32Error(DWORD captured_error) noexcept;

class UniqueHandle final {
public:
    UniqueHandle() noexcept = default;
    explicit UniqueHandle(HANDLE value) noexcept;
    ~UniqueHandle();
    UniqueHandle(UniqueHandle&&) noexcept;
    UniqueHandle& operator=(UniqueHandle&&) noexcept;
    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;

    [[nodiscard]] HANDLE get() const noexcept;
    [[nodiscard]] explicit operator bool() const noexcept;
    [[nodiscard]] HANDLE release() noexcept;
    void reset(HANDLE value = nullptr) noexcept;
};

} // namespace gc::platform::win32
```

- [ ] **Step 1: Implement strict UTF-8 to UTF-16**

Return an empty string for empty input without calling Win32. Reject inputs
larger than `INT_MAX`. Use a sizing call and a writing call to
`MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, ...)`; require exact
written length and return the immediately captured error for either failure.
Catch allocation failure at the `noexcept` boundary and report a typed
out-of-memory error.

- [ ] **Step 2: Implement strict UTF-16 to UTF-8**

Use `WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, ...)` with the same
empty/length/sizing/write rules. Do not use an ANSI code page, best-fit
mapping, or a default character.

- [ ] **Step 3: Format a previously captured error**

Call `FormatMessageW` using the provided code, trim only terminal CR/LF, and
return `Win32FormatError{captured_error, immediate_format_error}` if formatting
or allocation fails. Preserve the caller's captured code and do not call
`GetLastError` to decide which source error to describe. The caller owns any
numeric or feature-specific fallback text.

- [ ] **Step 4: Implement ordinary HANDLE ownership**

Treat both `nullptr` and `INVALID_HANDLE_VALUE` as empty. Move transfers the
value, `release` disowns without closing, and `reset` closes the prior valid
value exactly once. The destructor calls only `CloseHandle` and never logs,
throws, waits, disconnects a pipe, joins a thread, or terminates a process.

- [ ] **Step 5: Make CMake ownership unambiguous**

Change `src/Platform/CMakeLists.txt` to add `Win32`; the new Win32 CMake file
adds `Hooking` and defines `gc_platform_win32`. Avoid adding both
`Win32/Hooking` and `Win32` from the parent. `gc_hooking` remains a sibling
target and does not become part of the primitive library.

---

## Task 2: Verify the primitives through real Win32 behavior

**Files:**

- Create: `tests/Platform/Win32PrimitivesTests.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Exercise strict conversions**

Round-trip empty, ASCII, and Japanese text through both production functions.
Require invalid UTF-8 and a lone UTF-16 surrogate to fail with a non-success
captured Win32 code. Do not use a copied conversion implementation as the
oracle.

- [ ] **Step 2: Exercise real HANDLE moves**

Create unnamed Win32 event handles. Verify default/invalid emptiness, move
construction, move assignment, `release`, `reset`, and scope closure by
querying the real handle with `GetHandleInformation`. Close a released handle
explicitly in the test so it is not leaked.

- [ ] **Step 3: Exercise error formatting without locale coupling**

Require `FormatWin32Error(ERROR_FILE_NOT_FOUND)` to return nonempty text without
asserting an English system message. For an intentionally unknown numeric
code, accept either system-provided text or a `Win32FormatError` that retains
the exact source code and a non-success formatting code.

- [ ] **Step 4: Run the focused test**

```powershell
cmake --build --preset msvc32-debug --target gc_win32_primitives_tests
ctest --preset msvc32-debug -R Win32Primitives --output-on-failure
```

---

## Task 3: Replace duplicated text conversion

**Files:**

- Modify: `src/Config/ConfigDocument.cpp`
- Modify: `src/Config/RegistryConfig.cpp`
- Modify: `src/SystemPath/SystemRoot.cpp`
- Modify: `src/SystemPath/SystemPathRouter.cpp`
- Modify: `src/SystemPath/TtxInitGuard.cpp`
- Modify: `src/Loader/DllMain.cpp`
- Modify: `src/Rfid/Feature.cpp`
- Modify: `src/Input/Win32/ControllerCatalog.cpp`
- Modify: `src/Input/Polling/InputPollingRuntime.cpp`
- Modify: `src/Audio/AudioPatch.cpp`
- Modify: `src/Audio/Asio/AsioDriver.cpp`
- Modify: `src/Audio/Asio/AsioDriverCatalog.cpp`
- Modify: `tools/ConfigGUI/Main.cpp`
- Modify: `tools/ConfigGUI/AudioBackendEditorModel.cpp`
- Modify: owning `CMakeLists.txt` files

- [ ] **Step 1: Migrate configuration and path callers**

Replace local two-pass conversion helpers in Config, RegistryConfig,
SystemRoot, SystemPathRouter, Ttx, and Loader. Preserve each caller's path,
error type, logging text, fallback text, and fatal/optional decision. The
shared helper returns mechanics only.

- [ ] **Step 2: Migrate feature diagnostics and names**

Replace copies in RFID, Input controller discovery/polling diagnostics, Audio,
ASIO driver/catalog code, and ConfigGUI. Preserve caller-specific fallback
labels, GUI dialogs, and allocation behavior outside real-time callback paths.

- [ ] **Step 3: Exclude code-page interception deliberately**

Do not rewrite `JapaneseLocaleCompatibility`'s
`MultiByteToWideChar`/`WideCharToMultiByte` detours. They implement the APIs
being intercepted and preserve game code-page semantics; calling the shared
UTF helpers there would recurse or change the contract.

- [ ] **Step 4: Prove one production implementation remains**

```powershell
rg -n 'MultiByteToWideChar|WideCharToMultiByte' src tools
```

Expected production mechanics: `Platform/Win32/Utf.cpp`, the intentional
Japanese-locale detours, and direct API type declarations needed by those
detours. Every other result requires a documented exception.

---

## Task 4: Replace duplicated Win32 error formatting

**Files:**

- Modify: `src/SystemPath/TtxInitGuard.cpp`
- Modify: every additional caller identified by Plan 01 that directly calls
  `FormatMessageW` only to render a captured error
- Modify: owning `CMakeLists.txt` files

- [ ] **Step 1: Pass captured codes, not ambient state**

At each failing Win32 call, capture `GetLastError` before logging, allocation,
cleanup, or formatting. Pass that exact code to `FormatWin32Error`.

- [ ] **Step 2: Preserve structured codes**

Keep the numeric `DWORD` in typed errors and use the formatted text only for
logs/popups. If formatting fails, the caller produces its established numeric
fallback. Do not replace structured error fields with strings.

- [ ] **Step 3: Audit direct formatting**

```powershell
rg -n 'FormatMessage[AW]?\(' src tools
```

Expected: one call in `Win32Error.cpp` plus any API-interception implementation
whose semantics cannot use the helper. Record every exception in the
baseline.

---

## Task 5: Replace ordinary HANDLE owners

**Files:**

- Modify: `src/Audio/Asio/AsioIsolatedProcess.cpp`
- Modify: `src/Rfid/CardReaderInterface.cpp`
- Modify: `src/Logging/SessionLog.h`
- Modify: `src/Logging/SessionLog.cpp`
- Modify: `src/Diagnostics/CrashDumpHandler.cpp`
- Modify: `src/Input/Win32/ControllerCatalog.cpp`
- Modify: `src/TestModeStorage/NativeStorageProbe.cpp`
- Modify: `src/Nesys/Launcher/NesysServiceLauncher.cpp`
- Modify: `tools/CardReaderTestClient/CardReaderClient.cpp`
- Modify: owning `CMakeLists.txt` files

- [ ] **Step 1: Delete local CloseHandle-only wrappers**

Replace the local `UniqueHandle` implementations in ASIO isolated-process,
RFID card-reader, and CardReaderTestClient code. Preserve whether each API
returns null or `INVALID_HANDLE_VALUE`; the common owner treats both as empty.

- [ ] **Step 2: Migrate simple stored and temporary handles**

Use `UniqueHandle` for SessionLog's file and for one-shot file/device/thread
handles in CrashDumpHandler, ControllerCatalog, NativeStorageProbe, and the
NESYS launcher. Use `release` only where ownership is demonstrably transferred
to another API/object.

- [ ] **Step 3: Preserve explicit close-failure contracts**

Where current behavior reports `CloseHandle` failure as part of a structured
result, call `release` and perform the explicit checked close at that boundary
instead of hiding the result in the destructor. RAII still covers earlier
failure paths.

---

## Task 6: Keep specialized lifetimes specialized

**Files:**

- Modify: `docs/architecture/loader-cleanup-baseline.md`

- [ ] **Step 1: Record intentional non-migrations**

Do not mechanically replace:

```text
InputPolling timer/event/thread shutdown
WASAPI render-event and COM shutdown
ASIO startup/shutdown event and thread coordination
ConfigGUI AudioOperationWorker cancellation lifecycle
named-pipe disconnect/flush ownership
process/job/thread handles with required wait/resume/terminate ordering
registry keys, sockets, COM pointers, MMCSS registrations, hook objects
intentionally process-lifetime globals
```

For each retained direct `CloseHandle`, state the extra lifecycle rule that
makes ordinary ownership insufficient.

- [ ] **Step 2: Do not wrap borrowed handles**

Hook callback parameters, handles stored by the game/NESYS process, and values
returned without ownership transfer remain raw borrowed handles.

---

## Task 7: Delete the proven shallow seams

**Files:**

- Delete: `src/Input/Win32/HidApi.h`
- Delete: `src/Input/Win32/HidApi.cpp`
- Modify: `src/Input/Win32/RawHidController.h`
- Modify: `src/Input/Win32/RawHidController.cpp`
- Modify: `src/Input/Win32/RawInputPacket.h`
- Modify: `src/Input/Win32/RawInputPacket.cpp`
- Modify: `src/Input/Polling/ForegroundPolicy.h`
- Modify: `src/Input/Polling/ForegroundPolicy.cpp`
- Modify: `src/Input/Polling/InputPollingRuntime.cpp`
- Modify: `src/Input/CMakeLists.txt`
- Modify: `src/Rfid/Runtime.h`
- Modify: `src/Rfid/Runtime.cpp`
- Delete after last caller: `src/SystemPath/StartupFatal.h`
- Delete after last caller: `src/SystemPath/StartupFatal.cpp`
- Modify: `src/SystemPath/CMakeLists.txt`
- Modify: remaining files marked `remove` in
  `docs/architecture/loader-cleanup-baseline.md`

- [ ] **Step 1: Remove `HidApi`**

Current evidence has one production table, no second production
implementation, and no test caller. Let `RawHidController` call the six Win32
HID APIs directly. Keep pure parsing/evaluation logic separate; do not replace
the table with six `std::function` members.

- [ ] **Step 2: Remove `CardWorkerApi` if the ledger confirms current evidence**

The current table has one production implementation and no test use while
`Runtime` understands all three operations (`start_detached`, key state, and
sleep). Move the production calls into `Runtime` and delete the table/default
factory. Preserve detached worker start, once flags, error publication, key
polling cadence, and exception boundaries.

- [ ] **Step 3: Remove `RawInputApi` and `ForegroundApi`**

Current evidence shows one production function in each table and no alternate
production implementation. Let `RawInputPacketBuffer` call
`GetRawInputData` directly while retaining `HidReports` as pure byte-layout
logic. Query foreground HWND/PID in the polling adapter and pass plain values
into the transition policy; do not pass a table of Win32 function pointers
through the runtime.

- [ ] **Step 4: Remove the legacy fatal action table and module**

By this plan, Plans 02 through 06 have routed terminal failures through the
common fatal-process reporter. Move any remaining DllMain, Ttx, AutoPlay
runtime, Absolute Judgement, and Widescreen runtime callers to that same
deep interface, then delete `StartupFatalActions`,
`ProductionStartupFatalActions`, `PublishStartupFatal`, and the misleading
SystemPath-owned files. Preserve once-only popup/log publication and
unconditional abort.

- [ ] **Step 5: Apply every other `remove` ledger row**

For each Plan 01 seam marked `remove`, delete its production adapter and any
synthetic machinery that exists solely for it. Update callers to the direct
production boundary or a pure decision function. Do not delete rows marked
`keep`; current expected keeps include the stateful `IWasapiApi` COM boundary
and `StartupConfigurationActions`, whose real failure paths are exercised by
production-facing configuration tests.

- [ ] **Step 6: Confirm earlier known removals stayed gone**

Require no remaining `FeatureHookLayerActions`, MinHook resolver/operation
table, Audio embedded hook operation table, feature executable-memory action
table, or public Widescreen hook-action seam.

---

## Task 8: Verify platform consolidation and commit

- [ ] **Step 1: Run the primitive and affected focused tests**

```powershell
cmake --build --preset msvc32-debug --target gc_win32_primitives_tests gc_config_contract_tests gc_config_startup_tests
ctest --preset msvc32-debug -R 'Win32Primitives|ConfigContract|ConfigStartup' --output-on-failure
cmake --build --preset msvc32-release --target gc_win32_primitives_tests gc_config_contract_tests gc_config_startup_tests
ctest --preset msvc32-release -R 'Win32Primitives|ConfigContract|ConfigStartup' --output-on-failure
```

- [ ] **Step 2: Run ownership audits**

```powershell
rg -n 'struct HidApi|ProductionHidApi|CardWorkerApi|ProductionCardWorkerApi|struct RawInputApi|struct ForegroundApi|StartupFatalActions|ProductionStartupFatalActions|PublishStartupFatal' src tests tools
rg -n 'class UniqueHandle|struct UniqueHandle|MultiByteToWideChar|WideCharToMultiByte|FormatMessage[AW]?\(' src tools
rg -n 'CloseHandle\(' src tools
```

The first command returns no matches. Classify every result from the latter
two against the shared implementation or the documented specialized-
lifecycle list.

- [ ] **Step 3: Run full static verification**

```powershell
cmake --build --preset msvc32-debug
ctest --preset msvc32-debug -j 4
cmake --build --preset msvc32-release
ctest --preset msvc32-release -j 4
git diff --check
git status --short --branch
```

These results do not establish target-process startup, Win32 detour behavior,
audio shutdown, RFID/card-reader behavior, or GUI acceptance.

- [ ] **Step 4: Commit**

```powershell
git add -- src\Platform src\Config src\SystemPath src\Loader src\Rfid src\Input src\Audio src\Logging src\Diagnostics src\TestModeStorage src\Nesys tools\ConfigGUI tools\CardReaderTestClient tests docs\architecture\loader-cleanup-baseline.md
git commit -m "Consolidate Win32 primitives and remove shallow seams"
```

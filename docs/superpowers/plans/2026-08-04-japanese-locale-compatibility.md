# Japanese Locale Compatibility Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace this game's Locale Emulator dependency with a minimal public-Win32 Japanese locale and Tokyo-time shim, propagate it into the NESYS process, remove the harmful font experiment, and collect finite pass-through filesystem evidence.

**Architecture:** Install one required transactional MinHook owner for code-page, locale, and time APIs before either executable's CRT startup. Reuse the existing targeted suspended NESYS launcher, extend the existing game `Kernel32Hooks` owner for filesystem observations, and give the NESYS process a separate best-effort diagnostic owner so no API target is detoured twice. Keep locale policy, hook plumbing, and temporary diagnostic state in focused `src/Locale` units.

**Tech Stack:** C++23, Win32 x86, MinHook, plog, CMake/CTest, MSVC x86 static runtime

## Global Constraints

- Work only in `H:\gc\artifacts\GCLoader\.worktrees\japanese-font-charset` on `fix/japanese-font-charset`.
- Treat `H:\gc\artifacts\GCLoader` as source scope and `H:\gc` as runtime/deployment scope. Do not deploy or mutate the runtime tree during implementation or static verification.
- Run all build commands from an x86 Visual Studio developer PowerShell initialized with:

  ```powershell
  & 'C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\Tools\Launch-VsDevShell.ps1' -Arch x86 -HostArch x86 -SkipAutomaticLocation
  ```

- Launch acceptance without Locale Emulator. Coexistence with Locale Emulator Core is explicitly unsupported because its private GDI hook causes the font fallback.
- Install required locale/time hooks for both game and NESYS process roles before executable CRT startup; any core install failure is transactional and fail-closed.
- Map only `CP_ACP` and `CP_THREAD_ACP` conversion tokens to 932. Preserve every explicit code page, including literal `CP_OEMCP`, UTF-8, and explicit 932.
- Return LCID `0x0411`, fixed UTC+09:00 Tokyo time with no DST, and make the observed `SetLocalTime` call a successful no-op.
- Do not hook `ntdll`, `win32u`, GDI, USER, clipboard, NLS registry access, the PEB/TEB, or generic child creation. Do not add SafetyHook or executable-image patches.
- Filesystem diagnostics are pass-through only. Preserve original arguments, output buffers, results, and `LastError`; never retry a mutation or substitute a wide result.
- Suppress normal successful ASCII filesystem traffic. Allow at most 32 unique non-ASCII events and 32 unique failure events, plus one startup and one cap line per process.
- Keep game-only input, audio, RFID/JVS, storage, and runtime-patch initialization out of the NESYS process.
- Add no runtime or ConfigGUI setting. Do not modify either Locale Emulator source tree.
- Do not add source-text/regex tests, production-table mirrors, or nominal coverage tests. Automated evidence and operator runtime acceptance remain separate.

## File Structure

- `src/Locale/JapaneseLocalePolicy.h/.cpp`: pure CP932 token mapping, Japanese constants, fixed Tokyo descriptor, and UTC-to-Tokyo conversion.
- `src/Locale/JapaneseLocaleCompatibility.h/.cpp`: required public Win32 detours, original trampolines, transaction ownership, install result, and one-shot time-write diagnostic.
- `src/Locale/FilesystemDiagnostics.h/.cpp`: shared bounded classifier, CP932 rendering, exclusions, deduplication, reentrancy guard, probes, and production log sink.
- `src/Locale/ServiceFilesystemHooks.h/.cpp`: NESYS-process-only pass-through file detours and their separate best-effort MinHook transaction.
- `src/Locale/CMakeLists.txt`: focused production targets with no dependency cycle between locale, NESYS, and game Kernel32 ownership.
- `tests/Locale/JapaneseLocalePolicyTests.cpp`: independent code-page and calendar-boundary oracle.
- `tests/Locale/JapaneseLocaleCompatibilityTests.cpp`: exact hook request surface and forwarding/last-error tests.
- `tests/Locale/FilesystemDiagnosticsTests.cpp`: bounded logging, formatting, probe, exclusion, deduplication, and reentrancy tests.
- `tests/Locale/ServiceFilesystemHookTests.cpp`: service request-set and pass-through detour tests.
- `src/Win32Hooks/Kernel32Hooks.h/.cpp`: existing game API owner, extended only at unowned ANSI pass-through branches.
- `src/Rfid/Feature.cpp`: existing game Kernel32 composition point; owns the game diagnostic state without moving unrelated feature behavior.
- `src/Nesys/NesysServiceProcess.h/.cpp`: makes the targeted NESYS launcher independent of network/registry feature settings.
- `src/Loader/DllMain.cpp`: installs required locale compatibility immediately after process-log initialization and publishes startup-fatal detail.
- `src/Font/*` and `tests/Font/*`: removed after the replacement is fully linked and tested.

---

### Task 1: Build the independently testable CP932 and Tokyo policy

**Files:**
- Create: `src/Locale/JapaneseLocalePolicy.h`
- Create: `src/Locale/JapaneseLocalePolicy.cpp`
- Create: `src/Locale/CMakeLists.txt`
- Modify: `src/CMakeLists.txt:1-14`
- Create: `tests/Locale/JapaneseLocalePolicyTests.cpp`
- Create: `tests/Locale/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt:1-12`

**Interfaces:**
- Consumes: Win32 `SYSTEMTIME`, `FILETIME`, `TIME_ZONE_INFORMATION`, `CP_ACP`, and `CP_THREAD_ACP` contracts.
- Produces: `gc::locale_compatibility::MapDefaultCodePage(UINT) noexcept`.
- Produces: `gc::locale_compatibility::TokyoTimeZoneInformation() noexcept`.
- Produces: `gc::locale_compatibility::ConvertUtcToTokyo(const SYSTEMTIME&, SYSTEMTIME*) noexcept`.
- Produces: constants `kJapaneseCodePage == 932` and `kJapaneseLcid == 0x0411`.

- [ ] **Step 1: Write and register the failing policy test**

Create `JapaneseLocalePolicyTests.cpp` with the repository's existing `Expect`/integer-failure style. Assert the exact mapping surface:

```cpp
failures += Expect(
    MapDefaultCodePage(CP_ACP) == 932,
    "CP_ACP maps to CP932");
failures += Expect(
    MapDefaultCodePage(CP_THREAD_ACP) == 932,
    "CP_THREAD_ACP maps to CP932");
failures += Expect(
    MapDefaultCodePage(CP_OEMCP) == CP_OEMCP &&
        MapDefaultCodePage(CP_UTF8) == CP_UTF8 &&
        MapDefaultCodePage(932) == 932,
    "explicit code pages pass through");
failures += Expect(
    kJapaneseCodePage == 932 && kJapaneseLcid == 0x0411,
    "Japanese constants are exact");
```

Verify the complete timezone structure, including zeroed transition dates and names:

```cpp
const auto zone = TokyoTimeZoneInformation();
failures += Expect(
    zone.Bias == -540 && zone.StandardBias == 0 &&
        zone.DaylightBias == 0 &&
        zone.StandardDate.wMonth == 0 &&
        zone.DaylightDate.wMonth == 0 &&
        std::wstring_view{zone.StandardName} == L"Tokyo Standard Time" &&
        std::wstring_view{zone.DaylightName} == L"Tokyo Standard Time",
    "Tokyo timezone is fixed UTC plus nine with no DST");
```

Use independently written expected dates for at least these conversions:

```cpp
ExpectTokyo(SYSTEMTIME{2024, 2, 0, 29, 16, 30, 0, 0},
            SYSTEMTIME{2024, 3, 0, 1, 1, 30, 0, 0});
ExpectTokyo(SYSTEMTIME{2025, 12, 0, 31, 23, 59, 59, 999},
            SYSTEMTIME{2026, 1, 0, 1, 8, 59, 59, 999});
```

Also assert a null output and an invalid input date return `false` without changing a canary output.

Register `tests/Locale` in `tests/CMakeLists.txt` and add:

```cmake
add_executable(JapaneseLocalePolicyTests
        JapaneseLocalePolicyTests.cpp)
target_link_libraries(JapaneseLocalePolicyTests PRIVATE
        gc_japanese_locale_policy)
add_test(NAME JapaneseLocalePolicyTests
        COMMAND JapaneseLocalePolicyTests)
```

- [ ] **Step 2: Configure the focused target and verify RED**

```powershell
cmake --preset msvc32-debug
cmake --build --preset msvc32-debug --target JapaneseLocalePolicyTests
```

Expected: configure or compile fails because `gc_japanese_locale_policy` and `Locale/JapaneseLocalePolicy.h` do not exist.

- [ ] **Step 3: Declare the policy surface**

Create `JapaneseLocalePolicy.h` with this exact public contract:

```cpp
#pragma once

#include <Windows.h>

namespace gc::locale_compatibility {

inline constexpr UINT kJapaneseCodePage = 932;
inline constexpr LCID kJapaneseLcid = 0x0411;

[[nodiscard]] UINT MapDefaultCodePage(UINT code_page) noexcept;
[[nodiscard]] TIME_ZONE_INFORMATION
TokyoTimeZoneInformation() noexcept;
[[nodiscard]] bool ConvertUtcToTokyo(
    const SYSTEMTIME& utc,
    SYSTEMTIME* local) noexcept;

} // namespace gc::locale_compatibility
```

- [ ] **Step 4: Implement CP mapping and the complete fixed timezone**

Implement `MapDefaultCodePage` as exactly:

```cpp
return code_page == CP_ACP || code_page == CP_THREAD_ACP
    ? kJapaneseCodePage
    : code_page;
```

Zero-initialize `TIME_ZONE_INFORMATION`, assign both names with a bounded copy, set `Bias = -540`, and leave both transition-date structures zero. Do not query the host timezone.

- [ ] **Step 5: Implement checked UTC-to-Tokyo conversion**

Use `SystemTimeToFileTime`, combine the high/low halves through `ULARGE_INTEGER`, reject addition overflow, add exactly `9LL * 60 * 60 * 10'000'000` 100-nanosecond units, and call `FileTimeToSystemTime`. Write the caller's output only after every operation succeeds so invalid input preserves the canary.

- [ ] **Step 6: Add and link the policy target**

Create `src/Locale/CMakeLists.txt`:

```cmake
add_library(gc_japanese_locale_policy STATIC
        JapaneseLocalePolicy.cpp)
target_include_directories(gc_japanese_locale_policy PUBLIC
        ${PROJECT_SOURCE_DIR}/src)
```

Add `add_subdirectory(Locale)` immediately after `add_subdirectory(Platform)` in `src/CMakeLists.txt`, so later locale targets can consume the already-declared hook infrastructure and can themselves be consumed by `Win32Hooks`. Keep the current Font subdirectory during this task; removal happens only after the replacement is complete.

- [ ] **Step 7: Build and run the policy test to verify GREEN**

```powershell
cmake --preset msvc32-debug
cmake --build --preset msvc32-debug --target JapaneseLocalePolicyTests
ctest --preset msvc32-debug -R '^JapaneseLocalePolicyTests$' --output-on-failure
```

Expected: all code-page, timezone-structure, date-boundary, invalid-input, and output-preservation assertions pass.

- [ ] **Step 8: Commit the policy**

```powershell
git add -- src/Locale/JapaneseLocalePolicy.h src/Locale/JapaneseLocalePolicy.cpp src/Locale/CMakeLists.txt src/CMakeLists.txt tests/Locale/JapaneseLocalePolicyTests.cpp tests/Locale/CMakeLists.txt tests/CMakeLists.txt
git diff --cached --check
git commit -m "Add Japanese locale policy"
```

---

### Task 2: Install the required public locale and time hooks before CRT startup

**Files:**
- Create: `src/Locale/JapaneseLocaleCompatibility.h`
- Create: `src/Locale/JapaneseLocaleCompatibility.cpp`
- Modify: `src/Locale/CMakeLists.txt`
- Create: `tests/Locale/JapaneseLocaleCompatibilityTests.cpp`
- Modify: `tests/Locale/CMakeLists.txt`
- Modify: `src/Loader/DllMain.cpp:1-215`
- Modify: `src/CMakeLists.txt:34-61`

**Interfaces:**
- Consumes: Task 1 policy functions, `gc::win32_hooks::HookRequest`, `gc::win32_hooks::MinHookTransaction`, `gc::win32_hooks::HookInstallError`, and `gc::nesys_service::ProcessRole`.
- Produces: `JapaneseLocaleHookRequests BuildJapaneseLocaleHookRequests(OriginalJapaneseLocaleApi*) noexcept` with exactly ten documented Kernel32 exports.
- Produces: forwarding seams for `GetCPInfo`, `MultiByteToWideChar`, `WideCharToMultiByte`, `GetTimeZoneInformation`, `GetLocalTime`, and `SetLocalTime` suppression.
- Produces: `std::expected<void, gc::win32_hooks::HookInstallError> InstallJapaneseLocaleCompatibility(gc::nesys_service::ProcessRole) noexcept`.

- [ ] **Step 1: Write and register the failing hook-contract test**

Create a test that builds the request set and independently checks this exact ordered surface, unique exports, non-null detours, and distinct original slots:

```cpp
constexpr std::array<std::string_view, 10> expected_exports{
    "GetACP",
    "GetOEMCP",
    "GetThreadLocale",
    "GetUserDefaultLCID",
    "GetCPInfo",
    "MultiByteToWideChar",
    "WideCharToMultiByte",
    "GetTimeZoneInformation",
    "GetLocalTime",
    "SetLocalTime",
};

OriginalJapaneseLocaleApi originals{};
const auto requests = BuildJapaneseLocaleHookRequests(&originals);
failures += Expect(
    requests.size() == expected_exports.size(),
    "required public locale hook count is exact");
```

The uniqueness check must compare the observable `export_name` values; do not grep source text.

- [ ] **Step 2: Add failing forwarding and last-error tests**

Use capturing Win32-compatible fake originals. For `GetCPInfo`, require CP932 for default tokens, the same output pointer, the fake's return value, and its `ERROR_INSUFFICIENT_BUFFER`:

```cpp
SetLastError(ERROR_SUCCESS);
const BOOL result = detail::InvokeGetCPInfo(
    CP_ACP,
    &info,
    CaptureGetCPInfo);
failures += Expect(
    result == FALSE && capture.code_page == 932 &&
        capture.info == &info &&
        GetLastError() == ERROR_INSUFFICIENT_BUFFER,
    "GetCPInfo maps only the code-page token");
```

For both conversion functions, fill every flag, pointer, length, default-character, and used-default-character field with distinct canaries. Test `CP_ACP`, `CP_THREAD_ACP`, `CP_OEMCP`, `CP_UTF8`, and explicit 932. The fake changes last error; require the unchanged fake return and error.

For timezone and local time:

```cpp
SetLastError(ERROR_ACCESS_DENIED);
TIME_ZONE_INFORMATION zone{};
const auto zone_id = detail::InvokeGetTimeZoneInformation(&zone);
failures += Expect(
    zone_id == TIME_ZONE_ID_UNKNOWN && zone.Bias == -540 &&
        GetLastError() == ERROR_ACCESS_DENIED,
    "timezone hook returns fixed Tokyo and preserves last error");

SYSTEMTIME local{};
g_fake_utc = SYSTEMTIME{2025, 12, 0, 31, 23, 0, 0, 0};
SetLastError(ERROR_INVALID_DATA);
detail::InvokeGetLocalTime(
    &local,
    CaptureGetSystemTime,
    CaptureFallbackGetLocalTime);
failures += Expect(
    local.wYear == 2026 && local.wMonth == 1 && local.wDay == 1 &&
        local.wHour == 8 && GetLastError() == ERROR_INVALID_DATA,
    "GetLocalTime derives Tokyo from UTC and preserves last error");
```

Call `detail::SuppressSetLocalTime` twice with one `std::atomic_bool` latch and a capturing observer. Require `TRUE` twice, zero operating-system setter calls by construction, one observer call, and unchanged last error.

- [ ] **Step 3: Build the new target and verify RED**

Register `JapaneseLocaleCompatibilityTests` against a not-yet-existing `gc_japanese_locale_compatibility` target:

```cmake
add_executable(JapaneseLocaleCompatibilityTests
        JapaneseLocaleCompatibilityTests.cpp)
target_link_libraries(JapaneseLocaleCompatibilityTests PRIVATE
        gc_japanese_locale_compatibility)
add_test(NAME JapaneseLocaleCompatibilityTests
        COMMAND JapaneseLocaleCompatibilityTests)
```

Then run:

```powershell
cmake --preset msvc32-debug
cmake --build --preset msvc32-debug --target JapaneseLocaleCompatibilityTests
```

Expected: compile or link failure because the compatibility surface is absent.

- [ ] **Step 4: Declare the original API and test seam types**

Declare Win32-compatible aliases and one original table, including:

```cpp
using GetCPInfoApi = BOOL(WINAPI*)(UINT, LPCPINFO);
using MultiByteToWideCharApi = int(WINAPI*)(
    UINT, DWORD, LPCCH, int, LPWSTR, int);
using WideCharToMultiByteApi = int(WINAPI*)(
    UINT, DWORD, LPCWCH, int, LPSTR, int, LPCCH, LPBOOL);
using GetSystemTimeApi = void(WINAPI*)(LPSYSTEMTIME);
using GetLocalTimeApi = void(WINAPI*)(LPSYSTEMTIME);
using SetLocalTimeObserver = void(*)() noexcept;

struct OriginalJapaneseLocaleApi {
    decltype(&::GetACP) get_acp{};
    decltype(&::GetOEMCP) get_oem_cp{};
    decltype(&::GetThreadLocale) get_thread_locale{};
    decltype(&::GetUserDefaultLCID) get_user_default_lcid{};
    GetCPInfoApi get_cp_info{};
    MultiByteToWideCharApi multi_byte_to_wide_char{};
    WideCharToMultiByteApi wide_char_to_multi_byte{};
    decltype(&::GetTimeZoneInformation) get_time_zone_information{};
    decltype(&::GetLocalTime) get_local_time{};
    decltype(&::SetLocalTime) set_local_time{};
};

inline constexpr std::size_t kJapaneseLocaleHookCount = 10;
using JapaneseLocaleHookRequests = std::array<
    gc::win32_hooks::HookRequest,
    kJapaneseLocaleHookCount>;

[[nodiscard]] JapaneseLocaleHookRequests
BuildJapaneseLocaleHookRequests(
    OriginalJapaneseLocaleApi* originals) noexcept;

namespace detail {

[[nodiscard]] BOOL InvokeGetCPInfo(
    UINT code_page,
    LPCPINFO info,
    GetCPInfoApi original) noexcept;
[[nodiscard]] int InvokeMultiByteToWideChar(
    UINT code_page,
    DWORD flags,
    LPCCH source,
    int source_size,
    LPWSTR destination,
    int destination_size,
    MultiByteToWideCharApi original) noexcept;
[[nodiscard]] int InvokeWideCharToMultiByte(
    UINT code_page,
    DWORD flags,
    LPCWCH source,
    int source_size,
    LPSTR destination,
    int destination_size,
    LPCCH default_character,
    LPBOOL used_default_character,
    WideCharToMultiByteApi original) noexcept;
[[nodiscard]] DWORD InvokeGetTimeZoneInformation(
    LPTIME_ZONE_INFORMATION information) noexcept;
void InvokeGetLocalTime(
    LPSYSTEMTIME local,
    GetSystemTimeApi get_system_time,
    GetLocalTimeApi fallback) noexcept;
[[nodiscard]] BOOL SuppressSetLocalTime(
    std::atomic_bool& notification_latch,
    SetLocalTimeObserver observer) noexcept;

} // namespace detail
```

Expose only the request builder and forwarding helpers under `detail`; keep production trampolines and transaction state private to the `.cpp`.

- [ ] **Step 5: Implement exact forwarding helpers**

`InvokeGetCPInfo` and both conversion helpers call `MapDefaultCodePage` once, then call their original with every other argument untouched. They do no logging, allocation, or extra Win32 work.

`InvokeGetTimeZoneInformation` captures incoming last error, assigns the complete Task 1 descriptor, restores the error, and returns `TIME_ZONE_ID_UNKNOWN`. `InvokeGetLocalTime` captures last error, calls the injected `GetSystemTime`, converts that UTC value, writes the caller output, and restores last error. Give it the original `GetLocalTime` trampoline as a second injected callback; if conversion unexpectedly rejects the current UTC value, call that trampoline once so the `void` API never returns an uninitialized structure. Add a test with an invalid fake UTC value that proves this fallback and last-error preservation. `SuppressSetLocalTime` preserves last error, atomically calls the observer once, and returns `TRUE` without accepting an original setter callback.

- [ ] **Step 6: Implement the ten production detours and request builder**

Constant detours return `932`, `932`, `0x0411`, and `0x0411` without touching last error. The other detours delegate to the tested helpers. `SetLocalTime` ignores only the observed input value, invokes a no-throw once-only plog observer, and never calls its stored trampoline.

Build all requests against `kernel32.dll`. Do not add `GetLocaleInfoA/W`, `GetSystemDefaultLCID`, `GetDynamicTimeZoneInformation`, or any `Nt*` target.

- [ ] **Step 7: Implement transactional installation**

Own one process-lifetime `std::unique_ptr<MinHookTransaction>` and one original table. The public installer:

```cpp
[[nodiscard]] std::expected<void, gc::win32_hooks::HookInstallError>
InstallJapaneseLocaleCompatibility(
    gc::nesys_service::ProcessRole role) noexcept;
```

must return success on a repeated call, install the complete ten-request span atomically, clear every original slot after failure, catch all exceptions, and log one success line containing role, `acp=932`, `lcid=0x0411`, and `utc_offset_minutes=540`. Return the exact `gc::win32_hooks::HookInstallError` from `MinHookTransaction::Install`; do not downgrade it to `bool`.

- [ ] **Step 8: Link the compatibility target and wire fail-closed startup**

Add:

```cmake
add_library(gc_japanese_locale_compatibility STATIC
        JapaneseLocaleCompatibility.cpp)
target_include_directories(gc_japanese_locale_compatibility PUBLIC
        ${PROJECT_SOURCE_DIR}/src
        ${minhook_SOURCE_DIR}/include
        ${plog_SOURCE_DIR}/include)
target_link_libraries(gc_japanese_locale_compatibility PUBLIC
        gc_hooking
        gc_japanese_locale_policy
        gc_nesys_process)
```

Link `iDmacDrv32` to this target. In `DllMain`, call the installer immediately after `InitProcessLog(role)` and before crash-dump/config initialization:

```cpp
const auto locale =
    gc::locale_compatibility::InstallJapaneseLocaleCompatibility(role);
if (!locale) {
    PublishJapaneseLocaleCompatibilityFatal(locale.error());
    return FALSE;
}
```

Add a no-throw fatal formatter using `HookInstallStageName`, export name, Win32 error, and MinHook status. Publish title `GCLoader Japanese locale setup error` with a new dedicated exit code `25`. Keep the old font diagnostic call temporarily so this task isolates locale startup behavior; Task 7 removes it.

- [ ] **Step 9: Run focused tests and build the DLL to verify GREEN**

```powershell
cmake --preset msvc32-debug
cmake --build --preset msvc32-debug --target JapaneseLocalePolicyTests JapaneseLocaleCompatibilityTests iDmacDrv32
ctest --preset msvc32-debug -R '^(JapaneseLocalePolicyTests|JapaneseLocaleCompatibilityTests)$' --output-on-failure
```

Expected: exact request, forwarding, last-error, time, and once-only suppression tests pass, and the x86 DLL links with locale installation before `ConfigManager`.

- [ ] **Step 10: Commit required locale/time hooks**

```powershell
git add -- src/Locale/JapaneseLocaleCompatibility.h src/Locale/JapaneseLocaleCompatibility.cpp src/Locale/CMakeLists.txt tests/Locale/JapaneseLocaleCompatibilityTests.cpp tests/Locale/CMakeLists.txt src/Loader/DllMain.cpp src/CMakeLists.txt
git diff --cached --check
git commit -m "Install Japanese locale API hooks"
```

---

### Task 3: Make targeted NESYS injection independent of optional NESYS features

**Files:**
- Modify: `src/Nesys/NesysServiceProcess.cpp:181-214`
- Test: `tests/Nesys/NesysServicePatchTests.cpp:279-324`

**Interfaces:**
- Consumes: existing `ResolveNesysFeaturePlan(ProcessRole, bool, bool)`, exact `NesysService.exe -app` predicate, and suspended-child injector.
- Produces: a game-role plan with `service_launcher=true` and one API hook even when network and registry virtualization are both disabled.
- Preserves: service-role feature counts, unrelated child pass-through, caller-requested suspension, and fail-closed injection behavior.

- [ ] **Step 1: Change only the all-disabled game expectation and verify RED**

Update the first game plan assertion to:

```cpp
failures += expect_plan(
    ResolveNesysFeaturePlan(ProcessRole::Game, false, false),
    NesysFeaturePlan{
        true, false, false, false, false, false, true, false, 1},
    "game locale-only launcher");
```

Leave the other seven expected plans unchanged. They already include the game launcher when another NESYS policy is enabled and must catch accidental count drift.

- [ ] **Step 2: Run the NESYS plan test and verify RED**

```powershell
cmake --build --preset msvc32-debug --target NesysServicePatchTests
ctest --preset msvc32-debug -R '^NesysServicePatchTests$' --output-on-failure
```

Expected: only `game locale-only launcher` fails because the current plan disables every hook.

- [ ] **Step 3: Make game launcher enablement unconditional**

Initialize the plan's enabled state as:

```cpp
plan.enabled = network_enabled || registry_enabled ||
    role == ProcessRole::Game;
```

Retain the existing final block that assigns `service_launcher` only for the game role and increments `api_hook_count` once. Do not change service-role `ExitProcess` diagnostics or any network/registry count.

- [ ] **Step 4: Run the complete NESYS focused tests and DLL build**

```powershell
cmake --build --preset msvc32-debug --target NesysServicePatchTests NesysHookTransactionTests iDmacDrv32
ctest --preset msvc32-debug -R '^(NesysServicePatchTests|NesysHookTransactionTests)$' --output-on-failure
```

Expected: all plan combinations pass; the existing tests continue proving only `NesysService.exe -app` is intercepted, unrelated children pass through, and resume/injection failures fail closed.

- [ ] **Step 5: Commit targeted propagation**

```powershell
git add -- src/Nesys/NesysServiceProcess.cpp tests/Nesys/NesysServicePatchTests.cpp
git diff --cached --check
git commit -m "Always inject locale shim into NESYS"
```

---

### Task 4: Build bounded, deduplicated filesystem observation

**Files:**
- Create: `src/Locale/FilesystemDiagnostics.h`
- Create: `src/Locale/FilesystemDiagnostics.cpp`
- Modify: `src/Locale/CMakeLists.txt`
- Create: `tests/Locale/FilesystemDiagnosticsTests.cpp`
- Modify: `tests/Locale/CMakeLists.txt`

**Interfaces:**
- Consumes: raw ANSI path pointers after an original call, original success/error, explicit CP932 conversion, and optional read-only probe callbacks.
- Produces: `FilesystemDiagnostics::Start(std::span<const AnsiFilesystemApi>) noexcept` and `FilesystemDiagnostics::Observe(const AnsiFilesystemObservation&) noexcept`.
- Produces: one shared production classifier used by game and NESYS hook owners without owning either hook transaction.

- [ ] **Step 1: Write the failing classification and exclusion tests**

Declare the intended public surface in the test:

```cpp
enum class FilesystemDiagnosticRole { game, service };
enum class AnsiFilesystemApi {
    create_file,
    get_file_attributes,
    find_first_file,
    find_next_file,
    create_directory,
    delete_file,
    move_file,
    copy_file,
};
enum class WideProbeOutcome {
    not_run,
    invalid_cp932,
    exists,
    missing,
    inaccessible,
};

inline constexpr std::array<AnsiFilesystemApi, 8>
    kObservedAnsiFilesystemApis{
        AnsiFilesystemApi::create_file,
        AnsiFilesystemApi::get_file_attributes,
        AnsiFilesystemApi::find_first_file,
        AnsiFilesystemApi::find_next_file,
        AnsiFilesystemApi::create_directory,
        AnsiFilesystemApi::delete_file,
        AnsiFilesystemApi::move_file,
        AnsiFilesystemApi::copy_file,
    };

struct AnsiFilesystemObservation {
    AnsiFilesystemApi api{};
    LPCSTR first_path{};
    LPCSTR second_path{};
    bool succeeded{};
    DWORD last_error{ERROR_SUCCESS};
};
```

Use an injected sink and assert:

- one `Start(kObservedAnsiFilesystemApis)` call emits exactly one line with the
  role, all eight API names, `non_ascii_capacity=32`, and
  `failure_capacity=32`;
- successful `data\image.dds` emits nothing;
- failed `data\missing.dat` emits one `class=failure` record;
- successful `data\x89\xBC_start.dds` emits one `class=non_ascii` record;
- `ERROR_NO_MORE_FILES` for `find_next_file` emits nothing;
- `COM2`, `\\.\pipe\nesys_games`, `loader-log.txt`, and `loader-service-log.txt` emit nothing even on failure.

Use adjacent C++ string literals for CP932 bytes so hexadecimal escapes cannot consume later characters:

```cpp
constexpr char japanese_path[] =
    "data\\" "\x89\xBC" "_start.dds";
```

- [ ] **Step 2: Add failing formatting, probe, and `LastError` tests**

The injected probe must receive `L"data\\仮_start.dds"` for the CP932 bytes above. Require the emitted message to contain bounded escaped bytes, the UTF-8 decoded path, `probe=exists`, the API name, result class, and original error. Set a different last error in both probe and sink; require `Observe` to restore its incoming last error.

Pass a path longer than 192 bytes and require `truncated=true`, no more than 192 rendered input bytes, and a stable hash/dedup identity based on the bounded inspection window. Invalid CP932 must report `probe=invalid_cp932` without attempting a wide probe.

- [ ] **Step 3: Add failing deduplication, cap, and reentrancy tests**

Exercise these exact invariants:

```cpp
diagnostics.Observe(same_failure);
diagnostics.Observe(same_failure);
failures += Expect(sink.event_lines == 1,
                   "duplicate event logs once");

for (int i = 0; i < 40; ++i) {
    diagnostics.Observe(UniqueAsciiFailure(i));
}
for (int i = 0; i < 40; ++i) {
    diagnostics.Observe(UniqueNonAsciiEvent(i));
}
failures += Expect(
    sink.failure_lines == 32 && sink.non_ascii_lines == 32 &&
        sink.cap_lines == 1,
    "independent fixed budgets emit one total cap line");
```

Have the sink call `Observe` recursively once. Require only the outer event and unchanged last error, proving the thread-local guard covers probing and logging.

- [ ] **Step 4: Register the test and verify RED**

Add `FilesystemDiagnosticsTests` linked to a not-yet-existing `gc_locale_filesystem_diagnostics`:

```cmake
add_executable(FilesystemDiagnosticsTests
        FilesystemDiagnosticsTests.cpp)
target_link_libraries(FilesystemDiagnosticsTests PRIVATE
        gc_locale_filesystem_diagnostics)
add_test(NAME FilesystemDiagnosticsTests
        COMMAND FilesystemDiagnosticsTests)
```

Then run:

```powershell
cmake --preset msvc32-debug
cmake --build --preset msvc32-debug --target FilesystemDiagnosticsTests
```

Expected: configure or compile fails because the diagnostic unit is absent.

- [ ] **Step 5: Declare the bounded diagnostic owner and actions**

Create this public shape:

```cpp
inline constexpr std::size_t kFilesystemCategoryCapacity = 32;
inline constexpr std::size_t kFilesystemRenderedPathLimit = 192;
inline constexpr std::size_t kFilesystemInspectionLimit = 4096;

struct FilesystemDiagnosticActions {
    void* context{};
    WideProbeOutcome (*probe)(
        void*,
        AnsiFilesystemApi,
        std::wstring_view,
        std::wstring_view) noexcept{};
    void (*emit)(void*, std::string_view) noexcept{};
};

[[nodiscard]] FilesystemDiagnosticActions
ProductionFilesystemDiagnosticActions() noexcept;

class FilesystemDiagnostics {
public:
    FilesystemDiagnostics(
        FilesystemDiagnosticRole role,
        FilesystemDiagnosticActions actions) noexcept;
    void Start(std::span<const AnsiFilesystemApi> apis) noexcept;
    void Observe(const AnsiFilesystemObservation& observation) noexcept;

private:
    std::array<std::atomic_uint64_t, 32> non_ascii_{};
    std::array<std::atomic_uint64_t, 32> failures_{};
    std::atomic_bool started_{};
    std::atomic_bool cap_logged_{};
    FilesystemDiagnosticRole role_{};
    FilesystemDiagnosticActions actions_{};
};
```

Delete copy/move operations. A process owns one instance; fixed atomic tables avoid allocator and lock dependencies on the hot path.

- [ ] **Step 6: Implement eligibility, deduplication, and exclusions**

Inspect at most 4096 bytes per input/returned name. Classify a high-byte event first; otherwise classify a regular-file failure. A non-ASCII failure consumes only the non-ASCII table. Hash role, API, class, all bounded first/second path bytes, success, error, and probe outcome with a deterministic 64-bit hash; reserve zero as an empty slot.

Use case-insensitive ASCII checks for device/pipe/COM/log exclusions. Exclude `\\.\`, `\\?\pipe\`, and `\Device\` prefixes; `COM` followed only by one or more decimal digits; and either loader-log basename. Do not exclude ordinary extended file paths such as `\\?\C:\data\...`. Treat `find_next_file + ERROR_NO_MORE_FILES` as expected. When insertion finds no empty slot, emit exactly `filesystem diagnostic category cap reached; additional events in capped categories suppressed` through `cap_logged_.exchange(true)` and suppress only events in that full category.

- [ ] **Step 7: Implement CP932 rendering, read-only probes, and reentrancy**

Use explicit `MultiByteToWideChar(932, MB_ERR_INVALID_CHARS, ...)`; never use `CP_ACP`. Render at most 192 raw bytes as escaped/hex-safe text and convert the decoded wide path to UTF-8 with explicit `CP_UTF8`. Include `truncated=true` when the rendering limit is reached.

Set a thread-local guard before conversion, probe, or sink calls. A nested `Observe` returns immediately. Capture `GetLastError()` on entry and restore it on every exit path, including exceptions. Production probe behavior is read-only:

- `create_file` and `get_file_attributes`: `GetFileAttributesW`;
- `find_first_file`: `FindFirstFileW` followed immediately by `FindClose` on success;
- `create_directory`, `delete_file`, `move_file`, and `copy_file`: source/destination `GetFileAttributesW` only;
- `find_next_file`: no probe.

The production sink emits to plog inside `try/catch`; it never passes raw ANSI bytes directly to the stream.

- [ ] **Step 8: Add the diagnostics target and verify GREEN**

Add:

```cmake
add_library(gc_locale_filesystem_diagnostics STATIC
        FilesystemDiagnostics.cpp)
target_include_directories(gc_locale_filesystem_diagnostics PUBLIC
        ${PROJECT_SOURCE_DIR}/src
        ${plog_SOURCE_DIR}/include)
```

Run:

```powershell
cmake --preset msvc32-debug
cmake --build --preset msvc32-debug --target FilesystemDiagnosticsTests
ctest --preset msvc32-debug -R '^FilesystemDiagnosticsTests$' --output-on-failure
```

Expected: classification, CP932 decoding, output bounds, probe outcomes, independent caps, one cap line, exclusion, reentrancy, and last-error tests pass.

- [ ] **Step 9: Commit the diagnostic core**

```powershell
git add -- src/Locale/FilesystemDiagnostics.h src/Locale/FilesystemDiagnostics.cpp src/Locale/CMakeLists.txt tests/Locale/FilesystemDiagnosticsTests.cpp tests/Locale/CMakeLists.txt
git diff --cached --check
git commit -m "Add bounded filesystem diagnostics"
```

---

### Task 5: Observe game ANSI filesystem pass-through in the existing Kernel32 owner

**Files:**
- Modify: `src/Win32Hooks/Kernel32Hooks.h:13-158`
- Modify: `src/Win32Hooks/Kernel32Hooks.cpp:52-194,523-754`
- Modify: `src/Win32Hooks/CMakeLists.txt`
- Modify: `src/Rfid/Feature.cpp:1-45,185-220`
- Test: `tests/Win32Hooks/Kernel32HookTests.cpp:393-680,918-1030,1780-1800`

**Interfaces:**
- Consumes: Task 4 `FilesystemDiagnostics`, existing RFID/storage/system routing, and existing `OriginalKernel32Api` forwarding table.
- Produces: optional `FilesystemDiagnostics*` constructor dependency at the end of the existing `Kernel32Hooks` signature.
- Produces: game-owned `FindNextFileA` and `CopyFileA` pass-through detours.
- Preserves: all existing route precedence; only otherwise-unowned ANSI calls are observed.

- [ ] **Step 1: Write failing request-union tests with diagnostics enabled**

Keep all existing no-diagnostics request counts unchanged. Construct one `FilesystemDiagnostics` with a capturing sink and pass its pointer as the fifth constructor argument. Require:

```cpp
failures += expect(
    diagnostic_only.BuildRequests().requests().size() == 21,
    "RFID plus diagnostic request count");
failures += expect(
    diagnostic_both.BuildRequests().requests().size() == 28,
    "combined route and diagnostic union count");
```

The diagnostic union must contain these ANSI names exactly once: `CreateFileA`, `GetFileAttributesA`, `FindFirstFileA`, `FindNextFileA`, `CreateDirectoryA`, `DeleteFileA`, `MoveFileA`, and `CopyFileA`. Keep `kMaxOwnedKernel32Hooks == 32` and the existing uniqueness check.

- [ ] **Step 2: Write failing pass-through observation tests**

Extend `OriginalFake` and `OriginalApi()` with `FindNextFileA` and `CopyFileA`. Test:

- unmatched `CreateFileA` failure emits the raw caller path, result failure, exact original error, and returns the original handle sentinel;
- a matching test-mode/system route emits no locale-filesystem event because another policy owns it;
- successful `FindNextFileA` with `cFileName = "\x89\xBC_start.dds"` emits one non-ASCII event;
- failed `FindNextFileA` with `ERROR_NO_MORE_FILES` emits nothing;
- `CopyFileA(source, destination, TRUE)` forwards both pointers and the flag exactly, returns the original `BOOL`, observes only afterward, and restores the original error.

Set the sink and probe to deliberately change last error so each assertion proves restoration at the public method boundary.

- [ ] **Step 3: Build `Kernel32HookTests` and verify RED**

```powershell
cmake --build --preset msvc32-debug --target Kernel32HookTests
```

Expected: compile failures for the new constructor dependency and missing original API fields/methods.

- [ ] **Step 4: Extend the original table, public methods, and detours**

Append the optional dependency to preserve all existing test call sites:

```cpp
Kernel32Hooks(
    gc::rfid::Runtime& rfid,
    gc::testmode_storage::Hooks& storage,
    gc::system_path::SystemPathRouter& system,
    OriginalKernel32Api originals = {},
    gc::locale_compatibility::FilesystemDiagnostics*
        filesystem_diagnostics = nullptr) noexcept;
```

Add exact `FindNextFileA` and `CopyFileA` original types, methods, and static detours. Add diagnostic-only request conditions without duplicating an export already selected by storage/system routing. The maximum combined set is 28, below the existing capacity 32.

- [ ] **Step 5: Observe only original ANSI pass-through results**

For every listed A method, leave COM2, storage, and system-path branches unchanged. Wrap only the final original-A call:

```cpp
const auto result = originals_.create_file_a(
    file_name,
    desired_access,
    share_mode,
    security_attributes,
    creation_disposition,
    flags_and_attributes,
    template_file);
const auto error = GetLastError();
if (filesystem_diagnostics_ != nullptr) {
    filesystem_diagnostics_->Observe({
        .api = AnsiFilesystemApi::create_file,
        .first_path = file_name,
        .succeeded = result != INVALID_HANDLE_VALUE,
        .last_error = error,
    });
}
SetLastError(error);
return result;
```

Use the corresponding success sentinel for each API. For `FindNextFileA`, pass `find_data->cFileName` only after a successful original call and tolerate a null output pointer without diagnostic dereference. For two-path APIs, populate both path fields.

- [ ] **Step 6: Compose one game diagnostic owner in `FeatureState`**

Add `FilesystemDiagnostics filesystem_diagnostics` before `Kernel32Hooks` in member order, construct it with role `game` and `ProductionFilesystemDiagnosticActions()`, and pass its address to `Kernel32Hooks`. After the game Kernel32 transaction commits successfully, call `Start(kObservedAnsiFilesystemApis)` so the startup line cannot claim hooks before installation.

Do not move `Kernel32Hooks` out of the existing feature transaction and do not initialize this state in the NESYS process.

- [ ] **Step 7: Link the shared diagnostic target and verify GREEN**

Link `gc_win32_hooks` publicly to `gc_locale_filesystem_diagnostics`, then run:

```powershell
cmake --preset msvc32-debug
cmake --build --preset msvc32-debug --target FilesystemDiagnosticsTests Kernel32HookTests iDmacDrv32
ctest --preset msvc32-debug -R '^(FilesystemDiagnosticsTests|Kernel32HookTests)$' --output-on-failure
```

Expected: old route tests remain green; new request count, route exclusion, A-result observation, FindNext completion suppression, CopyFile forwarding, and last-error assertions pass.

- [ ] **Step 8: Commit game filesystem observation**

```powershell
git add -- src/Win32Hooks/Kernel32Hooks.h src/Win32Hooks/Kernel32Hooks.cpp src/Win32Hooks/CMakeLists.txt src/Rfid/Feature.cpp tests/Win32Hooks/Kernel32HookTests.cpp
git diff --cached --check
git commit -m "Trace game ANSI filesystem calls"
```

---

### Task 6: Add a separate best-effort filesystem observer in the NESYS process

**Files:**
- Create: `src/Locale/ServiceFilesystemHooks.h`
- Create: `src/Locale/ServiceFilesystemHooks.cpp`
- Modify: `src/Locale/JapaneseLocaleCompatibility.cpp`
- Modify: `src/Locale/CMakeLists.txt`
- Create: `tests/Locale/ServiceFilesystemHookTests.cpp`
- Modify: `tests/Locale/CMakeLists.txt`

**Interfaces:**
- Consumes: Task 4 diagnostics, the existing `MinHookTransaction`, and `ProcessRole::Service` after the required locale transaction commits.
- Produces: `ServiceFilesystemHookRequests BuildServiceFilesystemHookRequests(OriginalServiceFilesystemApi*) noexcept` with the exact eight ANSI APIs.
- Produces: `std::expected<void, gc::win32_hooks::HookInstallError> InstallServiceFilesystemDiagnostics() noexcept` with independent transaction ownership.
- Preserves: required locale success if this temporary diagnostic transaction cannot install.

- [ ] **Step 1: Write and register the failing service request-set test**

Require the exact ordered export array:

```cpp
constexpr std::array<std::string_view, 8> expected_exports{
    "CreateFileA",
    "GetFileAttributesA",
    "FindFirstFileA",
    "FindNextFileA",
    "CreateDirectoryA",
    "DeleteFileA",
    "MoveFileA",
    "CopyFileA",
};
```

Assert eight unique `kernel32.dll` requests, non-null detours, and distinct original slots. Also assert the public installer returns a structured `gc::win32_hooks::HookInstallError`, not `bool`.

Declare the builder type explicitly:

```cpp
struct OriginalServiceFilesystemApi;

inline constexpr std::size_t kServiceFilesystemHookCount = 8;
using ServiceFilesystemHookRequests = std::array<
    gc::win32_hooks::HookRequest,
    kServiceFilesystemHookCount>;

[[nodiscard]] ServiceFilesystemHookRequests
BuildServiceFilesystemHookRequests(
    OriginalServiceFilesystemApi* originals) noexcept;

[[nodiscard]] std::expected<
    void,
    gc::win32_hooks::HookInstallError>
InstallServiceFilesystemDiagnostics() noexcept;
```

- [ ] **Step 2: Write failing service forwarding tests**

Use capturing fake originals and a `FilesystemDiagnostics` sink to exercise the three signature families:

- `CreateFileA`: seven arguments, handle result, and last error;
- `FindFirstFileA`/`FindNextFileA`: pattern/output pointers, returned filename, failure sentinel, and `ERROR_NO_MORE_FILES` suppression;
- `MoveFileA`/`CopyFileA`: both paths, `fail_if_exists`, `BOOL` result, and last error.

Call the exposed `detail::Invoke*` seams rather than global detours. Require the original call precedes observation by recording an operation sequence `{original, probe, emit}`.

- [ ] **Step 3: Build the service test and verify RED**

Register the target in `tests/Locale/CMakeLists.txt` as:

```cmake
add_executable(ServiceFilesystemHookTests
        ServiceFilesystemHookTests.cpp)
target_link_libraries(ServiceFilesystemHookTests PRIVATE
        gc_japanese_locale_compatibility)
add_test(NAME ServiceFilesystemHookTests
        COMMAND ServiceFilesystemHookTests)
```

```powershell
cmake --preset msvc32-debug
cmake --build --preset msvc32-debug --target ServiceFilesystemHookTests
```

Expected: compile or link failure because the service hook unit does not exist.

- [ ] **Step 4: Implement the original table and transparent detour seams**

Declare exact Win32 function-pointer fields for all eight APIs:

```cpp
struct OriginalServiceFilesystemApi {
    decltype(&::CreateFileA) create_file_a{};
    decltype(&::GetFileAttributesA) get_file_attributes_a{};
    decltype(&::FindFirstFileA) find_first_file_a{};
    decltype(&::FindNextFileA) find_next_file_a{};
    decltype(&::CreateDirectoryA) create_directory_a{};
    decltype(&::DeleteFileA) delete_file_a{};
    decltype(&::MoveFileA) move_file_a{};
    decltype(&::CopyFileA) copy_file_a{};
};
```

Expose these exact test seams under `detail`:

```cpp
HANDLE InvokeCreateFileA(
    LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES,
    DWORD, DWORD, HANDLE,
    decltype(&::CreateFileA), FilesystemDiagnostics*) noexcept;
DWORD InvokeGetFileAttributesA(
    LPCSTR, decltype(&::GetFileAttributesA),
    FilesystemDiagnostics*) noexcept;
HANDLE InvokeFindFirstFileA(
    LPCSTR, LPWIN32_FIND_DATAA, decltype(&::FindFirstFileA),
    FilesystemDiagnostics*) noexcept;
BOOL InvokeFindNextFileA(
    HANDLE, LPWIN32_FIND_DATAA, decltype(&::FindNextFileA),
    FilesystemDiagnostics*) noexcept;
BOOL InvokeCreateDirectoryA(
    LPCSTR, LPSECURITY_ATTRIBUTES, decltype(&::CreateDirectoryA),
    FilesystemDiagnostics*) noexcept;
BOOL InvokeDeleteFileA(
    LPCSTR, decltype(&::DeleteFileA),
    FilesystemDiagnostics*) noexcept;
BOOL InvokeMoveFileA(
    LPCSTR, LPCSTR, decltype(&::MoveFileA),
    FilesystemDiagnostics*) noexcept;
BOOL InvokeCopyFileA(
    LPCSTR, LPCSTR, BOOL, decltype(&::CopyFileA),
    FilesystemDiagnostics*) noexcept;
```

Each helper must:

1. call the original with identical parameters;
2. capture `GetLastError()` immediately;
3. submit one `AnsiFilesystemObservation` using the correct success sentinel and returned filename;
4. restore the captured error; and
5. return the original result.

Null original pointers are installation defects and are not a runtime fallback; no helper fabricates success.

- [ ] **Step 5: Install the service hooks in their own transaction**

Own a separate process-lifetime `MinHookTransaction`, original table, and service-role `FilesystemDiagnostics`. Resolve and install the exact request span atomically. Call `diagnostics.Start` only after commit. On failure, clear every original slot and return the exact transaction error.

The read-only probe and sink come from `ProductionFilesystemDiagnosticActions`; no game RFID/storage/system routing is linked into this target.

- [ ] **Step 6: Invoke diagnostics after required service locale installation**

At the end of successful `InstallJapaneseLocaleCompatibility(role)`, add:

```cpp
if (role == gc::nesys_service::ProcessRole::Service) {
    const auto diagnostics = InstallServiceFilesystemDiagnostics();
    if (!diagnostics) {
        LogServiceFilesystemDiagnosticInstallFailure(
            diagnostics.error());
    }
}
```

The warning must contain install stage, export, Win32 error, and MinHook status exactly once. Do not return the diagnostic error and do not roll back the already-committed required locale transaction.

- [ ] **Step 7: Link and run all locale/NESYS focused tests**

Add `ServiceFilesystemHooks.cpp` to `gc_japanese_locale_compatibility`, link that target to `gc_locale_filesystem_diagnostics`, and register the new test. Run:

```powershell
cmake --preset msvc32-debug
cmake --build --preset msvc32-debug --target JapaneseLocaleCompatibilityTests FilesystemDiagnosticsTests ServiceFilesystemHookTests NesysServicePatchTests iDmacDrv32
ctest --preset msvc32-debug -R '^(JapaneseLocaleCompatibilityTests|FilesystemDiagnosticsTests|ServiceFilesystemHookTests|NesysServicePatchTests)$' --output-on-failure
```

Expected: service request identity, forwarding order, output/return/last-error preservation, bounded diagnostic behavior, role selection, and targeted launcher tests pass.

- [ ] **Step 8: Commit NESYS filesystem observation**

```powershell
git add -- src/Locale/ServiceFilesystemHooks.h src/Locale/ServiceFilesystemHooks.cpp src/Locale/JapaneseLocaleCompatibility.cpp src/Locale/CMakeLists.txt tests/Locale/ServiceFilesystemHookTests.cpp tests/Locale/CMakeLists.txt
git diff --cached --check
git commit -m "Trace NESYS ANSI filesystem calls"
```

---

### Task 7: Remove the font experiment and verify the complete replacement

**Files:**
- Delete: `src/Font/FontCharsetCompatibility.h`
- Delete: `src/Font/FontCharsetCompatibility.cpp`
- Delete: `src/Font/CMakeLists.txt`
- Delete: `tests/Font/FontCharsetCompatibilityTests.cpp`
- Delete: `tests/Font/CMakeLists.txt`
- Modify: `src/Loader/DllMain.cpp:1-12,272-278`
- Modify: `src/CMakeLists.txt:1-61`
- Modify: `tests/CMakeLists.txt:1-12`
- Modify only if verification finds a direct defect: files owned by Tasks 1-6 that cause the failing assertion.

**Interfaces:**
- Consumes: complete locale/time installation, targeted child propagation, and both bounded filesystem observers.
- Produces: an `iDmacDrv32.dll` with no GDI/font diagnostic owner and no Locale Emulator runtime dependency.
- Preserves: historical font specs and commits as investigation evidence; runtime acceptance remains unperformed.

- [ ] **Step 1: Remove the game font include and installation call**

Delete:

```cpp
#include "Font/FontCharsetCompatibility.h"
```

and:

```cpp
static_cast<void>(
    gc::font::InstallJapaneseFontCharsetCompatibility());
```

Do not replace them with any GDI call, face-name filter, asset rewrite, or `win32u` inspection.

- [ ] **Step 2: Delete the obsolete source/test targets and files**

Remove the two Font `add_subdirectory` entries, remove `gc_font_compatibility` from `iDmacDrv32`, and delete only the five Font files listed above. Keep both historical design documents unchanged.

- [ ] **Step 3: Build and run the complete focused slice**

```powershell
cmake --preset msvc32-debug
cmake --build --preset msvc32-debug --target JapaneseLocalePolicyTests JapaneseLocaleCompatibilityTests FilesystemDiagnosticsTests ServiceFilesystemHookTests NesysServicePatchTests NesysHookTransactionTests Kernel32HookTests iDmacDrv32
ctest --preset msvc32-debug -R '^(JapaneseLocalePolicyTests|JapaneseLocaleCompatibilityTests|FilesystemDiagnosticsTests|ServiceFilesystemHookTests|NesysServicePatchTests|NesysHookTransactionTests|Kernel32HookTests)$' --output-on-failure
```

Expected: all new policy/hook/diagnostic behavior and existing NESYS/Kernel32 behavior pass with no Font target.

- [ ] **Step 4: Run the full x86 Debug preset graph**

```powershell
cmake --preset msvc32-debug
cmake --build --preset msvc32-debug
ctest --preset msvc32-debug -j 4 --output-on-failure
```

Expected: the complete Debug build and CTest suite pass. Record exact test count and elapsed time for handoff.

- [ ] **Step 5: Run the full x86 Release preset graph**

```powershell
cmake --preset msvc32-release
cmake --build --preset msvc32-release
ctest --preset msvc32-release -j 4 --output-on-failure
```

Expected: the complete Release build and CTest suite pass. Record exact test count and elapsed time for handoff.

- [ ] **Step 6: Inspect the built DLL and cleanup boundary**

```powershell
dumpbin /exports 'build-msvc32-release\dist\iDmacDrv32.dll'
dumpbin /imports 'build-msvc32-release\dist\iDmacDrv32.dll'
rg -n "FontCharsetCompatibility|NtGdiHfontCreate|CreateFontIndirectW|AddFontResourceExA" src tests
git diff --check
git status --short
```

Require exports and ordinals to match `src/Driver/iDmac/iDmacDrv32.def`; imports must not contain Locale Emulator DLLs. The `rg` command must find no obsolete source/test implementation reference. Historical documentation matches are outside the searched roots and remain valid. `git diff --check` is silent, and status contains only the intended Task 7 deletions/edits.

- [ ] **Step 7: Commit removal after both preset graphs pass**

```powershell
git add -- src/Font/FontCharsetCompatibility.h src/Font/FontCharsetCompatibility.cpp src/Font/CMakeLists.txt tests/Font/FontCharsetCompatibilityTests.cpp tests/Font/CMakeLists.txt src/Loader/DllMain.cpp src/CMakeLists.txt tests/CMakeLists.txt
git diff --cached --name-status
git diff --cached --check
git commit -m "Remove font compatibility experiment"
```

- [ ] **Step 8: Verify the committed branch without changing runtime state**

```powershell
git status --short --branch
git log -8 --oneline --decorate
Get-FileHash 'build-msvc32-release\dist\iDmacDrv32.dll' -Algorithm SHA256
```

Expected: the feature worktree is clean, the seven implementation commits are visible after the approved spec/plan commits, and the Release DLL hash is recorded. Do not copy the DLL to `H:\gc` in this task.

## Runtime Acceptance Handoff

Static completion must report these checks as still pending until the operator authorizes deployment and runs them:

1. Launch the game directly as administrator without Locale Emulator.
2. Confirm `loader-log.txt` reports the game locale transaction with ACP 932, LCID `0x0411`, and UTC offset 540.
3. Confirm the targeted launcher injects `NesysService.exe -app` and `loader-service-log.txt` reports the same locale/time policy.
4. Reach the result screen and verify the Infinity-font upward triangle appears instead of a literal `$`.
5. Exercise game-data and downloaded-entry paths previously sensitive to locale and confirm no entries disappear or parse incorrectly.
6. Confirm NESYS startup/update behavior and date-sensitive behavior remain correct while the host clock remains unchanged.
7. Inspect both bounded filesystem diagnostic streams. If an ANSI failure has a usable CP932-decoded wide counterpart, write a separate API/path-specific filesystem design. Otherwise add no filesystem conversion.
8. Remove the temporary filesystem diagnostic hooks after that evidence is captured, regardless of whether a follow-up filesystem fix is needed.

Do not report any of these runtime checks as passed from build or CTest evidence alone.

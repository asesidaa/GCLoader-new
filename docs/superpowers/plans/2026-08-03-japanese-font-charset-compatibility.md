# Japanese Font Charset Compatibility Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make elevated Japanese-locale launches retain the normal Japanese font selection by supplying Locale Emulator's missing default-to-Shift-JIS charset conversion inside the game process.

**Architecture:** Add one focused `gc_font_compatibility` unit that detours the public `GDI32!CreateFontIndirectW` export through the existing transactional MinHook wrapper. The detour copies the caller's `LOGFONTW`, changes only charset 0 or 1 to charset 128, and forwards it; `DllMain` installs the hook only for the game process and treats installation failure as a logged, non-fatal compatibility loss.

**Tech Stack:** C++23, Win32 GDI types, MinHook, CMake/Ninja, MSVC x86, CTest

## Global Constraints

- Locale Emulator continues to own code-page, locale, time-zone, and registry emulation.
- The compatibility hook runs only in the `game471.exe` game process.
- Hook only the public `GDI32!CreateFontIndirectW` export; do not patch `win32u`, game RVAs, or font assets.
- Add no `config.toml` or ConfigGUI setting.
- Never mutate the caller's `LOGFONTW` and never add allocation, locking, Win32 helper calls, or logging to the successful per-font path.
- Preserve explicit non-default charsets and every non-charset field.
- Installation failure is logged once and remains fail-open so the game can still launch.
- Keep automated/static verification separate from operator-confirmed in-game font acceptance.
- Do not deploy to or otherwise mutate the `H:\gc` runtime tree during implementation.
- Preserve the unrelated existing modification to `src/Rfid/Feature.cpp`; never stage it with this feature.

## File Structure

- Create `src/Font/FontCharsetCompatibility.h`: public installation entry point and the production detour seam exercised by tests.
- Create `src/Font/FontCharsetCompatibility.cpp`: charset transformation, GDI32 hook request, process-lifetime transaction ownership, and startup-only logging.
- Create `src/Font/CMakeLists.txt`: focused static library linked to the existing Win32 hook infrastructure.
- Modify `src/CMakeLists.txt`: register the new source unit and link it into `iDmacDrv32`.
- Create `tests/Font/FontCharsetCompatibilityTests.cpp`: behavioral oracle for forwarding, conversion, and caller immutability.
- Create `tests/Font/CMakeLists.txt`: focused CTest target.
- Modify `tests/CMakeLists.txt`: register the new focused test directory.
- Modify `src/Loader/DllMain.cpp`: invoke installation inside the existing game-only initialization branch.

---

### Task 1: Add and wire the Japanese font charset compatibility hook

**Files:**
- Create: `src/Font/FontCharsetCompatibility.h`
- Create: `src/Font/FontCharsetCompatibility.cpp`
- Create: `src/Font/CMakeLists.txt`
- Modify: `src/CMakeLists.txt`
- Create: `tests/Font/FontCharsetCompatibilityTests.cpp`
- Create: `tests/Font/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`
- Modify: `src/Loader/DllMain.cpp:273-322`

**Interfaces:**
- Consumes: `gc::win32_hooks::HookRequest` and `gc::win32_hooks::MinHookTransaction` from `src/Platform/Win32/Hooking/MinHookTransaction.h`.
- Produces: `bool gc::font::InstallJapaneseFontCharsetCompatibility() noexcept` for game-process startup.
- Produces: `HFONT gc::font::detail::InvokeCreateFontIndirectWDetour(const LOGFONTW*, CreateFontIndirectWApi) noexcept` as the exact production forwarding seam exercised by focused tests.

- [ ] **Step 1: Add the focused test and build scaffolding with declarations but no implementation**

Create `src/Font/FontCharsetCompatibility.h`:

```cpp
#pragma once

#include <Windows.h>

namespace gc::font {

using CreateFontIndirectWApi = HFONT(WINAPI*)(const LOGFONTW*);

namespace detail {

[[nodiscard]] HFONT InvokeCreateFontIndirectWDetour(
    const LOGFONTW* requested,
    CreateFontIndirectWApi original) noexcept;

} // namespace detail

[[nodiscard]] bool InstallJapaneseFontCharsetCompatibility() noexcept;

} // namespace gc::font
```

Create `src/Font/FontCharsetCompatibility.cpp` with only the include so the first focused build reaches the intended unresolved production behavior:

```cpp
#include "Font/FontCharsetCompatibility.h"
```

Create `src/Font/CMakeLists.txt`:

```cmake
add_library(gc_font_compatibility STATIC
        FontCharsetCompatibility.cpp
)
target_include_directories(gc_font_compatibility PUBLIC
        ${PROJECT_SOURCE_DIR}/src
)
target_link_libraries(gc_font_compatibility PUBLIC
        gc_hooking
)
```

In `src/CMakeLists.txt`, add `add_subdirectory(Font)` immediately after
`add_subdirectory(Platform)`, and add `gc_font_compatibility` to the private
link libraries for `iDmacDrv32`.

Create `tests/Font/CMakeLists.txt`:

```cmake
add_executable(FontCharsetCompatibilityTests
        FontCharsetCompatibilityTests.cpp)
target_link_libraries(FontCharsetCompatibilityTests PRIVATE
        gc_font_compatibility)
add_test(NAME FontCharsetCompatibilityTests
        COMMAND FontCharsetCompatibilityTests)
```

Add `add_subdirectory(Font)` to `tests/CMakeLists.txt`.

Create `tests/Font/FontCharsetCompatibilityTests.cpp`:

```cpp
#include "Font/FontCharsetCompatibility.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <iterator>

namespace {

int Expect(bool condition, const char* name) {
    if (condition) {
        return 0;
    }
    std::cerr << "Expectation failed: " << name << "\n";
    return 1;
}

struct CaptureState {
    int calls{};
    bool received_null{};
    LOGFONTW received{};
};

CaptureState* g_capture{};

HFONT ExpectedHandle() noexcept {
    return reinterpret_cast<HFONT>(std::uintptr_t{0x1234});
}

HFONT WINAPI CaptureCreateFontIndirectW(
    const LOGFONTW* requested) noexcept {
    ++g_capture->calls;
    g_capture->received_null = requested == nullptr;
    if (requested != nullptr) {
        g_capture->received = *requested;
    }
    return ExpectedHandle();
}

LOGFONTW CanaryLogFont(BYTE charset) {
    LOGFONTW value{};
    value.lfHeight = -42;
    value.lfWidth = 17;
    value.lfEscapement = 123;
    value.lfOrientation = 321;
    value.lfWeight = FW_BOLD;
    value.lfItalic = TRUE;
    value.lfUnderline = TRUE;
    value.lfStrikeOut = TRUE;
    value.lfCharSet = charset;
    value.lfOutPrecision = OUT_TT_PRECIS;
    value.lfClipPrecision = CLIP_LH_ANGLES;
    value.lfQuality = CLEARTYPE_NATURAL_QUALITY;
    value.lfPitchAndFamily = FIXED_PITCH | FF_MODERN;
    constexpr wchar_t face[] = L"InfinityFont_midiam_dot";
    std::copy(std::begin(face), std::end(face), value.lfFaceName);
    return value;
}

int TestCharsetConversion(
    BYTE requested_charset,
    BYTE expected_charset,
    const char* name) {
    auto requested = CanaryLogFont(requested_charset);
    const auto original_request = requested;
    auto expected = requested;
    expected.lfCharSet = expected_charset;

    CaptureState capture{};
    g_capture = &capture;
    const auto result =
        gc::font::detail::InvokeCreateFontIndirectWDetour(
            &requested,
            CaptureCreateFontIndirectW);

    int failures = 0;
    failures += Expect(result == ExpectedHandle(), name);
    failures += Expect(capture.calls == 1, "original called once");
    failures += Expect(!capture.received_null, "non-null request forwarded");
    failures += Expect(
        std::memcmp(&capture.received, &expected, sizeof(expected)) == 0,
        "only expected charset is changed");
    failures += Expect(
        std::memcmp(
            &requested,
            &original_request,
            sizeof(original_request)) == 0,
        "caller LOGFONTW remains unchanged");
    return failures;
}

} // namespace

int main() {
    int failures = 0;
    failures += TestCharsetConversion(
        ANSI_CHARSET,
        SHIFTJIS_CHARSET,
        "ANSI charset converts to Shift-JIS");
    failures += TestCharsetConversion(
        DEFAULT_CHARSET,
        SHIFTJIS_CHARSET,
        "default charset converts to Shift-JIS");
    failures += TestCharsetConversion(
        GB2312_CHARSET,
        GB2312_CHARSET,
        "explicit charset is preserved");

    CaptureState null_capture{};
    g_capture = &null_capture;
    const auto null_result =
        gc::font::detail::InvokeCreateFontIndirectWDetour(
            nullptr,
            CaptureCreateFontIndirectW);
    failures += Expect(
        null_result == ExpectedHandle(),
        "null request preserves original result");
    failures += Expect(
        null_capture.calls == 1 && null_capture.received_null,
        "null request is forwarded unchanged");

    return failures == 0 ? 0 : 1;
}
```

- [ ] **Step 2: Configure and run the focused test to verify the RED state**

From `H:\gc\artifacts\GCLoader`, initialize the x86 toolchain and build only the new test:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\Tools\Launch-VsDevShell.ps1' -Arch x86 -HostArch x86 -SkipAutomaticLocation
cmake --preset msvc32-debug
cmake --build --preset msvc32-debug --target FontCharsetCompatibilityTests
```

Expected: the link fails with an unresolved external symbol for `gc::font::detail::InvokeCreateFontIndirectWDetour`. A configure or compile failure is not the intended RED state and must be corrected before continuing.

- [ ] **Step 3: Implement the transformation and process-lifetime public-API hook**

Replace `src/Font/FontCharsetCompatibility.cpp` with:

```cpp
#include "Font/FontCharsetCompatibility.h"

#include "Platform/Win32/Hooking/MinHookTransaction.h"

#include <plog/Log.h>

#include <array>
#include <memory>
#include <span>
#include <utility>

namespace gc::font {
namespace {

CreateFontIndirectWApi g_original_create_font_indirect_w{};
std::unique_ptr<gc::win32_hooks::MinHookTransaction>
    g_hook_transaction;

HFONT WINAPI CreateFontIndirectWDetour(
    const LOGFONTW* requested) noexcept {
    return detail::InvokeCreateFontIndirectWDetour(
        requested,
        g_original_create_font_indirect_w);
}

void LogInstallException() noexcept {
    try {
        PLOG_WARNING
            << "FontCharsetCompatibility: initialization exception";
    } catch (...) {
    }
}

void LogInstallSuccess() noexcept {
    try {
        PLOG_INFO
            << "FontCharsetCompatibility: CreateFontIndirectW hook active";
    } catch (...) {
    }
}

} // namespace

HFONT detail::InvokeCreateFontIndirectWDetour(
    const LOGFONTW* requested,
    CreateFontIndirectWApi original) noexcept {
    if (original == nullptr) {
        return nullptr;
    }
    if (requested == nullptr) {
        return original(nullptr);
    }

    auto adjusted = *requested;
    if (adjusted.lfCharSet == ANSI_CHARSET ||
        adjusted.lfCharSet == DEFAULT_CHARSET) {
        adjusted.lfCharSet = SHIFTJIS_CHARSET;
    }
    return original(&adjusted);
}

bool InstallJapaneseFontCharsetCompatibility() noexcept {
    if (g_hook_transaction != nullptr) {
        return true;
    }

    try {
        auto candidate =
            std::make_unique<gc::win32_hooks::MinHookTransaction>();
        const std::array requests{
            gc::win32_hooks::HookRequest{
                .module_name = L"GDI32.dll",
                .export_name = "CreateFontIndirectW",
                .detour = reinterpret_cast<LPVOID>(
                    &CreateFontIndirectWDetour),
                .original = reinterpret_cast<LPVOID*>(
                    &g_original_create_font_indirect_w),
            },
        };

        const auto installed = candidate->Install(
            std::span<const gc::win32_hooks::HookRequest>{requests});
        if (!installed) {
            g_original_create_font_indirect_w = nullptr;
            // MinHookTransaction already logged the exact failed stage,
            // export, Win32 error, or MinHook status.
            return false;
        }

        g_hook_transaction = std::move(candidate);
        LogInstallSuccess();
        return true;
    } catch (...) {
        g_original_create_font_indirect_w = nullptr;
        LogInstallException();
        return false;
    }
}

} // namespace gc::font
```

The production detour must remain a thin call into the tested helper. Do not add face-name filtering or per-call diagnostics.

- [ ] **Step 4: Wire the hook into game-only startup without making it fatal**

Add this include to `src/Loader/DllMain.cpp` with the other feature headers:

```cpp
#include "Font/FontCharsetCompatibility.h"
```

Inside the `ShouldRunGameOnlyInitialization(role)` branch beginning around `src/Loader/DllMain.cpp:273`, invoke the installer before the other game-only runtime features:

```cpp
static_cast<void>(
    gc::font::InstallJapaneseFontCharsetCompatibility());
```

Do not branch on the Boolean result. The installer owns the success/exception
log, `MinHookTransaction` owns exact resolve/create/enable failure logs, and
failure intentionally remains non-fatal.

- [ ] **Step 5: Run the focused Debug and Release tests**

```powershell
cmake --build --preset msvc32-debug --target FontCharsetCompatibilityTests iDmacDrv32
ctest --preset msvc32-debug -R '^FontCharsetCompatibilityTests$' --output-on-failure
cmake --preset msvc32-release
cmake --build --preset msvc32-release --target FontCharsetCompatibilityTests iDmacDrv32
ctest --preset msvc32-release -R '^FontCharsetCompatibilityTests$' --output-on-failure
```

Expected: both focused tests pass, and both x86 `iDmacDrv32` targets link successfully.

- [ ] **Step 6: Run full static verification and inspect only this task's changes**

```powershell
cmake --build --preset msvc32-debug
ctest --preset msvc32-debug -j 4 --output-on-failure
cmake --build --preset msvc32-release
ctest --preset msvc32-release -j 4 --output-on-failure
git diff --check
git status --short
git diff -- src/Font src/CMakeLists.txt src/Loader/DllMain.cpp tests/Font tests/CMakeLists.txt
```

Expected:

- Both complete preset graphs build.
- Both full CTest suites pass.
- `git diff --check` reports no errors.
- The diff contains only the font feature, its tests/CMake wiring, and the game-only startup call.
- `src/Rfid/Feature.cpp` may remain modified, but its contents are not inspected, staged, or changed by this task.
- No file under `H:\gc` is changed and no in-game success is claimed.

- [ ] **Step 7: Commit the atomic implementation**

```powershell
git add -- src/Font/FontCharsetCompatibility.h src/Font/FontCharsetCompatibility.cpp src/Font/CMakeLists.txt src/CMakeLists.txt src/Loader/DllMain.cpp tests/Font/FontCharsetCompatibilityTests.cpp tests/Font/CMakeLists.txt tests/CMakeLists.txt
git diff --cached --check
git commit -m "fix: preserve Japanese font charset under elevation"
git status --short --branch
```

Expected: the commit contains only the eight listed implementation/test/build files. The pre-existing unstaged `src/Rfid/Feature.cpp` modification remains outside the commit.

## Deferred Runtime Acceptance

After implementation is statically complete, deployment requires separate user authorization because `H:\gc` is the runtime tree. The acceptance run is:

1. Deploy the newly built x86 `iDmacDrv32.dll` only after explicit approval.
2. Launch `game471.exe` through Locale Emulator's Japanese administrator profile.
3. Confirm `loader-log.txt` contains `FontCharsetCompatibility: CreateFontIndirectW hook active`.
4. Visually compare the affected numbers with the normal Japanese-profile appearance.
5. Record static verification and gameplay acceptance separately.

# Font Selection Diagnostics Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Produce a temporary Release DLL that observes bundled-font registration and deeper GDI font selection without rewriting the game's requested charset.

**Architecture:** Keep the existing game-only MinHook transaction and make its `CreateFontIndirectW` path transparent. Add `AddFontResourceExA` to the same transaction, with a tested forwarding/last-error seam, and log the first 16 bytes of `win32u!NtGdiHfontCreate` once so a deeper Locale Emulator hook can be detected.

**Tech Stack:** C++23, Win32 GDI, MinHookTransaction, plog, CMake/CTest, MSVC x86

## Global Constraints

- Work only in `H:\gc\artifacts\GCLoader\.worktrees\japanese-font-charset`.
- Do not mutate `H:\gc` during implementation or static verification.
- Preserve Win32 arguments, return values, and last-error behavior.
- Keep all per-call traces bounded and removable.
- Keep runtime acceptance separate from automated verification.

---

### Task 1: Add the observational font pipeline trace

**Files:**
- Modify: `src/Font/FontCharsetCompatibility.h`
- Modify: `src/Font/FontCharsetCompatibility.cpp`
- Test: `tests/Font/FontCharsetCompatibilityTests.cpp`

**Interfaces:**
- Consumes: `gc::win32_hooks::MinHookTransaction::Install(std::span<const HookRequest>)`
- Produces: `detail::InvokeCreateFontIndirectWDetour(const LOGFONTW*, CreateFontIndirectWApi)` as a transparent forwarding seam
- Produces: `detail::InvokeAddFontResourceExADetour(LPCSTR, DWORD, PVOID, AddFontResourceExAApi, FontResourceObserver)` as an exact forwarding, observation, and last-error-preservation seam
- Produces: `detail::FormatNtGdiEntryBytes(const std::array<std::byte, 16>&)` as the deterministic entry-byte formatter used by the one-shot trace
- Produces: bounded runtime records named `FontCharsetCompatibility: font_resource`, `FontCharsetCompatibility: detour_hit`, and `FontCharsetCompatibility: ntgdi_entry`

- [ ] **Step 1: Write the failing pass-through test**

Extend `CaptureState` with the received pointer and replace the charset-conversion assertions with exact pass-through assertions:

```cpp
struct CaptureState {
    int calls{};
    bool received_null{};
    const LOGFONTW* received_pointer{};
    LOGFONTW received{};
};

int TestFontRequestPassThrough(BYTE charset, const char* name) {
    auto requested = CanaryLogFont(charset);
    const auto original_request = requested;

    CaptureState capture{};
    g_capture = &capture;
    const auto result =
        gc::font::detail::InvokeCreateFontIndirectWDetour(
            &requested,
            CaptureCreateFontIndirectW);

    int failures = 0;
    failures += Expect(result == ExpectedHandle(), name);
    failures += Expect(capture.calls == 1, "original called once");
    failures += Expect(
        capture.received_pointer == &requested,
        "original request pointer forwarded");
    failures += Expect(
        std::memcmp(
            &capture.received,
            &original_request,
            sizeof(original_request)) == 0,
        "all LOGFONTW fields forwarded unchanged");
    return failures;
}
```

Call it for `ANSI_CHARSET`, `DEFAULT_CHARSET`, and `GB2312_CHARSET`.

- [ ] **Step 2: Run the focused test and verify RED**

Run from the x86 Visual Studio developer environment:

```powershell
cmake --build --preset msvc32-debug --target FontCharsetCompatibilityTests
ctest --preset msvc32-debug -R '^FontCharsetCompatibilityTests$' --output-on-failure
```

Expected: the test fails because the current detour passes a copied structure and changes charset `0/1` to `128`.

- [ ] **Step 3: Make `CreateFontIndirectW` observational**

Change the production seam to forward the original pointer directly:

```cpp
HFONT detail::InvokeCreateFontIndirectWDetour(
    const LOGFONTW* requested,
    CreateFontIndirectWApi original) noexcept {
    return original != nullptr ? original(requested) : nullptr;
}
```

Remove `ForwardedCharset`, report `forwarded_charset` from the unchanged request, and add `mode=pass_through` to the runtime record.

- [ ] **Step 4: Run the focused test and verify GREEN**

Run the Step 2 commands again.

Expected: `FontCharsetCompatibilityTests` passes.

- [ ] **Step 5: Write the failing resource-observation test**

Declare these production-facing types and seam in the header:

```cpp
using AddFontResourceExAApi = int(WINAPI*)(LPCSTR, DWORD, PVOID);
using FontResourceObserver = void(*)(
    LPCSTR,
    DWORD,
    PVOID,
    int,
    DWORD) noexcept;

[[nodiscard]] int InvokeAddFontResourceExADetour(
    LPCSTR file,
    DWORD flags,
    PVOID reserved,
    AddFontResourceExAApi original,
    FontResourceObserver observer) noexcept;
```

Add a capturing fake original and observer. The original returns `3` and sets `ERROR_ACCESS_DENIED`; the observer deliberately changes last error. Assert that the path pointer, flags `0x30`, reserved pointer, result `3`, and observed error are exact, and that `GetLastError()` after the seam remains `ERROR_ACCESS_DENIED`.

- [ ] **Step 6: Build the focused target and verify RED**

Run:

```powershell
cmake --build --preset msvc32-debug --target FontCharsetCompatibilityTests
```

Expected: link failure for the declared but undefined `InvokeAddFontResourceExADetour`.

- [ ] **Step 7: Implement and install the resource trace**

Implement the seam so it calls the original, captures `GetLastError()`, invokes the observer, restores the captured error, and returns the original result. Add an `AddFontResourceExA` detour that uses this seam and a bounded observer which logs:

```text
FontCharsetCompatibility: font_resource call=<n> file=<input> resolved=<absolute> flags=<value> reserved=<ptr> exists=<0|1> result=<n> last_error=<n>
```

Add `AddFontResourceExA` as the second `HookRequest` in the existing transaction. Clear both original pointers on any installation failure.

- [ ] **Step 8: Run the focused test and verify GREEN**

Run the Step 2 commands again.

Expected: `FontCharsetCompatibilityTests` passes and the target links cleanly.

- [ ] **Step 9: Write the failing entry-byte formatting test**

Declare this diagnostic formatter in the header:

```cpp
[[nodiscard]] std::array<char, 48> FormatNtGdiEntryBytes(
    const std::array<std::byte, 16>& bytes) noexcept;
```

Pass a literal 16-byte array and assert the independently written lowercase,
space-separated string, including correct null termination. This catches
reversed nibbles, missing bytes, separator errors, and an unterminated log
field.

- [ ] **Step 10: Build the focused target and verify RED**

Run the focused build from Step 6.

Expected: link failure for the declared but undefined
`FormatNtGdiEntryBytes`.

- [ ] **Step 11: Add the formatter and one-shot deeper-GDI entry trace**

Implement the formatter with a fixed lowercase hex lookup table and no dynamic
allocation. On the first non-recursive font detour call, use already-loaded
`win32u.dll` and `GetProcAddress` to inspect `NtGdiHfontCreate`. Log its
address and first 16 bytes without loading a new module or patching memory:

On the first non-recursive font detour call, use already-loaded `win32u.dll` and `GetProcAddress` to inspect `NtGdiHfontCreate`. Log its address and first 16 bytes in lowercase hexadecimal, without loading a new module or patching memory:

```text
FontCharsetCompatibility: ntgdi_entry module=<ptr> address=<ptr> bytes=<16 space-separated bytes>
```

If either lookup fails, log `address=00000000 bytes=<unavailable>` and continue.

- [ ] **Step 12: Run the focused test and verify GREEN**

Run the Step 2 commands again.

Expected: `FontCharsetCompatibilityTests` passes.

- [ ] **Step 13: Verify both configurations**

Run:

```powershell
cmake --build --preset msvc32-debug
ctest --preset msvc32-debug -j 4 --output-on-failure
cmake --build --preset msvc32-release
ctest --preset msvc32-release -j 4 --output-on-failure
git diff --check
```

Expected: both complete builds succeed, every test passes, and `git diff --check` reports no errors.

- [ ] **Step 14: Commit the temporary trace**

```powershell
git add -- src/Font/FontCharsetCompatibility.h src/Font/FontCharsetCompatibility.cpp tests/Font/FontCharsetCompatibilityTests.cpp
git commit -m "debug: trace bundled font registration"
```

Record the Release `iDmacDrv32.dll` SHA-256 and leave runtime deployment pending explicit approval.

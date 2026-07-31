# Fullscreen Device-Loss Retry Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Keep `game471.exe` alive across fullscreen Alt+Tab by deferring lazy renderer initialization when its dynamic Direct3D 9 vertex-buffer creation fails.

**Architecture:** Add one focused `RendererDeviceLoss` runtime-patch module. A guarded SafetyHook mid-hook observes the exact `CreateVertexBuffer` HRESULT; failure clears the renderer's initialized flag and redirects to the native initializer epilogue so the next render retries, while success remains entirely native. Installation is always-on in the game process, byte-contract guarded, and fail-closed.

**Tech Stack:** C++23, Win32 x86/MSVC, SafetyHook 0.7, CMake/Ninja, existing standalone CTest style.

## Global Constraints

- Support only preferred image base `0x00400000` and the current `game471.exe` binary contracts.
- Hook HRESULT-test RVA `0x000E79F7`; retry through native epilogue RVA `0x000E7EE9`.
- Require HRESULT-site bytes `85 C0 7C 59 8B 4F 0C` and epilogue bytes `5F 5E 5B 8B E5 5D C3` before installing.
- Clear only renderer byte offset `+0x484`, and only after a negative HRESULT.
- Keep zero and positive HRESULTs on the original path without any register or memory mutation.
- Do not allocate, invoke Direct3D, or emit per-call logging from the render hook.
- Catch both C++ and structured memory faults at the callback boundary; redirect only after the initialized-byte write succeeds.
- Install automatically only in the game process; add no `config.toml` or ConfigGUI surface.
- Reject an unsupported base, unreadable contract, byte mismatch, or hook-creation failure without publishing an active hook.
- Do not modify or deploy files in the `H:\gc` runtime tree.
- Treat automated build/signature evidence as static verification; fullscreen Alt+Tab remains user-run runtime acceptance.

---

## File Structure

- Create `src/Patches/RendererDeviceLoss/RendererDeviceLossPatch.h`: binary constants, retry writer seam, install actions/errors, pure context transform, installer, and production initializer declaration.
- Create `src/Patches/RendererDeviceLoss/RendererDeviceLossPatch.cpp`: retry behavior, guarded Win32 memory access, signature preflight, SafetyHook ownership, logging, and one-shot production initialization.
- Create `tests/Patches/RendererDeviceLossPatchTests.cpp`: context-transform and injected installer behavior tests.
- Modify `src/Patches/CMakeLists.txt`: compile the new source into `gc_runtime_patches`.
- Modify `tests/Patches/CMakeLists.txt`: register `RendererDeviceLossPatchTests`.
- Modify `src/Loader/DllMain.cpp`: initialize the mandatory patch in the game-process branch.

### Task 1: Retry Context Transform

**Files:**
- Create: `tests/Patches/RendererDeviceLossPatchTests.cpp`
- Create: `src/Patches/RendererDeviceLoss/RendererDeviceLossPatch.h`
- Create: `src/Patches/RendererDeviceLoss/RendererDeviceLossPatch.cpp`
- Modify: `src/Patches/CMakeLists.txt`
- Modify: `tests/Patches/CMakeLists.txt`

**Interfaces:**
- Consumes: `safetyhook::Context` register state at RVA `0x000E79F7` (`EAX` is HRESULT and `ESI` is the renderer object).
- Produces: `bool ApplyRendererDeviceLossRetry(safetyhook::Context&, std::uintptr_t, RendererInitializedWriter) noexcept` and the binary constants used by Task 2.

- [ ] **Step 1: Add a failing context-transform test target**

Register the test in `tests/Patches/CMakeLists.txt`:

```cmake
add_executable(RendererDeviceLossPatchTests
        RendererDeviceLossPatchTests.cpp)
target_link_libraries(RendererDeviceLossPatchTests PRIVATE
        gc_runtime_patches)
add_test(NAME RendererDeviceLossPatchTests
        COMMAND RendererDeviceLossPatchTests)
```

Create `tests/Patches/RendererDeviceLossPatchTests.cpp` with a canary-filled
`safetyhook::Context`, an injected initialized-byte writer, and these first
three cases:

```cpp
auto failed = CanaryContext();
failed.eax = 0x88760868U; // D3DERR_DEVICELOST, negative HRESULT
failed.esi = 0x12345000U;
const auto failed_before = failed;
WriterState writer{};
const bool deferred = ApplyRendererDeviceLossRetry(
    failed,
    kPreferredImageBase,
    {.context = &writer, .clear_initialized = ClearInitialized});
failures += Expect(
    deferred && writer.calls == 1 &&
        writer.renderer == failed_before.esi &&
        writer.offset == kRendererInitializedOffset &&
        failed.eip == kPreferredImageBase + kRendererInitializerEpilogueRva &&
        ContextEqualsExceptEip(failed, failed_before),
    "failed vertex-buffer creation defers initialization");

for (const std::uint32_t result : {0U, 1U, 0x7FFFFFFFU}) {
    auto success = CanaryContext();
    success.eax = result;
    const auto before = success;
    WriterState untouched{};
    failures += Expect(
        !ApplyRendererDeviceLossRetry(
            success,
            kPreferredImageBase,
            {.context = &untouched,
             .clear_initialized = ClearInitialized}) &&
            untouched.calls == 0 && ContextEquals(success, before),
        "nonnegative HRESULT preserves native context");
}

auto rejected = CanaryContext();
rejected.eax = 0x80004005U;
const auto rejected_before = rejected;
WriterState failing_writer{.succeed = false};
failures += Expect(
    !ApplyRendererDeviceLossRetry(
        rejected,
        kPreferredImageBase,
        {.context = &failing_writer,
         .clear_initialized = ClearInitialized}) &&
        failing_writer.calls == 1 &&
        ContextEquals(rejected, rejected_before),
    "failed guarded write preserves native path");
```

The fake writer records the renderer address and offset and changes an owned
byte only when `succeed` is true. `ContextEqualsExceptEip` must restore `EIP`
before comparing the complete context with `std::memcmp`.

- [ ] **Step 2: Run the focused build and witness RED**

Run:

```powershell
cmake --preset msvc32-debug
cmake --build --preset msvc32-debug --target RendererDeviceLossPatchTests
```

Expected: compilation fails because
`Patches/RendererDeviceLoss/RendererDeviceLossPatch.h` and
`ApplyRendererDeviceLossRetry` do not exist yet. A configuration/toolchain
error is not an acceptable RED; repair the environment and rerun until the
failure is caused by the missing production behavior.

- [ ] **Step 3: Implement the minimal retry transform**

Define these constants and interfaces in
`src/Patches/RendererDeviceLoss/RendererDeviceLossPatch.h`:

```cpp
inline constexpr std::uintptr_t kPreferredImageBase = 0x00400000U;
inline constexpr std::uint32_t kVertexBufferResultRva = 0x000E79F7U;
inline constexpr std::uint32_t kRendererInitializerEpilogueRva = 0x000E7EE9U;
inline constexpr std::size_t kRendererInitializedOffset = 0x484U;

struct RendererInitializedWriter {
    void* context{};
    bool (*clear_initialized)(
        void*, std::uintptr_t, std::size_t) noexcept{};
};

[[nodiscard]] bool ApplyRendererDeviceLossRetry(
    safetyhook::Context& context,
    std::uintptr_t image_base,
    RendererInitializedWriter writer) noexcept;
```

Implement only the tested behavior in the `.cpp`:

```cpp
bool ApplyRendererDeviceLossRetry(
    safetyhook::Context& context,
    std::uintptr_t image_base,
    RendererInitializedWriter writer) noexcept {
    if (static_cast<std::int32_t>(context.eax) >= 0) {
        return false;
    }
    if (context.esi == 0 || writer.clear_initialized == nullptr ||
        image_base != kPreferredImageBase ||
        !writer.clear_initialized(
            writer.context,
            context.esi,
            kRendererInitializedOffset)) {
        return false;
    }
    context.eip = static_cast<std::uint32_t>(
        image_base + kRendererInitializerEpilogueRva);
    return true;
}
```

Add `RendererDeviceLoss/RendererDeviceLossPatch.cpp` to
`gc_runtime_patches` in `src/Patches/CMakeLists.txt`.

- [ ] **Step 4: Run the focused test and witness GREEN**

Run:

```powershell
cmake --build --preset msvc32-debug --target RendererDeviceLossPatchTests
ctest --preset msvc32-debug -R "^RendererDeviceLossPatchTests$" --output-on-failure
```

Expected: build succeeds and the single focused test passes with no warnings
or diagnostic output.

- [ ] **Step 5: Commit the retry transform**

```powershell
git add -- src/Patches/RendererDeviceLoss/RendererDeviceLossPatch.h src/Patches/RendererDeviceLoss/RendererDeviceLossPatch.cpp src/Patches/CMakeLists.txt tests/Patches/RendererDeviceLossPatchTests.cpp tests/Patches/CMakeLists.txt
git commit -m "fix: defer failed renderer vertex-buffer initialization"
```

### Task 2: Guarded Hook Installation and Game Integration

**Files:**
- Modify: `tests/Patches/RendererDeviceLossPatchTests.cpp`
- Modify: `src/Patches/RendererDeviceLoss/RendererDeviceLossPatch.h`
- Modify: `src/Patches/RendererDeviceLoss/RendererDeviceLossPatch.cpp`
- Modify: `src/Loader/DllMain.cpp`

**Interfaces:**
- Consumes: `ApplyRendererDeviceLossRetry`, Task 1 binary constants, Win32 main-module memory, and `safetyhook::create_mid`.
- Produces: `InstallRendererDeviceLossPatch(std::uintptr_t, RendererInstallActions) noexcept` for deterministic testing and `RendererDeviceLossPatchInit() noexcept` for `DllMain`.

- [ ] **Step 1: Extend the test with failing installer cases**

Add an injected installer model to the test:

```cpp
enum class RendererContractSite {
    None,
    VertexBufferResult,
    InitializerEpilogue,
};

enum class RendererInstallStage {
    None,
    InvalidActions,
    UnexpectedImageBase,
    PreflightRead,
    PreflightMismatch,
    HookInstall,
};

struct RendererInstallError {
    RendererInstallStage stage{};
    RendererContractSite site{};
};

struct RendererInstallActions {
    void* context{};
    bool (*read)(void*, std::uintptr_t, std::span<std::byte>) noexcept{};
    bool (*install_hook)(void*, std::uintptr_t) noexcept{};
    void (*reset_hook)(void*) noexcept{};
};

[[nodiscard]] std::expected<void, RendererInstallError>
InstallRendererDeviceLossPatch(
    std::uintptr_t image_base,
    RendererInstallActions actions) noexcept;
```

Use a `FakeInstallState` whose `read` callback supplies the production expected
patterns for the two requested addresses and can independently fail or corrupt
either read. Assert all of the following:

```cpp
// Exact contracts: two reads, then one hook at base + result RVA, no reset.
// Wrong base: UnexpectedImageBase, zero reads, zero hook calls.
// Missing read/install/reset callback: InvalidActions, zero side effects.
// Read failure at each site: PreflightRead with that site, no hook call.
// One-byte mismatch at each site: PreflightMismatch with that site, no hook.
// Hook creation failure: HookInstall and exactly one reset call.
```

Also add a negative-HRESULT test with `ESI == 0`; it must leave the complete
context unchanged and never call the writer.

- [ ] **Step 2: Run the focused build and witness RED**

Run:

```powershell
cmake --build --preset msvc32-debug --target RendererDeviceLossPatchTests
```

Expected: compilation fails because the installer types/function and expected
contract arrays are not yet declared. The existing Task 1 transform cases must
remain conceptually valid.

- [ ] **Step 3: Implement signature preflight and one-hook installation**

Add the tested enums, structs, function, and exact patterns to the header:

```cpp
inline constexpr std::array<std::byte, 7> kVertexBufferResultPattern{
    std::byte{0x85}, std::byte{0xC0}, std::byte{0x7C},
    std::byte{0x59}, std::byte{0x8B}, std::byte{0x4F},
    std::byte{0x0C},
};
inline constexpr std::array<std::byte, 7> kRendererEpiloguePattern{
    std::byte{0x5F}, std::byte{0x5E}, std::byte{0x5B},
    std::byte{0x8B}, std::byte{0xE5}, std::byte{0x5D},
    std::byte{0xC3},
};
```

`InstallRendererDeviceLossPatch` must validate all action callbacks and the
preferred base, read and compare the HRESULT contract first and the epilogue
contract second, call `install_hook` only after both match, and call
`reset_hook` exactly once if installation reports failure. Return the precise
stage and site described by the tests.

Add production adapters in the `.cpp`:

```cpp
bool ProductionRead(
    void*, std::uintptr_t address, std::span<std::byte> output) noexcept;
bool ProductionClearInitialized(
    void*, std::uintptr_t renderer, std::size_t offset) noexcept;
void OnVertexBufferCreateResult(safetyhook::Context& context) noexcept;
bool ProductionInstallHook(void* opaque, std::uintptr_t address) noexcept;
void ProductionResetHook(void* opaque) noexcept;
```

`ProductionRead` and `ProductionClearInitialized` use `__try/__except` around
the exact `memcpy`/byte write. `OnVertexBufferCreateResult` wraps
`ApplyRendererDeviceLossRetry` in `try/catch (...)` and emits no logging.
`ProductionInstallHook` stores one `safetyhook::MidHook` in a candidate runtime
object and targets `OnVertexBufferCreateResult`.

Implement `RendererDeviceLossPatchInit()` as a one-shot, atomic, fail-closed
initializer:

```cpp
[[nodiscard]] bool RendererDeviceLossPatchInit() noexcept;
```

It resolves `GetModuleHandleW(nullptr)`, rejects any base other than
`kPreferredImageBase`, constructs an unpublished candidate runtime, calls the
injected installer with production actions, moves the candidate into global
ownership only on success, and logs one install success or the exact failure
stage/site. Repeated calls return the stored first result.

- [ ] **Step 4: Initialize the patch only for the game process**

Include the new header in `src/Loader/DllMain.cpp`. In the existing
`ShouldRunGameOnlyInitialization(role)` branch, immediately after the timing
patch succeeds, add:

```cpp
if (!gc::renderer_device_loss::RendererDeviceLossPatchInit()) {
    PLOG_ERROR
        << "RendererDeviceLossPatch: fail-closed DLL attach";
    return FALSE;
}
PLOG_DEBUG
    << "Renderer device-loss retry initialization complete!";
```

Do not add this initialization to the NESYS-process branch.

- [ ] **Step 5: Run focused Debug and Release tests and witness GREEN**

Run:

```powershell
cmake --build --preset msvc32-debug --target RendererDeviceLossPatchTests iDmacDrv32
ctest --preset msvc32-debug -R "^RendererDeviceLossPatchTests$" --output-on-failure
cmake --preset msvc32-release
cmake --build --preset msvc32-release --target RendererDeviceLossPatchTests iDmacDrv32
ctest --preset msvc32-release -R "^RendererDeviceLossPatchTests$" --output-on-failure
```

Expected: both configurations build the x86 DLL and pass every focused case.

- [ ] **Step 6: Commit installation and integration**

```powershell
git add -- src/Patches/RendererDeviceLoss/RendererDeviceLossPatch.h src/Patches/RendererDeviceLoss/RendererDeviceLossPatch.cpp src/Loader/DllMain.cpp tests/Patches/RendererDeviceLossPatchTests.cpp
git commit -m "fix: retry renderer restore after Direct3D device loss"
```

### Task 3: Binary and Repository Verification

**Files:**
- Verify only: `H:\gc\game471.exe.i64`
- Verify only: `src/Patches/RendererDeviceLoss/RendererDeviceLossPatch.*`
- Verify only: complete repository build/test graph

**Interfaces:**
- Consumes: the completed always-on patch and current IDA database.
- Produces: static evidence ready for user-run fullscreen Alt+Tab acceptance.

- [ ] **Step 1: Re-read the live IDB contracts through the existing daemon**

From `H:\IDACLI`, connect with `AgentSession.start(..., daemon=True)`, call
`probe_backend(require_ida=True)`, and read seven bytes at `0x004E79F7` and
`0x004E7EE9`:

```python
__result__ = {
    "result_site": ida_bytes.get_bytes(0x004E79F7, 7).hex(" "),
    "epilogue": ida_bytes.get_bytes(0x004E7EE9, 7).hex(" "),
}
```

Expected:

```text
result_site: 85 c0 7c 59 8b 4f 0c
epilogue:    5f 5e 5b 8b e5 5d c3
```

- [ ] **Step 2: Run the complete Debug and Release build/test graphs**

Run from an x86 MSVC developer environment:

```powershell
cmake --build --preset msvc32-debug
ctest --preset msvc32-debug -j 4
cmake --build --preset msvc32-release
ctest --preset msvc32-release -j 4
```

Expected: both full builds succeed and both full CTest suites report zero
failures.

- [ ] **Step 3: Inspect only owned changes and repository hygiene**

Run:

```powershell
git diff --check
git status --short --branch
git log -3 --oneline
```

Inspect both implementation commits and confirm there are no generated build
artifacts or unrelated edits in the diff. Do not deploy the DLL into `H:\gc`.

- [ ] **Step 4: Hand off runtime acceptance without overclaiming**

Report the dump-backed cause, exact hook behavior, both build/test results, and
the IDB byte verification. State explicitly that runtime acceptance is still
pending until the user launches fullscreen, reaches a post-load scene,
Alt+Tabs out and back, and confirms that rendering resumes without process
termination.

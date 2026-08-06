# Relaxed Game Binary Compatibility Revision Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Revise GCLoader's four-site game bootstrap so every clean/already-patched combination is accepted, only missing patches are written, PE metadata is ignored, and write failure aborts startup without rollback.

**Architecture:** Keep the existing `GameCompatibility` unit and injected memory seam, but make the four fixed RVA byte contracts the complete applicability test. Preflight all four sites, reject unreadable or unknown bytes before mutation, skip already-patched sites, and stop immediately on the first failed required write.

**Tech Stack:** C++23, Win32 x86 (`VirtualProtect`, `FlushInstructionCache`), `std::expected`, CMake/Ninja/MSVC presets, the existing `SystemPath/StartupFatal` publisher, and executable CTest targets.

## Global Constraints

- Work only in `H:\gc\artifacts\GCLoader`; `H:\gc` remains read-only binary evidence and is not deployed to or mutated.
- Continue on `feature/game-binary-compatibility-patches` in the normal checkout; do not use agents or a worktree.
- Keep exactly four fixed contracts: RVA `0x000B0896` (`75 02` to `90 90`), RVA `0x00102C7B` (`75 3B` to `EB 3B`), RVA `0x00103EE6` (`E8 45 F6 FF FF` to five NOPs), and RVA `0x002F7AC3` (`31` to `32`).
- Do not inspect DOS/NT headers, timestamp, machine, preferred image base, entry point, image size, header size, section count, or other PE metadata.
- An executable is applicable only when every site is readable and exactly clean or already patched. Accept all 16 clean/patched combinations.
- Preflight all four sites before any write. Unknown bytes and read failures produce zero writes.
- Write only clean sites in manifest order. On a write failure, return the exact failure immediately; do not roll back earlier process-local writes.
- Keep checked `base + RVA` arithmetic, guarded reads/writes, `VirtualProtect`, instruction-cache flush, and protection restoration.
- Patch only the game process. NESYS continues to skip the initializer.
- Keep the patch set mandatory and configuration-free.
- Do not add source-text/regex tests or import the production manifest into tests. Test literals remain independently derived from the two hashed executables.
- Run focused and full x86 MSVC Debug/Release verification. Do not claim game runtime acceptance.

---

## File Structure

- Modify `src/Patches/GameCompatibility/GameBinaryPatch.h` to remove PE-identity and rollback-only API fields and rename the successful write state to `PatchedImage`.
- Modify `src/Patches/GameCompatibility/GameBinaryPatch.cpp` to remove all PE header reads, classify sites independently, accept partial states, selectively write clean sites, and stop without rollback.
- Modify `tests/Patches/GameBinaryPatchTests.cpp` to cover all 16 combinations, patch-site-only reads, preflight rejection, selective writes, and immediate failure.
- Modify `src/Patches/GameCompatibility/GameBinaryPatchDiagnostics.cpp` to classify only unknown site bytes as an unsupported executable and remove identity/rollback output.
- Modify `tests/Patches/GameBinaryPatchDiagnosticsTests.cpp` to verify the revised unsupported/setup messages and absence of rollback semantics.
- Verify `src/Loader/DllMain.cpp` without changing its ordering: the existing result-name helper changes the successful log token from `patched_clean` to `patched`.

---

### Task 1: Patch-Site-Only Applicability and Selective Writes

**Files:**
- Modify: `tests/Patches/GameBinaryPatchTests.cpp`
- Modify: `src/Patches/GameCompatibility/GameBinaryPatch.h`
- Modify: `src/Patches/GameCompatibility/GameBinaryPatch.cpp`

**Interfaces:**
- Consumes: loaded main-module base and `GameBinaryPatchActions` read/write callbacks.
- Produces:

```cpp
enum class GameBinaryImageState {
    PatchedImage,
    AlreadyPatchedImage,
};

enum class GameBinaryPatchStage {
    None,
    ResolveModule,
    InvalidActions,
    AddressRange,
    SiteRead,
    UnknownBytes,
    SiteWrite,
};

struct GameBinaryPatchError {
    GameBinaryPatchStage stage{GameBinaryPatchStage::None};
    GameBinaryPatchSite site{GameBinaryPatchSite::None};
    std::uint32_t rva{};
    GameBinaryBytePattern expected_clean{};
    GameBinaryBytePattern expected_patched{};
    GameBinaryBytePattern actual{};
    GameBinaryMemoryStage memory_stage{GameBinaryMemoryStage::None};
    DWORD win32_error{};
};
```

`InstallGameBinaryPatch()`, `ProductionGameBinaryPatchActions()`, and `GameBinaryPatchInit()` keep their current signatures. Remove `GameBinaryIdentityField`, `GameBinaryIdentityFieldName()`, `HeaderRead`, `IdentityMismatch`, `MixedState`, all identity-value fields, and both rollback fields.

- [ ] **Step 1: Replace strict-state tests with the failing relaxed contract**

In `tests/Patches/GameBinaryPatchTests.cpp`, remove PE-header fixture construction, identity-mismatch tests, mixed-state rejection, and rollback assertions. Keep the independent `kFixtureSites` byte fixture.

Add `<bit>` for `std::popcount`; remove Win32 PE-structure-only fixture code and any standard headers that become unused.

Create applicable images from a four-bit mask:

```cpp
constexpr std::uint8_t kAllPatchedMask =
    (1U << kGameBinaryPatchSiteCount) - 1U;

FakeImage ApplicableImage(std::uint8_t patched_mask) {
    FakeImage fake{};
    for (std::size_t index = 0; index < kFixtureSites.size(); ++index) {
        const auto& fixture = kFixtureSites[index];
        const bool patched =
            (patched_mask & (1U << index)) != 0;
        fake.StoreBytes(
            fixture.rva,
            patched ? std::span<const std::byte>{fixture.patched}
                    : std::span<const std::byte>{fixture.clean});
    }
    return fake;
}
```

Add `TestEveryCleanPatchedCombinationCompletes()`. For literal masks `0x0` through `0xF`, call the real installer and require:

```cpp
const auto expected_writes =
    kGameBinaryPatchSiteCount - std::popcount(mask);
const auto expected_state = mask == kAllPatchedMask
    ? GameBinaryImageState::AlreadyPatchedImage
    : GameBinaryImageState::PatchedImage;
```

Assert the result state, `site_count == 4`, exact write count, all four final patched byte ranges, and exact write-address order containing only initially clean sites. Also require `read_addresses` to contain exactly the four patch-site addresses in manifest order; a read at `fake.base` or a PE-header offset is a regression.

Retain an unknown-byte case for every site and a read-failure case for every site. Each must report the named site/RVA and perform zero writes because complete preflight precedes mutation.

Replace rollback tests with these observable cases:

```cpp
failures += TestEveryRequiredWriteFailureStopsImmediately();
failures += TestPostCopyFailureStopsWithoutRollback();
failures += TestPartialImageFailurePreservesPriorWrites();
```

For `TestEveryRequiredWriteFailureStopsImmediately()`, start all-clean and fail calls 1 through 4 before copy. Require exactly `call` write attempts, no later site address, earlier successful sites patched, and the failed/later sites clean.

For `TestPostCopyFailureStopsWithoutRollback()`, fail call 2 after copy for both `FlushInstructionCache` and `RestoreProtection`. Require exactly two attempts, sites 0 and 1 patched, sites 2 and 3 clean, and the original memory stage/error in `GameBinaryPatchError`.

For `TestPartialImageFailurePreservesPriorWrites()`, start with sites 0 and 2 already patched, fail the second required write (site 3) before copy, and require write addresses `[site 1, site 3]`; sites 0, 1, and 2 remain patched while site 3 remains clean. No restoration calls occur.

Keep invalid-actions, overflowing-base, unknown-byte, read-failure, and second-install coverage. The overflowing-base case must fail `AddressRange` before any callback.

- [ ] **Step 2: Run the focused Debug test and verify RED**

Run from the Visual Studio 18 Insiders x86 environment:

```powershell
$vcvars = 'C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat'
cmd.exe /d /s /c "call `"$vcvars`" >nul && cmake --build --preset msvc32-debug --target GameBinaryPatchTests && ctest --preset msvc32-debug -R ^GameBinaryPatchTests$"
```

Expected: the test executable builds against the old API but fails behaviorally because partial masks return `MixedState`, the installer reads PE headers, and write failures trigger rollback. If API expectations are changed in the same test edit, a compile failure naming `PatchedImage` is also an acceptable first RED; add no production changes until that failure is observed.

- [ ] **Step 3: Simplify the public contract**

In `GameBinaryPatch.h`, make the interface match the **Interfaces** block above. Preserve byte-pattern, site, memory-stage, action, result, installer, production-action, initializer, and remaining name-helper declarations exactly.

- [ ] **Step 4: Remove PE inspection and classify all sites independently**

In `GameBinaryPatch.cpp`, delete all supported PE constants, DOS/NT reads, identity helpers, and whole-image mixed-state logic.

Replace the range helper with fixed-RVA arithmetic only:

```cpp
bool CheckedPatchAddress(
    std::uintptr_t image_base,
    std::uint32_t rva,
    std::size_t size,
    std::uintptr_t& address) noexcept {
    if (size == 0) {
        return false;
    }
    constexpr auto maximum =
        std::numeric_limits<std::uintptr_t>::max();
    if (rva > maximum - image_base) {
        return false;
    }
    address = image_base + rva;
    return size - 1 <= maximum - address;
}
```

Validate nonzero base and callbacks, compute all four addresses, read all four patterns, then classify each as `Clean` or `Patched`. Return `UnknownBytes` immediately during classification only after every read completed successfully. Track whether at least one site is clean.

- [ ] **Step 5: Selectively write clean sites and stop on failure**

If no site is clean, return:

```cpp
GameBinaryPatchResult{
    .state = GameBinaryImageState::AlreadyPatchedImage,
    .site_count = kContracts.size(),
}
```

Otherwise iterate in manifest order, skip `Patched` states, and invoke `actions.write()` only for `Clean` states. On failure, return `SiteWrite` immediately with site, RVA, clean/patched/actual patterns, memory stage, and Windows error. Do not invoke any additional read or write callback and do not restore prior sites.

After all required writes succeed, return `PatchedImage` with `site_count == 4`. Update `GameBinaryImageStateName(PatchedImage)` to return `"patched"`; remove obsolete enum-name branches and the identity helper.

Keep guarded production reads/writes unchanged: a production write must still restore page protection even when copying or cache flushing fails. This protection restoration is local cleanup inside one write operation, not patch rollback.

- [ ] **Step 6: Run focused Debug and Release tests**

Run:

```powershell
$vcvars = 'C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat'
cmd.exe /d /s /c "call `"$vcvars`" >nul && cmake --build --preset msvc32-debug --target GameBinaryPatchTests && ctest --preset msvc32-debug -R ^GameBinaryPatchTests$ && cmake --build --preset msvc32-release --target GameBinaryPatchTests && ctest --preset msvc32-release -R ^GameBinaryPatchTests$"
```

Expected: both focused suites pass with no failed cases.

- [ ] **Step 7: Commit the selective patch engine revision**

```powershell
git add -- src/Patches/GameCompatibility/GameBinaryPatch.h src/Patches/GameCompatibility/GameBinaryPatch.cpp tests/Patches/GameBinaryPatchTests.cpp
git commit -m "Accept partially patched game binaries"
```

---

### Task 2: Diagnostics for Byte Applicability and Immediate Abort

**Files:**
- Modify: `tests/Patches/GameBinaryPatchDiagnosticsTests.cpp`
- Modify: `src/Patches/GameCompatibility/GameBinaryPatchDiagnostics.cpp`
- Verify: `src/Loader/DllMain.cpp`

**Interfaces:**
- Consumes: revised `GameBinaryPatchError`, `GameBinaryPatchStageName()`, `GameBinaryPatchSiteName()`, and `GameBinaryMemoryStageName()` from Task 1.
- Produces: unchanged `BuildGameBinaryPatchFatalDiagnostic(const GameBinaryPatchError&)`, with unknown bytes mapped to the unsupported-version title and operational failures mapped to the setup-error title.

- [ ] **Step 1: Write failing revised diagnostic tests**

Delete identity-mismatch and mixed-state test cases. Keep the test-local `TestPattern()` helper.

The unknown-byte case remains:

```cpp
GameBinaryPatchError{
    .stage = GameBinaryPatchStage::UnknownBytes,
    .site = GameBinaryPatchSite::RfidComPort,
    .rva = 0x002F7AC3U,
    .expected_clean = TestPattern({0x31}),
    .expected_patched = TestPattern({0x32}),
    .actual = TestPattern({0x33}),
}
```

Require title `L"GCLoader unsupported game version"`, exit code `26`, exact log evidence for stage/site/RVA/all three patterns, and modal text stating that every required site must be either clean or already patched. The modal must mention `loader-log.txt`; it no longer tells the user to use only `game_decrypted.exe`.

Revise write/read setup cases to require stage, site, RVA, memory stage, and Windows error while asserting that neither log nor modal contains `rollback` in any casing. Add an `AddressRange` case requiring the setup-error title without a site or Windows error.

- [ ] **Step 2: Run diagnostic tests and verify RED**

```powershell
$vcvars = 'C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat'
cmd.exe /d /s /c "call `"$vcvars`" >nul && cmake --build --preset msvc32-debug --target GameBinaryPatchDiagnosticsTests && ctest --preset msvc32-debug -R ^GameBinaryPatchDiagnosticsTests$"
```

Expected: tests fail because the current formatter emits identity/mixed handling, rollback fields, and the strict `game_decrypted.exe` instruction.

- [ ] **Step 3: Simplify fatal diagnostic formatting**

In `GameBinaryPatchDiagnostics.cpp`:

```cpp
const bool unsupported =
    error.stage == GameBinaryPatchStage::UnknownBytes;
```

Remove identity display/hex helpers and rollback helpers. The log still begins `GameBinaryPatch: startup failed stage=<stage>`, appends site/RVA and byte patterns when present, and appends memory stage/Windows error when present. It must not emit a rollback field.

Use this unsupported modal structure:

```text
This executable does not contain supported bytes at a required GCLoader patch site.

Patch site: <site>
RVA: <zero-padded RVA>

Every required patch site must be either clean or already patched.

See loader-log.txt for the exact byte comparison.
```

The setup modal retains stage/site/RVA/memory stage/Windows error plus the security-software guidance, but removes the rollback line.

- [ ] **Step 4: Build the DLL and run the focused integration slice**

```powershell
$vcvars = 'C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat'
cmd.exe /d /s /c "call `"$vcvars`" >nul && cmake --build --preset msvc32-debug --target iDmacDrv32 GameBinaryPatchTests GameBinaryPatchDiagnosticsTests RendererDeviceLossPatchTests SwitchInputPatchTests && ctest --preset msvc32-debug -R ^(GameBinaryPatchTests^|GameBinaryPatchDiagnosticsTests^|RendererDeviceLossPatchTests^|SwitchInputPatchTests^)$"
```

Expected: the DLL links and all four selected suites pass. Inspect the compiled Debug DLL for ANSI `GameBinaryPatch: state=` and `patched`, plus wide `GCLoader unsupported game version`.

- [ ] **Step 5: Commit revised diagnostics**

```powershell
git add -- src/Patches/GameCompatibility/GameBinaryPatchDiagnostics.cpp tests/Patches/GameBinaryPatchDiagnosticsTests.cpp
git commit -m "Report patch-site compatibility failures"
```

---

### Task 3: Full Static Verification

**Files:**
- Verify: all files changed by Tasks 1 and 2
- Read-only evidence: `H:\gc\game_decrypted.exe`
- Read-only evidence: `H:\gc\game471.exe`

**Interfaces:**
- Consumes: both revision commits.
- Produces: complete Debug/Release build and test evidence, compiled-artifact evidence, unchanged source-executable hashes, and clean feature-branch status.

- [ ] **Step 1: Run the complete Debug graph and suite**

```powershell
$vcvars = 'C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat'
cmd.exe /d /s /c "call `"$vcvars`" >nul && cmake --preset msvc32-debug && cmake --build --preset msvc32-debug && ctest --preset msvc32-debug -j 4"
```

Expected: configuration/build succeed and all Debug tests pass.

- [ ] **Step 2: Run the complete Release graph and suite**

```powershell
$vcvars = 'C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat'
cmd.exe /d /s /c "call `"$vcvars`" >nul && cmake --preset msvc32-release && cmake --build --preset msvc32-release && ctest --preset msvc32-release -j 4"
```

Expected: configuration/build succeed and all Release tests pass.

- [ ] **Step 3: Inspect the Release DLL**

Read `build-msvc32-release\dist\iDmacDrv32.dll` as ANSI and UTF-16LE. Require ANSI markers `GameBinaryPatch: state=`, `patched`, `already_patched`, all four site tokens, and wide title `GCLoader unsupported game version`.

- [ ] **Step 4: Prove runtime evidence was not modified**

```powershell
Get-FileHash -Algorithm SHA256 -LiteralPath `
    'H:\gc\game_decrypted.exe', `
    'H:\gc\game471.exe'
```

Expected:

```text
H:\gc\game_decrypted.exe  795AB03F944BA7716AB257869C6BA394D19288E6484A17FACF1600ED377595DF
H:\gc\game471.exe         FEAD3BD4D0E0985F101965EDC417DD2B96522F8716FF789D84618FEB0D7A2522
```

- [ ] **Step 5: Inspect branch scope**

```powershell
git diff --check
git status --short
git log -6 --oneline
git diff --stat main...HEAD
```

Expected: no whitespace errors, a clean feature branch, only the GameCompatibility implementation/tests, loader bootstrap, and matching design/plan documents in the feature delta.

Runtime acceptance remains separate: clean, legacy-patched, and deliberately partially patched images must be operator-tested later. Do not deploy the DLL or launch the game during this plan.

## Self-Review

- **Spec coverage:** Task 1 removes every PE metadata dependency, accepts all 16 clean/patched combinations, preflights all sites, selectively writes clean sites, immediately stops on write failure, and removes rollback state. Task 2 aligns diagnostics and startup logging. Task 3 covers both build presets, all tests, linked markers, hashes, and scope.
- **Non-goals:** No signature scanning, heuristic addresses, configuration, detours, unrelated refactors, deployment, runtime executable write, or gameplay claim is included.
- **Type consistency:** `PatchedImage`, `AlreadyPatchedImage`, the reduced patch-stage/error model, and unchanged initializer/diagnostic signatures are used consistently across production, tests, diagnostics, and loader logging.
- **Placeholder scan:** Every test case, byte contract, expected callback sequence, error field, command, commit path, and expected result is explicit.

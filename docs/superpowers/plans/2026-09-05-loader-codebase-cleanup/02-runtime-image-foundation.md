# RuntimeImage Foundation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace feature-local executable-memory mechanics with one production-only `RuntimeImage` module and establish the once-only log/popup/abort terminal path used after any post-preflight failure.

**Architecture:** `gc_runtime_image` owns checked RVA resolution, guarded reads, protection changes, writes, cache flushing, protection restoration, read-back verification, and checked pointer exchange. `gc_fatal_process` is a lower-level diagnostics target owning once-only log/popup/abort publication so features never depend upward on Loader. Features own semantic sites and expected bytes. No interface stores rollback bytes or accepts injected memory actions.

**Tech Stack:** C++23, Win32 x86, `std::expected`, PE mapped-image metadata, `ReadProcessMemory`, `WriteProcessMemory`, `VirtualQuery`, `VirtualProtect`, `FlushInstructionCache`, `InterlockedCompareExchangePointer`, CMake/Ninja/MSVC.

**Spec:** `docs/superpowers/specs/2026-09-05-loader-codebase-cleanup-design.md`

## Global Constraints

- Complete Plan 01 first and preserve its baseline commit.
- Do not use TDD or add fake executable memory, injected read/write tables,
  copied binary fixtures, or source-grep tests.
- Revalidate current GameCompatibility and Countdown contracts against
  `H:\gc\game471.exe.i64` before moving them; write the bounded IDA-CLI batch
  script under `.codex-tmp` and disconnect after the batch.
- Preflight every site in a feature plan before the first write. A write
  failure after mutation immediately uses the terminal abort path; it does not
  reverse earlier writes.
- A successful write includes protection restoration and exact read-back.
- Build results are static evidence only. Do not deploy or launch a process.

---

## Task 1: Add common RuntimeImage value types

**Files:**

- Create: `src/Patches/RuntimeImage/BytePattern.h`
- Create: `src/Patches/RuntimeImage/RuntimeImageError.h`
- Create: `src/Patches/RuntimeImage/RuntimeImage.h`
- Create: `src/Patches/RuntimeImage/CMakeLists.txt`
- Modify: `src/Patches/CMakeLists.txt`

**Interfaces:**

- Consumes: the current process's loaded main module and feature-owned compile-
  time contracts.
- Produces:

```cpp
namespace gc::runtime_image {

using Rva = std::uint32_t;
inline constexpr std::size_t kMaximumPatternBytes = 32;

struct BytePattern final {
    std::array<std::byte, kMaximumPatternBytes> bytes{};
    std::uint8_t size{};
    [[nodiscard]] std::span<const std::byte> view() const noexcept;
    friend bool operator==(const BytePattern&, const BytePattern&) = default;
};

template <std::uint8_t... Values>
[[nodiscard]] consteval BytePattern PatternOf() noexcept;

enum class MemoryKind : std::uint8_t { code, data };
enum class MemoryStage : std::uint8_t {
    resolve_module,
    parse_image,
    address_range,
    query,
    read,
    protect,
    write,
    flush_instruction_cache,
    restore_protection,
    read_back,
    compare_exchange,
};

struct SiteIdentity final {
    std::string_view feature;
    std::string_view site;
    Rva rva{};
};

struct RuntimeImageError final {
    MemoryStage stage{};
    SiteIdentity identity{};
    std::uintptr_t address{};
    std::size_t size{};
    BytePattern expected{};
    BytePattern observed{};
    DWORD win32_error{};
    bool memory_changed{};
    bool restore_attempted{};
    bool restore_succeeded{};
};

enum class BytePatchState : std::uint8_t {
    original,
    installed,
    mismatch,
};

struct BytePatch final {
    SiteIdentity identity;
    BytePattern original;
    BytePattern replacement;
    MemoryKind memory_kind{MemoryKind::code};
};

class RuntimeImage final {
public:
    [[nodiscard]] static std::expected<RuntimeImage, RuntimeImageError>
    MainModule() noexcept;

    [[nodiscard]] std::uintptr_t base() const noexcept;
    [[nodiscard]] std::uint32_t size() const noexcept;
    [[nodiscard]] std::expected<std::uintptr_t, RuntimeImageError>
    Resolve(const SiteIdentity&, std::size_t) const noexcept;
    [[nodiscard]] std::expected<BytePattern, RuntimeImageError>
    Read(const SiteIdentity&, std::uint8_t) const noexcept;
    [[nodiscard]] std::expected<BytePatchState, RuntimeImageError>
    Inspect(const BytePatch&) const noexcept;
    [[nodiscard]] std::expected<void, RuntimeImageError>
    Write(const SiteIdentity&, BytePattern replacement, MemoryKind) const noexcept;
    [[nodiscard]] std::expected<void, RuntimeImageError>
    ExchangePointer(
        const SiteIdentity&, void* expected, void* replacement) const noexcept;
};

} // namespace gc::runtime_image
```

`Write` performs no semantic pre-write membership check. The validated plan is
the authorization to write; this is required so the exact-known-hash path can
skip redundant site reads. `Write` still verifies the resulting bytes.

- [ ] **Step 1: Implement `BytePattern` and `PatternOf`**

Reject zero-length patterns and more than 32 bytes with `static_assert`. Keep
formatting out of this header; diagnostics format a `view()` at the edge.

- [ ] **Step 2: Add the RuntimeImage target**

Create `gc_runtime_image` as a static library with public include root
`${PROJECT_SOURCE_DIR}/src` and private `plog` only if the implementation
actually logs. Prefer returning errors without logging from this layer.

- [ ] **Step 3: Compile the empty interface before mechanics**

Run:

```powershell
cmake --preset msvc32-debug
cmake --build --preset msvc32-debug --target gc_runtime_image
```

Expected: the new target compiles with no feature dependencies and no
SafetyHook, reflect-cpp, Loader, or MinHook dependency.

---

## Task 2: Implement production loaded-image access

**Files:**

- Create: `src/Patches/RuntimeImage/RuntimeImage.cpp`
- Modify: `src/Patches/RuntimeImage/CMakeLists.txt`

- [ ] **Step 1: Resolve and validate the loaded main image**

`MainModule()` must:

1. call `GetModuleHandleW(nullptr)`;
2. guarded-read `IMAGE_DOS_HEADER` and `IMAGE_NT_HEADERS32`;
3. require `IMAGE_DOS_SIGNATURE`, `IMAGE_NT_SIGNATURE`, and
   `IMAGE_FILE_MACHINE_I386`;
4. retain the mapped base and `OptionalHeader.SizeOfImage`;
5. reject zero size and overflowing `[base, base + size)`.

Do not use the preferred image base as the loaded base.

- [ ] **Step 2: Implement checked RVA resolution and reads**

`Resolve()` rejects zero size, integer overflow, a span outside
`SizeOfImage`, and pages that are uncommitted, guarded, or inaccessible.
`Read()` uses `ReadProcessMemory(GetCurrentProcess(), ...)`, requires the exact
byte count, and returns the observed fixed-size pattern.

- [ ] **Step 3: Implement a fully checked byte write**

For the exact resolved span:

1. call `VirtualProtect` with `PAGE_EXECUTE_READWRITE` for code and
   `PAGE_READWRITE` for data;
2. call `WriteProcessMemory` and require the exact byte count;
3. flush the instruction cache only for code;
4. restore the captured protection and require success;
5. read back through `ReadProcessMemory` and require exact replacement bytes.

Set `memory_changed` as soon as any byte may have been written. Preserve the
first operation failure plus restoration flags in `RuntimeImageError`. Never
attempt to write original bytes back.

- [ ] **Step 4: Implement checked pointer exchange**

Resolve and validate one pointer-sized data slot, change its protection,
perform `InterlockedCompareExchangePointer(slot, replacement, expected)`,
restore protection, and read the slot back. A mismatched prior pointer is a
`compare_exchange` error with expected/observed pointer bytes. Do not use
`safetyhook::unprotect`; it does not report restoration failure in v0.7.0.

- [ ] **Step 5: Build RuntimeImage in both configurations**

```powershell
cmake --build --preset msvc32-debug --target gc_runtime_image
cmake --build --preset msvc32-release --target gc_runtime_image
```

Expected: both x86 static libraries build with warnings treated according to
the existing project options.

---

## Task 3: Add the terminal fatal-process publisher

**Files:**

- Create: `src/Diagnostics/FatalProcess.h`
- Create: `src/Diagnostics/FatalProcess.cpp`
- Modify: `src/Diagnostics/CMakeLists.txt`
- Modify: `src/Loader/DllMain.cpp`

**Produces:**

```cpp
namespace gc::diagnostics {

struct FatalProcessReport final {
    std::string log;
    std::wstring modal;
    std::wstring title;
};

[[noreturn]] void AbortProcess(FatalProcessReport report) noexcept;

} // namespace gc::diagnostics
```

- [ ] **Step 1: Implement once-only publication without an action table**

Use one process-wide atomic latch. The first caller attempts detailed `PLOG_ERROR`
and `MessageBoxW(MB_OK | MB_ICONERROR)`, with allocation-free fallback literals.
Every caller then invokes `std::abort()`. Do not return `FALSE`, call a reverse
rollback, or expose injected callbacks.

Define the coherent `gc_fatal_process` target and link it only to the logging/
Win32 libraries needed by this implementation. Runtime patch features may
depend downward on it; it must not depend on Loader or any feature target.

- [ ] **Step 2: Route GameCompatibility fatal publication through it**

Keep `BuildGameBinaryPatchFatalDiagnostic()` as typed formatting for now, but
replace the DllMain-local latch and `PublishStartupFatal` call with
`AbortProcess`. Remove the now-dead `PublishGameBinaryPatchFatal` helper only
after its last caller is gone.

- [ ] **Step 3: Build the loader target**

```powershell
cmake --build --preset msvc32-debug --target iDmacDrv32
cmake --build --preset msvc32-release --target iDmacDrv32
```

Expected: both DLLs link; no runtime process is launched.

---

## Task 4: Migrate GameCompatibility to RuntimeImage

**Files:**

- Modify: `src/Patches/GameCompatibility/GameBinaryPatch.h`
- Modify: `src/Patches/GameCompatibility/GameBinaryPatch.cpp`
- Modify: `src/Patches/GameCompatibility/GameBinaryPatchDiagnostics.*`
- Modify: `src/Patches/AutoPlay/AutoPlayPatch.cpp`
- Modify: `src/Patches/AutoPlay/AutoPlayPatchDiagnostics.*`
- Modify: `src/Patches/SongUnlock/SongUnlockPatch.cpp`
- Modify: `src/Patches/CMakeLists.txt`
- Delete after last use: feature-local byte-pattern, memory-stage, memory-error,
  and `GameBinaryPatchActions` declarations/definitions

- [ ] **Step 1: Revalidate the four current sites in IDA**

Require the existing contracts at RVAs `0x000B0896`, `0x00102C7B`,
`0x00103EE6`, and `0x002F7AC3`, including decoded instruction meaning and the
clean/patched bytes already documented in the feature. Stop on any mismatch.

- [ ] **Step 2: Express the manifest using common types**

Keep `GameBinaryPatchSite` for domain diagnostics, but make its manifest an
array of `runtime_image::BytePatch`. Keep the exact current manifest order.

- [ ] **Step 3: Preserve full preflight and remove the injected memory seam**

Resolve one `RuntimeImage` and inspect all four sites before mutation. Accept
only the all-original clean image or the all-installed legacy image; reject a
mixed/partially patched image and any unknown bytes. For the clean image write
all four sites in manifest order. For the legacy image perform no writes. On
the first write error call `AbortProcess` with the precise site and
RuntimeImage error. Do not restore prior successful sites.

- [ ] **Step 4: Remove superseded mechanics**

Delete `ProductionGameBinaryPatchActions`, local address/read/write helpers,
local `VirtualProtect`/copy/flush code, and memory-stage name duplication.
In AutoPlay and SongUnlock, replace calls through that deleted action table
with `RuntimeImage::Read`, `Inspect`, and `Write`, and replace
`AutoPlayBytePattern` with `runtime_image::BytePattern`. Keep their current
feature-owned RVAs, ordering, hook ownership, and diagnostics until Plan 06a.

---

## Task 5: Migrate Countdown without an unpatch path

**Files:**

- Modify: `src/Patches/Countdown/CountdownTimerFreeze.h`
- Modify: `src/Patches/Countdown/CountdownTimerFreeze.cpp`
- Modify: `src/Patches/Framerate/FrameratePatch.cpp`
- Modify: `src/Patches/CMakeLists.txt`

- [ ] **Step 1: Revalidate the 32 call sites and target**

Use IDA to require every current call RVA targets
`kRvaGlobalFrameDeltaSeconds`, every return RVA equals call RVA plus five, and
the replacement remains `D9 EE 90 90 90`. Record any discrepancy rather than
partially accepting the table.

- [ ] **Step 2: Replace mutable enable/disable state with one startup plan**

Change the public entry point to:

```cpp
[[nodiscard]] std::expected<void, runtime_image::RuntimeImageError>
InstallCountdownTimerFreeze(
    const runtime_image::RuntimeImage& image,
    bool enabled) noexcept;
```

When disabled, contribute no writes. When enabled, derive all 32 original
`CALL rel32` patterns and inspect all sites before the first write. Accept only
all-original or all-installed state; reject a mixed/partially patched set.
Then write all 32 replacements in manifest order when original. Remove
`SetCountdownTimerFreezeEnabled`,
the reverse/unpatch branch, mutable `patches_applied`, and best-effort loops.

- [ ] **Step 3: Make a failed write terminal**

Propagate the typed error to the framerate startup formatter and call
`AbortProcess`. No failure may merely log and return to DllMain after a write
could have changed memory.

---

## Task 6: Verify and commit the foundation

- [ ] **Step 1: Audit duplicate mechanics**

Run:

```powershell
rg -n 'GameBinaryPatchActions|ProductionGameBinaryPatchActions|GameBinaryMemoryStage|make_writable|apply_countdown_timer_freeze_patches|Rollback' src\Patches\GameCompatibility src\Patches\Countdown
rg -n 'VirtualProtect|FlushInstructionCache|ReadProcessMemory|WriteProcessMemory|InterlockedCompareExchangePointer' src\Patches\RuntimeImage src\Patches\GameCompatibility src\Patches\Countdown
```

Expected: the first command has no superseded memory/rollback implementation;
the second shows memory mechanics only in RuntimeImage for these features.

- [ ] **Step 2: Run complete static verification**

```powershell
cmake --build --preset msvc32-debug
ctest --preset msvc32-debug -j 4
cmake --build --preset msvc32-release
ctest --preset msvc32-release -j 4
git diff --check
git status --short --branch
```

Do not describe these results as patch or countdown runtime success.

- [ ] **Step 3: Commit**

```powershell
git add -- src\Patches\RuntimeImage src\Patches\GameCompatibility src\Patches\AutoPlay src\Patches\SongUnlock src\Patches\Countdown src\Patches\Framerate\FrameratePatch.cpp src\Diagnostics\FatalProcess.h src\Diagnostics\FatalProcess.cpp src\Diagnostics\CMakeLists.txt src\Loader\DllMain.cpp src\CMakeLists.txt src\Patches\CMakeLists.txt
git commit -m "Add shared runtime image foundation"
```

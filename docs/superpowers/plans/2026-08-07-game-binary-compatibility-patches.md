# Game Binary Compatibility Patches Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make GCLoader recreate the four intentional `game471.exe` modifications in memory when the clean supported `game_decrypted.exe` starts, while accepting the exact legacy-prepatched state and rejecting every unsupported executable state with a clear prompt.

**Architecture:** Add one game-only bootstrap patch unit with a private four-site manifest, strict PE identity validation, whole-image clean/prepatched classification, and an injected transactional memory API. Add a small diagnostic formatter for structured failures, then invoke the patch unit immediately after game-process log initialization and before every other game mutation.

**Tech Stack:** C++23, Win32 x86 (`IMAGE_NT_HEADERS32`, `VirtualProtect`, `FlushInstructionCache`), `std::expected`, CMake/Ninja/MSVC presets, the existing `SystemPath/StartupFatal` publisher, and plain executable CTest targets.

## Global Constraints

- Work only in `H:\gc\artifacts\GCLoader`; `H:\gc` is read-only runtime and binary evidence unless the user separately authorizes deployment.
- Build from an x86 MSVC developer environment and use the checked-in `msvc32-debug` and `msvc32-release` presets.
- Support only the x86 PE with timestamp `0x5FA90825`, preferred image base `0x00400000`, entry RVA `0x0010964A`, image size `0x00433000`, header size `0x00000400`, and five sections.
- Recreate exactly four sites: RVA `0x000B0896` (`75 02` to `90 90`), RVA `0x00102C7B` (`75 3B` to `EB 3B`), RVA `0x00103EE6` (`E8 45 F6 FF FF` to five NOPs), and RVA `0x002F7AC3` (`31` to `32`).
- Treat only the complete all-clean and complete all-prepatched states as supported; reject mixed or unknown site states before writing.
- Never replay the memory dump's resolved IAT or any writable `.data` difference.
- The patch set is required and has no configuration switch.
- Patch only the game process; never inspect or mutate the NESYS process image.
- Preflight every site before mutation, use checked address arithmetic, and roll back every possibly written site on failure.
- Do not let exceptions cross `DllMain`, a Win32 callback, or an injected memory action.
- Do not add source-text/regex tests or a test-owned copy of the production manifest. The small test fixture must state that its values were independently derived from the two hashed executables in the design spec.
- Build/static verification and operator-run game acceptance remain separate. Do not deploy, launch the game, or claim runtime success while executing this plan.

---

## File Structure

- Create `src/Patches/GameCompatibility/GameBinaryPatch.h` for the public result/error model, injected memory-action interface, name helpers, installer seam, and production initializer.
- Create `src/Patches/GameCompatibility/GameBinaryPatch.cpp` for the private PE identity and four-site manifest, state classification, transaction/rollback, guarded production reads, and checked Win32 writes.
- Create `src/Patches/GameCompatibility/GameBinaryPatchDiagnostics.h` for the allocation-owning fatal diagnostic value and formatter declaration.
- Create `src/Patches/GameCompatibility/GameBinaryPatchDiagnostics.cpp` for unsupported-version versus setup-failure log/modal/title formatting.
- Create `tests/Patches/GameBinaryPatchTests.cpp` for independently derived PE/site fixtures and installer transaction behavior.
- Create `tests/Patches/GameBinaryPatchDiagnosticsTests.cpp` for exact user-facing failure classification and required evidence fields.
- Modify `src/Patches/CMakeLists.txt` to compile both new production `.cpp` files into `gc_runtime_patches`.
- Modify `tests/Patches/CMakeLists.txt` to register both focused test executables.
- Modify `src/Loader/DllMain.cpp` to run the game-only bootstrap before locale and every later game feature, publish fatal diagnostics, and log the successful state.

The feature is one subsystem: all files implement or verify the same supported-image bootstrap contract. No decomposition into separate COM, mouse, or dongle plans is needed because partial installation is explicitly forbidden.

---

### Task 1: Supported-Image Classifier and Transactional Patch Engine

**Files:**
- Create: `src/Patches/GameCompatibility/GameBinaryPatch.h`
- Create: `src/Patches/GameCompatibility/GameBinaryPatch.cpp`
- Create: `tests/Patches/GameBinaryPatchTests.cpp`
- Modify: `src/Patches/CMakeLists.txt:1-17`
- Modify: `tests/Patches/CMakeLists.txt:1-58`

**Interfaces:**
- Consumes: an actual loaded-image base plus injected memory actions, or `GetModuleHandleW(nullptr)` through the production initializer.
- Produces:

```cpp
namespace gc::game_compatibility {

inline constexpr std::size_t kGameBinaryPatchSiteCount = 4;
inline constexpr std::size_t kMaximumGameBinaryPatternBytes = 5;

struct GameBinaryBytePattern {
    std::array<std::byte, kMaximumGameBinaryPatternBytes> bytes{};
    std::uint8_t size{};

    [[nodiscard]] std::span<const std::byte> view() const noexcept;
    friend bool operator==(
        const GameBinaryBytePattern&,
        const GameBinaryBytePattern&) = default;
};

enum class GameBinaryPatchSite {
    None,
    NativeMouseEvents,
    DongleFailure,
    DongleSecurityTransmit,
    RfidComPort,
};

enum class GameBinaryImageState {
    PatchedCleanImage,
    AlreadyPatchedImage,
};

enum class GameBinaryIdentityField {
    None,
    DosMagic,
    NtSignature,
    OptionalHeaderMagic,
    Machine,
    Timestamp,
    PreferredImageBase,
    EntryPointRva,
    SizeOfImage,
    SizeOfHeaders,
    SectionCount,
};

enum class GameBinaryMemoryStage {
    None,
    Read,
    Protect,
    Copy,
    FlushInstructionCache,
    RestoreProtection,
};

enum class GameBinaryPatchStage {
    None,
    ResolveModule,
    InvalidActions,
    HeaderRead,
    IdentityMismatch,
    AddressRange,
    SiteRead,
    UnknownBytes,
    MixedState,
    SiteWrite,
};

struct GameBinaryMemoryError {
    GameBinaryMemoryStage stage{GameBinaryMemoryStage::None};
    DWORD win32_error{};
};

using GameBinaryMemoryResult =
    std::expected<void, GameBinaryMemoryError>;

struct GameBinaryPatchActions {
    void* context{};
    GameBinaryMemoryResult (*read)(
        void*,
        std::uintptr_t,
        std::span<std::byte>) noexcept{};
    GameBinaryMemoryResult (*write)(
        void*,
        std::uintptr_t,
        std::span<const std::byte>) noexcept{};
};

struct GameBinaryPatchError {
    GameBinaryPatchStage stage{GameBinaryPatchStage::None};
    GameBinaryPatchSite site{GameBinaryPatchSite::None};
    GameBinaryIdentityField identity_field{GameBinaryIdentityField::None};
    std::uint32_t rva{};
    std::uint64_t expected_identity{};
    std::uint64_t actual_identity{};
    GameBinaryBytePattern expected_clean{};
    GameBinaryBytePattern expected_patched{};
    GameBinaryBytePattern actual{};
    GameBinaryMemoryStage memory_stage{GameBinaryMemoryStage::None};
    DWORD win32_error{};
    bool rollback_attempted{};
    bool rollback_complete{};
};

struct GameBinaryPatchResult {
    GameBinaryImageState state{};
    std::size_t site_count{};
};

[[nodiscard]] std::expected<
    GameBinaryPatchResult,
    GameBinaryPatchError>
InstallGameBinaryPatch(
    std::uintptr_t image_base,
    GameBinaryPatchActions actions) noexcept;

[[nodiscard]] GameBinaryPatchActions
ProductionGameBinaryPatchActions() noexcept;

[[nodiscard]] std::expected<
    GameBinaryPatchResult,
    GameBinaryPatchError>
GameBinaryPatchInit() noexcept;

[[nodiscard]] const char* GameBinaryPatchStageName(
    GameBinaryPatchStage stage) noexcept;
[[nodiscard]] const char* GameBinaryPatchSiteName(
    GameBinaryPatchSite site) noexcept;
[[nodiscard]] const char* GameBinaryIdentityFieldName(
    GameBinaryIdentityField field) noexcept;
[[nodiscard]] const char* GameBinaryMemoryStageName(
    GameBinaryMemoryStage stage) noexcept;
[[nodiscard]] const char* GameBinaryImageStateName(
    GameBinaryImageState state) noexcept;

} // namespace gc::game_compatibility
```

- [ ] **Step 1: Register the focused test target and write the independent fixture**

Append to `tests/Patches/CMakeLists.txt`:

```cmake
add_executable(GameBinaryPatchTests
        GameBinaryPatchTests.cpp)
target_link_libraries(GameBinaryPatchTests PRIVATE
        gc_runtime_patches)
add_test(NAME GameBinaryPatchTests
        COMMAND GameBinaryPatchTests)
```

Create `tests/Patches/GameBinaryPatchTests.cpp`. Include the production header,
`Windows.h`, `<algorithm>`, `<array>`, `<cstddef>`, `<cstdint>`, `<cstring>`,
`<initializer_list>`, `<optional>`, `<span>`, and `<vector>`. The fixture must allocate a
`0x00433000`-byte vector and use a synthetic base such as `0x10000000`; its
read/write callbacks translate `address - base` into the vector only after
overflow and bounds checks.

Define the byte helper and fake memory seam explicitly:

```cpp
constexpr std::uintptr_t kFakeBase = 0x10000000U;
constexpr std::size_t kSupportedImageSize = 0x00433000U;
constexpr std::uint32_t kNtHeaderOffset = 0x138U;

std::vector<std::byte> Bytes(
    std::initializer_list<std::uint8_t> values) {
    std::vector<std::byte> result;
    result.reserve(values.size());
    for (const auto value : values) {
        result.push_back(static_cast<std::byte>(value));
    }
    return result;
}

struct WriteFailure {
    std::size_t call{};
    bool after_copy{};
};

struct FakeImage {
    std::uintptr_t base{kFakeBase};
    std::vector<std::byte> bytes =
        std::vector<std::byte>(kSupportedImageSize);
    std::optional<std::uintptr_t> failed_read_address{};
    std::vector<WriteFailure> write_failures{};
    std::vector<std::uintptr_t> read_addresses{};
    std::vector<std::uintptr_t> write_addresses{};
    std::size_t write_calls{};

    template <typename T>
    void Store(std::size_t offset, const T& value) {
        std::memcpy(bytes.data() + offset, &value, sizeof(value));
    }

    void StoreBytes(
        std::uint32_t rva,
        std::span<const std::byte> value) {
        std::copy(value.begin(), value.end(), bytes.begin() + rva);
    }
};

GameBinaryMemoryResult FakeRead(
    void* opaque,
    std::uintptr_t address,
    std::span<std::byte> output) noexcept;

GameBinaryMemoryResult FakeWrite(
    void* opaque,
    std::uintptr_t address,
    std::span<const std::byte> input) noexcept;

GameBinaryPatchActions FakeActions(FakeImage& fake) noexcept {
    return {
        .context = &fake,
        .read = FakeRead,
        .write = FakeWrite,
    };
}
```

`FakeRead` and `FakeWrite` both reject `address < fake.base`, checked-subtract
the base, and reject `offset > bytes.size()` or a span larger than the remaining
bytes. `FakeRead` records the address before consulting `failed_read_address`.
`FakeWrite` increments and records its one-based call number, checks
`write_failures`, optionally fails before copying or copies then fails, and
otherwise copies the complete span. Failures return `Read` or `Copy` with
`ERROR_NOACCESS`; this lets tests distinguish a failed forward write from the
rollback calls that follow it.

Build the supported headers with real Win32 structures:

```cpp
void WriteSupportedHeaders(FakeImage& fake) {
    IMAGE_DOS_HEADER dos{};
    dos.e_magic = IMAGE_DOS_SIGNATURE;
    dos.e_lfanew = kNtHeaderOffset;
    fake.Store(0, dos);

    IMAGE_NT_HEADERS32 nt{};
    nt.Signature = IMAGE_NT_SIGNATURE;
    nt.FileHeader.Machine = IMAGE_FILE_MACHINE_I386;
    nt.FileHeader.NumberOfSections = 5;
    nt.FileHeader.TimeDateStamp = 0x5FA90825U;
    nt.FileHeader.SizeOfOptionalHeader =
        sizeof(IMAGE_OPTIONAL_HEADER32);
    nt.OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR32_MAGIC;
    nt.OptionalHeader.ImageBase = 0x00400000U;
    nt.OptionalHeader.AddressOfEntryPoint = 0x0010964AU;
    nt.OptionalHeader.SizeOfImage = 0x00433000U;
    nt.OptionalHeader.SizeOfHeaders = 0x00000400U;
    fake.Store(kNtHeaderOffset, nt);
}
```

Keep the independent fixture local to the test and explain its provenance in a
comment:

```cpp
// Independently extracted from SHA-256
// 795AB03F944BA7716AB257869C6BA394D19288E6484A17FACF1600ED377595DF
// (clean) and
// FEAD3BD4D0E0985F101965EDC417DD2B96522F8716FF789D84618FEB0D7A2522
// (legacy patched).
// Do not replace these values by importing the production manifest.
struct FixtureSite {
    std::uint32_t rva;
    std::vector<std::byte> clean;
    std::vector<std::byte> patched;
};

const std::array<FixtureSite, 4> kFixtureSites{
    FixtureSite{0x000B0896U, Bytes({0x75, 0x02}),
                Bytes({0x90, 0x90})},
    FixtureSite{0x00102C7BU, Bytes({0x75, 0x3B}),
                Bytes({0xEB, 0x3B})},
    FixtureSite{0x00103EE6U,
                Bytes({0xE8, 0x45, 0xF6, 0xFF, 0xFF}),
                Bytes({0x90, 0x90, 0x90, 0x90, 0x90})},
    FixtureSite{0x002F7AC3U, Bytes({0x31}), Bytes({0x32})},
};
```

Implement these exact behavioral cases in `main()` using explicit assertions
and a failure count:

```cpp
failures += TestCleanImageWritesAllFourSites();
failures += TestLegacyPatchedImageWritesNothing();
failures += TestEveryIdentityFieldMismatchWritesNothing();
failures += TestUnknownBytesAtEverySiteWriteNothing();
failures += TestMixedImageWritesNothing();
failures += TestHeaderAndSiteReadFailuresWriteNothing();
failures += TestInvalidActionsAndAddressRangesTouchNothing();
failures += TestEveryForwardWriteFailureRollsBackToClean();
failures += TestCurrentFailedWriteIsIncludedInRollback();
failures += TestRollbackFailureIsReportedAndNeverSucceeds();
failures += TestSecondInstallIsAlreadyPatchedNoOp();
return failures == 0 ? 0 : 1;
```

The fake write callback records every attempted address before applying its
configured failure. This makes a failure at forward write `N` followed by
reverse rollback calls independently observable. After every failure case,
compare all four image ranges with their clean fixture bytes. For the dedicated
rollback-failure case, fail one rollback callback, require
`rollback_attempted == true` and `rollback_complete == false`, and verify the
installer still attempted restoration of every earlier site.

- [ ] **Step 2: Run the new target and verify the red state**

Run from an x86 MSVC developer PowerShell:

```powershell
cmake --preset msvc32-debug
cmake --build --preset msvc32-debug --target GameBinaryPatchTests
```

Expected: build fails because
`Patches/GameCompatibility/GameBinaryPatch.h` and the production symbols do not
exist yet. A passing test or a failure caused only by a stale compiler
environment is not the expected red state.

- [ ] **Step 3: Create the public contract exactly once**

Create `src/Patches/GameCompatibility/GameBinaryPatch.h` with the complete
interface in the **Interfaces** block above. Include `Windows.h`, `<array>`,
`<cstddef>`, `<cstdint>`, `<expected>`, and `<span>`. Implement
`GameBinaryBytePattern::view()` inline as:

```cpp
return {bytes.data(), static_cast<std::size_t>(size)};
```

Do not expose the four-site manifest or supported PE constants from the header;
tests must remain an independent oracle.

- [ ] **Step 4: Implement strict PE validation and whole-image classification**

Create `src/Patches/GameCompatibility/GameBinaryPatch.cpp`. Define a private
`PatchContract` and this exact manifest:

```cpp
struct PatchContract {
    GameBinaryPatchSite site{};
    std::uint32_t rva{};
    GameBinaryBytePattern clean{};
    GameBinaryBytePattern patched{};
};

constexpr std::array<PatchContract, kGameBinaryPatchSiteCount> kContracts{
    PatchContract{
        GameBinaryPatchSite::NativeMouseEvents,
        0x000B0896U,
        Pattern<2>({0x75, 0x02}),
        Pattern<2>({0x90, 0x90}),
    },
    PatchContract{
        GameBinaryPatchSite::DongleFailure,
        0x00102C7BU,
        Pattern<2>({0x75, 0x3B}),
        Pattern<2>({0xEB, 0x3B}),
    },
    PatchContract{
        GameBinaryPatchSite::DongleSecurityTransmit,
        0x00103EE6U,
        Pattern<5>({0xE8, 0x45, 0xF6, 0xFF, 0xFF}),
        Pattern<5>({0x90, 0x90, 0x90, 0x90, 0x90}),
    },
    PatchContract{
        GameBinaryPatchSite::RfidComPort,
        0x002F7AC3U,
        Pattern<1>({0x31}),
        Pattern<1>({0x32}),
    },
};
```

Define the helper before the manifest so every size is checked at compile time:

```cpp
template <std::size_t Size>
constexpr GameBinaryBytePattern Pattern(
    std::array<std::uint8_t, Size> values) noexcept {
    static_assert(Size > 0);
    static_assert(Size <= kMaximumGameBinaryPatternBytes);
    GameBinaryBytePattern pattern{};
    pattern.size = static_cast<std::uint8_t>(Size);
    for (std::size_t index = 0; index < Size; ++index) {
        pattern.bytes[index] = static_cast<std::byte>(values[index]);
    }
    return pattern;
}
```

Validation order is deterministic:

1. Reject null actions or a zero image base as `InvalidActions` without reads.
2. Read `IMAGE_DOS_HEADER`; map an action failure to `HeaderRead` with its
   `memory_stage` and `win32_error`.
3. Require `e_magic == IMAGE_DOS_SIGNATURE`; require nonnegative `e_lfanew`,
   checked `base + e_lfanew` arithmetic, and the complete
   `IMAGE_NT_HEADERS32` range below the supported `0x400`-byte header bound.
4. Read `IMAGE_NT_HEADERS32` and validate, in order, signature, optional-header
   magic, machine, timestamp, preferred image base, entry RVA, image size,
   header size, and section count.
5. Report the first mismatch as `IdentityMismatch` with the exact
   `GameBinaryIdentityField`, expected numeric value, and actual numeric value.
6. Require every full `base + rva + size` range to fit within the validated
   `0x00433000` image and within `std::uintptr_t`.
7. Read all four sites into an array before writing. Map failure to `SiteRead`
   with the exact site/RVA.
8. If a site matches neither pattern, return `UnknownBytes` with both expected
   patterns and the actual pattern.
9. If known site states are mixed, return `MixedState` at the first site whose
   clean/prepatched state differs from site zero.
10. If all four are prepatched, return
    `{AlreadyPatchedImage, kGameBinaryPatchSiteCount}` with zero writes.

Use `std::equal` on the exact `view()` lengths. Never compare the unused tail of
`GameBinaryBytePattern`.

- [ ] **Step 5: Implement the all-clean transaction and exhaustive rollback**

For an all-clean image, set `possibly_applied = index + 1` before invoking each
write callback. If the write fails:

1. Preserve `SiteWrite`, the primary site/RVA, memory stage, and Windows error.
2. Set `rollback_attempted = true`.
3. Call the injected write action for every possibly applied contract in reverse
   order, including the current failed write.
4. Continue rollback even after one restore callback fails.
5. Re-read every possibly applied range and require its exact clean pattern.
6. Set `rollback_complete` only when every restore call and verification read
   succeeds.
7. Return the original `SiteWrite` error; never convert incomplete rollback to
   success.

After four successful writes, return
`{PatchedCleanImage, kGameBinaryPatchSiteCount}`. A second installer call sees
all four patched patterns and performs no additional writes.

- [ ] **Step 6: Implement guarded production memory actions and initializer**

The production read action rejects zero/empty input, then copies through SEH:

```cpp
__try {
    std::memcpy(
        output.data(),
        reinterpret_cast<const void*>(address),
        output.size());
    return {};
} __except (EXCEPTION_EXECUTE_HANDLER) {
    return std::unexpected(GameBinaryMemoryError{
        .stage = GameBinaryMemoryStage::Read,
        .win32_error = ERROR_NOACCESS,
    });
}
```

The production write action performs `VirtualProtect` to
`PAGE_EXECUTE_READWRITE`, guarded `memcpy`, `FlushInstructionCache`, and
restoration of the exact previous protection. It must attempt protection
restoration after copy or flush failure. Return the most safety-relevant stage
in this precedence: failed initial protection, failed guarded copy, failed
cache flush, failed protection restoration. When restoration also fails after a
copy/flush failure, report `RestoreProtection` and that `GetLastError()` value;
the outer transaction still rolls the current site back.

`GameBinaryPatchInit()` resolves the main module, returning `ResolveModule` and
`GetLastError()` if unavailable, then calls:

```cpp
return InstallGameBinaryPatch(
    reinterpret_cast<std::uintptr_t>(module),
    ProductionGameBinaryPatchActions());
```

Implement every enum name helper with exhaustive switches and stable lowercase
tokens. Required result tokens are `patched_clean` and `already_patched`; stage
and site tokens use the enum spelling converted to snake case.

- [ ] **Step 7: Add the implementation to the runtime patch library**

Add this entry near the top of `src/Patches/CMakeLists.txt`'s
`gc_runtime_patches` source list:

```cmake
        GameCompatibility/GameBinaryPatch.cpp
```

Do not create a new library and do not add a dependency: the existing target
already has the needed Win32 and C++ runtime context.

- [ ] **Step 8: Build and run the focused tests in both configurations**

Run:

```powershell
cmake --preset msvc32-debug
cmake --build --preset msvc32-debug --target GameBinaryPatchTests
ctest --preset msvc32-debug -R '^GameBinaryPatchTests$'

cmake --preset msvc32-release
cmake --build --preset msvc32-release --target GameBinaryPatchTests
ctest --preset msvc32-release -R '^GameBinaryPatchTests$'
```

Expected: both selected CTest runs report `GameBinaryPatchTests` passed. The
transaction tests independently prove zero-write preflight rejection, clean
installation, legacy no-op, current-site rollback, reverse rollback, and
rollback-failure reporting.

- [ ] **Step 9: Commit the patch engine**

Run:

```powershell
git add -- src/Patches/GameCompatibility/GameBinaryPatch.h src/Patches/GameCompatibility/GameBinaryPatch.cpp src/Patches/CMakeLists.txt tests/Patches/GameBinaryPatchTests.cpp tests/Patches/CMakeLists.txt
git commit -m "Add transactional game binary compatibility patches"
```

Expected: one commit containing only the patch engine, its CMake registration,
and its behavioral tests.

---

### Task 2: Unsupported-Version Diagnostics and Loader Bootstrap

**Files:**
- Create: `src/Patches/GameCompatibility/GameBinaryPatchDiagnostics.h`
- Create: `src/Patches/GameCompatibility/GameBinaryPatchDiagnostics.cpp`
- Create: `tests/Patches/GameBinaryPatchDiagnosticsTests.cpp`
- Modify: `src/Patches/CMakeLists.txt:1-18`
- Modify: `tests/Patches/CMakeLists.txt:1-63`
- Modify: `src/Loader/DllMain.cpp:14-26,102-250,255-280`

**Interfaces:**
- Consumes: `GameBinaryPatchError`, `GameBinaryPatchResult`,
  `GameBinaryPatchInit()`, the enum name helpers from Task 1, process-role
  detection, process logging, and `gc::system_path::PublishStartupFatal`.
- Produces:

```cpp
namespace gc::game_compatibility {

struct GameBinaryPatchFatalDiagnostic {
    std::string log;
    std::wstring modal;
    std::wstring title;
    DWORD exit_code{26};
};

[[nodiscard]] GameBinaryPatchFatalDiagnostic
BuildGameBinaryPatchFatalDiagnostic(
    const GameBinaryPatchError& error);

} // namespace gc::game_compatibility
```

- [ ] **Step 1: Write failing diagnostic contract tests**

Append to `tests/Patches/CMakeLists.txt`:

```cmake
add_executable(GameBinaryPatchDiagnosticsTests
        GameBinaryPatchDiagnosticsTests.cpp)
target_link_libraries(GameBinaryPatchDiagnosticsTests PRIVATE
        gc_runtime_patches)
add_test(NAME GameBinaryPatchDiagnosticsTests
        COMMAND GameBinaryPatchDiagnosticsTests)
```

Create `tests/Patches/GameBinaryPatchDiagnosticsTests.cpp`; include both new
production headers plus `<cassert>`, `<cstddef>`, `<cstdint>`,
`<initializer_list>`, and `<string>`. Construct errors directly rather than
invoking memory writes. Cover these exact cases:

```cpp
failures += TestIdentityMismatchUsesUnsupportedVersionPrompt();
failures += TestUnknownBytesShowsSiteRvaAndAllPatterns();
failures += TestMixedStateUsesUnsupportedVersionPrompt();
failures += TestWriteFailureUsesSetupPromptAndRollbackEvidence();
failures += TestReadFailureUsesSetupPromptAndWindowsError();
return failures == 0 ? 0 : 1;
```

Define a test-local pattern helper; the private production manifest remains
inaccessible:

```cpp
GameBinaryBytePattern TestPattern(
    std::initializer_list<std::uint8_t> values) {
    assert(!values.empty());
    assert(values.size() <= kMaximumGameBinaryPatternBytes);
    GameBinaryBytePattern pattern{};
    pattern.size = static_cast<std::uint8_t>(values.size());
    std::size_t index = 0;
    for (const auto value : values) {
        pattern.bytes[index++] = static_cast<std::byte>(value);
    }
    return pattern;
}
```

The unknown-byte fixture must set:

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

Require title `L"GCLoader unsupported game version"`, exit code `26`, log
fields `stage=unknown_bytes`, `site=rfid_com_port`, `rva=0x002f7ac3`,
`expected_clean=31`, `expected_patched=32`, and `actual=33`, plus modal text
that names `game_decrypted.exe` and `loader-log.txt`.

The write-failure fixture must set `stage=SiteWrite`,
`memory_stage=RestoreProtection`, `win32_error=5`,
`rollback_attempted=true`, and `rollback_complete=false`. Require title
`L"GCLoader game patch setup error"` and both the rollback failure and Windows
error in log/modal output.

- [ ] **Step 2: Run the diagnostic target and verify the red state**

Run:

```powershell
cmake --build --preset msvc32-debug --target GameBinaryPatchDiagnosticsTests
```

Expected: build fails because `GameBinaryPatchDiagnostics.h` and
`BuildGameBinaryPatchFatalDiagnostic` do not exist.

- [ ] **Step 3: Implement deterministic diagnostic formatting**

Create the header with the interface above and implement the formatter in
`GameBinaryPatchDiagnostics.cpp` with `std::ostringstream` and
`std::wostringstream`.

Classify only these stages as an unsupported executable:

```cpp
const bool unsupported =
    error.stage == GameBinaryPatchStage::IdentityMismatch ||
    error.stage == GameBinaryPatchStage::UnknownBytes ||
    error.stage == GameBinaryPatchStage::MixedState;
```

Unsupported modal structure:

```text
This GCLoader build supports only the verified decrypted Groove Coaster executable.

[Identity field and expected/actual values, or patch site and zero-padded RVA]

Use the supported game_decrypted.exe and remove other executable modifications.

See loader-log.txt for the exact comparison.
```

Setup-failure modal structure:

```text
GCLoader could not apply the required in-memory game patches.

Stage: <stage>
Patch site: <site when present>
RVA: <zero-padded RVA when present>
Windows error: <decimal value when nonzero>
Rollback: not attempted | complete | incomplete

Check loader-log.txt and verify that security software is not blocking executable-memory changes.
```

Render byte patterns as contiguous two-digit lowercase hexadecimal with no
separators, render RVAs as `0x` plus eight lowercase hexadecimal digits, and
render identity values with a `0x` prefix. The log must always begin
`GameBinaryPatch: startup failed` and include every structured field that is
applicable. Let allocation failure throw to the `DllMain` fallback; do not hide
partial strings inside this formatter.

- [ ] **Step 4: Compile diagnostics into `gc_runtime_patches` and run both focused tests**

Add this source immediately after `GameBinaryPatch.cpp` in
`src/Patches/CMakeLists.txt`:

```cmake
        GameCompatibility/GameBinaryPatchDiagnostics.cpp
```

Run:

```powershell
cmake --build --preset msvc32-debug --target GameBinaryPatchTests GameBinaryPatchDiagnosticsTests
ctest --preset msvc32-debug -R '^GameBinaryPatch(Tests|DiagnosticsTests)$'
```

Expected: both focused tests pass.

- [ ] **Step 5: Add the one-shot fatal publisher to `DllMain.cpp`**

Add includes for both new headers with the existing patch includes. Add a
namespace-local function before the other fatal publishers:

```cpp
void PublishGameBinaryPatchFatal(
    const gc::game_compatibility::GameBinaryPatchError& error) noexcept {
    static std::atomic_bool published{false};
    constexpr DWORD exit_code = 26;

    try {
        const auto diagnostic =
            gc::game_compatibility::
                BuildGameBinaryPatchFatalDiagnostic(error);
        gc::system_path::PublishStartupFatal(
            published,
            diagnostic.log,
            diagnostic.modal,
            diagnostic.title,
            diagnostic.exit_code);
        return;
    } catch (...) {
    }

    gc::system_path::PublishStartupFatal(
        published,
        "GameBinaryPatch: startup failure formatting failed",
        L"GCLoader could not validate or patch the game executable. "
        L"Check loader-log.txt for details.",
        L"GCLoader game patch setup error",
        exit_code);
}
```

This follows the existing startup-fatal sequence: log, modal, terminate, and
fail-fast fallback. Do not call `MessageBoxW` directly.

- [ ] **Step 6: Invoke the patch before every other game mutation**

Immediately after `InitProcessLog(role)` and before
`InstallJapaneseLocaleCompatibility(role)`, add:

```cpp
if (gc::nesys_service::ShouldRunGameOnlyInitialization(role)) {
    const auto game_binary_patch =
        gc::game_compatibility::GameBinaryPatchInit();
    if (!game_binary_patch) {
        PublishGameBinaryPatchFatal(game_binary_patch.error());
        return FALSE;
    }
    PLOG_INFO
        << "GameBinaryPatch: state="
        << gc::game_compatibility::GameBinaryImageStateName(
               game_binary_patch->state)
        << " sites=" << game_binary_patch->site_count;
}
```

The existing NESYS branch reaches locale compatibility without calling the
patch initializer. Do not move configuration parsing, crash-dump setup, RFID,
renderer, audio, framerate, or Switch input logic.

- [ ] **Step 7: Build the DLL and run the complete focused slice in Debug**

Run:

```powershell
cmake --build --preset msvc32-debug --target iDmacDrv32 GameBinaryPatchTests GameBinaryPatchDiagnosticsTests RendererDeviceLossPatchTests SwitchInputPatchTests
ctest --preset msvc32-debug -R '^(GameBinaryPatchTests|GameBinaryPatchDiagnosticsTests|RendererDeviceLossPatchTests|SwitchInputPatchTests)$'
```

Expected: the DLL links and all four selected tests pass. This proves the new
bootstrap integrates without breaking the two nearest guarded runtime-patch
families; it does not prove game startup.

- [ ] **Step 8: Inspect the Debug artifact, not source text**

Run:

```powershell
$artifactPath = 'build-msvc32-debug\dist\iDmacDrv32.dll'
$artifactBytes = [System.IO.File]::ReadAllBytes($artifactPath)
$artifactAnsi = [System.Text.Encoding]::ASCII.GetString($artifactBytes)
$artifactWide = [System.Text.Encoding]::Unicode.GetString($artifactBytes)
if (-not $artifactAnsi.Contains('GameBinaryPatch: state=')) {
    throw 'Missing GameBinaryPatch state marker in Debug DLL'
}
if (-not $artifactWide.Contains('GCLoader unsupported game version')) {
    throw 'Missing unsupported-version prompt in Debug DLL'
}
```

Expected: the command returns without throwing. This is compiled-artifact
evidence, not a source-text test.

- [ ] **Step 9: Commit diagnostics and startup integration**

Run:

```powershell
git add -- src/Patches/GameCompatibility/GameBinaryPatchDiagnostics.h src/Patches/GameCompatibility/GameBinaryPatchDiagnostics.cpp src/Patches/CMakeLists.txt src/Loader/DllMain.cpp tests/Patches/GameBinaryPatchDiagnosticsTests.cpp tests/Patches/CMakeLists.txt
git commit -m "Initialize supported game binary patches at startup"
```

Expected: one commit containing the formatter, user prompt, first-game-mutation
bootstrap call, artifact marker, and diagnostic tests.

---

### Task 3: Full Static Verification and Runtime-Acceptance Handoff

**Files:**
- Verify only: `src/Patches/GameCompatibility/GameBinaryPatch.h`
- Verify only: `src/Patches/GameCompatibility/GameBinaryPatch.cpp`
- Verify only: `src/Patches/GameCompatibility/GameBinaryPatchDiagnostics.h`
- Verify only: `src/Patches/GameCompatibility/GameBinaryPatchDiagnostics.cpp`
- Verify only: `src/Loader/DllMain.cpp`
- Verify only: `tests/Patches/GameBinaryPatchTests.cpp`
- Verify only: `tests/Patches/GameBinaryPatchDiagnosticsTests.cpp`
- Read-only evidence: `H:\gc\game_decrypted.exe`
- Read-only evidence: `H:\gc\game471.exe`

**Interfaces:**
- Consumes: the two implementation commits from Tasks 1 and 2.
- Produces: complete Debug/Release build and CTest evidence, compiled-artifact
  evidence, unchanged runtime-binary hashes, a clean repository boundary, and a
  clearly deferred operator runtime checklist.

- [ ] **Step 1: Reconfigure, build, and test the complete Debug preset**

Run from the x86 MSVC developer PowerShell:

```powershell
cmake --preset msvc32-debug
cmake --build --preset msvc32-debug
ctest --preset msvc32-debug -j 4
```

Expected: configuration succeeds, every Debug target builds, and the complete
Debug CTest suite passes with no failed tests.

- [ ] **Step 2: Reconfigure, build, and test the complete Release preset**

Run:

```powershell
cmake --preset msvc32-release
cmake --build --preset msvc32-release
ctest --preset msvc32-release -j 4
```

Expected: configuration succeeds, every RelWithDebInfo target builds, and the
complete Release CTest suite passes with no failed tests.

- [ ] **Step 3: Inspect the Release artifact for required user-facing markers**

Run:

```powershell
$artifactPath = 'build-msvc32-release\dist\iDmacDrv32.dll'
$artifactBytes = [System.IO.File]::ReadAllBytes($artifactPath)
$artifactAnsi = [System.Text.Encoding]::ASCII.GetString($artifactBytes)
$artifactWide = [System.Text.Encoding]::Unicode.GetString($artifactBytes)
foreach ($marker in @(
    'GameBinaryPatch: state=',
    'native_mouse_events',
    'dongle_failure',
    'dongle_security_transmit',
    'rfid_com_port')) {
    if (-not $artifactAnsi.Contains($marker)) {
        throw "Missing Release marker: $marker"
    }
}
if (-not $artifactWide.Contains('GCLoader unsupported game version')) {
    throw 'Missing Release unsupported-version prompt'
}
```

Expected: the command returns without throwing and proves the linked DLL owns
the four named contracts plus the required prompt.

- [ ] **Step 4: Prove the runtime executables were not changed**

Run:

```powershell
Get-FileHash -Algorithm SHA256 -LiteralPath `
    'H:\gc\game_decrypted.exe', `
    'H:\gc\game471.exe' |
    Select-Object Path, Hash
```

Expected exact hashes:

```text
H:\gc\game_decrypted.exe  795AB03F944BA7716AB257869C6BA394D19288E6484A17FACF1600ED377595DF
H:\gc\game471.exe         FEAD3BD4D0E0985F101965EDC417DD2B96522F8716FF789D84618FEB0D7A2522
```

Any mismatch is a stop condition. Do not restore or replace either binary
without explicit user direction.

- [ ] **Step 5: Inspect repository scope and commit boundaries**

Run:

```powershell
git diff --check
git status --short
git log -3 --oneline
```

Expected: `git diff --check` is silent; the working tree is clean; the two most
recent implementation commits are the Task 2 and Task 1 commits, followed by
the design/plan history. No runtime file under `H:\gc` appears in Git status.

- [ ] **Step 6: Record the unclaimed runtime acceptance boundary**

Report build/static success separately from these still-pending operator checks:

```text
1. Clean game_decrypted.exe boots and logs:
   GameBinaryPatch: state=patched_clean sites=4
2. Legacy game471.exe boots and logs:
   GameBinaryPatch: state=already_patched sites=4
3. The game opens GCLoader's emulated COM2 RFID path.
4. The recurring dongle path does not suspend the game.
5. Native buffered mouse-button events remain disabled.
6. An intentionally unsupported executable copy shows
   "GCLoader unsupported game version" and stops initialization.
```

Do not copy `iDmacDrv32.dll`, launch either game executable, create an
unsupported executable copy, or modify runtime state as part of this plan.
Those actions require a separate explicit deployment/runtime-acceptance request.

## Self-Review

- **Spec coverage:** Task 1 implements every PE identity field, all four exact
  clean/replacement contracts, all-clean and all-prepatched success, mixed and
  unknown rejection, checked range handling, full preflight, production memory
  protection/cache handling, reverse rollback, and structured evidence. Task 2
  implements the exact unsupported-version prompt, setup-failure distinction,
  one-shot fatal path, game-only first-mutation ordering, success logging, and
  compiled-artifact markers. Task 3 covers both presets, full CTest, artifact
  inspection, unchanged source-binary hashes, repository scope, and the honest
  runtime-acceptance boundary.
- **Non-goal coverage:** No task patches IAT or `.data`, changes config, scans
  for unknown versions, adds hooks/detours, edits COM/RFID behavior outside the
  one string byte, refactors unrelated patch families, deploys a DLL, launches
  the game, or mutates `H:\gc`.
- **Placeholder scan:** Every file, interface, enum, identity field, site/RVA,
  byte pattern, prompt title, exit code, test case, build command, artifact
  marker, commit path, and expected result is explicit. The words “unknown” and
  “mixed” refer only to supported-state classifications, not unfinished work.
- **Type consistency:** `GameBinaryBytePattern`, `GameBinaryPatchSite`,
  `GameBinaryImageState`, `GameBinaryIdentityField`,
  `GameBinaryMemoryStage`, `GameBinaryPatchStage`,
  `GameBinaryMemoryError`, `GameBinaryPatchActions`,
  `GameBinaryPatchError`, `GameBinaryPatchResult`,
  `InstallGameBinaryPatch()`, `GameBinaryPatchInit()`, and
  `BuildGameBinaryPatchFatalDiagnostic()` keep identical names and signatures
  across producers, tests, diagnostics, and `DllMain`.

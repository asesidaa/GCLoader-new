# Build Detection and Versioned Preflight Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Detect game and NESYS images once, select exact known variants by SHA-256, support unknown candidates through structural detection plus complete local contracts, and produce one immutable approved versioned-plan set before mutation.

**Architecture:** A central detector owns image identity and build selection; each feature owns its RVAs, byte/pointer contracts, ABI, and capability. The plan validator understands generic site kinds, address ranges, overlaps, collisions, dependencies, and known/unknown proof rules, but never contains a central feature address table.

**Tech Stack:** C++23, Win32 x86 PE structures, Windows CNG (`bcrypt`), `std::expected`, `std::variant`, `std::span`, CMake/Ninja/MSVC.

**Spec:** `docs/superpowers/specs/2026-09-05-loader-codebase-cleanup-design.md`

## Global Constraints

- Complete Plans 01 and 02 first.
- Do not add a generic plugin/feature registry. Game and NESYS startup remain
  explicit composition code.
- Exact known hashes select a descriptor directly and do not reread each site
  to prove identity. Unknown hashes must validate every mandatory and enabled
  site before any operation is approved.
- A known descriptor may represent an explicitly supported patched image.
  Arbitrary hybrid or partially patched unknown images are rejected. Patched
  site state is accepted only through an exact known patched-image descriptor.
- Do not add an older-build RVA or ABI without direct current binary/IDA
  evidence. This plan creates the extension shape; it does not guess a 2.06
  profile.
- This is native patch infrastructure. Do not add fake images, copied binaries,
  mock memory, or table-mirroring tests.

---

## Task 1: Add typed image and build identities

**Files:**

- Create: `src/Patches/GameVersion/GameBuild.h`
- Create: `src/Nesys/NesysBuild.h`
- Create: `src/Patches/GameVersion/ImageIdentity.h`
- Create: `src/Patches/GameVersion/ImageIdentity.cpp`
- Create: `src/Patches/GameVersion/KnownImages.h`
- Create: `src/Patches/GameVersion/KnownImages.cpp`
- Create: `src/Patches/GameVersion/CMakeLists.txt`
- Modify: `src/Patches/CMakeLists.txt`

**Interfaces:**

```cpp
namespace gc::game_version {

struct Sha256Digest final {
    std::array<std::byte, 32> bytes{};
    friend bool operator==(const Sha256Digest&, const Sha256Digest&) = default;
};

enum class DetectionProof : std::uint8_t {
    exact_known_hash,
    complete_local_contract,
};

enum class GameBuild : std::uint8_t { groove_coaster_471 };
enum class GameImageVariant : std::uint8_t {
    clean,
    legacy_patched,
    locally_verified,
};

struct LoadedImageIdentity final {
    std::filesystem::path path;
    Sha256Digest sha256;
    std::uint64_t file_size{};
    std::uint16_t machine{};
    std::uint32_t time_date_stamp{};
    std::uint32_t preferred_image_base{};
    std::uint32_t size_of_image{};
};

template <class Build, class Variant>
struct BuildSelection final {
    Build build;
    Variant variant;
    DetectionProof proof;
    LoadedImageIdentity identity;
};

[[nodiscard]] std::expected<LoadedImageIdentity, IdentityError>
ReadLoadedExecutableIdentity(HMODULE module) noexcept;

[[nodiscard]] std::expected<
    BuildSelection<GameBuild, GameImageVariant>, DetectionError>
DetectGameBuild(HMODULE module) noexcept;

} // namespace gc::game_version
```

NESYS defines `NesysBuild`, `NesysImageVariant`, and the corresponding typed
alias; do not reuse `GameBuild` for the NESYS process.

- [ ] **Step 1: Implement SHA-256 with Windows CNG**

Resolve the module path with a dynamically resized `GetModuleFileNameW`
buffer. Open that exact file with sharing that does not block the existing
process. Hash its bytes using `BCryptOpenAlgorithmProvider(BCRYPT_SHA256_ALGORITHM)`,
`BCryptCreateHash`, `BCryptHashData`, and `BCryptFinishHash`. Close every CNG
and file resource on all paths. Link `gc_game_version` privately to `bcrypt`.

- [ ] **Step 2: Capture stable PE facts separately from the hash**

Read the loaded PE headers through `RuntimeImage`-style guarded access and
populate the identity fields. These facts select structural candidates only;
they do not override an exact hash match and do not by themselves authorize a
mutation.

- [ ] **Step 3: Compile the identity module**

```powershell
cmake --preset msvc32-debug
cmake --build --preset msvc32-debug --target gc_game_version
cmake --build --preset msvc32-release --target gc_game_version
```

---

## Task 2: Add exact known image descriptors

**Files:**

- Modify: `src/Patches/GameVersion/KnownImages.h`
- Modify: `src/Patches/GameVersion/KnownImages.cpp`

**Produces:**

```cpp
template <class Build, class Variant>
struct KnownImageDescriptor final {
    Build build;
    Variant variant;
    Sha256Digest sha256;
    std::uint64_t file_size{};
    std::uint16_t machine{IMAGE_FILE_MACHINE_I386};
    std::uint32_t preferred_image_base{};
};
```

- [ ] **Step 1: Encode the two verified game variants**

Add exactly these initial descriptors:

```text
4.71 clean
size:   3691008
sha256: 795AB03F944BA7716AB257869C6BA394D19288E6484A17FACF1600ED377595DF

4.71 legacy_patched
size:   3691008
sha256: FEAD3BD4D0E0985F101965EDC417DD2B96522F8716FF789D84618FEB0D7A2522
```

Both use preferred base `0x00400000`. The variant names describe known site
state; both select `GameBuild::groove_coaster_471`.

- [ ] **Step 2: Encode the verified NESYS image separately**

```text
current NESYS image
size:   368640
sha256: 487402D4ABDEF6A857A397CF25C9D681CB6F6052965C500361B0FD14D00913F2
```

Name its typed build from the binary's actual version evidence during the
Plan 06h IDA pass; until then use the neutral enumerator
`NesysBuild::current_supported` rather than inventing a game version number.

- [ ] **Step 3: Make known lookup exact and allocation-free**

Search a compile-time descriptor array by the 32 digest bytes and file size.
On a match, return `DetectionProof::exact_known_hash` immediately. Do not call
any feature contract reader from this branch.

---

## Task 3: Define feature IDs, capabilities, and feature-owned profiles

**Files:**

- Create: `src/Patches/GameVersion/FeatureCapability.h`
- Create: `src/Patches/GameVersion/FeatureProfile.h`
- Modify: `src/Patches/GameVersion/CMakeLists.txt`

**Interfaces:**

```cpp
enum class FeatureId : std::uint8_t {
    game_compatibility,
    auto_play,
    song_unlock,
    switch_input,
    absolute_judgement,
    framerate,
    countdown,
    test_mode_timing,
    renderer_device_loss,
    windowed_widescreen,
    asio_close,
    nesys_ping,
};

enum class Capability : std::uint8_t { unavailable, supported };

struct FeatureRequirement final {
    FeatureId feature;
    bool mandatory{};
    bool enabled{};
};

template <class Profile, class Build, class Variant>
using ProfileSelector =
    std::optional<Profile> (*)(Build, Variant) noexcept;
```

- [ ] **Step 1: Keep profile selectors in their owning features**

Each later feature adds `ProfileFor(GameBuild, GameImageVariant)` beside its
existing manifest. The central module contains only `FeatureId` and capability
state, never feature RVAs, instruction bytes, calling conventions, structure
offsets, or callback addresses.

- [ ] **Step 2: Make unsupported enabled features explicit**

An optional disabled feature contributes nothing. An enabled feature whose
selector returns `nullopt`, or a mandatory feature without a profile, yields a
typed `unsupported_feature` detection error naming build, variant, and
feature. This is how older builds declare absent 4.71-only capabilities.

---

## Task 4: Add immutable versioned plans and the global validator

**Files:**

- Create: `src/Patches/GameVersion/VersionedPlan.h`
- Create: `src/Patches/GameVersion/VersionedPlan.cpp`
- Create: `src/Patches/GameVersion/VersionedPlanDiagnostics.h`
- Create: `src/Patches/GameVersion/VersionedPlanDiagnostics.cpp`
- Modify: `src/Patches/GameVersion/CMakeLists.txt`

**Interfaces:**

```cpp
enum class VersionedOperationKind : std::uint8_t {
    byte_patch,
    inline_hook,
    mid_hook,
    global_vtable_slot,
    read_only_contract,
};

struct SiteContract final {
    FeatureId feature;
    std::string_view site;
    VersionedOperationKind kind;
    runtime_image::Rva rva{};
    std::uint32_t protected_span{};
    runtime_image::BytePattern original{};
    runtime_image::BytePattern installed{};
    std::uint32_t install_order{};
};

enum class SiteDisposition : std::uint8_t {
    install,
    already_installed,
    verify_only,
};

struct FeaturePlan final {
    FeatureId feature;
    std::span<const SiteContract> sites;
    std::span<const FeatureId> install_after;
};

class VersionedPlanSet final {
public:
    [[nodiscard]] std::expected<void, PlanError>
    Add(FeaturePlan plan) noexcept;
    [[nodiscard]] std::expected<ApprovedVersionedPlan, PlanError>
    Validate(const runtime_image::RuntimeImage&,
             DetectionProof,
             GameImageVariant) const noexcept;
};
```

The later hook plans extend the operation payloads with concrete callbacks and
original storage; the target contract and validation identity remain these
types.

- [ ] **Step 1: Reject malformed plans before reading memory**

Require unique `(feature, site)`, nonempty names, nonzero spans, valid RVAs,
known operation kind, complete dependency references, and a deterministic
topological install order. Detect dependency cycles and duplicate add calls.

- [ ] **Step 2: Reject address conflicts**

Resolve all spans through `RuntimeImage`, sort by start address, and reject:

- overlapping byte writes;
- duplicate exclusive hook addresses;
- byte writes intersecting any hook's protected instruction span;
- vtable slots sharing an address with a different expected/replacement pair;
- arithmetic overflow or a span outside the mapped image.

This validation runs for known and unknown hashes.

- [ ] **Step 3: Implement the two proof paths**

For `exact_known_hash`, use the feature profile's variant-specific site
disposition and perform no contract `Read`. For `complete_local_contract`,
read every site first and require the exact original contract for every
operation that will mutate or hook the image; read-only contracts require
their one exact expected state. An installed or mixed state on this path is a
mismatch, even when its bytes equal a known replacement. Return no approved
plan if any site fails. No operation installs while validation is running.

- [ ] **Step 4: Preserve all mismatch detail**

`PlanError` must carry build/variant/proof, feature/site, RVA/address, expected
original/installed bytes or pointer, observed value, overlap peer when
applicable, and underlying RuntimeImage error. Formatting lives in
`VersionedPlanDiagnostics`, not in the validator.

---

## Task 5: Implement structural candidate selection

**Files:**

- Create: `src/Patches/GameVersion/BuildDetector.h`
- Create: `src/Patches/GameVersion/BuildDetector.cpp`
- Modify: `src/Patches/GameVersion/CMakeLists.txt`

- [ ] **Step 1: Select candidates without mutation**

For an unknown digest, filter the compiled build-descriptor array by i386
machine, file size, preferred base, mapped image size, and other stable PE
facts actually verified for that build. Return a typed error unless exactly
one candidate remains.

- [ ] **Step 2: Defer final acceptance to the complete plan**

Return a candidate token, not a `BuildSelection`, until all mandatory/enabled
feature profiles have been collected and locally validated. Only the complete
validator may promote the token to
`DetectionProof::complete_local_contract`.

- [ ] **Step 3: Keep game and NESYS candidate sets distinct**

The process role chooses the detector before candidate selection. Never test a
NESYS image against game descriptors or allow a NESYS-only contract to satisfy
a game candidate.

---

## Task 6: Add explicit composition entry points without activating them yet

**Files:**

- Create: `src/Loader/GameVersionedStartupPlan.h`
- Create: `src/Loader/GameVersionedStartupPlan.cpp`
- Create: `src/Loader/NesysVersionedStartupPlan.h`
- Create: `src/Loader/NesysVersionedStartupPlan.cpp`
- Modify: `src/CMakeLists.txt`

**Produces:**

```cpp
[[nodiscard]] std::expected<PreparedGameVersionedStartup, StartupPlanError>
PrepareGameVersionedStartup(
    HMODULE process_module,
    const ValidatedConfig& config) noexcept;

[[nodiscard]] std::expected<PreparedNesysVersionedStartup, StartupPlanError>
PrepareNesysVersionedStartup(
    HMODULE process_module,
    const NesysSettings& settings) noexcept;
```

- [ ] **Step 1: Make composition explicit**

The game function calls feature profile builders by name in declared order;
the NESYS function calls only NESYS builders. Do not discover builders through
static registration, linker sections, global constructors, or a generic
feature registry.

- [ ] **Step 2: Keep the new path dormant until migration completes**

Compile these entry points but do not replace the current DllMain startup flow
in this plan. Plans 06a through 06h fill the plan, and Plan 09 performs the
single final startup cutover after every versioned family participates.

---

## Task 7: Verify and commit build detection

- [ ] **Step 1: Recheck the source evidence identities**

```powershell
Get-FileHash -Algorithm SHA256 -LiteralPath H:\gc\game_decrypted.exe
Get-FileHash -Algorithm SHA256 -LiteralPath H:\gc\game471.exe
Get-FileHash -Algorithm SHA256 -LiteralPath H:\gc\NesysService.exe
```

Require exact equality with Task 2. This reads evidence only.

- [ ] **Step 2: Run static verification**

```powershell
cmake --build --preset msvc32-debug
ctest --preset msvc32-debug -j 4
cmake --build --preset msvc32-release
ctest --preset msvc32-release -j 4
git diff --check
git status --short --branch
```

- [ ] **Step 3: Commit**

```powershell
git add -- src\Patches\GameVersion src\Nesys\NesysBuild.h src\Patches\CMakeLists.txt src\Loader\GameVersionedStartupPlan.h src\Loader\GameVersionedStartupPlan.cpp src\Loader\NesysVersionedStartupPlan.h src\Loader\NesysVersionedStartupPlan.cpp src\CMakeLists.txt
git commit -m "Add executable build detection and preflight"
```

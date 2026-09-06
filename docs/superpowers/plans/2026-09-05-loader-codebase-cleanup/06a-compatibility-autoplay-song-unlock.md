# Compatibility, AutoPlay, and SongUnlock Migration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Complete the concrete versioned-operation executor and migrate GameCompatibility, AutoPlay, and SongUnlock from eager feature-local installation into feature-owned 4.71 profiles collected behind the global barrier.

**Architecture:** Each feature returns an immutable profile for the selected build/variant. The Loader combines those operations with the approved plan, then a concrete executor delegates byte writes to RuntimeImage and hook ownership to HookRegistry. Exact known variants carry their known site disposition; unknown images must be entirely original.

**Tech Stack:** C++23, RuntimeImage, build/profile selection, SafetyHook-backed HookRegistry, `std::variant`, `std::expected`, CMake/Ninja/MSVC, IDA-CLI evidence.

**Spec:** `docs/superpowers/specs/2026-09-05-loader-codebase-cleanup-design.md`

## Global Constraints

- Complete Plans 01 through 05 first.
- Use `H:\gc\game471.exe.i64` to revalidate every RVA, byte prefix,
  instruction, ABI, and control-flow assumption before editing a manifest.
- The two known 4.71 variants are coherent whole-image profiles. As corrected
  on 2026-09-06, unknown hashes classify each complete byte-patch site locally.
- Disabled AutoPlay or SongUnlock contributes no sites and no hook.
- Complete-plan preflight precedes the first byte write or hook installation.
  Installation failure aborts; no reverse rollback exists.
- Do not add fake-memory or fake-hook tests. Build evidence is not gameplay,
  marker, or save-suppression acceptance.

---

## Task 1: Complete concrete versioned operation payloads and execution

**Files:**

- Modify: `src/Patches/GameVersion/VersionedPlan.h`
- Modify: `src/Patches/GameVersion/VersionedPlan.cpp`
- Create: `src/Loader/VersionedStartupExecutor.h`
- Create: `src/Loader/VersionedStartupExecutor.cpp`
- Modify: `src/Patches/GameVersion/CMakeLists.txt`
- Modify: `src/CMakeLists.txt`

**Interfaces:**

```cpp
struct BytePatchOperation final {
    SiteContract contract;
    runtime_image::BytePattern replacement;
};

struct InlineHookOperation final {
    SiteContract contract;
    void* detour{};
    hooking::OriginalPublisher original;
};

struct MidHookOperation final {
    SiteContract contract;
    safetyhook::MidHookFn callback{};
};

struct GlobalVtableSlotOperation final {
    SiteContract contract;
    void* expected{};
    void* replacement{};
};

struct ReadOnlyContractOperation final {
    SiteContract contract;
};

using VersionedOperation = std::variant<
    BytePatchOperation,
    InlineHookOperation,
    MidHookOperation,
    GlobalVtableSlotOperation,
    ReadOnlyContractOperation>;

[[nodiscard]] std::expected<void, StartupInstallError>
InstallApprovedVersionedPlan(
    const ApprovedVersionedPlan&,
    const runtime_image::RuntimeImage&,
    hooking::HookRegistry&) noexcept;
```

- [ ] **Step 1: Keep validation and installation separate**

The approved plan contains the original immutable operations plus the
validator-produced disposition and resolved address. It cannot be constructed
outside `VersionedPlanSet::Validate`. The executor never reselects a build,
profile, or enabled feature.

- [ ] **Step 2: Install in explicit order**

Sort by validated feature dependency and `install_order`, with a stable site
ordinal tie-breaker. For `already_installed`, log one startup summary row and
perform no operation. `verify_only` performs no mutation. Delegate byte writes
and vtable exchange to RuntimeImage and inline/mid retention to HookRegistry.

- [ ] **Step 3: Make every failure terminal at the composition boundary**

Return a typed `StartupInstallError` containing the failed operation and
underlying RuntimeImage/HookError. `GameStartup` formats it and calls
`AbortProcess`; it never invokes the next operation.

---

## Task 2: Add the GameCompatibility profile

**Files:**

- Create: `src/Patches/GameCompatibility/GameCompatibilityProfile.h`
- Create: `src/Patches/GameCompatibility/GameCompatibilityProfile.cpp`
- Modify: `src/Patches/GameCompatibility/GameBinaryPatch.h`
- Modify: `src/Patches/GameCompatibility/GameBinaryPatch.cpp`
- Modify: `src/Patches/GameCompatibility/GameBinaryPatchDiagnostics.*`
- Modify: `src/Patches/CMakeLists.txt`

- [ ] **Step 1: Revalidate the four operations**

Require these existing sites and instruction meanings:

| Site | RVA | Original | Replacement |
|---|---:|---|---|
| native mouse events | `0x000B0896` | `75 02` | `90 90` |
| dongle failure | `0x00102C7B` | `75 3B` | `EB 3B` |
| dongle security transmit | `0x00103EE6` | `E8 45 F6 FF FF` | `90 90 90 90 90` |
| RFID COM port | `0x002F7AC3` | `31` | `32` |

If direct IDA evidence differs, stop and update the design/plan with the
actual contract before implementation.

- [ ] **Step 2: Return variant-specific dispositions**

`ProfileFor(groove_coaster_471, clean)` marks all four `install`.
`ProfileFor(groove_coaster_471, legacy_patched)` marks all four
`already_installed`. `locally_verified` carries both byte forms and lets the
validator derive `install` or `already_installed` independently at each site.

- [ ] **Step 3: Remove eager installation**

Replace `GameBinaryPatchInit()` with
`BuildGameCompatibilityPlan(BuildSelection)`. Remove feature-local plan loops,
state classification, and fatal publication now owned by the common validator
and executor. Keep feature/site diagnostic names.

---

## Task 3: Add the AutoPlay profile and registry-owned marker hook

**Files:**

- Create: `src/Patches/AutoPlay/AutoPlayProfile.h`
- Create: `src/Patches/AutoPlay/AutoPlayProfile.cpp`
- Modify: `src/Patches/AutoPlay/AutoPlayPatch.h`
- Modify: `src/Patches/AutoPlay/AutoPlayPatch.cpp`
- Modify: `src/Patches/AutoPlay/AutoPlayMarker.*`
- Modify: `src/Patches/AutoPlay/AutoPlayPatchDiagnostics.*`
- Modify: `src/Patches/CMakeLists.txt`

- [ ] **Step 1: Revalidate all five native contracts**

Require:

| Purpose | RVA | Contract |
|---|---:|---|
| native auto-play getter | `0x0003CADA` | `8A 80 A5 00 00 00` -> `B0 01 90 90 90 90` |
| HIDDEN/AD-LIB completion getter | `0x0003CAFA` | `8A 80 A6 00 00 00` -> `B0 01 90 90 90 90` |
| do-not-save result | `0x00269951` | `0F 95 C1` -> `B1 01 90` |
| outer-frame marker seam | `0x00058BE9` | exact current 10-byte instruction prefix; mid hook |
| native debug-text entry | `0x00069650` | exact function prefix and `__cdecl` varargs ABI; read-only |

Confirm state `+0xA7` remains excluded.

- [ ] **Step 2: Build a profile only when enabled**

The profile contains three byte writes, one mid hook, and one read-only call
target contract. Preserve the safe install order: publish marker runtime/text
target, retain the dormant mid hook, no-save, HIDDEN/AD-LIB, then native
auto-play. The common executor enables the hook in that declared order and
aborts on failure.

- [ ] **Step 3: Remove feature-local hook/memory ownership**

Delete the AutoPlay `safetyhook::MidHook` owner, local create/enable/reset
logic, local rollback state, and duplicate byte formatting. The registry owns
the hook; AutoPlay owns only process-lifetime callback state and marker policy.

- [ ] **Step 4: Preserve visible and persistence behavior**

Keep the four fixed native text calls, mandatory warning wording/coordinates,
allocation-free successful render callback, save suppression, and fatal marker
failure behavior unchanged.

---

## Task 4: Add the SongUnlock profile

**Files:**

- Create: `src/Patches/SongUnlock/SongUnlockProfile.h`
- Create: `src/Patches/SongUnlock/SongUnlockProfile.cpp`
- Modify: `src/Patches/SongUnlock/SongUnlockPatch.h`
- Modify: `src/Patches/SongUnlock/SongUnlockPatch.cpp`
- Modify: `src/Patches/CMakeLists.txt`

- [ ] **Step 1: Revalidate the 4.71 branch contract**

Require RVA `0x00257854`, original
`0F 85 1D 02 00 00`, replacement `E9 1E 02 00 00 90`, decoded control flow,
and the exact affected availability predicate.

- [ ] **Step 2: Replace eager initialization with plan contribution**

`BuildSongUnlockPlan(selection, enabled)` returns no sites when disabled and
one `BytePatchOperation` when enabled. Remove its local address, read, write,
formatting, exception, and logging loops.

- [ ] **Step 3: Reserve older behavior as a distinct future profile**

Do not encode a 2.06 RVA in this plan. The profile selector makes room for a
separate older SongUnlock branch after direct evidence; it never falls back to
the 4.71 branch.

---

## Task 5: Add these features to game plan composition

**Files:**

- Modify: `src/Loader/GameVersionedStartupPlan.cpp`
- Modify: `src/Loader/GameVersionedStartupPlan.h`
- Modify: `src/Loader/VersionedStartupExecutor.cpp`

- [ ] **Step 1: Add explicit feature requirements**

Always add GameCompatibility as mandatory. Add AutoPlay and SongUnlock only
when enabled by `ValidatedConfig`. Declare the install dependency/order:

```text
GameCompatibility -> AutoPlay -> SongUnlock
```

This order preserves current startup behavior but does not weaken complete-
plan preflight.

- [ ] **Step 2: Do not cut over DllMain yet**

Remove these three eager calls only from the prepared migration path. Keep the
global prepared plan dormant until every remaining versioned feature has moved
and Plan 09 performs one atomic startup-flow cutover.

---

## Task 6: Verify and commit the simple-profile migration

- [ ] **Step 1: Audit obsolete ownership**

```powershell
rg -n 'GameBinaryPatchInit|AutoPlayPatchInit|SongUnlockPatchInit|AutoPlayBytePattern|ProductionGameBinaryPatchActions|rollback|safetyhook::(InlineHook|MidHook)' src\Patches\GameCompatibility src\Patches\AutoPlay src\Patches\SongUnlock
```

Expected: no eager installer, feature-local hook owner, rollback field, or
feature-local memory implementation remains. SafetyHook callback context types
may remain where required by the mid-hook ABI.

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
git add -- src\Patches\GameVersion src\Patches\GameCompatibility src\Patches\AutoPlay src\Patches\SongUnlock src\Patches\CMakeLists.txt src\Loader\GameVersionedStartupPlan.h src\Loader\GameVersionedStartupPlan.cpp src\Loader\VersionedStartupExecutor.h src\Loader\VersionedStartupExecutor.cpp src\CMakeLists.txt
git commit -m "Migrate simple patches to version profiles"
```

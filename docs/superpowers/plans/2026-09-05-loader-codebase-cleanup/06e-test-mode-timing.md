# Test Mode Timing Versioned Migration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Move Test Mode Timing's 15 byte contracts, 13 vtable-pointer contracts, one row-count write, two inline hooks, native call targets, and layout offsets into a feature-owned 4.71 profile and the global versioned barrier.

**Architecture:** `TestModeTimingProfile` supplies a typed native ABI to the feature. RuntimeImage validates bytes/pointers and performs the one write; HookRegistry owns the two hooks and publishes originals. The 13-slot sound-form carrier vtable remains feature-owned object construction, not a VMT hook.

**Tech Stack:** C++23, RuntimeImage, GameBuild profiles, SafetyHook inline hooks, x86 game ABI, CMake/Ninja/MSVC, IDA-CLI.

**Spec:** `docs/superpowers/specs/2026-09-05-loader-codebase-cleanup-design.md`

## Global Constraints

- Complete Plans 01 through 06d first.
- Test Mode Timing is mandatory in the current game startup. Missing profile,
  contract mismatch, pointer mismatch, write failure, or hook failure aborts.
- Keep the carrier vtable as a constructed table for a loader-owned object. Do
  not replace it with SafetyHook `VmtHook`/`VmHook` or register it as a global
  vtable-slot detour.
- Preserve UI row behavior, allocation/deallocation ownership, native calling
  conventions, live timing application, and test-mode persistence behavior.
- Revalidate every target and object-layout assumption against
  `H:\gc\game471.exe.i64` before moving constants.
- Do not add fake native-object or fake-memory tests.

---

## Task 1: Revalidate the complete Timing ABI

**Files:**

- Create: `.codex-tmp/loader-cleanup-test-mode-timing-profile.py` (untracked)
- Read: `src/Patches/TestModeTiming/TimingSettingsGameAbi.*`
- Read: `src/Patches/TestModeTiming/TimingSettingsPatch.*`

- [ ] **Step 1: Verify all byte contracts and hooks**

Require the current 15 contracts for main constructor/render, sound
constructor, allocator/deallocator, child registration, base update, cell text,
selection, title/help functions, timing manager, and both timing setters.
Require hook targets `0x173EA0` and `0x173C60`, exact prefixes, protected spans,
receiver/argument ABI, and original return behavior.

- [ ] **Step 2: Verify the one direct write**

At RVA `0x173ED5`, prove the original row-count instruction/data byte contract,
replacement value, and why the write expands the form from four to eleven
rows. Record code/data classification.

- [ ] **Step 3: Verify the sound-form carrier contract**

At vtable RVA `0x2FB864`, validate exactly 13 pointer entries and their target
RVAs:

```text
06AB20 06AB20 00C9B0 04D070 0C2680 16B0C0 16B440
16B290 16B230 16AD60 16AC20 16A9A0 0C2F20
```

Verify the scalar-deleting destructor slot, overridden base-update slot, form
size `0x1D4`, and ownership/lifetime of the constructed object.

- [ ] **Step 4: Verify data and object offsets**

Recheck global offsets `0x3D9878`/`0x3D987C`, form/grid/status/help/title
offsets, field widths, and timing-manager/setter semantics. Each offset needs
an owning type/function, not only a numeric xref.

---

## Task 2: Add `TestModeTimingProfile`

**Files:**

- Create: `src/Patches/TestModeTiming/TestModeTimingProfile.h`
- Create: `src/Patches/TestModeTiming/TestModeTimingProfile.cpp`
- Modify: `src/Patches/TestModeTiming/TimingSettingsGameAbi.h`
- Modify: `src/Patches/TestModeTiming/TimingSettingsGameAbi.cpp`
- Modify: `src/Patches/TestModeTiming/CMakeLists.txt`

**Interfaces:**

```cpp
struct TimingNativeLayout final {
    std::size_t sound_form_size{};
    std::array<runtime_image::Rva, 13> sound_vtable_targets{};
    // Existing form/grid/global offsets as named fields.
};

struct TestModeTimingProfile final {
    game_version::GameBuild build;
    game_version::GameImageVariant variant;
    std::array<game_version::VersionedOperation, 18> operations;
    std::array<PointerContract, 13> sound_vtable;
    TimingNativeLayout layout;
};

[[nodiscard]] std::optional<TestModeTimingProfile>
ProfileFor(game_version::GameBuild,
           game_version::GameImageVariant) noexcept;
```

The 18 operations are 15 read-only byte contracts, one byte write, and two
inline hooks. The 13 pointer contracts are validated additionally as one named
carrier-vtable contract.

- [ ] **Step 1: Move concrete game facts into the profile**

Keep function-pointer aliases in `TimingSettingsGameAbi.h`. Move every 4.71
RVA, byte prefix, vtable target, object/global offset, and size into the
profile. Add compile-time assertions for 15, 13, 1, 2, and 18 counts.

- [ ] **Step 2: Build `TimingGameAbi` from the approved profile**

Resolve callable/data addresses from the selected RuntimeImage only after the
plan is approved. `BuildTimingGameAbi` takes `RuntimeImage` plus
`TestModeTimingProfile`; it does not read `GetModuleHandleW` or preferred base.

- [ ] **Step 3: Return no fallback profile**

Only 4.71 variants return a profile. A future older build must supply all
native call targets, pointer contracts, and layouts or explicitly mark the
feature unavailable.

---

## Task 3: Remove `TimingPatchTransaction` and local hooks

**Files:**

- Modify: `src/Patches/TestModeTiming/TimingSettingsGameAbi.h`
- Modify: `src/Patches/TestModeTiming/TimingSettingsGameAbi.cpp`
- Modify: `src/Patches/TestModeTiming/TimingSettingsPatch.h`
- Modify: `src/Patches/TestModeTiming/TimingSettingsPatch.cpp`

- [ ] **Step 1: Delete duplicated patch infrastructure**

Remove `TimingBytePattern`, `TimingByteContract`, `TimingCheckedWrite`,
`TimingHookOperation`, `TimingMemoryApi`, `TimingInstallStage`,
`TimingInstallError`, `TimingPatchTransaction`, production read/write helpers,
rollback logic, and original-state restoration.

- [ ] **Step 2: Replace feature-owned hook objects with originals**

Delete `main_constructor_hook` and `main_render_hook` from the runtime owner.
Store typed native original function pointers published by HookRegistry. Keep
the carrier-vtable array and feature runtime object because they are feature
state, not detour ownership.

- [ ] **Step 3: Preserve construction safety**

Construct the runtime owner, approved `TimingGameAbi`, and carrier vtable before
the hooks are enabled. The constructor/render callbacks must never observe an
uninitialized ABI or moved runtime.

- [ ] **Step 4: Preserve live timing behavior**

Keep `ApplyLiveTiming` semantics and its genuine native/runtime boundary. If
Plan 01 marks `TimingLiveActions` as a fake-only seam, leave its deletion to
Plan 08; do not conflate that cleanup with native profile migration.

---

## Task 4: Add Timing to global game composition

**Files:**

- Modify: `src/Loader/GameVersionedStartupPlan.cpp`
- Modify: `src/Loader/VersionedStartupExecutor.cpp`
- Modify: `src/Loader/DllMain.cpp`

- [ ] **Step 1: Add the mandatory profile**

Always request `TestModeTimingProfile` for the selected game build. Add all 31
contracts to the global validator before any mutation. Pointer contracts
participate in duplicate/overlap checks even though the carrier vtable itself
is not modified.

- [ ] **Step 2: Preserve install order**

After complete preflight, perform the row-count write, publish runtime/carrier
state, install main constructor, then main render. Declare dependencies on
GameCompatibility and prepared system configuration.

- [ ] **Step 3: Remove eager installation**

Delete `TimingSettingsPatchInit()` from the migration startup path. There must
be no second local preflight or hook creation after the common executor.

---

## Task 5: Verify and commit

- [ ] **Step 1: Audit mechanism separation**

```powershell
rg -n 'TimingPatchTransaction|TimingMemoryApi|TimingBytePattern|rollback|InlineHook::create|safetyhook::InlineHook|VmtHook|VmHook' src\Patches\TestModeTiming
rg -n 'k[A-Za-z0-9_]+Rva|k[A-Za-z0-9_]+Offset' src\Patches\TestModeTiming --glob '!TestModeTimingProfile.cpp'
```

Expected: no transaction/local hook owner; concrete game constants live in the
profile; no VMT hook type is used for the carrier.

- [ ] **Step 2: Run static verification**

```powershell
cmake --build --preset msvc32-debug
ctest --preset msvc32-debug -j 4
cmake --build --preset msvc32-release
ctest --preset msvc32-release -j 4
git diff --check
git status --short --branch
```

Do not claim test-mode UI, timing persistence, live setter, or object-lifetime
runtime acceptance.

- [ ] **Step 3: Commit**

```powershell
git add -- src\Patches\TestModeTiming src\Loader\GameVersionedStartupPlan.cpp src\Loader\VersionedStartupExecutor.cpp src\Loader\DllMain.cpp
git commit -m "Migrate test mode timing to version profiles"
```

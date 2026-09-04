# Windowed Widescreen and VMT Migration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Move Windowed Widescreen's 40 byte-site contracts, nine pointer contracts, 36 hook bindings, function ABIs, and native layout into a feature-owned 4.71 profile, using checked global vtable-slot replacement for its two game-owned slots.

**Architecture:** The Widescreen profile describes 18 inline hooks, 16 mid hooks, two global vtable-slot hooks, six read-only byte sites, seven other read-only pointer contracts, and 36 callback ABIs. HookRegistry owns SafetyHook detours; RuntimeImage performs checked global slot exchange. Rendering/window/gameplay policy remains feature-owned.

**Tech Stack:** C++23, RuntimeImage, GameBuild profiles, SafetyHook inline/mid callbacks, checked atomic vtable-slot replacement, Direct3D9, Win32 window policy, CMake/Ninja/MSVC, IDA-CLI.

**Spec:** `docs/superpowers/specs/2026-09-05-loader-codebase-cleanup-design.md`

## Global Constraints

- Complete Plans 01 through 06f first.
- When Windowed Widescreen is disabled, contribute no Widescreen sites,
  including no renderer pre/post-reset hooks or vtable-slot replacements.
- Preserve V1 scope: ordinary unrotated desktop/windowed upright output. Do not
  add fullscreen, borderless, monitor rotation, Windows rotation, or a rotation
  setting.
- The two game-owned global vtable entries affect current and future objects;
  use checked global slot replacement. Do not use SafetyHook `VmtHook`/`VmHook`,
  which clone a live object's vtable.
- A future per-object virtual method interception may use SafetyHook
  `VmtHook`/`VmHook` with object-lifetime ownership, but this plan adds none.
- Revalidate every byte/pointer/ABI/layout contract against
  `H:\gc\game471.exe.i64` before moving it.
- Do not add fake D3D/window/native object tests or callback recorders.

---

## Task 1: Revalidate the complete Widescreen manifest

**Files:**

- Create: `.codex-tmp/loader-cleanup-widescreen-profile.py` (untracked)
- Read: `src/Patches/WindowedWidescreen/WindowedWidescreenAbi.*`
- Read: `src/Patches/WindowedWidescreen/WindowedWidescreenPatch.*`
- Read: `src/Patches/WindowedWidescreen/WindowedWidescreenPatchTransaction.*`

- [ ] **Step 1: Verify all 42 current byte rows**

Require exact RVA/prefix and decoded meaning for every row in
`WindowedWidescreenByteContracts()`. Classify the current rows as exactly:

```text
18 inline-hook entry sites
16 mid-hook instruction sites
6 read-only byte/call targets
2 global vtable-slot pointer encodings
```

The migrated profile represents the two vtable rows as pointer contracts, so
its byte-contract count becomes 40. No site may disappear silently.

- [ ] **Step 2: Verify all nine pointer rows**

Verify slot RVA, expected target RVA, ownership, and callback ABI for five main
config setters, common 2D render, common 3D render, MovieClip accept, and shape
draw visit. Prove the last two are game-owned global vtable slots.

- [ ] **Step 3: Verify all 36 hook ABIs**

For each `WindowedWidescreenFunctionAbi`, validate hook kind, calling
convention, argument count, original-call behavior, relevant registers/stack,
and protected span. The two reset hooks must match the Renderer Device Loss
before/after callbacks from Plan 06f.

- [ ] **Step 4: Verify layout and global-data facts**

Recheck main config vtable, renderer owner offsets, fixed decorated style,
batch queue pointer/count/stride/pending count, mouse words, MovieClip name/
hash offsets, gameplay feedback/tune collection offsets, network-status matrix
stack slot, and any other numeric native fact still in
`WindowedWidescreenPatch.cpp`.

---

## Task 2: Add checked global `VtableSlotHook`

**Files:**

- Create: `src/Patches/RuntimeImage/VtableSlotHook.h`
- Create: `src/Patches/RuntimeImage/VtableSlotHook.cpp`
- Modify: `src/Patches/RuntimeImage/CMakeLists.txt`
- Modify: `src/Loader/VersionedStartupExecutor.cpp`

**Interfaces:**

```cpp
namespace gc::runtime_image {

struct VtableSlotHook final {
    SiteIdentity identity;
    Rva slot_rva{};
    void* expected_original{};
    void* replacement{};
    void** original_storage{};
};

[[nodiscard]] std::expected<void, RuntimeImageError>
InstallVtableSlotHook(
    const RuntimeImage&,
    const VtableSlotHook&) noexcept;

} // namespace gc::runtime_image
```

- [ ] **Step 1: Publish original before exchange**

Validate nonnull pointers/storage, resolve the pointer-sized slot, publish the
profile's expected original into typed feature storage, then call
`RuntimeImage::ExchangePointer`. The expected pointer must still be present at
the atomic compare/exchange.

- [ ] **Step 2: Require protection restoration and read-back**

Use RuntimeImage's exact-span protection and error flags. A restore failure,
unexpected prior pointer, or wrong final pointer aborts through the common
executor. Do not call `safetyhook::unprotect` and do not write the original
back.

- [ ] **Step 3: Keep ownership process-lifetime**

Record installed slot identity in the process registry/approved plan for
diagnostics, but add no destructor that reverses the slot during DLL detach.

---

## Task 3: Add `WindowedWidescreenProfile`

**Files:**

- Create: `src/Patches/WindowedWidescreen/WindowedWidescreenProfile.h`
- Create: `src/Patches/WindowedWidescreen/WindowedWidescreenProfile.cpp`
- Modify: `src/Patches/WindowedWidescreen/WindowedWidescreenAbi.h`
- Modify: `src/Patches/WindowedWidescreen/WindowedWidescreenAbi.cpp`
- Modify: `src/Patches/CMakeLists.txt`

**Interfaces:**

```cpp
struct WidescreenNativeLayout final {
    runtime_image::Rva main_config_vtable{};
    runtime_image::Rva batch_queue_pointer{};
    std::size_t renderer_owner_device_offset{};
    std::size_t renderer_owner_window_offset{};
    std::size_t renderer_owner_style_offset{};
    // Remaining verified queue, mouse, MovieClip, gameplay, and matrix fields.
};

struct WindowedWidescreenProfile final {
    game_version::GameBuild build;
    game_version::GameImageVariant variant;
    std::array<ByteSiteContract, 40> byte_contracts;
    std::array<PointerContract, 9> pointer_contracts;
    std::array<WidescreenFunctionAbi, 36> function_abis;
    std::array<game_version::VersionedOperation, 36> hooks;
    WidescreenNativeLayout layout;
};

[[nodiscard]] std::optional<WindowedWidescreenProfile>
ProfileFor(game_version::GameBuild,
           game_version::GameImageVariant) noexcept;
```

- [ ] **Step 1: Normalize common patterns and site kinds**

Replace the feature `BytePattern` with `runtime_image::BytePattern` and
`WidescreenHookKind` with common operation kinds. Keep
`WidescreenContractSite` for feature diagnostics.

- [ ] **Step 2: Move every concrete native fact into the profile**

The general ABI header keeps typed callback signatures and semantic field
names only. Concrete RVAs, expected bytes/pointers, function ABI rows, object
offsets, globals, strides, counts, and styles reside in the selected profile.

- [ ] **Step 3: Assert manifest counts at compile time**

Require 40 byte contracts, nine pointer contracts, 18 inline operations, 16
mid operations, two vtable-slot operations, six read-only byte rows, seven
read-only pointer rows, and 36 function ABI rows. A count change requires an
explicit design/plan update.

- [ ] **Step 4: Expose no default older profile**

Only verified 4.71 variants return the profile. Any older build must supply a
complete separate layout/manifest or declare Widescreen unavailable.

---

## Task 4: Delete the Widescreen hook transaction and local ownership

**Files:**

- Delete: `src/Patches/WindowedWidescreen/WindowedWidescreenPatchTransaction.h`
- Delete: `src/Patches/WindowedWidescreen/WindowedWidescreenPatchTransaction.cpp`
- Modify: `src/Patches/WindowedWidescreen/WindowedWidescreenPatch.h`
- Modify: `src/Patches/WindowedWidescreen/WindowedWidescreenPatch.cpp`
- Modify: `src/Patches/CMakeLists.txt`

- [ ] **Step 1: Remove installation abstractions**

Delete `WidescreenContractManifest`, `WidescreenHookRequest`,
`WidescreenInstallActions`, `InstallWindowedWidescreenHooks`, hook capacities,
create/enable/reset adapters, candidate rollback, resource detach rollback,
owner unpublish, and rollback diagnostic fields.

- [ ] **Step 2: Remove SafetyHook objects from feature runtime**

Store typed originals needed by inline detours and immutable callback state;
the registry owns all 34 SafetyHook objects. Store expected originals for the
two vtable detours as typed function pointers published before exchange.

- [ ] **Step 3: Bind renderer reset callbacks through the profile**

Use the Widescreen `reset_pre` and `reset_post` contracts at RVAs `0x0005B28B`
and `0x0005B474` with the Renderer Device Loss callbacks. They participate in
the Widescreen plan only when enabled and remain part of the global preflight.

- [ ] **Step 4: Preserve feature candidate preparation**

Construct compositor/device/resource/window/render-space/gameplay feedback
state before hook enable. Candidate preparation errors remain feature errors
and call `AbortProcess`; there is no detach/reset attempt after mutation.

---

## Task 5: Preserve distinct VMT mechanisms

**Files:**

- Modify: `src/Patches/WindowedWidescreen/WindowedWidescreenPatch.cpp`
- Modify: `docs/architecture/loader-cleanup-baseline.md`

- [ ] **Step 1: Replace exactly two global slots**

Install only:

```text
MovieClip instance vtable + 0x14: expected target RVA 0x000E0CD0
MovieClip draw visitor vtable + 0x4C: expected target RVA 0x000CC880
```

The selected profile supplies both slot RVAs and expected targets.

- [ ] **Step 2: Record no per-object VMT use**

Append a post-migration baseline note: two checked global slot hooks, zero
`VmtHook`, zero `VmHook`, and one separate Test Mode Timing carrier-vtable
mechanism. Do not conflate them in diagnostics or naming.

---

## Task 6: Add Widescreen to game composition

**Files:**

- Modify: `src/Loader/GameVersionedStartupPlan.cpp`
- Modify: `src/Loader/VersionedStartupExecutor.cpp`
- Modify: `src/Loader/DllMain.cpp`

- [ ] **Step 1: Add all enabled contracts before mutation**

When enabled, add all 40 byte and nine pointer contracts and 36 hook operations
to the global validator. When disabled, add none. Treat a missing enabled
profile as fatal unsupported-build.

- [ ] **Step 2: Declare dependencies**

Widescreen installs after Renderer Device Loss runtime/resource state and
after Test Mode Timing where their render/test-mode seams interact. Preserve
the current explicit hook order from the manifest; do not sort by RVA.

- [ ] **Step 3: Remove eager initialization**

Delete `WindowedWidescreenPatchInit` from the migration startup path. Keep its
settings-to-runtime preparation as the feature's plan-builder/state-builder
interface.

---

## Task 7: Verify and commit

- [ ] **Step 1: Audit counts and ownership**

```powershell
rg -n 'WindowedWidescreenPatchTransaction|WidescreenInstallActions|rollback|create_inline|create_mid|safetyhook::unprotect|InterlockedCompareExchangePointer|safetyhook::InlineHook|safetyhook::MidHook' src\Patches\WindowedWidescreen
rg -n 'VmtHook|VmHook' src
```

Expected: no transaction/local SafetyHook/unprotect/CAS implementation; no
production `VmtHook`/`VmHook` use; checked CAS exists only in RuntimeImage.

- [ ] **Step 2: Run static verification**

```powershell
cmake --build --preset msvc32-debug
ctest --preset msvc32-debug -j 4
cmake --build --preset msvc32-release
ctest --preset msvc32-release -j 4
git diff --check
git status --short --branch
```

Do not claim visual placement, window behavior, device reset, tutorial/test-
mode containment, network status, or gameplay HUD runtime acceptance.

- [ ] **Step 3: Commit**

```powershell
git add -- src\Patches\RuntimeImage src\Patches\WindowedWidescreen src\Patches\CMakeLists.txt src\Loader\GameVersionedStartupPlan.cpp src\Loader\VersionedStartupExecutor.cpp src\Loader\DllMain.cpp docs\architecture\loader-cleanup-baseline.md
git commit -m "Migrate widescreen and vtable hooks to profiles"
```

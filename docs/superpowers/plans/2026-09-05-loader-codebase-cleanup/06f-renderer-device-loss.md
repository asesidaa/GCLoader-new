# Renderer Device Loss Versioned Migration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Move Renderer Device Loss's six mid hooks, four continuation/ABI contracts, and native layout offsets into a feature-owned 4.71 profile while keeping D3D resource lifecycle policy independent of hook ownership.

**Architecture:** `RendererDeviceLossProfile` owns game-image facts. HookRegistry owns six core detours. `RendererResourceLifecycle` remains the behavior owner and exposes two callbacks that the Widescreen profile will bind to its pre/post-reset sites in Plan 06g.

**Tech Stack:** C++23, RuntimeImage, GameBuild profiles, SafetyHook mid-hook context, Direct3D9 resource lifecycle, CMake/Ninja/MSVC, IDA-CLI.

**Spec:** `docs/superpowers/specs/2026-09-05-loader-codebase-cleanup-design.md`

## Global Constraints

- Complete Plans 01 through 06e first.
- Renderer Device Loss remains mandatory for the game process. Any profile,
  contract, or hook install failure aborts.
- Preserve D3D result interpretation, retry branches, output initialization,
  cleanup continuations, and renderer/resource ownership exactly.
- Widescreen pre/post-reset targets are not guessed here; Plan 06g supplies
  their versioned addresses from the Widescreen profile.
- Revalidate every register, stack, branch target, and structure offset against
  `H:\gc\game471.exe.i64` before moving constants.
- Do not add fake D3D devices, hook engines, callback recorders, or static tests
  that claim device-loss behavior.

---

## Task 1: Revalidate the renderer hook and control-flow contracts

**Files:**

- Create: `.codex-tmp/loader-cleanup-renderer-device-loss-profile.py` (untracked)
- Read: `src/Patches/RendererDeviceLoss/RendererDeviceLossPatch.*`
- Read: `src/Patches/RendererDeviceLoss/RendererResourceLifecycle.*`

- [ ] **Step 1: Verify the six physical hook sites**

Require exact current prefixes, decoded instruction spans, and callback state
for:

```text
0x000E67D8 device_lost_tail
0x000E79F7 vertex_buffer_result
0x000E7A84 index_buffer_result
0x000E5578 vertex_buffer_lock_guard
0x000E691E direct_lock_result
0x000E5662 buffered_unlock_result
```

Use the actual `k*Pattern` arrays as comparison inputs, but report the bytes
read from IDA independently.

- [ ] **Step 2: Verify four non-hook targets**

Verify renderer initializer epilogue `0x000E7EE9`, vertex-buffer lock failure
`0x000E55E2`, direct batch cleanup `0x000E6AD6`, and buffered unlock
continuation `0x000E5679`. Each becomes a read-only contract because callbacks
redirect execution to it.

- [ ] **Step 3: Verify native layout operands**

Recheck renderer initialized offset `0x484`, index-buffer holder offset
`0x778`, vertex-buffer lock output stack offset `0x14`, register meanings, and
the renderer owner passed at post-reset.

---

## Task 2: Add `RendererDeviceLossProfile`

**Files:**

- Create: `src/Patches/RendererDeviceLoss/RendererDeviceLossProfile.h`
- Create: `src/Patches/RendererDeviceLoss/RendererDeviceLossProfile.cpp`
- Modify: `src/Patches/RendererDeviceLoss/RendererDeviceLossPatch.h`
- Modify: `src/Patches/CMakeLists.txt`

**Interfaces:**

```cpp
struct RendererNativeLayout final {
    std::size_t initialized_offset{};
    std::size_t index_buffer_holder_offset{};
    std::size_t vertex_buffer_lock_output_stack_offset{};
    runtime_image::Rva initializer_epilogue{};
    runtime_image::Rva vertex_buffer_lock_failure{};
    runtime_image::Rva direct_batch_cleanup{};
    runtime_image::Rva buffered_unlock_continuation{};
};

struct RendererDeviceLossProfile final {
    game_version::GameBuild build;
    game_version::GameImageVariant variant;
    std::array<game_version::VersionedOperation, 10> operations;
    RendererNativeLayout layout;
};

[[nodiscard]] std::optional<RendererDeviceLossProfile>
ProfileFor(game_version::GameBuild,
           game_version::GameImageVariant) noexcept;
```

The ten operations are six mid hooks and four read-only branch/call targets.

- [ ] **Step 1: Move all game-image facts into the profile**

Delete concrete RVAs, byte arrays, and offsets from the general patch header.
Use named profile fields and `runtime_image::BytePattern`. Add compile-time
assertions for six hooks, four read-only targets, and ten total operations.

- [ ] **Step 2: Keep D3D policy out of the profile**

The profile contains no retry count, COM/resource container, logging policy,
or renderer behavior. It describes only versioned native contracts/layout.

---

## Task 3: Move hook ownership to the process registry

**Files:**

- Modify: `src/Patches/RendererDeviceLoss/RendererDeviceLossPatch.cpp`
- Modify: `src/Patches/RendererDeviceLoss/RendererDeviceLossPatch.h`
- Modify: `src/Patches/RendererDeviceLoss/RendererResourceLifecycle.*`

- [ ] **Step 1: Delete the six feature-owned `MidHook` objects**

Remove core hook fields, local preflight reads, create calls, reset callbacks,
and `RendererInstallActions` hook mechanics. Callbacks receive immutable
`RendererNativeLayout` through published process-lifetime runtime state.

- [ ] **Step 2: Preserve callback transformations**

Keep all existing context-register mutations, D3D HRESULT checks, output
pointer writes, retry decisions, and continuation redirections. Replace only
the source of addresses/offsets.

- [ ] **Step 3: Make callback state stable before enable**

Construct and publish the renderer runtime and resource lifecycle before the
first of the six hook operations installs. No callback may observe null or
moved feature state.

---

## Task 4: Replace the Widescreen reset-hook pair transaction with callbacks

**Files:**

- Modify: `src/Patches/RendererDeviceLoss/RendererDeviceLossPatch.h`
- Modify: `src/Patches/RendererDeviceLoss/RendererDeviceLossPatch.cpp`
- Modify: `src/Patches/WindowedWidescreen/WindowedWidescreenPatch.cpp` only to
  compile against the new contribution boundary

- [ ] **Step 1: Delete local reset-hook ownership**

Remove `RendererResetHookPair`, `RendererResetHookPairActions`, pair states,
`PrepareDisabled`, `Enable`, `Reset`, and the two Widescreen hook fields.

- [ ] **Step 2: Expose behavior callbacks, not installation functions**

Expose:

```cpp
void OnWidescreenBeforeReset(safetyhook::Context&) noexcept;
void OnWidescreenAfterReset(safetyhook::Context&) noexcept;
```

and a typed resource-failure publisher. Plan 06g binds these callbacks to its
two versioned sites. Renderer Device Loss never receives raw Widescreen RVAs.

- [ ] **Step 3: Preserve resource failure semantics**

Before-reset and after-reset resource errors remain fatal through the common
abort publisher. Do not silently continue with invalid compositor resources.

---

## Task 5: Add Renderer Device Loss to game composition

**Files:**

- Modify: `src/Loader/GameVersionedStartupPlan.cpp`
- Modify: `src/Loader/VersionedStartupExecutor.cpp`
- Modify: `src/Loader/DllMain.cpp`

- [ ] **Step 1: Add all ten mandatory contracts**

Request the selected renderer profile unconditionally for the game process.
Add its six hooks and four read-only targets to the complete barrier.

- [ ] **Step 2: Preserve current install order**

Declare the six core hooks in their current manifest order. Construct resource
lifecycle state first. The Widescreen reset callbacks remain unbound until
Plan 06g adds its enabled feature plan.

- [ ] **Step 3: Remove eager installation**

Delete `RendererDeviceLossPatchInit()` from the migration startup path. No
renderer hook may be created outside HookRegistry.

---

## Task 6: Verify and commit

- [ ] **Step 1: Audit direct mechanics**

```powershell
rg -n 'create_mid|MidHook::create|safetyhook::MidHook|RendererResetHookPair|RendererInstallActions|PrepareResetHooks|EnableResetHooks|ResetHooks|k[A-Za-z0-9_]+Rva|k[A-Za-z0-9_]+Offset' src\Patches\RendererDeviceLoss --glob '!RendererDeviceLossProfile.cpp'
```

Expected: no local hook/reset transaction or concrete 4.71 constant outside
the profile; callback context signatures remain.

- [ ] **Step 2: Run static verification**

```powershell
cmake --build --preset msvc32-debug
ctest --preset msvc32-debug -j 4
cmake --build --preset msvc32-release
ctest --preset msvc32-release -j 4
git diff --check
git status --short --branch
```

Do not claim D3D device-loss, reset, retry, or resource-recovery behavior.

- [ ] **Step 3: Commit**

```powershell
git add -- src\Patches\RendererDeviceLoss src\Patches\WindowedWidescreen\WindowedWidescreenPatch.cpp src\Patches\CMakeLists.txt src\Loader\GameVersionedStartupPlan.cpp src\Loader\VersionedStartupExecutor.cpp src\Loader\DllMain.cpp
git commit -m "Migrate renderer hooks to version profiles"
```

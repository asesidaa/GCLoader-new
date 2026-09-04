# Switch Input Versioned Migration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Move the optional Switch gameplay input detours into a feature-owned 4.71 profile and the complete versioned preflight barrier, with originals published by the central HookRegistry and fatal failure instead of silent Arcade fallback.

**Architecture:** Switch policy remains feature-owned. Its profile contributes two inline hook entry contracts and one mid-hook instruction contract only when Switch style is selected. Callback state stores typed trampolines rather than SafetyHook objects; the process registry owns the physical hooks.

**Tech Stack:** C++23, RuntimeImage contracts, GameBuild profiles, SafetyHook-backed HookRegistry, x86 `__thiscall`/`__fastcall`, SafetyHook mid-hook context, CMake/Ninja/MSVC, IDA-CLI.

**Spec:** `docs/superpowers/specs/2026-09-05-loader-codebase-cleanup-design.md`

## Global Constraints

- Complete Plans 01 through 06a first.
- When configured for Arcade style, contribute no versioned sites and preserve
  the current native input path.
- When configured for Switch style, all three hooks are mandatory. A profile,
  contract, create, or enable failure aborts startup; do not silently fall back
  to Arcade.
- Preserve high-rate input transport and gameplay semantics. This migration
  changes ownership and validation only.
- Revalidate query ABIs, stack offsets, register state, and exact instruction
  prefixes against `H:\gc\game471.exe.i64` before editing.
- Do not add fake hooks, stack-access callback tests, or synthetic executable
  memory as proof.

---

## Task 1: Freeze the Switch native profile from IDA

**Files:**

- Create: `.codex-tmp/loader-cleanup-switch-profile.py` (untracked evidence helper)
- Read: `src/Input/Switch/SwitchInputPatch.h`
- Read: `src/Input/Switch/SwitchInputPatch.cpp`

- [ ] **Step 1: Revalidate the three hook contracts**

Require:

| Site | RVA | Expected prefix | Hook kind |
|---|---:|---|---|
| pressed edge query | `0x00259640` | `55 8B EC 83 EC 18 89 4D EC C6 45 FF 00 8B 4D EC` | inline |
| held state query | `0x00259570` | same 16-byte entry prefix | inline |
| diagonal match | `0x001D32A0` | `0F B6 55 8B 83 FA 01 75 2B` | mid |

The IDA output must also establish:

- pressed/held receiver and argument order;
- `__thiscall` native entry and `__fastcall` detour bridge;
- return width `std::uint8_t`;
- diagonal frame-pointer ownership;
- local offsets `-0x75`, `-0x7C`, and `-0x68` and their value widths;
- the protected instruction span consumed by SafetyHook.

Stop if any fact differs; do not adjust a constant from disassembly alone
without following its control-flow/ownership use.

---

## Task 2: Add `SwitchInputProfile`

**Files:**

- Create: `src/Input/Switch/SwitchInputProfile.h`
- Create: `src/Input/Switch/SwitchInputProfile.cpp`
- Modify: `src/Input/Switch/SwitchInputPatch.h`
- Modify: `src/Input/CMakeLists.txt`

**Interfaces:**

```cpp
struct SwitchInputProfile final {
    game_version::GameBuild build;
    game_version::GameImageVariant variant;
    std::array<game_version::VersionedOperation, 3> operations;
    std::ptrdiff_t native_match_offset{};
    std::ptrdiff_t target_direction_offset{};
    std::ptrdiff_t current_direction_offset{};
};

[[nodiscard]] std::optional<SwitchInputProfile>
ProfileFor(
    game_version::GameBuild,
    game_version::GameImageVariant) noexcept;

[[nodiscard]] game_version::FeaturePlan
BuildSwitchInputPlan(
    const game_version::BuildSelection<
        game_version::GameBuild,
        game_version::GameImageVariant>&,
    const SwitchInputSettings&) noexcept;
```

- [ ] **Step 1: Move all native constants into the profile**

The policy header keeps logical input aliases only. RVAs, byte prefixes,
protected spans, native calling convention metadata, and diagonal stack
offsets move to `SwitchInputProfile`.

- [ ] **Step 2: Return no plan for Arcade**

`BuildSwitchInputPlan` returns an empty optional contribution for Arcade. For
Switch, absence of a profile is an `unsupported_feature` startup error, not a
fallback.

- [ ] **Step 3: Give each operation a stable name/order**

Use `pressed_edge`, `held_state`, and `diagonal_match`, with install order
pressed -> held -> diagonal. The global validator checks all three before the
first versioned operation anywhere in the process.

---

## Task 3: Remove feature-local SafetyHook ownership

**Files:**

- Modify: `src/Input/Switch/SwitchInputPatch.cpp`
- Modify: `src/Input/Switch/SwitchInputPatch.h`

- [ ] **Step 1: Replace hook-object originals with typed trampolines**

Define the native function type once:

```cpp
using GameplayQueryFn = std::uint8_t(__thiscall*)(
    void* self,
    int input_device_id,
    LogicalInputId logical_input,
    int gameplay_frame);
```

Store one pressed and one held trampoline in process-lifetime callback state.
The HookRegistry publishes them after disabled creation and before enable.
`query_gameplay_with_aliases` calls these pointers directly; it does not know
about `safetyhook::InlineHook`.

- [ ] **Step 2: Retain only feature runtime state**

Keep active style and diagnostic counters. Remove `g_pressed_edge_hook`,
`g_held_state_hook`, `g_diagonal_match_hook`, `reset_hooks`,
`install_hooks_transactionally`, `HookCreationResults`, local base/RVA helpers,
local signature reads, and all create/reset calls.

- [ ] **Step 3: Publish Switch behavior only after all three installs**

Leave active state as Arcade while the common executor installs the three
hooks; callbacks reached during that interval call their original trampoline.
After all three succeed, run one feature completion callback that stores
Switch with release semantics. A failure aborts before startup continues.

- [ ] **Step 4: Preserve callback hot-path behavior**

Keep first-acceptance logging, guarded stack access, direction alias policy,
and exception containment. Do not add successful-call logging or allocation.

---

## Task 4: Add Switch to the game versioned plan

**Files:**

- Modify: `src/Loader/GameVersionedStartupPlan.cpp`
- Modify: `src/Loader/VersionedStartupExecutor.cpp`
- Modify: `src/Loader/DllMain.cpp` only to remove the dormant duplicate path

- [ ] **Step 1: Contribute the optional plan explicitly**

After the framerate-independent settings are available, call
`BuildSwitchInputPlan(selection, settings.switch_input())`. Declare it after
GameCompatibility and before any feature that calls the native input query
functions during initialization.

- [ ] **Step 2: Remove the old eager initializer**

Delete `SwitchInputPatchInit(settings.switch_input())` from the migration
startup path. No second call may create or activate hooks outside the approved
plan.

---

## Task 5: Verify and commit

- [ ] **Step 1: Audit ownership and constants**

```powershell
rg -n 'create_inline|create_mid|InlineHook|MidHook|reset_hooks|preflight_signatures|kGameplayPressedQueryRva|kGameplayHeldQueryRva|kDiagonalMatchRva' src\Input\Switch
```

Expected: creation/ownership is absent; native constants occur only in the
profile; SafetyHook context remains only in the mid callback signature.

- [ ] **Step 2: Run static verification**

```powershell
cmake --build --preset msvc32-debug
ctest --preset msvc32-debug -j 4
cmake --build --preset msvc32-release
ctest --preset msvc32-release -j 4
git diff --check
git status --short --branch
```

Do not claim Arcade/Switch feel, diagonal matching, or high-FPS input runtime
behavior.

- [ ] **Step 3: Commit**

```powershell
git add -- src\Input\Switch src\Input\CMakeLists.txt src\Loader\GameVersionedStartupPlan.cpp src\Loader\VersionedStartupExecutor.cpp src\Loader\DllMain.cpp
git commit -m "Migrate Switch input hooks to version profiles"
```

# Absolute Judgement Versioned Migration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Move every Absolute Judgement native hook, callable target, structure offset, and callback ABI into a feature-owned 4.71 profile and the complete versioned startup barrier.

**Architecture:** The feature keeps its scheduler, history, scope, stage, diagnostics, and runtime policy. `AbsoluteJudgementProfile` owns all game-image facts. The central registry owns ten physical hooks and publishes typed originals; Absolute Judgement owns only process-lifetime callback/runtime state.

**Tech Stack:** C++23, RuntimeImage, GameBuild profiles, SafetyHook inline/mid callbacks, x86 calling conventions and register context, CMake/Ninja/MSVC, IDA-CLI.

**Spec:** `docs/superpowers/specs/2026-09-05-loader-codebase-cleanup-design.md`

## Global Constraints

- Complete Plans 01 through 06b first.
- Treat enabled and disabled modes separately: the three lifecycle hooks remain
  mandatory in both modes; the judgement replacement hooks and timing-grade
  diagnostic are mandatory when Absolute Judgement is enabled.
- Do not weaken fatal invariants or scheduler/runtime correctness to simplify
  ownership. This plan changes image-version data and hook installation only.
- If any included hook cannot be created or enabled, abort startup. The current
  warning-only timing-grade install failure is removed when the feature is
  enabled.
- Revalidate every site and ABI directly in `H:\gc\game471.exe.i64` using a
  bounded saved IDA-CLI script. Do not infer older-version compatibility.
- Do not add fake hooks, fake native objects, callback recorders, or copied
  executable fixtures.

---

## Task 1: Revalidate the complete native contract

**Files:**

- Create: `.codex-tmp/loader-cleanup-absolute-judgement-profile.py` (untracked)
- Read: `src/Patches/AbsoluteJudgement/NativeJudgementAbi.h`
- Read: `src/Patches/AbsoluteJudgement/AbsoluteJudgementPatch.cpp`
- Read: `src/Patches/AbsoluteJudgement/AbsoluteJudgementRuntime.cpp`

- [ ] **Step 1: Verify all ten hook sites**

Verify RVA, exact prefix, decoded instructions, protected span, live register/
stack state, and callback timing for:

```text
0x26251C gameplay_initialization  mid
0x2641CC semantic_stage_entry     mid
0x264D9A semantic_stage_exit      mid
0x240239 loop_guard               mid
0x22DFB0 pressed                  inline
0x22DF50 held                     inline
0x22DD30 released                 inline
0x22E480 direction                inline
0x22DAA0 held_age                 inline
0x1D0E00 timing_grade             inline
```

Require exact equality with the current prefix arrays, including the 18-byte
timing-grade prefix. Stop on a discrepancy.

- [ ] **Step 2: Verify every callable native target**

Follow callers and type the ABI for:

```text
0x2402D0 loop tail
0x1D68E0 recognition
0x1CF930 score
0x001040 get input manager
0x0011D0 get global
0x0011E0 get config
0x210400 get sound manager
0x2122B0 get group cursor
```

Capture an exact function-entry or call-site byte contract for each so the
unknown-hash barrier can validate more than an RVA guess.

- [ ] **Step 3: Verify layout and ownership offsets**

Recheck tune stack offsets `-0x32C` and `-0x2B4`, collection offsets,
judgement/score state offsets, player index, booster, game time, safe-frame,
score counters, publication bytes, timing-grade float index, and sound group.
For every offset record the owning object/function and value width.

---

## Task 2: Add `AbsoluteJudgementProfile`

**Files:**

- Create: `src/Patches/AbsoluteJudgement/AbsoluteJudgementProfile.h`
- Create: `src/Patches/AbsoluteJudgement/AbsoluteJudgementProfile.cpp`
- Modify: `src/Patches/AbsoluteJudgement/NativeJudgementAbi.h`
- Modify: `src/Patches/CMakeLists.txt`

**Interfaces:**

```cpp
struct AbsoluteJudgementNativeLayout final {
    runtime_image::Rva loop_tail;
    runtime_image::Rva recognition;
    runtime_image::Rva score;
    runtime_image::Rva get_input_manager;
    runtime_image::Rva get_global;
    runtime_image::Rva get_config;
    runtime_image::Rva get_sound_manager;
    runtime_image::Rva get_group_cursor;
    // Existing stack, object, publication, score, and timing offsets follow.
};

struct AbsoluteJudgementProfile final {
    game_version::GameBuild build;
    game_version::GameImageVariant variant;
    std::array<game_version::VersionedOperation, 18> enabled_operations;
    std::array<game_version::VersionedOperation, 3> disabled_operations;
    AbsoluteJudgementNativeLayout layout;
};

[[nodiscard]] std::optional<AbsoluteJudgementProfile>
ProfileFor(game_version::GameBuild,
           game_version::GameImageVariant) noexcept;
```

- [ ] **Step 1: Move all image facts out of generic runtime headers**

`NativeJudgementAbi.h` retains typed C++ function signatures and semantic
layout field names, but the selected profile supplies their concrete RVAs and
offsets. No gameplay source file reads a global 4.71 constant directly.

- [ ] **Step 2: Build exact enabled/disabled manifests**

Disabled mode contributes the three lifecycle mid hooks. Enabled mode
contributes all ten hooks plus eight read-only callable-target contracts.
Avoid duplicate site rows by having enabled composition reference the shared
lifecycle entries from the profile.

- [ ] **Step 3: Require explicit profile absence for older builds**

Return `nullopt` for every unverified build. Do not use the 4.71 layout as a
default member initializer or fallback.

---

## Task 3: Replace hook objects with typed originals and registry ownership

**Files:**

- Modify: `src/Patches/AbsoluteJudgement/AbsoluteJudgementPatch.h`
- Modify: `src/Patches/AbsoluteJudgement/AbsoluteJudgementPatch.cpp`
- Modify: `src/Patches/AbsoluteJudgement/AbsoluteJudgementRuntime.h`
- Modify: `src/Patches/AbsoluteJudgement/AbsoluteJudgementRuntime.cpp`

- [ ] **Step 1: Publish inline trampolines into callback state**

Use the existing `PressedFn`, `HeldFn`, `ReleasedFn`, `DirectionFn`,
`HeldAgeFn`, and `TimingGradeFn` aliases as original slots. Callback code calls
those typed pointers, never `InlineHook::unsafe_thiscall` on a feature-owned
hook object.

- [ ] **Step 2: Publish the selected layout immutably**

`InitializeAbsoluteJudgementRuntime` receives a value-owned
`AbsoluteJudgementNativeLayout` and resolves its callable addresses through
the already selected RuntimeImage. It does not reselect a build or read global
4.71 constants.

- [ ] **Step 3: Delete the local hook transaction**

Remove `AbsoluteJudgementHooks`, `g_hooks`, `g_active_hooks`, `InstallStage`,
`InstallHooks`, `InstallTimingGradeDiagnosticHook`, prefix-reading helpers,
per-hook create/enable loops, and local install-failure publisher. Hook errors
flow through the common versioned executor and `AbortProcess`.

- [ ] **Step 4: Preserve runtime-fatal behavior**

Runtime invariant failures inside an active stage remain feature-typed and
fatal. Route their final publication through the once-only common abort
publisher without changing predicate IDs, operand capture, or log semantics.

---

## Task 4: Preserve deliberate installation and publication order

**Files:**

- Modify: `src/Patches/AbsoluteJudgement/AbsoluteJudgementProfile.cpp`
- Modify: `src/Loader/GameVersionedStartupPlan.cpp`
- Modify: `src/Loader/VersionedStartupExecutor.cpp`

- [ ] **Step 1: Declare current operational order**

For enabled mode, use:

```text
pressed -> held -> released -> direction -> held_age -> loop_guard
-> semantic_stage_exit -> gameplay_initialization -> semantic_stage_entry
-> timing_grade
```

For disabled mode, use:

```text
semantic_stage_exit -> gameplay_initialization -> semantic_stage_entry
```

The semantic entry hook remains the final core activation point. Timing-grade
remains later but is now required when included.

- [ ] **Step 2: Publish callback/runtime state before enable**

Construct and publish the scheduler/runtime owner, selected layout, and inline
original slots before the relevant hooks become callable. No hook sees a
moved-from owner.

- [ ] **Step 3: Add the feature requirement explicitly**

Absolute Judgement contributes lifecycle hooks for every game startup. Its
configured enabled value selects the three-site or full manifest. Declare its
dependencies on prepared input transition transport and selected audio clock
domain without making those modules depend on versioning.

---

## Task 5: Verify and commit

- [ ] **Step 1: Audit native constants and SafetyHook ownership**

```powershell
rg -n 'InlineHook::create|MidHook::create|safetyhook::InlineHook|safetyhook::MidHook|InstallHooks|InstallTimingGradeDiagnosticHook|k[A-Za-z0-9_]+Rva' src\Patches\AbsoluteJudgement
```

Expected: no feature-owned hook object/create loop; concrete RVAs occur only
in `AbsoluteJudgementProfile.cpp`; callback context types remain where ABI
requires them.

- [ ] **Step 2: Run complete static verification**

```powershell
cmake --build --preset msvc32-debug
ctest --preset msvc32-debug -j 4
cmake --build --preset msvc32-release
ctest --preset msvc32-release -j 4
git diff --check
git status --short --branch
```

Do not claim note timing, grades, input history, audio-clock alignment, or
gameplay acceptance.

- [ ] **Step 3: Commit**

```powershell
git add -- src\Patches\AbsoluteJudgement src\Patches\CMakeLists.txt src\Loader\GameVersionedStartupPlan.cpp src\Loader\VersionedStartupExecutor.cpp
git commit -m "Migrate absolute judgement to version profiles"
```

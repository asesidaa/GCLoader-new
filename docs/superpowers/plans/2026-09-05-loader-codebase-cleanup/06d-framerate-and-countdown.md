# Framerate and Countdown Versioned Migration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Move the 17-write/53-hook framerate manifest and the 32-site countdown freeze patch into feature-owned game profiles and the one complete versioned barrier, deleting the last patch transaction and rollback implementation.

**Architecture:** Static 4.71 native contracts live in `FramerateGameProfile` and `CountdownProfile`; configured target-FPS math remains in a renamed timing profile. The common validator approves the complete manifest, RuntimeImage performs direct writes, HookRegistry owns hooks, and feature runtime stores only trampolines/state needed by callbacks.

**Tech Stack:** C++23, RuntimeImage, GameBuild profiles, SafetyHook inline/mid callback ABI, checked timing math, CMake/Ninja/MSVC, IDA-CLI.

**Spec:** `docs/superpowers/specs/2026-09-05-loader-codebase-cleanup-design.md`

## Global Constraints

- Complete Plans 01 through 06c first.
- Preserve the game's 60 Hz authored frame unit and the separation between
  high-rate input transport and chart/render/judgement/score cadence.
- Every one of the 17 direct writes, 53 hook sites, and enabled countdown's 32
  writes participates in preflight before any versioned operation installs.
- A 4.71-only feature is capability-disabled for an older build; its RVAs are
  never copied into another profile.
- Delete rollback/reset callbacks and reverse writes. Any install failure after
  the barrier calls the terminal abort path.
- Revalidate the entire manifest against `H:\gc\game471.exe.i64`; do not rely
  only on existing C++ arrays or older planning prose.
- Do not add fake memory/hook tests or callback recorders.

---

## Task 1: Revalidate and classify the complete native manifest

**Files:**

- Create: `.codex-tmp/loader-cleanup-framerate-profile.py` (untracked)
- Read: `src/Patches/Framerate/FrameratePatchPlan.*`
- Read: `src/Patches/Framerate/FramerateEffectTiming.*`
- Read: `src/Patches/Framerate/FramerateMenuTiming.*`
- Read: `src/Patches/Framerate/FrameratePatch.cpp`
- Read: `src/Patches/Countdown/CountdownTimerFreeze.*`

- [ ] **Step 1: Audit the 17 direct writes**

For each `CheckedWrite`, output feature-local site name, RVA, exact original
bytes, replacement derivation, decoded instruction/data purpose, and code/data
classification. Require exactly 17 unique nonoverlapping spans.

- [ ] **Step 2: Audit the 53 hook sites**

Audit the 11 pre-effect, 34 effect, 2 post-effect, and 6 menu-timing contracts.
For each record hook kind, protected span, exact expected prefix, callback ABI,
live register/stack operands, and whether an original trampoline is called.
Require exactly 53 unique targets.

- [ ] **Step 3: Audit the 32 countdown calls**

For each call RVA, require a decoded `CALL rel32` to RVA `0x002350C0`, return
RVA `call + 5`, and replacement `D9 EE 90 90 90`. Require exactly 32 unique
sites and document why timer freeze is enabled by the current setting.

- [ ] **Step 4: Cross-check intersections**

Sort all protected spans and prove no direct write/countdown range overlaps a
hook's replaced instruction span. Any overlap must be resolved in the profile
and design before implementation.

---

## Task 2: Separate target-FPS math from game-version facts

**Files:**

- Rename: `src/Patches/Framerate/FramerateProfile.h` to
  `src/Patches/Framerate/FramerateTimingProfile.h`
- Rename: `src/Patches/Framerate/FramerateProfile.cpp` to
  `src/Patches/Framerate/FramerateTimingProfile.cpp`
- Modify: all includes/callers of `FramerateProfile`
- Modify: `src/Patches/CMakeLists.txt`

- [ ] **Step 1: Rename the existing value type**

Use `FramerateTimingProfile` for configured FPS, authored-frame duration,
ratios, repeat timing, and other build-independent calculations. Preserve its
public behavior and range validation.

- [ ] **Step 2: Reserve `GameProfile` for native contracts**

Do not place RVAs or expected instruction bytes in the timing profile. This
terminology prevents a future older game profile from being confused with a
target-FPS calculation.

---

## Task 3: Add `FramerateGameProfile` and dynamic plan construction

**Files:**

- Create: `src/Patches/Framerate/FramerateGameProfile.h`
- Create: `src/Patches/Framerate/FramerateGameProfile.cpp`
- Modify: `src/Patches/Framerate/FrameratePatchPlan.h`
- Modify: `src/Patches/Framerate/FrameratePatchPlan.cpp`
- Modify: `src/Patches/Framerate/FramerateEffectTiming.*`
- Modify: `src/Patches/Framerate/FramerateMenuTiming.*`
- Modify: `src/Patches/CMakeLists.txt`

**Interfaces:**

```cpp
struct FramerateGameProfile final {
    game_version::GameBuild build;
    game_version::GameImageVariant variant;
    std::array<FramerateWriteContract, 17> writes;
    std::array<FramerateHookContract, 53> hooks;
};

[[nodiscard]] std::optional<FramerateGameProfile>
ProfileFor(game_version::GameBuild,
           game_version::GameImageVariant) noexcept;

[[nodiscard]] std::expected<game_version::FeaturePlan, FrameratePlanError>
BuildFrameratePlan(
    const FramerateGameProfile&,
    const FramerateTimingProfile&,
    const FramerateSettings&,
    audio::AudioBackend) noexcept;
```

- [ ] **Step 1: Move static contracts without changing order**

Move all RVA/prefix/original metadata from the current plan/effect/menu files
into the game profile. Preserve the exact current 53-entry concatenation and
17-write order. Add compile-time size assertions for 17 and 53.

- [ ] **Step 2: Keep replacements derived from validated settings**

`BuildFrameratePlan` derives replacement operands and callback runtime values
from `FramerateTimingProfile`; it combines them with the selected native
contracts. Reject arithmetic overflow or an unsupported backend before adding
the feature plan.

- [ ] **Step 3: Make capability absence explicit**

`ProfileFor` returns the 4.71 profile for the three 4.71 image variants and
`nullopt` otherwise. The caller treats enabled framerate without a profile as
fatal unsupported-build, not as native 60 FPS fallback.

---

## Task 4: Remove `FrameratePatchTransaction`

**Files:**

- Delete: `src/Patches/Framerate/FrameratePatchTransaction.h`
- Delete: `src/Patches/Framerate/FrameratePatchTransaction.cpp`
- Modify: `src/Patches/Framerate/FrameratePatch.h`
- Modify: `src/Patches/Framerate/FrameratePatch.cpp`
- Modify: `src/Patches/Framerate/FramerateDiagnostics.*`
- Modify: `src/Patches/CMakeLists.txt`

- [ ] **Step 1: Replace transaction-local types**

Use `runtime_image::BytePattern`, `BytePatchOperation`,
`InlineHookOperation`, and `MidHookOperation`. Delete `CheckedWrite`,
`HookOperation`, `FramerateMemoryApi`, install/reset callbacks, applied counts,
original-byte storage, `Rollback`, and `VerifyOriginalState`.

- [ ] **Step 2: Remove feature-owned SafetyHook objects**

Delete the 53 hook objects from `RuntimeHooks`. Keep typed original trampolines
for the four inline MovieClip/Navigator hooks that call native behavior; the
HookRegistry publishes them before enable. Mid callbacks retain only their
required runtime operand/state pointers.

- [ ] **Step 3: Replace fatal transaction diagnostics**

Keep feature-specific plan-construction and runtime diagnostic records. For
preflight/install failures, format the common site identity and underlying
RuntimeImage/HookError, then call `AbortProcess`. Remove all rollback attempted/
complete fields and wording.

---

## Task 5: Add `CountdownProfile` to the same barrier

**Files:**

- Create: `src/Patches/Countdown/CountdownProfile.h`
- Create: `src/Patches/Countdown/CountdownProfile.cpp`
- Modify: `src/Patches/Countdown/CountdownTimerFreeze.h`
- Modify: `src/Patches/Countdown/CountdownTimerFreeze.cpp`
- Modify: `src/Patches/CMakeLists.txt`

- [ ] **Step 1: Move 4.71 RVAs into the profile**

The profile owns the global delta target and all 32 call/return RVAs. Build
the exact original `CALL rel32` patterns using checked 32-bit displacement
math during plan construction.

- [ ] **Step 2: Contribute all or nothing**

When `timer_freeze_enabled` is false, contribute no Countdown feature. When
true, contribute exactly 32 byte operations ordered after the framerate
feature. Remove any remaining direct initializer, mutable enable flag, reverse
patch, and best-effort count logging.

---

## Task 6: Compose callback state and feature order

**Files:**

- Modify: `src/Loader/GameVersionedStartupPlan.cpp`
- Modify: `src/Loader/VersionedStartupExecutor.cpp`
- Modify: `src/Loader/DllMain.cpp`

- [ ] **Step 1: Prepare runtime state before installation**

Create the framerate runtime, authored/gameplay clocks, monitor, and callback
operand state before adding hook operations. Publish immutable pointers before
the executor reaches the first hook.

- [ ] **Step 2: Declare installation dependencies**

Framerate installs after audio state needed by its selected backend and after
Absolute Judgement's required input/timeline preparation. Countdown installs
after every framerate direct write/hook. Preserve the current 17 writes -> 53
hooks -> 32 countdown writes ordering.

- [ ] **Step 3: Remove eager startup calls**

Delete `FrameratePatchInit`, `SetCountdownTimerFreezeEnabled`, and
`CountdownTimerFreezeInit` from the migration startup path. No site may install
outside the approved plan.

---

## Task 7: Verify and commit

- [ ] **Step 1: Audit counts and duplicate mechanics**

```powershell
rg -n 'FrameratePatchTransaction|FramerateMemoryApi|CheckedWrite|HookOperation|Rollback|SetCountdownTimerFreezeEnabled|CountdownTimerFreezeInit|create_inline|create_mid|safetyhook::InlineHook|safetyhook::MidHook' src\Patches\Framerate src\Patches\Countdown
rg -n 'std::array<FramerateWriteContract, 17>|std::array<FramerateHookContract, 53>' src\Patches\Framerate\FramerateGameProfile.*
```

Expected: no transaction/rollback/local hook owner; the profile count
assertions remain exact.

- [ ] **Step 2: Run complete static verification**

```powershell
cmake --build --preset msvc32-debug
ctest --preset msvc32-debug -j 4
cmake --build --preset msvc32-release
ctest --preset msvc32-release -j 4
git diff --check
git status --short --branch
```

Do not claim high-FPS timing, countdown behavior, menu/effect pacing, input
feel, or gameplay acceptance.

- [ ] **Step 3: Commit**

```powershell
git add -- src\Patches\Framerate src\Patches\Countdown src\Patches\CMakeLists.txt src\Loader\GameVersionedStartupPlan.cpp src\Loader\VersionedStartupExecutor.cpp src\Loader\DllMain.cpp
git commit -m "Migrate framerate and countdown to version profiles"
```

# Widescreen Gameplay Feedback Placement Implementation Plan

> **Execution:** Implement inline in the current workspace. Do not delegate to
> subagents, deploy runtime artifacts, or stop/restart any process.

**Goal:** Preserve the accepted full-width 3D stage while rendering gameplay
HUD content at native 720 x 1280 scale, place each complete combo counter on
its player side, and move only the local player's judgement word with the
primary combo cluster.

**Architecture:** Add a third game-visible render space, `gameplay_hud`, which
uses the wide scene render target with native logical dimensions and a centered
720 x 1280 viewport. The existing gameplay-effects boundary enters this space.
Two guarded combo hooks temporarily select left/right native viewports around
the CHAIN label, digits, and celebration layers, flushing native draw work at
each boundary. A checked vtable-slot hook targets
`MovieClipInstance::Accept`, then narrows execution to the exact `imc_ico_n`
and `imc_ico_l` instances, draw visitor, and active gameplay frame. Each exact
subtree forcibly reapplies the centered native-width hardware viewport, drains
its native batches, and restores the preceding placement. Calls from boot and
ordinary menus pass directly to the original method. The hook never advances
or drains the animation manager.
The existing gameplay-track boundary captures the active CTune before the
player-visual draw. The generic effect-tree bracket recognizes only Player 1's
five judgement roots while the stage is still in physical 3D, temporarily
borrowing a native right-side HUD viewport with exact D3D state
capture/restore. The tutorial uses its later concrete group-6 draw bracket
inside the gameplay-HUD pass. Menus and other 2D tasks continue to use the
existing native texture compositor.

**Tech stack:** C++23, Win32 x86, Direct3D 9, SafetyHook, CMake/Ninja.

**Prior design:**
`docs/superpowers/specs/2026-08-26-windowed-widescreen-stage-design.md` and
`docs/superpowers/plans/2026-09-01-windowed-widescreen-stage.md`.

## Constraints

- Work only in `H:\gc\artifacts\GCLoader`. Treat `H:\gc` as analysis/runtime
  evidence; do not deploy from this plan.
- Keep the output contract fixed at height 1280 and width >= 720. Add no new
  configuration or GUI option.
- Keep menus, selection screens, common 2D tasks, and their centered native
  canvas behavior unchanged.
- Keep the stage background, perspective track, chart objects, hit effects,
  player markers, and opponent judgements unchanged. Keep each combo counter's
  normal and celebration layers together.
- Move the complete per-entry combo presentation only. Entry 0 uses the right
  720-pixel viewport, entry 1 uses the left viewport, and any unexpected entry
  falls back to the centered viewport.
- Move only Player 1's `MISS`/`GOOD`/`COOL`/`GREAT` roots from CTune
  slots 93 through 97 to the right-side native viewport; do not derive
  placement from another participant or a runtime participant count.
- At 720 x 1280, all viewport and judgement deltas collapse to the native
  result.
- Keep render callbacks allocation-free and free of success-path logging.
- Runtime byte preflight remains transactional and fail-closed. Do not add
  signature scanning or compatibility guesses.
- IDA/assembly evidence gathered during design is implementation evidence.
  Post-build verification is limited to compilation, linkage, diff hygiene,
  and artifact inspection; it must not launch IDA or run the IDA-backed
  contract audit as a proof step.
- Static verification cannot establish final visual placement. The operator's
  later in-game check remains the acceptance boundary.
- Do not add automated tests that model or assert game, hook, renderer, HUD,
  combo, judgement, or visual behavior. Pure models, fake devices, synthetic
  executable memory, and callback wrappers are not the runtime oracle.

## Frozen native contracts

Preferred image base is `0x00400000`; all implementation addresses are RVAs.

| Site | RVA | Guard | Meaning |
| --- | ---: | --- | --- |
| Gameplay track / CTune capture | `0x00262FA8` | `E8 D3 56 FE FF 8B 4D C4` | Capture the active CTune before the player-visual function renders Player 1 judgement in physical 3D. |
| Gameplay HUD entry | `0x00263041` | existing `E8 FA 5C FE FF E8 D5 00 DF FF` | Enter the new centered gameplay-HUD space before `0x00648D40`. |
| Combo begin | `0x001E4503` | `E8 A8 D0 FF FF` | Before the ordinary static CHAIN-label draw. Loop entry is `[ebp-0x14]`. |
| Combo normal digits | `0x001E4550` | `E8 0B 7B FE FF` | Read-only byte witness inside the guarded ordinary-counter window. |
| Combo end | `0x001E4B58` | `8B 55 E4 8B 45 E0 89 02 E9 D9 F8 FF FF` | Restore the centered HUD viewport at the shared join after every layer using the entry's combo value. |
| Gameplay feedback draw begin | `0x001F11E8` | `E8 83 0D 00 00` | Before `sub_5F1F70`, compare the current effect root in `ecx` with Player 1 judgement slots 93 through 97, capture physical-3D D3D state, and apply the right native HUD viewport on a match. |
| Gameplay feedback draw end | `0x001F11ED` | `8B 4D F8 8B 51 0C 81 E2 00 40` | Flush the complete matched judgement tree and restore the exact captured physical-3D state. |
| Note tutorial group begin | `0x0024A2D5` | `E8 A6 6E FA FF` | Select Player 1's right gameplay-HUD viewport around the concrete tutorial group-6 draw. |
| Note tutorial group end | `0x0024A2DA` | `0F B6 55 08 85 D2 74 1B` | Restore the centered gameplay-HUD viewport immediately after that group draw. |
| Network-status `MovieClipInstance::Accept` slot | `0x002BE0E0` | `D0 0C 4E 00` / pointer target RVA `0x000E0CD0` | Patch one concrete vtable slot; the detour requires draw visitor RVA `0x002BB74C`, an active gameplay frame, and exact `imc_ico_n` or `imc_ico_l` identity before forcibly applying the centered native-width viewport. |

The judgement producer at `0x006463F0` is analysis evidence, not a mutation
site. It accepts rendered-owner slot `nn < 4`; that bound describes this
effect array and is not a claim about the maximum number of online or LAN
participants. For judgement grades 0 through 4 it resolves CTune slots:

```text
slot = 93 + 5 * nn + grade
```

The implementation deliberately supports Player 1 only and therefore matches
the five roots at slots 93 through 97. The remaining native owner lanes are not
interpreted as the game's multiplayer capacity. No effect position is
rewritten.

## Task 1: Add pure gameplay-feedback geometry

**Files:**

- Create: `src/Patches/WindowedWidescreen/GameplayFeedbackPlacement.h`
- Create: `src/Patches/WindowedWidescreen/GameplayFeedbackPlacement.cpp`
- Modify: `src/Patches/CMakeLists.txt`

- [x] Implement small viewport geometry types and `noexcept` pure functions. Reuse
  `OutputSize`, `kNativeWidth`, and `kNativeHeight` from `ResolutionModel`.
- [x] Center a 720 x 1280 viewport with integer margins, map combo entries 0/1
  to right/left, and make all other entries fall back to center.
- [x] Add the production source to `gc_runtime_patches`.

**Focused verify:**

```powershell
cmake --build --preset msvc32-debug --target gc_runtime_patches
```

## Task 2: Add the native-scale gameplay-HUD render space

**Files:**

- Modify: `src/Patches/WindowedWidescreen/RenderSpacePolicy.h`
- Modify: `src/Patches/WindowedWidescreen/RenderSpacePolicy.cpp`
- Modify: `src/Patches/WindowedWidescreen/PassClassifier.cpp`
- Modify: `src/Patches/WindowedWidescreen/NativeCanvasCompositor.h`
- Modify: `src/Patches/WindowedWidescreen/NativeCanvasCompositor.cpp`
- Modify: `src/Patches/WindowedWidescreen/D3D9CompositorDevice.h`
- Modify: `src/Patches/WindowedWidescreen/D3D9CompositorDevice.cpp`

- [x] Add `RenderSpace::gameplay_hud` with logical dimensions 720 x 1280 on
  the wide render target.
- [x] Implement gameplay-HUD placement changes so center/side transitions
  flush and verify pending batches before changing viewport; identical
  effective viewports are no-ops and failures remain structured.
- [x] Route `GameplayPass::orthographic_effects` to `gameplay_hud` while common
  2D tasks continue to classify as `native_2d`.
- [x] Refactor compositor transition choice by render target: `native_2d`
  binds the native texture; `physical_3d` and `gameplay_hud` bind the wide
  scene. Perform center copies only when crossing that target boundary.
- [x] Implement centered/left/right viewport and scissor rectangles on the
  wide scene. `gameplay_hud` uses native logical dimensions and has depth,
  depth-write, and stencil disabled just like native 2D.
- [x] Extend recovery and device-loss reset so the stable target and centered
  gameplay-HUD placement are deterministic.

**Focused verify:**

```powershell
cmake --build --preset msvc32-debug --target gc_runtime_patches
```

## Task 3: Install guarded combo, gameplay-feedback, and network-status hooks

**Files:**

- Modify: `src/Patches/WindowedWidescreen/WindowedWidescreenAbi.h`
- Modify: `src/Patches/WindowedWidescreen/WindowedWidescreenAbi.cpp`
- Modify: `src/Patches/WindowedWidescreen/WindowedWidescreenPatch.h`
- Modify: `src/Patches/WindowedWidescreen/WindowedWidescreenPatch.cpp`

- [x] Extend `WidescreenContractSite`, the contract arrays, ABI metadata, and
  stable site names. The final transaction contains 35 executable requests
  after the rejected judgement-coordinate hook is removed.
- [x] Change the existing gameplay-effects seam to request
  `RenderSpace::gameplay_hud`.
- [x] At combo begin, safely read `[ebp-0x14]`, map the entry to a side, and
  request that viewport. At combo end, request center. Any read/compositor
  failure publishes the existing fatal renderer error rather than continuing
  with mixed batch geometry.
- [x] Capture CTune before the player-visual draw. At the generic effect-tree
  bracket, match only Player 1 slots 93 through 97, temporarily render that
  judgement tree through the right native viewport, and restore exact
  physical-3D state afterward. Use the concrete tutorial group-6 call bracket
  to select right/center inside `GameplayHud`. Do not mutate effect
  coordinates or reference another owner slot.
- [x] Replace the ineffective panel/root wrappers with one transactionally
  guarded `MovieClipInstance::Accept` vtable-slot hook. Require the exact draw
  visitor, frame-local gameplay latch, and exact `imc_ico_n` or `imc_ico_l`
  identity. During each exact subtree, forcibly apply the centered native-width
  hardware viewport even when the cached placement already matches; expose
  logical `(0,0,720,1280)` queries, drain the subtree, restore the preceding
  placement, and fail open when the optional scope is unavailable. Do not
  finalize, wait on, execute, or reset the animation manager.
- [x] Keep the existing framerate hook at RVA `0x00246517` untouched; verify
  the new RVA is unique in both hook manifests.

**Focused verify:**

```powershell
cmake --build --preset msvc32-debug --target gc_runtime_patches
```

## Task 4: Record the implemented ownership boundary

**Files:**

- Modify: `docs/superpowers/specs/2026-08-26-windowed-widescreen-stage-design.md`
- Modify: `docs/reverse-engineering/ctune-effect-producer-manifest.md`

- [x] Amend the design with the distinction among centered native-texture 2D,
  centered gameplay HUD on the wide target, and temporary per-counter side
  viewports.
- [x] Record the full per-entry combo hook window so the milestone layer stays
  with the normal counter.
- [x] Record Player 1 slots 93 through 97, the four-lane native producer bound,
  and the distinction between those internal lanes and multiplayer participant
  capacity.
- [x] State that compilation and linkage prove only that the sources build.
  For this change, no automated game-behavior tests are used; only a later live
  game run can accept placement, hook integration, or renderer behavior.

## Task 5: Verify the implementation without deployment or IDA

- [x] Build Debug and Release to establish compilation and linkage only.
- [x] Run `git diff --check`.
- [x] Inspect `git diff --stat`, `git diff`, and `git status --short` for scope,
  stale placeholders, and accidental runtime/deployment changes.
- [x] Do not launch IDA, run the IDA-backed widescreen audit, start the game,
  deploy the DLL/GUI/config, or claim visual acceptance.

**Final verify:**

```powershell
cmake --build --preset msvc32-debug --target iDmacDrv32
cmake --build --preset msvc32-release --target iDmacDrv32
git diff --check
git status --short
```

## Plan self-review

- Every user-visible decision is represented: fixed-height windowed output,
  full-width stage, native-scale HUD, side combo, Player 1 judgement, no
  option, and no deployment.
- Every native mutation has an exact guarded RVA and participates in the
  existing all-or-nothing transaction.
- No synthetic test is used as a proxy for game behavior. Compilation proves
  only that the code builds; the deployed game is the placement oracle.
- No task requires an agent, worktree, process lifecycle action, IDA post-build
  proof, or runtime-root write.

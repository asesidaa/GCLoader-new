# Widescreen Tutorial and Test Mode Containment Implementation Plan

> **Execution:** Implement inline in the current workspace. Do not delegate to
> subagents, create a worktree, deploy runtime artifacts, or stop/restart any
> process.

**Goal:** Move the note tutorial effect family beside the local player's
gameplay feedback and render Test Mode as an unstretched, centered 720 x 1280
canvas inside the configured wide window.

**Architecture:** Extend the existing guarded widescreen transaction with
narrow draw boundaries. Capture CTune before the player-visual call, identify
Player 1's five judgement roots at the generic effect-tree draw, and borrow the
right native HUD viewport with exact physical-3D state capture/restore. Move
the note tutorial at its concrete group-6 draw boundary inside the later
gameplay-HUD pass. Test
Mode bypasses the game's normal render-frame wrapper with direct
`IDirect3DDevice9::BeginScene`/`EndScene` calls, so the guarded root-form
boundaries open a standalone compositor frame, render in `native_2d`, and
finish that frame before the direct `EndScene`; the physical window,
backbuffer, and configured width do not change.

**Tech stack:** C++23, Win32 x86, Direct3D 9, SafetyHook, CMake/Ninja.

**Spec:**
`docs/superpowers/specs/2026-08-26-windowed-widescreen-stage-design.md`

## Global constraints

- Work only in `H:\\gc\\artifacts\\GCLoader`. Do not deploy from this plan.
- Preserve the fixed-height output contract: height is exactly 1280 and width
  is at least 720.
- Test Mode remains entirely 2D: draw it at native 720 x 1280 scale, centered,
  with unused horizontal space left blank. Do not resize the window when
  entering or leaving Test Mode.
- Relocate the complete note-tutorial group-6 draw, which owns the note-type
  effects resolved from slots `0xB2`, `0xB3`, `0xB4`, `0xB5`, `0xB6`,
  `0xB9`, `0xBA`, `0xBB`, and `0xC0`.
- Tutorial belongs to Player 1 and always maps to the right gameplay-HUD
  viewport. Do not infer a second local player from network participant data.
- Match Player 1's judgement roots through native CTune slots 93 through 97.
  The native producer's four-lane bound is not a multiplayer participant
  limit, and no participant-derived selector is used.
- Leave the accepted wide stage, chart, ordinary HUD, combo, menu, and
  common-2D behavior unchanged.
- Every new executable site requires a named RVA, exact original-byte guard,
  ABI metadata, and participation in the existing all-or-nothing transaction.
- Keep render callbacks allocation-free and free of success-path logging.
- Do not add automated tests that model or assert game, native-hook, renderer,
  tutorial, Test Mode, or visual behavior. The target game is the runtime
  acceptance oracle.
- Post-build verification is compilation/linkage, IDE diagnostics, diff
  hygiene, artifact presence, and exact worktree accounting only. Do not invoke
  IDA after the build as proof of runtime behavior.

## Frozen native contracts

Preferred image base is `0x00400000`; all implementation addresses below are
RVAs.

| Site | RVA | Guard | Meaning |
| --- | ---: | --- | --- |
| Gameplay track / CTune capture | `0x00262FA8` | `E8 D3 56 FE FF 8B 4D C4` | Capture the active CTune before the player-visual call renders judgement in physical 3D. |
| Gameplay effects begin | `0x00263041` | existing `E8 FA 5C FE FF E8 D5 00 DF FF` | Validate the same active CTune and enter `gameplay_hud` before the note-tutorial renderer. |
| Gameplay effects end | `0x00263046` | `E8 D5 00 DF FF` | Clear the CTune/tutorial scope immediately after the gameplay-effects call. |
| Gameplay feedback draw begin | `0x001F11E8` | `E8 83 0D 00 00` | Match Player 1 judgement slots 93 through 97, capture physical-3D D3D state, and apply the right native HUD viewport. |
| Gameplay feedback draw end | `0x001F11ED` | `8B 4D F8 8B 51 0C 81 E2 00 40` | Flush the complete matched judgement tree and restore exact physical-3D state. |
| Note tutorial group begin | `0x0024A2D5` | `E8 A6 6E FA FF` | Select Player 1's right gameplay-HUD viewport for the concrete group-6 draw. |
| Note tutorial group end | `0x0024A2DA` | `0F B6 55 08 85 D2 74 1B` | Restore the centered gameplay-HUD viewport after the group draw. |
| Test Mode begin | `0x0023AA89` | `E8 D2 BB F3 FF E8 8D 86 E1 FF` | After the direct D3D `BeginScene`, open a standalone compositor frame and enter `native_2d` before `sub_576660` renders the Test Mode root window. |
| Test Mode end | `0x0023AA8E` | `E8 8D 86 E1 FF 89 85 80 FE FF FF 8B 8D` | Finish and composite the standalone frame immediately after the root-form call and before the direct D3D `EndScene`. |

The supported game collection ABI is not a zero-offset `std::vector`:
`sub_4128A0` derives count from pointers at `collection+0x0C` and
`collection+0x10`, while `sub_43D0C0` indexes the pointer array beginning at
`collection+0x0C`. The CTune effect collection begins at `CTune+0x1D6C`.

## Task 1: Extend guarded ABI ownership

**Files:**

- Modify: `src/Patches/WindowedWidescreen/WindowedWidescreenAbi.h`
- Modify: `src/Patches/WindowedWidescreen/WindowedWidescreenAbi.cpp`
- Modify: `src/Patches/WindowedWidescreen/WindowedWidescreenPatchTransaction.h`

- [x] Add contract-site identifiers, byte contracts, `mid_context` ABI records,
  and stable names for gameplay-effects end, gameplay-feedback draw begin/end,
  note-tutorial group begin/end, and Test Mode begin/end.
- [x] Raise the fixed hook capacity to 34 and keep the final request set exactly
  within that bound.
- [x] Keep all guards exact and fail closed; do not add scanning or fallback
  addresses.

## Task 2: Scope Player 1 gameplay-feedback placement

**Files:**

- Modify: `src/Patches/WindowedWidescreen/WindowedWidescreenPatch.cpp`

- [x] Capture the active gameplay CTune before the player-visual call and track
  whether the current generic manager draw selected Player 1 judgement.
- [x] Safely validate the native 24-byte pointer collection, including begin,
  end, element count, pointer arithmetic, and slots 93 through 97.
- [x] At the generic feedback boundary, ignore calls outside the active CTune
  scope. Match only Player 1 judgement roots, capture the physical-3D state,
  render through the right native viewport, and restore that exact state.
- [x] At the concrete group-6 call boundary, select Player 1's right
  gameplay-HUD viewport for tutorial and restore center immediately afterward.
- [x] Any unreadable native state, collection invariant failure, or compositor
  failure uses the existing fatal renderer boundary.

## Task 3: Contain Test Mode in native 2D

**Files:**

- Modify: `src/Patches/WindowedWidescreen/WindowedWidescreenPatch.cpp`

- [x] After Test Mode's direct D3D `BeginScene`, open a standalone compositor
  frame and request `native_2d`; do not assume the normal game-frame detour ran.
- [x] After the root render returns, finish and composite that standalone frame
  before Test Mode's direct D3D `EndScene`. A missing or duplicate local scope
  is a no-op rather than a process-fatal assertion.
- [x] Register the final 34 mid/detour hooks in the existing transaction.
  Remove the rejected judgement-coordinate hook, and do not alter
  window size, backbuffer size, display mode, or task-classifier defaults.

## Task 4: Record the implemented boundaries

**Files:**

- Modify: `docs/superpowers/specs/2026-08-26-windowed-widescreen-stage-design.md`
- Modify: `docs/reverse-engineering/ctune-effect-producer-manifest.md`

- [x] Document the tutorial group-6 bracket, Player 1 judgement slot set,
  pointer-collection ABI, physical overlay bracket, right-side placement, and
  exclusion of unrelated effects.
- [x] Document the Test Mode root-window call bracket and the centered native
  720 x 1280 result inside the unchanged wide output.
- [x] State explicitly that guarded installation and successful builds are
  static evidence only; user-observed gameplay and Test Mode remain the
  acceptance gates.

## Task 5: Verify without deployment or behavioral tests

- [x] Inspect CLion diagnostics for every modified C++ file.
- [x] Build `iDmacDrv32` with `msvc32-debug` and `msvc32-release` to establish
  compilation and linkage only.
- [x] Run `git diff --check`, inspect the complete diff/stat, and account for
  every changed path while preserving the pre-existing dirty worktree.
- [x] Do not run CTest as behavioral proof, invoke IDA after the build, launch
  the game, deploy artifacts, or claim visual/runtime acceptance.

## Plan self-review

- Player 1 feedback and Test Mode each have a narrow native owner and immediate
  state restoration.
- Player 1 judgement matching derives from live CTune slots 93 through 97, and
  tutorial placement uses its concrete group-6 call rather than effect names,
  copied assets, another participant's coordinates, or a broad renderer
  heuristic.
- The Test Mode bracket owns one complete standalone compositor frame around
  the direct root-form traversal; it cannot silently change the configured
  window or backbuffer dimensions.
- Every new mutation has an exact guarded RVA and the 34-request transaction
  fits the raised fixed capacity.
- No static test is presented as an oracle for game execution or visuals.

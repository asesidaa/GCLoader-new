# Narrowed widescreen HUD implementation plan

**Goal:** Apply native-width rendering only to explicitly selected UI draws on the current 4.74 target; preserve full-output stage drawing.

**Architecture:** Keep the cached CTune projection and mixed effects pass at output dimensions. Extend the existing selected-draw scope to save, apply, and restore projection together with viewport/scissor/depth state. Add separate scopes for authored stage-start title/player content and timed text. The rectangle requires no hook.

**Tech stack:** C++23, x86 SafetyHook, D3D9, versioned byte contracts, IDA-CLI, CLion MCP.

**Spec:** [Stage design](../specs/2026-08-26-windowed-widescreen-stage-design.md); [native evidence](../../reverse-engineering/widescreen-background-projection-followup-2026-09-06.md).

## Constraints

- Implement inline in the existing checkout. Preserve concurrent work.
- Current target only; retain the 2.06 contract tables, ABI/install order, and rendering behavior until current-target runtime acceptance.
- CLion MCP owns edits/IDE diagnostics; normal shell owns Git, IDA, and compilation.
- Native-patch policy: direct IDA and Debug/Release builds; no synthetic tests or fixtures.
- No deployment or process lifecycle changes are included. After the runtime
  follow-up, the operator authorized committing the confirmed 4.74 change.

## 1. Establish consumers

- [x] Trace CTune+110 construction and background/visualizer reads.
- [x] Trace all mixed-pass submissions in 648D40, including the final full-output fade.
- [x] Confirm selected effect packets retain world transforms but use the projection present at submission.
- [x] Confirm 64A295/64A2AB title/player draws and 64A2F8 timed-text draw use authored 720-wide coordinates. Their helpers explicitly calculate animation strips against 720.
- [x] Confirm finish announcement roots 0..6 query target width/2 and require no new placement hook.

## 2. Implement selected draw state

Files: GameplayHudHooks.*, WidescreenRuntime.h, NativeCanvasCompositor.*, D3D9CompositorDevice.*, RenderHooks.cpp under src/Patches/WindowedWidescreen.

- [x] Extend `BeginGameplayHudDraw(GameplayHudPlacement, bool native_projection = false)` and add a device action `set_native_hud_projection(void*)`. On selection, flush pending general batches preserving the pipeline; capture actual viewport/scissor/depth, save projection, scale its x/y orthographic coefficients to native dimensions, and select placement. On exit, flush selected work and restore only changed state.
- [x] Return native logical dimensions only while the selected native-projection scope is active. Retain the old default when the profile does not enable this policy.
- [x] Keep the current-target mixed effects pass physical. Keep 2.06's existing gameplay_hud pass and builder hook.
- [x] Add paired `StageTitleDraw*Mid`, `StagePlayersDraw*Mid`, and `TimedTextDraw*Mid` callbacks with distinct scope owners and centered placement.
- [x] Remove the abandoned pass-wide projection-copy hook and rectangle exception/storage/actions.

## 3. Bind and verify

Files: WindowedWidescreenAbi.*, WindowedWidescreenProfile.*, stage spec and native evidence record.

- [x] Add a profile-owned `selected_hud_draws_only` policy and bind it to the runtime ABI; enable only for the current target.
- [x] Remove the current projection site. Add six guarded mid-hook contracts at RVAs 24A295/24A29A, 24A2AB/24A2B0, 24A2F8/24A2FD. Keep the 2.06 tables unchanged.
- [x] Check new spans directly in IDA for complete instructions, interior entry points, and cross-feature overlap. The timed-text end also receives a native bypass branch and must accept an inactive scope.
- [x] Complete Debug and Release preset builds, CLion error inspections, and diff review.
- [x] Update documentation with final counts, native ownership, and build evidence.

## 4. Gameplay ONLINE follow-up

- [x] Confirm the operator's deployed DLL matches the narrowed build; record that effects look correct and ONLINE is a white block only during gameplay.
- [x] Trace Flash material submission in IDA: `4E3CA0` caches texture bindings and `4E44E0(0)` does not force a rebind. Identify the full-state restore across child clips as incompatible with these caches.
- [x] In `NetworkStatusHooks.cpp`, use the existing selected viewport scope without projection replacement for the current profile. Retain exact clip selection, local matrix compensation, and 2.06 behavior.
- [x] Build both complete presets, inspect the changed source with CLion, and retain only existing capped correction messages to identify the new path.

## Operator acceptance

- [x] The operator reported on 2026-09-06 that the 4.74 issues appeared fixed after the ONLINE follow-up and authorized commit followed by the 2.06 port. This accepts the reported regressions; the broader song/configuration checklist remains a regression checklist, not a claim that every case was run.

Implementation verification: both complete Debug/Release preset builds passed; CLion inspections reported no errors; all six new native spans match IDA and have no interior branch targets. The 2.06 byte/pointer tables, layout, base ABI and install order are unchanged. No runtime trace was needed for the established draw ownership. See the evidence record for the built DLL identity and outstanding runtime observations.

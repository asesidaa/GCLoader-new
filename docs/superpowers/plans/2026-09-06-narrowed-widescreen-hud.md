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

Implementation verification for the 4.74 commit: both complete Debug/Release preset builds passed; CLion inspections reported no errors; all six new native spans match IDA and have no interior branch targets. The 2.06 byte/pointer tables, layout, base ABI and install order were unchanged at that commit. No runtime trace was needed for the established draw ownership.

## 5. Authorized 2.06 port after commit 634ce44

- [x] Trace the old projection, background/visualizer/rectangle consumers, selected packet submission, title/player/timed-text helpers and Flash binding cache directly in IDA.
- [x] Correct the old bar-name mapping: opening title at 616A64 is centered; top-bar details at 616A90 follow the configured placement.
- [x] Add six centered-draw contracts, move the bar pair, remove the old projection hook and enable the existing narrowed policy. Preserve 4.74 contracts and behavior.
- [x] Verify all 100 2.06 widescreen contracts against IDA/executable bytes, instruction boundaries and cross-feature overlap. Build both complete presets and inspect through CLion.
- [x] Operator reported the 2.06 fix works after the separator follow-up and authorized cleanup/commit. Deployment and process actions were not performed by the agent; the broader regression checklist is not a claim of exhaustive coverage.

See the [2.06 native and build record](../../reverse-engineering/gc206-narrowed-widescreen-2026-09-06.md) for exact sites, DLL identity and the runtime checklist.

## 6. 2.06 dotted header separator

- [x] Identify the separator in both native language assets: `imc_head -> UNIQUE_150 -> UNIQUE_149 -> UNIQUE_148`, a 720 x 2 line at stable y=80.
- [x] Verify MovieClip definition/name fields and the direct-parent assignment in IDA. Add three read-only guards; reuse the existing clip hook with exact definition plus parent selection, enabled only by the 2.06 profile.
- [x] Apply configured top-bar placement with the existing Flash viewport scope and capped correction log. Keep native projection/material lifetime.
- [x] Verify all 103 native contracts, build both complete presets and inspect the source through CLion. Hook count remains 88.
- [x] Operator confirmed the separator fix works in the diagnostic run and authorized cleanup/commit. Other placement configurations and songs remain regression-checklist items rather than inferred acceptance.

### Separator runtime follow-up

- [x] Inspect the 21:13 operator log: exact separator selection and wrapper success are present, but the operator reports the visual defect remains.
- [x] Recheck terminal-shape/native target-binding paths in IDA. Add bounded, separator-only hardware viewport and composed-matrix snapshots without changing placement logic.
- [x] Build both complete presets and inspect the three diagnostic source files with CLion. DLL/PDB output must remain in the standard preset directories; the operator rejected temporary artifact copies.
- [x] Inspect the 22:33 diagnostic run: all seven samples show the selected separator's native shape draw resetting viewport/scissor from 720 to 2276 wide while retaining the authored matrix.
- [x] Extend the existing terminal-shape matrix correction only to the exact 2.06 separator, restore the matrix after native submission, and retain bounded corrected/restored snapshots for confirmation.
- [x] Build both complete presets and inspect the matrix follow-up with CLion. Diff validation passed; Release DLL is in `build-msvc32-release/dist`. Removed the two earlier temporary DLL/PDB copies.
- [x] Operator confirmation using the standard build output; the deployed DLL matches the matrix-follow-up artifact and the 22:46 log records corrected/restored coordinates.
- [x] Remove temporary separator traces and hardware snapshots; remove successful per-clip correction messages, keeping startup status and capped failure warnings.
- [x] Verify both complete presets and CLion diagnostics after cleanup. The authorized commit is limited to widescreen source and its evidence/design/plan documents; unrelated working-tree changes remain separate.

## 7. LAN compression after commit f10ea02

- [x] Match the deployed cleanup DLL and inspect the latest 2.06 log.
- [x] Recheck native target binding in IDA: `4D9620` skips a matching cached target, so a viewport reset is conditional rather than guaranteed per icon.
- [x] Pair full-output viewport/scissor with the existing selected-shape matrix compensation, inside the enclosing Flash clip scope. Preserve exact selection and native material state; add no tracing or native patch sites.
- [x] Build both complete presets and inspect the changed source through CLion. Release output remains in the standard preset directory.
- [x] Operator confirmed the reported LAN regression is fixed and authorized the commit. Retain ONLINE/separator and other placement configurations in the broader regression checklist without implying exhaustive coverage.

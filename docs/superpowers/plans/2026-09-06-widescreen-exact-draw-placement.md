# Finer widescreen gameplay placement implementation plan

Approved and implemented on 2026-09-06 in the existing checkout using CLion MCP.
The operator confirmed GREAT placement and disappearance of the right-side flash,
then authorized diagnostic cleanup and closeout. No deployment or process
lifecycle changes were performed during cleanup.
Native runtime-patch policy applies: IDA contracts and Debug/Release compilation,
without synthetic tests or fixtures.

## Intended behavior

The orthographic effects pass uses a centered native viewport. Only selected
top-bar draws use the configured left/center/right placement. Each entry's
CHAIN label, ordinary digits, numeric glow, and rounded-hundred number uses its
counter side; rectangular lines retain the centered enclosing viewport.
Primary fixed-position judgment text slots 12,15,18,24,27,30 decimal and tutorial slots
B2,B3,B4,B5,B6,B9,BA,BB,C0 hex carry selected placement into deferred submission.
Other roots, including track-position grade effects 93–97 and result slots 2–6,
receive no placement override.

The first fine-placement run exposed the missing text family; the six text
slots were added in the [judgment text follow-up](../../reverse-engineering/widescreen-judgment-text-followup-2026-09-06.md).
GREAT text is now operator-confirmed. The
[right-side flash follow-up](../../reverse-engineering/widescreen-right-flash-followup-2026-09-06.md)
removed the unnecessary track-effect selection; the operator confirms the flash
is gone.

## Native evidence

The operator run is documented in widescreen-placement-diagnostic-run-2026-09-06.md.
Fresh IDA-CLI requests and byte/branch evidence are under
.codex-tmp/widescreen-fix-20260906, with JSON outputs in
.codex-tmp/widescreen-diagnostics-20260906. Input SHA-256:
FEAD3BD4D0E0985F101965EDC417DD2B96522F8716FF789D84618FEB0D7A2522.

5E4C60 constructs the selected panel resources at authored y=80..210:
+172 stage panel, +216 stage/difficulty panel, +260 score caption,
+480 gauge backing, +524/+568 alternate-mode panels, +1008 extra-count caption,
+612/+656..788/+832..964 mode/player/status panels. Counter resources +304..
are selected separately. 5E1FA0 draws the compact names at y=38..57.
The 5E3EC0 function as a whole is not selected.

Every call and restoration boundary below was decoded in IDA. Some cdecl
restoration sites follow the three-byte stack cleanup to avoid covering a
shared branch target inside the detour span. Shared restoration entries do
nothing when no matching selected draw is active.

| Site | RVA | Protected bytes |
| --- | --- | --- |
| bar_difficulty_a_begin | 1E3F48 | E8 33 46 F8 FF |
| bar_difficulty_a_end | 1E3F4D | 83 C4 18 EB 36 |
| bar_difficulty_b_begin | 1E3F80 | E8 FB 45 F8 FF |
| bar_difficulty_b_end | 1E3F88 | E8 93 F1 E6 FF |
| bar_panel_480_begin | 1E3FB0 | E8 FB D5 FF FF |
| bar_panel_480_end | 1E3FB5 | E8 16 D2 E1 FF |
| bar_panel_524_begin | 1E3FD7 | E8 D4 D5 FF FF |
| bar_panel_524_end | 1E3FDC | E8 3F F1 E6 FF |
| bar_panel_568_begin | 1E4026 | E8 85 D5 FF FF |
| bar_panel_568_end | 1E402B | E8 A0 D1 E1 FF |
| bar_stage_panel_begin | 1E4063 | E8 48 D5 FF FF |
| bar_stage_panel_end | 1E4068 | 51 D9 E8 D9 1C 24 |
| bar_stage_current_begin | 1E40D0 | E8 8B 7F FE FF |
| bar_stage_current_end | 1E40D5 | 83 C4 20 51 D9 E8 |
| bar_stage_total_begin | 1E412D | E8 2E 7F FE FF |
| bar_stage_total_end | 1E4135 | E8 96 D0 E1 FF |
| bar_gauge_begin | 1E41AB | E8 90 8C FE FF |
| bar_gauge_end | 1E41B3 | E8 18 D0 E1 FF |
| bar_panel_216_begin | 1E41E3 | E8 C8 D3 FF FF |
| bar_panel_216_end | 1E41E8 | E8 E3 CF E1 FF |
| bar_score_panel_begin | 1E420A | E8 A1 D3 FF FF |
| bar_score_panel_end | 1E420F | 51 D9 E8 D9 1C 24 |
| bar_score_digits_begin | 1E4261 | E8 FA 7D FE FF |
| bar_score_digits_end | 1E4269 | E8 62 CF E1 FF |
| bar_extra_panel_a_begin | 1E4299 | E8 12 D3 FF FF |
| bar_extra_panel_a_end | 1E429E | 51 D9 E8 D9 1C 24 |
| bar_extra_digits_a_begin | 1E42EA | E8 71 7D FE FF |
| bar_extra_digits_a_end | 1E42F2 | E8 D9 CE E1 FF |
| bar_extra_panel_b_begin | 1E4314 | E8 97 D2 FF FF |
| bar_extra_panel_b_end | 1E4319 | 51 D9 E8 D9 1C 24 |
| bar_extra_digits_b_begin | 1E4365 | E8 F6 7C FE FF |
| bar_extra_digits_b_end | 1E436D | E8 5E CE E1 FF |
| bar_mode_panel_begin | 1E4B99 | E8 12 CA FF FF |
| bar_mode_panel_end | 1E4B9E | E8 2D C6 E1 FF |
| bar_player_panel_begin | 1E4BDF | E8 CC C9 FF FF |
| bar_player_panel_end | 1E4BE4 | E8 E7 C5 E1 FF |
| bar_status_panel_begin | 1E4C36 | E8 75 C9 FF FF |
| bar_status_panel_end | 1E4C3B | E8 E0 E4 E6 FF |
| bar_names_begin | 24A27F | E8 1C 7D F9 FF |
| bar_names_end | 24A284 | 8B 85 54 FE FF FF |
| chain_label_begin | 1E4503 | E8 A8 D0 FF FF |
| chain_label_end | 1E4508 | 51 D9 45 D8 D9 1C 24 |
| chain_digits_begin | 1E4550 | E8 0B 7B FE FF |
| chain_digits_end | 1E4555 | 83 C4 20 C7 45 CC 00 00 00 00 |
| chain_glow_begin | 1E4609 | E8 52 7A FE FF |
| chain_glow_end | 1E4611 | E9 4B FF FF FF |
| hundred_digits_begin | 1E4762 | E8 F9 78 FE FF |
| hundred_digits_end | 1E4767 | 83 C4 20 E8 B1 E9 E6 FF |
| effect_packet_begin | 1F10C4 | E8 37 F5 FF FF |
| effect_packet_end | 1F10C9 | 8B 4D AC C7 41 70 00 00 00 00 |

## Task 1: Scope only selected immediate draws

Files: GameplayHudHooks.*, NativeCanvasCompositor.*, D3D9CompositorDevice.*,
WidescreenRuntime.h, RenderHooks.cpp, WindowedWidescreenAbi.*, WindowedWidescreenProfile.*.

- Construct compositor/device with center as ordinary gameplay baseline; retain
  settings.gameplay_hud_placement() for selected bar and existing network icons.
- Add BeginGameplayHudDraw(placement)/EndGameplayHudDraw() to the compositor,
  permitting physical_3d and gameplay_hud. Save/restore actual viewport, scissor,
  Z-enable, Z-write, and stencil state through dedicated device actions. Preserve
  projection, world matrix, texture, blend, render target, and helper side effects.
  Flush the four general batches before placement changes, preserving the
  pipeline state around a nonempty flush because the native flush changes
  shaders, declarations, and vertex streams. Never flush the private
  effect-manager queue.
- Save the enclosing cached placement separately and publish the temporary
  placement only for the selected call. Reject nested/mismatched ownership.
- BarDrawBeginMid selects configured placement; BarDrawEndMid restores only an
  active bar draw. Twenty bar call pairs are explicit positive selections.
- Counter begin callbacks read the existing native entry local and select
  ResolveComboHudPlacement(entry). Separate matching restoration hooks follow
  each label/digit/glow/hundred call. The old broad counter-end hook was retained
  temporarily as an observation point, then removed at closeout.
- Viewport-reset virtualization respects an active selected draw even in
  physical_3d, using its native-width viewport. Outside a scope use ordinary
  render-space policy.
- Add every new span to the supported profile, ABI names, hook bindings,
  installation order, and preflight capacities. Remove diagnostic-only contracts
  after acceptance; retain all contracts used by production behavior.

## Task 2: Preserve ownership through the private effect queue

Files: GameplayHudHooks.*, WidescreenRuntime.h, WindowedWidescreenAbi.*,
WindowedWidescreenProfile.*.

- Classify roots by exact CTune pointer slots, independent of texture/group.
  The root-visit scope records ownership and manager identity without changing
  queued-draw placement. Selected direct roots with no manager use a draw scope.
- Store only selected packet addresses/owners in loader-owned metadata.
  Invalidate an address on every observed allocation before assigning current
  root ownership. Grow metadata only as needed, catch allocation failures at
  the callback boundary, and report fatal placement failure instead of silently
  losing required ownership. Remove entries on submission; reset on device loss.
- At 5F10C4, look up and consume selected packet ownership, begin its draw scope,
  then execute the native draw. At 5F10C9, restore the actual enclosing state. Native packet bytes,
  texture buckets, linked lists, and submit order remain unchanged.
- Group-6 begin/end callbacks became observational markers during diagnosis
  and were removed at closeout.
  Frame/effects boundaries require no unbalanced selected scope or pending
  selected packet metadata.

## Task 3: Temporary song-end diagnosis (completed and removed)

Historical files: GameplayDiagnostics.* (removed), WindowedWidescreenFeature.cpp.

- Separate exact CTune-slot root keys from named draw/counter keys and incidental
  roots. Recycle incidental keys and cap their capture rate independently so
  generated note roots cannot consume result/staff/counter capacity.
- Preserve first/reentry samples for selected/results and every new hundred
  crossing. Bound unselected root samples by time rather than every pointer's
  reentry. Report key eviction, incidental throttling, and event-buffer drops
  separately; failed reads remain explicitly invalid.
- Capture selected draw entry/restore and packet allocation/submit/restore so
  both movement and containment are observable. Temporary tracing remains
  automatic, with no environment or configuration switch.
- Completed after operator acceptance: remove GameplayDiagnostics.*, eight
  diagnostic-only hooks, diagnostic native layout fields, all trace calls,
  frame-end emission, diagnostic allocation, and temporary startup messages.

## Task 4: Review, build, and document

- Review decoded protected spans for whole instructions, interior branches,
  overlap with all existing profile sites, and matching begin/end control flow.
- Use CLion diagnostics on changed C++ files. Compile full Debug and Release
  preset graphs from the x86 MSVC environment; inspect warnings/errors.
- Run git diff --check and review changed-file ownership. Do not add synthetic
  runtime tests and do not claim compilation proves native execution.
- Record the implemented selections and the operator's actual observations in
  the design/diagnostic record. Preserve historical evidence after removing the
  tracing code.

## Completed verification and closeout

- Fresh IDA-CLI matched all 86 retained byte contracts against the supported
  game471 input. Hook spans contain complete instructions, have no interior
  code references, and do not overlap. The profile retains nine pointer
  contracts and installs 83 hooks, with 95 preflight operations.
- Both full msvc32-debug and msvc32-release builds passed with no compiler or
  linker warnings/errors. CMake regeneration reported dependency deprecation
  warnings from Zydis/Zycore. Build log:
  .codex-tmp/widescreen-closeout-20260906/build.log.
- CLion completed error inspections of GameplayHudHooks, WindowedWidescreenProfile,
  WindowedWidescreenAbi.h, WindowedWidescreenFeature, D3D9CompositorDevice,
  NativeCanvasCompositor, RenderHooks, and WidescreenRuntime without errors.
- git diff --check passed. No synthetic tests were added or run.
- The operator confirmed GREAT placement and that the flash was gone. The deployed
  correction DLL checked at closeout is
  558CF8CB0071013F55017CA437AFE0423C95DEB6978C397FD09CCA2C4799E48C.
- Cleanup Release artifact: build-msvc32-release/dist/iDmacDrv32.dll, SHA-256
  7CB548E3173BD77BA6E55FB9BC2F478FA958C87A0CA1067567E4E7DB8B24F1D9.
  Matching symbols: build-msvc32-release/src/iDmacDrv32.pdb.
  This rebuilt artifact has not been deployed or run during cleanup.

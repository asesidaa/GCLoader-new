# Windowed Widescreen Stage Design

## Background projection correction, 2026-09-06

The later screenshot comparison exposed two remaining defects: CTune+110 is
shared by the HUD, stage color background, and visualizers, while the expanding
hundred-chain rectangle needs more than the centered 720-pixel viewport.
The current-target correction leaves both the cached matrix and the mixed
effects pass at output dimensions. Native projection, logical dimensions and
viewport placement apply only inside the selected bar/counter/effect draws,
plus explicit centered stage-title, stage-player-detail and timed-text draw
scopes. Backgrounds, visualizers, rectangles and stage fades keep their native
full-output state. There is no rectangle exception or shared projection hook.
This supersedes the shared matrix-builder hook and centered native-width
mixed-pass assumptions below.

The narrowed correction is implemented with 88 hooks. On 2026-09-06 the
operator reported that the 4.74 issues appeared fixed, including the ONLINE
follow-up; that correction was committed as `634ce44`. The subsequent 2.06
port also uses 88 hooks; the operator subsequently confirmed it works after
the dotted-separator follow-up and authorized cleanup/commit.
See the [native evidence, exact sites, and runtime checklist](../../reverse-engineering/widescreen-background-projection-followup-2026-09-06.md).

## GC 2.06 profile, 2026-09-06

The older game's narrowed port has 94 byte and nine pointer contracts for the
same 88-hook selected-draw policy. Its `CTune` effect collection is `+0x1D60`, and
the native Test Mode pass uses `0x207F3C/0x207F41`. The six judgement text
and nine reached tutorial slots remain shared; initialized-but-unselected
tutorial slot B7 remains excluded. Native contracts and both build presets
pass; the operator confirmed the reported 2.06 fix after the separator follow-up.
The old `bar_names` mapping incorrectly selected the opening title; the port
moves that pair to the actual top-bar details and centers the opening title
and player list separately. Its additional dotted separator is the unnamed
`UNIQUE_150` child of `imc_head` in both language assets. The existing Flash
clip hook selects that definition plus its direct parent, only for 2.06, and
applies the configured top-bar viewport. Runtime tracing shows its terminal
shape resets that viewport to full output, so only the selected separator's
shape receives the same scoped x-matrix compensation as the local-network
icon. The visitor matrix is restored immediately after native submission.
Three read-only field guards support this identity; the rest of the common
header remains unselected. Temporary separator traces and successful per-clip
messages were removed after acceptance; startup status and capped failure
warnings remain.
See the [2.06 port evidence](../../reverse-engineering/gc206-narrowed-widescreen-2026-09-06.md)
and [initial implementation record](../../reverse-engineering/gc206-implementation-2026-09-06.md).

## Placement containment correction, 2026-09-06

Initial runtime observations showed that the placement scopes were too broad.
The top-bar setting must affect only identified bar draws. The stage-start
image/title/player introduction, staff roll, hundred-chain expanding rectangle,
and stage-finish announcements must remain centered, and song visualizers must
retain their native placement. Group 6 and the combined HUD/counter function
are not exclusive owners of the elements we want to move.

The operator's diagnostic run confirmed the broad scopes and deferred judgment
viewport loss. The correction is now implemented with twenty explicit bar draw
pairs, separate label/digit/glow/hundred-number pairs, and exact judgment/tutorial
ownership carried to packet submission. The orthographic baseline is centered;
group 6 and the combined HUD function are not placement scopes. Diagnostic logs
established these boundaries, and the operator reported most placement correct. A follow-up adds fixed-position judgment text roots
12,15,18,24,27,30 decimal: the earlier 93–97 selection covers track-position
grade effects, not these text roots. See the
[judgment text evidence and correction](../../reverse-engineering/widescreen-judgment-text-followup-2026-09-06.md).
The operator now confirms GREAT text placement. Slots 93–97 are removed from
placement selection so their rotating, expanding grade effects retain the native
track position. The operator confirms the right-side flash is gone. The fix is
accepted for closeout; temporary trace storage, logging, and eight diagnostic-only
hooks are removed. The final enabled profile has 83 permanent hooks. See the
[right-side flash follow-up](../../reverse-engineering/widescreen-right-flash-followup-2026-09-06.md).
This supersedes older placement boundaries below. See the
[implemented boundaries and closed diagnostic record](../../reverse-engineering/widescreen-placement-diagnostics.md)
and [implementation plan](../plans/2026-09-06-widescreen-exact-draw-placement.md).

## Context

Groove Coaster's supported runtime is already an upright, windowed 720 x 1280
application. The current `H:\gc\data\system.cfg` states:

```text
WindowMode     = 1
WindowSize     = (720, 1280)
WindowResize   = 1
MultiSampleType = 0
```

IDA confirms that these are the real runtime dimensions, not presentation
labels. `game471.exe` parses the two `WindowSize` elements in order, passes
them to the native width and height setters, and uses those settings for the
window and Direct3D 9 backbuffer. The constructor's earlier 1280 x 720 values
are only defaults overwritten by `data/system.cfg` during startup.

The player will keep Windows and the monitor in their normal orientation. The
first release remains windowed and fixed-size at launch. No monitor rotation,
Windows orientation change, transposed render target, or final image rotation
belongs in this feature.

Deleting stage `_clip.dat` files has demonstrated that the perspective stage
can expose geometry outside the authored 720-pixel width. That alone is not a
complete widescreen mode: native edge-resizing lets Direct3D stretch the
existing backbuffer, which also stretches and misaligns 2D content. The
required design separates an expanded upright 3D render space from an exact
native 720 x 1280 2D render space while preserving their original draw order.

Implementation belongs in `H:\gc\artifacts\GCLoader`. The runtime tree at
`H:\gc`, including `data/system.cfg` and `_clip.dat`, is evidence only and is
not edited or deployed by this work.

## Native Evidence

The current `game471.exe.i64` establishes these rendering contracts. Virtual
addresses assume the preferred image base `0x00400000`; implementation must
express selected patch sites as RVAs with exact expected-byte guards.

| Native site | Observed responsibility | Design consequence |
| --- | --- | --- |
| `0x00635250` | Parses `WindowMode`, both `WindowSize` values, `WindowResize`, `CheckDeviceLost`, and `MultiSampleType` from `data/system.cfg`. | Runtime dimensions and window policy are directly available before device creation. |
| `0x0063C360` | Applies the parsed width and height through main-config vtable slots `+24` and `+28`, resize through `+40`, and mode through `+48`. | The feature can override the loaded settings through the native configuration seam without modifying `system.cfg`. |
| `0x00459CC0` | Stores the first `WindowSize` value in both configured width fields. | The first value is physical upright width. |
| `0x00459CE0` | Stores the second `WindowSize` value in both configured height fields. | The second value is physical upright height. |
| `0x0045AF10` | Builds Direct3D presentation parameters and creates the device from the selected configuration pair. | The real backbuffer uses the configured upright output size directly. |
| `0x0045B8A0` | Creates and sizes the game window from the same configuration. | Window client and backbuffer can share one upright output size. |
| `0x0045B6D0` | Handles `WM_SIZE` by reapplying the existing viewport without resetting/reallocating the backbuffer. | Native edge-resizing is presentation scaling, not true resolution change. |
| `0x0045B490` | Calls `Present` with null source and destination rectangles. | A client/backbuffer mismatch would stretch the completed frame. |
| `0x0045AC70` | Clears the current target and calls `BeginScene`. | The wide scene target must be bound before the native clear. |
| `0x0045ACE0` | Calls `EndScene` and the native system callback. | The completed scene must be copied to the real backbuffer before native end-frame handling. |
| `0x00452F20` through `0x00452FD0` | Return separate screen and current-target dimensions as integer and float variants. | The persistent screen domain can remain 720 x 1280 while the persistent target domain carries the physical output size; active render passes still require space-aware answers. |
| `0x00453660`, `0x00452F60`, `0x00452F80` | The combined publisher writes both screen and target dimensions; the scalar setters rewrite only target width or height. | Initialization must publish a native screen and then restore a physical target. Device reset must preserve the physical target dimensions. |
| `0x00453140` | Restores a full-target viewport from native global dimensions. | Native 2D scope must prevent this helper from restoring the wide viewport. |
| `0x0063F5F0`, `0x0063F660` | Build and cache perspective projections from target width/height and authored FOV scale. | Persistent target aspect must be physical even when the screen/UI domain remains native; otherwise the cached portrait projection is stretched across the wide viewport. |
| `0x0063E4C0`, `0x0063E710`, `0x005ECC30` | Load and validate `_clip.dat` visibility tables. | Asset deletion is unnecessary; policy can select the existing fallback. |
| `0x00644000` | Uses authored per-frame/part visibility when valid, otherwise live frustum testing. | Widescreen uses live frustum culling while retaining installed files. |
| `0x0045C1B0`, `0x0045C7D0` | Dispatch task rendering in priority order. | Contiguous task-level 2D and 3D regions can be classified centrally. |
| `0x00662F10` | Interleaves orthographic and perspective gameplay rendering in one task. | Gameplay needs internal subpass transitions in addition to task classification. |
| `0x006449F0`, `0x00641DE0` | Bind stage-background transforms and render the animated four-corner color quad using target width/height before the perspective track. | This is stage-owned output, not HUD; it must use physical space so the authored background fills the widened stage. |
| `0x005E4503` through `0x005E4B58` | CHAIN label, ordinary digits, numeric glow, enlarged hundred-number burst, and separate expanding rectangular lines; entry at `[ebp-0x14]`. | The current bracket also moves the rectangle and must be narrowed to exact selected draws. |
| `0x00662FA8` | Calls the perspective-track owner with CTune in `ecx` immediately before the player-visual function. | Capture CTune early enough to identify Player 1 judgement, which renders before the later gameplay-HUD pass. |
| `0x006463F0` | Produces judgement effects only for native owner lanes `nn < 4`; grades 0 through 4 use CTune slot `93 + 5 * nn + grade`. | Player 1 is the first lane, slots 93 through 97. The other lanes are not interpreted as multiplayer participant capacity, and the producer is not hooked. |
| `0x005F11E8` through `0x005F11ED` | Calls the generic effect-tree renderer with the current effect root in `ecx`. | An exact match for Player 1 judgement temporarily captures physical-3D D3D state, applies the right native HUD viewport for the complete tree, then restores the captured state. |
| `0x0064A2D5` through `0x0064A2DA` | Draws effect-manager group 6, including tutorials and stage-finish announcements. | Whole-group movement is too broad; identify selected roots and their submitted packets. |
| `0x0063AA89` through `0x0063AA8E` | Calls `0x00576660`, which renders the Test Mode `CRootWindow`, before continuing LoopLastTask bookkeeping. | A guarded call bracket can render the complete Test Mode form through the centered native canvas without resizing or stretching the wide output. |

The `_clip.dat` parser accepts a header plus one byte for each
`part x frame` combination. Rendering uses a nonzero authored byte as
visibility permission and takes the live-frustum branch only when the table is
unavailable or invalid. These files are authored visibility/optimization data,
not framebuffer dimensions.

The engine also submits pre-transformed `D3DFVF_XYZRHW` screen-space vertices,
including FVF `0x10104` at `0x004A3DB0`. A centered viewport cannot translate
all such vertices. This rules out a viewport-only production solution even
though it would work for normally projected geometry.

## Goals

- Create a fixed-size window with an exact configured upright client and
  backbuffer size.
- Accept output widths of at least 720 at the fixed 1280-pixel height without
  scaling native 2D. Width has no product-specific maximum; normal signed,
  pixel-area, monitor-fit, and Direct3D capability limits still apply.
- Keep authored menus and common 2D tasks on the centered 720 x 1280 native
  texture. Render gameplay HUD/effects at native 720 x 1280 scale directly on
  the wide scene through a 720 x 1280 viewport, without horizontal stretching.
- Keep the gameplay HUD centered except while drawing an ordinary combo
  counter: entry 0 uses the right 720-pixel viewport and entry 1 uses the left.
  Move Player 1's tutorial group and judgement roots to the right viewport;
  opponent judgement, hundred-chain rectangular lines, and finish announcements
  remain centered. Numeric glow and enlarged hundred-number draws are distinct
  from the rectangular lines.
- Let the verified stage-owned color background fill the physical stage before
  the native UI overlay is composited.
- Render verified perspective stage passes across the complete output.
- Preserve native render order and blending when 2D and 3D alternate within a
  frame.
- Keep `_clip.dat` installed and select live-frustum culling through explicit
  policy.
- Preserve the original center view's perspective pixel scale while exposing
  additional view around it.
- Integrate compositor resources with Direct3D device-loss/reset handling.
- Reject unsupported binaries, capabilities, and partial installation without
  silently displaying stretched content.

## Non-goals

- Fullscreen or borderless-fullscreen output in the first release.
- Live edge-resizing, maximization, or following arbitrary client-size changes
  in the first release.
- Monitor rotation, Windows orientation changes, transposed targets, or a
  rotation setting.
- General relayout, scaling, or replacement of authored 2D assets beyond the
  explicitly guarded ordinary-combo and local-judgement exceptions.
- Widening orthographic chart, HUD, menu, or gameplay-effect passes. The
  verified stage-background color quad is explicitly stage-owned and is not
  part of this native UI restriction.
- Supporting multisampled scene rendering in the first release.
- Forcing all stage parts visible when live frustum culling rejects them.
- Editing or deleting `_clip.dat` or `data/system.cfg`.
- Supporting unverified executables through signatures or scanning.
- Deployment to `H:\gc` or claiming in-game acceptance from static checks.

## Coordinate Spaces

The design uses two upright coordinate sizes and three game-visible render
spaces. Nothing is rotated.

### Output and wide-scene space

`OutputSize = (output_width, 1280)` is simultaneously:

- the real decorated window's client size;
- the Direct3D backbuffer size; and
- the offscreen wide-scene render-target size.

Examples are 1137 x 1280 and 1920 x 1280. The offscreen scene and backbuffer
have the same orientation and dimensions. The final compositor copy is direct,
not rotated or scaled.

### Native 2D canvas space

`NativeCanvasSize = (720, 1280)` is invariant. Its centered output rectangle
is:

```text
native_x = floor((output_width  -  720) / 2)
native_y = 0
native_rect = (native_x, native_y, 720, 1280)
```

Examples:

| Output | Native rectangle | Extra area |
| --- | --- | --- |
| 720 x 1280 | `(0, 0, 720, 1280)` | None |
| 1137 x 1280 | `(208, 0, 720, 1280)` | 208 left, 209 right |
| 1920 x 1280 | `(600, 0, 720, 1280)` | 600 left and right |

Odd horizontal differences put the extra pixel on the right. Native content
is copied one-to-one with no filtering scale.

### Gameplay HUD space

`GameplayHud` retains native logical dimensions `(720, 1280)` but renders
directly into the wide-scene texture. Its normal viewport is `native_rect`.
Temporary ordinary-counter viewports are:

```text
left_combo_rect  = (0,                  0, 720, 1280)
right_combo_rect = (output_width - 720, 0, 720, 1280)
```

The viewport and scissor move together. Native dimension queries still return
720 x 1280, and the hooked native viewport-reset helper translates any local
viewport origin by the active gameplay-HUD origin. This prevents a native
reset from collapsing a centered or side HUD viewport back to output `x = 0`.
At 720 x 1280, centered, left, and right resolve to the same rectangle.

## Alternatives Considered

### Change global dimensions and render directly to the backbuffer

This is equivalent to the observed resized-window behavior. Screen-space and
orthographic content sees the new dimensions or is stretched by `Present`, so
2D placement and proportions change. It is rejected.

### Center 2D with a viewport and virtualized screen getters

This is cheap and works for transformed Flash content. It cannot reposition
the confirmed pre-transformed `XYZRHW` paths without intercepting all immediate
and buffered screen-space drawing. That patch surface is larger and more
fragile than giving 2D a genuinely native target. It is rejected as the
production architecture.

### Render a transparent UI overlay

A transparent layer would need to reproduce every native straight-alpha,
premultiplied-alpha, destination-dependent, and color-write behavior. A single
transparent result cannot generally recover drawing performed against an
already-rendered background. It is rejected.

### Copy with `StretchRect`

`StretchRect` cannot run inside the game's existing
`BeginScene`/`EndScene` pair. Splitting one native frame into multiple scene
pairs would change submission structure and ordering. These are explicit
Direct3D 9 constraints in the Microsoft documentation for
[`StretchRect`](https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3ddevice9-stretchrect)
and
[`EndScene`](https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3ddevice9-endscene).
The design uses render-target textures and textured quads inside the existing
scene instead.

### Selected: background-preserving segmented compositor

The selected design renders into a wide texture, copies the current scene
center into a true 720 x 1280 target before each native 2D segment, lets the
game perform its original blending there, and copies the completed result back
into the scene center. It handles transformed and pre-transformed content
uniformly and preserves destination-dependent blending for common 2D tasks.

The gameplay task uses a separate `GameplayHud` space. It keeps the wide scene
bound and renders with native 720 x 1280 logical dimensions through a centered
viewport. Ordinary combo hooks temporarily select the left or right native-
sized viewport, with a batch flush before each viewport change. This avoids a
wide/native texture crossing inside the interleaved gameplay task and keeps
the full-width stage/background intact.

## Architecture

```text
WindowedWidescreenSettings
        |
        v
ResolutionModel ----------> output size / native_rect
        |
        +-> NativeWindowPolicy
        |       native config override, exact client/backbuffer
        |
        +-> NativeCanvasCompositor <---- RendererResourceLifecycle
        |       wide scene, native canvas, gameplay HUD, copy state
        |
        +-> RenderSpacePolicy <---------- PassClassifier
        |       Physical3D / Native2D / GameplayHud / Compositor
        |
        +-> GameplayFeedbackPlacement
        |       centered/left/right HUD viewport, combo ownership,
        |       Player 1 tutorial-group/judgement ownership
        |
        +-> AuthoredClipBypass
                always uses live-frustum visibility while enabled
```

### ResolutionModel

A pure component validates configured dimensions and derives `native_rect`
and input-coordinate mapping. It has no Direct3D, window, executable-memory,
or serialization dependencies.

### NativeWindowPolicy

After `0x0063C360` has applied `data/system.cfg`, the feature uses the same
native main-config setter seam to establish its enabled V1 policy:

- window mode enabled;
- configured GCLoader output width and height;
- native window resizing disabled; and
- existing device-loss checking retained.

The runtime file remains unchanged. Applying the override after native parsing
means command-line and file handling complete normally, then one explicit
feature policy becomes authoritative before device/window creation.

The window retains move, minimize, restore, and close support. Its style omits
the thick sizing frame and maximize action so client dimensions cannot diverge
from the backbuffer. `AdjustWindowRect` derives outer dimensions from the exact
client size.

At startup, monitor work areas are examined without introducing monitor
configuration. The primary monitor is selected when it fits; otherwise the
first enumerated monitor whose work area fits is selected. If no work area can
contain the adjusted outer rectangle, enabled startup fails rather than
scaling or creating a partly inaccessible window.

### RenderSpacePolicy

The render thread holds a thread-local scope:

```cpp
enum class RenderSpace {
    Physical3D,
    Native2D,
    GameplayHud,
    Compositor,
};
```

In `Physical3D`, screen and current-target queries report `OutputSize` and the
viewport covers the wide scene. In `Native2D`, they report 720 x 1280 and
viewport reset means the full native canvas. `Compositor` is loader-owned and
cannot invoke game rendering. In `GameplayHud`, queries also report 720 x 1280,
but the wide scene remains bound and viewport reset preserves the active
centered/left/right output origin.

The same native getters are also used outside drawing by window sizing,
device reset, and non-render helpers. When no compositor frame is active,
getter and viewport hooks therefore pass through to the original native
function unchanged. Render-space virtualization begins only after the
frame-begin hook has published `Physical3D` and ends before native end-frame
handling. This keeps lifecycle code independent of render-thread scope while
retaining a fatal invariant for any game query made in `Compositor` space.

The game has two persistent dimension domains, not one logical canvas.
`0x00453660` writes both the screen pair used by screen/UI matrices and the
target pair consumed by the perspective builders. Window/device initialization
at `0x0045B8A0` calls that combined publisher with the physical window size.
The guarded detour instead calls the original publisher with 720 x 1280, then
calls target-only setters `0x00452F60` and `0x00452F80` with the configured
physical width and height. Device reset detours likewise preserve the physical
values supplied to those target-only setters.

The resulting persistent invariant is `screen = 720 x 1280` and
`target = OutputSize`. Screen-owned cached 2D matrices therefore remain
native-sized while perspective matrices cached outside an active compositor
frame receive the wide aspect. During an active `Native2D` or `GameplayHud`
segment, render-space-aware target getters temporarily return 720 x 1280;
during `Physical3D` they return the output size. Publishing both pairs as
physical produces the observed 270-pixel-wide UI strip. Publishing both pairs
as native fixes the UI but leaves a portrait perspective matrix that D3D
stretches across the wide viewport.

Gameplay HUD projection is one verified exception to screen ownership.
`0x0063F9E0` constructs the cached perspective and HUD matrices before an
active compositor frame. Its call at `0x0063FDBA` supplies target width and
height to orthographic builder `0x005DF170`, stores the result at owner
`+0x110`, and `0x00648D40` later binds that matrix as `D3DTS_PROJECTION` while
rendering gameplay HUD/effects. The guarded mid-hook at RVA `0x0023FDBA`
therefore changes only the pending orthographic call's `right` and `bottom`
stack arguments to 720 and 1280. It preserves `left`, `top`, near/far planes,
the physical target pair, and the separately cached perspective matrix.

The policy records the render-thread identity at first frame entry. A scope
transition from another thread is a fatal invariant violation. Normal getter
hooks contain no per-call logging.

### PassClassifier

The task-dispatch hook classifies stable task identities by native name and
vtable. It changes space only when requested space differs from current space,
allowing adjacent equal-policy tasks to remain one segment.

Unknown tasks default to `Native2D` and emit at most one development diagnostic
per stable identity. This protects unclassified menus from stretching. It may
temporarily keep an unclassified 3D scene native-sized until ownership is
verified.

Mixed gameplay rendering is classified at verified call boundaries inside
`0x00662F10`:

| Native call | Space | Reason |
| --- | --- | --- |
| `0x006449F0` | `Physical3D` | Stage-background transforms and the `0x00641DE0` target-sized animated color quad. |
| `0x00648680` | `Physical3D` | Perspective track and stage-part rendering. |
| `0x0064DA90` | `Physical3D` | Perspective player visual. |
| optional `0x00645120` | `Physical3D` | Perspective stage-related rendering. |
| `0x005C9B10` | `Physical3D` barrier | Flushes four native buffered queues before the next transition. |
| `0x00648D40` | `GameplayHud` | Orthographic gameplay effects, chart visuals, and HUD render at native scale directly on the wide scene. |

`CCommon3DTask` at priority 700 uses `Physical3D`.
`CCommon2DTask` at priority 800 uses `Native2D`.

Every target or gameplay-HUD viewport transition flushes the four native
buffered queues through `0x005C9B10` before changing target or viewport. The
final native pending-buffer check remains fail-closed. Generic compositor
transitions do not finalize or execute the animation command manager: doing so
changes RVB scheduling in ordinary menus.

The network-status exception is deliberately scoped to exact instances named
`imc_ico_n` and `imc_ico_l`. The transaction guards and replaces only
`MovieClipInstance::Accept` vtable slot VA `0x006BE0E0` (RVA `0x002BE0E0`),
whose supported target is VA `0x004E0CD0`. The detour additionally requires the
draw-traverse visitor vtable VA `0x006BB74C`, the frame-local gameplay latch,
and an active gameplay render space. It verifies the zero-seeded 33x name hash
and exact runtime string before changing any state; all other movie clips pass
unchanged.

During a qualifying `GameplayHud` visit, the compositor forcibly reapplies the
physically centered 720 x 1280 hardware viewport even when its cached placement
already says centered. The game-facing screen/target and viewport queries remain
logical `(0,0,720,1280)` during the exact subtree. The subtree's pending native
batches are drained before the preceding gameplay-HUD placement is forcibly
reapplied. This is required because game rendering can overwrite D3D viewport
state without updating the compositor's placement cache. Boot, attract, and
ordinary-menu visits pass directly through because those frames never arm the
latch. Missing identity, inactive compositor state, or an unavailable optional
placement scope also fails open. The implementation does not finalize, wait on,
execute, reset, or otherwise alter the animation command manager. The rejected
panel `+0x0C` and full-`common.rvb` wrappers were broader or earlier than the
actual clip traversal and did not correct the icons.

Within `GameplayHud`, RVA `0x001E4503` currently begins the per-entry counter
window before the CHAIN label. RVA `0x001E4550` is the ordinary digit call and
is temporarily instrumented by a read-only diagnostic detour on every widescreen run. RVA
`0x001E4B58` restores configured HUD placement at the shared join after numeric
glow, enlarged hundred-number drawing, and the separate expanding rectangle.
Entry 0 selects right, entry 1 left, and an unexpected entry center. The
rectangle's inclusion in this interval is an observed containment defect.

The gameplay-track call at RVA `0x00262FA8` captures CTune before the
player-visual function. Within that scope, the generic manager call at RVA
`0x001F11E8` receives its current effect root in `ecx`. Exact pointer
matches against Player 1 slots 93 through 97 capture the current physical-3D
D3D state and temporarily apply the right native HUD viewport. `sub_5F1F70`
visits children, but can enqueue their sprites in the manager's private queue.
RVA `0x001F11ED` restores the captured state after flushing the four general
native queues. That flush does not establish completion of the private queue;
the diagnostic run pairs allocation and later submission to observe this gap.

The gameplay-effects call at RVA `0x00263041` then enters `GameplayHud`.
Within `GC120FPS_GameplayRender_Effects_FrameDomainTiming`, the direct group-6
call at RVA `0x0024A2D5` currently selects Player 1's right viewport, and RVA
`0x0024A2DA` restores configured gameplay-HUD placement. Group 6 also contains
stage-finish roots and other effects. The final correction must select exact
tutorial owners at their actual draw boundary.

Test Mode is a direct root-form traversal rather than a common-task vtable
dispatch. `sub_453120` returns the renderer's `IDirect3DDevice9*` from
`GWMainPC+0x08`; LoopLastTask calls that device's vtable slots `+0xA4` and
`+0xA8` directly for `BeginScene` and `EndScene`, bypassing the game's normal
frame wrappers at `0x0045AC70` and `0x0045ACE0`. The guarded boundary at RVA
`0x0023AA89` therefore opens a standalone compositor frame after the direct
`BeginScene` and requests `Native2D`. RVA `0x0023AA8E` finishes and composites
that frame after `sub_576660` renders the complete form and before the direct
`EndScene`. A missing or duplicate local scope is not process-fatal. The
configured wide window and backbuffer remain unchanged, so Test Mode is
centered and unstretched with unused horizontal space left blank.

## Frame Data Flow

### Begin frame

The hook around `0x0045AC70` binds the wide scene texture and its compatible
depth surface before invoking the original function. The game's original
clear color, depth/stencil flags, and `BeginScene` therefore operate directly
on the complete output-sized scene.

### Enter native 2D

After the previous segment's batch barrier:

1. capture the game's complete draw state;
2. bind the 720 x 1280 native-canvas target without a depth surface, thereby
   unbinding the scene texture as a render target;
3. draw `native_rect` from the scene texture into the complete native canvas;
4. reapply captured game state;
5. explicitly set the full native viewport and scissor after state restore;
6. enter `Native2D`; and
7. invoke native rendering.

Copying the current center background before 2D preserves destination-
dependent blending. The canvas does not need to encode a separately
compositable alpha layer.

Approved native 2D segments render with depth testing, depth writes, and
stencil disabled. The compositor reapplies those states after restoring the
game pipeline, then verifies them before entering the segment. A later native
draw that requires a different policy must be explicitly designed rather than
inheriting physical depth state accidentally.

### Enter gameplay HUD

After the gameplay batch barrier, the compositor keeps the wide scene and its
depth surface bound, switches logical dimensions to 720 x 1280, applies the
centered 720 x 1280 viewport/scissor, and disables depth testing, depth writes,
and stencil. No scene/native texture copy occurs when moving between
`Physical3D` and `GameplayHud` because both spaces use the wide target.
Gameplay HUD blending therefore operates against the actual wide-scene color
already present at each destination pixel.

Ordinary combo begin/end hooks temporarily change only the gameplay-HUD
viewport and scissor after flushing pending batches. The native viewport-reset
detour adds the active output origin to any game-supplied local origin, so the
game cannot erase the temporary placement. Returning to center leaves the
remaining chart/HUD/effect rendering at native scale in `native_rect`.

### Return to physical 3D

After the native segment's batch barrier:

1. capture draw state;
2. bind the wide scene and its depth surface, thereby unbinding the native
   canvas as a render target;
3. copy the complete native canvas opaquely into `native_rect`;
4. reapply captured game state;
5. explicitly set the full wide viewport and scissor after state restore; and
6. enter `Physical3D`.

Later perspective geometry can overwrite the center result exactly where
native render order requires while also drawing into newly exposed regions.

### End frame

Before `0x0045ACE0` calls native `EndScene`, the compositor:

1. closes an outstanding native or gameplay-HUD segment if necessary;
2. binds the real output-sized backbuffer;
3. draws the complete wide scene into the complete backbuffer one-to-one; and
4. invokes native end-frame handling.

The original `EndScene` and windowed `Present` run once. There is no
additional scene pair, image rotation, scaling, or CPU readback.

### Copy-state contract

All copy quads use:

- point sampling and clamped addressing;
- Direct3D 9 half-pixel-correct destination coordinates;
- one-to-one source/destination texel mapping;
- fixed-function `XYZRHW | TEX1` vertices or loader-owned equivalent;
- solid fill with culling, user clip planes, coordinate wrapping, and texture
  transforms disabled;
- opaque color selection with alpha blending, depth, stencil, fog, and scissor
  disabled;
- all color channels enabled;
- sRGB texture sampling and sRGB writes disabled; and
- a `D3DSBT_ALL` state block around loader-owned drawing.

Render targets and depth surfaces are saved and restored explicitly. Because
the all-state block includes viewport and scissor, the destination target's
intended viewport/scissor is explicitly re-established after applying the
captured block. A texture is always unbound from render-target use before it
is sampled. This also satisfies the documented
[`SetRenderTarget`](https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3ddevice9-setrendertarget)
viewport reset and depth-surface compatibility rules.

## Projection Policy

Output height is exactly 1280. The game keeps its native authored vertical FOV
and uses the wider live target aspect ratio:

```text
vertical_fov = 75 degrees * native_CTune_scale
aspect = output_width / 1280
horizontal_fov = 2 * atan(tan(vertical_fov / 2) * aspect)
```

Increasing width from 720 therefore expands horizontal perspective naturally.
The persistent physical target getters already supply the required aspect to
the perspective builders at `0x0063F5F0` and `0x0063F660`; no projection-scale
detour is required at fixed height. Near/far planes and native CTune scale
remain unchanged.

Output beyond monitor or device capabilities is rejected during setup.
Common orthographic tasks run against the true 720 x 1280 native canvas.
Gameplay orthographic passes run with the same logical extents through the
active 720 x 1280 `GameplayHud` viewport on the wide scene. The gameplay HUD
matrix described above is constructed explicitly with 720 x 1280 extents
because its native owner reads the persistent physical target pair before the
compositor frame begins.

### Player 1 judgement placement

`sub_6463F0` accepts a rendered judgement-owner slot `nn` only when
`nn < 4`. For grades 0 through 4 it resolves the effect root from:

```text
CTune.effect_slots[93 + 5 * nn + grade]
```

The four values are native judgement-effect owner lanes, not an asserted limit
on matching or LAN participants. Current scope is Player 1 only, so the
widescreen runtime builds the five-slot set at 93 through 97. It does not read
participant state, and no other lane is used as a coordinate or direction
reference.

When the manager presents one of those exact roots at RVA `0x001F11E8`, the
runtime captures the physical-3D D3D state and selects Player 1's right 720 x
1280 HUD viewport for the complete effect-tree draw. It flushes the tree and
restores the captured physical state at RVA `0x001F11ED`. It does not rewrite
the effect's position, mutate other lanes, or hook the producer. At width 720,
the right and centered viewports are identical.

## Stage Clip Bypass

Enabled widescreen always skips the valid `_clip.dat` branch at `0x006441CA`
and continues at `0x0064422F`. This selects the game's existing no-valid-table
path, allowing `0x00643BE0` to test stage parts against the widened projection.
It does not bypass live frustum culling or force all parts visible.

There is no clip policy or configuration option. The installed asset is not
deleted or modified. Disabling widescreen installs no clip hook and therefore
retains native authored behavior.

## Configuration

The strict runtime document gains these required experimental fields, with
disabled defaults present in the distributed configuration:

```toml
[experimental]
enable_windowed_widescreen_stage = false
widescreen_window_width = 1920
widescreen_window_height = 1280
```

There is no rotation, fullscreen, borderless, UI-scale, UI-anchor, monitor, or
resize-policy field in V1.

The pure configuration compiler validates:

- width is at least 720, with no arbitrary product maximum;
- height is exactly 1280;
- rectangle/dimension arithmetic cannot overflow the settings types.

Enabled startup capability checks, outside semantic compilation, validate:

- the adjusted decorated window fits at least one monitor work area;
- both dimensions fit `D3DCAPS9` texture limits;
- the native backbuffer format supports a single-level
  `D3DUSAGE_RENDERTARGET` texture;
- the compatible depth format/surface can be created; and
- active `MultiSampleType` is disabled.

The supported runtime currently supplies `MultiSampleType = 0`. A different
active setting rejects enabled widescreen rather than silently disabling
anti-aliasing or inventing an in-scene resolve path.

## Device and Resource Lifecycle

The wide scene texture, native-canvas texture, scene depth surface, acquired
surfaces, and state blocks are default-pool resources and must be released
before Direct3D `Reset`, as required by the Direct3D 9
[`Reset`](https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3ddevice9-reset)
lifecycle.

The reset path at `0x0045B270` performs a native pre-reset notification, calls
`Reset`, restores dimensions/viewport, and performs a post-reset notification.
A renderer-resource lifecycle owner coordinates the existing renderer recovery
and compositor:

```text
Disabled
  -> AwaitingDevice
  -> Active
  -> ReleasingForReset
  -> AwaitingReset
  -> Recreating
  -> Active
```

The compositor releases after native pre-reset subscribers and before
`Reset`. It recreates only after a successful reset and native post-reset
notification. Minimize/restore and lost-device sequences may repeat without
leaking COM references or retaining default-pool resources across reset.
The same pre-reset release closes any interrupted logical compositor frame;
this covers a failed native `BeginScene` whose normal frame-end hook never ran
and prevents the first frame after recreation from being rejected as nested.

The shared lifecycle is justified because existing renderer recovery and the
new compositor are two production consumers of the same ordering. It remains
renderer-specific, not a general callback registry.

## Input Mapping

Gameplay booster/button input is logical and independent of screen
coordinates.

The native mouse/debug path uses client coordinates directly. During a native
UI hit test it subtracts `native_rect` and rejects coordinates outside the
720 x 1280 canvas. No rotation, stretched coordinate, or side-band clamping is
applied.

## Installation and Failure Behavior

All native hooks and patches form one guarded transaction:

1. confirm the supported loaded image base and checked address arithmetic;
2. read every site and continuation target before creating the first hook;
3. verify device/window pointers only through proven native ownership paths;
4. create candidate hooks and reset all candidates if any installation fails;
5. publish runtime ownership only after every hook succeeds; and
6. emit one structured success record or precise stage/site failure.

If static preflight fails, native `data/system.cfg` behavior remains unchanged.
If enabled device resources cannot be created before first render, startup
stops with a clear error rather than showing stretched output. A mid-session
compositor failure suspends rendering and selects the existing startup-fatal
and logging boundary; rendering does not continue with uncertain targets.

Normal rendering, dimension getters, and successful transitions produce no
per-call logging. Once-per-identity classification diagnostics and aggregate
transition counters are development-only.

## Source Ownership

The feature is focused under `src/Patches/WindowedWidescreen/`:

```text
WindowedWidescreenPatch.h/.cpp
ResolutionModel.h/.cpp
NativeCanvasCompositor.h/.cpp
RenderSpacePolicy.h/.cpp
PassClassifier.h/.cpp
```

Configuration fields remain owned by the shared configuration document and
compiler. Device-reset ordering is integrated with the existing renderer
device-loss module rather than duplicated in a second unordered hook. Loader
startup invokes the initializer only in the game process and after validated
settings are available.

The implementation record names each selected RVA, exact expected bytes, and
continuation established during reverse engineering. These contracts coexist
with the renderer and framerate patch manifests and are checked fail-closed in
the target process before any widescreen hook is enabled.

## Verification Boundary

Static tests, models, mocks, fake devices, synthetic executable memory, copied
constants, and test-only callback wrappers are not oracles for game behavior,
native hook integration, renderer behavior, visual placement, or runtime
acceptance. No widescreen test may encode this design and then report that the
game follows it. Such tests were removed rather than retained as workflow
ceremony.

The only permitted exception is a narrowly scoped property formally derivable
from an authoritative, independently sourced contract. Even then, the claim is
limited to that property and cannot be promoted into evidence that the target
process executed the hook or displayed the intended result.

Post-build verification for this implementation is intentionally limited to:

1. complete `msvc32-debug` and `msvc32-release` compilation and linkage;
2. artifact presence/metadata inspection;
3. `git diff --check`; and
4. exact final worktree-change accounting.

Those checks establish only that the edited source compiles, links, and has a
reviewable diff. The executable byte guards are enforced transactionally when
the supported target process launches; post-build IDA invocation is neither
required nor accepted as runtime proof.

## Runtime Acceptance

Actual acceptance remains an operator-run game session with a deliberately
deployed build. It requires:

- disabled-feature baseline using native 720 x 1280 behavior;
- enabled 720 x 1280 window with pixel-identical placement;
- enabled 1137 x 1280 and 1920 x 1280 windows on an unrotated desktop;
- UI elements measuring the same 720 x 1280 pixel bounds in every mode;
- perspective geometry, not stretched pixels, occupying additional regions;
- `_clip.dat` remains installed while widened geometry is selected through the
  native live-frustum branch;
- correct Player 1 placement in the current one-player scope; multiplayer
  participant capacity is not inferred from the native judgement lanes;
- entry-0 and entry-1 CHAIN labels and counter digits follow their assigned
  sides without stretching; numeric glow and hundred-number drawing are
  checked separately, and hundred-chain rectangular lines remain centered;
- Player 1's fixed-position judgment text follows the primary combo cluster,
  with track-position grade effects and opponent feedback unchanged;
- all note-tutorial variants and their companion effect appear beside the
  local player's feedback cluster, while non-tutorial gameplay effects remain
  centered;
- chart objects, hit effects, player markers, stage color, and widened
  perspective geometry retain their intended positions while combo,
  judgement, and tutorial placement changes;
- Test Mode renders as an unstretched 720 x 1280 form centered within each
  configured wide window, with blank side regions and no window resize;
- menu, song-select, gameplay, result, and attract transitions without stale
  or one-frame mistargeted content;
- at least three consecutive tunes across several stage themes;
- minimize/restore and repeated device-loss recovery without leaks or flashes;
- edge-resizing and maximize unavailable in V1;
- 120/240-FPS GPU timing, copy count, and video-memory measurements showing no
  unacceptable regression; and
- startup/lifecycle summaries without render-loop log spam.

The supplied 1137 x 1280 comparison is evidence that wider stage geometry can
exist; it is not acceptance of UI isolation, lifecycle, or performance.

## Authoritative V1 Contract

V1 is a fixed-size decorated window on an unrotated Windows desktop. Its
client, real backbuffer, and wide scene use the same upright configured size.
Verified perspective passes use that complete scene. Common 2D tasks render
through the centered 720 x 1280 native texture; the direct Test Mode root-form
render owns a standalone compositor frame around that same native space.
Gameplay HUD/effects render at native logical scale on the wide scene through
a centered 720 x 1280 viewport. Only twenty identified top-bar calls use the
configured bar placement. Separate CHAIN label, ordinary digit, numeric glow,
and hundred-number calls use the existing per-entry side mapping, restoring
the actual enclosing state after each call. The expanding rectangle remains
centered. The two persistent network-status clips retain their existing
gameplay-only placement policy.

Primary fixed-position judgment text slots 12,15,18,24,27,30 decimal and the
identified tutorial slots use the right native viewport at actual drawing.
Managed roots carry ownership through allocation to packet submission; direct
roots use a matching immediate scope. Track-position grade effects 93–97,
other group-6 roots, introductions, staff rolls, and result announcements
receive no placement override. The operator confirmed GREAT text placement
and disappearance of the right-side flash. Temporary diagnostics are removed;
the profile retains 83 permanent hooks.

The final scene copy is one-to-one and unrotated. The feature never asks the player to
rotate the display and exposes no rotation or fullscreen setting. While
enabled, authored `_clip.dat` bounds are always bypassed in favor of the game's
existing live-frustum path; this behavior is not configurable.

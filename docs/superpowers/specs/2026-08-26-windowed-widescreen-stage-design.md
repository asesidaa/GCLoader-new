# Windowed Widescreen Stage Design

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
- Keep authored HUD, menu, chart-overlay, and gameplay-effect presentation
  exactly 720 x 1280 pixels and center it in the output.
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
- Relayout, anchoring, scaling, or replacement of authored 2D assets.
- Widening orthographic chart, HUD, menu, or gameplay-effect passes. The
  verified stage-background color quad is explicitly stage-owned and is not
  part of this native UI restriction.
- Supporting multisampled scene rendering in the first release.
- Forcing all stage parts visible when live frustum culling rejects them.
- Editing or deleting `_clip.dat` or `data/system.cfg`.
- Supporting unverified executables through signatures or scanning.
- Deployment to `H:\gc` or claiming in-game acceptance from static checks.

## Coordinate Spaces

The design uses two upright spaces.

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
uniformly and preserves destination-dependent blending.

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
        |       wide scene, native canvas, copy state
        |
        +-> RenderSpacePolicy <---------- PassClassifier
        |       Physical3D / Native2D / Compositor
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
    Compositor,
};
```

In `Physical3D`, screen and current-target queries report `OutputSize` and the
viewport covers the wide scene. In `Native2D`, they report 720 x 1280 and
viewport reset means the full native canvas. `Compositor` is loader-owned and
cannot invoke game rendering.

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
frame receive the wide aspect. During an active `Native2D` segment,
render-space-aware target getters temporarily return 720 x 1280; during
`Physical3D` they return the output size. Publishing both pairs as physical
produces the observed 270-pixel-wide UI strip. Publishing both pairs as native
fixes the UI but leaves a portrait perspective matrix that D3D stretches
across the wide viewport.

Gameplay HUD projection is one verified exception to screen ownership.
`0x0063F9E0` constructs the cached perspective and HUD matrices before an
active compositor frame. Its call at `0x0063FDBA` supplies target width and
height to orthographic builder `0x005DF170`, stores the result at owner
`+0x110`, and `0x00648D40` later binds that matrix as `D3DTS_PROJECTION` while
rendering the native canvas. The guarded mid-hook at RVA `0x0023FDBA`
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
| `0x00648D40` | `Native2D` | Orthographic gameplay effects, chart visuals, and HUD. |

`CCommon3DTask` at priority 700 uses `Physical3D`.
`CCommon2DTask` at priority 800 uses `Native2D`.

Every target transition requires proof that no native batch remains pending.
Known flush points are used where available. Development diagnostics assert
the observed pending-buffer state at each transition. A transition with a
pending batch is rejected rather than submitting deferred geometry to the
wrong target.

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

1. closes an outstanding native segment if necessary;
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
Approved orthographic passes run against the true 720 x 1280 canvas. The
gameplay HUD matrix described above is also constructed explicitly with 720 x
1280 extents because its native owner reads the persistent physical target
pair before the compositor frame begins.

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

The implementation plan must re-read the current IDB, convert each final
virtual address to a named RVA, record exact expected bytes and continuations,
and prove coexistence with current renderer and framerate patches before source
implementation begins.

## Tests

Every automated test protects an independently derived invariant.

### ResolutionModel tests

- 720 x 1280 produces a zero-offset 720 x 1280 native rectangle.
- 1137 x 1280 produces 208/209 horizontal side distribution.
- 1920 x 1280 produces 600-pixel side regions.
- 3840 x 1280 remains valid and centers the native canvas.
- client-to-native mapping round-trips boundary and interior coordinates.
- invalid width, non-1280 height, signed-range, and area overflow are rejected.

The pure model covers invalid dimensions and overflow. Startup
capability tests separately cover texture-cap excess, unsupported render-target
formats, multisampling, and the no-fitting-monitor case; they do not introduce
environment queries into `ResolutionModel`.

### Render state-machine tests

Using injected device actions and owned fake COM references:

- frame begin binds wide scene before native clear/begin;
- contiguous equal-space tasks do not copy unnecessarily;
- `Physical3D -> Native2D` copies scene center before native rendering;
- `Native2D -> Physical3D` copies completed native content back once;
- frame end closes native space, copies the scene once, then invokes native
  end-frame handling;
- state restore is followed by the destination target's correct viewport and
  scissor;
- failed bind, surface acquisition, state capture, or draw publishes no
  partial state and selects the fatal boundary;
- lost/reset releases every default-pool reference before reset and recreates
  exactly once; and
- non-render-thread transition is rejected.

### Policy and patch tests

- verified task identities and gameplay subpasses select intended space;
- unknown identities select native space and deduplicate diagnostics;
- enabled widescreen always redirects the guarded clip gate to the native
  live-frustum continuation;
- logical resolution initialization and reset preserve native screen
  dimensions and physical target dimensions as separate persistent domains;
- the physical target pair supplies wide aspect while the screen/UI pair stays
  native, with no redundant projection-scale hooks;
- screen/current-target integer and float getters agree in each scope;
- native config override forces windowed fixed-size settings only when enabled;
- all native contracts preflight before installation; and
- failure at each hook creation resets earlier candidates and publishes no
  runtime owner.

Tests do not grep source, duplicate executable byte tables as an oracle, patch
a live process, or claim visual correctness.

## Static Verification

Implementation completion requires:

1. focused Debug and Release builds/tests for the feature;
2. complete `msvc32-debug` and `msvc32-release` builds and CTest suites;
3. fresh daemon-backed IDA reads of every final RVA, expected bytes,
   continuation, config seam, and pass boundary;
4. artifact inspection confirming configuration strings and guarded contracts
   exist only in the game-process runtime;
5. `git diff --check`; and
6. exact final worktree-change accounting.

These establish source, build, and supported-binary contracts only.

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
- correct one-player and two-player layouts;
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
Verified perspective passes use that complete scene. Every approved
orthographic pass renders through an exact centered 720 x 1280 native canvas.
The final scene copy is one-to-one and unrotated. The feature never asks the
player to rotate the display and exposes no rotation or fullscreen setting.
While enabled, authored `_clip.dat` bounds are always bypassed in favor of the
game's existing live-frustum path; this behavior is not configurable.

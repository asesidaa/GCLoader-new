# Stage background projection and selected UI draw follow-up

Status: narrowed implementation built for the current target; operator visual
acceptance is pending. Porting to 2.06 is deferred until that acceptance.
No deployment, commits, or process lifecycle changes were performed.

## Scope and evidence

The operator supplied three screenshot pairs: visualizer bars in Play merrily,
hundred-chain rectangles in the same song, and the opening red gradient in
Magical Panic Adventure. They show stage effects stretched horizontally and
rectangle sides clipped. The frames differ, so they are not a pixel-for-pixel
comparison.

The report calls the current target 4.74. Existing internal names remain
`groove_coaster_471` and `H:/gc/game471.exe.i64`. Direct IDA-CLI analysis used
that database, input SHA-256:
`FEAD3BD4D0E0985F101965EDC417DD2B96522F8716FF789D84618FEB0D7A2522`.

Repeatable IDA requests are under `.codex-tmp/widescreen-background-20260906`.
The short-lived client writes outputs under
`.codex-tmp/widescreen-diagnostics-20260906`. Relevant requests include
`native_passes`, `native_background_draws`, `native_visualizer_and_spans`,
`native_projection_consumers`, `narrow_mixed_pass`, `narrow_ui_consumers`,
`narrow_centered_content`, `narrow_staff_and_sites`, `narrow_text_pipeline`, and
`narrow_boundaries`. Analysis artifact hashes are not used.

## Root causes

### The cached projection is shared

VA 63FDBA constructs CTune+110 from target dimensions. The previous loader hook
changed the builder arguments to 720 by 1280. That matrix also serves stage
content:

- 6449F0 binds it at 644A40 before the animated color background (641DE0) and
  visualizer dispatcher (6448A0).
- 641DE0 builds a four-corner color quad, or four quads using nine authored
  colors, from physical target width/height. It submits through 5796F0/57A9B0.
- Visualizers 1,2,4,5,6,7 derive their centers from target width/height.
  Visualizer 3 copies CTune+110 into parameters used by 65EDD0/5CA8A0.
- 648D40 binds the same projection at 648E04. This is a mixed effects pass,
  not an exclusive HUD owner.

With output-width vertices/viewport but a 720-wide projection, the horizontal
mapping becomes `x_pixel = x_world * output_width / 720`. This stretches the
background and visualizers and clips their right side.

### The whole mixed pass was restricted to the HUD viewport

The previous `gameplay_hud` classification applied a centered 720-wide viewport
and native dimension getters to all of 648D40. Its contents include bar and
counter draws, effect groups, stage introductions, timed text, rectangle lines,
and the full-output transition fade at 64A57D. Selected viewport hooks alone
could not undo this enclosing policy.

The rectangle follows the enlarged hundred-chain number in 5E3EC0. Its width
query is 5E47A9 and height query is 5E47C6. Three rectangles are constructed
around `(width/2,height/2)`, using horizontal radius
`height/2 * 1.75 * expansion` and vertical radius `height/2 * expansion`.
Submissions at 5E4ADD/5E4B05/5E4B24 call 579CE0 directly. A centered 720-wide
viewport clips their vertical sides once the radius exceeds 360.

## Implemented ownership policy

The current target leaves both the cached CTune matrix and the entire mixed
pass at physical output dimensions. It has no hook at the projection builder
23FDBA, the mixed-pass projection-pointer push 248DEC, or either rectangle
boundary. The earlier uncommitted approach using a private projection for the
whole mixed pass plus a rectangle exception was replaced before deployment.

Only explicit UI draws enter a native projection/viewport scope:

| Content | Selection boundary | Placement |
| --- | --- | --- |
| Top bar, gauge, score, tune count, player/status panels, bar names | Existing twenty draw pairs | Configured left/center/right |
| CHAIN label, digits, glow, enlarged hundred-chain number | Existing four separate pairs in 5E3EC0 | Entry 0 right, entry 1 left, otherwise center |
| Primary fixed judgment text | CTune slots 12,15,18,24,27,30 | Right |
| Note tutorials | CTune slots B2,B3,B4,B5,B6,B9,BA,BB,C0 (hex) | Right |
| Stage-start title image | 64A295 call to 5E16A0 | Center |
| Stage-start song/player details and associated icons | 64A2AB call to 5E24D0 | Center |
| Authored timed text, including credits using this renderer | 64A2F8 call to 5C6980 | Center |

The exact network icon children `imc_ico_l` and `imc_ico_n` use a viewport
scope without replacing Flash's own projection. Their follow-up state-lifetime
correction is documented in [the network icon analysis](widescreen-network-status-icons.md#2026-09-06-physical-pass-texture-state-regression).

The title/player routines reach 63F080/63EDB0: they have fixed native positions
and explicitly calculate animation strips against width 720. Timed text reaches
5C7160/5C6F90, which similarly uses an authored 720-wide reveal strip and direct
textured quads. These are specific UI consumers, not mixed stage-effect passes.

The effect-root selection is unchanged. Managed roots carry ownership into
allocated packets. At 5F10C4, selected packets draw through 5F0600: the packet
binds its world matrix but does not replace projection. Direct roots are scoped
around their existing draw. Recursive children retain their root's ownership;
packet ordering is unchanged. Track-position grade effects in slots 93..97
remain unselected.

For each selected draw, the compositor flushes pending general batches with
pipeline preservation, captures viewport/scissor/depth states, and saves the
actual D3D projection. It multiplies m11 by output_width/720 and m22 by
output_height/1280, preserving native axis signs, origin and depth terms.
The scope reports native logical dimensions and applies the selected viewport.
After submission it flushes selected general batches and restores the saved
projection and viewport/scissor/depth states. Native world, texture, blend and
shader changes retain their normal lifetime. The shared CTune matrix is never
written.

All unselected draws inherit the native full-output state. In particular:

- Background gradients, visualizers, rectangle lines and transition fades need
  no compensating hooks or special restoration paths.
- Announcement effect roots 0..6 calculate their positions from target width/2
  and height/2 in 648D40, so they remain centered with full-output projection.
- Selected draw state ends before any subsequent unselected packet or geometry.

## Versioned contracts

| New current-target contract | RVA | Protected bytes |
| --- | --- | --- |
| stage_title_draw_begin | 24A295 | E8 06 74 F9 FF |
| stage_title_draw_end | 24A29A | 8B 95 54 FE FF FF |
| stage_players_draw_begin | 24A2AB | E8 20 82 F9 FF |
| stage_players_draw_end | 24A2B0 | C7 45 FC FF FF FF FF |
| timed_text_draw_begin | 24A2F8 | E8 83 C6 F7 FF |
| timed_text_draw_end | 24A2FD | D9 05 80 C2 6F 00 |

All six spans match IDA bytes and contain complete instructions with no interior
branch targets. 64A2FD is also the destination of 64A2E0's skip-text branch;
the exit callback accepts an inactive scope on that path. The spans do not
overlap the existing widescreen or other declared feature sites.

The current profile has 91 byte contracts, nine pointer contracts, 88 hooks
and 100 preflight operations. A profile-owned `selected_hud_draws_only` policy
enables the new behavior. The 2.06 profile defaults to false and retains its
86 byte contracts, nine pointer contracts, 83 hooks, original matrix-builder
hook and mixed-pass policy. Its byte/pointer tables, native layout, base ABI and
install order are unchanged. Immutable profile spans permit differing counts.

## Verification and next run

Both complete `msvc32-debug` and `msvc32-release` preset builds passed with no
compiler/linker errors or warnings. CLion error inspections of the modified
implementation files passed. `git diff --check` passed. Native runtime-patch
policy applies: no synthetic tests, fixtures or mock runs were added or run.
Static evidence establishes hook contracts and compilation, not visual success.

Release DLL: `build-msvc32-release/dist/iDmacDrv32.dll`.
Matching PDB: `build-msvc32-release/src/iDmacDrv32.pdb`.
SHA-256 after the network-icon viewport-scope correction:
`23BA48D09A09C81985DA9E146526E92458B985F17F769D3ABC25AA3A884A0986`.
Both complete preset builds passed again, and CLion found no errors in
`NetworkStatusHooks.cpp`.

The operator deployed the initial narrowed build
`51E7DBB0968A4B391F389B0C6CA2DA0D6F14CCC08D85AB1C1220B6C3CFB7F15B`
(live DLL hash verified) and reported that the effects now looked fine.
That run exposed a separate ONLINE graphic regression: a solid white block
only during gameplay, with menus correct. Native analysis found that the
network wrapper restored all D3D state across a child while the Flash renderer
retained its updated binding caches. The follow-up uses the existing selected
viewport scope without projection replacement, preserving those bindings.
The follow-up DLL has been built but has not been deployed by this task.

The operator run must check Play merrily's visualizer bars and rectangle sides,
Magical Panic Adventure's opening red-to-black gradient, and the full-stage
start/end fades. Also check configured top-bar placement, GREAT/GOOD/MISS and
CHAIN placement, tutorials, centered stage-start title/player content, finish
announcements and a song with credits/timed text.

No temporary logging or environment switch was needed: native ownership and
submission boundaries are established. Existing capped network correction logs
identify the new viewport scope. Existing unmatched-scope/pending-packet fatal
checks remain. On 2026-09-06 the operator reported that the 4.74 issues now
appeared fixed and authorized committing this change, then porting to 2.06.
This is operator acceptance of the reported regressions, not evidence that
every song and configuration in the broader checklist was exercised.

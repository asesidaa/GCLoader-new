# Widescreen Network Status Icon Ownership

This record freezes the IDA results already collected for the upper-left
NESYS/local-network status icons. Do not rerun the exploratory scripts merely
to rediscover these addresses. Runtime acceptance remains a later operator-run
game check; this document records native ownership and rejected patch seams.

## Scope

The visual defect is limited to the two status icons displayed from boot
through menus and gameplay:

- `CPanel_NetOut`: NESYS/outbound status;
- `CPanel_NetIn`: local/inbound status.

They are not part of the combo counter. The ordinary chain number and its
celebration layer have separate gameplay ownership and must remain untouched by
the network-icon correction. Because both status panels exist before gameplay,
class identity alone is insufficient: any widescreen correction must also be
gated by the established gameplay-frame state.

## Frozen binary identities

Preferred image base is `0x00400000`; implementation addresses are recorded as
both VA and RVA where useful.

| Native owner | VA | RVA | Evidence |
| --- | ---: | ---: | --- |
| `CCommon2DTask::Render` | `0x005F5670` | `0x001F5670` | Common 2D render owner. |
| Common 2D construction | `0x005F6D50` | `0x001F6D50` | Establishes the panel-object slots consumed by `Render`. |
| Panel base method A | `0x005F48B0` | `0x001F48B0` | Shared by both derived status panels. |
| Panel base method B | `0x005F48D0` | `0x001F48D0` | Shared by both derived status panels. |
| Panel base initialize | `0x005F4930` | `0x001F4930` | Shared panel initialization. |
| Panel base construct | `0x005F4A40` | `0x001F4A40` | Shared panel construction. |
| Panel movie-manager access | `0x005F4BD0` | `0x001F4BD0` | Shared movie ownership path. |
| `CPanel_NetOut` construct | `0x00433D40` | `0x00033D40` | NESYS panel construction. |
| `CPanel_NetOut` update | `0x00433E00` | `0x00033E00` | NESYS panel update. |
| `CPanel_NetOut` initialize | `0x00433ED0` | `0x00033ED0` | Resolves the NESYS status clip/state. |
| `CPanel_NetIn` construct | `0x00433F90` | `0x00033F90` | Local-network panel construction. |
| `CPanel_NetIn` update | `0x00434010` | `0x00034010` | Local-network panel update. |
| `CPanel_NetIn` initialize | `0x00434090` | `0x00034090` | Resolves the local status clip/state. |
| Shared primary destructor | `0x00435780` | `0x00035780` | Used by both derived panels. |
| Shared secondary destructor thunk | `0x00435D30` | `0x00035D30` | Used by both secondary vtables. |

### Exact vtable identities

`CPanel_NetOut` primary vtable is at VA `0x006F9FC4`:

| Offset | Target |
| ---: | ---: |
| `+0x00` | `0x00435780` |
| `+0x04` | `0x00433D40` |
| `+0x08` | `0x00433E00` |
| `+0x0C` | `0x0040C9B0` |
| `+0x10` | `0x00433ED0` |
| `+0x14` | `0x005F48D0` |
| `+0x18` | `0x005F48B0` |

Its secondary vtable begins at VA `0x006F9FE4`; its first two entries are
`0x00435D30` and `0x0042B090`.

`CPanel_NetIn` primary vtable is at VA `0x006F9FF0`:

| Offset | Target |
| ---: | ---: |
| `+0x00` | `0x00435780` |
| `+0x04` | `0x00433F90` |
| `+0x08` | `0x00434010` |
| `+0x0C` | `0x0040C9B0` |
| `+0x10` | `0x00434090` |
| `+0x14` | `0x005F48D0` |
| `+0x18` | `0x005F48B0` |

Its secondary vtable begins at VA `0x006FA010`; its first two entries are
`0x00435D30` and `0x0042B090`.

These vtable addresses are exact object-type witnesses. Do not infer status
ownership from every object using the shared `0x005F48B0` or `0x005F48D0`
methods.

## Common-2D dispatch ownership

`CCommon2DTask::Render` submits the `common.rvb` root through
`0x005F47D0`, then invokes virtual slot `+0x0C` for the panel pointers stored at
owner offsets:

```text
+0x80  +0x84  +0x88  +0x8C  +0x90
+0x94  +0x98  +0x9C  +0xA0
```

The two network panels are members of this per-panel dispatch set. The render
function's vtable witness is VA `0x006F9B0C -> 0x005F5670`. The common movie
root is stored at task offset `+0x54`, but that root is broader than the two
network panels and is not a valid correction scope.

The status clips are resolved under `imc_head` and use the network-specific
children/states (`imc_ico_n`, `imc_ico_l`, `jf_nesys_on/off`, and
`jf_local_on/off`). These names identify the two persistent status components;
they do not make the rest of `common.rvb` part of this fix.

## Related movie/transform boundaries

The completed trace set established these downstream boundaries for resolving
an object-specific transform seam:

| Boundary | VA | RVA |
| --- | ---: | ---: |
| Root goto/play label | `0x004DAF20` | `0x000DAF20` |
| Find instance by path | `0x004DA550` | `0x000DA550` |
| Draw traversal visit | `0x004CE270` | `0x000CE270` |
| Persistent-root visit | `0x004E0D10` | `0x000E0D10` |
| Status-child visit | `0x004E0CD0` | `0x000E0CD0` |
| Movie-clip draw visit | `0x004CEED0` | `0x000CEED0` |
| Draw-traverse constructor | `0x004D0450` | `0x000D0450` |
| Transform getter | `0x004D3420` | `0x000D3420` |
| Movie-clip child enumerator | `0x004DC770` | `0x000DC770` |
| Shape-instance visit | `0x004D7C50` | `0x000D7C50` |
| Shape draw visitor | `0x004CCC20` | `0x000CCC20` |

Associated vtable witnesses are persistent-root VA `0x006BDF5C`, status-child
VA `0x006BE0CC`, and visitor VA `0x006BB74C`. These are analysis anchors for an
exact status-object correction; they are not authorization to hook every movie
or transform globally.

The later leaf-contract query established the complete synchronous subtree
boundary. `MovieClipInstance::Accept` dispatches visitor slot `+0x3C` to
`0x004CEED0`. That visitor calls the instance's virtual `+0x128` child
enumerator, `0x004DC770`, which walks the child list and calls virtual `+0x14`
(`Accept`) for each visible child before returning. `ShapeInstance::Accept` is
VA `0x004D7C50` through vtable VA `0x006BD3DC`; it dispatches visitor slot
`+0x40` to shape visitor `0x004CCC20`. Consequently, the exact named movie-clip
wrapper already encloses the terminal shape visits. Moving the same viewport
scope to a leaf hook would add a global vtable hook without changing the
effective lifetime.

## Rejected patch paths

### Animation-manager lifecycle forcing

Do not call or hook the animation-manager getter at VA `0x0063B9B0` or its
execute path at VA `0x0063B9F0` to force a draw boundary. The attempted adapter
finalized, waited on, executed, or reset manager work while buffers could still
be checked out. The native finalizer can return `0x10016`; ignoring that result
and draining an incomplete queue caused assertions before gameplay. This route
is structurally invalid for this fix and must not be reconstructed.

### Generic `common.rvb` traversal wrapper

Capturing the task's `+0x54` root and wrapping persistent-root visit RVA
`0x000E0D10` in native 2D avoided the manager-lifecycle crash, but it did not
correct the two status icons. It is both broader than the requested ownership
and empirically ineffective. Remove it rather than treating its lack of crash
as visual success.

### Generic common-2D or shared-panel behavior

Do not change the whole common 2D task, every object using the shared panel
methods, or the global movie/transform pipeline. The icons are present from
boot onward, while the requested correction applies only during widescreen
gameplay. Any accepted implementation must require all of:

1. widescreen feature active;
2. gameplay-frame state active;
3. exact `CPanel_NetOut` or `CPanel_NetIn` ownership, or exact child identity
   captured from those owners;
4. fail-open behavior when optional identity/transform state is unavailable;
5. no animation-manager finalize/wait/execute/reset operation.

## Preserved analysis corpus

The exploratory IDAPython programs are retained under `.codex-tmp/`. The
network-status conclusion above consolidates the results of:

```text
ida_common_status_clips.py
ida_common_status_components.py
ida_status_panel_runtime_seams.py
ida_status_panel_draw_methods.py
ida_status_label_resolution.py
ida_status_runtime_contracts.py
ida_status_visit_dispatch.py
ida_status_matrix_window.py
ida_status_transform_getter.py
ida_widescreen_chain_icons_ownership.py
ida_widescreen_chain_icons_focus.py
ida_widescreen_chain_icons_focus2.py
ida_widescreen_chain_end_and_icons_owner.py
ida_widescreen_icons_owner_trace.py
ida_widescreen_icons_terminal_draws.py
ida_widescreen_common2d_panel_vtables.py
ida_widescreen_common2d_render_flow.py
ida_widescreen_anim_manager_lifecycle.py
ida_widescreen_anim_worker_execution.py
ida_widescreen_command_system_barrier.py
ida_status_movieclip_accept_contract.py
ida_status_leaf_draw_contract.py
ida_status_shape_draw_contract.py
ida_status_traversal_order_contract.py
```

Those scripts are retained for provenance and future investigation of a
different question. They are not a required build or post-build verification
step, and the addresses in this record should be used instead of repeatedly
running the same traces.

## Superseded panel diagnostic contract

The 2026-09-04 diagnostic candidate temporarily instrumented the two exact
panel `+0x0C` slots, stage markers, render space, and native batch counts. That
instrumentation answered the ownership question below and is not part of the
corrective image. In particular, there is no reason to repeat the panel run or
retain its once-per-frame logging.

### 2026-09-04 runtime result

The instrumented run at 20:19-20:20 resolved the `+0x0C` question:

- both exact vtable slots were replaced successfully and read back as the
  detour address;
- both panels were called once per rendered frame, with stable objects
  `CPanel_NetOut=0x1A7B6300` and `CPanel_NetIn=0x1A7B6480` in that process;
- after gameplay began, every captured panel call occurred at the start of the
  next frame before `GameplayStageBackgroundMid`, so the frame-local gameplay
  latch was false;
- the active space was consistently `RenderSpace::native_2d` (`1`), with an
  active compositor frame and centered placement;
- native batch counts were `0,0,0,0` both immediately before and immediately
  after each original virtual call.

Therefore virtual slot `+0x0C` is definitively not the icon geometry submission
boundary. Extending the gameplay latch or changing the viewport around this
method would still be ineffective. Remove this hook from the corrective design;
the patch must operate at the downstream status-clip visit/transform boundary.

## Exact clip-level replacement contract

A short-lived scripted IDA query established the callable contract for
`MovieClipInstance::Accept` without holding the shared IDA daemon:

- function VA `0x004E0CD0`, RVA `0x000E0CD0`, end VA `0x004E0CE1`;
- IDA type `int __thiscall(void *this, int visitor)`;
- `MovieClipInstance` vtable VA `0x006BE0CC`;
- `Accept` is vtable index 5, offset `+0x14`, at slot VA `0x006BE0E0`
  (RVA `0x002BE0E0`);
- the supported executable stores target `0x004E0CD0` in that slot, encoded as
  little-endian bytes `D0 0C 4E 00`.

The wrapper moves the movie-clip `this` pointer to the visitor call's stack
argument, loads the visitor into `ecx`, and tail-jumps through visitor vtable
offset `+0x3C`. The replacement therefore hooks the one `MovieClipInstance`
virtual slot and preserves the original call as
`int __thiscall(MovieClipInstance *, visitor *)`.

The corrective detour is deliberately narrower than the discarded panel and
root wrappers:

1. require active widescreen callbacks, the current-frame gameplay latch, an
   active compositor frame, and the exact draw-visitor vtable
   `0x006BB74C` (RVA `0x002BB74C`);
2. match the zero-seeded 33x name hash and then the exact runtime string for
   only `imc_ico_n` or `imc_ico_l` (instance offsets `+0x140` and `+0x120`);
3. correct only `RenderSpace::gameplay_hud` or `RenderSpace::physical_3d`, using
   a physically centered native-HUD viewport and a scoped logical
   `(0,0,720,1280)` dimension/viewport view while that subtree is visited;
4. call the original unchanged in every other scene, space, or identity case;
5. fail open if optional compositor state cannot be changed, without an
   assertion, runtime-fatal publication, or animation-manager lifecycle call.

Logging is bounded to the first success or first failure for each exact clip;
there is no frame-level tracing in the replacement. Debug and Release builds
compiled and linked on 2026-09-04. This is static/build evidence only, not
visual runtime acceptance.

### 2026-09-04 first clip-level runtime result

The first deployed clip-level run reached both exact children in
`RenderSpace::gameplay_hud` (`2`) during the same gameplay frame. Both reported
the former `gameplay-hud-left` action. The NESYS icon became unstretched but
remained at the physical top-left, while the local-LAN icon remained stretched.

That run proved that `left` was not the desired destination, but it did not
prove why the two clips differed. The next candidate incorrectly promoted an
unverified viewport-reset explanation into the implementation.

### 2026-09-04 centered-reset runtime result

The 21:43-21:44 run again reached both exact clips in
`RenderSpace::gameplay_hud`; both bounded records reported successful
`gameplay-hud-center` correction, with no correction failure or assertion in
the log. Visually, nothing moved and the NESYS icon regressed to stretched.
This disproves the forced game viewport-reset branch and shows that a successful
policy-state transition is not evidence that the physical D3D viewport was
actually rewritten.

The concrete stale-state path is in `NativeCanvasCompositor`: its ordinary
placement setter returns immediately when the requested viewport equals its
cached placement. Native game rendering can overwrite the D3D viewport without
updating that cache. The status correction therefore needs a narrow reapply
operation that always flushes pending native batches and writes the requested
hardware viewport, even when the cached placement already matches. The game-
facing scoped viewport remains logical `(0,0,720,1280)`; the compositor alone
adds the physical centered origin. The exact subtree is drained before the
previous placement is forcibly reapplied. Runtime acceptance of that replacement
remains pending.

### 2026-09-05 terminal-shape and pipeline result

The bounded terminal-shape run resolved the remaining NESYS/local divergence at
the exact named subtrees:

- both clips reached one terminal shape definition through the expected draw
  visitor while the wide-scene target was active;
- the composed matrices were the authored transforms: local LAN translation
  `(90,52)` and NESYS translation `(39,52)`, both with unit scale;
- at each terminal hook entry the hardware viewport/scissor was the centered
  native rectangle `(778,0,720,1280)` for the tested `2276x1280` output;
- while the local LAN shape call unwound, the hardware viewport/scissor changed
  to the full wide scene `(0,0,2276,1280)`; the NESYS shape call left the
  centered native viewport intact;
- the captured shape definitions were not submitted elsewhere during the
  observed gameplay frame.

This rules out an incorrect authored transform, the common root, and a second
visible submission. The local LAN graphic alone restores a full-width viewport
inside its terminal draw path, after the outer exact-clip wrapper has centered
the viewport. Because the logical projection remains 720 units wide, that
specific state change expands and displaces the local icon by
`output_width / 720`.

The corrective implementation therefore keeps the exact movie-clip and
gameplay gates and compensates only terminal shapes visited under
`imc_ico_l`. For output width `W`, it applies `s = 720 / W` to the matrix's X
basis and maps its translation as `x' = s * (x + native_left)`. The original
matrix is restored synchronously when the terminal visitor returns. The NESYS
matrix is never changed, non-gameplay instances never enter the scope, and an
unreadable or non-writable visitor matrix fails open. The temporary pipeline,
definition, and boundary diagnostics are not retained in the corrective image.

## Acceptance boundary

Compilation and guarded-site installation can establish only that the patch is
buildable and that the supported executable matched its static contracts. Only
an operator-run deployed game session can establish that both network icons are
unstretched and correctly placed during widescreen gameplay,
while menus and the existing combo/judgement/tutorial behavior remain
unchanged.

# GC 2.06 narrowed widescreen port

Date: 2026-09-06. Baseline: `634ce44`, the operator-confirmed 4.74 correction.
Status: operator confirmed the 2.06 fix works and authorized cleanup/commit.
Temporary separator traces and successful per-clip correction logs are removed.

## Native target and ownership

Direct IDA-CLI analysis used `H:\gc2_game\game_decrypted.exe.i64` and
its input `H:\gc2_game\game_decrypted.exe`. Both input identities agree:
SHA-256 `556722C899ED75F33F17900BCD94BBB07288789547A30A982B3FB6ABFF6FB61C`.
The image base is `0x00400000`. Addresses below are VAs unless marked RVA.
All batches used the real IDA backend and disconnected afterward.

The same shared-projection defect exists on 2.06:

- `60C510` builds `CTune+0x110` using target width and height at
  `60C8AE/60C8C3`, then calls the orthographic builder `5B7E80` at
  `60C8EA`. The removed hook formerly replaced those dimensions with 720/1280.
- Background pass `611AF0` binds `CTune+0x110` as projection and calls
  `60EEE0` for background geometry and `6119A0` for visualizers.
  Background geometry queries physical dimensions at `60F43D/60F445`;
  the color quad at `60FFF9` also uses target dimensions. Visualizer dispatch
  `611810` copies the same projection for case 3 before calling `62AFF0`.
- Mixed pass `615560` binds the same projection at `615624`. Its final fade
  queries target dimensions and submits a full-output quad at `616D62`.
- Counter renderer `5BD660` also owns the expanding rectangle. After the
  selected counter draws, it queries width/height at `5BDF49/5BDF66` and
  submits three line rectangles at `5BE27D/5BE2A5/5BE2C4`. They must remain
  on the physical viewport, outside the four selected counter scopes.
- Selected effect packet `5CD150` binds its world transform through
  `5658F0` and submits via `565680`; it inherits projection. Existing
  root/packet ownership and the `CTune+0x1D60` collection remain unchanged.

Both builds now use the existing selected-draw projection/viewport scope.
The 2.06 profile enables `selected_hud_draws_only`, removes its matrix-builder
hook at RVA `0x20C8EA`, and leaves the mixed pass physical. No rectangle,
background or visualizer exception is installed.

## Corrected title and top-bar selection

The previous 2.06 `bar_names` mapping followed call order instead of matching
callee ownership. Native order differs from 4.74:

| Content | 2.06 callee and caller | Native evidence | Placement |
| --- | --- | --- | --- |
| Stage-start title image | `5BB190` at `616A64` | Calls `60BD90(0, this+64, 30, 240, frame)`; the helper draws at x=40,y=512 with width=640,height=256 | Center |
| Stage-start player/details list | `5BB0C0` at `616A7A` | Calls `60BD90` for rows at y=799..926; authored reveal uses width 720 in `60BAE0` | Center |
| Top-bar PLAYER label and player details | `5BADB0` at `616A90` | Draws PLAYER at (249,38), name/details at (260,57)/(363,54); matches 4.74 `5E1FA0` | Configured side |
| Timed text | `59F530` at `616ADD` | Reaches `59FFF0 -> 59FC40 -> 59FA70`; reveal strips explicitly use width 720 | Center |

The old bar pair at `616A64/616A69` incorrectly selected the opening title.
That address pair now belongs to the centered title scope. The bar pair moves
to `616A90/616A95`. The player-list scope is separate from both.

## Guarded sites

| Contract | RVA | Protected bytes |
| --- | --- | --- |
| stage_title_draw_begin | `0x216A64` | `E8 27 47 FA FF` |
| stage_title_draw_end | `0x216A69` | `8B 85 4C FE FF FF` |
| stage_players_draw_begin | `0x216A7A` | `E8 41 46 FA FF` |
| stage_players_draw_end | `0x216A7F` | `8B 95 4C FE FF FF` |
| timed_text_draw_begin | `0x216ADD` | `E8 4E 8A F8 FF` |
| timed_text_draw_end | `0x216AE2` | `D9 05 5C 9C 6B 00` |
| bar_names_begin | `0x216A90` | `E8 1B 43 FA FF` |
| bar_names_end | `0x216A95` | `C7 45 FC FF FF FF FF` |

All eight spans contain complete instructions, with no interior entry points.
`616AE2` also receives the skip-text branch from `616AC5`; the shared
end callback already permits an inactive timed-text scope on that path.
The 2.06 profile contains 94 byte and nine pointer contracts, comprising
88 hooks and 15 read-only checks, including the separator identity below.
Both builds share callback ABI/order arrays;
their bytes, pointer values and native layouts remain separate. The 4.74
contracts, layout and resulting callback order are unchanged from `634ce44`.

## Network indicator pipeline

2.06 Flash uses the same binding cache contract. Shape visitor `4C1870`
reaches `GWDrawFunc` at primary vtable `681294`; mesh draw `4D9D50` and
immediate draw `4DB810` call `4D9620(0)`. Texture helper `4D8DE0` compares
desired texture at `+0x108` against remembered bound texture at `+0x120`
and skips a matching bind. Full D3D restoration across a clip would invalidate
that cache exactly as on 4.74.

Enabling the narrowed profile also selects the corrected network wrapper:
viewport/scissor/depth are scoped without replacing Flash projection or
restoring native texture/material state after the clip. Exact `imc_ico_l`
and `imc_ico_n` selection, gameplay gating and local-icon matrix compensation
remain unchanged. `5A2C50 -> 5A20E0` flushes nonempty general queues and changes
the vertex pipeline, so the existing pipeline-preserving flush is retained.
The existing capped correction messages identify `physical-viewport` and
`physical-viewport-local-matrix`; the separator follow-up uses the same logger.
The initial port added no environment switch or per-frame trace. The later
separator investigation below adds temporary bounded snapshots.

## 2.06 dotted header separator follow-up

The operator reported an additional dotted line below the player name and
above the song title. This belongs to the common Flash UI, separate from
`5BADB0` and its selected top-bar name/detail sprites.

Both actual 2.06 assets, `data/2d_boost/common.rvb` and `common_eng.rvb`, place
`imc_head` (`UNIQUE_167`) at `(360,16)`. Its unnamed depth-3 child is
`UNIQUE_150`, at `(0,64)` in the stable header frame. The child's
`jf_line_off` / `jf_line_on` timeline contains `UNIQUE_149`, which renders
`UNIQUE_148`: a 720 x 2 shape, bounds `(-360,-1)..(360,1)`, filled from
`Image45`. Its stable screen position is therefore centered on `(360,80)`.
The network icons, timer and header background are separate sibling clips.

IDA establishes the runtime identity and synchronous draw lifetime:

- `4D6470` constructs the MovieClip instance with definition pointer `+0x118`.
  Getter `4C6930` returns it; the definition name getter `4C6AD0` reads `+0x10`.
- `4D3740 -> 4D2770 -> 4D1930` places children. Instruction `4D19F5` stores
  the direct parent at child `+0x150`. This is distinct from the movie-root
  pointer at `+0x130`.
- The existing guarded slot `6806F0 -> 4D5FA0` dispatches the MovieClip draw
  visitor. Its child enumeration `4D1BF0` synchronously visits the subtree.

Selection requires the definition name `UNIQUE_150` and a direct parent named
`imc_head`, in addition to the existing gameplay-frame and draw-visitor gates.
Only the 2.06 profile supplies this symbol and field layout; the empty default
disables separator selection for 4.74. Missing or unrecognized identity leaves
the native draw alone. The existing clip viewport scope applies configured
top-bar placement without replacing Flash projection or restoring its native
texture/material bindings. No extra detour or whole-header scope is installed.

Three new read-only guards verify the field evidence before hook installation:

| Contract | RVA | Expected bytes |
| --- | --- | --- |
| movie_clip_definition_getter | `0xC6930` | `8B 81 18 01 00 00 C3` |
| movie_clip_parent_assignment | `0xD19F5` | `89 BE 50 01 00 00` |
| movie_definition_name_getter | `0xC6AD0` | `8B 41 10 C3` |

During diagnosis, the capped common-HUD correction log reported
`clip=header_separator` once per success/failure outcome. The initial viewport-only implementation reported
`action=physical-viewport`. The terminal-matrix follow-up below required
matrix correction and reported `action=physical-viewport-separator-matrix`.
After operator acceptance, successful correction messages were removed;
each selected clip retains at most one failure warning.

## Separator runtime rejection and bounded trace, 21:13 run

The operator reported that the separator still did not behave correctly.
`H:\gc2_game\loader-log.txt` is 23,316 bytes for this run. At line 73,
`21:13:36.783`, frame 9845, the selected object `0x1A9FD680` reports
`clip=header_separator space=0 action=physical-viewport`. Both network icons
also report success. The profile is 2.06, output `2276x1280`, placement `left`.
The deployed DLL and shared build DLL matched
`84B6A4615E0B49290D8969E927C5925C3AC40CD285993F316D0BD9007EF98FA0`
when inspected. This confirms identity matching and wrapper completion;
it does not confirm the hardware viewport at primitive submission.

The failed log is preserved as
`.codex-tmp/widescreen-206-20260906/separator-failed-2113-loader-log.txt`.
Fresh IDA inspection of `4C1870` and `4D9620` retains the terminal-shape and
native target-binding investigation. A downstream viewport reset, as previously
observed for the local-network icon, remains a hypothesis for this separator.

The diagnostic follow-up changes no placement logic. It reads the actual D3D
viewport, scissor and projection plus the visitor's composed matrix at clip
entry, terminal-shape entry/exit, clip exit and scope restoration. Only the
exact selected separator is sampled: visits 1, 2, 3, 121, 361, 721 and 1441,
with at most two terminal shapes per sample, for at most 49 trace records on
the gameplay render thread. Logging is temporary, uses the Info level, and
has no environment/configuration switch. Around ten seconds of gameplay at
the observed 240 FPS is sufficient to reach all samples.

Both complete presets and CLion diagnostics passed for the diagnostic build.
Diagnostic DLL SHA-256:
`C9B6AE9009206296E19599359098213AAD84BE35E534D1143CB1AEFA58933A33`.

## Separator terminal-matrix correction, 22:33 run

The latest operator log is 37,123 bytes, ending at 22:34:01. The deployed DLL
matches the diagnostic SHA-256 above. Startup selects 2.06, output
`2276x1280`, HUD placement `left`, and all 88 widescreen hooks.

All seven samples show the same transition. The first is at log lines 72-76,
frame 8813; the last is at lines 106-110, frame 10253:

| Phase | Hardware viewport / scissor | Composed matrix translation |
| --- | --- | --- |
| clip-enter | `0,0,720,1280` | `(360,16)` |
| shape-enter | `0,0,720,1280` | `(360,80)` |
| shape-leave | `0,0,2276,1280` | `(360,80)` |
| clip-leave / clip-restored | `0,0,2276,1280` | `(360,16)` |

The horizontal matrix scale stays 1 and the projection stays
`0.00277778,0.0015625,0,0`. The exact separator is selected, but native shape
submission overwrites its scoped viewport and scissor with full output.
IDA was rechecked through the existing daemon: slot `67DE18 -> 4C1870` and
the target-binding path in `4D9620` retain the previously established contract.
This matches the already corrected local-network icon's rendering behavior.

Reuse its terminal-shape matrix compensation for the exact selected 2.06
separator. With `s = 720 / output_width`, multiply the composed horizontal
matrix components by `s` and set translation x to
`s * (original_x + configured_hud_left)`. Restore the original matrix
immediately after the native shape visitor returns. At this run's dimensions
and left placement, the expected corrected x scale is about `0.316344` and
translation x about `113.884`, retaining y=80. These are expected inputs for
the next run, not an observed rendering result.

The shared helper is now named `ScopedCommonHudShapeMatrix`; selection is
explicitly limited to `imc_ico_l` and the profile-selected separator. The
4.74 profile still cannot select the separator. The correction adds no native
sites, changes no Flash projection/material restoration, and does not select
the whole header or any stage effect.

The confirmation build kept the same temporary sample count and 49-record
maximum. Shape phases reported `shape-corrected` before native submission and
`shape-restored` after the visitor matrix is restored. Correction failure
reports `shape-matrix-unavailable`; wrapper success now requires that the
matrix correction actually ran.

DLL/PDB artifacts must stay in the checked-in presets' standard output
directories: `build-msvc32-debug/dist` and `build-msvc32-release/dist`.
The operator rejected the earlier temporary DLL/PDB copy; `.codex-tmp` must
not be used for DLL build output or preserved DLL copies.
The two temporary DLL/PDB copies were removed. Both complete preset builds
passed for the matrix follow-up, CLion reported no source errors, and
`git diff --check` passed. Release output at 22:41:12:
`build-msvc32-release/dist/iDmacDrv32.dll`, SHA-256
`7946FBB97FCBD4F84CBB13B89F81218389090C86502A5883C6B7E4840C471BE8`.
The build log is `.codex-tmp/widescreen-206-20260906/separator-matrix-build.log`.
Visual confirmation was received in the following closeout.

## Operator acceptance and diagnostic cleanup

The operator reported "Now it works" and authorized cleanup and commit.
The deployed DLL matches the matrix-follow-up SHA-256 `7946FBB9...C471BE8`
recorded above. The 43,412-byte operator log ends at 22:46:43. At lines 82-83,
frame 15460, `shape-corrected` records x scale `0.316344`, translation
`113.884`, and y=80; `shape-restored` returns x scale to 1 and translation to
360 after native submission. Line 86 reports the separator matrix correction.
This records the actual corrected/restored matrix and the operator's visual
confirmation. It does not claim every song or placement configuration was run.

Cleanup removes the separator sampling state, all trace calls, and the
temporary D3D viewport/scissor/projection snapshot API. Successful common-HUD
correction logs are also removed. Normal profile startup status and at most
one failure warning per selected clip remain. No rendering selection, matrix
math, native guard, or hook placement changes during this cleanup.

Both complete Debug and Release preset builds passed after cleanup. CLion
reported no errors in the changed hook and device files. The final Release
DLL is `build-msvc32-release/dist/iDmacDrv32.dll`, SHA-256
`07A3B0C96EB6FF94A62CA0307151F7F0B2D01B61D9414663B9CA3CBBAFB87A73`.
Build evidence: `.codex-tmp/widescreen-206-20260906/separator-cleanup-build.log`.
This artifact differs from the operator-tested image only through the logging
cleanup in this follow-up; no post-cleanup gameplay run was claimed.

## Verification and operator run

- All 103 current 2.06 widescreen contracts match the IDB and executable bytes.
- Hook spans decode to complete instructions, with no interior branch entries.
- No widescreen mutation overlaps another widescreen or other feature site.
  This records the original narrowed-port audit. Subsequent concurrent
  song-unlock/compatibility work is separate; this diagnostic follow-up adds
  no native patch sites or mutations.
- Complete Debug and Release preset builds passed; CLion inspections of the
  changed implementation reported no errors. `git diff --check` passed.
- No synthetic tests, binary fixtures or mock runtime were added or run.

Repeatable IDA queries/results and build log are under
`.codex-tmp/widescreen-206-20260906/`: `consumers`, `selected_contract`,
`leaf_consumers`, `title_and_flash`, `final_boundaries_and_pipeline`,
`flash_texture`, and `profile_contracts`. The separator follow-up adds the
`dotted_line_*` native/asset queries and `dotted-line-build.log`.

Original separator build DLL SHA-256:
`906A0ECD00DC5400C3E8561D6865F8AD67774449FB0118B7D006B908AD45BA06`.
Subsequent runs use the standard build output. Earlier hashes identify the
historical diagnostic runs, not the final artifact after logging cleanup.

The operator accepted the reported fix and authorized committing this port.
No deployment or game process action was performed by the agent. Retain the
broader regression checklist for 2.06 background gradients, visualizers,
rectangle expansion, stage-start title/player list, configured top-bar details,
GREAT/GOOD/MISS and CHAIN placement, tutorials, end announcements/timed text,
the dotted separator following the top bar, and both network indicators.
Static/build results do not establish unobserved in-game outcomes.

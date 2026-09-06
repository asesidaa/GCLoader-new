# GC 2.06: patches added since the September 2 audit

Date: 2026-09-06. Source baseline: `86807302a17677abab9871f6d72b657c94ee67b5`.
Status: native audit completed; the resulting loader port is implemented and
compiled. Runtime acceptance is pending. See the
[implementation and validation record](gc206-implementation-2026-09-06.md).

## Result

The earlier portability conclusion still holds. The new AutoPlay feature and
the current widescreen placement behavior have viable 2.06 implementations.
AutoPlay needs a different save-suppression patch. Widescreen needs the new
draw boundaries and its own effect-collection offset. NESYS 2.86.1 retains the
existing ping-hook ABI with an older RVA and signature.

The refactor supplies the common detector, feature plans, approval barrier,
runtime image operations, and SafetyHook registry. It deliberately contains
only 4.71 and NESYS 2.97 profiles at the audit baseline. Adding the older profiles also requires
removing several remaining 4.71 assumptions from the framerate callbacks and
variable-sized feature manifests.

## Evidence boundary

Analysis used `ida-cli` from `H:\IDACLI` and IDA/Hex-Rays on:

- `H:\gc2_game\game_decrypted.exe.i64`;
- `H:\gc\game471.exe.i64`;
- `H:\gc2_game\NesysService.exe.i64`.

Every batch required `ida_available=true`. Clients disconnected after each
batch; no IDB edits or saves were requested. Loader source review used CLion
MCP after the user's instruction. During the audit stage, no loader source,
executable, configuration, deployment, or running game was changed. The later
implementation changes source and build artifacts only, as recorded above.

Repeatable IDA queries, decoded instructions, decompiles, and results are in
`H:\gc\artifacts\GCLoader\.codex-tmp\gc206-2026-09-06`.
The final byte comparison, `gc206-native-contracts.json`, uses file offsets
supplied by IDA: **93/93 spans agree with the actual 2.06 executable**. These
are 86 widescreen byte contracts, two newer vtable cells, and five AutoPlay
sites. The current 86 widescreen source patterns also agree with the 4.71 IDB.
Those counts describe static evidence, not execution tests.

The September 2 reports under `H:\gc\tmp\gc206_patch_analysis` remain the
baseline for the previously audited families. This pass does not claim to
repeat every old-family check. Their native meanings remain useful; old
installer/rollback prose is superseded by the completed September 5 refactor.

## Exact executable identities

| Image | SHA-256 | File size | Machine | Preferred base | SizeOfImage | PE timestamp |
| --- | --- | ---: | --- | --- | --- | --- |
| Game 2.06 | `556722C899ED75F33F17900BCD94BBB07288789547A30A982B3FB6ABFF6FB61C` | 3405312 | `0x014C` | `0x00400000` | `0x003E7000` | `0x565828C3` |
| NESYS 2.86.1 | `328C48B01F884E0B32B39E44936661B224A4D9E48C679BE3F8CA3AE74A9760A4` | 368640 | `0x014C` | `0x00400000` | `0x0005C000` | `0x5451056C` |

Hashes and PE identity fields were freshly read from the executable files.
The NESYS file's version resource reports `2.86.1`. No script or generated
artifact hashes are used as evidence.

## What changed since the earlier audit

| Change | Current scope | 2.06 disposition |
| --- | --- | --- |
| Native AutoPlay | Two getters, score-save containment, render marker | Portable with a new save-state branch patch |
| Widescreen selected HUD draws | Twenty bar pairs; four counter pairs | Present; old draw-owner RVAs and exact boundaries recovered |
| Widescreen deferred feedback | Root selection, packet allocation, packet submission | Present; shared ownership algorithm, old effect-collection offset |
| Widescreen Test Mode placement | Scope native Test Mode draw | Present at a different state-machine location |
| Widescreen network indicators | Two global class vtable slots | Present with the same callback conventions |
| Architecture cleanup | Profiles, common preflight and hook installation | Reuse; populate 2.06/2.86.1 and finish native ABI selection |

Current widescreen has 86 byte contracts and nine pointer contracts; the
September 2 report covered 30 and seven. These counts include read-only
contracts as well as writes/hooks.

## AutoPlay native contract

All addresses in the tables are RVAs from the loaded image base.

| Role | 4.71 RVA | 2.06 RVA | 2.06 original bytes | Proposed operation |
| --- | --- | --- | --- | --- |
| Native auto-play getter | `0x03CADA` | `0x030AFA` | `8A 80 A5 00 00 00` | `B0 01 90 90 90 90` |
| Complete IsMute descriptors | `0x03CAFA` | `0x030B1A` | `8A 80 A6 00 00 00` | `B0 01 90 90 90 90` |
| Score-save containment | `0x269951` | `0x1EF52A` | `74 0F` | `90 90`; take the native post-save path |
| Marker before render subscribers | `0x058BE9` | `0x049FB9` | `8D 44 24 08 50 E8 7D 03 00 00` | Same marker callback; protect the five-byte LEA/PUSH seam |
| Native debug text | `0x069650` | `0x05ABF0` | `55 8B EC 6A FF 68 88 53 63 00 64 A1 00 00 00 00` | Read-only function contract; same x86 cdecl varargs ABI |

### Judgement and CSV ownership

The old getters are functions at `0x030AF0` and `0x030B10`. They read
judgement-state bytes `+0xA5` and `+0xA6` respectively.

- `0x1AA690` first checks the auto-play getter, waits until the descriptor's
  authored target, and completes it with that target timestamp and grade 3.
  Its IsMute branch checks the second getter before choosing miss/completion.
  This older helper has no 4.71-style alternate grade-state branch to patch.
- `0x1AAF40` exits before physical free-input queries when auto play is true.
  The other native manual-handler callers are also guarded by this getter.
- `0x1AF100` is the actual old result CSV exporter. It checks the same getter
  **before `fopen_s`**. The gameplay state machine reaches this exporter with
  its generated timestamped CSV pathname. Its much smaller body must not be
  matched to the newer exporter using a function-size assumption.

No synthetic booster input or additional gameplay input interception is needed.
The existing marker strings and marker activation after successful installation
remain applicable.

### Save suppression is a semantic variant

2.06 has neither `data/expconfig.cfg` nor the newer `DoNotSaveCardData` key in
the analyzed image. Its `OutputStageInputData` key belongs to `data/system.cfg`.
Copying 4.71's parser patch would be incorrect.

The old `CNesysCommTask::ProcessSavePlayerData` constructor at `0x1EF450`
initializes state `+0x18` to zero and request pointers `+0x1C..+0x28` to null.
Its step function is `0x1EF4C0`, referenced by its class vtable at `0x2B71EC`.
At state zero:

```text
1EF51C  mov ecx,[ebp+8]
1EF51F  call 014570           ; card-presence predicate
1EF524  movzx ecx,al
1EF527  cmp ecx,1
1EF52A  jz 1EF53B             ; original: enter the card-save chain
1EF52C  mov edx,[ebp-4190h]
1EF532  mov dword ptr [edx+18h],0Dh
1EF539  jmp 1EF577
```

Replacing only `74 0F` with NOPs always reaches state 13, before any card-save
request is constructed. States 1–12 contain the ordinary card, card-detail,
B-data, changed-record and music-detail save work. States 13–16 retain the
native post-save completion work, including command 310 and the stage/status
operation 1605; state 17 completes the process normally.

This is the old counterpart of 4.71's no-save path, which enters its own
post-save state 17 and then retains completion handling. Do **not** force the
old function directly into terminal state 17 or short-circuit the whole
NESYS process. No extra mutable scheduler flag is needed.

The proposed 2.06 AutoPlay plan keeps the same five-operation shape: native
text target, marker hook, save suppression, IsMute getter, auto-play getter.
Use a build-neutral name such as `suppress_card_save` for the differing save
site. AutoPlay remains one indivisible opt-in safety feature.

Runtime acceptance must still observe the finish-game transaction sequence,
server/card state, lack of result CSV output, marker visibility, and authored
hidden-note completion. The static bypass does not establish those observations.

## Widescreen ownership and layout

| Native owner | 4.71 RVA | 2.06 RVA | Evidence |
| --- | --- | --- | --- |
| Selected bar and counters | `0x1E3EC0` | `0x1BD660` | Same panel/number/gauge call order, four counter draw families |
| Effect packet producer | `0x1F0B00` | `0x1CD650` | EAX carries newly allocated packet before initialization |
| Effect packet drain | `0x1F0F60` | `0x1CDAB0` | ECX carries packet at draw, then unlink/recycle |
| Effect-root traversal | `0x1F1180` | `0x1CDCD0` | Selected root in ECX at the native child traversal |
| Gameplay effects renderer | `0x248D40` | `0x215560` | Same primary text and reached tutorial selection |
| Gameplay render dispatcher | `0x262F10` | `0x1C3520` | Stage, track, effects, and flush ownership |

The collection moves from **`CTune+0x1D6C` to `CTune+0x1D60`**. Its element
range still uses pointers at collection `+0x0C/+0x10`, with four-byte entries.
The old CTune constructor initializes that collection at `0x1C0B20`; its
effect-root setup is `0x1C1560`.

The reached primary text slots remain decimal **12, 15, 18, 24, 27, 30**:
the old renderer computes `24*player + 12*history + 9 + 3*grade`.
The reached tutorial family remains hexadecimal
**B2, B3, B4, B5, B6, B9, BA, BB, C0**. The old setup table at `0x2A9C28`
also initializes B7, but the reached note-type selector has no type-6 case;
B7 is not added to the placement selection merely because it is initialized.
Keep track effects (93–97 decimal), result roots (2–6), and unrelated roots
outside selected placement.

Other relevant contracts are unchanged:

- counter entry local `EBP-0x14`;
- effect-root manager field `+0x74`;
- packet allocation result EAX, submission owner ECX, links `+0x70/+0x74`;
- renderer device/window/style `+0x08/+0x8C/+0x98`;
- network visitor matrix stack `+0x1A0`;
- four general batch queues; never flush the private effect-manager queue
  from a placement callback.

Preserve the current policy: centered enclosing gameplay pass; configured
placement only around selected bar calls; counter-side placement around
individual counter calls; right placement carried by selected feedback packets.

### Test Mode boundaries

The 2.06 native Test Mode draw call is `0x207F3C -> 0x161110`, followed by
restoration at `0x207F41`. They sit between device calls through slots
`+0xA4/+0xA8`. This scopes the native form rendering, including loader-added
timing rows.

The earlier structural candidate `0x207F1A` is the device accessor **before**
the native draw. It is rejected as the restoration site. The final semantic
record supersedes the candidate alignment in `gc206-widescreen.json`.

### New vtable contracts

| Role | 2.06 cell RVA | Expected target RVA | ABI |
| --- | --- | --- | --- |
| `MovieClipInstance::accept`, class slot `+0x14` | `0x2806F0` | `0x0D5FA0` | thiscall(instance, visitor), then visitor slot `+0x3C` |
| `DrawTraverse` shape visit, class slot `+0x4C` | `0x27DE18` | `0x0C1870` | thiscall(visitor, shape) |

Corresponding class vtables are `0x2806DC` and `0x27DDCC`. These are global
class-slot registrations through the existing registry; expected pointers
must be resolved against the loaded base. The original seven pointer
contracts remain in `07_windowed_widescreen/windowed_widescreen_contract.json`.

## NESYS 2.86.1

The ping routine starts at RVA `0x008E20`, versus `0x008E40` in 2.97. The
2.86.1 prefix is:

```text
51 53 55 56 57 50 8B D9 8D 6B 04 6A 10 55 C7 44
24 1C 00 00 00 00 E8 02 72 02 00 83 C4 0C 8D 73
```

At entry EAX contains the target string and ECX owns the ping object. The
routine copies the string into object `+4`, prepares/sends the ping, waits for
its worker, and reports the result. `OnServicePingAddress` can be shared.
Select this profile using the **NESYS executable's own identity**, independently
of the game version. Registry, resolver, adapter, launch and pipe policy stay
with their current owners; their real-process acceptance is still pending.

## Work remaining in the refactored loader

| Area | Current fact | Required port change |
| --- | --- | --- |
| Build identity | `GameBuild` has only 4.71; `NesysBuild` only 2.97 | Add the two verified identities and readable diagnostic names |
| Preflight | Exact hashes select known profiles; unknown candidates require complete local contracts | Preserve this policy and existing fail-fast install behavior |
| Framerate | Fixed 53-hook/10-target profile; several callbacks bind registers by hook ID | Real per-build rows and ABI choices; old register/stack/continuation contracts |
| Countdown | Fixed 32-operation profile | 26 old semantic calls; six later-only calls omitted |
| Widescreen | 86 byte/9 pointer profile and shared callbacks | Populate the new mappings and old layout; preserve packet ownership |
| AutoPlay | 4.71 parser no-save patch | Old native save-state branch described above |
| Diagnostics | Build name depends only on game-vs-service variant | Print the actual build enumerator |

Old high-FPS capability omissions remain the two `RemoteCadence*` sites and
three `UnlockReward*Store` sites. Disable the unlock-prompt inspection before
reading its absent task layout. General high-FPS and countdown support remain
in scope. The old audio-resync continuation is `0x20CD07`, which resumes native
logic; it is not interchangeable with the newer epilogue despite the current
target name `audio_resync_epilogue`.

See the [implementation plan](../superpowers/plans/2026-09-06-gc206-version-support.md).

## Reproduction

Run from the source repository, with the existing IDA-CLI installation:

```powershell
python -B .codex-tmp/gc206-2026-09-06/ida_read.py gc206 semantic .codex-tmp/gc206-2026-09-06/query_semantic_checks.py
python -B .codex-tmp/gc206-2026-09-06/ida_read.py gc206 native-contracts .codex-tmp/gc206-2026-09-06/query_native_contracts.py
python -B .codex-tmp/gc206-2026-09-06/ida_read.py nesys206 ping .codex-tmp/gc206-2026-09-06/query_nesys.py
```

The local artifact directory retains each query's inputs and prerequisite
results. No query patches or saves an IDB. The complete widescreen address
table follows, generated from the final semantic record; its lengths describe
whole-instruction protected spans, not proof of installed-hook behavior.


| Contract | 2.06 RVA | Protected bytes | Length |
| --- | --- | --- | ---: |
| `screen_width_int` | `0x044180` | `A1 A8 0D 74 00` | 5 |
| `screen_height_int` | `0x044190` | `A1 AC 0D 74 00` | 5 |
| `screen_width_float` | `0x0441A0` | `D9 05 B0 0D 74 00` | 6 |
| `screen_height_float` | `0x0441B0` | `D9 05 B4 0D 74 00` | 6 |
| `logical_target_width_set` | `0x0441C0` | `DB 44 24 04 8B 44 24 04` | 8 |
| `logical_target_height_set` | `0x0441E0` | `DB 44 24 04 8B 44 24 04` | 8 |
| `target_width_int` | `0x044200` | `A1 B8 0D 74 00` | 5 |
| `target_height_int` | `0x044210` | `A1 BC 0D 74 00` | 5 |
| `target_width_float` | `0x044220` | `D9 05 C0 0D 74 00` | 6 |
| `target_height_float` | `0x044230` | `D9 05 C4 0D 74 00` | 6 |
| `viewport_reset` | `0x0443A0` | `8B 4C 24 04 33 C0` | 6 |
| `logical_resolution_set` | `0x0448C0` | `6A FF 68 3B 49 63 00` | 7 |
| `frame_begin` | `0x04C030` | `51 53 56 8D 44 24 08` | 7 |
| `frame_end` | `0x04C0A0` | `8B 41 08 8B 08` | 5 |
| `reset_pre` | `0x04C64B` | `83 BE 94 00 00 00 00` | 7 |
| `reset_post` | `0x04C834` | `83 C4 04 B8 01 00 00 00` | 8 |
| `window_device_create` | `0x04CC60` | `83 EC 64 53 55` | 5 |
| `task_dispatch` | `0x04D620` | `8B 09 8B 01 8B 50 10` | 7 |
| `mouse_debug_poll` | `0x0A3E10` | `55 8B EC 83 EC 08` | 6 |
| `batch_flush` | `0x1A2C50` | `55 8B EC 83 EC 08 C7 45 FC 00 00 00 00` | 13 |
| `bar_difficulty_a_begin` | `0x1BD6E8` | `E8 C3 61 F9 FF` | 5 |
| `bar_difficulty_a_end` | `0x1BD6ED` | `83 C4 18 EB 36` | 5 |
| `bar_difficulty_b_begin` | `0x1BD720` | `E8 8B 61 F9 FF` | 5 |
| `bar_difficulty_b_end` | `0x1BD728` | `E8 53 6C E8 FF` | 5 |
| `bar_panel_480_begin` | `0x1BD750` | `E8 DB CA FF FF` | 5 |
| `bar_panel_480_end` | `0x1BD755` | `E8 56 3A E4 FF` | 5 |
| `bar_panel_524_begin` | `0x1BD777` | `E8 B4 CA FF FF` | 5 |
| `bar_panel_524_end` | `0x1BD77C` | `E8 FF 6B E8 FF` | 5 |
| `bar_panel_568_begin` | `0x1BD7C6` | `E8 65 CA FF FF` | 5 |
| `bar_panel_568_end` | `0x1BD7CB` | `E8 E0 39 E4 FF` | 5 |
| `bar_stage_panel_begin` | `0x1BD803` | `E8 28 CA FF FF` | 5 |
| `bar_stage_panel_end` | `0x1BD808` | `51 D9 E8 D9 1C 24` | 6 |
| `bar_stage_current_begin` | `0x1BD870` | `E8 2B 79 FE FF` | 5 |
| `bar_stage_current_end` | `0x1BD875` | `83 C4 20 51 D9 E8` | 6 |
| `bar_stage_total_begin` | `0x1BD8CD` | `E8 CE 78 FE FF` | 5 |
| `bar_stage_total_end` | `0x1BD8D5` | `E8 D6 38 E4 FF` | 5 |
| `bar_gauge_begin` | `0x1BD94B` | `E8 30 86 FE FF` | 5 |
| `bar_gauge_end` | `0x1BD953` | `E8 58 38 E4 FF` | 5 |
| `bar_panel_216_begin` | `0x1BD983` | `E8 A8 C8 FF FF` | 5 |
| `bar_panel_216_end` | `0x1BD988` | `E8 23 38 E4 FF` | 5 |
| `bar_score_panel_begin` | `0x1BD9AA` | `E8 81 C8 FF FF` | 5 |
| `bar_score_panel_end` | `0x1BD9AF` | `51 D9 E8 D9 1C 24` | 6 |
| `bar_score_digits_begin` | `0x1BDA01` | `E8 9A 77 FE FF` | 5 |
| `bar_score_digits_end` | `0x1BDA09` | `E8 A2 37 E4 FF` | 5 |
| `bar_extra_panel_a_begin` | `0x1BDA39` | `E8 F2 C7 FF FF` | 5 |
| `bar_extra_panel_a_end` | `0x1BDA3E` | `51 D9 E8 D9 1C 24` | 6 |
| `bar_extra_digits_a_begin` | `0x1BDA8A` | `E8 11 77 FE FF` | 5 |
| `bar_extra_digits_a_end` | `0x1BDA92` | `E8 19 37 E4 FF` | 5 |
| `bar_extra_panel_b_begin` | `0x1BDAB4` | `E8 77 C7 FF FF` | 5 |
| `bar_extra_panel_b_end` | `0x1BDAB9` | `51 D9 E8 D9 1C 24` | 6 |
| `bar_extra_digits_b_begin` | `0x1BDB05` | `E8 96 76 FE FF` | 5 |
| `bar_extra_digits_b_end` | `0x1BDB0D` | `E8 9E 36 E4 FF` | 5 |
| `bar_mode_panel_begin` | `0x1BE32B` | `E8 00 BF FF FF` | 5 |
| `bar_mode_panel_end` | `0x1BE330` | `E8 7B 2E E4 FF` | 5 |
| `bar_player_panel_begin` | `0x1BE371` | `E8 BA BE FF FF` | 5 |
| `bar_player_panel_end` | `0x1BE376` | `E8 35 2E E4 FF` | 5 |
| `bar_status_panel_begin` | `0x1BE3C8` | `E8 63 BE FF FF` | 5 |
| `bar_status_panel_end` | `0x1BE3CD` | `E8 AE 5F E8 FF` | 5 |
| `chain_label_end` | `0x1BDCA8` | `51 D9 45 D8 D9 1C 24` | 7 |
| `chain_digits_end` | `0x1BDCF5` | `83 C4 20 C7 45 CC 00 00 00 00` | 10 |
| `chain_glow_end` | `0x1BDDB1` | `E9 4B FF FF FF` | 5 |
| `hundred_digits_end` | `0x1BDF07` | `83 C4 20 E8 71 64 E8 FF` | 8 |
| `chain_glow_begin` | `0x1BDDA9` | `E8 F2 73 FE FF` | 5 |
| `hundred_digits_begin` | `0x1BDF02` | `E8 99 72 FE FF` | 5 |
| `chain_label_begin` | `0x1BDCA3` | `E8 88 C5 FF FF` | 5 |
| `chain_digits_begin` | `0x1BDCF0` | `E8 AB 74 FE FF` | 5 |
| `effect_packet_allocated` | `0x1CD700` | `89 45 F4 83 7D F4 00` | 7 |
| `effect_packet_end` | `0x1CDC19` | `8B 4D AC C7 41 70 00 00 00 00` | 10 |
| `effect_packet_begin` | `0x1CDC14` | `E8 37 F5 FF FF` | 5 |
| `gameplay_feedback_draw_begin` | `0x1CDD38` | `E8 83 0D 00 00` | 5 |
| `gameplay_feedback_draw_end` | `0x1CDD3D` | `8B 4D F8 8B 51 0C` | 6 |
| `test_mode_native_begin` | `0x207F3C` | `E8 CF 91 F5 FF` | 5 |
| `test_mode_native_end` | `0x207F41` | `E8 3A C4 E3 FF` | 5 |
| `config_apply` | `0x209190` | `55 8B EC 83 EC 14` | 6 |
| `gameplay_hud_projection` | `0x20C8EA` | `E8 91 B5 FA FF` | 5 |
| `live_frustum_helper` | `0x210CE0` | `55 8B EC 81 EC C0 00 00 00 89 8D 58 FF FF FF` | 15 |
| `clip_default` | `0x2112C6` | `C6 45 DF 00` | 4 |
| `clip_gate` | `0x2112CA` | `8B 95 80 FE FF FF` | 6 |
| `clip_continuation` | `0x21132F` | `8B 4D D8 E8 39 A6 E1 FF 0F B6 C0` | 11 |
| `clip_owner` | `0x211100` | `55 8B EC 81 EC A0 01 00 00 56 57 89 8D 80 FE FF FF` | 17 |
| `bar_names_end` | `0x216A69` | `8B 85 4C FE FF FF` | 6 |
| `bar_names_begin` | `0x216A64` | `E8 27 47 FA FF` | 5 |
| `gameplay_stage_background` | `0x1C35B0` | `E8 3B E5 04 00` | 5 |
| `gameplay_track` | `0x1C35B8` | `E8 03 38 05 00` | 5 |
| `gameplay_effects` | `0x1C3608` | `E8 53 1F 05 00` | 5 |
| `gameplay_effects_end` | `0x1C360D` | `E8 6E 0D E8 FF` | 5 |

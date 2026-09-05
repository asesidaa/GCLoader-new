# Loader cleanup native evidence

## 2026-09-05: compatibility, AutoPlay, SongUnlock (Plan 06a)

Source migration follows the frozen cleanup baseline. Evidence was read from
the existing `H:\gc\game471.exe.i64` IDA database through a short-lived
IDA-CLI client (backend required IDA), with file offsets supplied by IDA.
Each listed byte contract was also compared directly against
`H:\gc\game_decrypted.exe` and `H:\gc\game471.exe`.
No IDB mutation/save, game launch, or deployment occurred.

| Feature/site | RVA | Native contract and consequence |
|---|---:|---|
| Compatibility/native_mouse_events | 0x000B0896 | Clean JNZ +2 (`75 02`) enters the mouse-event dispatch; the known patched file contains two NOPs, falling into the existing jump past that dispatch. |
| Compatibility/dongle_failure | 0x00102C7B | Clean JNZ +0x3B follows the dongle result test; `EB 3B` unconditionally bypasses the failure-log/SuspendThread path. |
| Compatibility/dongle_security_transmit | 0x00103EE6 | Clean `E8 45 F6 FF FF` calls RVA0x103530 after pushing its argument; known patched file has five NOPs. The existing caller cleanup at RVA0x103EEB remains. |
| Compatibility/rfid_com_port | 0x002F7AC3 | Character in the native COM string: clean `31` (COM1), patched `32` (COM2). This is a data write. |
| AutoPlay/native_auto_play | 0x0003CADA | Thiscall getter at RVA0x3CAD0 returns object byte +0xA5 in AL. Replace its six-byte load with MOV AL,1 and four NOPs, retaining the epilogue. |
| AutoPlay/complete_is_mute | 0x0003CAFA | Adjacent thiscall getter at RVA0x3CAF0 returns object byte +0xA6 in AL; same replacement. The separate +0xA7 setter/getter at RVA0x3CB10/0x3CB30 remains untouched. |
| AutoPlay/do_not_save_card_data | 0x00269951 | SETNZ CL follows parsing of the named DoNotSaveCardData field, and CL is stored to config-object byte +2 at RVA0x269957. MOV CL,1 plus NOP forces that result without altering neighboring persistence fields. |
| AutoPlay/marker_seam | 0x00058BE9 | Prefix `8D 44 24 08 50 E8 8D 03 00 00` is LEA EAX,[ESP+8], PUSH EAX, CALL RVA0x58F80 in the outer render phase, before render subscribers. SafetyHook retains/replays native instructions; callback does not edit saved context. The full 10-byte prefix remains the conservative protected contract. |
| AutoPlay/native_debug_text | 0x00069650 | Prefix `55 8B EC 6A FF`; cdecl caller cleanup (plain RET). Stack parameters supply float x/y, packed ARGB, format pointer, and the address of subsequent varargs. It constructs a gw::GWDebDispInstance_Text and invokes the debug display object. The existing typed cdecl varargs declaration and four fixed calls are retained. |
| SongUnlock/availability_branch | 0x00257854 | JNZ RVA0x257A77 follows CMP [EAX+0x8C],0, where EAX comes from the RVA0x1260 accessor. Replacement `E9 1E 02 00 00 90` reaches the same target unconditionally. That branch sets object field +0xBB8 to15 and bypasses the three-difficulty clear-state loop; subsequent extra-content checks remain. IDA's Concurrency-labelled accessor name is not used as semantic evidence. |

Both known files retain the original five AutoPlay contracts and SongUnlock
branch. Only the four compatibility sites differ between these coherent
variants. Unknown hashes must match every contributed original contract;
there is no local installed/mixed-byte classification in these feature profiles.

The approved plan now owns each concrete operation and its resolved address.
Byte replacements, original publisher slots, hook callbacks, pointer-slot
values, memory kinds, dependency order and overlap checks are validated before
approval. The executor binds to the same image base/size, preserves stable
ordering, and never reselects a profile. Hook failures retain ownership and
return their typed cause to the caller's fatal boundary; no reset or reverse
write follows failure.

Plan06a's feature-local eager-installer removal and Plan09's deferred global
cutover are reconciled by `Loader/TransitionalVersionedStartup.*`: the old
early compatibility and post-config optional startup stages call the shared
validator/executor while the complete prepared path remains dormant. This
adapter is temporary and must be deleted in Plan09. Intermediate DLLs are not
release checkpoints and do not constitute the final global-preflight design.

Runtime acceptance (marker display, save suppression, song availability and
startup in the real game) remains unperformed.

## 2026-09-05: Switch gameplay input (Plan 06b)

The same existing IDA daemon and a disconnected-after-use client verified all
three signatures against both game files before migration. The x86 SafetyHook
E9 implementation was also inspected: it decodes whole instructions until at
least five bytes are covered, and the x86 path has no FF fallback.

| Site | RVA | Original signature | Replaced span |
|---|---:|---|---:|
| pressed_edge | 0x00259640 | 55 8B EC 83 EC 18 89 4D EC C6 45 FF 00 8B 4D EC | 6 |
| held_state | 0x00259570 | 55 8B EC 83 EC 18 89 4D EC C6 45 FF 00 8B 4D EC | 6 |
| diagonal_match | 0x001D32A0 | 0F B6 55 8B 83 FA 01 75 2B | 7 |

Pressed/held entries receive the manager in ECX and three stack arguments:
input-device ID, logical-input ID and gameplay frame. Native return is AL
and the epilogue is RET0xC. The manager selects the device and dispatches its
pressed (+0x18) or held (+0x1C) vtable method, using its current frame for
frame -1. Native call sites push frame, logical input and device ID in that
order, then set ECX. The detour uses the matching fastcall bridge with an
ignored EDX argument; its original slot is a typed thiscall function.

The diagonal seam belongs to the EBP frame established at RVA0x1D2E50.
Instruction operands directly confirm these widths and ownership:

- EBP-0x75 is the one-byte native match flag. Native logic initializes it at
  RVA0x1D3147 and sets it after exact/adjacent-direction acceptance.
- EBP-0x7C is the four-byte target direction from the note-angle conversion
  at RVA0x1D318A, stored at RVA0x1D3192.
- EBP-0x68 is the four-byte current direction, loaded by the native raw
  direction query and then mapped by RVA0x1D2E00 before comparisons.
- The mid seam replays MOVZX EDX,[EBP-0x75] and CMP EDX,1; the JNZ remains
  at RVA0x1D32A7. Updating only the match byte before replay preserves register
  and flag production by those native instructions.

The profile binds the confirmed layout to its diagonal callback. Feature
policy still applies aliases and records only first acceptances. Typed,
SEH-guarded stack reads/writes replace the one-production StackAccessor table.
No hook object, signature-reading loop, reset, or fallback-to-Arcade installer
remains in Switch. Callback state stays Arcade until the executor succeeds,
then one release store activates Switch. Arcade contributes no operations.

Plan06b refines the common contract model: hook signatures may extend past
the physical replaced span. Address validation covers the larger of the two;
byte validation checks the complete signature; conflict detection uses the
physical span. AutoPlay's earlier conservative 10-byte marker span is now
the actual five-byte LEA/PUSH span, retaining its complete 10-byte signature.
No native-byte contract was shortened.

The temporary Loader adapter also handles the existing Switch startup slot
until Plan09. The dormant complete plan places Switch after compatibility.
This evidence establishes static bytes/ABI/control flow, not input feel,
diagonal behavior or high-FPS acceptance in the running game.

## 2026-09-05: Absolute Judgement (Plan 06c)

Saved read-only IDA-CLI script:
`.codex-tmp/loader-cleanup-absolute-judgement-profile.py`; results:
`.codex-tmp/loader-cleanup-absolute-judgement-native.json`. The client used
the existing game471 daemon and disconnected; no database mutation/save or
game launch occurred. All 18 contracts exactly match the IDB and both known
executable files. Hook spans cover whole instructions for the x86 E9 jump.

| Site | RVA | Exact signature | Hook replaced span |
|---|---:|---|---:|
| GameplayInitialization | 0x26251c | 89 4D 80 E8 2C 60 F0 FF | 8 |
| SemanticStageEntry | 0x2641cc | 8B 8D 4C FD FF FF C7 41 10 00 00 00 00 | 6 |
| SemanticStageExit | 0x264d9a | 8B 95 4C FD FF FF C7 42 04 13 00 00 00 | 6 |
| LoopGuard | 0x240239 | 0F 8E 91 00 00 00 | 6 |
| Pressed | 0x22dfb0 | 55 8B EC 83 EC 28 89 4D D8 C6 45 FF 00 8B 4D D8 | 6 |
| Held | 0x22df50 | 55 8B EC 83 EC 0C 89 4D F4 C6 45 FF 00 8B 4D F4 | 6 |
| Released | 0x22dd30 | 55 8B EC 83 EC 28 89 4D D8 C6 45 FF 00 8B 4D D8 | 6 |
| Direction | 0x22e480 | 55 8B EC 83 EC 08 89 4D F8 8B 45 0C D9 EE D9 18 | 6 |
| HeldAge | 0x22daa0 | 55 8B EC 83 EC 08 89 4D F8 C7 45 FC 00 00 00 00 | 6 |
| TimingGrade | 0x1d0e00 | 55 8B EC 83 EC 4C 89 4D CC 8B 45 08 D9 80 B0 00 00 00 | 6 |
| LoopTail | 0x2402d0 | 8B 4D F8 51 8B 8D D4 FC FF FF E8 01 7E DF FF | read only |
| Recognition | 0x1d68e0 | 55 8B EC 6A FF 68 31 A6 67 00 64 A1 00 00 00 00 | read only |
| Score | 0x1cf930 | 55 8B EC 83 EC 0C 89 4D F4 8B 45 F4 | read only |
| GetInputManager | 0x1040 | 55 8B EC 6A FF 68 8E D6 67 00 64 A1 00 00 00 00 | read only |
| GetGlobal | 0x11d0 | 55 8B EC E8 18 FF FF FF 5D C3 CC CC | read only |
| GetConfig | 0x11e0 | 55 8B EC E8 E8 FF FF FF 8B C8 E8 81 FF FF FF | read only |
| GetSoundManager | 0x210400 | 55 8B EC A1 9C 24 7F 00 5D C3 CC CC | read only |
| GetGroupCursor | 0x2122b0 | 55 8B EC 6A FF 68 9B 8D 67 00 64 A1 00 00 00 00 | read only |

Gameplay initialization observes ECX before MOV [EBP-80h],ECX and the next
native call. Its parent is CTuneGameManager_InitGameplayState at RVA2624F0.
Semantic entry and exit belong to the frame established at RVA2630B0:
[EBP-2B4h] is the four-byte saved Tune receiver (saved at RVA2630DC).
Entry follows state17 commit and precedes zeroing Tune+10h and filling
frame-zero input history. Exit precedes committing state19. These callbacks
do not replace those native writes.

The loop guard belongs to CTuneGameManager_ProcessJudgementFrame at
RVA2401E0. [EBP-32Ch] is its four-byte saved receiver (RVA2401E9).
The six-byte JLE follows CMP [receiver+14h],0; the callback executes the
owned scheduler then transfers EIP to the verified loop-tail continuation.
The tail uses the original EBP frame and the saved player index [EBP-8];
it is a continuation, not a separately callable function.

Pressed, held and released receive CBooster in ECX, logical input ID and
frame on the stack, return AL and use RET8. Direction receives booster ID,
float X/Y output pointers and frame, returns EAX and uses RET10h. Held age
receives one unsigned input ID, returns EAX and uses RET4. Timing grade
receives judgement state in ECX, a note pointer and recognition milliseconds,
returns EAX and uses RET8. Its FLD at RVA1D0E0C reads the four-byte float
at note+B0h (float index44). The detours retain the corresponding fastcall
bridge and typed thiscall originals.

Recognition uses ECX judgement state, milliseconds and native frame, RET8.
Score uses ECX score state and milliseconds, RET4; its EAX result remains
ignored by the existing caller. Input/global/config/sound-manager accessors
take no arguments, return a pointer in EAX and use caller-clean/no-argument
RET. Group cursor takes ECX sound manager and one integer group, returns a
signed cursor in EAX and uses RET4; native gameplay pushes group2 at
RVA24016A, calls the sound accessor and then the cursor function.

| Layout | Native owner/evidence | Width |
|---|---|---:|
| Tune judgement collection +254h | Member149At RVA12250 adds254h; loop resolves element before recognition | embedded collection, four-byte elements |
| Tune score collection +26Ch | Member155At RVA380E0 adds26Ch; loop resolves element before score | embedded collection, four-byte elements |
| Collection begin +Ch / end +10h | RVA128A0 computes (end-begin)/4; RVA3D0C0 bounds-checks index then returns begin+index*4 | four-byte pointers |
| Global player +CB4h | Global accessor result read at RVA240226/240244 | four-byte integer |
| Input manager booster +4 | SetCurrentFrameAndFillHistory RVA259860 checks and loads this+4 before booster call | four-byte pointer |
| Configuration game-time offset +2Ch | Config accessor at RVA24015C, subtraction at RVA240164 | four-byte signed integer |
| Configuration hold safe-frame +64h | Config accessor at RVA1D433E, read at RVA1D4352 | four-byte signed integer |
| Configuration slide-hold safe-frame +68h | Config accessor at RVA1D370D, read at RVA1D3721 | four-byte signed integer |
| Score miss/good/cool/great +78h/+7Ch/+80h/+84h | ApplyGrade RVA1CE680 increments [this+78h+grade*4] at RVA1CE765/1CE772; miss branch increments +78h at RVA1CE8C7/1CE8D0 | four-byte counters |
| Judgement arrange +AAh | Recognition clears at RVA1D6939 | one byte |
| Judgement left/right free tap +EDh/+EEh | Recognition clears at RVA1D6953/1D6960 | one byte |
| Timing-grade target +B0h | Timing-grade FLD at RVA1D0E0C | four-byte float |

The enabled manifest has ten hooks and eight read-only targets. Hook order:
pressed, held, released, direction, held age, loop guard, semantic exit,
gameplay initialization, semantic entry, timing grade. The central registry
publishes each typed original before enabling its hook. Runtime bindings and
the copied layout are published before the first hook. Timing grade is now
required; a create/enable failure aborts through the common executor.

Code inspection identified a necessary correction to the illustrative
three-operation disabled manifest: stage entry in stock/ASIO mode calls the
native configuration accessor. Disabled mode therefore has **three lifecycle
hooks plus the read-only get_config contract**. This keeps that existing
native call inside the unknown-hash barrier. The three hooks are mandatory
on every startup; their callbacks preserve the previous inactive behavior
for stock non-ASIO mode. No new audio-hook prerequisite is imposed on that
previously inactive mode. The runtime still checks the existing input rate,
clock capability and committed audio route before activating relevant state.

Scheduler, history, query scope and semantic stage policy are unchanged.
Feature-typed fatal predicate IDs, operand capture, stopped-recognition flag
and detailed fatal logs are retained; final termination now uses AbortProcess.
The obsolete local hook transaction and warning-only diagnostic installer
are gone. Checked image resolution replaces runtime base-plus-RVA arithmetic.

Full Debug and RelWithDebInfo builds and all five existing tests passed in
each configuration. No new native test seams were added. Note timing,
grades, audio alignment and gameplay acceptance remain unperformed.

## 2026-09-05: Framerate and Countdown (Plan 06d)

The saved IDA-CLI script `.codex-tmp/loader-cleanup-framerate-profile.py`
and `loader-cleanup-framerate-native.json` verify **17 direct writes,
53 available hooks, 32 countdown calls and ten callable/continuation targets**
against both known game files and the existing IDB. All matched. Sorting the
102 mutation spans produced zero overlaps. The script records whole decoded
hook spans, surrounding instructions, native operand widths, parent functions,
inline decompilation/epilogues and representative callers. It does not install
hooks, mutate the IDB or launch the game.

| Direct write | RVA | Original bytes | Kind / derived replacement |
|---|---:|---|---|
| gameplay frame milliseconds | 0x002FC0A0 | 55 55 85 41 | data; frame milliseconds |
| visual frame milliseconds | 0x002F4604 | 55 55 85 41 | data; frame milliseconds |
| gameplay frame seconds | 0x002FC280 | 89 88 88 3C | data; frame seconds |
| render smoothing step | 0x002E8F00 | 00 00 80 40 | data; smoothing |
| render offset-decay step | 0x002E8F04 | 00 00 A0 40 | data; decay |
| XIO repeat initial duration | 0x00055CCC | C7 00 10 00 00 00 | code; scaled16 frames |
| XIO repeat next duration | 0x00055CDD | C7 00 08 00 00 00 | code; scaled8 frames |
| native keyboard repeat initial duration | 0x0005F843 | C7 86 D4 02 00 00 10 00 00 00 | code; scaled16 frames |
| native keyboard repeat next duration | 0x0005F84D | C7 86 D8 02 00 00 08 00 00 00 | code; scaled8 frames |
| gameplay countdown duration | 0x002645EE | C7 80 14 1D 00 00 78 00 00 00 | code; two-second frame count |
| render EAX countdown duration | 0x00249A5E | B8 78 00 00 00 | code; two-second frame count |
| render EDX countdown duration | 0x00249A73 | BA 78 00 00 00 | code; two-second frame count |
| palette normalizer operand one | 0x0022BACF | D8 2D AC BB 6F 00 | code; stable target-FPS operand address |
| palette normalizer operand two | 0x0022BAD5 | D8 35 AC BB 6F 00 | code; stable target-FPS operand address |
| chart seconds-to-frames operand | 0x00262CB6 | D8 0D AC BB 6F 00 | code; stable target-FPS operand address |
| non-song menu repeat initial duration | 0x00382CE8 | 10 00 00 00 | data; scaled16 frames |
| non-song menu repeat interval | 0x00382CEC | 03 00 00 00 | data; scaled3 frames |

The seven data writes are float timing operands and integer menu-repeat
values. The ten code writes retain their opcode/addressing prefix and replace
only the final four-byte immediate or address. The stable target-FPS float
belongs to the process-lifetime runtime; it is published before plan approval.
Checked timing arithmetic and the existing supported FPS range are unchanged.

| Available hook in preserved manifest order | RVA | Replaced span | ABI kind |
|---|---:|---:|---|
| MovieClipGoto | 0x000DEA30 | 7 | inline |
| MovieClipAdvance | 0x000DF940 | 5 | inline |
| PaletteCompare | 0x0022BA60 | 6 | mid |
| StageClipFrame | 0x00244054 | 9 | mid |
| IfblWait | 0x002309D4 | 6 | mid |
| StageBgmPreload | 0x0021001A | 6 | mid |
| TuneCountdownCompare | 0x002648F7 | 7 | mid |
| AudioSkipMargin | 0x0024018F | 6 | mid |
| AudioSkipInterval | 0x002401BD | 5 | mid |
| AudioResyncPolicy | 0x002401C4 | 9 | mid |
| GameplaySongClock | 0x00264DB2 | 5 | mid |
| GameplayEffectAdvance | 0x00264E2D | 5 | mid |
| EffectCadence6 | 0x0024063B | 8 | mid |
| EffectCadence5 | 0x002408D7 | 8 | mid |
| EffectCadence4 | 0x00240C9C | 8 | mid |
| EffectCadence16A | 0x00241213 | 11 | mid |
| EffectCadence16B | 0x0024122F | 6 | mid |
| EffectCadence8 | 0x00241268 | 8 | mid |
| RemoteCadenceA | 0x002632DB | 8 | mid |
| RemoteCadenceB | 0x00263646 | 8 | mid |
| GameplayBlink | 0x0024A1B9 | 7 | mid |
| GreatGoodLifetimeOperand | 0x002464A8 | 5 | mid |
| GreatGoodFrameOperand | 0x00246528 | 8 | mid |
| EffectLifetimeAOperand | 0x00248F00 | 5 | mid |
| EffectFrameAOperand | 0x00248F8C | 8 | mid |
| EffectLifetimeBOperand | 0x0024912B | 5 | mid |
| EffectFrameBOperand | 0x002491E0 | 6 | mid |
| DirectEffectFrameOperand | 0x00249C14 | 8 | mid |
| ChartEffectFrameAOperand | 0x0024BC8B | 8 | mid |
| ChartEffectFrameBOperand | 0x0024CC8A | 8 | mid |
| ChartEffectFrameCOperand | 0x0024CCBE | 8 | mid |
| ChartEffectFrameDOperand | 0x0024D836 | 8 | mid |
| FixedVisualFrameOperand | 0x00250AD5 | 8 | mid |
| GameplayCountdownAssetFrame | 0x00249A9C | 6 | mid |
| PlayerPositionInitA | 0x00263240 | 7 | mid |
| PlayerPositionInitB | 0x002632B2 | 7 | mid |
| PlayerPositionInitC | 0x0026359B | 7 | mid |
| PlayerPositionInitD | 0x00263615 | 7 | mid |
| PlayerPositionAssetFrame | 0x0024EF43 | 7 | mid |
| PlayerPositionDenominatorA | 0x0024F76D | 6 | mid |
| PlayerPositionDenominatorB | 0x0024FD40 | 6 | mid |
| EffectFlowItemFrame | 0x001F0310 | 6 | mid |
| EffectTutorialElapsed | 0x00249593 | 6 | mid |
| EffectChartPreRollDuration | 0x0024A934 | 6 | mid |
| EffectPlayerModuloDividend | 0x0025072E | 8 | mid |
| MovieClipPreprocessVisit | 0x000EFB90 | 7 | inline |
| RankingEntryCounterStore | 0x00216EB4 | 5 | mid |
| HitChartEntryCounterStore | 0x0026562F | 6 | mid |
| UnlockRewardCountdownStore | 0x00030DA3 | 6 | mid |
| UnlockRewardPrimaryStateStore | 0x00030E54 | 6 | mid |
| UnlockRewardSecondaryStateStore | 0x00030F23 | 6 | mid |
| NavigatorAdvance | 0x001B6310 | 6 | inline |
| OuterFrame | 0x00058B70 | 5 | mid |

This preserves the actual existing concatenation: 11 pre-effect, 34 effect,
six menu, then two post-effect entries. The plan's grouped inventory listed
the last two groups in a different order; the live source order is retained.
Short legacy signatures now extend through the complete decoded replaced
span. Longer signatures retain all their original bytes.

Four inline hooks retain typed originals. MovieClip goto receives ECX and two
integers, returns AL and uses RET8; advance receives ECX and two byte-valued
stack arguments, returns AL and uses RET8. Preprocessing visit receives ECX
and one integer, returns void and uses RET4. Navigator advance receives ECX,
returns the receiver/pointer in EAX and uses RET. Native effect-manager advance
takes ECX and no stack arguments; the existing caller ignores its EAX result.

Mid callbacks preserve register/flag production and native timing boundaries:

- Palette compares the four-byte [EAX+Ch] counter; IFBL stores ECX at [EDX+3Ch];
  BGM preload gates ADD EAX,1; Tune countdown compares [EDX+1D14h].
- Audio interval consumes EDX:EAX with divisor [ECX+3Ch]. Margin/drift are
  signed four-byte locals at EBP-24h/-Ch; suppression resumes at the verified
  original epilogue. Shared song-clock intercepts its original CALL with ECX
  Tune, using Tune+10h/+14h and config+2Ch. Group cursor remains group2.
- Effect cadence consumes the existing EDX/ECX/EAX test/modulo operands.
  Tune receivers are four-byte locals at EBP-32Ch or EBP-2B4h; remote phase
  is a signed four-byte local at EBP-1FCh. Clock-domain selection is unchanged.
- Authored operand callbacks redirect the exact EAX/ECX/EDX base used by the
  following x87 multiply/divide at +18h. The profile owns that operand carrier.
  Player-position counters are [base+index*4+1D54h], and the x87 denominator
  reads a four-byte integer at +C4h. The profile owns that carrier as well.
- Countdown asset frame maps ECX before [EAX+8] store. Flow-item frame maps
  EAX before [EDX+8], tutorial elapsed maps EDX before its stack store,
  pre-roll scales EAX before its stack store, and player modulo maps EAX
  before IDIV ECX. These preserve the original callback policies.
- Menu gates preserve EAX/EDX values and either replay or skip the native
  four-byte counter stores. Ranking and HitChart resumes follow their
  indirect stores; UnlockReward resumes follow +376Ch/+37D4h stores.
  MovieClip instance offsets and prompt-name policy are preserved.

| Runtime target | RVA | Exact read-only signature |
|---|---:|---|
| audio_resync_epilogue | 0x002401D4 | 5E 8B E5 5D C3 |
| get_sound_manager | 0x00210400 | 55 8B EC A1 9C 24 7F 00 |
| get_group_cursor | 0x002122B0 | 55 8B EC 6A FF 68 9B 8D 67 00 |
| get_config | 0x000011E0 | 55 8B EC E8 E8 FF FF FF |
| advance_gameplay_effect | 0x001F08A0 | 55 8B EC 83 EC 10 89 4D F0 |
| ranking_resume | 0x00216EB9 | E9 CA FD FF FF E8 ED FB F4 FF |
| hitchart_resume | 0x00265637 | 8D 4D A4 E8 21 C0 DC FF |
| unlock_countdown_resume | 0x00030DA9 | 8B 4D DC 83 B9 6C 37 00 00 00 |
| unlock_primary_resume | 0x00030E5A | 8B 55 DC 83 BA D4 37 00 00 1F |
| unlock_secondary_resume | 0x00030F29 | 8B 4D DC 81 C1 70 37 00 00 |

Only targets needed by the selected hooks enter the manifest. Approved target
addresses are resolved through the selected RuntimeImage and published before
the first hook enables. No callback selects a build or adds a global RVA.

All 32 countdown sites are in CountdownProfile in their original order. Each
decodes as CALL rel32 to RVA2350C0, with return=call+5 and replacement
D9 EE 90 90 90. This replaces the returned delta with x87 zero and preserves
the timer's caller-side x87 contract. The setting is the existing immutable
`timer_freeze_enabled` snapshot. Disabled mode contributes no countdown
feature; enabled mode contributes all 32, after every framerate operation.

A second important distinction from the illustrative plan counts is retained:
**53 is the available hook manifest, not the installed count for every mode**.
At native60 timing the direct-write plan is empty. Existing audio/FPS selection
still chooses OuterFrame alone for the original watchdog, includes legacy
resync for that route, or selects the shared song-clock consumers. Transformed
timing includes the corresponding remaining hooks; mutually exclusive audio
routes are never installed together. Every selected contract enters preflight.

The renamed FramerateTimingProfile contains only existing timing math.
FramerateGameProfile owns the static contracts, layout values, operand ABI
carriers and native effect evidence. PreparedFrameratePlan owns dynamic
operations until VersionedPlanSet copies them. Runtime state, typed originals,
authored/gameplay clocks and the monitor have stable process lifetime.
FrameratePatchTransaction and CountdownTimerFreeze's eager installer were
deleted through the CLion source patch tool. No reverse writes/reset path or
feature-owned SafetyHook object remains.

The temporary Loader adapter validates Framerate plus enabled Countdown
together before installing either. The dormant complete game plan orders
Framerate after Absolute Judgement and Countdown after Framerate; Plan09
will replace the temporary adapter with the complete startup barrier.
Existing Framerate diagnostic action tables remain for their Plan08 migration.

Debug and RelWithDebInfo builds and the five existing tests passed in both
configurations. This is static/build proof; high-FPS pacing, countdown freeze,
menu/effect motion and gameplay acceptance remain unperformed.

## Plan06e: Test Mode Timing

The saved read-only IDA batches `.codex-tmp/loader-cleanup-test-mode-timing-profile.py`
and `loader-cleanup-test-mode-timing-layout.py` used short-lived clients of
`game471.exe.i64`. IDA mapping compared all 15 original byte contracts, all
13 four-byte vtable entries, and the row instruction independently against
both executable variants recorded above; every comparison passed.

| Native contract | RVA | Original bytes |
| --- | --- | --- |
| main constructor | 0x173EA0 | 55 8B EC 6A FF 68 A7 9A 67 00 64 A1 00 00 00 00 |
| main render | 0x173C60 | 55 8B EC 81 EC 9C 00 00 00 A1 94 93 77 00 33 C5 |
| sound constructor | 0x16AE80 | 55 8B EC 6A FF 68 97 71 67 00 64 A1 00 00 00 00 |
| game allocator | 0x23BD20 | 55 8B EC 8B 45 08 50 E8 94 FE FF FF 83 C4 04 5D |
| game deallocator | 0x23BD00 | 55 8B EC 8B 45 08 50 E8 44 FE FF FF 83 C4 04 5D |
| register child | 0xC2C90 | 55 8B EC 51 89 4D FC 8B 45 FC 8B 48 2C 8B 55 08 |
| base form update | 0xC2E40 | 55 8B EC 83 EC 0C 89 4D F4 C7 45 F8 00 00 00 00 |
| set grid cell text | 0xC1200 | 55 8B EC 51 89 4D FC 8B 45 FC 8B 4D 08 3B 48 28 |
| set selection | 0xC1C00 | 55 8B EC 51 89 4D FC 8B 45 FC 83 78 28 00 75 02 |
| draw title | 0x176940 | 55 8B EC 83 7D 14 04 75 07 C7 45 14 00 00 00 00 |
| set title position | 0x176900 | 55 8B EC 8B 45 0C 50 8B 4D 08 51 8B 0D 64 25 7F |
| draw help | 0x176920 | 55 8B EC 8B 45 14 50 8B 4D 10 51 8B 55 0C 52 |
| timing manager accessor | 0x1040 | 55 8B EC 6A FF 68 8E D6 67 00 64 A1 00 00 00 00 |
| judgment timing setter | 0x259310 | 55 8B EC 51 89 4D FC 8B 4D FC E8 B1 7D DA FF 0F |
| game timing setter | 0x259350 | 55 8B EC 51 89 4D FC 8B 4D FC E8 71 7D DA FF 0F |

The main constructor hook protects five complete instruction bytes; its
16-byte signature is retained. The main render hook protects nine complete
bytes and retains its 16-byte signature. ECX is the receiver. Constructor
takes one stack argument, returns the receiver in EAX, and uses RET 4;
render takes frame/input, returns the receiver in EAX, and uses RET 8.

The code write at RVA `0x173ED5` changes `6A 0B` to `6A 0C`: native
Main construction passes 11 rows to GWTestModeSelectForm, and the patch
expands the main menu to 12. The plan's sentence saying four to eleven is
incorrect. The timing carrier separately has four editor rows. Existing
row 10 timing entry, row 11 exit routing, temporary native selection 10,
and degradation to the native 11-row menu on allocation failure are preserved.

The SoundTest vtable at RVA `0x2FB864` contains these 13 target RVAs in order:
`0x6AB20 0x6AB20 0xC9B0 0x4D070 0xC2680 0x16B0C0 0x16B440 0x16B290 0x16B230 0x16AD60 0x16AC20 0x16A9A0 0xC2F20`.
Slot 3 is the scalar-deleting destructor: it runs the SoundTest destructor,
then the game deallocator when flags bit 0 is set, returning the receiver.
Slot 5's sound-specific update is replaced in the constructed carrier table
by base update RVA `0x0C2E40`. Slots 2, 6, 7, 8, 9 and 10 receive the existing
feature callbacks. This remains object construction; no native vtable slot
is modified, and no SafetyHook VMT object is introduced.

Native ownership/layout evidence:
- Main constructor RVA `0x173EA0` allocates `0x1D4` bytes for SoundTest and
  passes the constructor's parent. SoundTest constructor RVA `0x16AE80`
  constructs a nine-row, two-column base form. Child registration RVA
  `0x0C2C90` stores into the owner's child array. Raw allocation failure and
  constructor failure retain their existing deallocator paths; prepared
  object failure uses the scalar-deleting destructor.
- GWTestModeSelectForm initialization RVA `0x0C2A00` owns pointer grid +28h,
  pointer child array +2Ch, 32-bit row count +30h, active child +34h, and
  flags +38h. VGWTestModeWindowList constructor RVA `0x0C0550` owns 32-bit
  rows +28h, columns +2Ch and selection +4Ch. Main construction owns pointer
  status +3Ch, help +40h and title +44h.
- The CGameData accessor RVA `0x0010F0` returns VA `0x7D9848`.
  System-config accessors RVAs `0x0011E0`/`0x0011D0`/`0x001170`
  resolve CGameData +8. The timing config loader's named JudgTimeOffset and
  GameTimeOffset reads store 32-bit integers at configuration +28h/+2Ch:
  resulting RVAs `0x3D9878`/`0x3D987C`. They are mutable data, resolved
  with checked bounds, not compared against a startup value.
- Timing setters take ECX manager and one signed stack value. They check
  manager availability and its active pointer, then forward judgement to
  the active object's +14h field and game timing to +10h. Existing live
  application order remains game global, judgement global, manager lookup,
  game setter, judgement setter; setter return values are not reinterpreted.
- Allocator/deallocator, title, title-position, help and timing-manager
  accessor keep cdecl; SoundTest construction, registration, base update,
  cell text, selection and timing setters keep thiscall.

The profile contributes 31 common operations: 15 read-only byte contracts,
one write, two inline hooks and 13 read-only pointer contracts. The readonly
rows share the common identity/range validation and run in preflight; they
do not claim mutation spans. RuntimeImage performs the row write before
HookRegistry installs constructor then render. The approved ABI and stable
carrier table are published before hook enable. Failure aborts through the
common publisher; TimingPatchTransaction and its rollback/local mechanics
are removed. Live/render/lifecycle/commit action tables remain for Plan08.

The dormant complete plan requires this feature after GameCompatibility;
its runtime path requires prepared system configuration. The temporary
Loader adapter retains the current staging until Plan09's complete cutover.
Debug and RelWithDebInfo builds and all five existing tests passed in both
configurations. No test-mode UI, persistence, live setter or object-lifetime
runtime acceptance was performed.

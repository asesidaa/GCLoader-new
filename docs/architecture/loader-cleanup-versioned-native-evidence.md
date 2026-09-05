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

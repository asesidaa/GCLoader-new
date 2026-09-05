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

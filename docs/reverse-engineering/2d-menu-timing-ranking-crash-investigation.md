# Stage A ranking-screen crash investigation

Status: root cause confirmed; no source or deployed DLL changes

## Evidence identity

- Full-session log: `H:\gc\loader-log.txt`
  - size: 61,704 bytes
  - SHA-256: `29935BEC9AB11736AEEAAEDC6396DCBC2A01C40F81C1DA13F7FBB2C85C4FE7A3`
- Crash dump: `H:\gc\game471.1DD1C646285DAB2.crash.dmp`
  - size: 176,486 bytes
  - SHA-256: `DF24E584FC5D7C55BCDA5AE3E32F3A165CB067F53BD7D09884A3362E7E62E611`
- Deployed DLL: `H:\gc\iDmacDrv32.dll`
  - size: 5,656,064 bytes
  - SHA-256: `4D2336BE5A6BD1F0009692BB0382BD9284D0204038C3568FE850B74B25D3028F`
- The deployed DLL hash matches both the Stage A immutable build archive and
  `build-msvc32-release\dist\iDmacDrv32.dll`.
- Byte-identical copies of all three runtime artifacts are preserved under
  `H:\gc\artifacts\runtime-builds\2d-menu-timing\stage-a-observe\runs\20260726-033536-ranking-crash`.

## Log findings before dump analysis

- Startup selected `menu_timing_mode=observe`, installed all 53 framerate hooks,
  and committed the patch transaction.
- The 240 FPS external cap was validated.
- Gameplay completed far enough to exercise the gameplay-effect producers and
  the post-play unlock screen.
- The unlock hooks activated and continued safely in observe mode:
  `unlock_countdown=2/8/1` and `unlock_state_secondary=2/8/1`.
- At the final log record, both ranking and hit-chart diagnostic paths remained
  completely untouched: `ranking_entry=0/0` and `hitchart_entry=0/0`.
- No `menu_timing_activation path=ranking_entry` line was emitted.
- The last runtime-stat line is approximately two seconds before the dump file
  timestamp (the logger text and filesystem metadata differ by a fixed one-hour
  display offset).

The log therefore narrows the ranking failure to the first ranking-screen path,
at or before the callback reaches its counter increment and one-shot activation
log.

## Dump proof

WinDbg 10.0.29617.1000 reports:

- exception: `0xC0000005`, invalid-pointer write
- exception IP: `0x00616EBB`, `game471.exe+0x216EBB`
- attempted write: `0x1B0A9C60`
- process uptime: 253 seconds
- crashing stack begins:
  - `game471+0x216EBB`
  - `game471+0x23B972`
  - `game471+0x23BA34`
  - `game471+0x23173F`

The live bytes at the ranking hook site were:

```text
00616EB7  E9 82 B1 00 01    jmp 0162203E
00616EBC  90                nop
00616EBD  90                nop
```

Starting execution at `0x00616EB9`, two bytes inside that detour, decodes as:

```text
00616EB9  B1 00             mov cl,0
00616EBB  01 90 90 E8 ED FB add dword ptr [eax-0x04121770],edx
```

The second accidental instruction is exactly the faulting write reported by
the dump.

## IDA control-flow proof

The unpatched `game471.exe.i64` contains:

```text
00616CA9  83 38 00          cmp dword ptr [eax],0
00616CAC  0F 8C 07 02 00 00 jl  loc_616EB9
...
00616EB4  8B 4D E0          mov ecx,[ebp-20h]
00616EB7  89 01             mov [ecx],eax
00616EB9  E9 CA FD FF FF    jmp loc_616C88
```

The intended hook instruction at `0x00616EB7` is only two bytes. SafetyHook's
x86 detour requires at least five whole instruction bytes, so installation
relocated and overwrote a seven-byte span:

```text
[0x00616EB7, 0x00616EB9)  mov [ecx],eax
[0x00616EB9, 0x00616EBE)  jmp loc_616C88
```

That span is not single-entry. The negative-entry branch at `0x00616CAC`
directly targets the second instruction at `0x00616EB9`. After hook
installation, the branch still targets `0x00616EB9`, which is now the middle of
the detour. It executes the two accidental instructions shown by WinDbg and
faults at `0x00616EBB`.

This proves the crash is caused by the ranking hook placement, not the
observe-mode callback, its diagnostic memory read, logging, or the ranking
screen's original store. The bypass path never reaches the hook callback,
which also explains the final `ranking_entry=0/0` counter and missing ranking
activation log.

## Paired hit-chart defect

The hit-chart hook has the same invalid placement even though it was not the
fault in this dump:

```text
00665347  jl  loc_665637
...
00665635  89 01             mov [ecx],eax
00665637  8D 4D A4          lea ecx,[ebp-5Ch]
```

SafetyHook overwrites exactly five bytes at `0x00665635`, including the
three-byte instruction at `0x00665637`. The branch at `0x00665347` enters that
second instruction directly. With the hook installed it instead enters two
bytes inside the detour, so this path is independently crash/corruption-prone.

The other five new menu hooks do not have this structural problem:

- MovieClip preprocessing: seven-byte overwrite, no external interior entry
- MovieClip stop: one ten-byte instruction
- unlock countdown: one six-byte instruction
- unlock primary state: one six-byte instruction
- unlock secondary state: one six-byte instruction

## Why static tests passed

The hook-contract tests validate only the intended instruction's leading bytes:

- ranking: `89 01`
- hit chart: `89 01`

They do not decode the complete minimum detour span or reject external
control-flow entries into relocated interior instructions. The bindings and
observe/correct decision tests therefore passed while both live hook sites were
unsafe.

## Required correction shape

Do not install SafetyHook mid-hooks at either two-byte store.

A repair must move each detour to a single-entry instruction window and make
the callback skip to an intact shared continuation when suppressing the store.
IDA identifies viable nearby windows for the implementation design:

- ranking: hook at `0x00616EB4`; the five-byte relocated span is
  `mov ecx,[ebp-20h]` plus `mov [ecx],eax`, leaving the shared continuation
  `0x00616EB9` intact
- hit chart: hook at `0x0066562F`; the six-byte relocated instruction is
  `mov ecx,[ebp-94h]`, leaving both the store at `0x00665635` and the shared
  continuation at `0x00665637` intact; suppression must resume directly at
  `0x00665637`

Before deployment, the regression contract must validate the complete
SafetyHook overwrite span and its interior-entry invariant, not just the first
opcode.

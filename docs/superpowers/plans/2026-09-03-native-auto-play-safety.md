# Native Auto Play Safety Implementation Plan

**Goal:** Add one opt-in GCLoader feature that enables Groove Coaster's native
auto-play path, completes HIDDEN/AD-LIB descriptors, suppresses score/card
persistence, and draws a mandatory in-game warning marker.

**Spec:** `docs/superpowers/specs/2026-09-03-native-auto-play-safety-design.md`

## Evidence and execution policy

- Work only in `H:\gc\artifacts\GCLoader`. Use `H:\gc` as read-only binary,
  IDB, and runtime evidence unless deployment is separately authorized.
- This native patch does not use TDD and does not add or extend unit tests,
  fake executable memory, synthetic hook backends, copied fixtures, callback
  recorders, or standalone generated-output verifiers.
- Run the saved IDA-CLI Python files directly. Each script connects to the
  existing daemon only for the duration of that process. Do not hold a session
  and do not stop, restart, replace, or otherwise alter the daemon or another
  process.
- Establish the native contract from the actual database: named RVA, current
  bytes, decoded instructions, calling convention, ownership, and control
  flow. Generated files are only readable analysis output.
- Compilation is build evidence only. Gameplay, audio, saving, marker
  visibility, and input suppression require a separately authorized real-game
  run.
- Do not deploy a DLL, edit `H:\gc\data\expconfig.cfg`, change either game
  executable, or launch the game during this implementation.

## Frozen native contract

Analysis target: `H:\gc\game471.exe.i64`, preferred image base `0x00400000`.

| Purpose | RVA | Clean bytes | Patched bytes |
|---|---:|---|---|
| Native auto-play getter (`+0xA5`) | `0x0003CADA` | `8A 80 A5 00 00 00` | `B0 01 90 90 90 90` |
| HIDDEN/AD-LIB completion getter (`+0xA6`) | `0x0003CAFA` | `8A 80 A6 00 00 00` | `B0 01 90 90 90 90` |
| Do-not-save-card-data result | `0x00269951` | `0F 95 C1` | `B1 01 90` |
| Outer-frame marker seam | `0x00058BE9` | `8D 44 24 08 50 E8 8D 03 00 00` | SafetyHook mid-hook only |
| Native debug-text entry | `0x00069650` | begins `55 8B EC 6A FF` | call target only |

Native text ABI:

```cpp
using NativeDebugTextFn =
    int(__cdecl*)(float x, float y, std::uint32_t argb,
                  const char* format, ...);
```

The call format is always `"%s", text`. The feature does not patch grade state
`+0xA7`, score formulas, input transport, free-tap handlers, the CSV exporter,
or NESYS protocol code.

## Task 1: Add the required launch setting

Files:

- `config.toml`
- `src/Config/ConfigDocument.h`
- `src/Config/ConfigCompiler.h`
- `src/Config/ConfigCompiler.cpp`
- `tools/ConfigGUI/Main.cpp`

Actions:

1. Add required `[experimental].enable_auto_play = false` to the distributed
   configuration.
2. Carry it through `ExperimentalConfig` into value-owned
   `ValidatedConfig::enable_auto_play()`.
3. Add one ConfigGUI checkbox and a warning that auto play disables score
   saving and displays a permanent marker.
4. Keep the setting game-only. Do not add it to `NesysProcessConfiguration` or
   any hot-reload path.

## Task 2: Add the fixed marker producer

Files:

- `src/Patches/AutoPlay/AutoPlayMarker.h`
- `src/Patches/AutoPlay/AutoPlayMarker.cpp`
- `src/Patches/CMakeLists.txt`

Actions:

1. Draw exactly four native-text calls per active frame:
   - black shadow at `(34, 34)`: `AUTO PLAY`;
   - black shadow at `(34, 54)`: `SCORE SAVE DISABLED`;
   - yellow foreground at `(32, 32)`: `AUTO PLAY`;
   - yellow foreground at `(32, 52)`: `SCORE SAVE DISABLED`.
2. Emit nothing until the complete patch transaction publishes active state.
3. Keep the callback allocation-free, configuration-free, and logging-free on
   success.
4. Contain native text-call faults at the leaf call boundary and publish one
   fatal error if the mandatory marker can no longer be produced.

## Task 3: Install the five-site transaction

Files:

- `src/Patches/AutoPlay/AutoPlayPatch.cpp`
- `src/Patches/CMakeLists.txt`

Actions:

1. When disabled, return before resolving the game module or touching native
   memory.
2. Resolve the game image base with checked address arithmetic.
3. Read all five contracts before any mutation. Direct sites accept only their
   exact clean or exact patched bytes; the hook seam and call target accept
   only their exact native bytes.
4. Reject an unknown form with the site, RVA, expected bytes, actual bytes, and
   underlying memory error where applicable.
5. Install the dormant marker hook first.
6. Write owned clean sites in this order: do-not-save, `+0xA6`, then `+0xA5`.
7. If an operation fails, reset the hook and restore only writes performed by
   this invocation, in reverse order. Preserve both the initiating failure and
   any rollback failure in diagnostics.
8. Publish native text target and marker-active state only after every required
   operation succeeds. A repeated call after commit is a no-op.

## Task 4: Bind production memory, hook, diagnostics, and startup

Files:

- `src/Patches/AutoPlay/AutoPlayPatch.h`
- `src/Patches/AutoPlay/AutoPlayPatch.cpp`
- `src/Patches/AutoPlay/AutoPlayPatchDiagnostics.h`
- `src/Patches/AutoPlay/AutoPlayPatchDiagnostics.cpp`
- `src/Patches/CMakeLists.txt`
- `src/Loader/DllMain.cpp`

Actions:

1. Bind the transaction to the existing guarded game-binary memory actions and
   one feature-owned SafetyHook mid-hook.
2. Keep hook and native-text ownership in the auto-play runtime for the process
   lifetime.
3. Format startup failures with stage, site, RVA, expected/actual bytes,
   operation error, and rollback outcome. Use title
   `GCLoader auto play setup failed` and fail game-process attach when enabled
   setup cannot guarantee both no-save and the marker.
4. Initialize only in the game branch of `DllMain`, after validated input
   configuration is available and before `SongUnlockPatchInit`.
5. Do not call the initializer from the NESYS branch.

## Task 5: Direct analysis and build evidence

Saved scripts:

- `.codex-tmp/ida_autoplay_patch_contract.py`
- `.codex-tmp/ida_autoplay_mute_audio_closure.py`
- `.codex-tmp/ida_autoplay_grade_state.py`
- `.codex-tmp/ida_debug_text_outer_frame_trace.py`

Run each script as its own process:

```powershell
python .codex-tmp\ida_autoplay_patch_contract.py
python .codex-tmp\ida_autoplay_mute_audio_closure.py
python .codex-tmp\ida_autoplay_grade_state.py
python .codex-tmp\ida_debug_text_outer_frame_trace.py
```

Read the results for the exact native contract in this plan. If the actual
RVA, bytes, instruction meaning, ABI, ownership, or control flow differs, stop
and report the discrepancy rather than inventing replacement values.

Build the affected x86 targets in an x86 MSVC Developer PowerShell:

```powershell
$env:GC_ASIO_SDK_DIR='H:\gc\artifacts\ASIOSDK'
cmake --preset msvc32-debug
cmake --build --preset msvc32-debug --target iDmacDrv32 ConfigGUI
cmake --preset msvc32-release
cmake --build --preset msvc32-release --target iDmacDrv32 ConfigGUI
```

Finish with source hygiene only:

```powershell
git diff --check
git status --short
```

Do not run CTest for this feature and do not create a substitute verifier or
artifact scanner.

## Runtime acceptance requiring separate authorization

Only an authorized real-game run can establish:

1. Disabled mode leaves normal input, free taps, saving, and marker absence
   unchanged.
2. Enabled mode ignores gameplay input while pause, service, and exit remain
   usable.
3. Taps, holds, slides, scratches, paired components, and HIDDEN/AD-LIB content
   receive native GREAT results at authored timestamps.
4. HIDDEN/AD-LIB arrangement sounds remain audible and ignored physical input
   creates no generic free-tap sound.
5. Both marker lines stay visible through gameplay and results, including
   windowed widescreen output.
6. No result CSV, normal finish-game save transaction, or card/server state
   mutation occurs.
7. Restarting with the setting disabled removes the marker and restores normal
   saving.

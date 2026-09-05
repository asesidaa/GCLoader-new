# Loader Cleanup Baseline

## Repository and Toolchain

Captured 2026-09-05 in `H:\gc\artifacts\GCLoader` before cleanup.

| Item | Evidence |
|---|---|
| Branch | `main`, ahead of `origin/main` by 203 commits |
| Source commit | `cee730504c25e16428e540635c3b6226379e6faa` |
| Commit date and subject | `2026-09-05T06:27:54+08:00 Add loader cleanup implementation plans` |
| Initial worktree | Clean; no pre-existing modified or untracked paths |
| Checkout | Main checkout; Git directory and common directory both `.git`; no superproject |
| CMake / Ninja | `4.2.0-rc1` / `1.12.0` |
| Compiler | MSVC `19.51.36256.0`, Visual Studio 18 Insiders, tool directory `14.51.36231` |
| Target / language / CRT | Windows x86, C++23, static `/MTd` Debug and `/MT` RelWithDebInfo |
| Presets | `msvc32-debug` (Debug), `msvc32-release` (RelWithDebInfo), Ninja |
| ASIO SDK | `H:/gc/artifacts/ASIOSDK`, `2.3.4` |

The initial Debug cache named `cl`; the initial Release cache named
`Hostx64/x86/cl.exe`. Configuring both presets from `vcvars32.bat` regenerated
the compiler descriptions with `Hostx86/x86/cl.exe`. CMake's compiler-change
restart reset the Release build type to Debug, so that first Release-directory
build is not Release proof. `cmake --fresh --preset msvc32-release` corrected
this: the final cache is `RelWithDebInfo` and Ninja flags contain `/O2 /DNDEBUG
-MT -Zi`. Neither cache was hand-edited.

| Dependency | Configured revision |
|---|---|
| MinHook (removal baseline) | `c3fcafdc10146beb5919319d0683e44e3c30d537` |
| miniaudio | `9634bedb5b5a2ca38c1ee7108a9358a4e233f14d` |
| tomlplusplus | `v3.4.0` |
| SafetyHook | `v0.7.0` |
| reflect-cpp | `v0.25.0` |
| plog | `1.1.11` |
| ImGui | `v1.92.8` |
| Zydis | `v4.1.0` |

Evidence: root `CMakeLists.txt:18-52`, `cmake/ProjectOptions.cmake`, preset
caches and generated compiler descriptions. Dependency configure output has
existing CMake minimum-version deprecation warnings for Zydis/Zycore.

## Build and Test Baseline

Static evidence only. No game/NESYS launch, deployment, or runtime configuration
change is part of this baseline.

| Preset | Configure | Complete build | CTest inventory | CTest result |
|---|---|---|---|---|
| Debug | Exit 0 | Exit 0, 448 build steps | 5 tests | 5/5 passed, exit 0 |
| Release | Exit 0 after fresh configure | Exit 0, 448 build steps | Same 5 tests | 5/5 passed, exit 0 |

Exact test order in both presets from `ctest --preset msvc32-debug -N`:

1. `ExactWasapiClockCompatibility`
2. `ExactHistoryIsolation`
3. `ImeSuppression`
4. `ConfigContract`
5. `ConfigStartup`

## iDmac Export ABI

The `.def` supplies ordinals 1 through 14 and has no `LIBRARY` directive.
The CMake DLL target and both built export directories supply `iDmacDrv32.dll`.
`src/Driver/iDmac/iDmacDrv32.cpp` also exports `iDmacDrvProgramDownload` through
`extern "C" __declspec(dllexport)`, with a linker-assigned ordinal 15 in both builds.
Every exported source entry uses `__cdecl`; the existing extra export must be
preserved along with all fourteen explicitly assigned ordinals.

| Ordinal | Export | Source ordinal |
|---:|---|---|
| 1 | `iDmacDrvOpen` | 1 |
| 2 | `iDmacDrvClose` | 2 |
| 3 | `iDmacDrvDmaRead` | 3 |
| 4 | `iDmacDrvDmaWrite` | 4 |
| 5 | `iDmacDrvRegisterRead` | 5 |
| 6 | `iDmacDrvRegisterWrite` | 6 |
| 7 | `iDmacDrvRegisterBufferRead` | 7 |
| 8 | `iDmacDrvRegisterBufferWrite` | 8 |
| 9 | `iDmacDrvMemoryRead` | 9 |
| 10 | `iDmacDrvMemoryWrite` | 10 |
| 11 | `iDmacDrvMemoryBufferRead` | 11 |
| 12 | `iDmacDrvMemoryBufferWrite` | 12 |
| 13 | `iDmacDrvMemoryReadExt` | 13 |
| 14 | `iDmacDrvMemoryWriteExt` | 14 |
| 15 | `iDmacDrvProgramDownload` | Unspecified, `dllexport` at `.cpp:172` |

| PE property | Debug | Release |
|---|---|---|
| Machine | `14C` (x86) | `14C` (x86) |
| File characteristics | `2102`: executable, 32-bit, DLL | `2102`: executable, 32-bit, DLL |
| Optional magic | `10B` (PE32) | `10B` (PE32) |
| Subsystem | 2, Windows GUI | 2, Windows GUI |
| DLL characteristics | `140`: dynamic base, NX compatible | `140`: dynamic base, NX compatible |
| Preferred image base | `10000000` | `10000000` |
| Export count / ordinal base | 15 / 1 | 15 / 1 |

Full ordered `dumpbin /exports` name tables (all are local code exports;
no forwarders). RVAs are build-layout evidence, not fixed ABI requirements.

| Ordinal | Hint | Debug RVA | Release RVA | Name |
|---:|---|---|---|---|
| `2` | `0` | `00002FDB` | `0000314D` | `iDmacDrvClose` |
| `3` | `1` | `00015EBF` | `00017003` | `iDmacDrvDmaRead` |
| `4` | `2` | `00008837` | `00008EB3` | `iDmacDrvDmaWrite` |
| `11` | `3` | `00004CD7` | `0000501F` | `iDmacDrvMemoryBufferRead` |
| `12` | `4` | `0001CE40` | `0001E54C` | `iDmacDrvMemoryBufferWrite` |
| `9` | `5` | `00005911` | `00005CF4` | `iDmacDrvMemoryRead` |
| `13` | `6` | `0001DE1C` | `0001F64A` | `iDmacDrvMemoryReadExt` |
| `10` | `7` | `0002080B` | `000222BE` | `iDmacDrvMemoryWrite` |
| `14` | `8` | `0001D318` | `0001EA79` | `iDmacDrvMemoryWriteExt` |
| `1` | `9` | `000156A9` | `000167C5` | `iDmacDrvOpen` |
| `15` | `A` | `0000CA54` | `0000D46D` | `iDmacDrvProgramDownload` |
| `7` | `B` | `00014BF5` | `00015C8F` | `iDmacDrvRegisterBufferRead` |
| `8` | `C` | `0001C828` | `0001DF07` | `iDmacDrvRegisterBufferWrite` |
| `5` | `D` | `000038F5` | `00003B11` | `iDmacDrvRegisterRead` |
| `6` | `E` | `0001AE4C` | `0001C3F0` | `iDmacDrvRegisterWrite` |

## Versioned Runtime Sites

Source inventory for Plan 01 Task 4, taken from current production source on 2026-09-05. **All RVAs, byte/pointer states, and callback ABIs below are source-declared contracts only. Native/IDA validation remains later work; this document does not claim that any callback ABI is verified against the native executable.**

`Versioned = Yes` means the site belongs in the game/NESYS image contract. Export hooks are deliberately non-versioned and are listed separately. `Protected span` is the source-declared prefix/write/pointer span. SafetyHook inline hooks protect the declared prefix before installation; mid hooks use the declared instruction window. All installed owners below are process-lifetime unless a shorter lifetime is stated. Production source contains no `VmtHook` or `VmHook` usage; the two widescreen global vtable slots are installed directly with checked pointer replacement.

### Coverage

- 171 executable-image mutation/interception sites: 4 Game Compatibility, 4 Auto Play, 1 Song Unlock, 70 Framerate, 32 Countdown, 3 Switch Input, 10 Absolute Judgement, 3 Test Mode Timing executable sites, 6 Renderer Device Loss, 36 Windowed Widescreen, 1 ASIO ordinary-close, 1 NESYS ping redirect.
- 1 additional Test Mode Timing carrier-vtable construction contract (read-only source vtable; a copied per-object vtable is modified, not the native global vtable).
- 31 additional versioned read-only dependency/continuation contracts: Auto Play native debug text (1), Test Mode Timing native ABI helpers beyond the two installed hook targets (13), Renderer continuations (4), and Widescreen branch/helper/config/common-render dependencies (13). These are listed after installed sites and are not counted as executable-image mutations/interceptions.
- 74 distinct non-versioned module/export hook sites: 27 Kernel32/RFID-system-storage exports, 10 locale exports, crash filter, Raw Input registration, TTX init, DirectSound, and 33 NESYS export sites. NESYS hooks are installed separately in game and service processes where noted.

### Game Compatibility

| Process | Feature | Site | Versioned | Installation | Module/RVA | Protected span | Expected clean / installed state | Callback ABI | Enabled when | Current owner / lifetime | Evidence source |
|---|---|---|---|---|---|---:|---|---|---|---|---|
| Game | Game Compatibility | `native_mouse_events` | Yes | checked byte write | game + `0x000B0896` | 2 | `75 02` -> `90 90` | N/A | every game attach | no retained object; process-image mutation | `src/Patches/GameCompatibility/GameBinaryPatch.cpp:35-60,164-228`; `src/Loader/DllMain.cpp:540-552` |
| Game | Game Compatibility | `dongle_failure` | Yes | checked byte write | game + `0x00102C7B` | 2 | `75 3B` -> `EB 3B` | N/A | every game attach | same | same |
| Game | Game Compatibility | `dongle_security_transmit` | Yes | checked byte write | game + `0x00103EE6` | 5 | `E8 45 F6 FF FF` -> `90 90 90 90 90` | N/A | every game attach | same | same |
| Game | Game Compatibility | `rfid_com_port` | Yes | checked byte write | game + `0x002F7AC3` | 1 | `31` -> `32` | N/A | every game attach | same | same |

### Auto Play and Song Unlock

| Process | Feature | Site | Versioned | Installation | Module/RVA | Protected span | Expected clean / installed state | Callback ABI | Enabled when | Current owner / lifetime | Evidence source |
|---|---|---|---|---|---|---:|---|---|---|---|---|
| Game | Auto Play | `do_not_save_card_data` | Yes | checked byte write | game + `0x00269951` | 3 | `0F 95 C1` -> `B1 01 90` | N/A | `enable_auto_play` | `g_runtime` (`AutoPlayRuntime`); process lifetime | `src/Patches/AutoPlay/AutoPlayPatch.cpp:39-101,280-511`; `src/Loader/DllMain.cpp:685-690` |
| Game | Auto Play | `complete_is_mute` | Yes | checked byte write | game + `0x0003CAFA` | 6 | `8A 80 A6 00 00 00` -> `B0 01 90 90 90 90` | N/A | `enable_auto_play` | same | same |
| Game | Auto Play | `native_auto_play` | Yes | checked byte write | game + `0x0003CADA` | 6 | `8A 80 A5 00 00 00` -> `B0 01 90 90 90 90` | N/A | `enable_auto_play` | same | same |
| Game | Auto Play | `marker_seam` | Yes | SafetyHook mid | game + `0x00058BE9` | 10 | `8D 44 24 08 50 E8 8D 03 00 00` -> mid hook enabled | `void(safetyhook::Context&) noexcept` | `enable_auto_play` | same | `src/Patches/AutoPlay/AutoPlayPatch.cpp:205-255,469-479` |
| Game | Song Unlock | `unlock_all_songs_and_difficulties` | Yes | checked byte write | game + `0x00257854` | 6 | `0F 85 1D 02 00 00` -> `E9 1E 02 00 00 90` | N/A | `unlock_all_songs_and_difficulties` | no retained object; process-image mutation | `src/Patches/SongUnlock/SongUnlockPatch.cpp:22-38`; `src/Loader/DllMain.cpp:692-697` |

### Countdown Timer Freeze

All 32 calls are checked as `E8 rel32` targeting game RVA `0x002350C0`; enabled state is `D9 EE 90 90 90`. The mutation is reversible and owned by the global countdown-freeze state. It is initialized by Framerate and toggled when `timer_freeze_enabled` changes.

| Process | Feature | Site | Versioned | Installation | Module/RVA | Protected span | Expected clean / installed state | Callback ABI | Enabled when | Current owner / lifetime | Evidence source |
|---|---|---|---|---|---|---:|---|---|---|---|---|
| Game | Countdown | `delta_call_01` | Yes | checked reversible byte write | game + `0x00030322` | 5 | `E8 rel32 -> 0x002350C0` -> `D9 EE 90 90 90` | N/A | Framerate `timer_freeze_enabled` | global countdown state; process lifetime, reversible | `src/Patches/Countdown/CountdownTimerFreeze.h:9-54`; `src/Patches/Countdown/CountdownTimerFreeze.cpp:56-119`; `src/Patches/Framerate/FrameratePatch.cpp:2472-2474` |
| Game | Countdown | `delta_call_02` | Yes | checked reversible byte write | game + `0x00030340` | 5 | `E8 rel32 -> 0x002350C0` -> `D9 EE 90 90 90` | N/A | Framerate `timer_freeze_enabled` | global countdown state; process lifetime, reversible | `src/Patches/Countdown/CountdownTimerFreeze.h:9-54`; `src/Patches/Countdown/CountdownTimerFreeze.cpp:56-119`; `src/Patches/Framerate/FrameratePatch.cpp:2472-2474` |
| Game | Countdown | `delta_call_03` | Yes | checked reversible byte write | game + `0x001B45D2` | 5 | `E8 rel32 -> 0x002350C0` -> `D9 EE 90 90 90` | N/A | Framerate `timer_freeze_enabled` | global countdown state; process lifetime, reversible | `src/Patches/Countdown/CountdownTimerFreeze.h:9-54`; `src/Patches/Countdown/CountdownTimerFreeze.cpp:56-119`; `src/Patches/Framerate/FrameratePatch.cpp:2472-2474` |
| Game | Countdown | `delta_call_04` | Yes | checked reversible byte write | game + `0x001B45F1` | 5 | `E8 rel32 -> 0x002350C0` -> `D9 EE 90 90 90` | N/A | Framerate `timer_freeze_enabled` | global countdown state; process lifetime, reversible | `src/Patches/Countdown/CountdownTimerFreeze.h:9-54`; `src/Patches/Countdown/CountdownTimerFreeze.cpp:56-119`; `src/Patches/Framerate/FrameratePatch.cpp:2472-2474` |
| Game | Countdown | `delta_call_05` | Yes | checked reversible byte write | game + `0x001B4871` | 5 | `E8 rel32 -> 0x002350C0` -> `D9 EE 90 90 90` | N/A | Framerate `timer_freeze_enabled` | global countdown state; process lifetime, reversible | `src/Patches/Countdown/CountdownTimerFreeze.h:9-54`; `src/Patches/Countdown/CountdownTimerFreeze.cpp:56-119`; `src/Patches/Framerate/FrameratePatch.cpp:2472-2474` |
| Game | Countdown | `delta_call_06` | Yes | checked reversible byte write | game + `0x001B4890` | 5 | `E8 rel32 -> 0x002350C0` -> `D9 EE 90 90 90` | N/A | Framerate `timer_freeze_enabled` | global countdown state; process lifetime, reversible | `src/Patches/Countdown/CountdownTimerFreeze.h:9-54`; `src/Patches/Countdown/CountdownTimerFreeze.cpp:56-119`; `src/Patches/Framerate/FrameratePatch.cpp:2472-2474` |
| Game | Countdown | `delta_call_07` | Yes | checked reversible byte write | game + `0x001A6FB4` | 5 | `E8 rel32 -> 0x002350C0` -> `D9 EE 90 90 90` | N/A | Framerate `timer_freeze_enabled` | global countdown state; process lifetime, reversible | `src/Patches/Countdown/CountdownTimerFreeze.h:9-54`; `src/Patches/Countdown/CountdownTimerFreeze.cpp:56-119`; `src/Patches/Framerate/FrameratePatch.cpp:2472-2474` |
| Game | Countdown | `delta_call_08` | Yes | checked reversible byte write | game + `0x001A6FD3` | 5 | `E8 rel32 -> 0x002350C0` -> `D9 EE 90 90 90` | N/A | Framerate `timer_freeze_enabled` | global countdown state; process lifetime, reversible | `src/Patches/Countdown/CountdownTimerFreeze.h:9-54`; `src/Patches/Countdown/CountdownTimerFreeze.cpp:56-119`; `src/Patches/Framerate/FrameratePatch.cpp:2472-2474` |
| Game | Countdown | `delta_call_09` | Yes | checked reversible byte write | game + `0x001A83B4` | 5 | `E8 rel32 -> 0x002350C0` -> `D9 EE 90 90 90` | N/A | Framerate `timer_freeze_enabled` | global countdown state; process lifetime, reversible | `src/Patches/Countdown/CountdownTimerFreeze.h:9-54`; `src/Patches/Countdown/CountdownTimerFreeze.cpp:56-119`; `src/Patches/Framerate/FrameratePatch.cpp:2472-2474` |
| Game | Countdown | `delta_call_10` | Yes | checked reversible byte write | game + `0x001A83D3` | 5 | `E8 rel32 -> 0x002350C0` -> `D9 EE 90 90 90` | N/A | Framerate `timer_freeze_enabled` | global countdown state; process lifetime, reversible | `src/Patches/Countdown/CountdownTimerFreeze.h:9-54`; `src/Patches/Countdown/CountdownTimerFreeze.cpp:56-119`; `src/Patches/Framerate/FrameratePatch.cpp:2472-2474` |
| Game | Countdown | `delta_call_11` | Yes | checked reversible byte write | game + `0x001AEF84` | 5 | `E8 rel32 -> 0x002350C0` -> `D9 EE 90 90 90` | N/A | Framerate `timer_freeze_enabled` | global countdown state; process lifetime, reversible | `src/Patches/Countdown/CountdownTimerFreeze.h:9-54`; `src/Patches/Countdown/CountdownTimerFreeze.cpp:56-119`; `src/Patches/Framerate/FrameratePatch.cpp:2472-2474` |
| Game | Countdown | `delta_call_12` | Yes | checked reversible byte write | game + `0x001AEFA3` | 5 | `E8 rel32 -> 0x002350C0` -> `D9 EE 90 90 90` | N/A | Framerate `timer_freeze_enabled` | global countdown state; process lifetime, reversible | `src/Patches/Countdown/CountdownTimerFreeze.h:9-54`; `src/Patches/Countdown/CountdownTimerFreeze.cpp:56-119`; `src/Patches/Framerate/FrameratePatch.cpp:2472-2474` |
| Game | Countdown | `delta_call_13` | Yes | checked reversible byte write | game + `0x001BB104` | 5 | `E8 rel32 -> 0x002350C0` -> `D9 EE 90 90 90` | N/A | Framerate `timer_freeze_enabled` | global countdown state; process lifetime, reversible | `src/Patches/Countdown/CountdownTimerFreeze.h:9-54`; `src/Patches/Countdown/CountdownTimerFreeze.cpp:56-119`; `src/Patches/Framerate/FrameratePatch.cpp:2472-2474` |
| Game | Countdown | `delta_call_14` | Yes | checked reversible byte write | game + `0x001BB123` | 5 | `E8 rel32 -> 0x002350C0` -> `D9 EE 90 90 90` | N/A | Framerate `timer_freeze_enabled` | global countdown state; process lifetime, reversible | `src/Patches/Countdown/CountdownTimerFreeze.h:9-54`; `src/Patches/Countdown/CountdownTimerFreeze.cpp:56-119`; `src/Patches/Framerate/FrameratePatch.cpp:2472-2474` |
| Game | Countdown | `delta_call_15` | Yes | checked reversible byte write | game + `0x001C1805` | 5 | `E8 rel32 -> 0x002350C0` -> `D9 EE 90 90 90` | N/A | Framerate `timer_freeze_enabled` | global countdown state; process lifetime, reversible | `src/Patches/Countdown/CountdownTimerFreeze.h:9-54`; `src/Patches/Countdown/CountdownTimerFreeze.cpp:56-119`; `src/Patches/Framerate/FrameratePatch.cpp:2472-2474` |
| Game | Countdown | `delta_call_16` | Yes | checked reversible byte write | game + `0x001C1824` | 5 | `E8 rel32 -> 0x002350C0` -> `D9 EE 90 90 90` | N/A | Framerate `timer_freeze_enabled` | global countdown state; process lifetime, reversible | `src/Patches/Countdown/CountdownTimerFreeze.h:9-54`; `src/Patches/Countdown/CountdownTimerFreeze.cpp:56-119`; `src/Patches/Framerate/FrameratePatch.cpp:2472-2474` |
| Game | Countdown | `delta_call_17` | Yes | checked reversible byte write | game + `0x001C55F6` | 5 | `E8 rel32 -> 0x002350C0` -> `D9 EE 90 90 90` | N/A | Framerate `timer_freeze_enabled` | global countdown state; process lifetime, reversible | `src/Patches/Countdown/CountdownTimerFreeze.h:9-54`; `src/Patches/Countdown/CountdownTimerFreeze.cpp:56-119`; `src/Patches/Framerate/FrameratePatch.cpp:2472-2474` |
| Game | Countdown | `delta_call_18` | Yes | checked reversible byte write | game + `0x001C5615` | 5 | `E8 rel32 -> 0x002350C0` -> `D9 EE 90 90 90` | N/A | Framerate `timer_freeze_enabled` | global countdown state; process lifetime, reversible | `src/Patches/Countdown/CountdownTimerFreeze.h:9-54`; `src/Patches/Countdown/CountdownTimerFreeze.cpp:56-119`; `src/Patches/Framerate/FrameratePatch.cpp:2472-2474` |
| Game | Countdown | `delta_call_19` | Yes | checked reversible byte write | game + `0x001C6746` | 5 | `E8 rel32 -> 0x002350C0` -> `D9 EE 90 90 90` | N/A | Framerate `timer_freeze_enabled` | global countdown state; process lifetime, reversible | `src/Patches/Countdown/CountdownTimerFreeze.h:9-54`; `src/Patches/Countdown/CountdownTimerFreeze.cpp:56-119`; `src/Patches/Framerate/FrameratePatch.cpp:2472-2474` |
| Game | Countdown | `delta_call_20` | Yes | checked reversible byte write | game + `0x001C6765` | 5 | `E8 rel32 -> 0x002350C0` -> `D9 EE 90 90 90` | N/A | Framerate `timer_freeze_enabled` | global countdown state; process lifetime, reversible | `src/Patches/Countdown/CountdownTimerFreeze.h:9-54`; `src/Patches/Countdown/CountdownTimerFreeze.cpp:56-119`; `src/Patches/Framerate/FrameratePatch.cpp:2472-2474` |
| Game | Countdown | `delta_call_21` | Yes | checked reversible byte write | game + `0x00201C22` | 5 | `E8 rel32 -> 0x002350C0` -> `D9 EE 90 90 90` | N/A | Framerate `timer_freeze_enabled` | global countdown state; process lifetime, reversible | `src/Patches/Countdown/CountdownTimerFreeze.h:9-54`; `src/Patches/Countdown/CountdownTimerFreeze.cpp:56-119`; `src/Patches/Framerate/FrameratePatch.cpp:2472-2474` |
| Game | Countdown | `delta_call_22` | Yes | checked reversible byte write | game + `0x00201C41` | 5 | `E8 rel32 -> 0x002350C0` -> `D9 EE 90 90 90` | N/A | Framerate `timer_freeze_enabled` | global countdown state; process lifetime, reversible | `src/Patches/Countdown/CountdownTimerFreeze.h:9-54`; `src/Patches/Countdown/CountdownTimerFreeze.cpp:56-119`; `src/Patches/Framerate/FrameratePatch.cpp:2472-2474` |
| Game | Countdown | `delta_call_23` | Yes | checked reversible byte write | game + `0x00201E70` | 5 | `E8 rel32 -> 0x002350C0` -> `D9 EE 90 90 90` | N/A | Framerate `timer_freeze_enabled` | global countdown state; process lifetime, reversible | `src/Patches/Countdown/CountdownTimerFreeze.h:9-54`; `src/Patches/Countdown/CountdownTimerFreeze.cpp:56-119`; `src/Patches/Framerate/FrameratePatch.cpp:2472-2474` |
| Game | Countdown | `delta_call_24` | Yes | checked reversible byte write | game + `0x00201E8F` | 5 | `E8 rel32 -> 0x002350C0` -> `D9 EE 90 90 90` | N/A | Framerate `timer_freeze_enabled` | global countdown state; process lifetime, reversible | `src/Patches/Countdown/CountdownTimerFreeze.h:9-54`; `src/Patches/Countdown/CountdownTimerFreeze.cpp:56-119`; `src/Patches/Framerate/FrameratePatch.cpp:2472-2474` |
| Game | Countdown | `delta_call_25` | Yes | checked reversible byte write | game + `0x00201FD4` | 5 | `E8 rel32 -> 0x002350C0` -> `D9 EE 90 90 90` | N/A | Framerate `timer_freeze_enabled` | global countdown state; process lifetime, reversible | `src/Patches/Countdown/CountdownTimerFreeze.h:9-54`; `src/Patches/Countdown/CountdownTimerFreeze.cpp:56-119`; `src/Patches/Framerate/FrameratePatch.cpp:2472-2474` |
| Game | Countdown | `delta_call_26` | Yes | checked reversible byte write | game + `0x00201FF3` | 5 | `E8 rel32 -> 0x002350C0` -> `D9 EE 90 90 90` | N/A | Framerate `timer_freeze_enabled` | global countdown state; process lifetime, reversible | `src/Patches/Countdown/CountdownTimerFreeze.h:9-54`; `src/Patches/Countdown/CountdownTimerFreeze.cpp:56-119`; `src/Patches/Framerate/FrameratePatch.cpp:2472-2474` |
| Game | Countdown | `delta_call_27` | Yes | checked reversible byte write | game + `0x00204FB6` | 5 | `E8 rel32 -> 0x002350C0` -> `D9 EE 90 90 90` | N/A | Framerate `timer_freeze_enabled` | global countdown state; process lifetime, reversible | `src/Patches/Countdown/CountdownTimerFreeze.h:9-54`; `src/Patches/Countdown/CountdownTimerFreeze.cpp:56-119`; `src/Patches/Framerate/FrameratePatch.cpp:2472-2474` |
| Game | Countdown | `delta_call_28` | Yes | checked reversible byte write | game + `0x00204FD5` | 5 | `E8 rel32 -> 0x002350C0` -> `D9 EE 90 90 90` | N/A | Framerate `timer_freeze_enabled` | global countdown state; process lifetime, reversible | `src/Patches/Countdown/CountdownTimerFreeze.h:9-54`; `src/Patches/Countdown/CountdownTimerFreeze.cpp:56-119`; `src/Patches/Framerate/FrameratePatch.cpp:2472-2474` |
| Game | Countdown | `delta_call_29` | Yes | checked reversible byte write | game + `0x002078A3` | 5 | `E8 rel32 -> 0x002350C0` -> `D9 EE 90 90 90` | N/A | Framerate `timer_freeze_enabled` | global countdown state; process lifetime, reversible | `src/Patches/Countdown/CountdownTimerFreeze.h:9-54`; `src/Patches/Countdown/CountdownTimerFreeze.cpp:56-119`; `src/Patches/Framerate/FrameratePatch.cpp:2472-2474` |
| Game | Countdown | `delta_call_30` | Yes | checked reversible byte write | game + `0x002078C2` | 5 | `E8 rel32 -> 0x002350C0` -> `D9 EE 90 90 90` | N/A | Framerate `timer_freeze_enabled` | global countdown state; process lifetime, reversible | `src/Patches/Countdown/CountdownTimerFreeze.h:9-54`; `src/Patches/Countdown/CountdownTimerFreeze.cpp:56-119`; `src/Patches/Framerate/FrameratePatch.cpp:2472-2474` |
| Game | Countdown | `delta_call_31` | Yes | checked reversible byte write | game + `0x0020B124` | 5 | `E8 rel32 -> 0x002350C0` -> `D9 EE 90 90 90` | N/A | Framerate `timer_freeze_enabled` | global countdown state; process lifetime, reversible | `src/Patches/Countdown/CountdownTimerFreeze.h:9-54`; `src/Patches/Countdown/CountdownTimerFreeze.cpp:56-119`; `src/Patches/Framerate/FrameratePatch.cpp:2472-2474` |
| Game | Countdown | `delta_call_32` | Yes | checked reversible byte write | game + `0x0020B143` | 5 | `E8 rel32 -> 0x002350C0` -> `D9 EE 90 90 90` | N/A | Framerate `timer_freeze_enabled` | global countdown state; process lifetime, reversible | `src/Patches/Countdown/CountdownTimerFreeze.h:9-54`; `src/Patches/Countdown/CountdownTimerFreeze.cpp:56-119`; `src/Patches/Framerate/FrameratePatch.cpp:2472-2474` |

### Framerate — Direct Writes

All rows are enabled only for transformed timing (`target_fps != native 60`). Values named “computed” come from the selected `FramerateProfile`. The transaction owns the writes and `g_runtime` owns retained state for process lifetime.

| Process | Feature | Site | Versioned | Installation | Module/RVA | Protected span | Expected clean / installed state | Callback ABI | Enabled when | Current owner / lifetime | Evidence source |
|---|---|---|---|---|---|---:|---|---|---|---|---|
| Game | Framerate | `gameplay_frame_milliseconds` | Yes | checked byte write | game + `0x002FC0A0` | 4 | IEEE-754 `1000/60` -> computed frame ms | N/A | transformed timing | Framerate transaction / `g_runtime`; process | `src/Patches/Framerate/FrameratePatchPlan.cpp:187-312`; `src/Patches/Framerate/FrameratePatch.cpp:2351-2475` |
| Game | Framerate | `visual_frame_milliseconds` | Yes | checked byte write | game + `0x002F4604` | 4 | IEEE-754 `1000/60` -> computed frame ms | N/A | transformed timing | same | same |
| Game | Framerate | `gameplay_frame_seconds` | Yes | checked byte write | game + `0x002FC280` | 4 | IEEE-754 `1/60` -> computed frame seconds | N/A | transformed timing | same | same |
| Game | Framerate | `render_smoothing_step` | Yes | checked byte write | game + `0x002E8F00` | 4 | float `4.0` -> computed smoothing step | N/A | transformed timing | same | same |
| Game | Framerate | `render_offset_decay_step` | Yes | checked byte write | game + `0x002E8F04` | 4 | float `5.0` -> computed decay step | N/A | transformed timing | same | same |
| Game | Framerate | `xio_repeat_initial` | Yes | checked byte write | game + `0x00055CCC` | 6 | `C7 00 10 00 00 00` -> scaled initial repeat | N/A | transformed timing | same | same |
| Game | Framerate | `xio_repeat_next` | Yes | checked byte write | game + `0x00055CDD` | 6 | `C7 00 08 00 00 00` -> scaled next repeat | N/A | transformed timing | same | same |
| Game | Framerate | `native_keyboard_repeat_initial` | Yes | checked byte write | game + `0x0005F843` | 10 | `C7 86 D4 02 00 00 10 00 00 00` -> scaled initial repeat | N/A | transformed timing | same | same |
| Game | Framerate | `native_keyboard_repeat_next` | Yes | checked byte write | game + `0x0005F84D` | 10 | `C7 86 D8 02 00 00 08 00 00 00` -> scaled next repeat | N/A | transformed timing | same | same |
| Game | Framerate | `gameplay_countdown_duration` | Yes | checked byte write | game + `0x002645EE` | 10 | `C7 80 14 1D 00 00 78 00 00 00` -> computed 2-second frame count | N/A | transformed timing | same | same |
| Game | Framerate | `render_eax_countdown` | Yes | checked byte write | game + `0x00249A5E` | 5 | `B8 78 00 00 00` -> computed countdown | N/A | transformed timing | same | same |
| Game | Framerate | `render_edx_countdown` | Yes | checked byte write | game + `0x00249A73` | 5 | `BA 78 00 00 00` -> computed countdown | N/A | transformed timing | same | same |
| Game | Framerate | `palette_normalizer_operand_one` | Yes | checked byte write | game + `0x0022BACF` | 6 | `D8 2D AC BB 6F 00` -> target-rate operand address | N/A | transformed timing | same | same |
| Game | Framerate | `palette_normalizer_operand_two` | Yes | checked byte write | game + `0x0022BAD5` | 6 | `D8 35 AC BB 6F 00` -> target-rate operand address | N/A | transformed timing | same | same |
| Game | Framerate | `chart_seconds_to_frames_operand` | Yes | checked byte write | game + `0x00262CB6` | 6 | `D8 0D AC BB 6F 00` -> target-rate operand address | N/A | transformed timing | same | same |
| Game | Framerate | `non_song_menu_repeat_initial` | Yes | checked byte write | game + `0x00382CE8` | 4 | uint32 `16` -> scaled initial repeat | N/A | transformed timing | same | same |
| Game | Framerate | `non_song_menu_repeat_interval` | Yes | checked byte write | game + `0x00382CEC` | 4 | uint32 `3` -> scaled interval | N/A | transformed timing | same | same |

### Framerate — Hook Sites

Enable-condition codes: **T** = transformed timing. **S** = shared-song-clock plan (`WasapiSharedSongClock` or `AsioQpcSongClock`). **L** = `WasapiLegacyResync`. **O** = always. Ordinary T sites are selected for OriginalWatchdog and Legacy plans and, except the three legacy audio sites, shared-clock plans. The seven shared gameplay consumers (`gameplay_effect_advance`, cadence 6/5/4/16A/16B/8) are also selected for S at native 60. `gameplay_song_clock` is S only; `audio_resync_policy` is L; `audio_skip_margin/interval` are T with non-shared clock; `outer_frame` is O.

| Process | Feature | Site | Versioned | Installation | Module/RVA | Protected span | Expected clean state | Callback ABI | Enabled when | Current owner / lifetime | Evidence source |
|---|---|---|---|---|---|---:|---|---|---|---|---|
| Game | Framerate | `movie_clip_goto` | Yes | SafetyHook inline | game + `0x000DEA30` | 7 | `6A FF 68 C9 38 67 00` | `char __fastcall(void*,void*,int,int)` | T | `FramerateHookStorage` in `g_runtime`; process | `src/Patches/Framerate/FrameratePatchPlan.cpp:71-123,327-361`; `src/Patches/Framerate/FrameratePatch.cpp:225-277,395-1021` |
| Game | Framerate | `movie_clip_advance` | Yes | SafetyHook inline | game + `0x000DF940` | 11 | `56 8B F1 8B 06 8B 90 4C 01 00 00` | `char __fastcall(void*,void*,char,char)` | T | same | same |
| Game | Framerate | `palette_target_rate_compare` | Yes | SafetyHook mid | game + `0x0022BA60` | 4 | `83 78 0C 3C` | `void(Context&)` | T | same | same |
| Game | Framerate | `stage_clip_frame_mapping` | Yes | SafetyHook mid | game + `0x00244054` | 3 | `89 4D F8` | `void(Context&)` | T | same | same |
| Game | Framerate | `ifbl_wait_duration` | Yes | SafetyHook mid | game + `0x002309D4` | 3 | `89 4A 3C` | `void(Context&)` | T | same | same |
| Game | Framerate | `stage_bgm_preload` | Yes | SafetyHook mid | game + `0x0021001A` | 3 | `83 C0 01` | `void(Context&)` | T | same | same |
| Game | Framerate | `tune_countdown_compare` | Yes | SafetyHook mid | game + `0x002648F7` | 7 | `83 BA 14 1D 00 00 78` | `void(Context&)` | T | same | same |
| Game | Framerate | `audio_skip_margin` | Yes | SafetyHook mid | game + `0x0024018F` | 3 | `8B 45 F4` | `void(Context&)` | T, non-shared clock | same | same |
| Game | Framerate | `audio_skip_interval` | Yes | SafetyHook mid | game + `0x002401BD` | 3 | `F7 79 3C` | `void(Context&)` | T, non-shared clock | same | same |
| Game | Framerate | `audio_resync_policy` | Yes | SafetyHook mid | game + `0x002401C4` | 21 | `8B 55 F8 52 E8 33 02 FD FF 8B C8 E8 2C 12 FD FF 5E 8B E5 5D C3` | `void(Context&)` | L | same | same |
| Game | Framerate | `gameplay_song_clock` | Yes | SafetyHook mid | game + `0x00264DB2` | 5 | `E8 B9 B2 FD FF` | `void(Context&)` | S | same | same |
| Game | Framerate | `gameplay_effect_advance` | Yes | SafetyHook mid | game + `0x00264E2D` | 5 | `E8 6E BA F8 FF` | `void(Context&)` | T or S | same | `src/Patches/Framerate/FramerateEffectTiming.cpp:153-256`; selection above |
| Game | Framerate | `effect_cadence_6` | Yes | SafetyHook mid | game + `0x0024063B` | 2 | `85 D2` | `void(Context&)` | T or S | same | same |
| Game | Framerate | `effect_cadence_5` | Yes | SafetyHook mid | game + `0x002408D7` | 2 | `85 D2` | `void(Context&)` | T or S | same | same |
| Game | Framerate | `effect_cadence_4` | Yes | SafetyHook mid | game + `0x00240C9C` | 2 | `85 D2` | `void(Context&)` | T or S | same | same |
| Game | Framerate | `effect_cadence_16_a` | Yes | SafetyHook mid | game + `0x00241213` | 2 | `85 D2` | `void(Context&)` | T or S | same | same |
| Game | Framerate | `effect_cadence_16_b` | Yes | SafetyHook mid | game + `0x0024122F` | 6 | `81 E1 0F 00 00 80` | `void(Context&)` | T or S | same | same |
| Game | Framerate | `effect_cadence_8` | Yes | SafetyHook mid | game + `0x00241268` | 2 | `85 C0` | `void(Context&)` | T or S | same | same |
| Game | Framerate | `remote_effect_cadence_4_a` | Yes | SafetyHook mid | game + `0x002632DB` | 2 | `85 D2` | `void(Context&)` | T | same | same |
| Game | Framerate | `remote_effect_cadence_4_b` | Yes | SafetyHook mid | game + `0x00263646` | 2 | `85 D2` | `void(Context&)` | T | same | same |
| Game | Framerate | `gameplay_blink_frame` | Yes | SafetyHook mid | game + `0x0024A1B9` | 2 | `D1 F8` | `void(Context&)` | T | same | same |
| Game | Framerate | `great_good_lifetime_ms` | Yes | SafetyHook mid | game + `0x002464A8` | 3 | `D8 48 18` | `void(Context&)` | T | same | same |
| Game | Framerate | `great_good_frame_ms` | Yes | SafetyHook mid | game + `0x00246528` | 3 | `D8 71 18` | `void(Context&)` | T | same | same |
| Game | Framerate | `effect_lifetime_a` | Yes | SafetyHook mid | game + `0x00248F00` | 3 | `D8 49 18` | `void(Context&)` | T | same | same |
| Game | Framerate | `effect_frame_a` | Yes | SafetyHook mid | game + `0x00248F8C` | 3 | `D8 72 18` | `void(Context&)` | T | same | same |
| Game | Framerate | `effect_lifetime_b` | Yes | SafetyHook mid | game + `0x0024912B` | 3 | `D8 49 18` | `void(Context&)` | T | same | same |
| Game | Framerate | `effect_frame_b` | Yes | SafetyHook mid | game + `0x002491E0` | 3 | `D8 72 18` | `void(Context&)` | T | same | same |
| Game | Framerate | `direct_effect_frame` | Yes | SafetyHook mid | game + `0x00249C14` | 3 | `D8 72 18` | `void(Context&)` | T | same | same |
| Game | Framerate | `chart_effect_frame_a` | Yes | SafetyHook mid | game + `0x0024BC8B` | 3 | `D8 71 18` | `void(Context&)` | T | same | same |
| Game | Framerate | `chart_effect_frame_b` | Yes | SafetyHook mid | game + `0x0024CC8A` | 3 | `D8 71 18` | `void(Context&)` | T | same | same |
| Game | Framerate | `chart_effect_frame_c` | Yes | SafetyHook mid | game + `0x0024CCBE` | 3 | `D8 72 18` | `void(Context&)` | T | same | same |
| Game | Framerate | `chart_effect_frame_d` | Yes | SafetyHook mid | game + `0x0024D836` | 3 | `D8 70 18` | `void(Context&)` | T | same | same |
| Game | Framerate | `fixed_visual_frame` | Yes | SafetyHook mid | game + `0x00250AD5` | 3 | `D8 71 18` | `void(Context&)` | T | same | same |
| Game | Framerate | `countdown_asset_frame` | Yes | SafetyHook mid | game + `0x00249A9C` | 3 | `89 48 08` | `void(Context&)` | T | same | same |
| Game | Framerate | `player_position_init_a` | Yes | SafetyHook mid | game + `0x00263240` | 7 | `89 84 91 54 1D 00 00` | `void(Context&)` | T | same | same |
| Game | Framerate | `player_position_init_b` | Yes | SafetyHook mid | game + `0x002632B2` | 7 | `89 84 8A 54 1D 00 00` | `void(Context&)` | T | same | same |
| Game | Framerate | `player_position_init_c` | Yes | SafetyHook mid | game + `0x0026359B` | 7 | `89 84 91 54 1D 00 00` | `void(Context&)` | T | same | same |
| Game | Framerate | `player_position_init_d` | Yes | SafetyHook mid | game + `0x00263615` | 7 | `89 84 8A 54 1D 00 00` | `void(Context&)` | T | same | same |
| Game | Framerate | `player_position_asset_frame` | Yes | SafetyHook mid | game + `0x0024EF43` | 7 | `2B 84 8A 54 1D 00 00` | `void(Context&)` | T | same | same |
| Game | Framerate | `player_position_denominator_a` | Yes | SafetyHook mid | game + `0x0024F76D` | 6 | `DB 80 C4 00 00 00` | `void(Context&)` | T | same | same |
| Game | Framerate | `player_position_denominator_b` | Yes | SafetyHook mid | game + `0x0024FD40` | 6 | `DB 80 C4 00 00 00` | `void(Context&)` | T | same | same |
| Game | Framerate | `effect_flow_item_frame` | Yes | SafetyHook mid | game + `0x001F0310` | 3 | `89 42 08` | `void(Context&)` | T | same | same |
| Game | Framerate | `tutorial_shared_elapsed` | Yes | SafetyHook mid | game + `0x00249593` | 6 | `89 95 74 FF FF FF` | `void(Context&)` | T | same | same |
| Game | Framerate | `chart_preroll_duration` | Yes | SafetyHook mid | game + `0x0024A934` | 3 | `89 45 9C` | `void(Context&)` | T | same | same |
| Game | Framerate | `player_effect_modulo_dividend` | Yes | SafetyHook mid | game + `0x0025072E` | 2 | `F7 F9` | `void(Context&)` | T | same | same |
| Game | Framerate | `movie_clip_preprocess_scope` | Yes | SafetyHook inline | game + `0x000EFB90` | 7 | `6A FF 68 10 49 67 00` | `void __fastcall(void*,void*,int)` | T | same | `src/Patches/Framerate/FramerateMenuTiming.cpp:22-80`; `src/Patches/Framerate/FrameratePatch.cpp:395-1021` |
| Game | Framerate | `ranking_entry_counter` | Yes | SafetyHook mid | game + `0x00216EB4` | 5 | `8B 4D E0 89 01` | `void(Context&)` | T | same | same |
| Game | Framerate | `hit_chart_entry_counter` | Yes | SafetyHook mid | game + `0x0026562F` | 6 | `8B 8D 6C FF FF FF` | `void(Context&)` | T | same | same |
| Game | Framerate | `unlock_reward_countdown` | Yes | SafetyHook mid | game + `0x00030DA3` | 6 | `89 90 6C 37 00 00` | `void(Context&)` | T | same | same |
| Game | Framerate | `unlock_reward_primary` | Yes | SafetyHook mid | game + `0x00030E54` | 6 | `89 81 D4 37 00 00` | `void(Context&)` | T | same | same |
| Game | Framerate | `unlock_reward_secondary` | Yes | SafetyHook mid | game + `0x00030F23` | 6 | `89 90 D4 37 00 00` | `void(Context&)` | T | same | same |
| Game | Framerate | `navigator_advance` | Yes | SafetyHook inline | game + `0x001B6310` | 15 | `55 8B EC 83 EC 08 89 4D FC 8B 45 FC 8B 48 60` | `void* __fastcall(void*,void*)` | T | same | `src/Patches/Framerate/FrameratePatchPlan.cpp:71-123`; `src/Patches/Framerate/FrameratePatch.cpp:395-1021` |
| Game | Framerate | `outer_frame` | Yes | SafetyHook mid | game + `0x00058B70` | 8 | `56 8B F1 8B 06 8B 50 24` | `void(Context&)` | O | same | same |

### Switch Input

| Process | Feature | Site | Versioned | Installation | Module/RVA | Protected span | Expected clean state | Callback ABI | Enabled when | Current owner / lifetime | Evidence source |
|---|---|---|---|---|---|---:|---|---|---|---|---|
| Game | Switch Input | `pressed_edge` | Yes | SafetyHook inline | game + `0x00259640` | 16 | `55 8B EC 83 EC 18 89 4D EC C6 45 FF 00 8B 4D EC` | `uint8_t __fastcall(void*,void*,int input_device_id,int logical_input,int gameplay_frame)`; original thiscall | input style Switch | global `g_pressed_edge_hook`; process | `src/Patches/Switch/SwitchInputPatch.h:15-26`; `src/Patches/Switch/SwitchInputPatch.cpp:285-317,408-516` |
| Game | Switch Input | `held_state` | Yes | SafetyHook inline | game + `0x00259570` | 16 | `55 8B EC 83 EC 18 89 4D EC C6 45 FF 00 8B 4D EC` | same | input style Switch | global `g_held_state_hook`; process | same |
| Game | Switch Input | `diagonal_match` | Yes | SafetyHook mid | game + `0x001D32A0` | 9 | `0F B6 55 8B 83 FA 01 75 2B` | `void(Context&)` | input style Switch | global `g_diagonal_match_hook`; process | same |

### Absolute Judgement

| Process | Feature | Site | Versioned | Installation | Module/RVA | Protected span | Expected clean state | Callback ABI | Enabled when | Current owner / lifetime | Evidence source |
|---|---|---|---|---|---|---:|---|---|---|---|---|
| Game | Absolute Judgement lifecycle | `gameplay_initialization` | Yes | SafetyHook mid | game + `0x0026251C` | 8 | `89 4D 80 E8 2C 60 F0 FF` | `void(Context&)` | Absolute Judgement or ASIO | global `AbsoluteJudgementHooks g_hooks`; process | `src/Patches/AbsoluteJudgement/NativeJudgementAbi.h:12-62`; `src/Patches/AbsoluteJudgement/AbsoluteJudgementPatch.cpp:54-105,359-568,776-836` |
| Game | Absolute Judgement lifecycle | `semantic_stage_entry` | Yes | SafetyHook mid | game + `0x002641CC` | 13 | `8B 8D 4C FD FF FF C7 41 10 00 00 00 00` | `void(Context&)` | Absolute Judgement or ASIO | same | same |
| Game | Absolute Judgement lifecycle | `semantic_stage_exit` | Yes | SafetyHook mid | game + `0x00264D9A` | 13 | `8B 95 4C FD FF FF C7 42 04 13 00 00 00` | `void(Context&)` | Absolute Judgement or ASIO | same | same |
| Game | Absolute Judgement | `loop_guard` | Yes | SafetyHook mid | game + `0x00240239` | 6 | `0F 8E 91 00 00 00` | `void(Context&)` | Absolute Judgement | same | same |
| Game | Absolute Judgement | `pressed` | Yes | SafetyHook inline | game + `0x0022DFB0` | 16 | `55 8B EC 83 EC 28 89 4D D8 C6 45 FF 00 8B 4D D8` | `uint8_t __fastcall(void*,void*,int id,int frame)`; original thiscall | Absolute Judgement | same | same |
| Game | Absolute Judgement | `held` | Yes | SafetyHook inline | game + `0x0022DF50` | 16 | `55 8B EC 83 EC 0C 89 4D F4 C6 45 FF 00 8B 4D F4` | same | Absolute Judgement | same | same |
| Game | Absolute Judgement | `released` | Yes | SafetyHook inline | game + `0x0022DD30` | 16 | `55 8B EC 83 EC 28 89 4D D8 C6 45 FF 00 8B 4D D8` | same | Absolute Judgement | same | same |
| Game | Absolute Judgement | `direction` | Yes | SafetyHook inline | game + `0x0022E480` | 16 | `55 8B EC 83 EC 08 89 4D F8 8B 45 0C D9 EE D9 18` | `int __fastcall(void*,void*,int booster,float* x,float* y,int frame)` | Absolute Judgement | same | same |
| Game | Absolute Judgement | `held_age` | Yes | SafetyHook inline | game + `0x0022DAA0` | 16 | `55 8B EC 83 EC 08 89 4D F8 C7 45 FC 00 00 00 00` | `int __fastcall(void*,void*,unsigned id)` | Absolute Judgement | same | same |
| Game | Absolute Judgement diagnostics | `timing_grade` | Yes | SafetyHook inline; best effort | game + `0x001D0E00` | 18 | `55 8B EC 83 EC 4C 89 4D CC 8B 45 08 D9 80 B0 00 00 00` | `int __fastcall(void*,void*,const float* note,int recognition_ms)` | Absolute Judgement; failure warns | same | same |

### Test Mode Timing

| Process | Feature | Site | Versioned | Installation | Module/RVA | Protected span | Expected clean / installed state | Callback ABI | Enabled when | Current owner / lifetime | Evidence source |
|---|---|---|---|---|---|---:|---|---|---|---|---|
| Game | Test Mode Timing | `main_row_count` | Yes | checked byte write | game + `0x00173ED5` | 2 | `6A 0B` -> `6A 0C` | N/A | every game attach | global `TimingRuntimeState`; process | `src/Patches/TestModeTiming/TimingSettingsGameAbi.cpp:228-236,296-452`; `src/Patches/TestModeTiming/TimingSettingsPatch.cpp:756-768,1031-1034`; `src/Loader/DllMain.cpp:726-733` |
| Game | Test Mode Timing | `main_constructor` | Yes | SafetyHook inline | game + `0x00173EA0` | 16 | `55 8B EC 6A FF 68 A7 9A 67 00 64 A1 00 00 00 00` | `void* __fastcall(void* self,void*,void* parent)`; original thiscall | every game attach | same | `src/Patches/TestModeTiming/TimingSettingsPatch.cpp:401-459,909-952` |
| Game | Test Mode Timing | `main_render` | Yes | SafetyHook inline | game + `0x00173C60` | 16 | `55 8B EC 81 EC 9C 00 00 00 A1 94 93 77 00 33 C5` | `void* __fastcall(void* self,void*,int frame,int input)`; original thiscall | every game attach | same | `src/Patches/TestModeTiming/TimingSettingsPatch.cpp:401-459,954-992` |
| Game | Test Mode Timing carrier | `sound_carrier_vtable` | Yes | read native vtable, copy to carrier, replace six slots in per-object copy; **not interception/global mutation** | game + `0x002FB864` | 52 (13 x 4) | 13 pointers to RVAs `06AB20,06AB20,00C9B0,04D070,0C2680,16B0C0,16B440,16B290,16B230,16AD60,16AC20,16A9A0,0C2F20` | carrier slots: activate `void* __fastcall(void*,void*)`; render `void* __fastcall(void*,void*,int,int)`; confirm/increment/decrement `int __fastcall(void*,void*,int,int,int)`; back `int __fastcall(void*,void*,int,int)` | every game attach; carrier constructed when test menu main form is constructed | carrier vtable array and carrier object in `TimingRuntimeState`; carrier lifetime | `src/Patches/TestModeTiming/TimingSettingsGameAbi.cpp:239-283`; `src/Patches/TestModeTiming/TimingSettingsPatch.cpp:52,224-273,572-652,771-907` |

### Renderer Device Loss

| Process | Feature | Site | Versioned | Installation | Module/RVA | Protected span | Expected clean state | Callback ABI | Enabled when | Current owner / lifetime | Evidence source |
|---|---|---|---|---|---|---:|---|---|---|---|---|
| Game | Renderer Device Loss | `device_lost_tail` | Yes | SafetyHook mid | game + `0x000E67D8` | 12 | `89 BE 18 01 00 00 89 BE 1C 01 00 00` | `void(Context&)` | every game attach | `RendererDeviceLossRuntime`; process | `src/Patches/RendererDeviceLoss/RendererDeviceLossAbi.h:88-239`; `src/Patches/RendererDeviceLoss/RendererDeviceLossPatch.cpp:640-751`; `src/Loader/DllMain.cpp:735-742` |
| Game | Renderer Device Loss | `vertex_buffer_result` | Yes | SafetyHook mid | game + `0x000E79F7` | 7 | `85 C0 7C 59 8B 4F 0C` | `void(Context&)` | every game attach | same | same |
| Game | Renderer Device Loss | `index_buffer_result` | Yes | SafetyHook mid | game + `0x000E7A84` | 9 | `85 C0 7D 13 68 E4 A5 71 00` | `void(Context&)` | every game attach | same | same |
| Game | Renderer Device Loss | `vertex_buffer_lock_guard` | Yes | SafetyHook mid | game + `0x000E5578` | 9 | `3B F9 72 05 E8 66 00 02 00` | `void(Context&)` | every game attach | same | same |
| Game | Renderer Device Loss | `direct_lock_result` | Yes | SafetyHook mid | game + `0x000E691E` | 11 | `8B 4C 24 14 51 8B 8E E4 01 00 00` | `void(Context&)` | every game attach | same | same |
| Game | Renderer Device Loss | `buffered_unlock_result` | Yes | SafetyHook mid | game + `0x000E5662` | 9 | `85 C0 7D 13 68 E4 A5 71 00` | `void(Context&)` | every game attach | same | same |

### Windowed Widescreen

All 36 requests are enabled only when Windowed Widescreen is configured. Inline/mid/vtable hooks are retained by `WindowedWidescreenRuntime`; the reset pair is physically owned by `RendererDeviceLossRuntime`. All have process lifetime. Mid callback ABI is `void(safetyhook::Context&) noexcept`.

| Process | Feature | Site | Versioned | Installation | Module/RVA | Protected span | Expected clean state | Callback ABI | Enabled when | Current owner / lifetime | Evidence source |
|---|---|---|---|---|---|---:|---|---|---|---|---|
| Game | Windowed Widescreen | `config_apply` | Yes | SafetyHook inline | game + `0x0023C360` | 11 | `55 8B EC 83 EC 14 E8 E5 F8 FF FF` | `int __cdecl(int)` | widescreen | Widescreen runtime; process | `src/Patches/WindowedWidescreen/WindowedWidescreenAbi.cpp:7-255`; `src/Patches/WindowedWidescreen/WindowedWidescreenPatch.cpp:3335-3518` |
| Game | Windowed Widescreen | `window_device_create` | Yes | SafetyHook inline | game + `0x0005B8A0` | 12 | `83 EC 64 53 55 56 57 6A 30 33 ED 8D` | `int __fastcall(void*,void*)`; original `int __thiscall(void*)` | widescreen | same | same |
| Game | Windowed Widescreen | `logical_resolution_set` | Yes | SafetyHook inline | game + `0x00053660` | 14 | `6A FF 68 EB DA 66 00 64 A1 00 00 00 00 50` | `int __cdecl(int,int)` | widescreen | same | same |
| Game | Windowed Widescreen | `logical_target_width_set` | Yes | SafetyHook inline | game + `0x00052F60` | 13 | `DB 44 24 04 8B 44 24 04 A3 F8 6F 78 00` | `int __cdecl(int)` | widescreen | same | same |
| Game | Windowed Widescreen | `logical_target_height_set` | Yes | SafetyHook inline | game + `0x00052F80` | 13 | `DB 44 24 04 8B 44 24 04 A3 FC 6F 78 00` | `int __cdecl(int)` | widescreen | same | same |
| Game | Windowed Widescreen | `frame_begin` | Yes | SafetyHook inline | game + `0x0005AC70` | 12 | `51 53 56 8D 44 24 08 57 50 8B F1 E8` | `int __fastcall(void*,void*)`; original `int __thiscall(void*)` | widescreen | same | same |
| Game | Windowed Widescreen | `frame_end` | Yes | SafetyHook inline | game + `0x0005ACE0` | 12 | `8B 41 08 8B 08 8B 91 A8 00 00 00 50` | `int __fastcall(void*,void*)`; original `int __thiscall(void*)` | widescreen | same | same |
| Game | Windowed Widescreen | `task_dispatch` | Yes | SafetyHook inline | game + `0x0005C1B0` | 12 | `8B 09 8B 01 8B 50 10 FF E2 CC CC CC` | `int __fastcall(void*,void*)`; original `int __thiscall(void*)` | widescreen | same | same |
| Game | Windowed Widescreen | `network_status_movie_clip_accept` | Yes | checked global vtable slot replacement | game + `0x002BE0E0` (`0x002BE0CC+0x14`) | 4 | pointer bytes `D0 0C 4E 00` / target game RVA `0x000E0CD0` | detour `int __fastcall(void* movie_clip,void*,void* visitor)`; original `int __thiscall(void*,void*)` | widescreen | `StoredVtableHook`; process | `src/Patches/WindowedWidescreen/WindowedWidescreenAbi.cpp:241-253,283-298`; `src/Patches/WindowedWidescreen/WindowedWidescreenPatch.cpp:270-442,1606-1700` |
| Game | Windowed Widescreen | `network_status_shape_draw_visit` | Yes | checked global vtable slot replacement | game + `0x002BB798` (`0x002BB74C+0x4C`) | 4 | pointer bytes `80 C8 4C 00` / target game RVA `0x000CC880` | detour `void __fastcall(void* visitor,void*,void* definition)`; original `void __thiscall(void*,void*)` | widescreen | `StoredVtableHook`; process | `src/Patches/WindowedWidescreen/WindowedWidescreenAbi.cpp:241-253,283-298`; `src/Patches/WindowedWidescreen/WindowedWidescreenPatch.cpp:270-442,1566-1604` |
| Game | Windowed Widescreen | `test_mode_native_begin` | Yes | SafetyHook mid | game + `0x0023AA89` | 10 | `E8 D2 BB F3 FF E8 8D 86 E1 FF` | `void(Context&)` | widescreen | Widescreen runtime; process | byte/source ranges above |
| Game | Windowed Widescreen | `test_mode_native_end` | Yes | SafetyHook mid | game + `0x0023AA8E` | 13 | `E8 8D 86 E1 FF 89 85 80 FE FF FF 8B 8D` | `void(Context&)` | widescreen | same | same |
| Game | Windowed Widescreen | `screen_width_int` | Yes | SafetyHook inline | game + `0x00052F20` | 6 | `A1 E8 6F 78 00 C3` | `uint32_t __cdecl()` | widescreen | same | same |
| Game | Windowed Widescreen | `screen_height_int` | Yes | SafetyHook inline | game + `0x00052F30` | 6 | `A1 EC 6F 78 00 C3` | `uint32_t __cdecl()` | widescreen | same | same |
| Game | Windowed Widescreen | `screen_width_float` | Yes | SafetyHook inline | game + `0x00052F40` | 7 | `D9 05 F0 6F 78 00 C3` | `float __cdecl()` | widescreen | same | same |
| Game | Windowed Widescreen | `screen_height_float` | Yes | SafetyHook inline | game + `0x00052F50` | 7 | `D9 05 F4 6F 78 00 C3` | `float __cdecl()` | widescreen | same | same |
| Game | Windowed Widescreen | `target_width_int` | Yes | SafetyHook inline | game + `0x00052FA0` | 6 | `A1 F8 6F 78 00 C3` | `uint32_t __cdecl()` | widescreen | same | same |
| Game | Windowed Widescreen | `target_height_int` | Yes | SafetyHook inline | game + `0x00052FB0` | 6 | `A1 FC 6F 78 00 C3` | `uint32_t __cdecl()` | widescreen | same | same |
| Game | Windowed Widescreen | `target_width_float` | Yes | SafetyHook inline | game + `0x00052FC0` | 7 | `D9 05 00 70 78 00 C3` | `float __cdecl()` | widescreen | same | same |
| Game | Windowed Widescreen | `target_height_float` | Yes | SafetyHook inline | game + `0x00052FD0` | 7 | `D9 05 04 70 78 00 C3` | `float __cdecl()` | widescreen | same | same |
| Game | Windowed Widescreen | `viewport_reset` | Yes | SafetyHook inline | game + `0x00053140` | 12 | `8B 4C 24 04 33 C0 83 EC 20 3B C8 0F` | `int __cdecl(int*)` | widescreen | same | same |
| Game | Windowed Widescreen | `mouse_debug_poll` | Yes | SafetyHook inline | game + `0x000B06B0` | 12 | `55 8B EC 83 EC 08 89 4D F8 8B 45 F8` | `POINT* __fastcall(void*,void*,std::uint32_t*)`; original `POINT* __thiscall(void*,std::uint32_t*)` | widescreen | same | same |
| Game | Windowed Widescreen | `gameplay_stage_background` | Yes | SafetyHook mid | game + `0x00262FA0` | 8 | `E8 4B 1A FE FF 8B 4D C4` | `void(Context&)` | widescreen | same | same |
| Game | Windowed Widescreen | `gameplay_track` | Yes | SafetyHook mid | game + `0x00262FA8` | 8 | `E8 D3 56 FE FF 8B 4D C4` | `void(Context&)` | widescreen | same | same |
| Game | Windowed Widescreen | `gameplay_effects` | Yes | SafetyHook mid | game + `0x00263041` | 10 | `E8 FA 5C FE FF E8 D5 00 DF FF` | `void(Context&)` | widescreen | same | same |
| Game | Windowed Widescreen | `gameplay_effects_end` | Yes | SafetyHook mid | game + `0x00263046` | 5 | `E8 D5 00 DF FF` | `void(Context&)` | widescreen | same | same |
| Game | Windowed Widescreen | `gameplay_hud_projection` | Yes | SafetyHook mid | game + `0x0023FDBA` | 16 | `E8 B1 F3 F9 FF 8B B5 24 FF FF FF 81 C6 D0 00 00` | `void(Context&)` | widescreen | same | same |
| Game | Windowed Widescreen | `combo_begin` | Yes | SafetyHook mid | game + `0x001E4503` | 5 | `E8 A8 D0 FF FF` | `void(Context&)` | widescreen | same | same |
| Game | Windowed Widescreen | `combo_end` | Yes | SafetyHook mid | game + `0x001E4B58` | 13 | `8B 55 E4 8B 45 E0 89 02 E9 D9 F8 FF FF` | `void(Context&)` | widescreen | same | same |
| Game | Windowed Widescreen | `gameplay_feedback_draw_begin` | Yes | SafetyHook mid | game + `0x001F11E8` | 5 | `E8 83 0D 00 00` | `void(Context&)` | widescreen | same | same |
| Game | Windowed Widescreen | `gameplay_feedback_draw_end` | Yes | SafetyHook mid | game + `0x001F11ED` | 10 | `8B 4D F8 8B 51 0C 81 E2 00 40` | `void(Context&)` | widescreen | same | same |
| Game | Windowed Widescreen | `note_tutorial_group_begin` | Yes | SafetyHook mid | game + `0x0024A2D5` | 5 | `E8 A6 6E FA FF` | `void(Context&)` | widescreen | same | same |
| Game | Windowed Widescreen | `note_tutorial_group_end` | Yes | SafetyHook mid | game + `0x0024A2DA` | 8 | `0F B6 55 08 85 D2 74 1B` | `void(Context&)` | widescreen | same | same |
| Game | Windowed Widescreen | `clip_gate` | Yes | SafetyHook mid | game + `0x002441CA` | 19 | `8B 95 80 FE FF FF 8B 82 4C 02 00 00 0F B6 88 5C 01 00 00` | `void(Context&)` | widescreen | same | same |
| Game | Windowed Widescreen | `reset_pre` | Yes | SafetyHook mid | game + `0x0005B28B` | 7 | `83 BE 94 00 00 00 00` | `void(Context&)` | widescreen | `RendererDeviceLossRuntime`; process | `src/Patches/WindowedWidescreen/WindowedWidescreenAbi.cpp:108-117`; `src/Patches/WindowedWidescreen/WindowedWidescreenPatch.cpp:586-646,744-841,3485-3491` |
| Game | Windowed Widescreen | `reset_post` | Yes | SafetyHook mid | game + `0x0005B474` | 8 | `83 C4 04 B8 01 00 00 00` | `void(Context&)` | widescreen | `RendererDeviceLossRuntime`; process | same |

### Audio and NESYS Versioned Sites

| Process | Feature | Site | Versioned | Installation | Module/RVA | Protected span | Expected clean state | Callback ABI | Enabled when | Current owner / lifetime | Evidence source |
|---|---|---|---|---|---|---:|---|---|---|---|---|
| Game | Exclusive Audio / ASIO | `asio_ordinary_close` | Yes | SafetyHook mid | game + `0x0023C853` | 16 | `FF 15 3C D6 6A 00 8B E5 5D C3 CC CC CC 55 8B EC` | `void(Context&) noexcept` | audio backend ASIO | global `g_asio_close_hook`; process | `src/Audio/AudioPatch.cpp:42-51,995-1041,1523-1582` |
| NESYS service | NESYS synthetic adapter | `service_ping_redirect` | Yes | SafetyHook mid | NESYS image + `0x00008E40` | 32 | `51 53 55 56 57 50 8B D9 8D 6B 04 6A 10 55 C7 44 24 1C 00 00 00 00 E8 02 73 02 00 83 C4 0C 8D 73` | `void(Context&) noexcept` | service role and synthetic network adapter enabled | global `g_service_ping_hook`; process | `src/Nesys/Network/SyntheticNetworkAdapter.h:32-48`; `src/Nesys/Network/SyntheticNetworkAdapter.cpp:22,41-50,282-388` |

### Required Read-only Versioned Dependencies

These sites are preflighted or used as verified branch/function/pointer dependencies but are not directly patched or hooked. They still belong in the versioned site contract because installed callbacks call them, redirect to them, or depend on their identity.

| Process | Feature | Site | Versioned | Kind | Module/RVA | Protected span | Expected state | Callback/callee ABI | Enabled when | Current owner / lifetime | Evidence source |
|---|---|---|---|---|---|---:|---|---|---|---|---|
| Game | Auto Play | `native_debug_text` | Yes | read-only native callee | game + `0x00069650` | 5 | `55 8B EC 6A FF` | `int __cdecl(float,float,uint32_t,const char*,...)` | Auto Play | dependency only; process image | `src/Patches/AutoPlay/AutoPlayPatch.cpp:39-101,280-419`; `src/Patches/AutoPlay/AutoPlayMarker.h:7-15` |
| Game | Test Mode Timing | `sound_constructor` | Yes | read-only native callee | game + `0x0016AE80` | 16 | `55 8B EC 6A FF 68 97 71 67 00 64 A1 00 00 00 00` | `void* __thiscall(void*,void*)` | every game attach | `TimingRuntimeState`; process | `src/Patches/TestModeTiming/TimingSettingsGameAbi.h:13-82`; `src/Patches/TestModeTiming/TimingSettingsGameAbi.cpp:162-225` |
| Game | Test Mode Timing | `game_allocator` | Yes | read-only native callee | game + `0x0023BD20` | 16 | `55 8B EC 8B 45 08 50 E8 94 FE FF FF 83 C4 04 5D` | `void* __cdecl(size_t)` | every game attach | same | same |
| Game | Test Mode Timing | `game_deallocator` | Yes | read-only native callee | game + `0x0023BD00` | 16 | `55 8B EC 8B 45 08 50 E8 44 FE FF FF 83 C4 04 5D` | `int __cdecl(void*)` | every game attach | same | same |
| Game | Test Mode Timing | `register_child` | Yes | read-only native callee | game + `0x000C2C90` | 16 | `55 8B EC 51 89 4D FC 8B 45 FC 8B 48 2C 8B 55 08` | `void* __thiscall(void*,int,void*)` | every game attach | same | same |
| Game | Test Mode Timing | `base_form_update` | Yes | read-only native callee | game + `0x000C2E40` | 16 | `55 8B EC 83 EC 0C 89 4D F4 C7 45 F8 00 00 00 00` | `int __thiscall(void*,int,int)` | every game attach | same | same |
| Game | Test Mode Timing | `set_grid_cell_text` | Yes | read-only native callee | game + `0x000C1200` | 16 | `55 8B EC 51 89 4D FC 8B 45 FC 8B 4D 08 3B 48 28` | `void* __thiscall(void*,int,int,const unsigned char*)` | every game attach | same | same |
| Game | Test Mode Timing | `set_selection` | Yes | read-only native callee | game + `0x000C1C00` | 16 | `55 8B EC 51 89 4D FC 8B 45 FC 83 78 28 00 75 02` | `int __thiscall(void*,int)` | every game attach | same | same |
| Game | Test Mode Timing | `draw_title` | Yes | read-only native callee | game + `0x00176940` | 16 | `55 8B EC 83 7D 14 04 75 07 C7 45 14 00 00 00 00` | `int __cdecl(const unsigned char*,const unsigned char*,const unsigned char*,int)` | every game attach | same | same |
| Game | Test Mode Timing | `set_title_position` | Yes | read-only native callee | game + `0x00176900` | 16 | `55 8B EC 8B 45 0C 50 8B 4D 08 51 8B 0D 64 25 7F` | `int __cdecl(int,int)` | every game attach | same | same |
| Game | Test Mode Timing | `draw_help` | Yes | read-only native callee | game + `0x00176920` | 16 | `55 8B EC 8B 45 14 50 8B 4D 10 51 8B 55 0C 52` | `int __cdecl(const unsigned char*,const unsigned char*,int,int)` | every game attach | same | same |
| Game | Test Mode Timing | `timing_manager_accessor` | Yes | read-only native callee | game + `0x00001040` | 16 | `55 8B EC 6A FF 68 8E D6 67 00 64 A1 00 00 00 00` | `void* __cdecl()` | every game attach | same | same |
| Game | Test Mode Timing | `judgment_timing_setter` | Yes | read-only native callee | game + `0x00259310` | 16 | `55 8B EC 51 89 4D FC 8B 4D FC E8 B1 7D DA FF 0F` | `int __thiscall(void*,int)` | every game attach | same | same |
| Game | Test Mode Timing | `game_timing_setter` | Yes | read-only native callee | game + `0x00259350` | 16 | `55 8B EC 51 89 4D FC 8B 4D FC E8 71 7D DA FF 0F` | `int __thiscall(void*,int)` | every game attach | same | same |
| Game | Renderer Device Loss | `initializer_epilogue` | Yes | read-only continuation | game + `0x000E7EE9` | 7 | `5F 5E 5B 8B E5 5D C3` | EIP continuation target | every game attach | Renderer runtime; process | `src/Patches/RendererDeviceLoss/RendererDeviceLossAbi.h:88-239`; `src/Patches/RendererDeviceLoss/RendererDeviceLossPatch.cpp:640-751` |
| Game | Renderer Device Loss | `vertex_buffer_lock_failure` | Yes | read-only branch target | game + `0x000E55E2` | 12 | `5F 5E 89 18 89 58 04 5B 59 C2 08 00` | EIP failure target | every game attach | same | same |
| Game | Renderer Device Loss | `direct_batch_cleanup` | Yes | read-only continuation | game + `0x000E6AD6` | 12 | `8B B6 E4 01 00 00 8B 5E 10 39 5E 0C` | EIP cleanup target | every game attach | same | same |
| Game | Renderer Device Loss | `buffered_unlock_continuation` | Yes | read-only continuation | game + `0x000E5679` | 12 | `8B 86 80 04 00 00 8B 8E 44 07 00 00` | EIP continuation target | every game attach | same | same |
| Game | Windowed Widescreen | `combo_normal_digits` | Yes | read-only call dependency | game + `0x001E4550` | 5 | `E8 0B 7B FE FF` | source contract only | widescreen | Widescreen runtime; process | `src/Patches/WindowedWidescreen/WindowedWidescreenAbi.cpp:149-239,257-298` |
| Game | Windowed Widescreen | `clip_default` | Yes | read-only branch dependency | game + `0x002441C6` | 4 | `C6 45 DF 00` | source contract only | widescreen | same | same |
| Game | Windowed Widescreen | `clip_continuation` | Yes | read-only continuation | game + `0x0024422F` | 10 | `8B 4D D8 E8 C9 18 DC FF 0F B6` | EIP continuation target | widescreen | same | same |
| Game | Windowed Widescreen | `batch_flush` | Yes | read-only native callee | game + `0x001C9B10` | 12 | `55 8B EC 83 EC 08 C7 45 FC 00 00 00` | `void __cdecl()` in source use | widescreen | same | same |
| Game | Windowed Widescreen | `clip_owner` | Yes | read-only native owner/callee | game + `0x00244000` | 12 | `55 8B EC 81 EC A0 01 00 00 56 57 89` | source contract only | widescreen | same | same |
| Game | Windowed Widescreen | `live_frustum_helper` | Yes | read-only native callee | game + `0x00243BE0` | 12 | `55 8B EC 81 EC C0 00 00 00 89 8D 58` | source contract only | widescreen | same | same |
| Game | Windowed Widescreen | `config_width_setter` | Yes | read-only vtable pointer | game + `0x002AE644` | 4 | pointer -> game RVA `0x00059CC0` | native vtable slot identity | widescreen | same | `src/Patches/WindowedWidescreen/WindowedWidescreenAbi.cpp:257-298`; config matching `WindowedWidescreenPatch.cpp:877-900` |
| Game | Windowed Widescreen | `config_height_setter` | Yes | read-only vtable pointer | game + `0x002AE648` | 4 | pointer -> game RVA `0x00059CE0` | native vtable slot identity | widescreen | same | same |
| Game | Windowed Widescreen | `config_resize_setter` | Yes | read-only vtable pointer | game + `0x002AE654` | 4 | pointer -> game RVA `0x00059D20` | native vtable slot identity | widescreen | same | same |
| Game | Windowed Widescreen | `config_minmax_setter` | Yes | read-only vtable pointer | game + `0x002AE658` | 4 | pointer -> game RVA `0x00059D40` | native vtable slot identity | widescreen | same | same |
| Game | Windowed Widescreen | `config_mode_setter` | Yes | read-only vtable pointer | game + `0x002AE65C` | 4 | pointer -> game RVA `0x00059D70` | native vtable slot identity | widescreen | same | same |
| Game | Windowed Widescreen | `common_2d_render` | Yes | read-only global function pointer | game + `0x002F9B0C` | 4 | pointer -> game RVA `0x001F5670` | native render target identity | widescreen | same | same |
| Game | Windowed Widescreen | `common_3d_render` | Yes | read-only global function pointer | game + `0x002FB228` | 4 | pointer -> game RVA `0x001784B0` | native render target identity | widescreen | same | same |

## Export Hook Sites

Every row in this section is **Versioned = No**. Protected span is N/A: current source resolves the named module/export and lets SafetyHook or MinHook build its own trampoline. Expected state is “module and export resolve; original target is non-null.” These hooks stay outside the global versioned-runtime-site barrier.

### Locale, Crash, Raw Input, TTX, and Audio

| Process | Feature | Site | Versioned | Installation | Module/export | Protected span | Expected state | Callback ABI | Enabled when | Current owner / lifetime | Evidence source |
|---|---|---|---|---|---|---|---|---|---|---|---|
| Game + NESYS service | Japanese Locale | `GetACP` | No | MinHook transaction | `kernel32.dll!GetACP` | N/A | export resolves | `UINT WINAPI()` | every attach, both roles | locale global transaction; process | `src/Locale/JapaneseLocaleCompatibility.cpp:15-18,30-120,154-225,326-347`; `src/Loader/DllMain.cpp:554-562` |
| Game + NESYS service | Japanese Locale | `GetOEMCP` | No | MinHook transaction | `kernel32.dll!GetOEMCP` | N/A | export resolves | `UINT WINAPI()` | every attach, both roles | same | same |
| Game + NESYS service | Japanese Locale | `GetThreadLocale` | No | MinHook transaction | `kernel32.dll!GetThreadLocale` | N/A | export resolves | `LCID WINAPI()` | every attach, both roles | same | same |
| Game + NESYS service | Japanese Locale | `GetUserDefaultLCID` | No | MinHook transaction | `kernel32.dll!GetUserDefaultLCID` | N/A | export resolves | `LCID WINAPI()` | every attach, both roles | same | same |
| Game + NESYS service | Japanese Locale | `GetCPInfo` | No | MinHook transaction | `kernel32.dll!GetCPInfo` | N/A | export resolves | `BOOL WINAPI(UINT,LPCPINFO)` | every attach, both roles | same | same |
| Game + NESYS service | Japanese Locale | `MultiByteToWideChar` | No | MinHook transaction | `kernel32.dll!MultiByteToWideChar` | N/A | export resolves | `int WINAPI(UINT,DWORD,LPCCH,int,LPWSTR,int)` | every attach, both roles | same | same |
| Game + NESYS service | Japanese Locale | `WideCharToMultiByte` | No | MinHook transaction | `kernel32.dll!WideCharToMultiByte` | N/A | export resolves | `int WINAPI(UINT,DWORD,LPCWCH,int,LPSTR,int,LPCCH,LPBOOL)` | every attach, both roles | same | same |
| Game + NESYS service | Japanese Locale | `GetTimeZoneInformation` | No | MinHook transaction | `kernel32.dll!GetTimeZoneInformation` | N/A | export resolves | `DWORD WINAPI(LPTIME_ZONE_INFORMATION)` | every attach, both roles | same | same |
| Game + NESYS service | Japanese Locale | `GetLocalTime` | No | MinHook transaction | `kernel32.dll!GetLocalTime` | N/A | export resolves | `void WINAPI(LPSYSTEMTIME)` | every attach, both roles | same | same |
| Game + NESYS service | Japanese Locale | `SetLocalTime` | No | MinHook transaction | `kernel32.dll!SetLocalTime` | N/A | export resolves | `BOOL WINAPI(const SYSTEMTIME*)` | every attach, both roles | same | same |
| Game | Crash Dump | `SetUnhandledExceptionFilter` | No | MinHook transaction | `kernel32.dll!SetUnhandledExceptionFilter` | N/A | export resolves; install may degrade to filter-only | `LPTOP_LEVEL_EXCEPTION_FILTER WINAPI(LPTOP_LEVEL_EXCEPTION_FILTER)` | every game attach | global `g_set_filter_hook`; process | `src/Diagnostics/CrashDumpHandler.cpp:52-60,300-340`; `src/Loader/DllMain.cpp:564-572` |
| Game | Raw Input registration guard | `RegisterRawInputDevices` | No | SafetyHook inline export hook | `user32.dll!RegisterRawInputDevices` | N/A | module/export resolve | `BOOL WINAPI(PCRAWINPUTDEVICE,UINT,UINT)` | input runtime initialization; game role | leaked singleton `GuardState`; process | `src/Input/Win32/RawInputRegistrationGuard.cpp:19-31,51-69,128-172` |
| Game | TTX init guard / System Path | `TtxUDLInit` | No | SafetyHook inline export hook | `TtxUpdateDownloader.dll!?TtxUDLInit@@YAHKKKK@Z` | N/A | module/export resolve | `int __cdecl(unsigned,unsigned,unsigned,unsigned)` | RFID/System Path feature initialization | `TtxInitGuard` member of `FeatureState`; feature/process | `src/SystemPath/TtxInitGuard.h:14-20,92-119`; `src/SystemPath/TtxInitGuard.cpp:141-214`; `src/Rfid/Feature.cpp:23-43,203-228` |
| Game | Exclusive Audio backend | `DirectSoundCreate8` | No | current ad-hoc MinHook | `dsound.dll!DirectSoundCreate8` | N/A | module/export resolve | `HRESULT WINAPI(LPCGUID,LPDIRECTSOUND8*,LPUNKNOWN)` | backend WASAPI-exclusive or ASIO; absent for DirectSound | audio globals/original; process | `src/Audio/AudioPatch.cpp:1060-1092,1309-1372,1441-1473,1528-1575` |

### Kernel32/RFID, COM, Storage, and System Path

All 27 hooks use the shared `gc::win32_hooks::MinHookTransaction` and exact Win32 export ABI (`WINAPI`; source original slots use `decltype(&::Export)`). They are owned by `FeatureState::{kernel32,transaction}` for process lifetime. The 15 unconditional sites support RFID/JVS; conditional sites are included only when the indicated routing layer is enabled.

| Process | Feature | Site | Versioned | Installation | Module/export | Protected span | Expected state | Callback ABI | Enabled when | Current owner / lifetime | Evidence source |
|---|---|---|---|---|---|---|---|---|---|---|---|
| Game | Kernel32/RFID-system-storage | `CreateFileA` | No | MinHook transaction | `kernel32.dll!CreateFileA` | N/A | module/export resolves | `HANDLE WINAPI(LPCSTR,DWORD,DWORD,LPSECURITY_ATTRIBUTES,DWORD,DWORD,HANDLE)` | always | `FeatureState` transaction; process | `src/Win32Hooks/Kernel32Hooks.h:14-159`; `src/Win32Hooks/Kernel32Hooks.cpp:82-160`; `src/Rfid/Feature.cpp:23-43,203-228` |
| Game | Kernel32/RFID-system-storage | `CreateFileW` | No | MinHook transaction | `kernel32.dll!CreateFileW` | N/A | module/export resolves | `HANDLE WINAPI(LPCWSTR,DWORD,DWORD,LPSECURITY_ATTRIBUTES,DWORD,DWORD,HANDLE)` | always | `FeatureState` transaction; process | `src/Win32Hooks/Kernel32Hooks.h:14-159`; `src/Win32Hooks/Kernel32Hooks.cpp:82-160`; `src/Rfid/Feature.cpp:23-43,203-228` |
| Game | Kernel32/RFID-system-storage | `WriteFile` | No | MinHook transaction | `kernel32.dll!WriteFile` | N/A | module/export resolves | `BOOL WINAPI(HANDLE,LPCVOID,DWORD,LPDWORD,LPOVERLAPPED)` | always | `FeatureState` transaction; process | `src/Win32Hooks/Kernel32Hooks.h:14-159`; `src/Win32Hooks/Kernel32Hooks.cpp:82-160`; `src/Rfid/Feature.cpp:23-43,203-228` |
| Game | Kernel32/RFID-system-storage | `FlushFileBuffers` | No | MinHook transaction | `kernel32.dll!FlushFileBuffers` | N/A | module/export resolves | `BOOL WINAPI(HANDLE)` | always | `FeatureState` transaction; process | `src/Win32Hooks/Kernel32Hooks.h:14-159`; `src/Win32Hooks/Kernel32Hooks.cpp:82-160`; `src/Rfid/Feature.cpp:23-43,203-228` |
| Game | Kernel32/RFID-system-storage | `ReadFile` | No | MinHook transaction | `kernel32.dll!ReadFile` | N/A | module/export resolves | `BOOL WINAPI(HANDLE,LPVOID,DWORD,LPDWORD,LPOVERLAPPED)` | always | `FeatureState` transaction; process | `src/Win32Hooks/Kernel32Hooks.h:14-159`; `src/Win32Hooks/Kernel32Hooks.cpp:82-160`; `src/Rfid/Feature.cpp:23-43,203-228` |
| Game | Kernel32/RFID-system-storage | `CloseHandle` | No | MinHook transaction | `kernel32.dll!CloseHandle` | N/A | module/export resolves | `BOOL WINAPI(HANDLE)` | always | `FeatureState` transaction; process | `src/Win32Hooks/Kernel32Hooks.h:14-159`; `src/Win32Hooks/Kernel32Hooks.cpp:82-160`; `src/Rfid/Feature.cpp:23-43,203-228` |
| Game | Kernel32/RFID-system-storage | `GetCommModemStatus` | No | MinHook transaction | `kernel32.dll!GetCommModemStatus` | N/A | module/export resolves | `BOOL WINAPI(HANDLE,LPDWORD)` | always | `FeatureState` transaction; process | `src/Win32Hooks/Kernel32Hooks.h:14-159`; `src/Win32Hooks/Kernel32Hooks.cpp:82-160`; `src/Rfid/Feature.cpp:23-43,203-228` |
| Game | Kernel32/RFID-system-storage | `EscapeCommFunction` | No | MinHook transaction | `kernel32.dll!EscapeCommFunction` | N/A | module/export resolves | `BOOL WINAPI(HANDLE,DWORD)` | always | `FeatureState` transaction; process | `src/Win32Hooks/Kernel32Hooks.h:14-159`; `src/Win32Hooks/Kernel32Hooks.cpp:82-160`; `src/Rfid/Feature.cpp:23-43,203-228` |
| Game | Kernel32/RFID-system-storage | `ClearCommError` | No | MinHook transaction | `kernel32.dll!ClearCommError` | N/A | module/export resolves | `BOOL WINAPI(HANDLE,LPDWORD,LPCOMSTAT)` | always | `FeatureState` transaction; process | `src/Win32Hooks/Kernel32Hooks.h:14-159`; `src/Win32Hooks/Kernel32Hooks.cpp:82-160`; `src/Rfid/Feature.cpp:23-43,203-228` |
| Game | Kernel32/RFID-system-storage | `SetCommMask` | No | MinHook transaction | `kernel32.dll!SetCommMask` | N/A | module/export resolves | `BOOL WINAPI(HANDLE,DWORD)` | always | `FeatureState` transaction; process | `src/Win32Hooks/Kernel32Hooks.h:14-159`; `src/Win32Hooks/Kernel32Hooks.cpp:82-160`; `src/Rfid/Feature.cpp:23-43,203-228` |
| Game | Kernel32/RFID-system-storage | `SetupComm` | No | MinHook transaction | `kernel32.dll!SetupComm` | N/A | module/export resolves | `BOOL WINAPI(HANDLE,DWORD,DWORD)` | always | `FeatureState` transaction; process | `src/Win32Hooks/Kernel32Hooks.h:14-159`; `src/Win32Hooks/Kernel32Hooks.cpp:82-160`; `src/Rfid/Feature.cpp:23-43,203-228` |
| Game | Kernel32/RFID-system-storage | `GetCommState` | No | MinHook transaction | `kernel32.dll!GetCommState` | N/A | module/export resolves | `BOOL WINAPI(HANDLE,LPDCB)` | always | `FeatureState` transaction; process | `src/Win32Hooks/Kernel32Hooks.h:14-159`; `src/Win32Hooks/Kernel32Hooks.cpp:82-160`; `src/Rfid/Feature.cpp:23-43,203-228` |
| Game | Kernel32/RFID-system-storage | `SetCommState` | No | MinHook transaction | `kernel32.dll!SetCommState` | N/A | module/export resolves | `BOOL WINAPI(HANDLE,LPDCB)` | always | `FeatureState` transaction; process | `src/Win32Hooks/Kernel32Hooks.h:14-159`; `src/Win32Hooks/Kernel32Hooks.cpp:82-160`; `src/Rfid/Feature.cpp:23-43,203-228` |
| Game | Kernel32/RFID-system-storage | `SetCommTimeouts` | No | MinHook transaction | `kernel32.dll!SetCommTimeouts` | N/A | module/export resolves | `BOOL WINAPI(HANDLE,LPCOMMTIMEOUTS)` | always | `FeatureState` transaction; process | `src/Win32Hooks/Kernel32Hooks.h:14-159`; `src/Win32Hooks/Kernel32Hooks.cpp:82-160`; `src/Rfid/Feature.cpp:23-43,203-228` |
| Game | Kernel32/RFID-system-storage | `GetCommTimeouts` | No | MinHook transaction | `kernel32.dll!GetCommTimeouts` | N/A | module/export resolves | `BOOL WINAPI(HANDLE,LPCOMMTIMEOUTS)` | always | `FeatureState` transaction; process | `src/Win32Hooks/Kernel32Hooks.h:14-159`; `src/Win32Hooks/Kernel32Hooks.cpp:82-160`; `src/Rfid/Feature.cpp:23-43,203-228` |
| Game | Kernel32/RFID-system-storage | `FindFirstFileA` | No | MinHook transaction | `kernel32.dll!FindFirstFileA` | N/A | module/export resolves | `HANDLE WINAPI(LPCSTR,LPWIN32_FIND_DATAA)` | test-mode storage | `FeatureState` transaction; process | `src/Win32Hooks/Kernel32Hooks.h:14-159`; `src/Win32Hooks/Kernel32Hooks.cpp:82-160`; `src/Rfid/Feature.cpp:23-43,203-228` |
| Game | Kernel32/RFID-system-storage | `FindFirstFileW` | No | MinHook transaction | `kernel32.dll!FindFirstFileW` | N/A | module/export resolves | `HANDLE WINAPI(LPCWSTR,LPWIN32_FIND_DATAW)` | storage or system-path routing | `FeatureState` transaction; process | `src/Win32Hooks/Kernel32Hooks.h:14-159`; `src/Win32Hooks/Kernel32Hooks.cpp:82-160`; `src/Rfid/Feature.cpp:23-43,203-228` |
| Game | Kernel32/RFID-system-storage | `CreateDirectoryA` | No | MinHook transaction | `kernel32.dll!CreateDirectoryA` | N/A | module/export resolves | `BOOL WINAPI(LPCSTR,LPSECURITY_ATTRIBUTES)` | test-mode storage | `FeatureState` transaction; process | `src/Win32Hooks/Kernel32Hooks.h:14-159`; `src/Win32Hooks/Kernel32Hooks.cpp:82-160`; `src/Rfid/Feature.cpp:23-43,203-228` |
| Game | Kernel32/RFID-system-storage | `CreateDirectoryW` | No | MinHook transaction | `kernel32.dll!CreateDirectoryW` | N/A | module/export resolves | `BOOL WINAPI(LPCWSTR,LPSECURITY_ATTRIBUTES)` | storage or system-path routing | `FeatureState` transaction; process | `src/Win32Hooks/Kernel32Hooks.h:14-159`; `src/Win32Hooks/Kernel32Hooks.cpp:82-160`; `src/Rfid/Feature.cpp:23-43,203-228` |
| Game | Kernel32/RFID-system-storage | `DeleteFileA` | No | MinHook transaction | `kernel32.dll!DeleteFileA` | N/A | module/export resolves | `BOOL WINAPI(LPCSTR)` | storage or system-path routing | `FeatureState` transaction; process | `src/Win32Hooks/Kernel32Hooks.h:14-159`; `src/Win32Hooks/Kernel32Hooks.cpp:82-160`; `src/Rfid/Feature.cpp:23-43,203-228` |
| Game | Kernel32/RFID-system-storage | `DeleteFileW` | No | MinHook transaction | `kernel32.dll!DeleteFileW` | N/A | module/export resolves | `BOOL WINAPI(LPCWSTR)` | storage or system-path routing | `FeatureState` transaction; process | `src/Win32Hooks/Kernel32Hooks.h:14-159`; `src/Win32Hooks/Kernel32Hooks.cpp:82-160`; `src/Rfid/Feature.cpp:23-43,203-228` |
| Game | Kernel32/RFID-system-storage | `GetFileAttributesA` | No | MinHook transaction | `kernel32.dll!GetFileAttributesA` | N/A | module/export resolves | `DWORD WINAPI(LPCSTR)` | storage or system-path routing | `FeatureState` transaction; process | `src/Win32Hooks/Kernel32Hooks.h:14-159`; `src/Win32Hooks/Kernel32Hooks.cpp:82-160`; `src/Rfid/Feature.cpp:23-43,203-228` |
| Game | Kernel32/RFID-system-storage | `GetFileAttributesW` | No | MinHook transaction | `kernel32.dll!GetFileAttributesW` | N/A | module/export resolves | `DWORD WINAPI(LPCWSTR)` | storage or system-path routing | `FeatureState` transaction; process | `src/Win32Hooks/Kernel32Hooks.h:14-159`; `src/Win32Hooks/Kernel32Hooks.cpp:82-160`; `src/Rfid/Feature.cpp:23-43,203-228` |
| Game | Kernel32/RFID-system-storage | `GetDiskFreeSpaceExA` | No | MinHook transaction | `kernel32.dll!GetDiskFreeSpaceExA` | N/A | module/export resolves | `BOOL WINAPI(LPCSTR,PULARGE_INTEGER,PULARGE_INTEGER,PULARGE_INTEGER)` | test-mode storage | `FeatureState` transaction; process | `src/Win32Hooks/Kernel32Hooks.h:14-159`; `src/Win32Hooks/Kernel32Hooks.cpp:82-160`; `src/Rfid/Feature.cpp:23-43,203-228` |
| Game | Kernel32/RFID-system-storage | `GetDiskFreeSpaceExW` | No | MinHook transaction | `kernel32.dll!GetDiskFreeSpaceExW` | N/A | module/export resolves | `BOOL WINAPI(LPCWSTR,PULARGE_INTEGER,PULARGE_INTEGER,PULARGE_INTEGER)` | test-mode storage | `FeatureState` transaction; process | `src/Win32Hooks/Kernel32Hooks.h:14-159`; `src/Win32Hooks/Kernel32Hooks.cpp:82-160`; `src/Rfid/Feature.cpp:23-43,203-228` |
| Game | Kernel32/RFID-system-storage | `MoveFileA` | No | MinHook transaction | `kernel32.dll!MoveFileA` | N/A | module/export resolves | `BOOL WINAPI(LPCSTR,LPCSTR)` | system-path routing | `FeatureState` transaction; process | `src/Win32Hooks/Kernel32Hooks.h:14-159`; `src/Win32Hooks/Kernel32Hooks.cpp:82-160`; `src/Rfid/Feature.cpp:23-43,203-228` |
| Game | Kernel32/RFID-system-storage | `MoveFileW` | No | MinHook transaction | `kernel32.dll!MoveFileW` | N/A | module/export resolves | `BOOL WINAPI(LPCWSTR,LPCWSTR)` | system-path routing | `FeatureState` transaction; process | `src/Win32Hooks/Kernel32Hooks.h:14-159`; `src/Win32Hooks/Kernel32Hooks.cpp:82-160`; `src/Rfid/Feature.cpp:23-43,203-228` |

### NESYS Export Hooks (Current MinHook Transaction)

These 33 distinct exports are assembled into `ApiHookRequest`s, resolved in full, and committed by `OwnedMinHookTransaction`; the owner is global `g_owned_hooks` for process lifetime. “Network override” below means the synthetic adapter/server-override plan is enabled. Service diagnostics and `ExitProcess` install whenever the service plan is enabled (network override or registry override). The game plan also installs `CreateProcessA` so child NESYS service launch can be intercepted.

| Process | Feature | Site | Versioned | Installation | Module/export | Protected span | Expected state | Callback ABI | Enabled when | Current owner / lifetime | Evidence source |
|---|---|---|---|---|---|---|---|---|---|---|---|
| Game + NESYS service | Synthetic adapter | `GetAdaptersInfo` | No | MinHook transaction | `iphlpapi.dll!GetAdaptersInfo` | N/A | export resolves | `ULONG WINAPI(PIP_ADAPTER_INFO,PULONG)` | network override | global owned transaction; process | `src/Nesys/Network/SyntheticNetworkAdapter.h:51-73`; `.cpp:404-440`; `src/Nesys/NesysServicePatch.cpp:124-265` |
| Game | Synthetic adapter | `NotifyAddrChange` | No | MinHook transaction | `iphlpapi.dll!NotifyAddrChange` | N/A | export resolves | `DWORD WINAPI(PHANDLE,LPOVERLAPPED)` | network override | same | same |
| Game | Synthetic adapter | `CancelIPChangeNotify` | No | MinHook transaction | `iphlpapi.dll!CancelIPChangeNotify` | N/A | export resolves | `BOOL WINAPI(LPOVERLAPPED)` | network override | same | same |
| NESYS service | Synthetic adapter | `GetIfTable` | No | MinHook transaction | `iphlpapi.dll!GetIfTable` | N/A | export resolves | `DWORD WINAPI(PMIB_IFTABLE,PULONG,BOOL)` | network override | same | same |
| NESYS service | Synthetic adapter | `GetInterfaceInfo` | No | MinHook transaction | `iphlpapi.dll!GetInterfaceInfo` | N/A | export resolves | `DWORD WINAPI(PIP_INTERFACE_INFO,PULONG)` | network override | same | same |
| NESYS service | Synthetic adapter | `GetNetworkParams` | No | MinHook transaction | `iphlpapi.dll!GetNetworkParams` | N/A | export resolves | `DWORD WINAPI(PFIXED_INFO,PULONG)` | network override | same | same |
| NESYS service | Synthetic adapter | `IpReleaseAddress` | No | MinHook transaction | `iphlpapi.dll!IpReleaseAddress` | N/A | export resolves | `DWORD WINAPI(PIP_ADAPTER_INDEX_MAP)` | network override | same | same |
| NESYS service | Synthetic adapter | `IpRenewAddress` | No | MinHook transaction | `iphlpapi.dll!IpRenewAddress` | N/A | export resolves | `DWORD WINAPI(PIP_ADAPTER_INDEX_MAP)` | network override | same | same |
| NESYS service | Synthetic adapter | `FlushIpNetTable` | No | MinHook transaction | `iphlpapi.dll!FlushIpNetTable` | N/A | export resolves | `DWORD WINAPI(DWORD)` | network override | same | same |
| Game + NESYS service | Server address override | `GetAddrInfoW` | No | MinHook transaction | `ws2_32.dll!GetAddrInfoW` | N/A | export resolves | `INT WSAAPI(PCWSTR,PCWSTR,const ADDRINFOW*,PADDRINFOW*)` | network override | same | `src/Nesys/Network/ServerAddressOverride.h:19-35`; `.cpp:386-410`; `src/Nesys/NesysServicePatch.cpp:124-265` |
| Game + NESYS service | Server address override | `GetAddrInfoExW` | No | MinHook transaction | `ws2_32.dll!GetAddrInfoExW` | N/A | export resolves | `INT WSAAPI(PCWSTR,PCWSTR,DWORD,LPGUID,const ADDRINFOEXW*,PADDRINFOEXW*,timeval*,LPOVERLAPPED,LPLOOKUPSERVICE_COMPLETION_ROUTINE,LPHANDLE)` | network override | same | same |
| NESYS service | Server address override | `gethostbyname` | No | MinHook transaction | `ws2_32.dll!gethostbyname` | N/A | export resolves | `hostent* WSAAPI(const char*)` | network override | same | same |
| Game + NESYS service | Registry override | `RegOpenKeyExA` | No | MinHook transaction | `advapi32.dll!RegOpenKeyExA` | N/A | export resolves | `LSTATUS WINAPI(HKEY,LPCSTR,DWORD,REGSAM,PHKEY)` | registry override configured | same | `src/Nesys/Registry/RegistryConfigOverride.h:18-31`; `.cpp:497-517`; `src/Nesys/NesysServicePatch.cpp:124-265` |
| Game + NESYS service | Registry override | `RegQueryValueExA` | No | MinHook transaction | `advapi32.dll!RegQueryValueExA` | N/A | export resolves | `LSTATUS WINAPI(HKEY,LPCSTR,LPDWORD,LPDWORD,LPBYTE,LPDWORD)` | registry override configured | same | same |
| Game + NESYS service | Registry override | `RegCloseKey` | No | MinHook transaction | `advapi32.dll!RegCloseKey` | N/A | export resolves | `LSTATUS WINAPI(HKEY)` | registry override configured | same | same |
| Game + NESYS service | NESYS thread policy | `SetThreadPriority` | No | MinHook transaction | `kernel32.dll!SetThreadPriority` | N/A | export resolves | `BOOL WINAPI(HANDLE,int)` | network override | same | `src/Nesys/ThreadPriorityOverride.h:22-39`; `.cpp:160-167`; `src/Nesys/NesysServicePatch.cpp:124-265` |
| NESYS service | Request diagnostics | `CreateFileA` | No | MinHook transaction | `kernel32.dll!CreateFileA` | N/A | export resolves | `HANDLE WINAPI(LPCSTR,DWORD,DWORD,LPSECURITY_ATTRIBUTES,DWORD,DWORD,HANDLE)` | service plan enabled | same | `src/Nesys/Diagnostics/RequestPipelineDiagnostics.cpp:1805-1897`; `src/Nesys/NesysServicePatch.cpp:124-265` |
| NESYS service | Request diagnostics | `CreateNamedPipeA` | No | MinHook transaction | `kernel32.dll!CreateNamedPipeA` | N/A | export resolves | exact SDK `decltype(&::CreateNamedPipeA)` / `WINAPI` | service plan enabled | same | same |
| NESYS service | Request diagnostics | `SetFilePointer` | No | MinHook transaction | `kernel32.dll!SetFilePointer` | N/A | export resolves | exact SDK `decltype(&::SetFilePointer)` / `WINAPI` | service plan enabled | same | same |
| NESYS service | Request diagnostics | `WriteFile` | No | MinHook transaction | `kernel32.dll!WriteFile` | N/A | export resolves | `BOOL WINAPI(HANDLE,LPCVOID,DWORD,LPDWORD,LPOVERLAPPED)` | service plan enabled | same | same |
| NESYS service | Request diagnostics | `FlushFileBuffers` | No | MinHook transaction | `kernel32.dll!FlushFileBuffers` | N/A | export resolves | `BOOL WINAPI(HANDLE)` | service plan enabled | same | same |
| NESYS service | Request diagnostics | `CloseHandle` | No | MinHook transaction | `kernel32.dll!CloseHandle` | N/A | export resolves | `BOOL WINAPI(HANDLE)` | service plan enabled | same | same |
| NESYS service | Request diagnostics | `WinHttpOpen` | No | MinHook transaction | `winhttp.dll!WinHttpOpen` | N/A | export resolves | exact SDK `decltype(&::WinHttpOpen)` / `WINAPI` | service plan enabled | same | same |
| NESYS service | Request diagnostics | `WinHttpSetTimeouts` | No | MinHook transaction | `winhttp.dll!WinHttpSetTimeouts` | N/A | export resolves | exact SDK `decltype(&::WinHttpSetTimeouts)` / `WINAPI` | service plan enabled | same | same |
| NESYS service | Request diagnostics | `WinHttpConnect` | No | MinHook transaction | `winhttp.dll!WinHttpConnect` | N/A | export resolves | exact SDK `decltype(&::WinHttpConnect)` / `WINAPI` | service plan enabled | same | same |
| NESYS service | Request diagnostics | `WinHttpOpenRequest` | No | MinHook transaction | `winhttp.dll!WinHttpOpenRequest` | N/A | export resolves | exact SDK `decltype(&::WinHttpOpenRequest)` / `WINAPI` | service plan enabled | same | same |
| NESYS service | Request diagnostics | `WinHttpSendRequest` | No | MinHook transaction | `winhttp.dll!WinHttpSendRequest` | N/A | export resolves | exact SDK `decltype(&::WinHttpSendRequest)` / `WINAPI` | service plan enabled | same | same |
| NESYS service | Request diagnostics | `WinHttpReceiveResponse` | No | MinHook transaction | `winhttp.dll!WinHttpReceiveResponse` | N/A | export resolves | exact SDK `decltype(&::WinHttpReceiveResponse)` / `WINAPI` | service plan enabled | same | same |
| NESYS service | Request diagnostics | `WinHttpQueryDataAvailable` | No | MinHook transaction | `winhttp.dll!WinHttpQueryDataAvailable` | N/A | export resolves | exact SDK `decltype(&::WinHttpQueryDataAvailable)` / `WINAPI` | service plan enabled | same | same |
| NESYS service | Request diagnostics | `WinHttpReadData` | No | MinHook transaction | `winhttp.dll!WinHttpReadData` | N/A | export resolves | exact SDK `decltype(&::WinHttpReadData)` / `WINAPI` | service plan enabled | same | same |
| NESYS service | Request diagnostics | `WinHttpCloseHandle` | No | MinHook transaction | `winhttp.dll!WinHttpCloseHandle` | N/A | export resolves | exact SDK `decltype(&::WinHttpCloseHandle)` / `WINAPI` | service plan enabled | same | same |
| NESYS service | NESYS termination | `ExitProcess` | No | MinHook transaction | `kernel32.dll!ExitProcess` | N/A | export resolves | `void WINAPI(UINT)` | service plan enabled | same | `src/Nesys/NesysServicePatch.cpp:41-79,124-265` |
| Game | NESYS child launcher | `CreateProcessA` | No | MinHook transaction | `kernel32.dll!CreateProcessA` | N/A | export resolves | `BOOL WINAPI(LPCSTR,LPSTR,LPSECURITY_ATTRIBUTES,LPSECURITY_ATTRIBUTES,BOOL,DWORD,LPVOID,LPCSTR,LPSTARTUPINFOA,LPPROCESS_INFORMATION)` | game role | same | `src/Nesys/Launcher/NesysServiceLauncher.cpp:13-26,268-275`; `src/Nesys/NesysServicePatch.cpp:124-265` |

## Shared Win32 Behavior Order

The baseline owns 27 original slots (`src/Win32Hooks/Kernel32Hooks.h:17-45`):
15 unconditional hooks and 12 conditional filesystem hooks
(`Kernel32Hooks.cpp:112-160`). All detours use `GuardDetour`; caught C++
exceptions return the export failure sentinel with `ERROR_UNHANDLED_EXCEPTION`.
This does not catch invalid-memory access or validate `active_`.

| Export | Pre-call order | Original call | Post-call order |
|---|---|---|---|
| CreateFileA/W | RFID COM2 completion -> system-path routing -> test-mode-storage transform | at most once | NESYS pipe-open observation |
| WriteFile | RFID COM2 completion | at most once | NESYS pipe-write observation |
| ReadFile | RFID COM2 completion | at most once | none |
| FlushFileBuffers | none | exactly once | NESYS pipe-flush observation |
| CloseHandle | RFID COM2 completion | at most once | NESYS tracked-handle removal |

The core table above describes feature order. Current implementation details:
system-path CreateFile matches return immediately and bypass both storage and
pipe-open observation. Non-system branches classify the original path for pipe
observation. Write, Flush and Close perform tracked-handle lookup before the
original (Write/Flush also timestamp); these are observer bookkeeping, not
completion handlers. COM2 Close returns without tracked-handle removal.
Evidence: `Kernel32Hooks.cpp:163-430`.

| Export | Routing and original selection | Enabled condition |
|---|---|---|
| CreateFileA | Exact non-null "COM2" completes; system match calls original W; otherwise storage transforms before original A | Always |
| CreateFileW | Exact non-null L"COM2" completes; system then storage; original W | Always |
| FindFirstFileA | Storage only; original A exactly once | Storage enabled |
| FindFirstFileW | System first, storage only if unmatched; original W at most once | Either path owner enabled |
| CreateDirectoryA | Storage only; original A exactly once | Storage enabled |
| CreateDirectoryW | System first, storage only if unmatched; original W at most once | Either path owner enabled |
| DeleteFileA | System match switches to W; otherwise storage then A | Either path owner enabled |
| DeleteFileW | System first, storage only if unmatched; original W | Either path owner enabled |
| GetFileAttributesA | System match switches to W; otherwise storage then A | Either path owner enabled |
| GetFileAttributesW | System first, storage only if unmatched; original W | Either path owner enabled |
| GetDiskFreeSpaceExA/W | Storage enabled sends nullptr directory; matching original exactly once | Storage enabled |
| MoveFileA | System routes source then destination; either match converts unmatched non-null ANSI operand and calls W; neither match calls A | System enabled |
| MoveFileW | System routes source then destination; original W | System enabled |

Filesystem detail evidence: `Kernel32Hooks.cpp:610-834`.
System routing returns null as unmatched, but converts non-null ANSI before
checking for D:\system and can fail conversion/allocation/filesystem work
(`SystemPathRouter.cpp:110-190`). Storage routing leaves null unchanged and
catches failures, falling back to the original path (`TestModeStorage/Hooks.cpp:16-120`).
It matches absolute D: paths beginning with a 32-hex-digit underscore
three-digit root (`TestModeStorage/Redirector.cpp:20-67`).

| Export / branch | Null handling and initialized outputs | Completion / validation | Original and LastError |
|---|---|---|---|
| CreateFileA COM2 | Exact non-null "COM2"; no output pointer | Starts worker; failed open gives INVALID_HANDLE_VALUE with worker error | No original; success has no explicit preserve/set |
| CreateFileW COM2 | Exact non-null L"COM2"; same outputs | Same | Same |
| CreateFileA/W normal | Null path passed through | System routing failure gives INVALID_HANDLE_VALUE and route error | At most once; save incoming at entry, restore before original; non-system branch captures result error and restores after observer |
| WriteFile emulated | Non-null count initialized to 0; null buffer valid only for zero length | Missing count, OVERLAPPED, or nonzero length with null buffer: FALSE/ERROR_INVALID_PARAMETER; port error propagated; count overflow ERROR_ARITHMETIC_OVERFLOW; success sets transferred count | No original; failure sets error, success has no explicit preserve/set |
| WriteFile normal | No wrapper output initialization/validation | No completion | Original once; no explicit incoming preservation around lookup/timestamp; capture immediate original error and restore after observer |
| ReadFile emulated | Non-null count initialized to 0; same null/OVERLAPPED checks as Write | Copies min(requested, unread reply), retaining remainder; zero/no reply succeeds with count 0; port/overflow errors propagated | No original; failure sets error, success has no explicit preserve/set |
| ReadFile normal | No wrapper output initialization/validation | None | Direct original once; wrapper does not read/write error |
| FlushFileBuffers | No output or wrapper validation; no emulated-handle special case | None | Original exactly once; lookup/timestamp has no explicit incoming preservation; immediate original error restored after observer |
| CloseHandle emulated | No outputs | Resets RFID bus and serial session, TRUE | No original; no explicit preserve/set |
| CloseHandle normal | No output/validation | None | Original once; lookup has no explicit incoming preservation; immediate original error restored after removal |
| GetCommModemStatus emulated | Reject null status; else assign MS_CTS_ON iff RFID address assigned, otherwise 0 | Null -> FALSE/ERROR_INVALID_PARAMETER | No original; success has no explicit preserve/set |
| EscapeCommFunction emulated | No outputs | Accept SETDTR, CLRDTR, SETRTS, CLRRTS, SETXOFF, SETXON, SETBREAK, CLRBREAK; others FALSE/ERROR_INVALID_FUNCTION | No original; failure sets port error; success has no explicit preserve/set |
| ClearCommError emulated | errors/status independently optional; errors=0, full status assignment with pending cbInQue | Always TRUE | No original; no explicit preserve/set |
| SetCommMask emulated | No outputs | Accept/store every mask, TRUE | No original; no explicit preserve/set |
| SetupComm emulated | No outputs | Accept/store both queue sizes, TRUE | No original; no explicit preserve/set |
| GetCommState emulated | Reject null; otherwise fully assign DCB | Null -> FALSE/ERROR_INVALID_PARAMETER | No original; success has no explicit preserve/set |
| SetCommState emulated | No outputs | Reject null, wrong DCBlength, ByteSize != 8, parity != NOPARITY, stop bits != ONESTOPBIT with FALSE/ERROR_INVALID_PARAMETER; other fields stored | No original; success has no explicit preserve/set |
| SetCommTimeouts emulated | No outputs | Reject null with FALSE/ERROR_INVALID_PARAMETER; otherwise store full structure | No original; success has no explicit preserve/set |
| GetCommTimeouts emulated | Reject null; otherwise fully assign output | Null -> FALSE/ERROR_INVALID_PARAMETER | No original; success has no explicit preserve/set |
| All nine COM APIs, normal handle | No wrapper initialization or validation | None | Direct original exactly once; wrapper does not read/write error |
| FindFirstFileA | Null path/output passed through; no find-data initialization | Storage fail-open, no completion | A exactly once; incoming restored before original |
| FindFirstFileW | Null path/output passed through | System error -> INVALID_HANDLE_VALUE; storage fail-open | W at most once; failure sets route error, otherwise incoming restored |
| CreateDirectoryA | Null path/security passed through | Storage fail-open | A exactly once; incoming restored |
| CreateDirectoryW | Null path/security passed through | System error -> FALSE | W at most once; failure sets route error, otherwise incoming restored |
| DeleteFileA/W | Null path passed through | System error -> FALSE | Selected original at most once; route error on failure, incoming restored otherwise |
| GetFileAttributesA/W | Null path passed through | System error -> INVALID_FILE_ATTRIBUTES | Selected original at most once; route error on failure, incoming restored otherwise |
| GetDiskFreeSpaceExA/W | No outputs initialized; directory becomes null when storage enabled | No wrapper validation | Matching original exactly once; incoming restored |
| MoveFileA | Null operands passed through (also in converted wide call) | Source/destination routing or required ANSI conversion error -> FALSE | Selected original at most once; error set on failure, incoming restored otherwise |
| MoveFileW | Null operands passed through | Source/destination route error -> FALSE | W at most once; route error on failure, incoming restored otherwise |

Normal successful Win32 calls do not necessarily define LastError; "original
error" means the value immediately after the original returns.
Evidence: `Kernel32Hooks.cpp:163-834`, `Rfid/ComPortState.cpp:75-218`.
The RFID handle is 0x1337; Open starts its worker once; Close resets the bus,
decoder, replies, DCB, timeouts, masks, queues, line states and sequence
(`Rfid/Runtime.cpp:51-97`, `Rfid/ComPortState.cpp:44-54,299-315`).
NESYS post-call helpers are `RequestPipelineDiagnostics.cpp:1732-1803`;
Kernel32 explicitly restores original LastError after them.

## Production Seam Ledger

# Production seam ledger

Decision basis: `keep` requires multiple production implementations or a real OS, driver, process, thread, COM, device, or ownership boundary. A sole fake/test adapter does not count. “No meaningful test” refers to the current five production test executables under `tests/`.

### Shared platform, startup, and configuration

| Seam | Production callers | Alternate production implementation | Lifetime/concurrency boundary | Existing meaningful test | Decision | Reason |
|---|---|---|---|---|---|---|
| `OriginalKernel32Api` (`src/Win32Hooks/Kernel32Hooks.h:17`) | `Kernel32HookRouter` stores/calls captured originals (`Kernel32Hooks.cpp:59`, `Kernel32Hooks.h:166`) | None; slots receive actual hook trampolines | Detour ABI and original-function lifetime | None | **keep** | The table is durable trampoline ownership across multiple Kernel32 hooks, a genuine hook/OS boundary. |
| `OriginalJapaneseLocaleApi` (`src/Locale/JapaneseLocaleCompatibility.h:25`) | Locale hook installation fills `g_originals` (`JapaneseLocaleCompatibility.cpp:15,155-223`) | None; slots receive actual hook trampolines | Detour ABI and process-global original-function lifetime | None | **keep** | It owns the original Win32 locale/time entry points required by active detours. |
| `ConfigReadActions` (`src/Loader/StartupConfiguration.h:18`) | Nested in `StartupConfigurationActions`; consumed by startup loader (`StartupConfiguration.cpp:122,211`) | None | Ordinary synchronous startup I/O | Startup policy/repair cases in `tests/Config/ConfigStartupTests.cpp:223-472` use fakes | **remove** | One production adapter exists solely to substitute file reads in tests; the policy can be tested below direct I/O. |
| `StartupConfigurationActions` (`src/Loader/StartupConfiguration.h:26`) | Startup configuration load (`StartupConfiguration.cpp:122,211`); production assembly at `:195-203` | None | Synchronous process startup; stage-aware file/root/probe/persistence boundary | Meaningful state/error/repair coverage in `ConfigStartupTests.cpp:223-472` | **keep** | Plan 08 explicitly expects this retained. PrepareProcessConfiguration coordinates strict validation, native-root/storage fallback, revalidation, and mandatory persistence before publishing immutable settings; its error/ordering contract is exercised through the production entry point. |
| `AtomicConfigWriteActions` (`src/Config/ConfigDocument.h:135`) | Atomic config replacement (`ConfigDocument.cpp:637`); startup production wiring (`StartupConfiguration.cpp:203`) | None | Temporary-file and replacement ownership during one call | Atomic-repair behavior in `ConfigStartupTests.cpp:318-403` | **remove** | One concrete filesystem implementation and one fake adapter; direct platform ownership should sit in a shared HANDLE/file primitive. |
| `DirectoryActions` (`src/SystemPath/SystemRoot.h:58`) | `PrepareGameSystemRoot` (`SystemRoot.h:68-71`, `SystemRoot.cpp:204`); startup at `StartupConfiguration.cpp:281` | None | Single synchronous `create_directories` call | Indirect startup cases in `ConfigStartupTests.cpp` | **remove** | It wraps one standard-library call and owns no durable directory resource. |
| `StartupFatalActions` (`src/SystemPath/StartupFatal.h:10`) | Fatal publication helpers (`StartupFatal.cpp:61,77`); production adapter at `:47` | None | Terminal startup reporting only | None | **remove** | Explicit known removal: one production forwarding table, no independent runtime owner. |

### Shared hooking, executable memory, and system-path hooks

| Seam | Production callers | Alternate production implementation | Lifetime/concurrency boundary | Existing meaningful test | Decision | Reason |
|---|---|---|---|---|---|---|
| `platform::hooking::ResolverApi` (`src/Platform/Win32/Hooking/MinHookTransaction.h:23`) | Shared `MinHookTransaction` (`MinHookTransaction.cpp:39`, header `:82-91`) | None | Synchronous DLL/export lookup | None | **remove** | Explicit known removal; it forwards `GetModuleHandle`/`GetProcAddress` for fake injection. |
| `platform::hooking::MinHookApi` (`MinHookTransaction.h:28`) | Shared hook transaction (`MinHookTransaction.cpp:40`, header `:83,91`) | None | MinHook global state is real, but this table does not own it independently | None | **remove** | Explicit known removal; one concrete MinHook adapter. Preserve concrete process-lifetime hook ownership; the doomed process performs no reverse rollback. |
| `nesys_service::MinHookApi` (`src/Nesys/NesysHookTransaction.h:46`) | `OwnedMinHookTransaction` (`NesysHookTransaction.cpp:98-193`), created by `NesysServicePatch.cpp:231-232` | None | Owns NESYS hook transaction lifetime | None | **remove** | Explicit “both MinHook APIs” removal; preserve concrete process-lifetime hook ownership, with no reverse rollback in the doomed process. |
| `TtxGuardInstallActions` (`src/SystemPath/TtxInitGuard.h:77`) | `InstallTtxInitGuard` (`TtxInitGuard.cpp:142`); production construction at `:217` | None | Hook install transaction | None | **remove** | Explicit required removal; one resolver/SafetyHook forwarding adapter. Preserve concrete process-lifetime hook ownership and fatal installation handling. |
| `TtxGuardRuntimeActions` (`TtxInitGuard.h:54`) | `InvokeTtxUdlInitGuard` (`TtxInitGuard.cpp:112`); constructed by live detour at `:337` | None | Hook callback and captured-original call | None | **remove** | One hook-local adapter. Original trampoline and failure publication can be owned directly by `TtxInitGuard`. |
| `GameBinaryPatchActions` (`src/Patches/GameCompatibility/GameBinaryPatch.h:69`) | Shared install path (`GameBinaryPatch.cpp:234-360`); AutoPlay and SongUnlock (`AutoPlayPatch.cpp:290`, `SongUnlockPatch.cpp:122`) | None | Guarded executable-image mutation | None | **remove** | Explicit known removal: feature-specific executable-memory action table with one production implementation. Preserve complete preflight before mutation and abort fatally on later failure; do not reverse rollback. |
| `FramerateMemoryApi` (`src/Patches/Framerate/FrameratePatchTransaction.h:42`) | Framerate transaction (`FrameratePatchTransaction.cpp:77`), runtime creation (`FrameratePatch.cpp:352`) | None | Executable-memory protection/write/flush transaction | None | **remove** | Explicit known removal; consolidate on shared executable-memory infrastructure while preserving complete preflight and fatal no-rollback failure behavior. |
| `TimingMemoryApi` (`src/Patches/TestModeTiming/TimingSettingsGameAbi.h:121`) | Timing patch transaction (`TimingSettingsGameAbi.cpp:292`), runtime at `TimingSettingsPatch.cpp:43` | None | Executable-memory transaction | None | **remove** | Explicit known removal; preserve complete preflight before mutation and fatal no-rollback failure behavior. |
| `WidescreenInstallActions` (`src/Patches/WindowedWidescreen/WindowedWidescreenPatchTransaction.h:26`) | Widescreen install transaction (`WindowedWidescreenPatchTransaction.cpp:42,59`), live construction (`WindowedWidescreenPatch.cpp:3501`) | None | Guarded hook/patch installation | None | **remove** | Explicit known removal; use shared patch/hook infrastructure while preserving complete preflight, concrete process-lifetime hook ownership, and fatal no-rollback failure behavior. |

### Input and RFID

| Seam | Production callers | Alternate production implementation | Lifetime/concurrency boundary | Existing meaningful test | Decision | Reason |
|---|---|---|---|---|---|---|
| `HidApi` (`src/Input/Win32/HidApi.h:8`) | `RawHidController` (`RawHidController.h:29,80`, `.cpp:199-205`) | None | HID parsing calls, but ownership stays in `RawHidController` | None | **remove** | Explicit known removal; complete one-to-one HidP forwarding table. |
| `ForegroundApi` (`src/Input/Polling/ForegroundPolicy.h:7`) | `NativeInputWorker` constructs and polls it (`InputPollingRuntime.cpp:202-206,647,733`) | None | Polled on the input worker thread | None | **remove** | One concrete foreground query tuple; pure transition policy already lives separately in `ForegroundTransitionTracker`. |
| `RawInputApi` (`src/Input/Win32/RawInputPacket.h:13`) | `RawInputPacketBuffer` (`RawInputPacket.cpp:64-74`), owned by input worker (`InputPollingRuntime.cpp:732`) | None | Win32 message payload valid only during packet read | None | **remove** | One `GetRawInputData` forwarding slot and no alternate production behavior; direct the OS call inside the packet owner. |
| `XInputApi` (`src/Input/Win32/XInputApi.h:12`) | Dynamically loaded by input worker (`InputPollingRuntime.cpp:510`), owned by `XInputController` (`XInputController.h:33,62`) | Multiple real system DLL candidates resolved in `XInputApi.cpp:44-84` | DLL module and function-pointer lifetime; controller/device boundary | None | **keep** | Explicit retention example: dynamically loaded XInput functions and module ownership vary at runtime. |
| `ImeSuppressionActions` (`src/Input/Win32/ImeSuppression.h:14`) | Production wrapper in `ImeSuppression.cpp:32-34`, invoked during game startup (`DllMain.cpp:529`) | None | Synchronous per-thread Win32 call | Fake-policy plus real-window integration in `tests/Input/Win32/ImeSuppressionTests.cpp:48-173` | **remove** | One adapter. Preserve the real-window integration behavior while calling Win32 directly. |
| `CardWorkerApi` (`src/Rfid/Runtime.h:16`) | RFID `Runtime` owns/calls it (`Runtime.cpp:56-68`, header `:43-48`) | None | Detached worker threads capture `Runtime`; keyboard polling and sleeps occur off-thread | None | **remove** | Plan 08's caller audit is confirmed: Runtime owns once flags, worker lifetime and all three operations. The table only forwards thread start, key state and sleep; direct calls preserve the existing detached-worker boundary inside Runtime. |
| `FeatureHookLayerActions` (`src/Rfid/Feature.h:30`) | RFID hook-layer installer (`Feature.cpp:90`), live construction (`:210`) | None | Synchronous feature initialization | None | **remove** | One production pair of calls used only to inject hypothetical install outcomes. |
| `RawInputMessageSink` (`src/Input/Win32/Win32InputWindow.h:10`) | `Win32InputWindow` dispatches WndProc messages (`Win32InputWindow.cpp:43`); `NativeInputWorker` implements it (`InputPollingRuntime.cpp:196`) | None | Win32 WndProc callback into input-thread-owned worker | None | **keep** | Genuine message callback and object-lifetime boundary between the window and input worker. |
| `ControllerStateView` (`src/Input/Win32/ControllerStateView.h:19`) | Binding evaluator/capture (`ControllerBindingEvaluator.cpp:28`, `InputCapture.cpp:43,134`), polling runtime (`InputPollingRuntime.cpp:579,636`) | `XInputController` and `RawHidController` (`XInputController.h:21`, `RawHidController.h:20`) | Device-specific state/view lifetime | Binding ownership/evaluation case in `ConfigContractTests.cpp:118,509` | **keep** | Two real production device implementations. |

### NESYS process

| Seam | Production callers | Alternate production implementation | Lifetime/concurrency boundary | Existing meaningful test | Decision | Reason |
|---|---|---|---|---|---|---|
| `ServiceChildApi` (`src/Nesys/Launcher/NesysServiceLauncher.h:11`) | Child finalization (`NesysServiceLauncher.cpp:181,212`); production adapter `:199` | None | Suspended child process/thread HANDLE ownership, resume/wait/terminate/close | None | **keep** | Genuine process and HANDLE ownership boundary; cleanup sequencing is independent of launcher policy. |

### ASIO

| Seam | Production callers | Alternate production implementation | Lifetime/concurrency boundary | Existing meaningful test | Decision | Reason |
|---|---|---|---|---|---|---|
| `AsioComActions` (`src/Audio/Asio/AsioDriver.h:51`) | `ProductionAsioDriverFactory` (`AsioDriver.cpp:275-282`) | None | COM creation occurs on the ASIO owner thread, but the table only wraps `CoCreateInstance` | None | **remove** | Focused audit confirms one forwarding slot beneath the real factory/driver ownership seams. |
| `IAsioDriver` (`AsioDriver.h:18`) | Capability probe/control panel/output backend (`AsioCapabilityProbe.cpp:62,111,150`, `AsioControlPanel.cpp:27,51`, `AsioOutputBackend.cpp:207`) | One wrapper, `AsioDriver` (`AsioDriver.cpp:35`) over arbitrary installed IASIO implementations | Live IASIO/driver session, buffers, callbacks, and lifecycle | None | **keep** | Genuine third-party driver/COM and callback ownership boundary. |
| `IAsioDriverFactory` (`AsioDriver.h:42`) | Probe, control panel, output backend (`AsioCapabilityProbe.cpp:145`, `AsioControlPanel.cpp:46`, `AsioOutputBackend.h:26,60`) | `ProductionAsioDriverFactory` only (`AsioDriver.h:65`) | Creates and transfers unique IASIO ownership | None | **keep** | Factory owns COM-driver construction and failure translation across three production clients. |
| `AsioRegistryActions` (`src/Audio/Asio/AsioDriverCatalog.h:38`) | `ProductionAsioRegistrySource` (`AsioDriverCatalog.cpp:426-431`) | None | Synchronous registry enumeration only | None | **remove** | One action under the actual registry-source boundary; fake native registry behavior does not justify it. |
| `IAsioRegistrySource` (`AsioDriverCatalog.h:20`) | Driver enumeration/resolution (`AsioDriverCatalog.cpp:304,384`), probe/control panel/output backend | `ProductionAsioRegistrySource` only (`AsioDriverCatalog.h:50`) | 32-bit registry view and installed-driver discovery | None | **keep** | Genuine OS registry/driver-discovery boundary shared by three production paths. |
| `IAsioIsolatedProcessActions` (`src/Audio/Asio/AsioIsolatedProcess.h:66`) | `AsioProbeClient` and `AsioControlPanelClient` (`AsioProbeClient.cpp:42-49`, `AsioControlPanelClient.cpp:44-51`) | `ProductionAsioIsolatedProcessActions` (`AsioIsolatedProcess.h:76`) | Child process, job, pipes, timeout, cancellation, output-cap, and HANDLE ownership | None | **keep** | Two production callers share a substantial process isolation boundary. |
| `IAsioProbeClient` (`src/Audio/Asio/AsioProbeClient.h:17`) | ConfigGUI editor/worker (`tools/ConfigGUI/AudioBackendEditorModel.cpp:546`, `AudioOperationWorker.cpp:33,50`) | `AsioProbeClient` only (`AsioProbeClient.h:32`) | ConfigGUI worker operation crossing an isolated child-process boundary | None | **keep** | The interface transfers a bounded process operation into the GUI worker and separates GUI lifetime from probe implementation. |
| `IAsioControlPanelClient` (`src/Audio/Asio/AsioControlPanelClient.h:20`) | ConfigGUI audio worker (`AudioOperationWorker.cpp:35,51`) | `AsioControlPanelClient` only (`AsioControlPanelClient.h:29`) | Cancellable GUI worker operation and isolated child process | None | **keep** | Genuine worker/cancellation/process boundary. |
| `AsioControlPanelActions` (`src/Audio/Asio/AsioControlPanel.h:18`) | `OpenAsioControlPanel` (`AsioControlPanel.cpp:44-130`); one live callback from `tools/ConfigGUI/AsioControlPanelMode.cpp:66-72` | None | Waits for visible driver-owned windows, but does not own them | None | **remove** | One callback supplied by one production caller; fold the wait behavior into the control-panel mode/client. |

### WASAPI and shared audio

| Seam | Production callers | Alternate production implementation | Lifetime/concurrency boundary | Existing meaningful test | Decision | Reason |
|---|---|---|---|---|---|---|
| `IWasapiApi` (`src/Audio/Wasapi/WasapiEndpoint.h:90`) | `WasapiEndpoint` (`WasapiEndpoint.cpp:339-369`) and exclusive engine startup (`ExclusiveAudioEngine.cpp:250`) | `Win32WasapiApi` (`WasapiEndpoint.cpp:30`) | COM apartment, endpoint/audio-client/service/event/MMCSS ownership; shutdown must occur on initializing audio thread | None | **keep** | Genuine COM, device, thread-affinity, and HANDLE boundary. |
| `WasapiPresentedOutputClockActions` (`src/Audio/Wasapi/WasapiPresentedOutputClock.h:12`) | Constructed by exclusive engine (`ExclusiveAudioEngine.cpp:285-286`) | None | QPC query during concurrent clock publication/read | None | **remove** | One QPC forwarding adapter; concurrency belongs in the publication object. |
| `IPresentedOutputClock` (`src/Audio/Mixer/PresentedOutputClock.h:9`) | `AudioRenderCore` (`AudioRenderCore.cpp:22-35,153-164`) | `WasapiPresentedOutputClock` (`WasapiPresentedOutputClock.h:24`); ASIO deliberately supplies no presented clock | Backend-dependent output-clock availability | No direct test | **keep** | Real backend variation: WASAPI has device-presented position while ASIO uses a different cursor model/no instance. |
| `ExactJudgementTimeline` (`src/Audio/ExactJudgementTimeline.h:46`) | Cross-feature registry and Absolute Judgement (`ExactJudgementTimeline.cpp:37-103`, `AbsoluteJudgementRuntime.cpp:430-442`) | `ExactWasapiClock` (`ExactWasapiClock.h:23`) | Shared/weak provider lifetime and audio-thread publication versus judgement-thread reads | `ExactWasapiClockCompatibilityTests.cpp:36-44` | **keep** | Genuine concurrency and provider ownership boundary, independently consumed outside the audio feature. |
| `IAudioEngineServices` (`src/Audio/DirectSound/DirectSoundFacade.h:22`) | DirectSound facade and backend controller (`DirectSoundFacade.cpp:85,322,480,532`, `AudioBackendController.cpp:70,185-198`) | `ExclusiveAudioEngine`, `AsioOutputBackend`, delegating `AudioBackendController` (`ExclusiveAudioEngine.h:68`, `AsioOutputBackend.h:19`, `AudioBackendController.h:81`) | Backend engine and voice lifetime; audio callback/worker threads | None | **keep** | Multiple real production backends. |
| `IAudioEngineController` (`src/Audio/AudioBackendController.h:19`) | DirectSound facade/device/buffers (`DirectSoundFacade.h:43,109-202`) | `AudioBackendController` only (`AudioBackendController.h:81`) | One-time backend startup synchronized by mutex/condition variable (`AudioBackendController.h:123-129`) and facade lifetime | None | **keep** | Genuine ownership/concurrency boundary between COM-facing DirectSound objects and selected backend engine. |
| `IAudioBackendControllerFactory` (`AudioBackendController.h:26`) | Audio hook initialization (`AudioPatch.cpp:866-914`, `AudioPatch.cpp:1370`) | `ProductionAudioBackendControllerFactory` only (`AudioPatch.cpp:866`) | Lazy singleton/controller allocation from hooked DirectSound entry | None | **remove** | One production factory introduced for synthetic substitution; ownership can remain in the audio patch runtime. |
| `IWasapiOutputBackendFactory` (`AudioBackendController.h:33`) | `AudioBackendController` (`AudioBackendController.cpp:15,70`), production assembly in `AudioPatch.cpp:708-758` | `ProductionWasapiOutputBackendFactory` only | Starts/transfers an audio-thread-owned engine | None | **remove** | One concrete adapter; the backend distinction is already represented by `IAudioEngineServices` and controller policy. |
| `IAsioOutputBackendFactory` (`AudioBackendController.h:42`) | `AudioBackendController` (`AudioBackendController.cpp:16,70`), production assembly in `AudioPatch.cpp:763-782` | `ProductionAsioOutputBackendFactory` only | Starts/transfers ASIO driver/callback ownership | None | **remove** | One concrete adapter; direct owned backend construction preserves the real ASIO seams. |
| `IAudioBackendControllerReporter` (`AudioBackendController.h:72`) | `AudioBackendController` (`AudioBackendController.cpp:17`), production reporter `AudioPatch.cpp:785-861` | `ProductionAudioBackendControllerReporter` only | Fatal publication from startup/controller failure | None | **remove** | One forwarding reporter with no independent lifetime or alternate production sink. |
| `IAudioEngineObserver` (`src/Audio/Wasapi/ExclusiveAudioEngine.h:56`) | Exclusive engine owns shared observer (`ExclusiveAudioEngine.h:140`, `.cpp:29-98`); production observer `AudioPatch.cpp:657-703` | One production observer | Audio and monitor threads call a shared, value-owning observer after initialization | None | **keep** | Genuine cross-thread callback/lifetime boundary. Prior crash evidence proves action data must outlive startup stack frames. |
| `AudioMinHookApi` (`src/Audio/AudioPatch.h:38`) | Audio hook install/rollback (`AudioPatch.cpp:1108-1149,1400-1513`) | None | Hook transaction only | None | **remove** | Explicit known removal: audio’s embedded MinHook table duplicates shared hook operations. Preserve concrete process-lifetime hook ownership; do not reverse rollback in the doomed process. |
| `AudioResolverApi` (`src/Audio/AudioPatchInternal.h:10`) | Audio hook setup (`AudioPatch.cpp:1135,1401`) | None | DLL/export lookup only | None | **remove** | Explicit known removal; one resolver adapter. |
| `AudioPatchPlatformActions` (`AudioPatchInternal.h:18`) | Error/reporting/factory/observer production paths (`AudioPatch.cpp:520-703,709-861,1149-1262`) | None | Long-lived callbacks cross ASIO/WASAPI control and monitor threads | None | **remove** | Explicit known removal. The current table has a real lifetime hazard, but the durable fix is owned reporter/observer state, not one monolithic platform action table. |

### Renderer and widescreen

| Seam | Production callers | Alternate production implementation | Lifetime/concurrency boundary | Existing meaningful test | Decision | Reason |
|---|---|---|---|---|---|---|
| `RendererResetHookPairActions` (`src/Patches/RendererDeviceLoss/RendererDeviceLossPatch.h:39`) | `RendererResetHookPair` (`RendererDeviceLossPatch.cpp:26`, live construction `:925`) | None | Two-hook prepare/enable/reset transaction | None | **remove** | One feature-specific hook adapter; preserve concrete process-lifetime hook ownership, with no reverse rollback in the doomed process. |
| `RendererResetFailureActions` (`RendererDeviceLossPatch.h:82`) | Renderer reset lifecycle (`RendererDeviceLossPatch.cpp:130,901`); widescreen callback at `WindowedWidescreenPatch.cpp:607` | None | Cross-feature failure notification during reset | None | **keep** | Genuine feature-ownership boundary: renderer reset reports failure without taking ownership of widescreen state. |
| `RendererDeviceLostActions` (`RendererDeviceLossPatch.h:249`) | Device-lost cleanup (`RendererDeviceLossPatch.cpp:538`) | None | Native renderer resource cleanup in one hook callback | None | **remove** | One executable-memory/COM forwarding adapter with no alternate production implementation. |
| `RendererInstallActions` (`RendererDeviceLossPatch.h:323`) | Renderer patch install (`RendererDeviceLossPatch.cpp:634`) | None | Executable read and hook install transaction | None | **remove** | Feature-specific executable-memory/hook table; preserve complete preflight and concrete process-lifetime hook ownership, with fatal no-rollback handling. |
| `CompositorDeviceActions` (`src/Patches/WindowedWidescreen/NativeCanvasCompositor.h:12`) | `NativeCanvasCompositor` owns it (`NativeCanvasCompositor.cpp:25`, header `:71,147`) | Produced by `D3D9CompositorDevice::DeviceActions` (`D3D9CompositorDevice.cpp:375`) | D3D9 COM resources and device-reset-sensitive ownership | None | **keep** | Genuine device/resource boundary between compositor policy and D3D9 implementation. |
| `NativeBatchActions` (`src/Patches/WindowedWidescreen/D3D9CompositorDevice.h:50`) | D3D9 device stores callbacks (`D3D9CompositorDevice.h:70,170`); wired by widescreen runtime (`WindowedWidescreenPatch.cpp:539`) | None | Observes native batch resource create/release across device reset | None | **keep** | Genuine cross-component resource-lifetime observer required to coordinate device-loss cleanup. |
| `ConfigApplyHookActions` (`WindowedWidescreenPatch.h:55`) | Only live construction and hook body in `WindowedWidescreenPatch.cpp:2712,2842` | None | Hook-local | None | **remove** | Explicit class of known removal: public Widescreen hook action with no external production caller. |
| `WindowDeviceHookActions` (`WindowedWidescreenPatch.h:78`) | `WindowedWidescreenPatch.cpp:2747,2897` | None | Hook-local | None | **remove** | Same one-adapter public hook seam. |
| `LogicalResolutionSetHookActions` (`WindowedWidescreenPatch.h:93`) | `WindowedWidescreenPatch.cpp:2646,2938` | None | Hook-local | None | **remove** | Same. |
| `LogicalTargetDimensionSetHookActions` (`WindowedWidescreenPatch.h:108`) | `WindowedWidescreenPatch.cpp:2682,2961` | None | Hook-local | None | **remove** | Same. |
| `FrameBoundaryHookActions` (`WindowedWidescreenPatch.h:120`) | `WindowedWidescreenPatch.cpp:2787,2825,2975` | None | Render-thread hook-local callback ordering | None | **remove** | Same; preserve call order in direct hook code. |
| `TaskDispatchHookActions` (`WindowedWidescreenPatch.h:133`) | `WindowedWidescreenPatch.cpp:1839,2999` | None | Render-thread hook-local | None | **remove** | Same. |
| `RenderSpaceHookActions` (`WindowedWidescreenPatch.h:148`) | `WindowedWidescreenPatch.cpp:1864,3032` | None | Render-thread hook-local | None | **remove** | Same. |
| `RenderDimensionHookActions` (`WindowedWidescreenPatch.h:174`) | `WindowedWidescreenPatch.cpp:2408,2442,3062,3086` | None | Render-thread hook-local | None | **remove** | Same. |
| `ViewportResetHookActions` (`WindowedWidescreenPatch.h:198`) | `WindowedWidescreenPatch.cpp:2530,3109` | None | Render-thread hook-local | None | **remove** | Same. |
| `MousePollHookActions` (`WindowedWidescreenPatch.h:229`) | `WindowedWidescreenPatch.cpp:2615,3177` | None | Input/render hook-local | None | **remove** | Same. |
| `WindowedWidescreenInitializationGateActions` (`WindowedWidescreenPatch.h:243`) | `WindowedWidescreenPatch.cpp:3212,3281` | None | Synchronous initialization gate | None | **remove** | Same; one callback adds no durable boundary. |

### Framerate and test-mode timing

| Seam | Production callers | Alternate production implementation | Lifetime/concurrency boundary | Existing meaningful test | Decision | Reason |
|---|---|---|---|---|---|---|
| `FrameratePlatformActions` (`src/Patches/Framerate/FramerateDiagnostics.h:17`) | Diagnostics/runtime state (`FramerateDiagnostics.cpp:69,124-308`), runtime acquisition (`FrameratePatch.cpp:2363`) | None | Main/render-thread timing and diagnostic publication | None | **remove** | One platform forwarding table; direct owned clock/reporting primitives are clearer. |
| `TimingRenderActions` (`src/Patches/TestModeTiming/TimingSettingsPatch.h:54`) | Test-mode renderer (`TimingSettingsPatch.cpp:194,591,812`) | None | Native UI calls during render | None | **remove** | One native-game adapter with no external production caller. |
| `CarrierLifecycleActions` (`TimingSettingsPatch.h:70`) | Carrier creation (`TimingSettingsPatch.cpp:308,655`) | None | Explicit allocate/construct/register/destroy/deallocate ownership sequence | None | **keep** | Genuine native-object ownership boundary; the compensating destruction paths are the abstraction’s substance. |
| `TimingCommitActions` (`TimingSettingsPatch.h:86`) | Commit policy (`TimingSettingsPatch.cpp:388,693,839`) | None | Synchronous save/apply/status sequence | None | **remove** | One production composition and no independent owner; extract pure commit decisions if testing is needed. |
| `TimingLiveActions` (`src/Patches/TestModeTiming/TimingSettingsGameAbi.h:210`) | Live apply (`TimingSettingsGameAbi.cpp:568-607`) | None | Native-memory/game-ABI writes | None | **remove** | Feature-specific executable-memory/native-call adapter with one implementation. Preserve complete preflight before mutation and fatal no-rollback failure behavior. |
| `Win32FileApi` (`src/Patches/TestModeTiming/SystemConfigTimingStore.h:74`) | `SystemConfigTimingStore` owns a copy (`SystemConfigTimingStore.h:89-104`, `.cpp:369-589`) | None | File HANDLE, temporary file, flush, replace, delete, and failure cleanup ownership | None | **keep** | Genuine OS/HANDLE and atomic-replacement ownership boundary. It should later use the shared movable HANDLE primitive, but deletion would spread cleanup policy. |

Decisions are source-backed baseline recommendations for the named cleanup slices.

Baseline adjudication against Plan 08: retain `StartupConfigurationActions`
for its staged production configuration contract; remove `CardWorkerApi`
because detached-thread lifetime belongs to `Runtime`, not the forwarding
table. Removing shallow constituent types of startup configuration must retain
that deeper boundary and its existing observable failure/ordering coverage.

## Target Dependency Baseline

`cmake --graphviz="$env:TEMP\gcloader-before-cleanup.dot" --preset msvc32-debug`
completed with exit 0. Graph captured at
`C:\Users\10614\AppData\Local\Temp\gcloader-before-cleanup.dot`.

| Target | Direct internal dependencies | Direct external/system links |
|---|---|---|
| gc_system_path | None | safetyhook::safetyhook |
| gc_nesys_network_config, gc_input_types, gc_timing | None | None |
| gc_config | gc_input_types, gc_nesys_network_config | tomlplusplus::tomlplusplus, reflectcpp |
| gc_asio | gc_asio_sdk (interface SDK include root) | ole32, advapi32 |
| gc_audio | gc_asio, gc_timing | minhook, safetyhook::safetyhook, miniaudio, dsound, dxguid, ole32, uuid, avrt, propsys, user32 |
| gc_input_win32 | gc_input_types | hid, imm32, user32 |
| gc_input | gc_input_win32 | ntdll, safetyhook::safetyhook, winmm |
| gc_nesys_process, gc_nesys_diagnostics | None | None (diagnostics directly includes MinHook/plog) |
| gc_nesys | gc_nesys_process, gc_nesys_diagnostics, gc_nesys_network_config | minhook, safetyhook::safetyhook |
| gc_logging | gc_nesys_process | None |
| gc_hooking | None | minhook |
| gc_japanese_locale_policy | None | None |
| gc_japanese_locale_compatibility | gc_hooking, gc_japanese_locale_policy, gc_nesys_process | None (direct MinHook include root) |
| gc_crash_dump | gc_hooking | dbghelp |
| gc_rfid_core | None | advapi32 |
| gc_test_mode_storage | None | None |
| gc_win32_hooks | gc_hooking, gc_nesys_diagnostics, gc_rfid_core, gc_system_path, gc_test_mode_storage | None (direct MinHook include root) |
| gc_rfid_feature | gc_hooking, gc_input_win32, gc_rfid_core, gc_system_path, gc_test_mode_storage, gc_win32_hooks | None (direct MinHook include root) |
| gc_runtime_patches | gc_audio, gc_input, gc_logging, gc_system_path, gc_timing | safetyhook::safetyhook |
| gc_test_mode_timing | gc_runtime_patches | safetyhook::safetyhook |
| gc_loader_startup | gc_config, gc_system_path, gc_test_mode_storage | None |
| iDmacDrv32 (shared) | gc_audio, gc_crash_dump, gc_input, gc_japanese_locale_compatibility, gc_loader_startup, gc_logging, gc_nesys, gc_rfid_feature, gc_runtime_patches, gc_system_path, gc_test_mode_timing | dsound, dxguid, ntdll, ole32, uuid, avrt, propsys, minhook, miniaudio, safetyhook::safetyhook, tomlplusplus::tomlplusplus, reflectcpp |
| gc_exact_wasapi_clock_compatibility_tests, gc_exact_history_isolation_tests (executables) | gc_audio | None |
| gc_ime_suppression_tests (executable) | gc_input_win32 | None |
| gc_config_contract_tests (executable) | gc_config, gc_input_win32 | None |
| gc_config_startup_tests (executable) | gc_loader_startup | None |
| imgui | None | Fetched ImGui sources |
| gc_config_gui_host | imgui | d3d11, dxgi, user32 |
| gc_config_gui_model | gc_asio, gc_config, gc_input_win32 | None |
| gc_config_gui_asio_mode_host | gc_asio | ole32, user32 |
| ConfigGUI (executable) | gc_asio, gc_config, imgui, gc_config_gui_asio_mode_host, gc_config_gui_host, gc_config_gui_model | ntdll, tomlplusplus::tomlplusplus, reflectcpp |
| gc_card_reader_test_client_transport | None | None |
| CardReaderTestClient (executable) | gc_card_reader_test_client_transport | gdi32, user32 |
| gc-package-corresponding-source (custom) | None | CMake/Git/PowerShell commands |

Unless marked otherwise these are static targets. Direct edge evidence:
`src/CMakeLists.txt:17-84`, each named feature's `CMakeLists.txt`,
`tests/CMakeLists.txt:1-65`, `tools/ConfigGUI/CMakeLists.txt:1-69`,
`tools/CardReaderTestClient/CMakeLists.txt:1-18`. External transitive targets
include SafetyHook -> Zydis -> Zycore; source packaging tracks Zydis explicitly.

| Dependency | Every directly linked project target | Include-only direct naming |
|---|---|---|
| MinHook | gc_hooking, gc_audio, gc_nesys, iDmacDrv32 | gc_rfid_feature, gc_japanese_locale_compatibility, gc_win32_hooks, gc_nesys_diagnostics |
| SafetyHook | gc_system_path, gc_audio, gc_input, gc_nesys, gc_runtime_patches, gc_test_mode_timing, iDmacDrv32 | None |
| reflect-cpp | gc_config, ConfigGUI, iDmacDrv32 | Same three explicitly name its include root |

These intentionally duplicated links and header paths are the before state,
not the final dependency direction.

## Open Evidence Gaps

- Game and NESYS fixed RVAs, expected byte windows, pointer slots, and carrier-vtable targets above are versioned runtime contracts. Export targets are resolved by module/export and remain outside that barrier.
- Test Mode Timing’s `sound_carrier_vtable` is object construction: source validates the native 13-slot vtable, copies it, changes six entries in the copy, and assigns the copy to a loader-owned carrier object. It is not a native global-vtable interception.
- Windowed Widescreen does mutate exactly two native global vtable slots (`network_status_movie_clip_accept`, `network_status_shape_draw_visit`) using checked pointer replacement. No production `VmtHook` or `VmHook` implementation is present.
- `NesysServiceLauncher.cpp` also uses `VirtualAllocEx`/`WriteProcessMemory` to place a DLL path string in the child process before remote loading. That is injection data, not an executable-image patch/hook site, so it is not listed as a runtime site.
- The source inventory establishes what must be modeled. IDA/native validation must still confirm protected spans, continuation targets, calling conventions, register/stack assumptions, and global-vtable slot identities before any ABI is called verified.

- Native byte/ABI/ownership contracts must be revalidated through bounded IDA
  batches before each later feature migration.
- `iDmacDrvProgramDownload` is exported by source but absent from the `.def`.
  Its built ordinal is frozen above; none of the explicit source ordinals differ.
- Runtime acceptance remains unperformed and requires a separately authorized
  deployed artifact and actual target-process observations.

### After shared dispatch — 2026-09-05

Plan 05 comparison against the frozen tables above and the pre-migration
Kernel32 implementation. The neutral dispatcher owns 18 shared original
slots; RFID owns the nine exclusive COM slots. Loader registers
RFID -> SystemPath -> TestModeStorage pre-handlers, followed by NESYS
post-observers, and freezes every chain before installing physical hooks.
Disabled routing owners register nothing. The resulting export enable
conditions remain the same (15 unconditional, up to 12 conditional).

Each NESYS post registration includes a paired before-original bookkeeping
callback and stack-owned observation state. This preserves tracked-handle
lookup and start timing before the native call, after routing, without giving
observers an original trampoline. Completion branches never activate this
state. System-path matches explicitly bypass storage and pipe-open observation.
The illustrative post-callback signature in Plan 05 is extended with this
per-call state; no global mutable timing slot or fake API table was introduced.

| Export | Comparison | Preserved contract and new owner |
|---|---|---|
| CreateFileA | preserved | RFID exact COM2 completion first, worker/open error and handle unchanged; system match selects W and bypasses storage/observation; otherwise storage A then original A; pipe identity remains the caller's original path. All seven arguments retained. |
| CreateFileW | preserved | Same ordering with wide matching/routing; original W; null path retained. |
| WriteFile | preserved | RFID initializes count before validation, rejects missing count/overlapped/nonzero null buffer, propagates port/overflow errors, writes transferred count. Native branch forwards all five arguments once; tracked-pipe timing and observation remain. |
| ReadFile | preserved | RFID count initialization, validation, port read/remainder ownership and errors unchanged; native branch forwards all five arguments once without observation. |
| FlushFileBuffers | preserved | Always native, including emulated handles; tracked-pipe timing and result/error observation retained. |
| CloseHandle | preserved | Emulated close resets the same runtime and returns TRUE; native close is attempted once, then tracked-handle removal occurs regardless of its result. |
| FindFirstFileA | preserved | Storage-only registration; null/path/find-data pointers and original A forwarding retained. |
| FindFirstFileW | preserved | System route before storage, route failure sentinel/error retained, original W at most once. |
| CreateDirectoryA | preserved | Storage-only routing; security pointer unchanged; original A once. |
| CreateDirectoryW | preserved | System route before storage; FALSE/route error on failure; original W with unchanged security. |
| DeleteFileA | preserved | System match selects W, otherwise storage then A; null path and route error retained. |
| DeleteFileW | preserved | System before storage; original W; route failure retained. |
| GetFileAttributesA | preserved | System match selects W, otherwise storage then A; INVALID_FILE_ATTRIBUTES and route error retained. |
| GetFileAttributesW | preserved | System before storage; original W and same failure sentinel/error. |
| GetDiskFreeSpaceExA | preserved | Storage-enabled directory is explicitly null; three output pointers are forwarded unchanged to original A. |
| GetDiskFreeSpaceExW | preserved | Same explicit null directory and untouched outputs, original W. |
| MoveFileA | preserved | Routes source then destination; neither match retains A and both original pointers. Either match selects W, converts only unmatched non-null operands, preserves null operands, propagates the first route/conversion error. |
| MoveFileW | preserved | Routes source then destination; replaces only matched operands; null/unmatched pointers retained; first route error propagated. |
| GetCommModemStatus | preserved | RFID rejects null status, assigns the same modem bits; normal handle directly forwards both arguments. |
| EscapeCommFunction | preserved | Same port function policy/errors; normal handle directly forwards. |
| ClearCommError | preserved | Independently optional error/status outputs, same zero/error and port-status assignment; normal handle directly forwards. |
| SetCommMask | preserved | Same stored mask and port error behavior; normal handle directly forwards. |
| SetupComm | preserved | Both queue sizes forwarded to the same port state or original; results/errors retained. |
| GetCommState | preserved | Same null rejection and full DCB output assignment; normal handle directly forwards. |
| SetCommState | preserved | Same null rejection, DCB validation/state, detailed failure log and error; normal handle directly forwards. |
| SetCommTimeouts | preserved | Same null rejection and complete timeout state assignment; normal handle directly forwards. |
| GetCommTimeouts | preserved | Same null rejection and full output assignment; normal handle directly forwards. |

Error handling details: each shared original's result/error is captured
immediately and restored after observers and replacement-string destruction.
RFID successful completions capture the error left by the actual emulated
operation; they do not substitute ERROR_SUCCESS or force the incoming error.
The nine exclusive COM bodies retain their direct-original/error behavior.
The approved common algorithm now explicitly restores incoming LastError
after Write/Flush/Close observer bookkeeping; the baseline lacked that
explicit restoration. Its bookkeeping consists of SRW-locked tracking lookup
and GetTickCount64, with no explicit SetLastError. This is an intentional
implementation change required by Plan 05, not a claim that undefined
successful Win32 LastError values have acquired new API semantics.
Unexpected C++ exceptions are converted to each API's existing failure
sentinel plus ERROR_UNHANDLED_EXCEPTION, including exceptions in pre-handlers
before their noexcept boundary. No native access-violation recovery was added.

Dependency/include audits find no feature headers or feature target links in
Win32Hooks, and no OriginalKernel32Api/coupled Kernel32Hooks remains. RFID
runtime ownership no longer includes system-path, storage, or their hooks.
Full Debug and RelWithDebInfo builds and all five existing CTest cases per
configuration passed using build-cleanup-msvc32-debug and
build-cleanup-msvc32-release. Existing dependency caches were left in place.
No callback-recorder tests, native fake APIs, runtime deployment, or
game/NESYS acceptance were performed.

# Static Verification and Handoff Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Produce fresh x86 build and complete CTest evidence, review only the owned diff, and hand the DLL to the operator with an explicit in-game acceptance checklist.

**Architecture:** Verification reconfigures through the repository CMake project inside `vcvars32.bat`, builds the production DLL and all tests, and runs complete CTest. Static success is reported separately from the operator's final deployed game result.

**Tech Stack:** CMake, Ninja, MSVC x86, CTest, Git, PowerShell.

## Global Constraints

- Apply every constraint in `README.md`.
- Do not deploy or launch the game without a separate operator request.
- Do not claim the audio is fixed from build or CTest results.
- Review only files owned by this plan set; preserve unrelated working-tree
  changes.

---

### Task 1: Fresh repository-driven verification

**Files:**
- Verify: `CMakeLists.txt`
- Verify: `build-msvc32-latest/CMakeCache.txt`
- Verify: all files committed by Plans 1-5

- [x] **Step 1: Inspect cache identity**

```powershell
Select-String -Path build-msvc32-latest/CMakeCache.txt -Pattern '^(CMAKE_C_COMPILER|CMAKE_CXX_COMPILER|CMAKE_MAKE_PROGRAM):'
```

Expected: x86 MSVC `cl.exe` and Ninja are populated. If absent or inconsistent,
use `cmake --fresh`; otherwise perform the normal configure below.

- [x] **Step 2: Reconfigure through CMake**

```powershell
& $env:ComSpec /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake -S . -B build-msvc32-latest -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl'
```

Expected: configure/generate succeeds and CMake discovers or fetches all
dependencies without manual paths.

- [x] **Step 3: Build production and every test**

```powershell
& $env:ComSpec /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build build-msvc32-latest'
```

Expected: Ninja exits 0 and produces `build-msvc32-latest/iDmacDrv32.dll`.

- [x] **Step 4: Run complete CTest**

```powershell
& $env:ComSpec /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && ctest --test-dir build-msvc32-latest --output-on-failure'
```

Expected: every registered test passes, including
`OutputPacingTrackerTests`, `MiniaudioMixerTests`,
`AudioCursorTimelineTests`, `SecondarySoundBufferTests`,
`WasapiEndpointTests`, `ExclusiveAudioEngineTests`, and
`WasapiAudioPatchTests`.

- [x] **Step 5: Review the owned diff and commit state**

Run separately:

```powershell
git status --short
git diff --check
git log -8 --oneline --decorate
```

Expected: no uncommitted owned implementation file remains, no whitespace error
is reported, and `FrameratePatch.cpp` is unchanged.

### Task 2: Operator handoff

- [x] **Step 6: Report static evidence without runtime overclaim**

Report the exact configure, build, and CTest results; DLL path and SHA-256; new
failure/counter behavior; and any remaining unrelated working-tree files.

- [x] **Step 7: Give the manual acceptance checklist**

The operator deploys and verifies at 10 ms:

1. startup is 44.1 kHz stereo PCM16 and 441 actual frames on the tested XONAR;
2. menus/attract audio is correct;
3. multiple stages keep BGM/SHOT synchronized;
4. tap and arrangement effects remain prompt during dense play;
5. pitch and tempo remain normal with no cracking;
6. resync seeks and transitions remain clean;
7. confirmed gaps, skipped frames, chronic pacing failures, genuine unmapped
   cursors, and endpoint failures remain zero;
8. pending-generation cursor queries may rise around expected game seeks;
9. enabled audio feels lower latency than the DirectSound baseline.

Only the operator's result closes gameplay acceptance.

## Static verification evidence

- The repository-driven configure completed under `vcvars32.bat` with the x86
  MSVC compiler and Ninja; no dependency path was supplied manually.
- The complete default build exited successfully and produced an x86
  `iDmacDrv32.dll` (`14C machine`).
- Complete CTest passed 21/21 with zero failures in 8.16 seconds.
- DLL SHA-256:
  `9763B7495F599AF00FFD3D1E0CB4AFB56B8C623831B2D433D0EB67755A2B1E78`.
- Ninja records 348 dependencies for the production
  `WasapiAudioPatch.cpp.obj`, including `ExclusiveAudioEngine.h` and
  `WasapiAudioPatchInternal.h`.
- A final configuration-contract audit removed the obsolete GUI instruction to
  use zero for the endpoint minimum. The shared tooltip contract is covered by
  `ConfigFeatureTests`, and both `ConfigFeatureTests` and `ConfigGUI` rebuilt
  successfully before the complete verification pass.
- The owned commit range has no whitespace errors, the worktree is clean, and
  `FrameratePatch.cpp` is absent from the changed-file set.
- Deployment, game launch, and operator gameplay acceptance were not performed
  during static verification.

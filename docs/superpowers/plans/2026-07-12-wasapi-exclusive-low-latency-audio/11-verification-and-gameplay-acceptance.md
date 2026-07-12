# WASAPI Audio Verification and Gameplay Acceptance Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans for automated verification. The operator, not an agent, performs and judges the gameplay steps. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Prove the integrated source passes deterministic checks, then obtain authoritative operator acceptance for correctness and perceived hit-sound latency in `game471.exe`.

**Architecture:** Automated verification stays in the repository/build tree and uses fake endpoints. Deployment then updates only runtime artifacts in `H:\gc`; disabled DirectSound establishes the baseline before enabled exclusive audio is exercised through menus and multiple stages.

**Tech Stack:** MSVC x86/Ninja, CMake/CTest, `dumpbin`, PowerShell hashes, `game471.exe`, `loader-log.txt`.

## Global Constraints

- Automated success is not acceptance.
- Never commit runtime `H:\gc` files.
- Do not overwrite unrelated operator settings in `H:\gc\config.toml`; add/change only `enable_wasapi_exclusive_audio`.
- Keep a recoverable copy of the currently deployed DLL/config outside the repository before deployment.
- First prove original DirectSound behavior with the flag false.
- Enabled mode must report exact 44,100 Hz stereo PCM16, exclusive event mode, driver-aligned minimum frames, and successful MMCSS.
- Any endpoint HRESULT failure, sustained silence fallback, sustained late wake, missing BGM/SHOT, desynchronization, bad seek/fade/transition, or worse perceived hit response fails acceptance.
- Do not claim physical input-to-speaker latency; no loopback/microphone measurement is part of this plan.

---

## Prerequisites

- Plans 01-10 are committed and individually green.
- The operator's default console endpoint is configured for exclusive access.

## File Structure

- Do not change production source in this plan.
- Read `CMakeLists.txt` only to confirm the expected product and test targets remain registered.
- Build and test from `build-msvc32-latest/` inside the repository.
- Temporarily update only `H:\gc\iDmacDrv32.dll` and the one audio flag in `H:\gc\config.toml`; inspect `H:\gc\loader-log.txt`.
- Record automated and operator results by updating this plan's checkboxes and adding a dated execution note beneath the completion gate. Never add runtime binaries, configuration, logs, or backups to Git.

### Task 1: Complete Automated Verification

- [ ] **Step 1: Confirm repository scope and cleanliness**

Run from `H:\gc\artifacts\GCLoader`:

```powershell
git status --short --branch
git log -12 --oneline
```

Expected: only intentional plan-checkbox state, if any, is modified; `.superpowers/` and unrelated untracked plans are not staged.

- [ ] **Step 2: Reconfigure the x86 build from the developer prompt**

```powershell
& $env:ComSpec /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake -S . -B build-msvc32-latest -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl'
```

Expected: configuration succeeds and reports the pinned miniaudio source without enabling device I/O.

- [ ] **Step 3: Build product and every audio-focused target**

```powershell
& $env:ComSpec /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build build-msvc32-latest --target iDmacDrv32 ConfigGUI ConfigFeatureTests AudioFormatTests AudioSnapshotTests AudioCursorTimelineTests MiniaudioMixerTests SecondarySoundBufferTests DirectSoundDeviceTests WasapiEndpointTests ExclusiveAudioEngineTests WasapiAudioPatchTests'
```

Expected: all targets build without warnings promoted to failures or unresolved Windows audio symbols.

- [ ] **Step 4: Run the complete CTest suite**

```powershell
ctest --test-dir build-msvc32-latest -N
ctest --test-dir build-msvc32-latest --output-on-failure
```

Expected: all tests pass, and the listing contains all nine audio-focused targets. There are at least 18 tests; unrelated features may legitimately add more before execution.

- [ ] **Step 5: Confirm x86 product and expected imports**

```powershell
& $env:ComSpec /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && dumpbin /headers build-msvc32-latest\iDmacDrv32.dll | findstr /i "machine x86"'
& $env:ComSpec /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && dumpbin /imports build-msvc32-latest\iDmacDrv32.dll | findstr /i "AVRT.dll OLE32.dll DSOUND.dll"'
```

Expected: machine is x86 and the production DLL resolves the intended Windows audio libraries.

### Task 2: Controlled Runtime Deployment

- [ ] **Step 6: Record and back up deployed state outside the repository**

```powershell
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$backup = Join-Path $env:TEMP "GCLoader-wasapi-$stamp"
New-Item -ItemType Directory -Path $backup | Out-Null
Copy-Item -LiteralPath 'H:\gc\iDmacDrv32.dll' -Destination $backup
Copy-Item -LiteralPath 'H:\gc\config.toml' -Destination $backup
Get-FileHash -Algorithm SHA256 'H:\gc\iDmacDrv32.dll', 'build-msvc32-latest\iDmacDrv32.dll'
Write-Host "Runtime backup: $backup"
```

Expected: backup contains the pre-test DLL/config and both hashes are recorded in the console.

- [ ] **Step 7: Add the required runtime key without changing other settings**

Edit `H:\gc\config.toml` so `[experimental]` contains exactly:

```toml
enable_wasapi_exclusive_audio = false
```

Re-read the entire `[experimental]` table and confirm all pre-existing operator values are unchanged.

- [ ] **Step 8: Deploy the verified DLL**

Ensure `game471.exe`, `ConfigGUI.exe`, and any process loading `iDmacDrv32.dll` are stopped, then run:

```powershell
Copy-Item -LiteralPath 'build-msvc32-latest\iDmacDrv32.dll' -Destination 'H:\gc\iDmacDrv32.dll' -Force
Get-FileHash -Algorithm SHA256 'H:\gc\iDmacDrv32.dll', 'build-msvc32-latest\iDmacDrv32.dll'
```

Expected: deployed and built DLL hashes match.

### Task 3: Operator Gameplay Acceptance

- [ ] **Step 9: Establish the original DirectSound baseline**

With `enable_wasapi_exclusive_audio = false`, launch `game471.exe` and confirm:

- startup succeeds;
- attract/menu audio plays;
- one stage has BGM, SHOT, tap, and arrangement audio;
- `loader-log.txt` reports requested/active backend as original DirectSound and reports that no audio hook was installed.

If this baseline fails, stop. Restore the backup or diagnose a non-WASAPI regression before enabling the feature.

- [ ] **Step 10: Enable exclusive audio and validate startup diagnostics**

Stop the game, change only:

```toml
enable_wasapi_exclusive_audio = true
```

Launch again. Confirm the log reports:

- requested and active backend `wasapi_exclusive`;
- endpoint friendly name and ID;
- exclusive PCM16, 2 channels, 44,100 Hz;
- default and minimum device periods;
- requested duration;
- actual driver-aligned endpoint frames and milliseconds;
- event-driven exclusive initialization success;
- MMCSS task `Pro Audio` and critical priority success;
- miniaudio mixer 44,100 Hz / 2 channels;
- no fallback message.

Any startup error dialog or missing field fails this step. Follow the dialog instruction and set the flag false before retrying the game.

- [ ] **Step 11: Exercise attract mode and menus**

Remain in attract/menu flows long enough to hear navigation, voices, and representative exceptional-format sounds. Confirm there is no silence, truncation, unexpected pitch, mono imbalance, looping artifact, or transition click.

- [ ] **Step 12: Play multiple stages**

Across at least two stages, confirm:

- both BGM and SHOT streams start;
- BGM/SHOT remain synchronized;
- gameplay resynchronization/seek works;
- fades change promptly and correctly;
- stage transitions stop/start cleanly;
- both tap channels and the arrangement effect work repeatedly;
- dense input does not drop or noticeably delay hit sounds.

- [ ] **Step 13: Inspect runtime counters after gameplay**

Exit normally and inspect the final non-real-time summary in `H:\gc\loader-log.txt`.

Required:

- gameplay-native buffer count is nonzero;
- gameplay BGM/SHOT/tap/arrangement created zero sample-rate-converted voices;
- endpoint HRESULT failures are zero;
- cursor-timeline failures are zero or explained by a bounded startup-only condition and do not grow during play;
- silence fallbacks are zero;
- late wakes are zero or isolated; a counter that grows continuously during a stage fails acceptance;
- maximum simultaneous voice count is plausible and nonzero.

- [ ] **Step 14: Record the operator's latency judgment**

The operator answers both questions:

1. Is all exercised game audio behavior correct?
2. Does tap/arrangement response feel materially less delayed than the disabled DirectSound baseline?

Both answers must be yes. If either is no, mark this plan unaccepted and preserve the failing log plus runtime backup path for debugging.

### Task 4: Closeout

- [ ] **Step 15: Leave a safe runtime state**

If accepted, leave the flag at the operator's preferred value. If not accepted, set it to `false` and confirm the original DirectSound baseline launches again. Do not delete the temporary backup until the operator confirms it is no longer needed.

- [ ] **Step 16: Report evidence without overclaiming**

The completion report must separate:

- automated build/CTest success;
- disabled DirectSound baseline result;
- enabled startup/log result;
- menu/stage correctness result;
- runtime counter result;
- operator latency judgment.

Do not write “latency measured” or provide a millisecond input-to-speaker number.

- [ ] **Step 17: Commit only the repository-side verification record**

After the execution note records the exact commands/results and the operator's two answers, inspect and commit only this plan file:

```powershell
git diff --check
git diff -- docs/superpowers/plans/2026-07-12-wasapi-exclusive-low-latency-audio/11-verification-and-gameplay-acceptance.md
git add -- docs/superpowers/plans/2026-07-12-wasapi-exclusive-low-latency-audio/11-verification-and-gameplay-acceptance.md
git diff --cached --check
git commit -m "Record WASAPI exclusive audio acceptance"
```

If acceptance fails, use `Record WASAPI exclusive audio verification failure` as the commit subject and leave the failed acceptance boxes unchecked. Never stage files from `H:\gc`.

## Completion Gate

This plan—and therefore the feature—is complete only after Step 14 receives two affirmative operator answers. Until then, source implementation may be build-complete but gameplay acceptance remains open.

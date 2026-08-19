> **ARCHIVED FAILED ATTEMPT — DO NOT EXECUTE.** This correction did not repair
> the complete input-to-judgement pipeline.

# High-FPS Late-Gate Preview Correction Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Prevent a high-FPS physical edge intended for the current note from being rejected by a pre-input late gate and immediately consuming the following note.

**Architecture:** Add a non-consuming, type-aware late-gate preview to the existing immutable judgement transaction. Keep the preview separate from the accepted edge so only an actual native handler acceptance can consume or physically grade input; preserve the existing selected-edge path for SCRATCH and BEAT, whose native input queries already occur before their late gates.

**Tech Stack:** C++23, CMake/Ninja, MSVC x86, CTest, SafetyHook, IDA-CLI daemon evidence.

## Global Constraints

- Execute inline in `H:\gc\artifacts\GCLoader\.worktrees\asio-audio-backend`; do not dispatch agents or create another worktree.
- Preserve the locked Arcade, Switch, paired-note, original-forgiveness, duration, free-tap, and target-FPS-60 no-op contracts.
- Review and record every note type ID 0 through 15 plus free tap independently.
- Use only the already-running daemon for `H:\gc\game471.exe.i64`; do not start, restart, save, or shut it down.
- Use `C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat` and `GC_ASIO_SDK_DIR=H:\gc\artifacts\ASIOSDK` for builds.
- Verify the full Debug and Release preset graphs; targeted tests alone are not completion evidence.
- Deploy the verified Release `iDmacDrv32.dll` directly to `H:\gc\iDmacDrv32.dll` without creating a backup.

---

### Task 1: Reproduce native late-gate order in transaction tests

**Files:**
- Modify: `tests/Input/HighFps/JudgementInputTransactionTests.cpp`
- Test: `tests/Input/HighFps/JudgementInputTransactionTests.cpp`

**Interfaces:**
- Consumes: `JudgementInputTransaction::BeginNote`, `SelectLateGateTime`, `ProbePressed`, `AcceptPressed`, `CompleteDirectionMatch`, `EndNote`, and `Finish`.
- Produces: regression coverage whose call order matches the supported executable.

- [ ] **Step 1: Add a failing NORMAL-order regression**

Create a transaction with a lane-0 input-4 edge at QPC 950,000 and recognition QPC 1,000,000. Call the late gate before any pressed query:

```cpp
auto tx = Transaction(edge_view);
tx.BeginNote(Note(GameplayNoteType::Normal));
Expect(
    tx.SelectLateGateTime(1'200) == 1'195,
    "NORMAL previews its qualifying edge before the native late gate");
Expect(
    tx.SelectGradeArgument(1'137) == 1'137,
    "a late-gate preview is not a grade association");
```

Then perform `ProbePressed`/`AcceptPressed`, end the note successfully, and assert that grade time becomes 1,132 and sequence 101 is committed exactly once.

- [ ] **Step 2: Add the complete 0-15 policy-order table**

For a lane-0 button edge, assert pre-query correction for IDs 1,3,6,7,8,9,15; no pre-query correction for IDs 0,4,5,11,12,13,14. For IDs 2 and 10, provide accepted direction 8 and assert a matching direction edge previews before the gate. Keep free tap outside `BeginNote` and prove it has no late-gate path.

- [ ] **Step 3: Add semantic rejection and isolation cases**

Assert that wrong-lane button input, Arcade direction-as-button input, wrong target direction, history-only direction state, and an expired edge do not shift the late gate. Assert that Switch same-booster direction aliases and adjacent-cardinal diagonal matches do shift it. Assert that a successfully consumed current-note edge cannot preview or hit the following note in the same transaction.

- [ ] **Step 4: Run the focused test and observe the expected RED result**

Run:

```powershell
& cmd.exe /d /s /c 'call "C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" >nul && set "GC_ASIO_SDK_DIR=H:\gc\artifacts\ASIOSDK" && cmake --build --preset msvc32-debug --target JudgementInputTransactionTests && ctest --preset msvc32-debug -R "^JudgementInputTransactionTests$" --output-on-failure'
```

Expected: the test executable builds, then fails because pre-input
`SelectLateGateTime` still returns recognition time.

### Task 2: Implement the type-aware non-consuming preview

**Files:**
- Modify: `src/Input/HighFps/JudgementInputTransaction.h`
- Modify: `src/Input/HighFps/JudgementInputTransaction.cpp`
- Test: `tests/Input/HighFps/JudgementInputTransactionTests.cpp`

**Interfaces:**
- Produces: `LateGateEdgeMode`, an explicit `NoteInputPolicy` field for all 16 IDs, and private preview selection retained separately from `selected_edge_`.
- Preserves: all public hook-facing transaction signatures.

- [ ] **Step 1: Add an explicit per-note late-gate mode**

Define:

```cpp
enum class LateGateEdgeMode : std::uint8_t {
    None,
    PreviewButton,
    PreviewDirection,
    SelectedBeforeGate,
};
```

Extend all 16 `NoteInputPolicy` rows. Map NORMAL/HOLD/MERRY/HIDDEN/HIDDEN2/CRITICAL/DUAL HOLD to `PreviewButton`, FLICK/SLIDE HOLD to `PreviewDirection`, SCRATCH/BEAT to `SelectedBeforeGate`, and NONE/lifecycle IDs to `None`.

- [ ] **Step 2: Compute a button preview at BeginNote**

Select button 4 for lane 0 or 9 for lane 1. In Arcade mode inspect only the real button candidate. In Switch mode inspect the real button first, then the existing `DirectionAliasesForButton` order, returning the first qualifying candidate. Do not populate a commit or `selected_edge_`.

- [ ] **Step 3: Compute a direction preview at BeginNote**

Choose the latest pending direction edge on the note lane's booster, merge its pressed cohort into `view_.held_now`, normalize the direction, and require `DirectionAccepted(active_note_, direction, style_)`. Do not preview history-only held state.

- [ ] **Step 4: Use preview only at the late gate**

In `SelectLateGateTime`, prefer `selected_edge_` so SCRATCH and BEAT retain native query-before-gate behavior; otherwise use `late_gate_preview_edge_`. Leave `SelectGradeArgument`, commit creation, and association diagnostics dependent only on `selected_edge_`. Reset both edges at every note boundary.

- [ ] **Step 5: Run the focused transaction test and observe GREEN**

Run the Task 1 command again. Expected: `JudgementInputTransactionTests` passes.

- [ ] **Step 6: Commit the behavioral correction**

```powershell
git add -- src/Input/HighFps/JudgementInputTransaction.h src/Input/HighFps/JudgementInputTransaction.cpp tests/Input/HighFps/JudgementInputTransactionTests.cpp
git commit -m "Correct native-order high-FPS late-gate rescue"
```

### Task 3: Add bounded runtime proof and finalize the binary audit record

**Files:**
- Modify: `src/Input/HighFps/HighFpsInputBridge.h`
- Modify: `src/Input/HighFps/HighFpsInputBridge.cpp`
- Modify: `tests/Input/HighFps/HighFpsInputBridgeTests.cpp`
- Modify: `docs/reverse-engineering/high-fps-input-judgement-hook-manifest.md`

**Interfaces:**
- Produces: `HighFpsInputDiagnosticRecord::late_gate_delta_ms` in existing bounded note/miss records.
- Records: exact native late-gate/input ordering and treatment for IDs 0-15 plus free tap.

- [ ] **Step 1: Add a failing bounded-diagnostic test**

Exercise a successful NORMAL note in native order and require its drained record and formatted text to contain `late_gate_delta_ms=-5`. Also require zero for a note with no correction.

- [ ] **Step 2: Run `HighFpsInputBridgeTests` and observe RED**

Run:

```powershell
& cmd.exe /d /s /c 'call "C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" >nul && set "GC_ASIO_SDK_DIR=H:\gc\artifacts\ASIOSDK" && cmake --build --preset msvc32-debug --target HighFpsInputBridgeTests && ctest --preset msvc32-debug -R "^HighFpsInputBridgeTests$" --output-on-failure'
```

Expected: compile or assertion failure because the field is absent.

- [ ] **Step 3: Populate the diagnostic without adding a new log source**

Reset the active-note delta in `BeginNote`, assign `selected_time - recognition_ms` in the bridge's `SelectLateGateTime`, copy it into `RecordActiveNoteDiagnostics`, and append it to `FormatHighFpsInputDiagnosticRecord`.

- [ ] **Step 4: Run the focused bridge test and observe GREEN**

Run the Task 3 test command again. Expected: `HighFpsInputBridgeTests` passes.

- [ ] **Step 5: Record the complete IDA audit**

Add a late-gate/input-order table to the hook manifest with one row for every ID 0-15 and free tap, including exact handler addresses, pre/post-gate input order, preview policy, grade seam, and lifecycle/duration boundary. Record that the daemon was connected with `AgentSession.connect` and that no IDB mutation occurred.

- [ ] **Step 6: Commit diagnostics and evidence**

```powershell
git add -- src/Input/HighFps/HighFpsInputBridge.h src/Input/HighFps/HighFpsInputBridge.cpp tests/Input/HighFps/HighFpsInputBridgeTests.cpp docs/reverse-engineering/high-fps-input-judgement-hook-manifest.md
git commit -m "Record high-FPS late-gate correction evidence"
```

### Task 4: Verify, deploy, and report the runtime boundary

**Files:**
- Verify only: full source and test graph
- Deploy: `H:\gc\iDmacDrv32.dll`

**Interfaces:**
- Consumes: committed correction and diagnostics.
- Produces: x86 Debug/Release build evidence, deployed binary identity, and an explicit pending 240 FPS cabinet acceptance boundary.

- [ ] **Step 1: Configure and build the complete Debug graph**

```powershell
& cmd.exe /d /s /c 'call "C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" >nul && set "GC_ASIO_SDK_DIR=H:\gc\artifacts\ASIOSDK" && cmake --preset msvc32-debug && cmake --build --preset msvc32-debug && ctest --preset msvc32-debug --output-on-failure'
```

Expected: all 103 registered tests pass, or the updated total if a new test target was added.

- [ ] **Step 2: Configure and build the complete Release graph**

```powershell
& cmd.exe /d /s /c 'call "C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" >nul && set "GC_ASIO_SDK_DIR=H:\gc\artifacts\ASIOSDK" && cmake --preset msvc32-release && cmake --build --preset msvc32-release && ctest --preset msvc32-release --output-on-failure'
```

Expected: the same complete test count passes.

- [ ] **Step 3: Verify source state and binary architecture**

Run `git diff --check`, require a clean worktree after commits, calculate the Release DLL SHA-256, and read its PE machine field. Expected machine: `0x014C` (x86).

- [ ] **Step 4: Deploy directly and verify byte identity**

Copy the verified Release `iDmacDrv32.dll` over `H:\gc\iDmacDrv32.dll` without a backup. Recalculate both SHA-256 values and require equality.

- [ ] **Step 5: Report the honest acceptance boundary**

Report static/native evidence, RED/GREEN focused tests, full Debug/Release totals, commits, deployed hash, and PE architecture. State that only a new 240 FPS run can prove the one-beat phase shift is gone; the expected log evidence is small nonzero `late_gate_delta_ms` on correctly associated current notes and no repeated -76 to -116 ms next-note pattern.

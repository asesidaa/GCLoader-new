# Safe Menu Counter Hooks Implementation Plan

> **Execution:** Run inline in the existing `ctune-effect-timing` worktree.
> Do not dispatch agents. Follow the red/green steps in order.

**Goal:** Replace the two structurally unsafe Ranking and HitChart mid-hooks
with single-entry SafetyHook spans, make every raw-store suppression use an
explicit original-code continuation, retain observe-only behavior, and deploy
a statically verified replacement DLL for runtime testing.

**Architecture:** One compile-time geometry table supplies each counter hook's
binding RVA and suppression continuation. Ranking relocates over its pointer
load plus store; HitChart relocates over its pointer load and leaves the store
intact. Frame-local counter pointers are read through a testable, best-effort
helper. The shared policy helper assigns an exact continuation to `EIP` only
for Correct/non-authored suppression. The active build remains Observe.

**Tech stack:** C++23, Win32/x86, SafetyHook 0.6.9, CMake/Ninja, MSVC x86
Debug and RelWithDebInfo presets, CTest, PowerShell, plog, and IDA-backed
`game471.exe` contracts.

**Design:** [Safe Menu Counter Hooks Design](../../../specs/2026-07-26-safe-menu-counter-hooks-design.md)

## Global constraints

- Modify source only in
  `H:\gc\artifacts\GCLoader\.worktrees\ctune-effect-timing`.
- Work inline without agents.
- Keep `ActiveMenuTimingMode()` set to `Observe`.
- Do not modify `game471.exe`, its IDB, game data, XFL, RVB, MTX, images, or
  configuration files.
- Do not remove or disable any current diagnostic, hook, counter, or log.
- Preserve `H:\gc\loader-log.txt`,
  `H:\gc\game471.1DD1C646285DAB2.crash.dmp`, the currently deployed DLL, and
  all archived evidence until the replacement build is fully verified.
- Do not change hook counts, capacity, ordering, native-plan exclusion,
  optional WASAPI behavior, Navigator position, or final `OuterFrame`
  position.
- Do not make a diagnostic read part of a timing decision.
- Treat passing builds as static proof only; the user owns in-game acceptance.

---

### Task 1: Preserve the crash diagnosis and mark the superseding contract

**Files:**

- Commit:
  `docs/reverse-engineering/2d-menu-timing-ranking-crash-investigation.md`
- Commit existing append-only changes:
  `docs/reverse-engineering/2d-menu-timing-runtime-validation.md`
- Modify:
  `docs/superpowers/specs/2026-07-25-complete-2d-menu-timing-design.md`
- Modify:
  `docs/superpowers/plans/2026-07-25-complete-2d-menu-timing/README.md`

- [ ] **Step 1: Verify evidence identities remain unchanged**

```powershell
Get-FileHash 'H:\gc\loader-log.txt' -Algorithm SHA256
Get-FileHash 'H:\gc\game471.1DD1C646285DAB2.crash.dmp' -Algorithm SHA256
Get-FileHash 'H:\gc\iDmacDrv32.dll' -Algorithm SHA256
```

Expected hashes:

```text
loader-log.txt  29935BEC9AB11736AEEAAEDC6396DCBC2A01C40F81C1DA13F7FBB2C85C4FE7A3
crash dump      DF24E584FC5D7C55BCDA5AE3E32F3A165CB067F53BD7D09884A3362E7E62E611
deployed DLL    4D2336BE5A6BD1F0009692BB0382BD9284D0204038C3568FE850B74B25D3028F
```

- [ ] **Step 2: Add non-destructive supersession notes**

Add a short note near the top of the 2026-07-25 design and plan-set README
stating that their Ranking/HitChart addresses and relative-skip semantics are
superseded by the 2026-07-26 safe-hook design. Do not rewrite the historical
Stage A plan.

- [ ] **Step 3: Validate and commit only the evidence/document correction**

```powershell
git diff --check
git diff -- `
  docs/reverse-engineering/2d-menu-timing-ranking-crash-investigation.md `
  docs/reverse-engineering/2d-menu-timing-runtime-validation.md `
  docs/superpowers/specs/2026-07-25-complete-2d-menu-timing-design.md `
  docs/superpowers/plans/2026-07-25-complete-2d-menu-timing/README.md
git add -- `
  docs/reverse-engineering/2d-menu-timing-ranking-crash-investigation.md `
  docs/reverse-engineering/2d-menu-timing-runtime-validation.md `
  docs/superpowers/specs/2026-07-25-complete-2d-menu-timing-design.md `
  docs/superpowers/plans/2026-07-25-complete-2d-menu-timing/README.md
git commit -m "docs: record menu hook crash diagnosis"
```

---

### Task 2: Write the hook-geometry and continuation regressions first

**Files:**

- Modify:
  `tests/Patches/Framerate/FramerateMenuTimingTests.cpp`

- [ ] **Step 1: Change the exact manifest expectations**

Replace the two unsafe expected rows with:

```cpp
ExpectedMenuHook{
    FramerateHookId::RankingEntryCounterStore,
    0x00216EB4,
    Pattern({0x8B, 0x4D, 0xE0, 0x89, 0x01}),
    "Ranking entry authored counter store",
    MenuTimingHookKind::Mid},
ExpectedMenuHook{
    FramerateHookId::HitChartEntryCounterStore,
    0x0026562F,
    Pattern({0x8B, 0x8D, 0x6C, 0xFF, 0xFF, 0xFF}),
    "HitChart entry authored counter store",
    MenuTimingHookKind::Mid},
```

- [ ] **Step 2: Add causal control-flow span assertions**

Record the IDA-proven targets and assert:

```text
old Ranking [0x216EB7, 0x216EBE) contains 0x216EB9
new Ranking [0x216EB4, 0x216EB9) does not contain 0x216EB9 internally
old HitChart [0x265635, 0x26563A) contains 0x265637
new HitChart [0x26562F, 0x265635) does not contain 0x265637 internally
```

Use an end-exclusive helper local to the test. Assert the old unsafe result as
well as the new safe result so the test preserves the causal regression.

- [ ] **Step 3: Change the store-gate test to exact continuation assignment**

Supply an arbitrary address unrelated to the canary `EIP` and require:

```cpp
action == MenuCounterStoreAction::Suppress
context.eip == suppress_resume_eip
all other registers and EFLAGS unchanged
```

Keep the existing Observe and Correct/authored unchanged-context cases.

- [ ] **Step 4: Add compile-time geometry expectations**

Tests must require one public geometry constant for each of the five raw
counter paths:

```text
Ranking           hook=0x216EB4 resume=0x216EB9
HitChart          hook=0x26562F resume=0x265637
Unlock countdown  hook=0x030DA3 resume=0x030DA9
Unlock primary    hook=0x030E54 resume=0x030E5A
Unlock secondary  hook=0x030F23 resume=0x030F29
```

The initial build may fail to compile because the new geometry API does not
yet exist. That is an acceptable RED result.

- [ ] **Step 5: Add frame-local destination resolver tests**

Specify a helper taking a `safetyhook::Context`, signed EBP displacement, and
an injected 32-bit safe-reader function. Cover:

- Ranking slot address `EBP-0x20`;
- HitChart slot address `EBP-0x94`;
- successful nonzero pointer resolution;
- read failure;
- zero pointer rejection; and
- no captured-context mutation.

- [ ] **Step 6: Run the focused test and record RED**

```powershell
cmake --build --preset msvc32-debug --target FramerateMenuTimingTests
ctest --preset msvc32-debug -R "^FramerateMenuTimingTests$" --output-on-failure
```

Expected: compilation fails for the not-yet-added API, or the executable fails
on the old RVAs/patterns and additive `EIP` behavior. Do not weaken assertions
to make the old implementation pass.

---

### Task 3: Implement safe geometry, diagnostics, and callbacks

**Files:**

- Modify: `src/Patches/Framerate/FramerateMenuTiming.h`
- Modify: `src/Patches/Framerate/FramerateMenuTiming.cpp`
- Modify: `src/Patches/Framerate/FrameratePatch.cpp`
- Modify:
  `tests/Patches/Framerate/FramerateMenuTimingTests.cpp` only for compilation
  corrections that preserve the approved assertions

- [ ] **Step 1: Add centralized compile-time geometry**

Define a small `MenuCounterHookGeometry` and five `inline constexpr` instances
in `FramerateMenuTiming.h`. Use those constants in:

- `kMenuTimingHookSites`;
- all five `InstallMidHook` template bindings; and
- all five runtime counter descriptors.

The Ranking and HitChart byte contracts must cover the complete five- and
six-byte spans. No two-byte mid-hook contract remains.

- [ ] **Step 2: Implement explicit continuation assignment**

Change:

```cpp
ApplyMenuCounterStoreGate(
    safetyhook::Context& context,
    MenuTimingMode mode,
    bool authored_tick,
    std::uintptr_t suppress_resume_eip)
```

On `Suppress`, assign:

```cpp
context.eip = static_cast<std::uint32_t>(suppress_resume_eip);
```

Do not alter any other context member. In the runtime callback, convert each
descriptor's continuation RVA to an absolute address with
`ExecutableBase() + suppress_resume_rva`.

- [ ] **Step 3: Implement testable frame-local pointer resolution**

Add a pure wrapper around an injected `ReadU32Safe`-compatible function. It
must read at `context.ebp + signed_offset`, reject a failed read and a zero
pointer, return the 32-bit destination as `uintptr_t`, and never mutate the
context.

- [ ] **Step 4: Make diagnostics optional and behavior-independent**

Let `ObserveMenuCounterStore` accept an optional destination:

1. decide/apply the gate first;
2. increment commit/would-suppress/suppress counters;
3. publish the activation line;
4. if destination resolution failed, increment
   `diagnostic_read_failures` once and return from diagnostics only; and
5. otherwise perform the existing best-effort old-value/boundary/sample work.

The gate result must never depend on pointer resolution.

- [ ] **Step 5: Relocate both callbacks**

Bind Ranking at `0x216EB4` and resolve its destination through `[EBP-0x20]`.
Bind HitChart at `0x26562F` and resolve its destination through `[EBP-0x94]`.
Keep `EAX` as the pending new counter value.

Do not manually emulate the relocated instructions in Observe mode; leave
`EIP` unchanged so SafetyHook's trampoline executes them.

- [ ] **Step 6: Run focused GREEN verification**

```powershell
cmake --build --preset msvc32-debug --target FramerateMenuTimingTests
ctest --preset msvc32-debug -R "^FramerateMenuTimingTests$" --output-on-failure
```

Expected: PASS.

- [ ] **Step 7: Inspect and commit the implementation**

```powershell
git diff --check
git diff -- `
  src/Patches/Framerate/FramerateMenuTiming.h `
  src/Patches/Framerate/FramerateMenuTiming.cpp `
  src/Patches/Framerate/FrameratePatch.cpp `
  tests/Patches/Framerate/FramerateMenuTimingTests.cpp
git add -- `
  src/Patches/Framerate/FramerateMenuTiming.h `
  src/Patches/Framerate/FramerateMenuTiming.cpp `
  src/Patches/Framerate/FrameratePatch.cpp `
  tests/Patches/Framerate/FramerateMenuTimingTests.cpp
git commit -m "fix: relocate unsafe menu counter hooks"
```

---

### Task 4: Run the full static and binary verification gate

**Files:**

- No planned source modifications.

- [ ] **Step 1: Run Debug configure, build, and all tests**

```powershell
cmake --preset msvc32-debug
cmake --build --preset msvc32-debug
ctest --preset msvc32-debug --output-on-failure
```

- [ ] **Step 2: Run Release configure, build, and all tests**

```powershell
cmake --preset msvc32-release
cmake --build --preset msvc32-release
ctest --preset msvc32-release --output-on-failure
```

- [ ] **Step 3: Recheck the active mode and structural invariants**

Confirm:

```text
active mode=observe
menu hook count=7
full transformed contract count=53
transformed plan count=52/53 without/with optional WASAPI
native plan count=1/2 without/with optional WASAPI
Ranking contract=0x216EB4 / 8B 4D E0 89 01
HitChart contract=0x26562F / 8B 8D 6C FF FF FF
all runtime operations non-null
```

- [ ] **Step 4: Verify the produced DLL**

```powershell
$candidate = (Resolve-Path `
  'build-msvc32-release\dist\iDmacDrv32.dll').Path
Get-Item $candidate | Select-Object FullName,Length,LastWriteTime
Get-FileHash $candidate -Algorithm SHA256
cmd /c `
  '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && dumpbin /headers build-msvc32-release\dist\iDmacDrv32.dll | findstr /i "machine x86"'
```

Expected: a nonempty x86 DLL and a new SHA-256 distinct from the crashing DLL.

- [ ] **Step 5: Review only the owned delta**

```powershell
git status --short
git diff HEAD~3..HEAD --check
git diff HEAD~3..HEAD --stat
git log -5 --oneline
```

Do not claim runtime success from this gate.

---

### Task 5: Archive, record, and deploy the observe-only replacement

**Files:**

- Add a new immutable directory under:
  `H:\gc\artifacts\runtime-builds\2d-menu-timing\stage-a-observe\<sha256>\`
- Append:
  `docs/reverse-engineering/2d-menu-timing-runtime-validation.md`
- Replace only after all previous gates pass:
  `H:\gc\iDmacDrv32.dll`

- [ ] **Step 1: Confirm the game is stopped**

```powershell
Get-Process game471 -ErrorAction SilentlyContinue
```

If a process is returned, stop and ask the user to close it. Do not terminate
the game automatically.

- [ ] **Step 2: Prove the current DLL already has an immutable archive**

Find a byte-identical archived copy of SHA-256
`4D2336BE5A6BD1F0009692BB0382BD9284D0204038C3568FE850B74B25D3028F`.
Do not delete or rewrite it.

- [ ] **Step 3: Archive the verified candidate additively**

Create:

```text
H:\gc\artifacts\runtime-builds\2d-menu-timing\stage-a-observe\<new-sha256>\iDmacDrv32.dll
```

Copy the verified Release DLL there only if the directory does not already
contain a different file. Verify source/archive hashes match.

- [ ] **Step 4: Append the candidate identity**

Record in the runtime validation ledger:

- source commit;
- DLL size, timestamp, SHA-256, and archive path;
- Debug and Release test totals;
- active mode `observe`;
- corrected hook RVAs and complete expected bytes;
- preserved crash-evidence identities; and
- the exact next runtime acceptance conditions.

Commit the append-only ledger update before deployment.

- [ ] **Step 5: Deploy and verify**

Copy the verified Release candidate over `H:\gc\iDmacDrv32.dll`. Do not touch
the log, dump, executable, IDB, configuration, or game-data files.

```powershell
Get-FileHash `
  'build-msvc32-release\dist\iDmacDrv32.dll' `
  -Algorithm SHA256
Get-FileHash 'H:\gc\iDmacDrv32.dll' -Algorithm SHA256
```

Expected: identical hashes.

- [ ] **Step 6: Hand off the runtime test**

Tell the user this DLL remains observe-only. Ask them to wait through both
Ranking and HitChart attract screens and retain the resulting log. Runtime
acceptance requires nonzero activation/counters and no crash; animation speed
is intentionally unchanged in this safety build.

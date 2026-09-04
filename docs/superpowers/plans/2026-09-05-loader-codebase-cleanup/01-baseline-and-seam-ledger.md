# Loader Cleanup Baseline and Seam Ledger Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Freeze the current build, ABI, patch/hook inventory, shared Win32 behavior order, dependency graph, and every production `Actions`/`Api` seam before deleting or consolidating infrastructure.

**Architecture:** Produce one checked-in Markdown baseline whose rows are keyed by feature and runtime site. It is evidence for later diffs, not a second architecture specification or generated HTML report.

**Tech Stack:** PowerShell, Git, CMake/Ninja/MSVC x86 presets, CTest, `dumpbin`, `rg`, Markdown.

**Spec:** `docs/superpowers/specs/2026-09-05-loader-codebase-cleanup-design.md`

## Global Constraints

- This plan is read-only except for `docs/architecture/loader-cleanup-baseline.md`.
- Inspect `H:\gc` only as runtime evidence. Do not copy, replace, launch, stop,
  or otherwise mutate its executables, DLLs, logs, or configuration.
- Record failures and unknowns honestly. Do not repair code in this plan.
- Use plain Markdown tables and fenced text. Do not generate HTML.
- A source inventory does not substitute for IDA verification of native ABI or
  target bytes during the later feature migrations.

---

## Task 1: Record repository and toolchain identity

**Files:**

- Create: `docs/architecture/loader-cleanup-baseline.md`

**Produces:** A baseline header containing date, branch, commit, worktree state,
generator, compiler, x86 target, and configured dependency revisions.

- [ ] **Step 1: Capture repository state without changing it**

Run:

```powershell
git status --short --branch
git rev-parse HEAD
git log -1 --format='%H %cI %s'
```

Require that any pre-existing changes are identified by path and preserved.
Write the exact output summary into the Markdown document.

- [ ] **Step 2: Capture configured tool and package identities**

Run:

```powershell
cmake --version
ninja --version
rg -n 'GC_PACKAGE_REVISION_|GC_CORRESPONDING_SOURCE_DEPENDENCIES' CMakeLists.txt
rg -n 'CMAKE_GENERATOR:|CMAKE_CXX_COMPILER:|CMAKE_SIZEOF_VOID_P:' build-msvc32-debug\CMakeCache.txt build-msvc32-release\CMakeCache.txt
```

Record SafetyHook `v0.7.0`, reflect-cpp `v0.25.0`, the current MinHook revision
as a removal baseline, and any cache discrepancy. Do not edit a cache.

- [ ] **Step 3: Add the document skeleton**

Create these top-level sections exactly:

```markdown
# Loader Cleanup Baseline

## Repository and Toolchain
## Build and Test Baseline
## iDmac Export ABI
## Versioned Runtime Sites
## Export Hook Sites
## Shared Win32 Behavior Order
## Production Seam Ledger
## Target Dependency Baseline
## Open Evidence Gaps
```

---

## Task 2: Freeze build and test status

**Files:**

- Modify: `docs/architecture/loader-cleanup-baseline.md`

- [ ] **Step 1: Configure, build, and test Debug**

From an x86 MSVC Developer PowerShell:

```powershell
$env:GC_ASIO_SDK_DIR = 'H:\gc\artifacts\ASIOSDK'
cmake --preset msvc32-debug
cmake --build --preset msvc32-debug
ctest --preset msvc32-debug -N
ctest --preset msvc32-debug -j 4
```

Record configure/build exit status, the exact test names and count from `-N`,
and pass/fail totals. Do not summarize a failure as passing because unrelated
targets built.

- [ ] **Step 2: Configure, build, and test Release**

Run:

```powershell
cmake --preset msvc32-release
cmake --build --preset msvc32-release
ctest --preset msvc32-release -N
ctest --preset msvc32-release -j 4
```

Record the same fields separately. Label both as static evidence only.

---

## Task 3: Freeze the iDmac binary contract

**Files:**

- Read: `src/Driver/iDmac/iDmacDrv32.def`
- Read: `build-msvc32-debug/dist/iDmacDrv32.dll`
- Read: `build-msvc32-release/dist/iDmacDrv32.dll`
- Modify: `docs/architecture/loader-cleanup-baseline.md`

- [ ] **Step 1: Record the source export contract**

Run:

```powershell
Get-Content -LiteralPath src\Driver\iDmac\iDmacDrv32.def
```

Copy the DLL name, every export name, and every ordinal into the ABI section.

- [ ] **Step 2: Record built PE and export evidence**

Run in the same x86 developer shell:

```powershell
dumpbin /headers build-msvc32-debug\dist\iDmacDrv32.dll
dumpbin /exports build-msvc32-debug\dist\iDmacDrv32.dll
dumpbin /headers build-msvc32-release\dist\iDmacDrv32.dll
dumpbin /exports build-msvc32-release\dist\iDmacDrv32.dll
```

Record PE machine `14C`, DLL characteristics relevant to later comparison,
and the full ordered export table. If source and binary ordinals differ, put
the mismatch in `Open Evidence Gaps` and stop this plan.

---

## Task 4: Inventory every runtime patch and hook

**Files:**

- Read: `src/Patches/**`
- Read: `src/Input/Switch/**`
- Read: `src/Input/Win32/RawInputRegistrationGuard.*`
- Read: `src/Audio/AudioPatch.*`
- Read: `src/Nesys/**`
- Read: `src/SystemPath/TtxInitGuard.*`
- Read: `src/Locale/JapaneseLocaleCompatibility.*`
- Read: `src/Diagnostics/CrashDumpHandler.*`
- Read: `src/Win32Hooks/Kernel32Hooks.*`
- Modify: `docs/architecture/loader-cleanup-baseline.md`

- [ ] **Step 1: Enumerate direct image mutation and SafetyHook sites**

Run:

```powershell
rg -n 'VirtualProtect|FlushInstructionCache|WriteProcessMemory|InterlockedCompareExchangePointer|safetyhook::|create_inline|create_mid|StartDisabled|unprotect\(' src
rg -n 'Rva|RVA|expected|signature|prefix|patched|replacement' src\Patches src\Input\Switch src\Audio\AudioPatch.cpp src\Nesys\Network\SyntheticNetworkAdapter.*
```

For each site, record these columns:

```markdown
| Process | Feature | Site | Kind | Module/RVA or export | Protected span | Expected state | Callback ABI | Enabled when | Current owner | Evidence source |
```

The inventory must include GameCompatibility, AutoPlay, SongUnlock,
Framerate, Countdown, Switch input, Absolute Judgement, Test Mode Timing,
Renderer Device Loss, Windowed Widescreen including both global vtable slots,
the ASIO ordinary-close hook, the NESYS ping redirect, Raw Input registration,
Ttx initialization, locale, crash-filter, DirectSound, Kernel32, and all NESYS
export hooks.

- [ ] **Step 2: Classify preflight separately from install**

For every row set both:

```markdown
Versioned contract: yes/no
Installation mechanism: byte write / SafetyHook inline / SafetyHook mid / global vtable slot / exported-function SafetyHook
```

Raw Input, Ttx, locale, crash-filter, DirectSound, Kernel32, and other exported
API hooks are non-versioned even though export resolution is validated before
installation. Fixed game/NESYS RVAs and game-owned vtable pointers are
versioned.

- [ ] **Step 3: Record ownership/lifetime**

Mark each hook process-lifetime, feature-lifetime, or object-lifetime. The
current Test Mode Timing carrier vtable is recorded as object construction,
not interception. Record that there is currently no production `VmtHook` or
`VmHook` use.

---

## Task 5: Record current shared Win32 behavior order

**Files:**

- Read: `src/Win32Hooks/Kernel32Hooks.cpp`
- Read: `src/Win32Hooks/Kernel32Hooks.h`
- Modify: `docs/architecture/loader-cleanup-baseline.md`

- [ ] **Step 1: Write the mandatory core order table**

Record these current chains exactly:

```markdown
| Export | Pre-call order | Original call | Post-call order |
|---|---|---|---|
| CreateFileA/W | RFID COM2 completion -> system-path routing -> test-mode-storage transform | at most once | NESYS pipe-open observation |
| WriteFile | RFID COM2 completion | at most once | NESYS pipe-write observation |
| ReadFile | RFID COM2 completion | at most once | none |
| FlushFileBuffers | none | exactly once | NESYS pipe-flush observation |
| CloseHandle | RFID COM2 completion | at most once | NESYS tracked-handle removal |
```

Also document `FindFirstFileA/W`, `CreateDirectoryA/W`, `DeleteFileA/W`,
`GetFileAttributesA/W`, `GetDiskFreeSpaceExA/W`, `MoveFileA/W`, and the COM
port functions. Record which path owner runs first and whether a transform
switches from the ANSI original to the wide original.

- [ ] **Step 2: Record observable Win32 contracts**

For each export record null handling, output initialization, short-circuit
result, error on invalid arguments, whether the original is called, and the
incoming/returned `LastError` rule. These rows are the comparison oracle for
Plan 05; they are not callback-recorder tests.

---

## Task 6: Complete the production seam ledger

**Files:**

- Read: all production headers and implementations under `src/`
- Modify: `docs/architecture/loader-cleanup-baseline.md`

- [ ] **Step 1: Enumerate seams**

Run:

```powershell
rg -n 'struct [A-Za-z0-9_]*(Actions|Api)|class I[A-Z][A-Za-z0-9_]*|Production[A-Za-z0-9_]*(Actions|Api)' src --glob '*.h' --glob '*.cpp'
```

Create one row per seam with columns:

```markdown
| Seam | Production callers | Alternate production implementation | Lifetime/concurrency boundary | Existing meaningful test | Decision | Reason |
```

The ledger must explicitly decide: `GameBinaryPatchActions`,
`FramerateMemoryApi`, `TimingMemoryApi`, `WidescreenInstallActions`,
`ResolverApi`, both MinHook APIs, `AudioResolverApi`,
`AudioPatchPlatformActions`, `TtxGuardInstallActions`,
`StartupFatalActions`, `HidApi`, `CardWorkerApi`, `ForegroundApi`,
`RawInputApi`, `XInputApi`, `DirectoryActions`, `Win32FileApi`,
`ServiceChildApi`, ASIO COM/registry/isolated-process seams, WASAPI
interfaces, and the audio engine factories/observers.

- [ ] **Step 2: Apply the deletion test**

Use these decisions only:

- `keep`: at least two real production implementations, or a genuine OS,
  driver, process, thread, COM, device, or ownership boundary;
- `remove`: it only forwards to one production implementation and exists to
  inject fake native behavior or hypothetical backends;
- `defer`: evidence is incomplete, with an exact missing fact named.

Do not retain a seam merely because an old test uses it. Do not delete a real
driver/process boundary merely because it has one current implementation.

---

## Task 7: Record and commit the dependency baseline

**Files:**

- Read: root and nested `CMakeLists.txt`
- Modify: `docs/architecture/loader-cleanup-baseline.md`

- [ ] **Step 1: Capture the target graph**

Run:

```powershell
cmake --graphviz="$env:TEMP\gcloader-before-cleanup.dot" --preset msvc32-debug
rg -n 'add_library|add_executable|target_link_libraries|target_include_directories|add_subdirectory' CMakeLists.txt src tests tools --glob 'CMakeLists.txt'
```

Summarize direct internal and external dependencies in Markdown. Record every
target that directly names MinHook, SafetyHook, and reflect-cpp.

- [ ] **Step 2: Validate the document**

Run:

```powershell
rg -n '^## ' docs\architecture\loader-cleanup-baseline.md
git diff --check -- docs\architecture\loader-cleanup-baseline.md
git status --short --branch
```

Require all nine sections, no trailing whitespace, and no source changes.

- [ ] **Step 3: Commit the frozen baseline**

```powershell
git add -- docs\architecture\loader-cleanup-baseline.md
git commit -m "Document loader cleanup baseline"
```

The commit message and resulting commit ID become the `before` reference for
every later plan.

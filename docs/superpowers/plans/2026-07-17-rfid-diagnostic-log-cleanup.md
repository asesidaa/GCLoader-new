# RFID Diagnostic Log Cleanup Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Remove investigation-only successful RFID/JVS hook and COM traffic logging while retaining concise startup, failure, and protocol-anomaly diagnostics.

**Architecture:** Logging remains at the boundaries that can fail: feature composition, hook installation, Win32 emulation, and malformed protocol handling. Normal polling and packet delivery perform no logging. The existing bounded byte formatter remains available only for failed writes where request bytes are actionable.

**Tech Stack:** C++23, Win32, MinHook, plog, CMake/Ninja, and CTest.

**Design:** [RFID/JVS Modernization Design](../specs/2026-07-16-rfid-jvs-modernization-design.md#Error-and-Diagnostic-Policy)

## Global Constraints

- Preserve every game-visible RFID, JVS, COM, hook, and storage behavior.
- Keep one successful `RFID/JVS feature active` summary.
- Keep actionable error and anomaly logs; remove successful low-level traces.
- Do not add a trace configuration switch or demote obsolete tracing to dormant debug code.
- Execute inline without subagents or a worktree.
- Build and test with the existing x86 MSVC configuration before deployment.

---

### Task 1: Make Successful RFID Traffic Quiet

**Files:**
- Modify: `tests/Win32Hooks/Kernel32HookTests.cpp`
- Modify: `Rfid/ComPortState.cpp`
- Modify: `Win32Hooks/Kernel32Hooks.cpp`
- Modify: `Win32Hooks/MinHookTransaction.cpp`
- Modify: `Rfid/Feature.cpp`

**Interfaces:**
- Consumes: Existing `CaptureAppender`, emulated COM hooks, fake MinHook backend, and JVS encoder.
- Produces: A regression test proving successful hook installation and COM/JVS request handling emit no low-level informational trace.

- [ ] **Step 1: Replace the verbose-log assertion with the desired quiet-success assertion**

Rename `test_diagnostic_logging()` to `test_successful_operations_are_not_traced()`. Clear the capture appender, successfully install one fake hook, open COM2, write one valid JVS request, read its reply, and assert that the captured messages contain none of these investigation-only prefixes:

```cpp
"RFID hooks: transaction"
"RFID hooks: resolved export="
"RFID hooks: created export="
"RFID hooks: enabled export="
"RFID COM2 trace api="
"RFID JVS decoded"
"RFID JVS queued reply"
```

Keep `test_diagnostic_formatting()` because hook failure-stage names and bounded failed-write formatting remain operational diagnostics.

- [ ] **Step 2: Run the focused test and verify RED**

```powershell
cmake --build build-msvc32-latest --target Kernel32HookTests
build-msvc32-latest\Kernel32HookTests.exe
```

Expected: the executable fails because current successful hook, COM, decode, and reply paths still emit informational messages.

- [ ] **Step 3: Remove only successful investigation traces**

Delete successful `PLOG_INFO` statements from `MinHookTransaction`, `Kernel32Hooks`, and `ComPortState`. Remove the redundant pre-install requested-hook summary from `Feature`; retain its single post-install activation summary. Preserve all failure and anomaly branches, including detailed hook-install failure, rollback, Win32 failures, checksum/framing errors, invalid retransmission, acknowledgement encoding failure, and sequencing violation.

- [ ] **Step 4: Rebuild and verify GREEN**

```powershell
cmake --build build-msvc32-latest --target Kernel32HookTests
build-msvc32-latest\Kernel32HookTests.exe
```

Expected: build succeeds and the executable returns zero.

- [ ] **Step 5: Verify source policy mechanically**

```powershell
rg -n "PLOG_INFO|RFID COM2 trace api=.*success|RFID JVS (decoded|queued reply|retransmit queued|no reply)|RFID hooks: (transaction|resolved|created|enabled|MinHook initialization)" Rfid Win32Hooks
```

Expected: only intentional feature/configuration startup information remains; no successful low-level diagnostic trace matches.

### Task 2: Verify, Commit, and Deploy

**Files:**
- Verify: all files modified by Task 1
- Deploy: `H:\gc\iDmacDrv32.dll`

**Interfaces:**
- Consumes: Quiet successful logging behavior from Task 1.
- Produces: A tested x86 DLL deployed only when the game is not running.

- [ ] **Step 1: Run the full build and test suite**

```powershell
cmake --build build-msvc32-latest
ctest --test-dir build-msvc32-latest --output-on-failure
```

Expected: the x86 production DLL builds and all tests pass.

- [ ] **Step 2: Review and commit the scoped diff**

```powershell
git diff --check
git status --short
git diff -- Rfid Win32Hooks tests/Win32Hooks docs/superpowers
git add -- Rfid/ComPortState.cpp Rfid/Feature.cpp Win32Hooks/Kernel32Hooks.cpp Win32Hooks/MinHookTransaction.cpp tests/Win32Hooks/Kernel32HookTests.cpp docs/superpowers/specs/2026-07-16-rfid-jvs-modernization-design.md docs/superpowers/plans/2026-07-17-rfid-diagnostic-log-cleanup.md
git commit -m "chore: remove RFID diagnostic traffic logs"
```

Expected: only logging-policy, implementation, test, and documentation files are committed.

- [ ] **Step 3: Deploy only if `game471.exe` is stopped**

```powershell
Get-Process game471 -ErrorAction SilentlyContinue
Copy-Item -LiteralPath build-msvc32-latest\iDmacDrv32.dll -Destination H:\gc\iDmacDrv32.dll -Force
Get-FileHash build-msvc32-latest\iDmacDrv32.dll,H:\gc\iDmacDrv32.dll -Algorithm SHA256
```

Expected: no game process is present and both hashes match. If the game is running, do not terminate it or replace the DLL.

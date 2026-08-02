# External Card Reader Interface Corrections Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Keep the GUI probe local to the build tree, let unelevated local clients submit cards to an elevated game, and provide a runnable dependency-free Python client example.

**Architecture:** `ServeOneCardReaderConnection` will create each named-pipe instance with an explicit authenticated-local DACL and low mandatory-integrity label while retaining `PIPE_REJECT_REMOTE_CLIENTS`. The GUI target will use its normal target-local CMake output directory instead of `GC_DIST_DIR`. A small Python `ctypes` script will implement the same exact one-request/one-response contract and remain source-only.

**Tech Stack:** C++23, Win32 named pipes and security descriptors, SDDL/Advapi32, CMake/Ninja x86 presets, Python 3 standard library, CTest.

## Global Constraints

- Work inline on `feature/external-card-reader-interface`; do not dispatch agents or create a worktree.
- Preserve the four unrelated edits in `RendererDeviceLossPatch.cpp`, `RendererDeviceLossPatch.h`, `Rfid/Feature.cpp`, and `RendererDeviceLossPatchTests.cpp`.
- Keep `PIPE_REJECT_REMOTE_CLIENTS`; there is no network transport or application-level authentication.
- Authenticated local processes must connect without elevation, even when the game is elevated.
- The GUI and Python clients are local test/example tools and must not appear in `${GC_DIST_DIR}`.
- Keep the protocol exactly 16 ASCII digits in one message and exact `OK` or `INVALID` responses.
- Apply every production behavior change test-first and observe the expected RED result before implementation.
- Treat builds/tests as static evidence; only the user's in-game observation is runtime acceptance.

---

### Task 1: Permit lower-integrity authenticated local clients

**Files:**
- Modify: `tests/Rfid/CardReaderInterfaceTests.cpp`
- Modify: `src/Rfid/CardReaderInterface.cpp`
- Modify: `src/Rfid/CMakeLists.txt`

**Interfaces:**
- Consumes: `ServeOneCardReaderConnection(const wchar_t*, CardScanState&)` and the existing one-message pipe contract.
- Produces: the same public function, now creating its pipe with an explicit security descriptor.
- Security descriptor: protected DACL granting `GA` to `SY` and `BA`, `GRGW` to `AU`, plus `S:(ML;;NW;;;LW)`.

- [ ] **Step 1: Add a lower-integrity client regression test**

Add test-only helpers that duplicate the current process token as an impersonation token, assign `SECURITY_MANDATORY_LOW_RID`, and apply it to the client thread with `SetThreadToken`. Exercise the existing real-pipe `Exchange` path while impersonating that token:

```cpp
const auto low_integrity_token = CreateLowIntegrityToken();
failures += Expect(
    low_integrity_token.has_value(),
    "low-integrity test token can be created");

const auto low_integrity_exchange = ExchangeAsToken(
    UniquePipeName(),
    card_scan,
    "1234567890123456",
    low_integrity_token->Get());
failures += Expect(
    HasOutcome(
        low_integrity_exchange,
        CardReaderConnectionOutcome::accepted) &&
        low_integrity_exchange.response == "OK",
    "lower-integrity authenticated client can submit a card");
```

The token helper must preserve the token's authenticated-user groups and change only its mandatory integrity level. It must always call `RevertToSelf` after the exchange.

- [ ] **Step 2: Run the focused test and observe RED**

Run:

```powershell
cmake --build --preset msvc32-debug --target CardReaderInterfaceTests
ctest --preset msvc32-debug -R '^CardReaderInterfaceTests$' --output-on-failure
```

Expected: the new exchange fails with `ERROR_ACCESS_DENIED` because the current `CreateNamedPipeW` call passes `nullptr` security attributes and inherits the server process's integrity level.

- [ ] **Step 3: Create and apply the explicit pipe security descriptor**

In `CardReaderInterface.cpp`, build the descriptor with `ConvertStringSecurityDescriptorToSecurityDescriptorW` and own its `LocalFree` lifetime through the `CreateNamedPipeW` call:

```cpp
constexpr wchar_t kPipeSecuritySddl[] =
    L"D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;GRGW;;;AU)"
    L"S:(ML;;NW;;;LW)";

SECURITY_ATTRIBUTES attributes{
    .nLength = sizeof(SECURITY_ATTRIBUTES),
    .lpSecurityDescriptor = descriptor.Get(),
    .bInheritHandle = FALSE,
};
```

Pass `&attributes` as the final `CreateNamedPipeW` argument. If descriptor conversion fails, return its Win32 error and do not fall back to default security. Include `<sddl.h>` and link `gc_rfid_core` publicly to `advapi32` so the DLL and focused tests receive the dependency.

- [ ] **Step 4: Run the focused test and observe GREEN**

Run:

```powershell
cmake --preset msvc32-debug
cmake --build --preset msvc32-debug --target CardReaderInterfaceTests
ctest --preset msvc32-debug -R '^CardReaderInterfaceTests$' --output-on-failure
```

Expected: the lower-integrity request returns exact `OK`, the supplied card is pending, and the existing valid/invalid/disconnect cases remain green.

- [ ] **Step 5: Commit the security correction**

```powershell
git add -- src/Rfid/CardReaderInterface.cpp src/Rfid/CMakeLists.txt tests/Rfid/CardReaderInterfaceTests.cpp
git commit -m "Allow unelevated local card reader clients"
```

---

### Task 2: Keep the GUI probe out of the distribution directory

**Files:**
- Modify: `tools/CardReaderTestClient/CMakeLists.txt`
- Modify: `docs/card-reader-interface.md`

**Interfaces:**
- Consumes: the existing `CardReaderTestClient` target.
- Produces: `build-msvc32-debug/tools/CardReaderTestClient/CardReaderTestClient.exe` and `build-msvc32-release/tools/CardReaderTestClient/CardReaderTestClient.exe`.

- [ ] **Step 1: Observe the current artifact-placement failure**

Run:

```powershell
if (Test-Path build-msvc32-debug/dist/CardReaderTestClient.exe) { throw 'GUI probe is incorrectly staged in Debug dist' }
if (Test-Path build-msvc32-release/dist/CardReaderTestClient.exe) { throw 'GUI probe is incorrectly staged in Release dist' }
```

Expected: RED because the current target sets `RUNTIME_OUTPUT_DIRECTORY` to `${GC_DIST_DIR}` and both stale artifacts exist.

- [ ] **Step 2: Remove the distribution output override**

Leave the PDB local, but remove the runtime output property:

```cmake
set_target_properties(CardReaderTestClient PROPERTIES
        PDB_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
```

Update the runtime-probe documentation to identify the target-local build paths and state that the GUI is not a distribution artifact.

- [ ] **Step 3: Remove only owned stale GUI artifacts and rebuild**

Delete these exact generated/deployed copies created by this feature:

```text
H:\gc\artifacts\GCLoader\build-msvc32-debug\dist\CardReaderTestClient.exe
H:\gc\artifacts\GCLoader\build-msvc32-release\dist\CardReaderTestClient.exe
H:\gc\CardReaderTestClient.exe
```

Then configure and build both target-local executables:

```powershell
cmake --preset msvc32-debug
cmake --build --preset msvc32-debug --target CardReaderTestClient
cmake --preset msvc32-release
cmake --build --preset msvc32-release --target CardReaderTestClient
```

- [ ] **Step 4: Verify GREEN artifact placement**

Run assertions that both target-local executables exist and neither `dist` copy exists. Inspect both local executables with `dumpbin /headers` and require `14C machine (x86)` and `2 subsystem (Windows GUI)`.

- [ ] **Step 5: Commit the placement correction**

```powershell
git add -- tools/CardReaderTestClient/CMakeLists.txt docs/card-reader-interface.md
git commit -m "Keep card reader test client local"
```

---

### Task 3: Add a dependency-free Python client example

**Files:**
- Create: `tools/CardReaderTestClient/send_card.py`
- Create: `tools/CardReaderTestClient/tests/test_send_card.py`
- Modify: `docs/card-reader-interface.md`

**Interfaces:**
- Produces: `encode_card_number(card_number: str) -> bytes`.
- Produces: `classify_response(response: bytes) -> str` returning only `"OK"` or `"INVALID"` and raising `RuntimeError` otherwise.
- Produces: `submit_card(card_number: str) -> str`, using only `ctypes` and `ctypes.wintypes` to call the Win32 pipe APIs.
- CLI: `python tools/CardReaderTestClient/send_card.py CARD_NUMBER`; stdout is the exact accepted response, exit `0` for `OK`, `2` for `INVALID`, and `1` for validation, transport, or unexpected-response failures.

- [ ] **Step 1: Write Python contract tests first**

Test exact ASCII validation and exact response recognition:

```python
def test_encode_card_number_accepts_exact_ascii_digits(self):
    self.assertEqual(
        send_card.encode_card_number("1234567890123456"),
        b"1234567890123456",
    )

def test_encode_card_number_rejects_malformed_values(self):
    for value in ("123", "123456789012345X", "１２３４５６７８９０１２３４５６"):
        with self.subTest(value=value), self.assertRaises(ValueError):
            send_card.encode_card_number(value)

def test_classify_response_requires_an_exact_protocol_response(self):
    self.assertEqual(send_card.classify_response(b"OK"), "OK")
    self.assertEqual(send_card.classify_response(b"INVALID"), "INVALID")
    for response in (b"", b"OK\0", b"UNKNOWN"):
        with self.subTest(response=response), self.assertRaises(RuntimeError):
            send_card.classify_response(response)
```

- [ ] **Step 2: Run the Python tests and observe RED**

Run:

```powershell
python -m unittest discover -s tools/CardReaderTestClient/tests -p 'test_*.py' -v
```

Expected: RED because `send_card.py` and its functions do not exist.

- [ ] **Step 3: Implement the minimal standard-library client**

Use `ctypes.WinDLL('kernel32', use_last_error=True)` with explicit signatures for `CreateFileW`, `SetNamedPipeHandleState`, `WriteFile`, `ReadFile`, and `CloseHandle`. Open `\\.\pipe\GCLoader.CardReader` with `GENERIC_READ | GENERIC_WRITE`, switch to message read mode, write exactly the 16 encoded bytes, verify the write count, read at most eight bytes, verify the read result, and classify only exact `OK` or `INVALID`. Always close a valid handle in `finally`; do not retry.

- [ ] **Step 4: Run the Python tests and syntax check to observe GREEN**

Run:

```powershell
python -m unittest discover -s tools/CardReaderTestClient/tests -p 'test_*.py' -v
python -m py_compile tools/CardReaderTestClient/send_card.py tools/CardReaderTestClient/tests/test_send_card.py
```

Expected: all Python tests pass and both files compile without syntax errors.

- [ ] **Step 5: Document Python usage and elevation behavior**

Add a `Minimal Python Client` section that links the checked-in script and shows:

```powershell
python tools/CardReaderTestClient/send_card.py 1234567890123456
```

Document that no third-party package or administrator elevation is required for an authenticated local client, while a client in another machine remains rejected and a Windows policy can still deny access.

- [ ] **Step 6: Commit the Python example**

```powershell
git add -- tools/CardReaderTestClient/send_card.py tools/CardReaderTestClient/tests/test_send_card.py docs/card-reader-interface.md
git commit -m "Add Python card reader client example"
```

---

### Task 4: Verify and redeploy the corrected runtime DLL

**Files:**
- Verify source branch and generated artifacts.
- Deploy: `build-msvc32-release/dist/iDmacDrv32.dll` to `H:\gc\iDmacDrv32.dll`.

**Interfaces:**
- Consumes: completed Tasks 1-3.
- Produces: a hash-verified corrected runtime DLL and local-only GUI/Python probe paths.

- [ ] **Step 1: Run complete Debug verification**

```powershell
cmake --preset msvc32-debug
cmake --build --preset msvc32-debug
ctest --preset msvc32-debug -j 4
```

Expected: configure/build succeed and every Debug test passes.

- [ ] **Step 2: Run complete Release verification**

```powershell
cmake --preset msvc32-release
cmake --build --preset msvc32-release
ctest --preset msvc32-release -j 4
```

Expected: configure/build succeed and every Release test passes.

- [ ] **Step 3: Audit the final tree and artifacts**

Run `git diff --check`, inspect `git status --short`, require both local GUI executables and both Python checks, and require `CardReaderTestClient.exe` to be absent from both `dist` directories and `H:\gc`. Confirm only the four unrelated user edits remain unstaged.

- [ ] **Step 4: Back up and deploy the corrected DLL**

Require `game471.exe` to be stopped. Create a timestamped directory under `H:\gc\deploy-backups`, preserve the current runtime DLL, copy only the verified Release `iDmacDrv32.dll` to `H:\gc`, and require source/runtime SHA-256 equality. Do not copy either local client into the runtime root.

- [ ] **Step 5: Commit final documentation adjustments if any**

Stage only feature-owned documentation changes. Do not stage or edit the unrelated user files.

- [ ] **Step 6: Hand off runtime acceptance**

Ask the user to start the elevated game, wait for COM2, then run either the build-tree GUI or the Python example unelevated. Record client `OK` separately from the game's one-shot card-flow observation.

# ASIO Control Panel Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let ConfigGUI open any selected 32-bit ASIO driver's genuine control panel through a background, crash-contained self-host process, then refresh exact capability validation when the panel host completes.

**Architecture:** Add a narrow `IAsioDriver::ControlPanel()` wrapper and a driver-lifetime runner that never creates buffers or starts audio. Reuse one hardened isolated-process implementation for the existing five-second probe and the new cancellation-driven panel host; ConfigGUI owns only asynchronous operation state and never loads vendor code.

**Tech Stack:** C++23, Win32/x86, Steinberg ASIO SDK 2.3.4+, COM STA, Win32 windows/message pump, Job Objects, anonymous pipes, ImGui, CMake 3.31+, Ninja/MSVC x86, CTest.

## Global Constraints

- The approved design is `docs/superpowers/specs/2026-08-09-asio-runtime-diagnostics-and-control-panel-design.md`.
- Work only in `H:\gc\artifacts\GCLoader\.worktrees\asio-audio-backend`; do not deploy to or mutate runtime `H:\gc`.
- Require `GC_ASIO_SDK_DIR=H:\gc\artifacts\ASIOSDK`; do not fetch or vendor the SDK.
- Target Windows 10+ and Win32/x86. Do not branch on Xonar, ASIO4ALL, FlexASIO, or another vendor name.
- The child is the existing `ConfigGUI.exe`, not a new distributed executable. It shows no console, wrapper, duplicate ConfigGUI, or taskbar item; only the driver's genuine panel may be visible.
- Pass the exact UTF-8 registry driver name only through bounded stdin. Never place user-controlled text on a command line or invoke a shell.
- Preserve restricted inherited handles, suspended creation, bounded stdout, no-shell launch, and kill-on-close Job Object containment.
- Preserve the probe's five-second timeout. The panel uses an operator-length wait plus explicit cancellation and has no normal timeout.
- The panel child may resolve/create/init the driver, call `controlPanel`, pump its UI, and release it. It must not create buffers, set sample rate, start/stop audio, or render.
- Keep the driver alive until a modal or same-process modeless panel closes. A vendor-owned external process is outside generic lifetime control.
- Normal panel completion triggers fresh capability inspection. Never overwrite a nonzero frame count or bypass Save/runtime validation.
- ConfigGUI shutdown signals cancellation before joining its worker so a hung vendor panel cannot block shutdown.
- New project-owned source/test files use `SPDX-License-Identifier: CC0-1.0`; the combined ASIO-enabled distribution policy remains GPL-3.0-only.
- Use behavior tests and injected interfaces or real test-only child processes; no source-text, regex, mirrored-production, tautological, or nominal-coverage tests.
- Run focused tests first, then complete Debug and RelWithDebInfo builds and suites from the Visual Studio x86 environment.

---

## File and Responsibility Map

| File | Responsibility |
|---|---|
| `src/Audio/Asio/AsioTypes.h` | Typed panel failures. |
| `src/Audio/Asio/AsioDriver.h/.cpp` | Mockable `IASIO::controlPanel()` forwarding. |
| `src/Audio/Asio/AsioControlPanel.h/.cpp` | No-stream driver resolution, initialization, display, pump, and release. |
| `src/Audio/Asio/AsioIsolatedProcess.h/.cpp` | Shared restricted launch, pipes, Job Object, timeout/cancellation, and bounded drain. |
| `src/Audio/Asio/AsioProbeClient.h/.cpp` | Existing probe adapted to the shared runner without behavior change. |
| `src/Audio/Asio/AsioProbeProtocol.h/.cpp` | Bounded panel request/success/failure messages in the existing ASIO IPC envelope. |
| `src/Audio/Asio/AsioControlPanelClient.h/.cpp` | Parent launch and closed/cancelled/failure translation. |
| `tools/ConfigGUI/AsioModeHost.h/.cpp` | Shared mode I/O, hidden owner, and modeless-window pump. |
| `tools/ConfigGUI/AsioControlPanelMode.h/.cpp` | Early `--asio-control-panel` child entrypoint. |
| `tools/ConfigGUI/AudioOperationWorker.h/.cpp` | Joinable inspection/panel/Save work and shutdown cancellation. |
| `tools/ConfigGUI/AudioBackendEditorModel.h/.cpp` | Exact editable panel request and inspection invalidation. |
| `tools/ConfigGUI/Main.cpp` | Button/status, conflicting-field lockout, early mode, and post-panel inspection. |
| `tests/Audio/AsioControlPanelTests.cpp` | Driver lifecycle and no-stream contract. |
| `tests/Audio/AsioIsolatedProcessTests.cpp` | Real timeout/cancellation and Job Object behavior. |
| `tests/Audio/AsioControlPanelClientTests.cpp` | Fixed mode, stdin privacy, cancellation, crash, and protocol taxonomy. |
| `tests/Config/ConfigGuiAsioModeHostTests.cpp` | Hidden owner and modeless message-pump behavior. |
| `tests/Config/ConfigGuiAudioOperationWorkerTests.cpp` | Mutual exclusion and cancellation-before-join. |
| `docs/reverse-engineering/asio-runtime-validation.md` | Operator flow and evidence boundary. |

---

### Task 1: Add the mockable driver control-panel lifecycle

**Files:**
- Create: `src/Audio/Asio/AsioControlPanel.h`
- Create: `src/Audio/Asio/AsioControlPanel.cpp`
- Create: `tests/Audio/AsioControlPanelTests.cpp`
- Modify: `src/Audio/Asio/AsioTypes.h`
- Modify: `src/Audio/Asio/AsioDriver.h`
- Modify: `src/Audio/Asio/AsioDriver.cpp`
- Modify: `src/Audio/Asio/AsioCallbackRuntime.cpp`
- Modify: `tests/Audio/AsioDriverTests.cpp`
- Modify: `src/Audio/Asio/CMakeLists.txt`
- Modify: `tests/Audio/CMakeLists.txt`
- Modify: `src/Audio/AudioPatch.cpp`
- Modify: `tools/ConfigGUI/AudioBackendEditorModel.cpp`

**Interfaces:**
- Consumes: exact registry name, `IAsioRegistrySource`, `IAsioDriverFactory`, one valid unshown owner `HWND`, and an injected modeless-window wait callback.
- Produces: `IAsioDriver::ControlPanel()`, `AsioControlPanelRequest`, `AsioControlPanelActions`, and `OpenAsioControlPanel(...)`.

- [ ] **Step 1: Write failing forwarding and lifecycle tests**

Add the driver operation and runner contract under test:

```cpp
virtual ASIOError ControlPanel() noexcept = 0;

struct AsioControlPanelRequest {
    std::string driver_name;
};

struct AsioControlPanelActions {
    void* context{};
    void (*wait_for_visible_windows)(void*, HWND) noexcept{};
};

std::expected<void, AsioFailure> OpenAsioControlPanel(
    IAsioRegistrySource&,
    IAsioDriverFactory&,
    const AsioControlPanelRequest&,
    HWND owner,
    AsioControlPanelActions) noexcept;
```

Make `FakeAsio::controlPanel()` configurable and assert the wrapper forwards its result exactly. In the runner test assert the call sequence `resolve -> create -> init(owner) -> controlPanel -> wait -> release`, with zero `CreateBuffers`, `Start`, `Stop`, and `SetSampleRate` calls. Cover `ASE_OK`, `ASE_SUCCESS`, missing registration, factory failure, `ASIOFalse` init, `ASE_NotPresent`, and another ASIO error. Panel errors carry stage `control_panel`, domain `asio`, exact result, and bounded driver message.

- [ ] **Step 2: Build and verify RED**

```powershell
cmake --build --preset msvc32-debug --target AsioDriverTests AsioControlPanelTests
```

Expected: compilation fails because the new method/types do not exist.

- [ ] **Step 3: Implement the no-stream lifecycle**

Forward production directly:

```cpp
ASIOError ControlPanel() noexcept override {
    return driver_->controlPanel();
}
```

Append `control_panel` and `control_panel_crash` after `probe_crash` in
`AsioFailureStage` so every existing probe wire value remains unchanged. Update
both stage-name formatters and make the callback runtime's enum-width assertion
name the new final enumerator. Reject empty name, null owner, or null wait
callback before driver creation. Resolve, create, initialize, call
`ControlPanel`, then invoke the wait callback before releasing the driver.
Treat `ASE_OK` and `ASE_SUCCESS` as success. Do not create an `AsioSession` or
query/change stream state.

- [ ] **Step 4: Run and verify GREEN**

```powershell
cmake --build --preset msvc32-debug --target AsioDriverTests AsioControlPanelTests
ctest --test-dir build-msvc32-debug --output-on-failure -R '^(AsioDriverTests|AsioControlPanelTests)$'
```

Expected: both tests pass, including all no-stream assertions.

- [ ] **Step 5: Commit**

```powershell
git add -- src/Audio/Asio/AsioTypes.h src/Audio/Asio/AsioDriver.h src/Audio/Asio/AsioDriver.cpp src/Audio/Asio/AsioCallbackRuntime.cpp src/Audio/Asio/AsioControlPanel.h src/Audio/Asio/AsioControlPanel.cpp src/Audio/Asio/CMakeLists.txt src/Audio/AudioPatch.cpp tools/ConfigGUI/AudioBackendEditorModel.cpp tests/Audio/AsioDriverTests.cpp tests/Audio/AsioControlPanelTests.cpp tests/Audio/CMakeLists.txt
git commit -m "Expose the ASIO driver control panel"
```

---

### Task 2: Extract one hardened isolated-process runner

**Files:**
- Create: `src/Audio/Asio/AsioIsolatedProcess.h`
- Create: `src/Audio/Asio/AsioIsolatedProcess.cpp`
- Create: `tests/Audio/AsioIsolatedProcessTests.cpp`
- Create: `tests/Audio/AsioIsolatedProcessTestChild.cpp`
- Modify: `src/Audio/Asio/AsioProbeClient.h`
- Modify: `src/Audio/Asio/AsioProbeClient.cpp`
- Modify: `tests/Audio/AsioProbeClientTests.cpp`
- Modify: `src/Audio/Asio/CMakeLists.txt`
- Modify: `tests/Audio/CMakeLists.txt`

**Interfaces:**
- Consumes: absolute executable, closed internal mode, bounded stdin/stdout, required isolation flags, and exactly one wait policy.
- Produces: `IAsioIsolatedProcessActions`, production actions, and exit/timeout/cancel/launch/job/I/O/overflow outcomes.

- [ ] **Step 1: Write shared launch-contract and real cancellation tests**

Define:

```cpp
enum class AsioInternalMode : std::uint8_t { probe, control_panel };
enum class AsioIsolatedProcessStatus : std::uint8_t {
    exited, timed_out, cancelled, create_failed,
    job_failed, io_failed, output_too_large,
};

struct AsioIsolatedProcessRequest {
    std::filesystem::path executable_path;
    AsioInternalMode mode{};
    std::span<const std::byte> standard_input;
    std::chrono::milliseconds timeout{};
    HANDLE cancellation_event{};
    std::uint32_t maximum_stdout_bytes{};
    DWORD creation_flags{};
    bool inherit_handles{};
    bool restricted_handle_list{};
    bool kill_on_job_close{};
    bool use_shell{};
};
```

Probe mode requires a nonnegative timeout and null cancellation handle. Panel mode requires a valid cancellation handle and zero timeout. `AsioInternalModeArgument` maps only to `--asio-probe` and `--asio-control-panel`.

The test-only child reads stdin and waits forever for either internal argument. Launch it once with a 25 ms probe timeout and once with a manual-reset event signalled after launch. Assert `timed_out` and `cancelled`, bounded return, joined reader, and no surviving process. Reject relative paths, shell use, zero stdout bound, conflicting wait policies, and missing isolation flags before launch.

- [ ] **Step 2: Build and verify RED**

```powershell
cmake --build --preset msvc32-debug --target AsioIsolatedProcessTests
```

Expected: compilation fails because the shared runner does not exist.

- [ ] **Step 3: Extract production ownership and preserve probe semantics**

Move handle RAII, restricted inheritance, current-module path, pipes, drain thread, Job Object, suspended creation/assignment/resume, wait, termination, and cleanup from `AsioProbeClient.cpp`. Build the command line only from the quoted absolute executable and closed mode mapping. Probe waits with its timeout. Panel waits with `WaitForMultipleObjects(process, cancellation_event, INFINITE)`; cancellation closes the kill-on-close job, then joins process and reader before returning.

Adapt `AsioProbeClient` to inject the shared actions and submit `AsioInternalMode::probe`. Preserve its public API and failure taxonomy.

- [ ] **Step 4: Run process and probe regression tests**

```powershell
cmake --build --preset msvc32-debug --target AsioIsolatedProcessTests AsioProbeClientTests
ctest --test-dir build-msvc32-debug --output-on-failure -R '^(AsioIsolatedProcessTests|AsioProbeClientTests)$'
```

Expected: both pass, including real `ConfigGUI.exe --asio-probe` behavior.

- [ ] **Step 5: Commit**

```powershell
git add -- src/Audio/Asio/AsioIsolatedProcess.h src/Audio/Asio/AsioIsolatedProcess.cpp src/Audio/Asio/AsioProbeClient.h src/Audio/Asio/AsioProbeClient.cpp src/Audio/Asio/CMakeLists.txt tests/Audio/AsioIsolatedProcessTests.cpp tests/Audio/AsioIsolatedProcessTestChild.cpp tests/Audio/AsioProbeClientTests.cpp tests/Audio/CMakeLists.txt
git commit -m "Share isolated ASIO child process hosting"
```

---

### Task 3: Add the bounded panel protocol and parent client

**Files:**
- Create: `src/Audio/Asio/AsioControlPanelClient.h`
- Create: `src/Audio/Asio/AsioControlPanelClient.cpp`
- Create: `tests/Audio/AsioControlPanelClientTests.cpp`
- Modify: `src/Audio/Asio/AsioProbeProtocol.h`
- Modify: `src/Audio/Asio/AsioProbeProtocol.cpp`
- Modify: `tests/Audio/AsioProbeProtocolTests.cpp`
- Modify: `src/Audio/Asio/CMakeLists.txt`
- Modify: `tests/Audio/CMakeLists.txt`

**Interfaces:**
- Consumes: panel request, cancellation event, current executable path, and Task 2 runner.
- Produces: bounded codecs and a client returning closed, cancelled, or typed failure.

- [ ] **Step 1: Write protocol and client tests**

Extend the existing framed codec:

```cpp
using AsioControlPanelResult = std::expected<void, AsioFailure>;
std::expected<std::vector<std::byte>, AsioProbeProtocolError>
EncodeAsioControlPanelRequest(const AsioControlPanelRequest&) noexcept;
std::expected<AsioControlPanelRequest, AsioProbeProtocolError>
DecodeAsioControlPanelRequest(std::span<const std::byte>) noexcept;
std::expected<std::vector<std::byte>, AsioProbeProtocolError>
EncodeAsioControlPanelResult(const AsioControlPanelResult&) noexcept;
std::expected<AsioControlPanelResult, AsioProbeProtocolError>
DecodeAsioControlPanelResult(std::span<const std::byte>) noexcept;
```

Use distinct panel request/success kinds and the existing bounded failure payload. Cover Unicode round trip, empty/oversized/invalid UTF-8 name, wrong kind, truncation, trailing bytes, success, and full panel failure.

Define:

```cpp
enum class AsioControlPanelCompletion : std::uint8_t { closed, cancelled };
class IAsioControlPanelClient {
public:
    virtual ~IAsioControlPanelClient() = default;
    virtual std::expected<AsioControlPanelCompletion, AsioFailure> Run(
        const AsioControlPanelRequest&, HANDLE cancellation_event) noexcept = 0;
};
```

With fake process actions assert absolute current ConfigGUI path, fixed control-panel mode, Unicode name only in stdin, zero timeout, exact cancellation handle, and all isolation flags. Cover structured failure, malformed output, cancellation, crash, launch/job/I/O failure, and overflow.

- [ ] **Step 2: Build and verify RED**

```powershell
cmake --build --preset msvc32-debug --target AsioProbeProtocolTests AsioControlPanelClientTests
```

Expected: compilation fails because the panel codec/client do not exist.

- [ ] **Step 3: Implement codec and outcome mapping**

Reuse the existing private writer, reader, envelope, UTF-8 validator, and
failure codec; do not create a second parser. Extend
`IsKnownFailureStage` through `control_panel_crash` and add a regression that
the encoded numeric values for all pre-existing stages are unchanged. Panel
success has an empty payload. `cancelled` maps to non-error completion. Only a
zero-exit structured result is decoded; an abnormal exit without one maps to
`control_panel_crash`. Preserve Win32 codes for process failures and impose no
timeout.

- [ ] **Step 4: Run and verify GREEN**

```powershell
cmake --build --preset msvc32-debug --target AsioProbeProtocolTests AsioProbeClientTests AsioControlPanelClientTests
ctest --test-dir build-msvc32-debug --output-on-failure -R '^(AsioProbeProtocolTests|AsioProbeClientTests|AsioControlPanelClientTests)$'
```

Expected: all pass; existing probe wire cases remain unchanged.

- [ ] **Step 5: Commit**

```powershell
git add -- src/Audio/Asio/AsioProbeProtocol.h src/Audio/Asio/AsioProbeProtocol.cpp src/Audio/Asio/AsioControlPanelClient.h src/Audio/Asio/AsioControlPanelClient.cpp src/Audio/Asio/CMakeLists.txt tests/Audio/AsioProbeProtocolTests.cpp tests/Audio/AsioControlPanelClientTests.cpp tests/Audio/CMakeLists.txt
git commit -m "Host ASIO control panels out of process"
```

---

### Task 4: Implement the background ConfigGUI panel mode

**Files:**
- Create: `tools/ConfigGUI/AsioModeHost.h`
- Create: `tools/ConfigGUI/AsioModeHost.cpp`
- Create: `tools/ConfigGUI/AsioControlPanelMode.h`
- Create: `tools/ConfigGUI/AsioControlPanelMode.cpp`
- Create: `tests/Config/ConfigGuiAsioModeHostTests.cpp`
- Modify: `tools/ConfigGUI/AsioProbeMode.cpp`
- Modify: `tools/ConfigGUI/Main.cpp`
- Modify: `tools/ConfigGUI/CMakeLists.txt`
- Modify: `tests/Config/CMakeLists.txt`
- Modify: `tests/Audio/AsioControlPanelClientTests.cpp`

**Interfaces:**
- Consumes: fixed `--asio-control-panel`, bounded stdin, Task 1 runner, Task 3 result codec, and child-local COM STA.
- Produces: `RunAsioControlPanelMode()`, an unshown tool owner, and a message pump that keeps same-process modeless vendor windows alive.

- [ ] **Step 1: Write hidden-owner, pump, and real-boundary tests**

Expose these mode-host operations:

```cpp
std::expected<std::vector<std::byte>, AsioModeHostError>
ReadAsioModeMessage(HANDLE input) noexcept;
bool WriteAsioModeMessage(
    HANDLE output, std::span<const std::byte>) noexcept;

class AsioHiddenOwnerWindow {
public:
    bool Create() noexcept;
    HWND get() const noexcept;
};

void WaitForVisiblePanelWindows(HWND hidden_owner) noexcept;
```

Assert the owner exists, is not visible, has `WS_EX_TOOLWINDOW`, and produces no taskbar-visible window. Create a synthetic visible tool window on the same test thread, arrange destruction through a posted message or timer, call the pump, and assert it dispatches the close and returns only after destruction.

Extend `AsioControlPanelClientTests` to launch real ConfigGUI mode with a deliberately absent driver and require a structured registry failure. This exercises the process boundary without showing an installed panel.

- [ ] **Step 2: Build and verify RED**

```powershell
cmake --build --preset msvc32-debug --target ConfigGuiAsioModeHostTests AsioControlPanelClientTests
```

Expected: compilation/link failure because the mode host and early panel entry do not exist.

- [ ] **Step 3: Share mode primitives and implement the child entry**

Move the probe mode's exact envelope read, bounded allocation, exact write/flush, COM guard, and hidden window into `AsioModeHost` without changing probe exit behavior.

Implement `WaitForVisiblePanelWindows` with `EnumWindows` filtered to the current process, `IsWindowVisible`, and exclusion of the hidden owner. Drain the STA queue with `MsgWaitForMultipleObjectsEx`, `PeekMessageW`, `TranslateMessage`, and `DispatchMessageW`. Perform one queue turn after `controlPanel()` returns before deciding that no modeless window exists.

The child flow is exact:

```text
read request -> initialize STA -> create unshown owner
-> OpenAsioControlPanel(..., WaitForVisiblePanelWindows)
-> encode one result -> write -> release driver/window/COM -> exit
```

Dispatch before normal GUI initialization:

```cpp
if (argc == 2 &&
    std::string_view{argv[1]} == "--asio-control-panel") {
    return RunAsioControlPanelMode();
}
```

Do not initialize D3D, Dear ImGui, configuration, or raw input in this mode.

- [ ] **Step 4: Run and verify GREEN**

```powershell
cmake --build --preset msvc32-debug --target ConfigGUI ConfigGuiAsioModeHostTests AsioProbeClientTests AsioControlPanelClientTests
ctest --test-dir build-msvc32-debug --output-on-failure -R '^(ConfigGuiAsioModeHostTests|AsioProbeClientTests|AsioControlPanelClientTests)$'
```

Expected: all pass; no test opens a real installed driver's UI.

- [ ] **Step 5: Commit**

```powershell
git add -- tools/ConfigGUI/AsioModeHost.h tools/ConfigGUI/AsioModeHost.cpp tools/ConfigGUI/AsioControlPanelMode.h tools/ConfigGUI/AsioControlPanelMode.cpp tools/ConfigGUI/AsioProbeMode.cpp tools/ConfigGUI/Main.cpp tools/ConfigGUI/CMakeLists.txt tests/Config/ConfigGuiAsioModeHostTests.cpp tests/Config/CMakeLists.txt tests/Audio/AsioControlPanelClientTests.cpp
git commit -m "Run the vendor ASIO panel in a background host"
```

---

### Task 5: Connect cancellable panel state to ConfigGUI

**Files:**
- Create: `tools/ConfigGUI/AudioOperationWorker.h`
- Create: `tools/ConfigGUI/AudioOperationWorker.cpp`
- Create: `tests/Config/ConfigGuiAudioOperationWorkerTests.cpp`
- Modify: `tools/ConfigGUI/AudioBackendEditorModel.h`
- Modify: `tools/ConfigGUI/AudioBackendEditorModel.cpp`
- Modify: `tools/ConfigGUI/Main.cpp`
- Modify: `tools/ConfigGUI/CMakeLists.txt`
- Modify: `tests/Config/ConfigGuiAudioBackendModelTests.cpp`
- Modify: `tests/Config/CMakeLists.txt`

**Interfaces:**
- Consumes: reusable probe/panel clients, editable `InputConfig`, and the existing atomic Save transaction.
- Produces: one joinable mutually exclusive worker, `BeginControlPanel()`, genuine-panel UI/status, cancellation-before-join, and post-panel inspection.

- [ ] **Step 1: Write operation-worker and model tests**

Move the existing private worker into the focused files and extend it:

```cpp
enum class Operation : std::uint8_t {
    idle, inspection, control_panel, save,
};

std::expected<void, std::string> StartControlPanel(
    const gc::audio::AsioControlPanelRequest&) noexcept;
std::optional<std::expected<
    gc::audio::AsioControlPanelCompletion,
    gc::audio::AsioFailure>> TakeControlPanel();
void Shutdown() noexcept;
```

Inject fake probe and panel clients. One panel completes normally; another blocks on the supplied cancellation event and records that it was signalled before returning `cancelled`. Assert mutual exclusion, a result taken exactly once, return to idle, cancellation-before-join, no cancellation error display, and exception containment.

Add and test:

```cpp
std::expected<gc::audio::AsioControlPanelRequest, std::string>
BeginControlPanel();
```

It requires ASIO and a nonempty exact name, preserves arbitrary Unicode bytes, and invalidates prior inspection before returning the request.

- [ ] **Step 2: Build and verify RED**

```powershell
cmake --build --preset msvc32-debug --target ConfigGuiAudioBackendModelTests ConfigGuiAudioOperationWorkerTests
```

Expected: compilation fails because the extracted worker and model method do not exist.

- [ ] **Step 3: Implement cancellation-safe worker ownership**

The worker owns one manual-reset cancellation event, one thread, one operation tag, owned clients, and optional owned results. Reset the event before panel launch. Shutdown is ordered:

```cpp
if (operation_ == Operation::control_panel) {
    SetEvent(panel_cancellation_event_);
}
if (worker_.joinable()) {
    worker_.join();
}
operation_ = Operation::idle;
```

Close the event only after joining. Inspection retains its five-second timeout; Save retains exact probe-before-atomic-replace behavior.

- [ ] **Step 4: Add button, status, lockout, and automatic inspection**

Place **Open ASIO Control Panel** beside **Inspect ASIO driver**. Enable it only for ASIO, a nonempty name, and idle worker. While an audio operation runs, disable backend, driver name, frame count, output pair, Inspect, panel, and Save; unrelated input settings remain editable.

Show `ASIO control panel is open...` in ConfigGUI, without a ConfigGUI modal/wrapper. Poll on the GUI thread:

```cpp
if (auto panel = audio_worker.TakeControlPanel()) {
    if (!*panel) {
        panel_error = DescribeAsioFailure(panel->error());
    } else if (**panel == AsioControlPanelCompletion::closed) {
        auto request = audio_editor.BeginInspection();
        if (request) {
            auto started = audio_worker.StartInspection(*request);
            if (!started) {
                audio_editor.CompleteInspection(std::unexpected(
                    AsioFailure{
                        .stage = AsioFailureStage::process_launch,
                        .detail = started.error(),
                    }));
            }
        }
    }
}
```

Clear old panel errors on the next attempt. Failure leaves inspection stale and config untouched. Successful post-panel inspection adopts preferred frames only if the editor value is zero.

- [ ] **Step 5: Run and verify GREEN**

```powershell
cmake --build --preset msvc32-debug --target ConfigGUI ConfigGuiAudioBackendModelTests ConfigGuiAudioOperationWorkerTests
ctest --test-dir build-msvc32-debug --output-on-failure -R '^(ConfigGuiAudioBackendModelTests|ConfigGuiAudioOperationWorkerTests)$'
```

Expected: both tests pass and no new executable target is produced.

- [ ] **Step 6: Commit**

```powershell
git add -- tools/ConfigGUI/AudioOperationWorker.h tools/ConfigGUI/AudioOperationWorker.cpp tools/ConfigGUI/AudioBackendEditorModel.h tools/ConfigGUI/AudioBackendEditorModel.cpp tools/ConfigGUI/Main.cpp tools/ConfigGUI/CMakeLists.txt tests/Config/ConfigGuiAudioBackendModelTests.cpp tests/Config/ConfigGuiAudioOperationWorkerTests.cpp tests/Config/CMakeLists.txt
git commit -m "Open ASIO driver settings from ConfigGUI"
```

---

### Task 6: Verify distribution and document the operator flow

**Files:**
- Modify: `docs/reverse-engineering/asio-runtime-validation.md`

**Interfaces:**
- Consumes: completed panel path, Debug/RelWithDebInfo artifacts, and runtime-only distribution contract.
- Produces: reproducible operator guidance and evidence without a gameplay-quality claim.

- [ ] **Step 1: Run focused Debug verification**

```powershell
$env:GC_ASIO_SDK_DIR='H:\gc\artifacts\ASIOSDK'
cmake --preset msvc32-debug
cmake --build --preset msvc32-debug --target ConfigGUI AsioDriverTests AsioControlPanelTests AsioIsolatedProcessTests AsioProbeProtocolTests AsioProbeClientTests AsioControlPanelClientTests ConfigGuiAsioModeHostTests ConfigGuiAudioBackendModelTests ConfigGuiAudioOperationWorkerTests
ctest --test-dir build-msvc32-debug --output-on-failure -R '^(AsioDriverTests|AsioControlPanelTests|AsioIsolatedProcessTests|AsioProbeProtocolTests|AsioProbeClientTests|AsioControlPanelClientTests|ConfigGuiAsioModeHostTests|ConfigGuiAudioBackendModelTests|ConfigGuiAudioOperationWorkerTests)$'
```

Expected: all focused tests pass.

- [ ] **Step 2: Run complete Debug and RelWithDebInfo verification**

```powershell
cmake --build --preset msvc32-debug
ctest --preset msvc32-debug -j 4
cmake --preset msvc32-release
cmake --build --preset msvc32-release
ctest --preset msvc32-release -j 4
```

Expected: both builds and complete suites pass.

- [ ] **Step 3: Verify runtime-only dist**

```powershell
Get-ChildItem -LiteralPath build-msvc32-debug\dist -Force | Select-Object Name
Get-ChildItem -LiteralPath build-msvc32-release\dist -Force | Select-Object Name
ctest --test-dir build-msvc32-debug --output-on-failure -R '^DistributionArtifactTests$'
ctest --test-dir build-msvc32-release --output-on-failure -R '^DistributionArtifactTests$'
```

Expected: primary files remain `ConfigGUI.exe`, `config.toml`, `card.txt`, and `iDmacDrv32.dll`; there is no panel/probe helper, console launcher, `imgui.ini`, source archive, logo, or license directory.

- [ ] **Step 4: Update the validation guide**

Replace stale standalone-probe wording with ConfigGUI self-hosting. Add a **Driver control panel** section stating that only the vendor UI is shown, no audio stream is opened, normal host completion triggers inspection, nonzero frames remain exact, and panel/build success does not prove crackle-free gameplay.

Record the implementation revision and both ConfigGUI hashes:

```powershell
git rev-parse HEAD
Get-FileHash build-msvc32-debug\dist\ConfigGUI.exe -Algorithm SHA256
Get-FileHash build-msvc32-release\dist\ConfigGUI.exe -Algorithm SHA256
```

- [ ] **Step 5: Check scope and commit documentation**

```powershell
git diff --check
git status --short
git add -- docs/reverse-engineering/asio-runtime-validation.md
git commit -m "Document isolated ASIO control panel access"
```

Expected: runtime `H:\gc` remains untouched.

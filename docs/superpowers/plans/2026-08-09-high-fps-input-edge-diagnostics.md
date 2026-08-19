> **ARCHIVED DIAGNOSTIC PLAN — DO NOT EXECUTE.** Its instrumentation was
> removed at the 2026-08-20 rollback. Retain it only as evidence provenance.

# High-FPS Input Edge Diagnostics Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an opt-in diagnostic build that shows where a gameplay press disappears between the loader's FastIO publication and the game's pressed-edge query, without changing input behavior.

**Architecture:** A focused `gc::input_diagnostics` unit owns fixed-size, lock-free counters and ten live logical-input generations. Existing source paths record FastIO publication and iDmac observation; two checked executable hooks record XIO edge construction and `CInputDevice` history; the existing Switch query hooks record native and final results. The framerate outer hook publishes an atomic frame/phase stamp and drains one compact snapshot on its existing five-second logging cadence.

**Tech Stack:** C++23, Win32 `QueryPerformanceCounter`, `std::atomic`, fixed `std::array` storage, SafetyHook, CMake/Ninja, MSVC x86, plain executable unit tests through CTest.

## Global Constraints

- `GC_ENABLE_INPUT_EDGE_DIAGNOSTICS` defaults to `OFF`; no TOML or ConfigGUI field is added.
- Diagnostic paths never alter an input word, query argument, return value, frame value, or Switch alias decision.
- Hot recorders are `noexcept` and perform no allocation, locking, formatting, or logging.
- The two new executable hooks are optional diagnostics. Their failure disables executable-stage tracing but never changes the existing Switch fallback decision.
- RVA and signature contracts remain specific to the verified `game471.exe` build.
- Runtime deployment is separate from source/build verification and happens only after the game is stopped.

---

### Task 1: Add the opt-in fixed diagnostic state

**Files:**

- Modify: `cmake/ProjectOptions.cmake`
- Modify: `src/Input/CMakeLists.txt`
- Create: `src/Input/Diagnostics/InputEdgeDiagnostics.h`
- Create: `src/Input/Diagnostics/InputEdgeDiagnostics.cpp`
- Modify: `tests/Input/CMakeLists.txt`
- Create: `tests/Input/Diagnostics/InputEdgeDiagnosticsTests.cpp`

- [ ] **Step 1: Write the failing state-model tests**

Cover the explicit FastIO/XIO mapping, one complete native generation, one aliased final result, release-plus-50-ms expiry, simultaneous independent inputs, phase bins, and bounded incomplete-sample output. Drive the state with caller-supplied QPC values so tests are deterministic.

```cpp
InputEdgeDiagnostics state{1'000'000};
state.RecordPublished(0, 1'000);
state.RecordPublished(FastIoMaskForLogicalInput(4), 2'000);
state.RecordIDmac(FastIoMaskForLogicalInput(4), 2'100);
state.RecordXio(kXioMaskForLogicalInput[4], stamp(7, 3), 2'200);
state.RecordHistory(object, 101, 1u << 4, stamp(8, 0), 2'300);
state.RecordNativeQuery(4, 101, 0x001D1234, true, stamp(8, 0), 2'400);
state.RecordFinalQuery(4, 4, 101, kNoDirectionAlias, true,
                       stamp(8, 0), 2'410);

const auto snapshot = state.TakeSnapshot(52'000);
EXPECT_EQ(snapshot.interval.completed[4], 1);
EXPECT_EQ(snapshot.interval.native_success[4], 1);
EXPECT_EQ(snapshot.interval.final_success[4], 1);
EXPECT_EQ(snapshot.incomplete_count, 0);
```

- [ ] **Step 2: Run the new target and confirm the red failure**

Run:

```powershell
$env:GC_ASIO_SDK_DIR='H:\gc\artifacts\ASIOSDK'
cmake --preset msvc32-debug -DGC_ENABLE_INPUT_EDGE_DIAGNOSTICS=ON
cmake --build --preset msvc32-debug --target InputEdgeDiagnosticsTests
```

Expected: compilation fails because the diagnostics API does not exist yet.

- [ ] **Step 3: Add the build option and compile definition**

Add to `cmake/ProjectOptions.cmake`:

```cmake
option(GC_ENABLE_INPUT_EDGE_DIAGNOSTICS
    "Enable bounded high-FPS gameplay input edge diagnostics" OFF)
add_compile_definitions(
    GC_ENABLE_INPUT_EDGE_DIAGNOSTICS=$<BOOL:${GC_ENABLE_INPUT_EDGE_DIAGNOSTICS}>)
```

Add the implementation to `gc_input` and add the test target to `tests/Input/CMakeLists.txt`.

- [ ] **Step 4: Implement the smallest complete fixed state**

Expose narrow production recorders plus an injectable state object for tests. Keep the public snapshot trivially copyable and fixed-size.

```cpp
inline constexpr std::size_t kLogicalInputCount = 10;
inline constexpr std::size_t kTransitionRecordCapacity = 256;
inline constexpr std::size_t kHistoryRecordCapacity = 512;
inline constexpr std::size_t kQueryRecordCapacity = 512;
inline constexpr std::size_t kIncompletePerInterval = 4;
inline constexpr std::size_t kIncompletePerProcess = 32;

enum class InputEdgeStage : std::uint8_t {
    Published,
    IDmac,
    Xio,
    History,
    NativeQuery,
};

struct FrameratePhaseStamp {
    std::uint32_t target_fps{60};
    std::uint64_t outer_epoch{};
    std::uint32_t phase{};
    bool authored_60hz_tick{true};
};

void RecordInputPublished(std::uint32_t previous,
                          std::uint32_t next) noexcept;
void RecordIDmacInputRead(std::uint32_t word) noexcept;
void PublishInputFramerateStamp(FrameratePhaseStamp stamp) noexcept;
InputEdgeDiagnosticSnapshot TakeInputEdgeDiagnosticSnapshot() noexcept;
```

Use atomics for cross-thread live-generation fields and monotonic write indices for fixed record arrays. A release starts the 50-ms diagnostic grace timer; `TakeSnapshot` performs expiry/classification and copies at most four incomplete records. It does not influence the input pipeline.

- [ ] **Step 5: Run the focused state tests**

Run:

```powershell
cmake --build --preset msvc32-debug --target InputEdgeDiagnosticsTests
ctest --preset msvc32-debug -R '^InputEdgeDiagnosticsTests$'
```

Expected: PASS.

- [ ] **Step 6: Commit the state model**

```powershell
git add cmake/ProjectOptions.cmake src/Input/CMakeLists.txt src/Input/Diagnostics tests/Input/CMakeLists.txt tests/Input/Diagnostics
git commit -m "Add bounded input edge diagnostic state"
```

---

### Task 2: Wire publication, iDmac, and framerate observations

**Files:**

- Modify: `src/Input/Polling/InputPollingRuntime.cpp`
- Modify: `src/Driver/iDmac/iDmacDrv32.cpp`
- Modify: `src/Patches/CMakeLists.txt`
- Modify: `src/Patches/Framerate/FrameratePatch.cpp`
- Modify: `tests/Input/Diagnostics/InputEdgeDiagnosticsTests.cpp`

- [ ] **Step 1: Add failing interval/reset and source-boundary tests**

Verify unchanged words do not create generations, every iDmac read increments its read counter, only changed iDmac words create transition records, and taking a snapshot resets interval counters without resetting cumulative counters.

```cpp
state.RecordPublished(0, fastio_button, 1'000);
state.RecordPublished(fastio_button, fastio_button, 1'100);
state.RecordIDmac(fastio_button, 1'200);
state.RecordIDmac(fastio_button, 1'300);
auto first = state.TakeSnapshot(2'000);
auto second = state.TakeSnapshot(3'000);
EXPECT_EQ(first.interval.published_rises[4], 1);
EXPECT_EQ(first.interval.idmac_reads, 2);
EXPECT_EQ(second.interval.published_rises[4], 0);
EXPECT_EQ(second.cumulative.published_rises[4], 1);
```

- [ ] **Step 2: Confirm the focused test fails, then wire source callbacks**

In `InputPollingRuntime::Publish`, record only inside `previous != next`:

```cpp
if (previous != next) {
    gc::input_diagnostics::RecordInputPublished(previous, next);
    PLOG_DEBUG << "Input snapshot fastio=0x" << std::hex << next << std::dec;
}
```

In the `FIO_NODE_0_INPUT` branch, record the value after `ReadPublishedInput()` and return it unchanged:

```cpp
result = gc::input::ReadPublishedInput();
gc::input_diagnostics::RecordIDmacInputRead(result);
break;
```

- [ ] **Step 3: Publish the existing outer-frame stamp**

Link `gc_runtime_patches` to `gc_input`. Immediately after the outer-call increment and authored-clock decision, publish:

```cpp
const auto outer = counters.outer_calls.load(std::memory_order_relaxed);
gc::input_diagnostics::PublishInputFramerateStamp({
    .target_fps = g_runtime->profile.target_fps(),
    .outer_epoch = outer,
    .phase = gc::input_diagnostics::AuthoredPhaseFor(
        outer, g_runtime->profile.target_fps()),
    .authored_60hz_tick = g_runtime->authored_60hz_tick.load(
        std::memory_order_acquire),
});
```

At the end of the existing five-second branch, take one snapshot and emit one `InputEdgeDiag: summary` line plus at most four `InputEdgeDiag: incomplete` lines. Put formatting in a small helper outside all hooks.

- [ ] **Step 4: Run focused input and framerate tests**

Run:

```powershell
cmake --build --preset msvc32-debug --target InputEdgeDiagnosticsTests FramerateDiagnosticsTests FramerateRuntimeTests
ctest --preset msvc32-debug -R '^(InputEdgeDiagnosticsTests|FramerateDiagnosticsTests|FramerateRuntimeTests)$'
```

Expected: PASS, with no new source-boundary logs during tests.

- [ ] **Step 5: Commit the source integrations**

```powershell
git add src/Input/Polling/InputPollingRuntime.cpp src/Driver/iDmac/iDmacDrv32.cpp src/Patches/CMakeLists.txt src/Patches/Framerate/FrameratePatch.cpp tests/Input/Diagnostics/InputEdgeDiagnosticsTests.cpp
git commit -m "Trace input publication and game reads"
```

---

### Task 3: Add checked XIO and frame-history hooks

**Files:**

- Create: `src/Input/Diagnostics/InputEdgeHookPolicy.h`
- Create: `src/Input/Diagnostics/InputEdgeHooks.h`
- Create: `src/Input/Diagnostics/InputEdgeHooks.cpp`
- Modify: `src/Input/CMakeLists.txt`
- Modify: `tests/Input/CMakeLists.txt`
- Create: `tests/Input/Diagnostics/InputEdgeHookPolicyTests.cpp`

- [ ] **Step 1: Write failing hook-contract tests**

Lock the verified RVAs and prefixes, all three edge caller signatures, and all-or-disabled resolution for the two diagnostic-owned hooks.

```cpp
static_assert(kXioEdgeRva == 0x00055C80);
static_assert(kInputHistoryRva == 0x0022CFB0);
EXPECT_TRUE(ValidateInputEdgeHookSignatures(valid, &mismatch));
valid.history[0] ^= 0xff;
EXPECT_FALSE(ValidateInputEdgeHookSignatures(valid, &mismatch));
EXPECT_EQ(mismatch, InputEdgeHookSite::History);
EXPECT_EQ(ResolveInputEdgeHookState({true, false}),
          InputEdgeHookState::Disabled);
```

- [ ] **Step 2: Confirm the contract test fails**

Run:

```powershell
cmake --build --preset msvc32-debug --target InputEdgeHookPolicyTests
```

Expected: compilation fails because the hook policy does not exist.

- [ ] **Step 3: Implement pure signature policy**

Use the exact verified prefixes:

```cpp
inline constexpr std::array<std::uint8_t, 19> kXioEdgeSignature{
    0x8B,0x4C,0x24,0x04,0x8B,0x01,0x8B,0x54,0x24,0x14,
    0x56,0x8B,0x32,0x33,0xF0,0xF7,0xD0,0x23,0xF0};
inline constexpr std::array<std::uint8_t, 12> kInputHistorySignature{
    0x55,0x8B,0xEC,0x83,0xEC,0x78,0x89,0x4D,0xA0,0x8B,0x4D,0xA0};
```

Also preflight `0x456550`, `0x456582`, and `0x4565BF` against their verified call bytes before installing either hook.

- [ ] **Step 4: Implement the two read-only detours**

`InstallInputEdgeHooks(base)` performs all reads and validation first, then creates both inline hooks transactionally. On any failure it resets only diagnostic-owned hooks, stores `incomplete=true`, and returns a typed state for the startup log.

The XIO detour calls the original exactly once, returns its original value, and records only the verified slot-2/group-0 pressed mask from caller `0x456582`. The history detour calls the original exactly once, queries held inputs `0..9` through the object's original virtual held method for the supplied frame, and records the resulting ten-bit mask. Detours catch all exceptions and never log.

- [ ] **Step 5: Run the hook-contract tests**

Run:

```powershell
cmake --build --preset msvc32-debug --target InputEdgeHookPolicyTests
ctest --preset msvc32-debug -R '^InputEdgeHookPolicyTests$'
```

Expected: PASS.

- [ ] **Step 6: Commit the executable observations**

```powershell
git add src/Input/Diagnostics src/Input/CMakeLists.txt tests/Input/CMakeLists.txt tests/Input/Diagnostics/InputEdgeHookPolicyTests.cpp
git commit -m "Observe native input edge stages"
```

---

### Task 4: Share the gameplay query hooks without changing semantics

**Files:**

- Modify: `src/Input/Switch/SwitchInputPatch.h`
- Modify: `src/Input/Switch/SwitchInputPatch.cpp`
- Modify: `tests/Input/Switch/SwitchInputPolicyTests.cpp`
- Modify: `tests/Input/Switch/SwitchInputPatchTests.cpp`
- Modify: `src/Loader/DllMain.cpp` only if startup ownership cannot remain inside `SwitchInputPatchInit`

- [ ] **Step 1: Write failing query-observer and Arcade-plan tests**

Add a pure hook-plan resolver with these required outcomes:

- diagnostics OFF + Arcade: no gameplay query hooks;
- diagnostics ON + Arcade: pressed and held hooks only, pass-through behavior;
- Switch: pressed, held, and diagonal hooks remain transactional;
- diagnostic executable-hook failure never changes the Switch plan.

Extend query tests with a callback probe that sees native attempts and the final result while the function still returns exactly the native/alias policy result.

- [ ] **Step 2: Capture the original note-judgment caller once**

At the outer pressed/held detour, calculate the caller RVA from `_ReturnAddress()` and pass it through `OriginalQueryContext`. Do not calculate it inside `query_original`, where it would identify the helper rather than the game caller.

```cpp
OriginalQueryContext context{
    .hook = &hook,
    .self = self,
    .input_device_id = input_device_id,
    .gameplay_frame = gameplay_frame,
    .caller_rva = CallerRva(_ReturnAddress()),
    .query_kind = kind,
};
```

After each trampoline call, record the native result. After alias resolution, record the final result and accepted alias. Held calls count true results but never complete a pressed generation.

- [ ] **Step 3: Refactor hook installation minimally**

Reuse the existing RVA `0x00259640` and `0x00259570` hooks; never install a second observer there. In diagnostic Arcade builds install those two hooks as a pass-through pair while keeping `g_active_state == Arcade`. In Switch mode preserve the current three-hook all-or-nothing behavior and diagonal fallback.

Call `InstallInputEdgeHooks` from the same game-process initialization path and emit one startup line after all states are known:

```text
InputEdgeDiag: active base=0x00400000 target_fps=240 style=Switch xio=on history=on queries=on capacities=10/256/256/512/512
```

- [ ] **Step 4: Run the focused policy and diagnostics tests**

Run:

```powershell
cmake --build --preset msvc32-debug --target SwitchInputPolicyTests SwitchInputPatchTests InputEdgeDiagnosticsTests InputEdgeHookPolicyTests
ctest --preset msvc32-debug -R '^(SwitchInputPolicyTests|SwitchInputPatchTests|InputEdgeDiagnosticsTests|InputEdgeHookPolicyTests)$'
```

Expected: PASS.

- [ ] **Step 5: Commit query correlation**

```powershell
git add src/Input/Switch src/Input/Diagnostics src/Loader/DllMain.cpp tests/Input/Switch
git commit -m "Correlate gameplay input queries"
```

---

### Task 5: Verify OFF/ON builds and prepare the runtime candidate

**Files:**

- Modify only if verification exposes a defect in files already owned by Tasks 1-4.

- [ ] **Step 1: Verify the default-OFF build**

Run:

```powershell
$env:GC_ASIO_SDK_DIR='H:\gc\artifacts\ASIOSDK'
cmake --preset msvc32-release -DGC_ENABLE_INPUT_EDGE_DIAGNOSTICS=OFF
cmake --build --preset msvc32-release --target iDmacDrv32
ctest --preset msvc32-release --output-on-failure
```

Expected: build and full suite PASS; no diagnostic hooks or runtime artifacts are enabled.

- [ ] **Step 2: Verify the diagnostic-ON build**

Run:

```powershell
cmake --preset msvc32-release -DGC_ENABLE_INPUT_EDGE_DIAGNOSTICS=ON
cmake --build --preset msvc32-release --target iDmacDrv32
ctest --preset msvc32-release --output-on-failure
```

Expected: build and full suite PASS.

- [ ] **Step 3: Audit behavior and scope**

Run:

```powershell
git diff --check
git status --short
rg -n "TODO|FIXME|placeholder|sleep_for|mutex|PLOG_" src/Input/Diagnostics
```

Confirm no diagnostic recorder logs, blocks, allocates, or modifies values. Confirm the only output is startup plus the existing five-second cadence and bounded incomplete lines.

- [ ] **Step 4: Commit verification fixes, if any**

Use a focused commit only when verification required source changes. Do not create an empty commit.

- [ ] **Step 5: Prepare deployment without changing runtime configuration**

Check whether the game process is stopped. If it is running, stop and ask before copying anything. If stopped, copy only the verified diagnostic `iDmacDrv32.dll` and its PDB to the established runtime locations, preserving `config.toml`, ASIO settings, logs, and all other runtime files.

- [ ] **Step 6: Runtime acceptance handoff**

Ask for one controlled 60-FPS run and one 240-FPS run using the same chart/input mode. Static completion is limited to build/test proof; the root-cause verdict waits for the resulting `InputEdgeDiag` summaries.

> **ARCHIVED FAILED ATTEMPT — DO NOT EXECUTE.** Runtime behavior disproved this
> implementation chain; see the failed-attempt archive index.

# High-FPS Gameplay Input Transition Bridge Implementation Plan

> Runtime correction (2026-08-10): two 240 FPS candidates transported all
> post-seed rises, but bounded query-aware carry improved misses only slightly
> and made grading unstable. Aggregate query gaps did not prove per-edge loss.
> Carry is removed: query return values again use only their exact committed
> frame. Bounded counters now classify exact delivery, expiry before a pressed
> query, and Switch alias coalescing without changing native return values.

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Preserve every captured logical gameplay press at rates above 60 FPS by assigning transitions to newly processed target-rate gameplay frames, while keeping 60 FPS behavior native and leaving judgement windows, audio time, and menu input unchanged.

**Architecture:** The existing input worker publishes combined FastIO state into a fixed 1024-record SPSC transition journal. A process-lifetime bridge consumes it at a verified Tune call site (`game471.exe` RVA `0x00264DC2`), builds a fixed 64-frame target-rate history, and serves frame-visible pressed/held values through the already-owned gameplay query hooks. The Tune hook and required pressed/held hooks install as one all-or-nothing input transaction; the framerate layer supplies outer-frame and playback-generation lifecycle signals and drains bounded diagnostics on its existing five-second cadence.

**Tech Stack:** C++23, Win32 `QueryPerformanceCounter`, acquire/release `std::atomic`, fixed `std::array` storage, SafetyHook, CMake/Ninja, MSVC x86, plain executable unit tests through CTest.

## Global Constraints

- `target_fps == 60` does not activate the transition journal, target-frame mapper, Tune commit hook, or corrected query values. Existing Switch aliases and passive diagnostics remain independent and must preserve native results exactly.
- `target_fps > 60` requires the Tune commit, pressed, and held hooks. Signature mismatch or hook-creation failure rolls back that complete hook set and fails game-process initialization.
- The bridge does not change Tune's selected step, the audio/song clock, native millisecond judgement windows, note matching, grades, long-note rules, repeat rules, menu input, or FastIO register publication.
- Hot producer, commit, and query paths are `noexcept`; they allocate no memory, take no locks, wait on nothing, and emit no per-event logs.
- Fixed capacities are 1024 transition records and 64 committed gameplay frames. Startup rejects a configured maximum positive step larger than the retained frame range.
- A pressed value is a pure frame-visible predicate. Every consumer querying the same input and gameplay frame sees the same result; no query consumes an edge.
- Switch button aliases query corrected effective values. Arcade and Switch styles therefore share one corrected timeline.
- Runtime faults fall back to native query behavior after clearing untrusted bridge state. They may lose the faulting transition, but cannot leave a stuck input, duplicate an edge, or fabricate a hit.
- `GC_ENABLE_INPUT_EDGE_DIAGNOSTICS` controls detailed periodic validation. Critical startup failures and the one startup mode line remain available independently.
- Source, tests, docs, builds, and commits stay in this worktree. Deployment copies only the verified DLL to `H:\gc`, does not create a backup, and does not alter `config.toml`, `card.txt`, logs, or ASIO settings.
- Run all CMake commands from an x86 MSVC developer environment with `GC_ASIO_SDK_DIR=H:\gc\artifacts\ASIOSDK`.

---

### Task 1: Add the fixed combined-transition journal

**Files:**

- Modify: `src/Input/Polling/InputSnapshotState.h`
- Modify: `src/Input/Polling/InputSnapshotState.cpp`
- Modify: `src/Input/CMakeLists.txt`
- Create: `src/Input/HighFps/InputTransitionJournal.h`
- Create: `src/Input/HighFps/InputTransitionJournal.cpp`
- Modify: `tests/Input/CMakeLists.txt`
- Create: `tests/Input/HighFps/InputTransitionJournalTests.cpp`

- [ ] **Step 1: Write the failing journal tests**

Register `InputTransitionJournalTests` and cover independently observable behavior:

1. FastIO bits map to game logical IDs `0..9` in the same order used by the native pressed/held wrappers.
2. An unchanged combined logical mask creates no transition.
3. One state change records sequence, QPC, epoch, resulting held mask, rising mask, and falling mask.
4. A chord is one ordered record with multiple changed bits.
5. Records preserve FIFO order through ring wraparound.
6. The 1025th unconsumed record is rejected, increments the overflow generation once, and does not corrupt the first 1024 records.
7. Producer/consumer index publication uses the intended full-capacity contract under a synthetic two-thread stress run.

Use caller-supplied QPC values so the tests are deterministic:

```cpp
InputTransitionJournal journal;

failures += Expect(journal.TryPush({
    .sequence = 7,
    .qpc = 12'500,
    .epoch = 3,
    .held = MaskFor(0) | MaskFor(5),
    .pressed = MaskFor(0) | MaskFor(5),
    .released = 0,
}), "chord enqueue");

const auto record = journal.TryPeek();
failures += Expect(record && record->sequence == 7,
                   "FIFO sequence is retained");
failures += Expect(record->pressed == (MaskFor(0) | MaskFor(5)),
                   "simultaneous rises share one record");
```

- [ ] **Step 2: Run the new target and confirm the red failure**

Run:

```powershell
$env:GC_ASIO_SDK_DIR='H:\gc\artifacts\ASIOSDK'
cmake --preset msvc32-debug -DGC_ENABLE_INPUT_EDGE_DIAGNOSTICS=ON
cmake --build --preset msvc32-debug --target InputTransitionJournalTests
```

Expected: compilation fails because `InputTransitionJournal` and its FastIO mapping do not exist yet.

- [ ] **Step 3: Expose one authoritative FastIO mapping**

Move the complete 17-action mask table currently local to `InputSnapshotState.cpp` into a public `inline constexpr` array in `InputSnapshotState.h`, ordered exactly as `LogicalAction`. Keep `InputSnapshotState::Compose` using that same table. The bridge reads only the first ten gameplay entries, so gameplay and service mappings cannot drift or require a duplicated table.

```cpp
inline constexpr std::size_t kGameplayLogicalInputCount = 10;
inline constexpr std::size_t kLogicalActionCount =
    static_cast<std::size_t>(LogicalAction::Count);
inline constexpr std::array<std::uint32_t, kLogicalActionCount>
    kFastIoMaskByLogicalAction{
        FastIoBits::P1_UP,
        FastIoBits::P2_UP,
        FastIoBits::P1_DOWN,
        FastIoBits::P2_DOWN,
        FastIoBits::P1_BUTTON_1,
        FastIoBits::P1_LEFT,
        FastIoBits::P2_LEFT,
        FastIoBits::P1_RIGHT,
        FastIoBits::P2_RIGHT,
        FastIoBits::P2_BUTTON_1,
        FastIoBits::P1_SERVICE_F1,
        FastIoBits::P1_SERVICE_I,
        FastIoBits::P1_SERVICE_P,
        FastIoBits::P1_START,
        FastIoBits::P2_START,
        FastIoBits::P2_SERVICE,
        FastIoBits::TEST_MODE,
    };
```

- [ ] **Step 4: Implement the SPSC journal**

Use this production-facing shape:

```cpp
namespace gc::high_fps_input {

using GameplayInputMask = std::uint16_t;
inline constexpr std::size_t kTransitionCapacity = 1024;

struct InputTransitionRecord {
    std::uint64_t sequence{};
    std::int64_t qpc{};
    std::uint64_t epoch{};
    GameplayInputMask held{};
    GameplayInputMask pressed{};
    GameplayInputMask released{};
};

class InputTransitionJournal final {
public:
    [[nodiscard]] bool TryPush(InputTransitionRecord record) noexcept;
    [[nodiscard]] std::optional<InputTransitionRecord> TryPeek() const noexcept;
    [[nodiscard]] bool Pop() noexcept;
    void DiscardAll() noexcept;
    [[nodiscard]] std::uint32_t depth() const noexcept;
    [[nodiscard]] std::uint64_t overflow_generation() const noexcept;

private:
    std::array<InputTransitionRecord, kTransitionCapacity> records_{};
    alignas(64) std::atomic<std::uint64_t> read_index_{};
    alignas(64) std::atomic<std::uint64_t> write_index_{};
    std::atomic<std::uint64_t> overflow_generation_{};
};

[[nodiscard]] GameplayInputMask GameplayMaskFromFastIo(
    std::uint32_t word) noexcept;
[[nodiscard]] std::optional<InputTransitionRecord> BuildTransitionRecord(
    std::uint32_t previous_fastio,
    std::uint32_t next_fastio,
    std::uint64_t sequence,
    std::int64_t qpc,
    std::uint64_t epoch) noexcept;

} // namespace gc::high_fps_input
```

`BuildTransitionRecord` masks both FastIO words to the ten gameplay inputs and returns `std::nullopt` when that combined logical mask did not change. Otherwise it derives the resulting held, pressed, and released masks used by the bridge.

The producer reads `read_index_` with acquire before deciding the ring is full, writes the record, then publishes `write_index_` with release. The consumer acquires `write_index_`, reads or discards records, then releases `read_index_`. The overflow path increments only the overflow generation; it never overwrites unread data.

- [ ] **Step 5: Run the focused journal test**

Run:

```powershell
cmake --build --preset msvc32-debug --target InputTransitionJournalTests
ctest --preset msvc32-debug -R '^InputTransitionJournalTests$'
```

Expected: PASS, including wraparound and the synthetic SPSC stress case.

- [ ] **Step 6: Commit the journal**

```powershell
git add src/Input/Polling/InputSnapshotState.h src/Input/Polling/InputSnapshotState.cpp src/Input/CMakeLists.txt src/Input/HighFps/InputTransitionJournal.h src/Input/HighFps/InputTransitionJournal.cpp tests/Input/CMakeLists.txt tests/Input/HighFps/InputTransitionJournalTests.cpp
git commit -m "Add gameplay input transition journal"
```

---

### Task 2: Map transitions onto newly processed gameplay frames

**Files:**

- Modify: `src/Input/CMakeLists.txt`
- Create: `src/Input/HighFps/GameplayInputTimeline.h`
- Create: `src/Input/HighFps/GameplayInputTimeline.cpp`
- Modify: `tests/Input/CMakeLists.txt`
- Create: `tests/Input/HighFps/GameplayInputTimelineTests.cpp`

- [ ] **Step 1: Write the failing target-frame tests**

Cover the complete mapper contract:

- initial reset seeds held state at the current frame with no rising edge;
- `step == 0` leaves records pending and creates no frame;
- repeated zero steps do not duplicate or reorder records;
- the first later positive step exposes deferred input without an additional frame of delay;
- `step == 1` commits all eligible records to the one new frame;
- `step > 1` creates every intermediate frame and partitions records chronologically;
- records older than the prior positive boundary clamp to the first new frame, a record exactly at the consumer boundary maps to the last, and a concurrently published record newer than that boundary stays pending;
- simultaneous chord bits remain simultaneous;
- press, hold, release, and steady held state produce native-style adjacent-frame edges;
- down and up inside one target frame produce one held/pressed frame and release on the next processed frame;
- repeated queries of one retained frame are stable and the edge is absent from the next frame;
- frame-number addition, QPC multiplication, invalid QPC order, and a step greater than 64 fail without partially publishing frames.

Representative zero-step and pulse assertions:

```cpp
timeline.Reset(4, 100, 0, 1'000);
journal.TryPush(transition(1, 1'100, 4, MaskFor(4), MaskFor(4), 0));
journal.TryPush(transition(2, 1'200, 4, 0, 0, MaskFor(4)));

failures += Expect(timeline.Commit(100, 0, 1'250, journal).has_value(),
                   "zero step defers");
failures += Expect(!timeline.Find(101, 4), "zero step publishes no frame");

failures += Expect(timeline.Commit(100, 1, 1'300, journal).has_value(),
                   "next positive step commits");
const auto pulse = timeline.Find(101, 4);
failures += Expect(pulse && (pulse->pressed & MaskFor(4)) != 0,
                   "sub-frame tap has one pressed frame");
failures += Expect((pulse->held & MaskFor(4)) != 0,
                   "pressed implies held");
```

- [ ] **Step 2: Run the new target and confirm the red failure**

```powershell
cmake --build --preset msvc32-debug --target GameplayInputTimelineTests
```

Expected: compilation fails because the timeline API does not exist.

- [ ] **Step 3: Add the fixed 64-frame timeline**

Use fixed records and explicit checked errors:

```cpp
inline constexpr std::size_t kGameplayFrameCapacity = 64;

struct CommittedGameplayFrame {
    std::uint32_t frame{};
    std::uint64_t epoch{};
    GameplayInputMask held{};
    GameplayInputMask pressed{};
    GameplayInputMask released{};
    std::uint64_t first_sequence{};
    std::uint64_t last_sequence{};
};

enum class GameplayTimelineError : std::uint8_t {
    InvalidQpc,
    StepExceedsCapacity,
    FrameOverflow,
    ArithmeticOverflow,
    FrameDiscontinuity,
};

class GameplayInputTimeline final {
public:
    [[nodiscard]] static std::expected<GameplayInputTimeline,
        GameplayTimelineError> Create(std::uint32_t maximum_step) noexcept;
    void Reset(std::uint64_t epoch, std::uint32_t current_frame,
               GameplayInputMask held, std::int64_t qpc) noexcept;
    [[nodiscard]] std::expected<GameplayCommitResult,
        GameplayTimelineError> Commit(
            std::uint32_t current_frame,
            std::uint32_t step,
            std::int64_t now_qpc,
            InputTransitionJournal& journal) noexcept;
    [[nodiscard]] std::optional<CommittedGameplayFrame> Find(
        std::uint32_t frame, std::uint64_t epoch) const noexcept;
};
```

Do not expose mutable frame storage. Publish each complete frame only after all transitions assigned to it have been applied.

- [ ] **Step 4: Implement deterministic QPC partitioning and pulse normalization**

For a positive step, map records over `(previous_positive_qpc, now_qpc]` into frames `current + 1` through `current + step`. Use checked 64-bit arithmetic and interval semantics equivalent to:

```cpp
frame_offset = ClampToRange(
    CeilDiv((event_qpc - previous_positive_qpc) * step,
            now_qpc - previous_positive_qpc),
    1U,
    step);
```

Clamp an older record to offset `1` and a record exactly at `now_qpc` to offset `step`; do not let QPC create or remove gameplay frames. Drain only records in the active epoch whose QPC is at or before the captured consumer boundary. A record published concurrently with a newer QPC remains pending for the next positive step.

Track physical held state separately from normalized held state. At each frame:

1. apply a previously deferred pulse release;
2. apply that frame's records in sequence order;
3. if a bit rose and fell within the frame, force it held for this frame and defer its release to the next processed frame;
4. derive `pressed = held[N] & ~held[N - 1]` and `released = held[N - 1] & ~held[N]`;
5. store the source sequence range.

This preserves `Pressed => Held` and never stretches an ordinary held press. The exceptional down/up pulse lasts exactly one target frame.

- [ ] **Step 5: Run the focused mapper test**

```powershell
cmake --build --preset msvc32-debug --target GameplayInputTimelineTests
ctest --preset msvc32-debug -R '^GameplayInputTimelineTests$'
```

Expected: PASS at synthetic 120, 144, 165, 240, and 360 FPS boundary intervals.

- [ ] **Step 6: Commit the timeline**

```powershell
git add src/Input/CMakeLists.txt src/Input/HighFps/GameplayInputTimeline.h src/Input/HighFps/GameplayInputTimeline.cpp tests/Input/CMakeLists.txt tests/Input/HighFps/GameplayInputTimelineTests.cpp
git commit -m "Map input transitions to gameplay frames"
```

---

### Task 3: Add the process-lifetime bridge, epochs, and native fallback

**Files:**

- Modify: `src/Input/CMakeLists.txt`
- Create: `src/Input/HighFps/HighFpsInputBridge.h`
- Create: `src/Input/HighFps/HighFpsInputBridge.cpp`
- Modify: `tests/Input/CMakeLists.txt`
- Create: `tests/Input/HighFps/HighFpsInputBridgeTests.cpp`

- [ ] **Step 1: Write the failing bridge-policy tests**

Exercise a real `HighFpsInputBridge` object rather than a test-only mirror:

- configuring 60 FPS returns disabled mode; recording, committing, and querying never handle a value;
- configuring above 60 FPS stays pending until the complete hook set activates;
- invalid QPC frequency, zero maximum step, or maximum step above 64 is rejected before activation;
- activation seeds the current FastIO held snapshot without a rise;
- a published combined mask change enqueues one timestamped record; an unchanged word does not;
- all consumers querying one committed frame see the same pressed bit;
- the next frame no longer reports that edge;
- the first nonnegative device ID queried in an active epoch becomes the local gameplay device, while another ID falls back to native;
- invalid logical IDs and missing/out-of-range frames fall back and increment the correct bounded counter;
- focus loss, disconnect, playback generation change, frame rollback, and gameplay inactivity each start a new epoch and seed current held state without a synthetic rise;
- an overflow generation discards the untrusted queue and resynchronizes to the latest held snapshot;
- a runtime mapper fault deactivates corrected queries and returns native fallback thereafter.

The broadcast-edge test must model both free-tap and note consumers:

```cpp
const auto free_tap = bridge.QueryPressed(12, 4, 201);
const auto note = bridge.QueryPressed(12, 4, 201);
failures += Expect(free_tap.handled && free_tap.value,
                   "free tap sees committed edge");
failures += Expect(note.handled && note.value,
                   "note logic sees the same edge");
bridge.CommitGameplayStepAtQpc(201, 1, 2'000);
failures += Expect(!bridge.QueryPressed(12, 4, 202).value,
                   "edge is absent next frame");
```

- [ ] **Step 2: Run the new target and confirm the red failure**

```powershell
cmake --build --preset msvc32-debug --target HighFpsInputBridgeTests
```

Expected: compilation fails because the bridge API does not exist.

- [ ] **Step 3: Implement the testable bridge and narrow production facade**

Use a fixed-state class plus `noexcept` global forwarding functions:

```cpp
enum class HighFpsInputBridgeMode : std::uint8_t {
    Disabled60Fps,
    PendingHooks,
    Active,
    NativeFallback,
};

enum class InputEpochResetReason : std::uint8_t {
    Activation,
    FocusLoss,
    DeviceDisconnect,
    GameplayInactive,
    FrameDiscontinuity,
    PlaybackGeneration,
    Overflow,
    RuntimeFault,
    Shutdown,
};

struct HighFpsInputBridgeConfig {
    std::uint32_t target_fps{60};
    std::int64_t qpc_frequency{};
    std::uint32_t maximum_step{1};
    bool gate_on_playback_activity{};
};

struct EffectiveInputQuery {
    bool handled{};
    bool value{};
};

[[nodiscard]] std::expected<void, HighFpsInputBridgeError>
ConfigureHighFpsInputBridge(HighFpsInputBridgeConfig config) noexcept;
void ActivateHighFpsInputBridge(std::uint32_t current_fastio) noexcept;
void DeactivateHighFpsInputBridge() noexcept;
void RecordPublishedFastIoTransition(
    std::uint32_t previous, std::uint32_t next) noexcept;
void RequestInputEpochReset(InputEpochResetReason reason) noexcept;
void ObserveGameplayOuterFrame(
    std::uint64_t outer_epoch, std::int64_t qpc) noexcept;
void ObserveGameplayPlayback(
    bool active, std::optional<std::uint64_t> generation) noexcept;
void CommitGameplayInputStep(
    std::uint32_t current_frame,
    std::uint32_t step,
    std::int64_t qpc) noexcept;
void ReportCommitHookRuntimeFault() noexcept;
[[nodiscard]] EffectiveInputQuery QueryGameplayPressed(
    int input_device_id, int logical_input,
    std::uint32_t gameplay_frame) noexcept;
[[nodiscard]] EffectiveInputQuery QueryGameplayHeld(
    int input_device_id, int logical_input,
    std::uint32_t gameplay_frame) noexcept;
void RecordFinalGameplayPressedQuery(
    int requested_input,
    int source_input,
    std::uint32_t gameplay_frame,
    std::uint32_t caller_rva,
    bool result,
    bool bridge_handled) noexcept;
[[nodiscard]] HighFpsInputBridgeSnapshot
TakeHighFpsInputBridgeSnapshot() noexcept;
[[nodiscard]] std::string FormatHighFpsInputBridgeSummary(
    const HighFpsInputBridgeSnapshot& snapshot);
```

Provide explicit `RecordPublishedFastIoTransitionAtQpc` and `CommitGameplayStepAtQpc` methods only on the concrete class for deterministic tests; production wrappers acquire QPC at their real boundaries. The process-lifetime object contains the journal, timeline, latest held mask, epoch/reset generations, mode, local device binding, and fixed diagnostics. It must not allocate on first input or query.

- [ ] **Step 4: Implement cross-thread epoch and overflow recovery**

Producer-side reset requests increment an atomic requested epoch after the existing input snapshot has been cleared/published. The consumer observes a changed epoch before draining, discards old records, resets the timeline at the current gameplay frame, and seeds from the latest atomic held mask with `pressed == 0`.

The producer always updates the latest held mask even when the journal is full. When the consumer observes a new overflow generation it performs the same resynchronization with reason `Overflow`. Do not retry the rejected record or synthesize an edge from the held snapshot.

Use outer-frame liveness without adding a new hook: `ObserveGameplayOuterFrame` checks whether the previous outer frame contained a Tune commit. One complete outer frame without a commit marks gameplay inactive and flushes pending transitions. The next Tune commit starts a fresh epoch. A current gameplay frame different from the last committed frame also resets before consuming.

When `gate_on_playback_activity` is true, an explicit inactive observation prevents the later Tune hook in that same native call from reopening or committing the epoch. Only the next `Exact` or `Rounded` observation re-enables commit. A transient `Failed` observation leaves the gate unchanged. Non-shared clock plans set the flag false and rely on Tune/outer-frame liveness plus frame discontinuity.

- [ ] **Step 5: Add fixed diagnostic snapshots and formatting**

Expose one snapshot and formatter from the bridge module. Keep interval and cumulative counters for:

- captured rises/falls and journal enqueues;
- committed records/frames and effective edges;
- `step == 0`, `step == 1`, and `step > 1` calls;
- zero-step deferrals, maximum positive step, queue depth, and event age;
- pressed/held query successes, native fallbacks, and active missing-frame fallbacks;
- left/right free-tap successes recorded later by the query hook;
- reset reasons, overflow, runtime fallback, duplicate frame, and missing frame invariants.

`TakeHighFpsInputBridgeSnapshot` may run only on the gameplay/outer thread. Formatting happens after snapshot extraction, never inside producer/commit/query recorders.

- [ ] **Step 6: Run the focused bridge tests**

```powershell
cmake --build --preset msvc32-debug --target HighFpsInputBridgeTests
ctest --preset msvc32-debug -R '^HighFpsInputBridgeTests$'
```

Expected: PASS with no allocation, lock, or logging in the exercised hot methods.

- [ ] **Step 7: Commit the bridge**

```powershell
git add src/Input/CMakeLists.txt src/Input/HighFps/HighFpsInputBridge.h src/Input/HighFps/HighFpsInputBridge.cpp tests/Input/CMakeLists.txt tests/Input/HighFps/HighFpsInputBridgeTests.cpp
git commit -m "Add high-FPS gameplay input bridge"
```

---

### Task 4: Feed the bridge from the combined input worker state

**Files:**

- Modify: `src/Input/Polling/InputPollingRuntime.cpp`
- Modify: `tests/Input/HighFps/HighFpsInputBridgeTests.cpp`
- Modify: `tests/Input/Polling/InputMapperTests.cpp`

- [ ] **Step 1: Extend the integration-facing contract tests before wiring**

Add assertions that independently cover the boundary being wired:

- the published FastIO word is reduced to only the ten gameplay bits;
- service/start/test changes do not create gameplay journal records;
- the selected keyboard/controller source is already combined before the bridge sees it;
- clearing one controller source produces the correct resulting held/released mask;
- reset seeding preserves an overlapping still-held logical input without creating a rise.

Keep `InputMapperTests` as the oracle for source selection and `HighFpsInputBridgeTests` as the oracle for transition creation; do not add a source-text or duplicated-production test. These tests exercise both production seams directly. The final one-line call placement in `NativeInputWorker::Publish` is verified by review and later runtime counters rather than by adding an invasive worker harness.

- [ ] **Step 2: Run the focused tests as a green pre-wiring baseline**

```powershell
cmake --build --preset msvc32-debug --target InputMapperTests HighFpsInputBridgeTests
ctest --preset msvc32-debug -R '^(InputMapperTests|HighFpsInputBridgeTests)$'
```

Expected: PASS. This establishes the mapper and bridge contracts before the production worker is connected; if a new assertion fails, fix the owning Task 1-3 behavior before proceeding.

- [ ] **Step 3: Record transitions immediately after atomic publication**

In `NativeInputWorker::Publish`, preserve the existing FastIO exchange and diagnostics, then call the bridge only when `previous != next`:

```cpp
const std::uint32_t previous =
    g_published_input.exchange(next, std::memory_order_acq_rel);
if (previous != next) {
    gc::high_fps_input::RecordPublishedFastIoTransition(previous, next);
    // Existing optional diagnostics and debug logging remain after this call.
}
```

The bridge performs its own disabled-mode fast return at 60 FPS. Do not move or replace `g_published_input`; FastIO and menu reads retain the current publication path.

- [ ] **Step 4: Request epochs at existing clear boundaries**

After the cleared combined snapshot has been published:

- focus transition with `clear_input` requests `FocusLoss`;
- an XInput `connected -> disconnected` transition requests `DeviceDisconnect`;
- a Raw HID removal requests `DeviceDisconnect` after `ReopenRawHid` publishes the cleared controller state;
- worker shutdown exchanges the published snapshot to zero, records that final combined transition when needed, then requests `Shutdown`.

Use one local `ClearPublishedInput(reason)` helper for shutdown paths so the bridge's latest held mask cannot remain stale when existing code bypasses `Publish()`. Do not reset on an unchanged controller report, reconnect probe, or device arrival. Preserve the existing behavior in which clearing controller state does not clear a keyboard source selected by the mapper.

- [ ] **Step 5: Run the focused polling/bridge tests**

```powershell
cmake --build --preset msvc32-debug --target InputMapperTests InputPollingRuntimeStartupTests HighFpsInputBridgeTests
ctest --preset msvc32-debug -R '^(InputMapperTests|InputPollingRuntimeStartupTests|HighFpsInputBridgeTests)$'
```

Expected: PASS; the startup test still completes within its five-second timeout.

- [ ] **Step 6: Commit producer integration**

```powershell
git add src/Input/Polling/InputPollingRuntime.cpp tests/Input/HighFps/HighFpsInputBridgeTests.cpp tests/Input/Polling/InputMapperTests.cpp
git commit -m "Capture combined gameplay input transitions"
```

---

### Task 5: Install the Tune and query hooks as one input transaction

**Files:**

- Create: `src/Input/Switch/GameplayInputHookTransaction.h`
- Create: `src/Input/Switch/GameplayInputHookTransaction.cpp`
- Modify: `src/Input/Switch/SwitchInputPatch.h`
- Modify: `src/Input/Switch/SwitchInputPatch.cpp`
- Modify: `src/Input/CMakeLists.txt`
- Create: `tests/Input/Switch/GameplayInputHookTransactionTests.cpp`
- Modify: `tests/Input/Switch/SwitchInputPatchTests.cpp`
- Modify: `tests/Input/Switch/SwitchInputPolicyTests.cpp`
- Modify: `tests/Input/CMakeLists.txt`

- [ ] **Step 1: Write failing transaction and hook-plan tests**

Add a small feature-owned hook transaction test using fake install/reset callbacks. Verify:

- preflight is separate and occurs before any install callback;
- failure at every operation resets the failed operation defensively and all prior operations in reverse order;
- a complete operation set stays installed;
- an invalid descriptor and over-capacity plan fail without installs.

Extend `SwitchInputPatchTests` to verify:

- the new Tune signature identifies `SwitchHookSite::GameplayInputCommit` on mismatch;
- 60-FPS Arcade without diagnostics installs none of the four behavioral/query hooks;
- 60-FPS diagnostic Arcade installs only pressed/held pass-through hooks;
- 60-FPS Switch installs pressed/held/diagonal but no Tune commit hook;
- high-FPS Arcade requires Tune commit plus pressed/held;
- high-FPS Switch requires Tune commit, pressed/held, and diagonal;
- any missing required high-FPS hook leaves the bridge inactive.

Also exercise a production `TryReadGameplayInputCommitState` helper with an injected guarded-memory reader: the valid `[ebp-0x2B4] -> Tune -> +0x10/+0x14` chain returns current/step, while an unreadable frame pointer, null Tune pointer, unreadable field, or oversized address fails without calling the bridge.

- [ ] **Step 2: Run the focused tests and confirm red failures**

```powershell
cmake --build --preset msvc32-debug --target GameplayInputHookTransactionTests SwitchInputPatchTests SwitchInputPolicyTests
```

Expected: compilation fails because the transaction and Tune hook contract do not exist.

- [ ] **Step 3: Add the binary-verified Tune commit contract**

The existing IDA daemon for `H:\gc\game471.exe.i64` verified this exact flow:

```text
0x00664DB2  call final audio/song-clock step selector
0x00664DBD  call gameplay clock ms initializer
0x00664DC2  mov eax,[ebp-2B4h]       ; Tune object
0x00664DC8  mov ecx,[eax+10h]        ; current frame
0x00664DD1  add ecx,[edx+14h]        ; final selected step
0x00664DDC  call 0x00659860           ; native input alignment
```

Add this guarded contract to `SwitchInputPatch.h`:

```cpp
inline constexpr std::uintptr_t kGameplayInputCommitRva = 0x00264DC2;
inline constexpr std::ptrdiff_t kTuneStackOffset = -0x2B4;
inline constexpr std::uintptr_t kTuneCurrentFrameOffset = 0x10;
inline constexpr std::uintptr_t kTuneStepOffset = 0x14;

inline constexpr std::array<std::uint8_t, 19>
    kGameplayInputCommitSignature{
        0x8B, 0x85, 0x4C, 0xFD, 0xFF, 0xFF,
        0x8B, 0x48, 0x10,
        0x8B, 0x95, 0x4C, 0xFD, 0xFF, 0xFF,
        0x03, 0x4A, 0x14,
        0x51,
    };

struct GameplayInputCommitState {
    std::uint32_t current_frame{};
    std::uint32_t step{};
};

struct GuardedMemoryAccessor {
    void* context{};
    bool (*read)(void*, std::uintptr_t, void*, std::size_t) noexcept{};
};

[[nodiscard]] bool TryReadGameplayInputCommitState(
    std::uintptr_t frame_pointer,
    GuardedMemoryAccessor memory,
    GameplayInputCommitState& state) noexcept;
```

This hook is backend-neutral: it runs after the final step is available and before native input alignment for original, WASAPI, and ASIO-backed clock plans.

- [ ] **Step 4: Generalize the existing hook plan without changing Switch policy**

Extend the existing plan rather than installing a second pressed/held detour:

```cpp
struct SwitchHookPlan {
    bool gameplay_input_commit{};
    bool pressed_edge{};
    bool held_state{};
    bool diagonal_match{};
};

constexpr SwitchHookPlan BuildSwitchHookPlan(
    bool switch_requested,
    bool diagnostics_enabled,
    bool high_fps_bridge_requested) noexcept;
```

`high_fps_bridge_requested` selects commit/pressed/held. `switch_requested` independently selects pressed/held/diagonal. Diagnostics independently selects pass-through pressed/held. The union is installed once.

Build selected SafetyHook operations and pass them to `GameplayInputHookTransaction`. Reset order is diagonal, held, pressed, then Tune commit when all four were installed. Do not include the optional XIO/history diagnostic hooks in the required bridge transaction.

- [ ] **Step 5: Commit frames at the verified Tune boundary**

Create one `safetyhook::MidHook` callback. Put the guarded address walk in the tested `TryReadGameplayInputCommitState` helper; the hook passes the real guarded reader, captures QPC, and calls `CommitGameplayInputStep`. It does not alter registers, EIP, stack, Tune fields, or the native call sequence.

```cpp
void HookGameplayInputCommit(safetyhook::Context& context) noexcept {
    GameplayInputCommitState state{};
    LARGE_INTEGER now{};
    if (!TryReadGameplayInputCommitState(
            context.ebp, {nullptr, GuardedMemoryRead}, state) ||
        !QueryPerformanceCounter(&now)) {
        gc::high_fps_input::ReportCommitHookRuntimeFault();
        return;
    }
    gc::high_fps_input::CommitGameplayInputStep(
        state.current_frame, state.step, now.QuadPart);
}
```

Use the repository's guarded `__try/__except` read style and catch all C++ exceptions at the hook boundary. A read/QPC failure switches the bridge to native fallback; it never changes native execution.

- [ ] **Step 6: Make pressed/held wrappers query effective frame history**

Retain `query_original` as the fallback and native diagnostic source. Add `query_effective` that first asks the bridge for the requested device/input/frame and falls back to `query_original` when `handled == false`.

For Arcade style, query `query_effective` once. For Switch style, pass `query_effective` to `QueryButtonWithDirectionAliases`, so the requested button and each direction alias use the same corrected frame. Under `GC_ENABLE_INPUT_EDGE_DIAGNOSTICS`, call/record the native predicate for comparison even when the bridge handles the return value; outside diagnostic builds, do not make an unnecessary native call.

After final alias resolution, record one final query result. Treat caller RVAs `0x001D20E0` and `0x001D2176` as left/right free-tap activations only when the final pressed result is true. Those call sites are after the routine's other eligibility checks and immediately precede flags at gameplay-object offsets 237 and 238.

- [ ] **Step 7: Activate only after the complete hook set commits**

Change `SwitchInputPatchInit` to return `bool`.

- At high FPS, preflight all selected signatures before creating any hook. Any required failure deactivates the bridge, rolls back the complete selected hook set, logs the failed stage/site once, and returns `false`.
- After all selected hooks commit, call `ActivateHighFpsInputBridge(ReadPublishedInput())` and return `true`.
- At 60 FPS, preserve existing Switch fallback behavior: a Switch-only hook failure may leave Arcade active, but the high-FPS bridge remains disabled and native input values are unchanged.

- [ ] **Step 8: Run the focused transaction/query tests**

```powershell
cmake --build --preset msvc32-debug --target GameplayInputHookTransactionTests SwitchInputPatchTests SwitchInputPolicyTests HighFpsInputBridgeTests InputEdgeDiagnosticsTests
ctest --preset msvc32-debug -R '^(GameplayInputHookTransactionTests|SwitchInputPatchTests|SwitchInputPolicyTests|HighFpsInputBridgeTests|InputEdgeDiagnosticsTests)$'
```

Expected: PASS; two effective queries for one frame both succeed and the following frame does not.

- [ ] **Step 9: Commit the all-or-nothing query boundary**

```powershell
git add src/Input/CMakeLists.txt src/Input/Switch src/Input/HighFps tests/Input/CMakeLists.txt tests/Input/Switch tests/Input/HighFps/HighFpsInputBridgeTests.cpp
git commit -m "Install high-FPS gameplay input hooks"
```

---

### Task 6: Wire framerate lifecycle, playback epochs, and bounded summaries

**Files:**

- Modify: `src/Patches/Framerate/GameplaySongClock.h`
- Modify: `src/Patches/Framerate/GameplaySongClock.cpp`
- Modify: `src/Patches/Framerate/FrameratePatch.h`
- Modify: `src/Patches/Framerate/FrameratePatch.cpp`
- Modify: `src/Loader/DllMain.cpp`
- Modify: `tests/Patches/Framerate/GameplaySongClockTests.cpp`
- Modify: `tests/Patches/Framerate/FramerateRuntimeTests.cpp`
- Modify: `tests/Input/HighFps/HighFpsInputBridgeTests.cpp`

- [ ] **Step 1: Add failing lifecycle and capacity assertions**

Verify:

- `GameplaySongClock::maximum_step()` is `max(1, floor(target_fps * 50 / 1000))` for the existing 50-ms catch-up policy;
- 120, 144, 165, 240, 360, and 500 FPS all fit the 64-frame bridge range;
- the production `detail::BuildHighFpsInputBridgeConfig` helper uses maximum step `1` for non-shared clock plans, the clock's actual maximum for the shared plan, and enables playback gating only for the shared plan;
- an exact playback generation change resets pending transitions without a synthetic rise;
- explicit inactive playback flushes pending input, while a transient failed/absent observation does not repeatedly fabricate resets;
- one outer frame without a Tune commit marks gameplay inactive;
- a 60-FPS configuration remains behaviorally disabled despite lifecycle notifications.

- [ ] **Step 2: Run focused tests and confirm the new assertions fail**

```powershell
cmake --build --preset msvc32-debug --target GameplaySongClockTests FramerateRuntimeTests HighFpsInputBridgeTests
ctest --preset msvc32-debug -R '^(GameplaySongClockTests|FramerateRuntimeTests|HighFpsInputBridgeTests)$'
```

Expected: failure because maximum-step and lifecycle wiring are not exposed.

- [ ] **Step 3: Configure the bridge before executable mutation**

Add a trivial `maximum_step() const noexcept` accessor to `GameplaySongClock`. Add and directly test `detail::BuildHighFpsInputBridgeConfig(target, qpc_frequency, audio_clock_plan, shared_maximum_step)` in `FrameratePatch.h/.cpp`; production must use that helper rather than restating the policy at the call site.

In `FrameratePatchInit`, after profile/QPC/clock construction but before direct-patch planning or transaction installation, configure the bridge with the helper result:

```cpp
const auto bridge_maximum_step =
    g_runtime->gameplay_song_clock.has_value()
        ? g_runtime->gameplay_song_clock->maximum_step()
        : 1U;

const auto bridge_config = detail::BuildHighFpsInputBridgeConfig(
    target,
    frequency.QuadPart,
    g_runtime->audio_clock_plan,
    bridge_maximum_step);
const auto bridge =
    gc::high_fps_input::ConfigureHighFpsInputBridge(bridge_config);
```

On configuration failure, publish the existing framerate startup fatal before executable memory is changed. At 60 FPS configuration succeeds in disabled mode.

- [ ] **Step 4: Publish outer-frame and playback lifecycle signals**

In `HookOuterFrame`, after obtaining `outer_epoch` and QPC, call `ObserveGameplayOuterFrame(outer_epoch, now.QuadPart)`. This reuses the existing mandatory outer hook and adds no new executable contract.

In `HookGameplaySongClock`, call `ObserveGameplayPlayback` immediately after resolving the input selection and before any early return:

- `Exact` and `Rounded` are active;
- `Exact` supplies `playback_generation` on every observation so the bridge can detect a change;
- `Inactive` marks gameplay inactive and flushes pending records;
- `Failed` leaves the current activity state alone and preserves native fallback behavior.

The later Tune commit hook remains the only place that assigns transitions to gameplay frames.

- [ ] **Step 5: Emit one compact bridge line on the existing cadence**

Extend the existing five-second `MaybeLogRuntimeStats` path to take and format a bridge snapshot. Do not create another timer or logging thread. Use a stable prefix and bounded fields:

```text
HighFpsInput: mode=active target=240 epoch=3 capture=42/41 enqueue=83 commit=83 frames=96 step=120/2100/3 defer=17 query=152/801 held=440 free_tap=21/20 depth=2 max_age_us=6120 reset=0/0/1/0 overflow=0 fallback=0 missing=0 duplicate=0
```

The exact field names may be compact, but every diagnostic listed in the approved design must be represented by an interval/cumulative value or a clearly named maximum. The existing `InputEdgeDiag` line remains the native-history/native-query half of the comparison; the new line reports committed/effective bridge behavior. Do not print individual transitions.

- [ ] **Step 6: Fail closed in the game process when the required bridge cannot install**

Update `DllMain.cpp`:

```cpp
if (!gc::switch_input::SwitchInputPatchInit()) {
    PLOG_ERROR << "GameplayInputPatch: fail-closed DLL attach";
    return FALSE;
}
```

The NESYS process continues to skip all game-only input/framerate initialization. At 60 FPS, this preserves the existing optional Switch-to-Arcade fallback; only a required high-FPS bridge failure returns `false`.

- [ ] **Step 7: Run lifecycle and startup tests**

```powershell
cmake --build --preset msvc32-debug --target GameplaySongClockTests FramerateRuntimeTests HighFpsInputBridgeTests SwitchInputPatchTests iDmacDrv32
ctest --preset msvc32-debug -R '^(GameplaySongClockTests|FramerateRuntimeTests|HighFpsInputBridgeTests|SwitchInputPatchTests)$'
```

Expected: PASS and a linked x86 DLL.

- [ ] **Step 8: Commit lifecycle integration**

```powershell
git add src/Patches/Framerate/GameplaySongClock.h src/Patches/Framerate/GameplaySongClock.cpp src/Patches/Framerate/FrameratePatch.h src/Patches/Framerate/FrameratePatch.cpp src/Loader/DllMain.cpp tests/Patches/Framerate/GameplaySongClockTests.cpp tests/Patches/Framerate/FramerateRuntimeTests.cpp tests/Input/HighFps/HighFpsInputBridgeTests.cpp
git commit -m "Wire gameplay input epochs and diagnostics"
```

---

### Task 7: Verify behavior, real-time constraints, and both build modes

**Files:**

- Modify only if verification exposes a defect in files owned by Tasks 1-6.

- [ ] **Step 1: Audit the hot paths mechanically**

Run:

```powershell
rg -n "PLOG_|std::mutex|condition_variable|sleep|new |make_unique|make_shared|push_back|std::vector|std::string|ostringstream" src/Input/HighFps src/Input/Switch/SwitchInputPatch.cpp
rg -n "CommitGameplayInputStep|QueryGameplayPressed|QueryGameplayHeld|RecordPublishedFastIoTransition" src
```

Inspect every hit. Startup/periodic formatting outside the hot methods is allowed. Producer, commit, and effective-query methods must contain no allocation, wait, lock, exception escape, or log call.

- [ ] **Step 2: Run the complete Debug suite with diagnostics disabled**

```powershell
$env:GC_ASIO_SDK_DIR='H:\gc\artifacts\ASIOSDK'
cmake --preset msvc32-debug -DGC_ENABLE_INPUT_EDGE_DIAGNOSTICS=OFF
cmake --build --preset msvc32-debug
ctest --preset msvc32-debug -j 4 --output-on-failure
```

Expected: all targets build and the complete suite passes. This proves the bridge does not depend on diagnostic-only code.

- [ ] **Step 3: Run the complete Release suite with diagnostics enabled**

```powershell
cmake --preset msvc32-release -DGC_ENABLE_INPUT_EDGE_DIAGNOSTICS=ON
cmake --build --preset msvc32-release
ctest --preset msvc32-release -j 4 --output-on-failure
```

Expected: all targets build and the complete suite passes under optimization with the runtime validation telemetry enabled.

- [ ] **Step 4: Validate source scope and artifact identity**

```powershell
git diff --check
git status --short
git log --oneline --decorate -10
Get-Item build-msvc32-release\dist\iDmacDrv32.dll | Select-Object FullName,Length,LastWriteTime
Get-FileHash build-msvc32-release\dist\iDmacDrv32.dll -Algorithm SHA256
```

Confirm:

- only the approved input, framerate lifecycle, loader startup, tests, CMake lists, spec status, and plan files changed;
- no generated build output is tracked;
- the Release DLL is newer than the last implementation commit;
- automated verification is reported separately from gameplay acceptance.

- [ ] **Step 5: Commit only verification fixes, if needed**

If verification required a source fix, make one focused commit naming the corrected behavior. Do not create an empty verification commit.

---

### Task 8: Deploy the verified candidate and follow the 240-then-60 runtime gate

**Files:**

- Deploy: `build-msvc32-release/dist/iDmacDrv32.dll` -> `H:\gc\iDmacDrv32.dll`
- Preserve: `H:\gc\config.toml`, `H:\gc\card.txt`, ASIO settings, logs, and every other runtime file.

- [ ] **Step 1: Verify the game is stopped**

```powershell
$game = Get-Process -Name game471 -ErrorAction SilentlyContinue
if ($game) { throw 'game471 is running; deployment not attempted' }
```

Expected: no process. Do not terminate the game automatically.

- [ ] **Step 2: Copy only the verified DLL, without a backup**

```powershell
$source = 'H:\gc\artifacts\GCLoader\.worktrees\asio-audio-backend\build-msvc32-release\dist\iDmacDrv32.dll'
$target = 'H:\gc\iDmacDrv32.dll'
$sourceHash = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
Copy-Item -LiteralPath $source -Destination $target -Force
$targetHash = (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash
if ($sourceHash -ne $targetHash) { throw 'deployed DLL hash mismatch' }
$sourceHash
```

Expected: source and runtime hashes match. Do not create a backup directory or copy the distribution/source archive.

- [ ] **Step 3: Hand off the 240-FPS run first**

Tell the operator the candidate is deployed and request only the 240-FPS run. The run must cover:

- empty chart sections and selectable free tap sounds;
- short notes and long-note heads;
- continued long-note holds and real releases;
- chords and dense passages;
- absence of duplicate or stuck inputs;
- unchanged judgement feel;
- no visible frame-pacing or audio-deadline regression.

Expected log conditions: bridge mode `active`, positive Tune commit counts,
`carry=0/0 window=1`, delivery/expiry/coalescing counters present, no overflow,
no runtime/native fallback during active gameplay, no missing/duplicate frame
invariant, and bounded queue depth/event age.

- [ ] **Step 4: Stop on any 240-FPS issue**

If the operator reports any miss, duplicate, stuck hold, free-tap failure, performance regression, or odd judgement at 240 FPS, inspect that latest `H:\gc\loader-log.txt` before requesting a 60-FPS run. Correct and redeploy through the same verification gate.

- [ ] **Step 5: Request the 60-FPS reference only after 240 FPS is accepted**

At 60 FPS the bridge must report disabled/no-op behavior. Passive query diagnostics must match native results, while ASIO/WASAPI and optional Switch behavior remain independently active. Compare the separately identifiable 240- and 60-FPS sessions without treating automated tests as subjective gameplay acceptance.

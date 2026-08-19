> **ARCHIVED FAILED ATTEMPT — DO NOT EXECUTE.** This plan and its successors
> did not produce correct game behavior.

# High-FPS Input Judgement Transactions Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the runtime-disproved exact-frame input bridge with a bounded, timestamp-preserving judgement transaction that removes high-FPS short-input drops while preserving native 60 FPS and Switch judgement behavior.

**Architecture:** A newest-wins transition transport feeds a QPC-based retained input timeline. Each call to the native judgement core opens one immutable transaction that exposes current state, exact 60 Hz history, and physical rises pending for no more than 1 / 60 second; note/free-tap consumption commits only when the core call ends. Guarded hooks leave recognition time authoritative, substitute event time only at the shared late gate and two grade callers, and compose the existing Switch aliases after the temporal correction.

**Tech Stack:** C++23, Win32 x86, QueryPerformanceCounter, SafetyHook, plog, CMake 3.31+, Ninja, CTest, MSVC x86.

## Global Constraints

- The approved design is `docs/superpowers/specs/2026-08-15-high-fps-input-judgement-transactions-design.md`. The locked compatibility record is `docs/reverse-engineering/high-fps-input-judgement-decisions.md`.
- Work in `H:\gc\artifacts\GCLoader\.worktrees\asio-audio-backend`. Runtime `H:\gc` is inspected or deployed only in the final runtime step.
- Execute inline. Do not dispatch subagents and do not create another worktree.
- Use the existing IDA daemon for `H:\gc\game471.exe.i64` through `AgentSession.connect`. Do not start, restart, or shut down the daemon.
- The supported `game471.exe` is 3,691,008 bytes with SHA-256 `FEAD3BD4D0E0985F101965EDC417DD2B96522F8716FF789D84618FEB0D7A2522`.
- Target Windows x86 and preserve every existing calling convention, return value, and exception boundary.
- At `target_fps = 60`, the new temporal correction is a behavioral no-op. The independently selected Switch aliases remain active.
- Recognition time remains authoritative for chart selection, the outer early gate, note state, misses, history-only forgiveness, paired aggregation, holds, repeats, and duration mechanics.
- A physical edge remains note-eligible only through exactly 1 / 60 second by rational QPC comparison. It is presented to native free tap at most once and retains one immutable event timestamp.
- Eligibility is `native recognition acceptance OR selected event-time acceptance`. The patch may add a success but may never turn a native success into failure.
- Physical time changes only the shared late-gate time and the existing grade-helper argument for a selected new edge. Never substitute it for the core song time.
- `JudgTimeOffset` and `GameTimeOffset` remain the only live timing settings in scope. Apply the event/recognition delta to the game's existing adjusted arguments so both offsets compose automatically; do not read or cache either setting in the input layer.
- Reconstruct `current_frame - 2` at recognition QPC minus exactly 2 / 60 second. The native held-age `<= 4` window ends after exactly 4 / 60 second.
- Preserve `HoldSafeFrame = 0`, `SlideHoldSafeFrame = 0`, `ScratchEnableTime = 250` milliseconds, and `BeatEnableTime = 200` milliseconds without reading or scaling them.
- Preserve all locked Switch rules for same-booster direction-to-button edges and holds, adjacent-cardinal diagonal matching, real inputs, paired boosters, and gameplay-only scope.
- Snapshot the native direction-normalization table once at high-FPS hook activation; do not assume the supported cabinet uses the identity mapping.
- Hot gameplay paths allocate no memory and emit no per-frame or per-query log.
- When a fixed buffer fills, append the newest record, discard the oldest, count the eviction, and continue. Do not enter fallback mode because of overflow.
- Remove the old target-frame mapper, query-driven expiry, multi-frame carry, step counters, and gameplay-input-commit hook. Do not layer the replacement over them.
- Build and test under `C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat` with `GC_ASIO_SDK_DIR=H:\gc\artifacts\ASIOSDK`.
- Static/build evidence is not cabinet acceptance. After deployment, the user runs 240 FPS first; any issue stops the sequence before the 60 FPS comparison.

---

## File Structure

### Transition and policy core

- Modify `src/Input/HighFps/InputTransitionJournal.h` and `.cpp`: fixed-capacity inter-thread transport; newest-wins overflow and ordered draining only.
- Create `src/Input/HighFps/JudgementInputTimeline.h` and `.cpp`: consumer-owned full-state history, rational state-at-time queries, pending edge expiry, and independent note/free-tap consumption.
- Create `src/Input/HighFps/JudgementInputTransaction.h` and `.cpp`: immutable per-core view, note-type policy, Arcade/Switch composition, query decisions, late-gate selection, grade delta, and deferred commit.
- Delete `src/Input/HighFps/GameplayInputTimeline.h` and `.cpp`: the superseded gameplay-frame mapper.

### Runtime and hooks

- Rewrite `src/Input/HighFps/HighFpsInputBridge.h` and `.cpp`: lifecycle facade joining producer transport, retained timeline, current transaction, and bounded diagnostics.
- Create `src/Input/HighFps/HighFpsJudgementHooks.h` and `.cpp`: supported-binary manifest, guarded preflight, all-or-nothing high-FPS hook set, and native-call wrappers.
- Modify `src/Input/Switch/SwitchInputPatch.h` and `.cpp`: compose pressed/held hooks and Switch aliases with the new bridge; replace the old commit-hook operation with the high-FPS judgement hook set.
- Keep `src/Input/Switch/SwitchInputPolicy.h` and `.cpp` behavior intact.

### Integration and evidence

- Modify `src/Input/Polling/InputPollingRuntime.cpp` only where renamed reset/runtime APIs require it; transition publication remains owned by polling.
- Modify `src/Patches/Framerate/FrameratePatch.h` and `.cpp`: simplify bridge configuration and retain only playback/outer-epoch lifecycle observations.
- Modify `src/Input/CMakeLists.txt` and `tests/Input/CMakeLists.txt` for the replacement files and test targets.
- Create `docs/reverse-engineering/high-fps-input-judgement-hook-manifest.md` with the executable identity, hook/helper signatures, caller inventories, and IDA-derived argument contracts.

### Tests

- Modify `tests/Input/HighFps/InputTransitionJournalTests.cpp`.
- Create `tests/Input/HighFps/JudgementInputTimelineTests.cpp`.
- Create `tests/Input/HighFps/JudgementInputTransactionTests.cpp`.
- Rewrite `tests/Input/HighFps/HighFpsInputBridgeTests.cpp`.
- Create `tests/Input/HighFps/HighFpsJudgementHooksTests.cpp`.
- Modify `tests/Input/Switch/SwitchInputPatchTests.cpp` and `tests/Input/Switch/GameplayInputHookTransactionTests.cpp`.
- Modify `tests/Patches/Framerate/FramerateRuntimeTests.cpp`.
- Delete `tests/Input/HighFps/GameplayInputTimelineTests.cpp`.

---

### Task 1: Make transition transport newest-wins

**Files:**
- Modify: `src/Input/HighFps/InputTransitionJournal.h:1-48`
- Modify: `src/Input/HighFps/InputTransitionJournal.cpp:1-108`
- Modify: `tests/Input/HighFps/InputTransitionJournalTests.cpp:1-210`

**Interfaces:**
- Consumes: timestamped `InputTransitionRecord` values from the input-polling producer.
- Produces:

```cpp
struct TransitionPushResult {
    bool evicted_oldest{};
    std::uint32_t depth{};
};

TransitionPushResult PushNewest(InputTransitionRecord record) noexcept;
std::size_t DrainInto(std::span<InputTransitionRecord> output) noexcept;
std::uint64_t eviction_count() const noexcept;
```

- `BuildTransitionRecord` and `GameplayMaskFromFastIo` retain their current signatures and mapping.

- [ ] **Step 1: Change the overflow and concurrency tests to the new contract**

Replace the “first over-capacity record is rejected” expectation with a fill of `kTransitionCapacity + 1` records:

```cpp
const auto overflow = wrapped.PushNewest(Record(kTransitionCapacity));
failures += Expect(
    overflow.evicted_oldest && overflow.depth == kTransitionCapacity,
    "full transport keeps the newest record and evicts one oldest record");

std::array<InputTransitionRecord, kTransitionCapacity> drained{};
const auto count = wrapped.DrainInto(drained);
failures += Expect(
    count == kTransitionCapacity &&
        drained.front().sequence == 1 &&
        drained.back().sequence == kTransitionCapacity,
    "drain preserves FIFO order across newest-wins eviction");
```

Rewrite the producer/consumer stress so the producer never retries. The consumer accepts monotonically increasing sequence numbers with gaps, requires the final published sequence to be observed, and verifies:

```cpp
observed_evictions + observed_records == stress_count
```

Keep the independent FastIO mapping, unchanged-state, simultaneous-cohort, seed, wrap, and drain-empty assertions.

- [ ] **Step 2: Build the focused test and verify RED**

Run:

```powershell
$env:GC_ASIO_SDK_DIR='H:\gc\artifacts\ASIOSDK'
& $env:ComSpec /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build --preset msvc32-debug --target InputTransitionJournalTests && ctest --preset msvc32-debug -R "^InputTransitionJournalTests$" --output-on-failure'
```

Expected: compile failure because `PushNewest` and `DrainInto` do not exist, or assertion failure because the current queue rejects the newest record.

- [ ] **Step 3: Implement a fixed, synchronized newest-wins ring**

Replace separate peek/pop operations with one short critical section around producer append and consumer drain. Keep storage as the existing fixed `std::array`; add no heap allocation.

```cpp
TransitionPushResult InputTransitionJournal::PushNewest(
    InputTransitionRecord record) noexcept
{
    std::lock_guard lock(mutex_);
    bool evicted = false;
    if (size_ == records_.size()) {
        read_slot_ = (read_slot_ + 1) % records_.size();
        --size_;
        ++eviction_count_;
        evicted = true;
    }
    const auto write_slot = (read_slot_ + size_) % records_.size();
    records_[write_slot] = record;
    ++size_;
    return {evicted, static_cast<std::uint32_t>(size_)};
}
```

`DrainInto` holds the same mutex, copies at most `output.size()` oldest records in FIFO order, advances `read_slot_`, and returns the copied count. `DiscardAll` clears `size_` without resetting the cumulative eviction counter. Remove `overflow_generation` and every API that permits a consumer to observe a record and pop it in separate operations.

- [ ] **Step 4: Run the focused test and verify GREEN**

Run the Step 2 command again.

Expected: `InputTransitionJournalTests` passes under the actual two-thread stress; ThreadSanitizer is not available on this Windows x86 build, so the stress must detect torn records, non-monotonic order, lost final publication, and accounting errors.

- [ ] **Step 5: Commit**

```powershell
git add -- src/Input/HighFps/InputTransitionJournal.h src/Input/HighFps/InputTransitionJournal.cpp tests/Input/HighFps/InputTransitionJournalTests.cpp
git commit -m "Keep newest high-FPS input transitions"
```

### Task 2: Add the QPC history and pending-edge timeline

**Files:**
- Create: `src/Input/HighFps/JudgementInputTimeline.h`
- Create: `src/Input/HighFps/JudgementInputTimeline.cpp`
- Create: `tests/Input/HighFps/JudgementInputTimelineTests.cpp`
- Modify: `src/Input/CMakeLists.txt:20-36`
- Modify: `tests/Input/CMakeLists.txt:91-104`

**Interfaces:**
- Consumes: ordered `InputTransitionRecord` batches, one epoch seed, recognition QPC, and positive QPC frequency.
- Produces:

```cpp
struct InputEdgeCandidate {
    std::uint64_t sequence{};
    std::int64_t qpc{};
    GameplayInputMask cohort{};
    std::uint8_t source_input{};
};

inline constexpr std::size_t kJudgementHistoryCapacity = 1024;

struct JudgementInputView {
    std::uint64_t epoch{};
    std::int64_t recognition_qpc{};
    GameplayInputMask held_now{};
    GameplayInputMask held_two_60hz_frames_ago{};
    std::array<std::uint8_t, kGameplayLogicalInputCount> held_age_60hz{};
    std::array<std::optional<InputEdgeCandidate>,
               kGameplayLogicalInputCount> note_edges{};
    std::array<std::optional<InputEdgeCandidate>,
               kGameplayLogicalInputCount> free_tap_edges{};
};

struct JudgementInputUseCommit {
    std::array<std::uint64_t, kGameplayLogicalInputCount>
        note_consumed_sequences{};
    std::array<std::uint64_t, kGameplayLogicalInputCount>
        free_tap_presented_sequences{};
};

class JudgementInputTimeline final {
public:
    void Reset(std::uint64_t epoch, GameplayInputMask held,
               std::int64_t qpc) noexcept;
    void Append(std::span<const InputTransitionRecord> records) noexcept;
    JudgementInputView BeginView(std::int64_t recognition_qpc,
                                std::int64_t qpc_frequency) noexcept;
    void CommitUses(const JudgementInputUseCommit& commit) noexcept;
    TimelineDiagnostics TakeDiagnostics() const noexcept;
};
```

- [ ] **Step 1: Add boundary tests using independently calculated rational oracles**

Use `qpc_frequency = 10'000'000` and cross-multiplication rather than target-frame counts. Assert:

```cpp
failures += Expect(
    IsWithin60HzIntervals(166'666, 1, 10'000'000) &&
        !IsWithin60HzIntervals(166'667, 1, 10'000'000),
    "pending edge expires at the exact rational 1/60 boundary");
failures += Expect(
    IsWithin60HzIntervals(666'666, 4, 10'000'000) &&
        !IsWithin60HzIntervals(666'667, 4, 10'000'000),
    "held-age forgiveness expires at the exact rational 4/60 boundary");
```

Add cases for:

- reset with a held control: `held_now` true, age reported as expired (`5`), and no note/free-tap edge;
- a rise visible in both edge arrays at the event QPC and at the last rational tick within 1 / 60;
- note consumption hiding only `note_edges` while the same sequence remains free-tap eligible;
- free-tap presentation hiding only `free_tap_edges` while note availability remains;
- both flags hiding the edge after a commit;
- current state after a release and reconstructed state at recognition minus exactly 2 / 60;
- synthetic held age `0` at the event, `1` through 1 / 60, `4` through 4 / 60, and `5` afterward;
- simultaneous different logical inputs retaining the same sequence/QPC cohort;
- two published transitions of one logical input retaining their real order;
- multiple still-unconsumed rises of the same logical input inside one 1 / 60
  sampling opportunity coalescing to the newest rise, while different logical
  inputs in one cohort remain independent;
- state queries older than the retained interval using the oldest full state;
- fixed-history overflow evicting oldest, keeping newest, and incrementing an eviction counter;
- epoch-mismatched records being ignored;
- 120, 144, 165, 240, and 360 target-rate loop schedules all producing identical QPC boundary answers.

- [ ] **Step 2: Register the test and verify RED**

Add `JudgementInputTimeline.cpp` to `gc_input` and register `JudgementInputTimelineTests`. Run:

```powershell
$env:GC_ASIO_SDK_DIR='H:\gc\artifacts\ASIOSDK'
& $env:ComSpec /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build --preset msvc32-debug --target JudgementInputTimelineTests && ctest --preset msvc32-debug -R "^JudgementInputTimelineTests$" --output-on-failure'
```

Expected: compile failure because the new timeline types do not exist.

- [ ] **Step 3: Implement rational helpers and retained full-state records**

Implement comparison without dividing the QPC frequency:

```cpp
bool IsWithin60HzIntervals(
    std::int64_t elapsed_qpc,
    std::uint32_t intervals,
    std::int64_t frequency) noexcept
{
    if (elapsed_qpc < 0 || frequency <= 0) {
        return false;
    }
    return static_cast<std::uint64_t>(elapsed_qpc) * 60ULL <=
        static_cast<std::uint64_t>(frequency) * intervals;
}
```

The bounded elapsed values are at most 4 / 60 second, so the products are safe for the validated QPC frequency. Compute synthetic age with exact ceiling semantics and saturate at `5`:

```cpp
const auto numerator = static_cast<std::uint64_t>(elapsed) * 60ULL;
const auto age = elapsed == 0
    ? 0U
    : static_cast<std::uint32_t>(std::min<std::uint64_t>(
          5U,
          (numerator + static_cast<std::uint64_t>(frequency) - 1U) /
              static_cast<std::uint64_t>(frequency)));
```

Retain complete post-transition state plus per-logical note-consumed and free-tap-presented masks. `BeginView` expires old pending flags, reconstructs state at `recognition_qpc - 2/60` by rational comparison, and copies a fixed ten-input snapshot. If more than one unconsumed rise for one logical input is still inside the same bounded sampling opportunity, expose the newest sequence and mark older same-input rises coalesced for that purpose. Never coalesce different logical inputs. The timeline must not mutate again until `CommitUses`.

- [ ] **Step 4: Implement independent consumption and newest-wins history**

`CommitUses` matches both sequence and logical source before setting a consumption flag; a stale or mismatched commit is ignored and counted as an invariant anomaly. When retained storage fills, overwrite exactly one oldest record, count it, and preserve the new record. A seeded record contains full held state but zero rise/fall masks.

- [ ] **Step 5: Run the timeline and journal tests**

```powershell
& $env:ComSpec /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build --preset msvc32-debug --target InputTransitionJournalTests JudgementInputTimelineTests && ctest --preset msvc32-debug -R "^(InputTransitionJournalTests|JudgementInputTimelineTests)$" --output-on-failure'
```

Expected: both pass, including non-divisor QPC boundaries, immutable consumption separation, history overflow, and simultaneous cohorts.

- [ ] **Step 6: Commit**

```powershell
git add -- src/Input/HighFps/JudgementInputTimeline.h src/Input/HighFps/JudgementInputTimeline.cpp src/Input/CMakeLists.txt tests/Input/HighFps/JudgementInputTimelineTests.cpp tests/Input/CMakeLists.txt
git commit -m "Add QPC judgement input timeline"
```

### Task 3: Implement the immutable note-aware transaction policy

**Files:**
- Create: `src/Input/HighFps/JudgementInputTransaction.h`
- Create: `src/Input/HighFps/JudgementInputTransaction.cpp`
- Create: `tests/Input/HighFps/JudgementInputTransactionTests.cpp`
- Modify: `src/Input/CMakeLists.txt`
- Modify: `tests/Input/CMakeLists.txt`
- Test unchanged contract: `tests/Input/Switch/SwitchInputPolicyTests.cpp`

**Interfaces:**
- Consumes: one `JudgementInputView`, current gameplay frame, recognition milliseconds/QPC, QPC frequency, `gc::input::GameplayInputStyle`, and a ten-entry native direction-normalization table captured at activation.
- Produces:

```cpp
enum class GameplayNoteType : std::uint8_t {
    None = 0, Normal = 1, Flick = 2, Hold = 3,
    Scratch = 4, Beat = 5, MerryGoRound = 6,
    Hidden = 7, Hidden2 = 8, Critical = 9,
    SlideHold = 10, SlideCounter = 11, Turn = 12,
    Spin = 13, Finish = 14, DualHold = 15,
};

enum class PressedQueryPurpose : std::uint8_t {
    Unscoped, Note, FreeTap,
};

struct NoteEvaluation {
    GameplayNoteType type{GameplayNoteType::None};
    std::uintptr_t note_identity{};
    std::uint32_t lane{};
    int recognition_ms{};
    std::array<int, 3> accepted_target_directions{};
};

using DirectionNormalizationTable = std::array<int, 10>;

struct EffectiveInputQuery {
    bool handled{};
    bool value{};
    InputMatchReason reason{InputMatchReason::Native};
    std::optional<InputEdgeCandidate> edge{};
};

class JudgementInputTransaction final {
public:
    void BeginNote(NoteEvaluation note) noexcept;
    void EndNote(bool handler_result) noexcept;
    EffectiveInputQuery ProbePressed(int source_input, bool native_value,
                                     PressedQueryPurpose purpose) noexcept;
    void AcceptPressed(int requested_input, int source_input,
                       PressedQueryPurpose purpose, bool result) noexcept;
    EffectiveInputQuery QueryHeld(int logical_input,
                                  std::uint32_t requested_frame,
                                  bool native_value) const noexcept;
    int QueryHeldAge(int logical_input, int native_value) const noexcept;
    int QueryDirection(int booster, int native_value) const noexcept;
    void CompleteDirectionMatch(bool accepted) noexcept;
    int SelectLateGateTime(int recognition_ms) const noexcept;
    int SelectGradeArgument(int original_argument) const noexcept;
    JudgementInputUseCommit Finish() noexcept;
};
```

- [ ] **Step 1: Add a complete note policy table test**

Build an independently declared expected table with exactly 16 entries:

```cpp
constexpr std::array expected{
    Expected{0, NoteInputKind::None, false, false},
    Expected{1, NoteInputKind::Button, true, false},
    Expected{2, NoteInputKind::Direction, true, false},
    Expected{3, NoteInputKind::Hold, false, true},
    Expected{4, NoteInputKind::Scratch, false, true},
    Expected{5, NoteInputKind::Beat, false, true},
    Expected{6, NoteInputKind::Button, true, false},
    Expected{7, NoteInputKind::Button, true, false},
    Expected{8, NoteInputKind::Button, true, false},
    Expected{9, NoteInputKind::PairedButton, true, false},
    Expected{10, NoteInputKind::DirectionHold, false, true},
    Expected{11, NoteInputKind::Lifecycle, false, false},
    Expected{12, NoteInputKind::Lifecycle, false, false},
    Expected{13, NoteInputKind::Lifecycle, false, false},
    Expected{14, NoteInputKind::Lifecycle, false, false},
    Expected{15, NoteInputKind::PairedHold, false, true},
};
```

For every row assert the policy kind, whether `0x5D0E00` grade selection is permitted, and whether long-form mechanics stay recognition-driven. Add free tap as a separate purpose, not a synthetic note type.

- [ ] **Step 2: Add behavioral transaction tests**

Use small hand-built `JudgementInputView` fixtures and assert:

- native true remains true with no edge and selects recognition time;
- native false becomes true only when a valid pending edge exists;
- NORMAL, HIDDEN, and HIDDEN2 run the same button-edge transaction while
  retaining their distinct type IDs;
- MERRY GO ROUND uses the button-edge transaction and applies its physical
  delta after the native segment adjustment;
- HOLD uses a pending edge only for its head and current held state for its
  body/release;
- an edge at the exact 1 / 60 boundary is visible and one tick later is absent;
- `AcceptPressed` records no consumption until `Finish`;
- every query before `Finish` still sees the same edge;
- free-tap acceptance marks only presentation, so a later transaction may use the same sequence for a note;
- presentation is false in the next transaction and therefore cannot replay its sound;
- Arcade real button takes precedence when a real button and direction rise together;
- Switch button queries accept each same-booster direction as an independent edge, including a second direction while the first is held;
- Switch held queries accept real button or any same-booster direction;
- exact diagonals remain native successes and either adjacent cardinal satisfies a Switch diagonal;
- a short direction down/up retained as an edge can satisfy a FLICK head without becoming hold continuation;
- current held, frame-minus-two history, and age are coherent across repeated calls;
- history-only direction acceptance has no selected physical grade edge;
- CRITICAL sees both boosters from one snapshot and selects the latest required new edge as completion time;
- a mixed real-button/direction-alias CRITICAL works per booster;
- a previously stored CRITICAL component is not retroactively regraded;
- DUAL HOLD stages both head consumptions while body held state remains current-time state;
- SLIDE HOLD head may select a new direction edge, but continuation never selects another grade time;
- SCRATCH selects only a direction actually accepted by its native query order;
- BEAT consumes each distinct press and leaves repeat interval time unchanged;
- types 11 through 14 never synthesize a query decision.

After constructing fixtures, enable the same global-new allocation probe used
by `tests/Audio/OutputPacingTrackerTests.cpp` around 10,000
begin/query/finish cycles and require zero allocations.

- [ ] **Step 3: Add timing-selection tests**

With recognition QPC `1'000'000`, event QPC `950'000`, and frequency `10'000'000`, require a rounded physical delta of `-5` milliseconds:

```cpp
failures += Expect(
    tx.SelectLateGateTime(1'200) == 1'195,
    "selected physical edge moves only the late-gate comparison time");
failures += Expect(
    tx.SelectGradeArgument(1'137) == 1'132,
    "grade delta composes with the caller's existing offset adjustment");
```

Also assert:

- half-millisecond ties round symmetrically away from zero;
- no selected edge returns both original arguments unchanged;
- history-only acceptance returns both unchanged;
- MERRY GO ROUND's pre-adjusted argument receives only the delta;
- nonzero `JudgTimeOffset` and `GameTimeOffset` fixtures change the original
  recognition/grade arguments, while the independently calculated physical
  delta remains identical;
- the transaction never exposes a replacement core/song time;
- duration/repeat note kinds have no grade adjustment;
- a selected event time cannot be later than recognition QPC;
- paired simultaneous edges share zero relative skew.

- [ ] **Step 4: Register/build and verify RED**

```powershell
& $env:ComSpec /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build --preset msvc32-debug --target JudgementInputTransactionTests && ctest --preset msvc32-debug -R "^JudgementInputTransactionTests$" --output-on-failure'
```

Expected: compile failure because the transaction policy does not exist.

- [ ] **Step 5: Implement the policy without runtime addresses**

Keep this module pure and allocation-free. Map held-mask directions to numpad codes `1..9` with `5` neutral:

```cpp
const int vertical = up == down ? 0 : (up ? 1 : -1);
const int horizontal = left == right ? 0 : (right ? 1 : -1);
return DirectionCode(vertical, horizontal);
```

Use the existing `DirectionAliasesForButton` and `IsSwitchDiagonalComponent` rather than duplicating Switch tables. For note heads, a valid retained directional rise may provide transient effective held/age/direction state; continuation queries use `held_now` only. Store proposed sequence consumption in the transaction and write it only from `Finish`.

Map the raw numpad direction through the injected
`DirectionNormalizationTable` before comparing it with the note's three
accepted target directions. Tests use identity and non-identity tables so a
future refactor cannot silently assume the cabinet mapping.

`SelectLateGateTime` returns the selected event time in the recognition-time domain. `SelectGradeArgument` applies the same rounded QPC delta to the already-adjusted caller argument. Neither method changes transaction recognition time.

- [ ] **Step 6: Run policy tests**

```powershell
& $env:ComSpec /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build --preset msvc32-debug --target JudgementInputTransactionTests SwitchInputPolicyTests && ctest --preset msvc32-debug -R "^(JudgementInputTransactionTests|SwitchInputPolicyTests)$" --output-on-failure'
```

Expected: both pass; the existing Switch policy test must require no changed expectation.

- [ ] **Step 7: Commit**

```powershell
git add -- src/Input/HighFps/JudgementInputTransaction.h src/Input/HighFps/JudgementInputTransaction.cpp src/Input/CMakeLists.txt tests/Input/HighFps/JudgementInputTransactionTests.cpp tests/Input/CMakeLists.txt
git commit -m "Add immutable judgement input policy"
```

### Task 4: Replace the exact-frame bridge with transaction lifecycle

**Files:**
- Rewrite: `src/Input/HighFps/HighFpsInputBridge.h:1-255`
- Rewrite: `src/Input/HighFps/HighFpsInputBridge.cpp:1-855`
- Rewrite: `tests/Input/HighFps/HighFpsInputBridgeTests.cpp:1-407`
- Delete: `src/Input/HighFps/GameplayInputTimeline.h`
- Delete: `src/Input/HighFps/GameplayInputTimeline.cpp`
- Delete: `tests/Input/HighFps/GameplayInputTimelineTests.cpp`
- Modify: `src/Input/CMakeLists.txt`
- Modify: `tests/Input/CMakeLists.txt`

**Interfaces:**
- Consumes: polling transitions, outer/playback lifecycle, and calls from the future hook layer.
- Produces:

```cpp
struct GameplayJudgementContext {
    std::uint32_t gameplay_frame{};
    int recognition_ms{};
    std::int64_t recognition_qpc{};
};

bool BeginGameplayJudgement(GameplayJudgementContext context) noexcept;
void EndGameplayJudgement() noexcept;
void BeginGameplayNote(NoteEvaluation note) noexcept;
void EndGameplayNote(bool handler_result) noexcept;
EffectiveInputQuery QueryGameplayPressed(
    int input_device_id, int logical_input, std::uint32_t gameplay_frame,
    bool native_value, PressedQueryPurpose purpose) noexcept;
void AcceptGameplayPressed(
    int requested_input, int source_input, PressedQueryPurpose purpose,
    bool result, bool direction_aliases_enabled) noexcept;
EffectiveInputQuery QueryGameplayHeld(
    int input_device_id, int logical_input, std::uint32_t requested_frame,
    bool native_value) noexcept;
int QueryGameplayHeldAge(int input_device_id, int logical_input,
                         int native_value) noexcept;
int QueryGameplayDirection(int input_device_id, int booster,
                           std::uint32_t gameplay_frame,
                           int native_value) noexcept;
void CompleteGameplayDirectionMatch(bool accepted) noexcept;
int SelectGameplayLateGateTime(int recognition_ms) noexcept;
int SelectGameplayGradeArgument(int original_argument) noexcept;
void ActivateHighFpsInputBridge(
    std::uint32_t current_fastio,
    gc::input::GameplayInputStyle style,
    DirectionNormalizationTable direction_normalization) noexcept;
```

- [ ] **Step 1: Rewrite bridge tests around core transactions**

Replace every `CommitGameplayStepAtQpc` and frame-carry assertion. Cover:

- 60 FPS configure/activate/begin/query/end calls are handled false and leave counters at zero;
- invalid high-FPS QPC frequency fails configuration before hooks;
- high FPS remains `PendingHooks` until activation;
- activation seeds a held control without a rise;
- beginning a core transaction drains all currently published transitions into the retained timeline;
- another transition published after begin is invisible until the next transaction;
- free tap sees an edge once, and a note in a later transaction within 1 / 60 sees the same original event QPC;
- note consumption is committed only by `EndGameplayJudgement`;
- a consumed note edge is absent next transaction;
- an unconsumed edge expires by QPC, not by pass count, at 144, 165, and 240 FPS schedules;
- wrong input device, invalid logical input, missing transaction, and non-current frame return native/unhandled;
- focus loss, device disconnect, playback generation change, gameplay inactivity, and shutdown clear/reseed without a synthetic press;
- transport and retained-history overflow keep the bridge active, retain newest input, and increment eviction counters;
- a hook callback fault returns native for that call and increments a bounded anomaly counter without permanent `NativeFallback` mode;
- simultaneous directions keep one cohort and can satisfy paired/chord queries in one immutable transaction;
- non-gameplay FastIO changes produce no transition;
- formatted summary contains transitions, note associations, free-tap presentations, physical grades, history acceptances, rescues, expiries, duplicate-presentation suppression, and evictions;
- 10,000 already-configured begin/query/end cycles perform zero allocations;
- old fields `committed_frames`, `press_delivery_window_frames`, `carried_edges`, `step_zero_calls`, `missing_frame_fallbacks`, and `maximum_carry_frames` no longer exist.

- [ ] **Step 2: Build and verify RED**

```powershell
& $env:ComSpec /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build --preset msvc32-debug --target HighFpsInputBridgeTests && ctest --preset msvc32-debug -R "^HighFpsInputBridgeTests$" --output-on-failure'
```

Expected: compile failure against the old frame-commit bridge.

- [ ] **Step 3: Replace bridge state and configuration**

Reduce `HighFpsInputBridgeConfig` to target FPS, QPC frequency, and the existing playback-gating boolean. Remove maximum gameplay step and `GameplayInputTimeline`. Store:

```cpp
InputTransitionJournal transport_;
JudgementInputTimeline timeline_;
std::optional<JudgementInputTransaction> transaction_;
std::array<InputTransitionRecord, kTransitionCapacity> drain_buffer_;
```

The producer method builds a transition and calls `PushNewest`. `BeginGameplayJudgement` drains once, appends to the timeline, creates one transaction view, and refuses nesting by returning native/unhandled while counting an anomaly. `EndGameplayJudgement` finishes and commits uses exactly once.

- [ ] **Step 4: Preserve ordinary epoch behavior without fallback modes**

Activation, focus loss, device disconnect, playback-generation changes, one complete outer frame without a core call, and shutdown request a fresh epoch. The next active core call drains/discards stale transport records and seeds from `ReadPublishedInput`. Overflow only increments diagnostics. Remove `NativeFallback`, frame discontinuity handling, and old commit-hook runtime-fault behavior.

- [ ] **Step 5: Delegate every query and timing seam to the active transaction**

The bridge validates local device identity and current gameplay frame before delegating. At 60 FPS or without a valid scoped transaction it returns the native value unmodified. Store style once at activation and copy it into each transaction; do not read configuration from query hooks.

Keep diagnostics as counters in normal builds. Under `GC_ENABLE_INPUT_EDGE_DIAGNOSTICS`, queue at most one compact record per drained rise and per committed association; format or flush them outside the producer callback.

- [ ] **Step 6: Remove superseded frame mapper files and CMake entries**

Delete `GameplayInputTimeline` and its test. Remove all includes, configuration, summary fields, and CMake target/source references. Confirm:

```powershell
rg -n "GameplayInputTimeline|CommitGameplayInputStep|maximum_carry_frames|carried_edges|press_delivery_window_frames" src tests
```

Expected: no production or test reference remains.

- [ ] **Step 7: Build and run the high-FPS core tests**

```powershell
& $env:ComSpec /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build --preset msvc32-debug --target InputTransitionJournalTests JudgementInputTimelineTests JudgementInputTransactionTests HighFpsInputBridgeTests && ctest --preset msvc32-debug -R "^(InputTransitionJournalTests|JudgementInputTimelineTests|JudgementInputTransactionTests|HighFpsInputBridgeTests)$" --output-on-failure'
```

Expected: all four pass and no test asserts target-frame delivery.

- [ ] **Step 8: Commit**

```powershell
git add -- src/Input/HighFps src/Input/CMakeLists.txt tests/Input/HighFps tests/Input/CMakeLists.txt
git commit -m "Replace exact-frame input bridge"
```

### Task 5: Add the guarded judgement hook set

**Files:**
- Create: `src/Input/HighFps/HighFpsJudgementHooks.h`
- Create: `src/Input/HighFps/HighFpsJudgementHooks.cpp`
- Create: `tests/Input/HighFps/HighFpsJudgementHooksTests.cpp`
- Create: `docs/reverse-engineering/high-fps-input-judgement-hook-manifest.md`
- Modify: `src/Input/CMakeLists.txt`
- Modify: `tests/Input/CMakeLists.txt`
- Modify: `src/Input/Switch/GameplayInputHookTransaction.h:1-42`
- Modify: `tests/Input/Switch/GameplayInputHookTransactionTests.cpp`

**Interfaces:**
- Consumes: executable base, supported-binary bytes, bridge callbacks, and one outer `GameplayInputHookOperation`.
- Produces:

```cpp
enum class JudgementHookSite : std::uint8_t {
    None, Core, Dispatcher, DirectionMatcher,
    HeldAge, Direction, LateGate, Grade,
};

struct HighFpsJudgementHookOperationContext {
    std::uintptr_t executable_base{};
};

bool PreflightHighFpsJudgementHooks(void* context) noexcept;
bool InstallHighFpsJudgementHooks(void* context) noexcept;
void ResetHighFpsJudgementHooks(void* context) noexcept;
```

- [ ] **Step 1: Record the live IDA evidence before coding**

Write the manifest with:

- executable size/SHA-256 from Global Constraints;
- IDA target `H:\gc\game471.exe.i64` and date 2026-08-15;
- these hook entry RVAs and expected prefixes:

| Site | RVA | Expected prefix |
|---|---:|---|
| Core 0x5D68E0 | `0x1D68E0` | `55 8B EC 6A FF 68 31 A6 67 00 64 A1 00 00 00 00` |
| Dispatcher 0x5D5720 | `0x1D5720` | `55 8B EC 83 EC 10 89 4D F4 C6 45 FB 00` |
| Direction matcher 0x5D2E50 | `0x1D2E50` | `55 8B EC 6A FF 68 62 92 67 00 64 A1 00 00 00 00` |
| Held age 0x6594D0 | `0x2594D0` | `55 8B EC 83 EC 10 89 4D F0 C7 45 FC 00 00 00 00` |
| Direction 0x659390 | `0x259390` | `55 8B EC 83 EC 14 89 4D F0 8B 4D F0 E8 2F 7D DA FF` |
| Late gate 0x5D0BE0 | `0x1D0BE0` | `55 8B EC 83 EC 0C 89 4D F4 C6 45 FB 00` |
| Grade 0x5D0E00 | `0x1D0E00` | `55 8B EC 83 EC 4C 89 4D CC 8B 45 08 D9 80 B0` |

Also record note descriptor helper `0x43CC50 / RVA 0x3CC50` and its prefix `55 8B EC 83 EC 08 89 4D F8 C7 45 FC 00 00 00 00`. It receives the dispatcher’s `a2` in ECX and `a3/a4` on the stack; type is descriptor offset 0 and target degrees is offset 240.

Record the two read-only direction helpers used to mirror the native matcher:

| Helper | RVA | Expected prefix |
|---|---:|---|
| Normalize direction 0x5D2E00 | `0x1D2E00` | `55 8B EC 83 EC 08 89 4D FC E8 C2 E3 E2 FF` |
| Angle to direction 0x62E1D0 | `0x22E1D0` | `55 8B EC 83 EC 08 C7 45 FC 05 00 00 00 D9 EE` |

At activation call the validated normalize helper for raw codes `0..9` once
and pass the resulting table to `ActivateHighFpsInputBridge`. At dispatcher
entry call the validated angle helper for `target_degrees`,
`target_degrees + 20.0f`, and `target_degrees - 20.0f` and store those three
codes plus the descriptor address as `note_identity` in `NoteEvaluation`. No
helper call is needed for note types that do not use the direction matcher.

Record caller inventories:

- grade: `0x5D1F2A` normal and `0x5D34C5` flick;
- late gate: `0x5D1E41, 0x5D33F1, 0x5D369F, 0x5D3A5D, 0x5D3E23, 0x5D42BA`;
- pressed: normal `0x5D1EC0`, free tap `0x5D20E0/0x5D2176`, beat `0x5D3A26`, scratch `0x5D3D83/0x5D3DA6/0x5D3DC9/0x5D3DEC`, hold `0x5D4325`;
- held: direction matcher `0x5D2F93/0x5D303B` and hold `0x5D43B8`;
- held age `0x5D2FC8` and direction `0x5D316F`;
- `0x5D04F0` duration callers remain untouched.
- both grade callers store the helper result as grade data after input
  acceptance; neither uses the helper return to undo the handler's accepted-hit
  flag.
- dispatcher types 11 through 14 have no handler/input-query branch, confirmed
  against both `0x5D5720` and the whole-binary input-wrapper xrefs.

- [ ] **Step 2: Add independent signature, plan, and rollback tests**

Use the manifest’s captured prefixes as an exact supported-binary fixture. Assert every site passes, then flip one byte at each site and require the correct `JudgementHookSite` mismatch. Add a fake seven-operation install sequence that fails at each index and asserts reverse-order reset of every earlier successful operation. A second install call after success must return success without creating another hook; reset followed by install must create exactly one fresh set.

Increase `kGameplayInputHookOperationCapacity` from 4 to 8 because the inner high-FPS hook-set transaction has seven operations. Keep the existing invalid descriptor, preflight failure, capacity, commit, and rollback tests.

- [ ] **Step 3: Register/build and verify RED**

```powershell
& $env:ComSpec /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build --preset msvc32-debug --target HighFpsJudgementHooksTests GameplayInputHookTransactionTests && ctest --preset msvc32-debug -R "^(HighFpsJudgementHooksTests|GameplayInputHookTransactionTests)$" --output-on-failure'
```

Expected: compile failure because the hook manifest API and seven-site operation do not exist.

- [ ] **Step 4: Implement preflight and one internally transactional hook set**

Preflight every hook and helper prefix before creating any SafetyHook object. Install the seven hooks through `RunGameplayInputHookTransaction`. If any creation fails, reset all earlier hooks and report the precise site. The outer Switch transaction treats this entire set as one operation, so a later pressed/held/diagonal failure also resets it.

- [ ] **Step 5: Implement native wrappers with exact x86 conventions**

Implement:

- core inline wrapper `void __fastcall(void* self, void*, int recognition_ms, int gameplay_frame)`: query QPC, begin one bridge transaction, call original once, and end via a non-throwing scope guard;
- dispatcher inline wrapper matching `char __thiscall(void*, int a2, int a3, int a4, unsigned lane, int recognition_ms)`: resolve descriptor, compute type and target-direction set, scope `BeginGameplayNote/EndGameplayNote` around the original;
- direction-matcher inline wrapper: call original and then `CompleteGameplayDirectionMatch(result != 0)` while dispatcher note scope is still active;
- held-age and direction wrappers: call original first, then ask the bridge for a scoped override;
- late-gate stdcall wrapper: pass `SelectGameplayLateGateTime(a4)` to the original helper instead of `a4` only when a selected edge exists; this preserves `sub_43C820` side effects when event time is also late;
- grade thiscall wrapper: pass `SelectGameplayGradeArgument(a3)` to the original helper and return its result unchanged.

Do not hook or alter `0x5D04F0`. Do not change the core’s `a2` song time. Catch loader-side exceptions inside each wrapper and fall back to the original native call without letting an exception cross the hook.

- [ ] **Step 6: Add pure caller classification tests**

Expose constexpr classification for the pressed call-site RVAs after subtracting image base:

```cpp
PressedQueryPurposeForCaller(0x001D20E0) == PressedQueryPurpose::FreeTap
PressedQueryPurposeForCaller(0x001D2176) == PressedQueryPurpose::FreeTap
PressedQueryPurposeForCaller(0x001D1EC0) == PressedQueryPurpose::Note
PressedQueryPurposeForCaller(0xDEADBEEF) == PressedQueryPurpose::Unscoped
```

Unknown callers must remain native even when a core transaction exists.

- [ ] **Step 7: Run focused tests and reference checks**

```powershell
& $env:ComSpec /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build --preset msvc32-debug --target HighFpsJudgementHooksTests GameplayInputHookTransactionTests && ctest --preset msvc32-debug -R "^(HighFpsJudgementHooksTests|GameplayInputHookTransactionTests)$" --output-on-failure'
rg -n "5D04F0|CommitGameplayInputStep|kGameplayInputCommitRva" src/Input/HighFps src/Input/Switch
```

Expected: both tests pass; `5D04F0` appears only in documentation/asserted “untouched” evidence, and no old commit hook remains in the new hook module.

- [ ] **Step 8: Commit**

```powershell
git add -- src/Input/HighFps/HighFpsJudgementHooks.h src/Input/HighFps/HighFpsJudgementHooks.cpp src/Input/Switch/GameplayInputHookTransaction.h src/Input/CMakeLists.txt tests/Input/HighFps/HighFpsJudgementHooksTests.cpp tests/Input/Switch/GameplayInputHookTransactionTests.cpp tests/Input/CMakeLists.txt docs/reverse-engineering/high-fps-input-judgement-hook-manifest.md
git commit -m "Add guarded judgement transaction hooks"
```

### Task 6: Compose temporal correction with Arcade and Switch query hooks

**Files:**
- Modify: `src/Input/Switch/SwitchInputPatch.h:1-298`
- Modify: `src/Input/Switch/SwitchInputPatch.cpp:1-828`
- Modify: `tests/Input/Switch/SwitchInputPatchTests.cpp:1-330`
- Test: `tests/Input/Switch/SwitchInputPolicyTests.cpp`
- Test: `tests/Input/HighFps/HighFpsInputBridgeTests.cpp`

**Interfaces:**
- Consumes: the high-FPS hook set as one transaction operation, native pressed/held results, caller purpose, and existing Switch alias policy.
- Produces: one complete gameplay hook plan:

```cpp
struct SwitchHookPlan {
    bool high_fps_judgement{};
    bool pressed_edge{};
    bool held_state{};
    bool diagonal_match{};
};
```

- [ ] **Step 1: Rewrite hook-plan tests before production code**

Require the four operating modes:

| Target/style | High-FPS set | Pressed/held | Diagonal |
|---|---:|---:|---:|
| 60 Arcade, diagnostics off | off | off | off |
| 60 Switch | off | on | on |
| high-FPS Arcade | on | on | off |
| high-FPS Switch | on | on | on |

Replace every `gameplay_input_commit` expectation with `high_fps_judgement`. Assert a high-FPS plan is complete only when the high-FPS set, pressed, and held hooks all install. Assert each outer-transaction failure resets every earlier operation and leaves high-FPS bridge inactive.

- [ ] **Step 2: Add query-composition tests with a fake native callback**

Exercise `query_gameplay_with_aliases` through a public pure helper or equivalent injected callbacks. Require:

- native true remains true even if the bridge has no candidate;
- native false plus a pending edge becomes true;
- Arcade never queries direction aliases for button 4/9;
- Switch queries the real button first, then direction aliases in existing order;
- a corrected direction edge can satisfy a Switch button and reports that direction as source;
- a direction held before the transaction satisfies Switch held but does not synthesize pressed;
- free-tap caller purpose is passed to the bridge and note caller purpose is distinct;
- unknown callers bypass correction;
- all alias probes see one immutable transaction view;
- `AcceptGameplayPressed` is called once for the final requested/source result, not once per failed probe.

- [ ] **Step 3: Build and verify RED**

```powershell
& $env:ComSpec /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build --preset msvc32-debug --target SwitchInputPatchTests && ctest --preset msvc32-debug -R "^SwitchInputPatchTests$" --output-on-failure'
```

Expected: compile/assertion failure because the plan still owns the frame-commit hook and queries the old bridge before native input.

- [ ] **Step 4: Remove the old commit hook and state reader**

Delete:

- `kGameplayInputCommitRva` and signature;
- `GameplayInputCommitState`, stack offsets, and `TryReadGameplayInputCommitState`;
- `g_gameplay_input_commit_hook` and `hook_gameplay_input_commit`;
- commit-hook preflight/install/reset/logging;
- `ReportCommitHookRuntimeFault` and `CommitGameplayInputStep` calls.

Add one outer transaction operation whose callbacks are `PreflightHighFpsJudgementHooks`, `InstallHighFpsJudgementHooks`, and `ResetHighFpsJudgementHooks`. After the outer transaction commits, obtain the validated direction-normalization table and pass it with the configured style into `ActivateHighFpsInputBridge`.

- [ ] **Step 5: Change pressed/held order to native-first, non-shrinking composition**

For each source probe:

```cpp
const auto native = query_original(context, logical_input);
const auto effective = context->pressed_query
    ? QueryGameplayPressed(device, logical_input, frame, native != 0, purpose)
    : QueryGameplayHeld(device, logical_input, frame, native != 0);
return effective.handled ? static_cast<std::uint8_t>(effective.value) : native;
```

After the existing alias helper chooses the requested/source pair, call `AcceptGameplayPressed` once. Leave `TryApplySwitchDiagonalMatch` unchanged; its mid-hook still broadens only a failed native diagonal comparison.

- [ ] **Step 6: Activate only after the entire outer hook transaction commits**

At high FPS, a signature mismatch or any inner/outer install failure fails closed before bridge activation and causes DLL attach failure as today. At 60 FPS Arcade with diagnostics disabled, install no correction hook. At 60 FPS Switch, activate only Switch pressed/held/diagonal behavior and never activate the high-FPS bridge.

- [ ] **Step 7: Run the combined input tests**

```powershell
& $env:ComSpec /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build --preset msvc32-debug --target SwitchInputPolicyTests SwitchInputPatchTests GameplayInputHookTransactionTests HighFpsJudgementHooksTests HighFpsInputBridgeTests && ctest --preset msvc32-debug -R "^(SwitchInputPolicyTests|SwitchInputPatchTests|GameplayInputHookTransactionTests|HighFpsJudgementHooksTests|HighFpsInputBridgeTests)$" --output-on-failure'
```

Expected: all pass, including 60 no-op, high-FPS Arcade, high-FPS Switch, transactional rollback, free-tap purpose, and native-success preservation.

- [ ] **Step 8: Commit**

```powershell
git add -- src/Input/Switch/SwitchInputPatch.h src/Input/Switch/SwitchInputPatch.cpp tests/Input/Switch/SwitchInputPatchTests.cpp
git commit -m "Compose judgement timing with Switch input"
```

### Task 7: Simplify framerate lifecycle and add bounded diagnostics

**Files:**
- Modify: `src/Patches/Framerate/FrameratePatch.h:1-28`
- Modify: `src/Patches/Framerate/FrameratePatch.cpp:38-52, 1554-1587, 1828-1850, 2028-2034, 2182-2200`
- Modify: `tests/Patches/Framerate/FramerateRuntimeTests.cpp:175-205`
- Modify: `src/Input/Polling/InputPollingRuntime.cpp:45-70, 370-385, 610-670, 875-890`
- Modify: `src/Input/HighFps/HighFpsInputBridge.h`
- Modify: `src/Input/HighFps/HighFpsInputBridge.cpp`
- Modify: `tests/Input/HighFps/HighFpsInputBridgeTests.cpp`

**Interfaces:**
- Consumes: target FPS, QPC frequency, playback activity/generation, outer epoch, input reset events.
- Produces: bridge activation line, one end-of-epoch/song summary, optional compact diagnostic rise/association/miss lines, and rate-limited anomalies.

- [ ] **Step 1: Change framerate configuration tests**

Replace audio-clock-dependent maximum-step expectations with:

```cpp
const auto config =
    gc::framerate::detail::BuildHighFpsInputBridgeConfig(
        240, 10'000'000, true);
failures += Expect(
    config.target_fps == 240 &&
        config.qpc_frequency == 10'000'000 &&
        config.gate_on_playback_activity,
    "framerate supplies only timing and lifecycle bridge inputs");
```

Add a 60 FPS case and require its bridge mode to stay disabled. Remove assertions about `maximum_step`.

- [ ] **Step 2: Add bounded diagnostic tests**

Use a fake log sink or bridge formatter capture. Require:

- activation produces one line containing target FPS, style, capacity, and hook status;
- one physical rise produces at most one diagnostic rise record;
- one committed note association and one free-tap presentation each produce one association record;
- repeated queries in the same transaction do not duplicate an association;
- a failed handler with a recent relevant edge produces one miss-context record for that note/edge pair;
- ordinary empty frames produce zero records;
- epoch end produces one summary with the required counters;
- eviction produces one cumulative count and a rate-limited anomaly, not one line per dropped record;
- 60 FPS observation lines report native/passive status and never report correction.

- [ ] **Step 3: Build and verify RED**

```powershell
& $env:ComSpec /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build --preset msvc32-debug --target FramerateRuntimeTests HighFpsInputBridgeTests && ctest --preset msvc32-debug -R "^(FramerateRuntimeTests|HighFpsInputBridgeTests)$" --output-on-failure'
```

Expected: compile/assertion failure because framerate still supplies gameplay-step configuration and the old summary fields.

- [ ] **Step 4: Remove frame-step coupling**

Change `BuildHighFpsInputBridgeConfig` to accept only target FPS, QPC frequency, and playback-gating selection. Keep `ObserveGameplayPlayback` calls because playback generation defines epochs. Keep `ObserveGameplayOuterFrame` because one outer frame without a judgement-core call marks gameplay inactive. Remove all bridge dependence on shared-song-clock `maximum_step`.

Update polling reset calls to the reduced `InputEpochResetReason` set: activation, focus loss, device disconnect, gameplay inactive, playback generation, and shutdown. Overflow and ordinary hook callback errors are diagnostics, not epoch/fallback modes.

- [ ] **Step 5: Emit diagnostics away from per-query hot paths**

Drain diagnostic rise records when a core transaction begins, and commit association records when it ends. Under `GC_ENABLE_INPUT_EDGE_DIAGNOSTICS`, use a fixed 256-entry newest-wins diagnostic ring and format one compact line per stored human input/association. Count overwritten diagnostic entries in the epoch summary. Normal builds retain only counters and end-of-song summary. Every logging call is wrapped so formatting/log exceptions cannot cross a hook.

Miss lines include note type, lane/component, selected physical/effective source, Arcade/Switch mode, reason, event-recognition delta, grade delta, handler result, and recent retained relevant edge sequences. Do not log every native miss pass.

- [ ] **Step 6: Run focused and integration tests**

```powershell
& $env:ComSpec /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build --preset msvc32-debug --target FramerateRuntimeTests InputPollingRuntimeStartupTests HighFpsInputBridgeTests SwitchInputPatchTests && ctest --preset msvc32-debug -R "^(FramerateRuntimeTests|InputPollingRuntimeStartupTests|HighFpsInputBridgeTests|SwitchInputPatchTests)$" --output-on-failure'
```

Expected: all pass; no target-frame bridge API remains.

- [ ] **Step 7: Run a superseded-symbol scan**

```powershell
rg -n "GameplayInputTimeline|CommitGameplayInputStep|gameplay_input_commit|press_delivery_window_frames|carried_edges|maximum_carry_frames|step_zero_calls|missing_frame_fallbacks|NativeFallback" src tests
```

Expected: zero hits except explicit absence assertions in tests or historical documentation outside `src`/`tests`. Remove the absence assertion once the scan itself proves the source tree clean.

- [ ] **Step 8: Commit**

```powershell
git add -- src/Patches/Framerate/FrameratePatch.h src/Patches/Framerate/FrameratePatch.cpp tests/Patches/Framerate/FramerateRuntimeTests.cpp src/Input/Polling/InputPollingRuntime.cpp src/Input/HighFps/HighFpsInputBridge.h src/Input/HighFps/HighFpsInputBridge.cpp tests/Input/HighFps/HighFpsInputBridgeTests.cpp
git commit -m "Integrate judgement transactions with runtime lifecycle"
```

### Task 8: Verify, deploy the DLL, and hand off 240 FPS acceptance

**Files:**
- Verify: all files changed by Tasks 1-7
- Deploy: `build-msvc32-release/dist/iDmacDrv32.dll` to `H:\gc\iDmacDrv32.dll`
- Inspect after user run: `H:\gc\loader-log.txt`

**Interfaces:**
- Consumes: committed implementation, both complete x86 preset graphs, supported executable, and the user’s runtime run.
- Produces: one hash-verified deployed DLL without a backup copy, followed by evidence-bounded 240 FPS acceptance.

- [ ] **Step 1: Run source and design-contract checks**

```powershell
git diff --check
git status --short
rg -n "GameplayInputTimeline|CommitGameplayInputStep|gameplay_input_commit|carried_edges|NativeFallback" src tests
rg -n "HoldSafeFrame|SlideHoldSafeFrame|ScratchEnableTime|BeatEnableTime" src/Input/HighFps src/Input/Switch
```

Expected: diff check is clean; superseded production symbols are absent; the
high-FPS/Switch implementation contains no read, scaling, or patching of the
four static configuration values; status contains only intended implementation
changes or is clean after task commits.

- [ ] **Step 2: Build and test the complete Debug graph**

```powershell
$env:GC_ASIO_SDK_DIR='H:\gc\artifacts\ASIOSDK'
& $env:ComSpec /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --preset msvc32-debug && cmake --build --preset msvc32-debug && ctest --preset msvc32-debug -j 4 --output-on-failure'
```

Expected: full Debug build succeeds and every CTest test passes.

- [ ] **Step 3: Build and test the complete Release graph**

```powershell
& $env:ComSpec /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --preset msvc32-release && cmake --build --preset msvc32-release && ctest --preset msvc32-release -j 4 --output-on-failure'
```

Expected: full RelWithDebInfo build succeeds and every CTest test passes.

- [ ] **Step 4: Inspect the Release PE and hook evidence**

```powershell
& $env:ComSpec /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && dumpbin /headers build-msvc32-release\dist\iDmacDrv32.dll'
git log --oneline --decorate -8
git status --short --branch
```

Expected: machine is x86 (`14C`), the intended task commits are present, and the worktree is clean. Re-run `AgentSession.connect` signature queries against the existing daemon and compare every hook/helper prefix to the checked manifest before deployment.

- [ ] **Step 5: Deploy only the verified DLL, with no backup**

Resolve the exact source and destination, copy once, and verify hashes:

```powershell
$source = (Resolve-Path 'build-msvc32-release\dist\iDmacDrv32.dll').Path
$destination = (Join-Path (Resolve-Path 'H:\gc').Path 'iDmacDrv32.dll')
Copy-Item -LiteralPath $source -Destination $destination -Force
$sourceHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $source).Hash
$deployedHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $destination).Hash
if ($sourceHash -ne $deployedHash) { throw 'Deployed DLL hash mismatch' }
```

Expected: source and deployed SHA-256 values match. Do not create or retain a backup DLL.

- [ ] **Step 6: Hand off the 240 FPS run**

Tell the user exactly which Release commit/hash was deployed and ask them to run 240 FPS first. Do not claim the input drop or judgement feel is fixed from static evidence.

After the user reports the run:

```powershell
Get-Item 'H:\gc\loader-log.txt' | Select-Object FullName,Length,LastWriteTime
Get-Content -Tail 500 'H:\gc\loader-log.txt'
```

Require activation to show the complete hook set and high-FPS transaction mode, then correlate physical rises, free-tap presentation, note association, event/recognition deltas, grade deltas, expiries, misses, and evictions.

- [ ] **Step 7: Apply the runtime acceptance gate**

If the user reports any dropped input, duplicate selectable hit sound, unstable or shifted grade, hold/repeat regression, hook anomaly, or normal-play eviction, stop and investigate that 240 FPS evidence. Do not ask for or interpret the 60 FPS comparison yet.

Only after the user accepts 240 FPS should they run the same chart/input material at 60 FPS. The 60 FPS log must show passive/no-op correction while the selected Arcade or Switch style remains correct.

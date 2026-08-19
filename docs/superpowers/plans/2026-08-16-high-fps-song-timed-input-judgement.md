> **ARCHIVED FAILED ATTEMPT — DO NOT EXECUTE.** Its loader-side composition
> model was superseded and ultimately failed runtime acceptance.

# High-FPS Song-Timed Input Judgement Implementation Plan

> **For the implementing agent:** REQUIRED SUB-SKILL: Use superpowers:executing-plans. Execute this plan inline in the current worktree; do not dispatch subagents.

**Goal:** Replace the rejected frame/QPC-lifetime input bridge with a song-timed, one-sample correction that prevents short-input loss at high FPS while preserving native judgement, Switch compatibility, and 60 FPS behavior.

**Architecture:** The polling producer keeps a fixed transition journal. The existing game-thread song-clock seam publishes exact or rounded QPC-associated anchors. A fixed song-timed timeline maps transitions into the same domain as each native recognition time R and freezes every ready transition into one immutable sample. Scoped query composition observes that sample without ownership or consumption, while narrow late-gate, grade, and native free-tap-branch hooks apply only the selected edge's T - R delta or routing decision.

**Tech stack:** C++23, Windows x86, CMake/Ninja presets, SafetyHook inline and mid hooks, existing plog diagnostics, standalone CTest executables.

**Authoritative design:** [High-FPS Song-Timed Input Judgement Bridge](../specs/2026-08-16-high-fps-song-timed-input-judgement-design.md)

---

## Execution Boundary

Work only in:

    H:\gc\artifacts\GCLoader\.worktrees\asio-audio-backend

The runtime tree H:\gc may be read for the supported executable, IDB, and logs. Do not copy, replace, rename, back up, or otherwise deploy a DLL in this plan. Runtime deployment and cabinet acceptance require a later explicit request.

Keep these rules locked throughout execution:

- Target FPS 60 installs no song-timed correction, free-tap correction, history reconstruction, late-gate adjustment, or grade adjustment. The independently selected Switch patch keeps its existing 60 FPS behavior.
- There is one logical current note and one current note type. Native left/right evaluations are booster components, not lanes or concurrent notes.
- Preserve the current Switch button-edge, held-button, exact-direction, and adjacent-cardinal rules.
- Do not patch or synthesize composite logical input IDs 10 through 19.
- JudgTimeOffset, GameTimeOffset, HoldSafeFrame, SlideHoldSafeFrame, ScratchEnableTime, and BeatEnableTime are not bridge inputs. Preserve the existing game/audio-clock code that already owns its static values; add no reads, watchers, caching, or controls.
- Do not change the DirectSound, WASAPI, or ASIO callback path. Anchor publication occurs only on the existing game-thread gameplay song-clock seam.
- Keep transition storage and diagnostics fixed-capacity and allocation-free on hot paths. On overflow, keep the newest record and evict the oldest.
- Do not use dispatcher return values, query order, or free-tap execution as input-consumption signals.
- Do not reset because a rendered frame has zero judgement calls, because a song-clock step is zero, or because no input query occurs in a core call.
- Initialize replacement state only through ordinary framerate/Switch patch startup after the process runtime is ready. Add no DllMain work, pre-CRT constructor dependency, audio-callback work, or cross-thread native judgement call.
- Every executable site keeps a named RVA, readable-memory check, expected original bytes, transactional installation, and reverse-order rollback.
- Exceptions must not cross native hook boundaries.

## Verified Binary Contracts Used by This Plan

The existing live IDA-CLI daemon for H:\gc\game471.exe.i64 was rechecked read-only while writing this plan.

- The new free-tap branch hook starts at VA 0x5D76CE, RVA 0x1D76CE.
- Its guarded prefix is:

      0F B6 85 57 FF FF FF 83 F8 01 75 0F

- The native permission byte is at EBP - 0xA9. Setting that byte from zero to one lets the existing instructions reach the native call at 0x5D76E4 to function 0x5D2040. The hook must never call 0x5D2040 itself.
- The descriptor returned by 0x43CC50 stores note type at +0, the native mute predicate field at +8, earliest-eligible milliseconds at +152, and target degrees at +240.
- The existing core, dispatcher, direction matcher, held-age, direction, late-gate, grade, descriptor, normalization, and angle signatures in HighFpsJudgementHooks.h remain required.

Record these facts in the reverse-engineering manifest when the hook is implemented. Reconfirm them through AgentSession.start(target, daemon=True, require_ida=True) immediately before changing the production hook table.

## Final Source Layout

### Create

- **src/Input/HighFps/SongTime.h**
- **src/Input/HighFps/SongTime.cpp**
  - Exact rational song-time value, anchor state, QPC mapping, comparison, and ABI rounding.
- **src/Input/HighFps/SongTimedInputTimeline.h**
- **src/Input/HighFps/SongTimedInputTimeline.cpp**
  - Fixed pending transition timeline, immutable sample construction, and authored-60 history.
- **src/Input/HighFps/JudgementInputSample.h**
- **src/Input/HighFps/JudgementInputSample.cpp**
  - Current-note/free-tap routing, pure query observations, note-family policy, Switch-compatible selection, and late/grade edge selection.
- **tests/Input/HighFps/SongTimeTests.cpp**
- **tests/Input/HighFps/SongTimedInputTimelineTests.cpp**
- **tests/Input/HighFps/JudgementInputSampleTests.cpp**

### Delete

- **src/Input/HighFps/JudgementInputTimeline.h**
- **src/Input/HighFps/JudgementInputTimeline.cpp**
- **src/Input/HighFps/JudgementInputTransaction.h**
- **src/Input/HighFps/JudgementInputTransaction.cpp**
- **tests/Input/HighFps/JudgementInputTimelineTests.cpp**
- **tests/Input/HighFps/JudgementInputTransactionTests.cpp**

These files implement the superseded 1/60 pending lifetime, per-note/free-tap availability, query-driven commit, and transaction ownership. Do not retain compatibility adapters around them.

### Modify

- **src/Input/HighFps/InputTransitionJournal.h**
- **src/Input/HighFps/InputTransitionJournal.cpp**
- **src/Input/HighFps/HighFpsInputBridge.h**
- **src/Input/HighFps/HighFpsInputBridge.cpp**
- **src/Input/HighFps/HighFpsJudgementHooks.h**
- **src/Input/HighFps/HighFpsJudgementHooks.cpp**
- **src/Input/Switch/SwitchInputPatch.h**
- **src/Input/Switch/SwitchInputPatch.cpp**
- **src/Patches/Framerate/FrameratePatch.h**
- **src/Patches/Framerate/FrameratePatch.cpp**
- **src/Input/CMakeLists.txt**
- **tests/Input/CMakeLists.txt**
- **tests/Input/HighFps/InputTransitionJournalTests.cpp**
- **tests/Input/HighFps/HighFpsInputBridgeTests.cpp**
- **tests/Input/HighFps/HighFpsJudgementHooksTests.cpp**
- **tests/Input/Switch/SwitchInputPatchTests.cpp**
- **tests/Patches/Framerate/FramerateRuntimeTests.cpp**
- **docs/reverse-engineering/high-fps-input-judgement-hook-manifest.md**
- **docs/reverse-engineering/high-fps-input-judgement-decisions.md**

GameplaySongClock.cpp and GameplaySongClockTests.cpp should remain unchanged unless a failing integration test proves that the existing step-selection contract itself must change. The input bridge consumes its observation; it does not redesign the audio step clock.

---

### Task 1: Remove the rejected transaction and lifetime implementation

**Files:**

- Create: **src/Input/HighFps/JudgementInputSample.h**
- Delete: the four old JudgementInputTimeline/JudgementInputTransaction production files
- Delete: the two matching old test files
- Modify: **src/Input/HighFps/HighFpsInputBridge.h**
- Modify: **src/Input/HighFps/HighFpsInputBridge.cpp**
- Modify: **src/Input/HighFps/HighFpsJudgementHooks.h**
- Modify: **src/Input/HighFps/HighFpsJudgementHooks.cpp**
- Modify: **src/Input/Switch/SwitchInputPatch.h**
- Modify: **src/Input/Switch/SwitchInputPatch.cpp**
- Modify: **src/Input/CMakeLists.txt**
- Modify: **tests/Input/CMakeLists.txt**
- Modify: **tests/Input/HighFps/HighFpsInputBridgeTests.cpp**

**Step 1: Add a regression that rejects the zero-step reset**

Retain ObserveGameplayOuterFrame temporarily as a passive observation seam and add a focused bridge test:

~~~cpp
HighFpsInputBridge bridge;
failures += Expect(bridge.Configure(Config()).has_value(), "configure");
Activate(bridge);
const auto before = bridge.TakeSnapshot();
bridge.ObserveGameplayOuterFrame(1, 100);
bridge.ObserveGameplayOuterFrame(2, 200);
const auto after = bridge.TakeSnapshot();
failures += Expect(
    after.reset_total == before.reset_total,
    "an outer frame without judgement does not reset input");
~~~

Run:

~~~powershell
cmake --build --preset msvc32-debug --target HighFpsInputBridgeTests
ctest --preset msvc32-debug -R "^HighFpsInputBridgeTests$"
~~~

Expected: the new assertion fails because the current bridge reports GameplayInactive after the first outer frame with no core call.

**Step 2: Move only neutral shared types, then delete the old policy**

In JudgementInputSample.h, initially define only the neutral declarations needed by the bridge and hook headers:

~~~cpp
enum class GameplayNoteType : std::uint8_t {
    None = 0,
    Normal = 1,
    Flick = 2,
    Hold = 3,
    Scratch = 4,
    Beat = 5,
    MerryGoRound = 6,
    Hidden = 7,
    Hidden2 = 8,
    Critical = 9,
    SlideHold = 10,
    SlideCounter = 11,
    Turn = 12,
    Spin = 13,
    Finish = 14,
    DualHold = 15,
};

enum class PressedQueryPurpose : std::uint8_t {
    Unscoped,
    CurrentNote,
    FreeTap,
};

enum class InputMatchReason : std::uint8_t {
    Native,
    Edge,
    Held,
    AuthoredHistory,
};

struct EffectiveInputQuery {
    bool handled{};
    bool value{};
    InputMatchReason reason{InputMatchReason::Native};
};

using DirectionNormalizationTable = std::array<int, 10>;
~~~

Use CurrentNote rather than Note. Do not carry NoteInputKind values named PairedButton or PairedHold into the replacement.

Delete the old timeline, transaction, commit structures, Finish calls, expiry counters, consumption arrays, query-order direction arming, and transaction tests. Update CMake and includes in one change.

**Step 3: Leave a compiling native-fallback bridge**

Keep configuration, activation, physical capture, fixed transport, epoch requests, and bounded diagnostics. Until Tasks 2 through 5 add the replacement, make BeginJudgement return false and make every query return the native value with handled false.

ObserveGameplayOuterFrame may retain the latest outer epoch for passive diagnostics, but must not call StartNewEpoch. EndJudgement and current-note callbacks become harmless no-ops in this intermediate commit.

Do not remove the installed hook transaction yet; its wrappers continue forwarding exactly one native call.

**Step 4: Replace obsolete tests with the safe intermediate contract**

HighFpsInputBridgeTests must cover:

- 60 FPS remains disabled even with an invalid QPC frequency;
- high FPS validates QPC and reaches PendingHooks then Active;
- the active native-fallback scope never changes a native true or false;
- transition overflow keeps the newest state and increments eviction count;
- focus, device, gameplay inactive, generation, and shutdown requests remain explicit reset reasons;
- ordinary outer frames never reset; and
- activation/capture/fallback perform no heap allocation under the existing allocation probe.

Run:

~~~powershell
cmake --build --preset msvc32-debug --target InputTransitionJournalTests HighFpsInputBridgeTests HighFpsJudgementHooksTests SwitchInputPatchTests
ctest --preset msvc32-debug -R "^(InputTransitionJournalTests|HighFpsInputBridgeTests|HighFpsJudgementHooksTests|SwitchInputPatchTests)$"
~~~

Expected: all selected tests pass and no old JudgementInputTimelineTests or JudgementInputTransactionTests target remains.

**Step 5: Commit**

~~~powershell
git add -- src/Input/HighFps/JudgementInputSample.h src/Input/HighFps/JudgementInputTimeline.h src/Input/HighFps/JudgementInputTimeline.cpp src/Input/HighFps/JudgementInputTransaction.h src/Input/HighFps/JudgementInputTransaction.cpp src/Input/HighFps/HighFpsInputBridge.h src/Input/HighFps/HighFpsInputBridge.cpp src/Input/HighFps/HighFpsJudgementHooks.h src/Input/HighFps/HighFpsJudgementHooks.cpp src/Input/Switch/SwitchInputPatch.h src/Input/Switch/SwitchInputPatch.cpp src/Input/CMakeLists.txt tests/Input/HighFps/JudgementInputTimelineTests.cpp tests/Input/HighFps/JudgementInputTransactionTests.cpp tests/Input/HighFps/HighFpsInputBridgeTests.cpp tests/Input/HighFps/HighFpsJudgementHooksTests.cpp tests/Input/Switch/SwitchInputPatchTests.cpp tests/Input/CMakeLists.txt
git commit -m "Remove superseded high-FPS input transactions"
~~~

---

### Task 2: Add exact song-time and anchor arithmetic

**Files:**

- Create: **src/Input/HighFps/SongTime.h**
- Create: **src/Input/HighFps/SongTime.cpp**
- Create: **tests/Input/HighFps/SongTimeTests.cpp**
- Modify: **src/Input/CMakeLists.txt**
- Modify: **tests/Input/CMakeLists.txt**

**Step 1: Write independent arithmetic tests**

Tests must construct anchors from known values and derive expected answers directly:

- exact 44,100 Hz and 48,000 Hz source-frame anchors;
- rounded millisecond anchors;
- positive and negative QPC deltas;
- equality and one-fraction-before/after comparisons against integer recognition milliseconds;
- half-millisecond ties rounded symmetrically away from zero at the native ABI;
- finite native float millisecond boundaries converted without target-frame rounding;
- invalid rate, QPC frequency, inactive/failed state, overflow, and invalid float rejection; and
- identical event placement when recognition ticks are generated at 144, 165, and 240 FPS.

The non-divisor-rate test must compare each mapped physical time to independently computed rational tick boundaries. It must not call production target-FPS conversion to generate its expected values.

Representative boundary case:

~~~cpp
const SongTimeAnchor anchor{
    .state = SongTimeAnchorState::Exact,
    .qpc = 10'000'000,
    .position = 48'000,
    .source_sample_rate = 48'000,
    .playback_generation = 7,
};
const auto event = MapQpcToSongTime(anchor, 10'005'000, 10'000'000);
failures += Expect(
    event && CompareSongTime(*event, 1'000) == std::strong_ordering::greater,
    "an event at 1000.5 ms is later than recognition 1000 ms");
failures += Expect(
    event && RoundSongTimeDeltaMilliseconds(*event, 1'000) == 1,
    "positive half-millisecond rounds away from zero");
~~~

Run the new target. Expected: configure or compile fails because SongTime does not exist.

**Step 2: Implement the exact value and mapping contract**

Use this public shape:

~~~cpp
enum class SongTimeAnchorState : std::uint8_t {
    Exact,
    Rounded,
    Inactive,
    Failed,
};

struct SongTimeAnchor {
    SongTimeAnchorState state{SongTimeAnchorState::Failed};
    std::int64_t qpc{};
    std::uint64_t position{};
    std::uint32_t source_sample_rate{};
    std::optional<std::uint64_t> playback_generation{};
};

struct SongTime {
    std::int64_t whole_milliseconds{};
    std::uint64_t fraction_numerator{};
    std::uint64_t fraction_denominator{1};
};

enum class SongTimeError : std::uint8_t {
    InvalidAnchor,
    InvalidFrequency,
    InvalidNativeMilliseconds,
    ArithmeticOverflow,
};

[[nodiscard]] std::expected<SongTime, SongTimeError> MapQpcToSongTime(
    const SongTimeAnchor& anchor,
    std::int64_t event_qpc,
    std::int64_t qpc_frequency) noexcept;
[[nodiscard]] std::expected<SongTime, SongTimeError>
SongTimeFromNativeMilliseconds(float value) noexcept;
[[nodiscard]] std::strong_ordering CompareSongTime(
    const SongTime& left,
    const SongTime& right) noexcept;
[[nodiscard]] std::strong_ordering CompareSongTime(
    const SongTime& left,
    int integer_milliseconds) noexcept;
[[nodiscard]] int RoundSongTimeDeltaMilliseconds(
    const SongTime& event,
    int recognition_milliseconds) noexcept;
~~~

Represent every SongTime canonically with a nonnegative proper fraction. Convert source frames and signed QPC deltas with quotient/remainder decomposition. Reduce denominators with gcd before checked products and additions. If the real denominators cannot be combined without overflow, return ArithmeticOverflow; do not switch to floating point.

Decode the finite IEEE-754 descriptor float into an exact binary rational or reject it. Do not multiply by target FPS anywhere in this module.

**Step 3: Run the focused tests**

~~~powershell
cmake --build --preset msvc32-debug --target SongTimeTests
ctest --preset msvc32-debug -R "^SongTimeTests$"
~~~

Expected: all SongTime tests pass.

**Step 4: Commit**

~~~powershell
git add -- src/Input/HighFps/SongTime.h src/Input/HighFps/SongTime.cpp tests/Input/HighFps/SongTimeTests.cpp src/Input/CMakeLists.txt tests/Input/CMakeLists.txt
git commit -m "Add exact gameplay song-time mapping"
~~~

---

### Task 3: Build physical transition records and immutable ready samples

**Files:**

- Modify: **src/Input/HighFps/InputTransitionJournal.h**
- Modify: **src/Input/HighFps/InputTransitionJournal.cpp**
- Create: **src/Input/HighFps/SongTimedInputTimeline.h**
- Create: **src/Input/HighFps/SongTimedInputTimeline.cpp**
- Modify: **tests/Input/HighFps/InputTransitionJournalTests.cpp**
- Create: **tests/Input/HighFps/SongTimedInputTimelineTests.cpp**
- Modify: **src/Input/CMakeLists.txt**
- Modify: **tests/Input/CMakeLists.txt**

**Step 1: Update transport tests for complete physical publications**

Change InputTransitionRecord to this contract:

~~~cpp
struct InputTransitionRecord {
    std::uint64_t sequence{};
    std::uint64_t cohort{};
    std::int64_t qpc{};
    std::uint64_t epoch{};
    GameplayInputMask held_before{};
    GameplayInputMask held_after{};
    GameplayInputMask rising{};
    GameplayInputMask falling{};
};
~~~

Update BuildTransitionRecord to receive both sequence and cohort. Assert that one FastIO publication gives all changed controls the same cohort and QPC, retains complete pre/post masks, and derives both edges. Rename test language from simultaneous/chord to input cohort.

Keep the existing producer/consumer stress test and newest-wins overflow oracle.

**Step 2: Write timeline tests before its implementation**

Use exact SongTime anchors and cover:

1. A rise captured during a zero-step rendered frame stays pending and appears once at the first R where T <= R.
2. A future event is hidden from each older catch-up R.
3. A rise and fall between core calls yields pressed plus forced held in one sample, then released held state in the following sample.
4. Different controls in one cohort retain the same T and are all visible.
5. Repeated reads of a sample do not alter masks or edges.
6. Multiple rises of the same control coalesce to the newest edge only; another control does not coalesce.
7. Zero, one, and multiple catch-up ticks preserve first-eligible delivery.
8. A reset seeds a held control as pre-held without a rising pulse.
9. Authored history returns current held, state exactly two rational 60 Hz samples ago, and held ages at the inclusive <= 1 and <= 4 boundaries.
10. Pending/history overflow evicts oldest, retains newest post-state, and continues.
11. A different epoch is rejected, and generation change requires a reset.

The test helper should build mapped transitions from explicit QPC, anchor, and expected T. Do not copy an internal production ring layout.

**Step 3: Implement the fixed timeline and authored history**

Expose immutable values:

~~~cpp
struct SongTimedInputEdge {
    std::uint64_t sequence{};
    std::uint64_t cohort{};
    std::uint8_t source_input{};
    SongTime time{};
};

struct JudgementInputSample {
    std::uint64_t epoch{};
    std::optional<std::uint64_t> playback_generation{};
    int recognition_milliseconds{};
    std::uint32_t gameplay_frame{};
    GameplayInputMask pressed{};
    GameplayInputMask held{};
    GameplayInputMask held_two_authored_samples_ago{};
    std::array<std::uint8_t, gc::input::kGameplayLogicalInputCount>
        held_age_authored60{};
    std::array<std::optional<SongTimedInputEdge>,
               gc::input::kGameplayLogicalInputCount>
        newest_rises{};
};

struct SongTimedTimelineDiagnostics {
    std::uint64_t mapped{};
    std::uint64_t deferred{};
    std::uint64_t delivered{};
    std::uint64_t coalesced{};
    std::uint64_t oldest_evictions{};
    std::uint64_t ignored_epoch_records{};
    std::uint64_t invariant_anomalies{};
};
~~~

SongTimedInputTimeline must:

- map drained physical records through one valid anchor and retain sequence order;
- keep records with T > R pending;
- move every T <= R into exactly one newly built sample;
- OR distinct rising controls;
- use the last post-transition state as ordinary held;
- OR a ready rise back into held only when that control also fell before the sample ended;
- retain the newest edge per control, counting older same-control rises as coalesced;
- update a small fixed authored-60 state ring at exact n / 60-second boundaries;
- seed pre-held controls with held age greater than four; and
- perform no heap allocation.

Every ready pulse ends with the sample. There is no CommitUses, Finish, note-consumed state, free-tap-presented state, expiry, or replay.

**Step 4: Run focused transport and timeline tests**

~~~powershell
cmake --build --preset msvc32-debug --target InputTransitionJournalTests SongTimedInputTimelineTests
ctest --preset msvc32-debug -R "^(InputTransitionJournalTests|SongTimedInputTimelineTests)$"
~~~

Expected: both targets pass, including 144/165/240-independent authored-history cases.

**Step 5: Commit**

~~~powershell
git add -- src/Input/HighFps/InputTransitionJournal.h src/Input/HighFps/InputTransitionJournal.cpp src/Input/HighFps/SongTimedInputTimeline.h src/Input/HighFps/SongTimedInputTimeline.cpp tests/Input/HighFps/InputTransitionJournalTests.cpp tests/Input/HighFps/SongTimedInputTimelineTests.cpp src/Input/CMakeLists.txt tests/Input/CMakeLists.txt
git commit -m "Build song-timed judgement samples"
~~~

---

### Task 4: Implement current-note routing and pure sample observations

**Files:**

- Modify: **src/Input/HighFps/JudgementInputSample.h**
- Create: **src/Input/HighFps/JudgementInputSample.cpp**
- Create: **tests/Input/HighFps/JudgementInputSampleTests.cpp**
- Modify: **src/Input/CMakeLists.txt**
- Modify: **tests/Input/CMakeLists.txt**

**Step 1: Write routing and idempotence tests**

Build JudgementInputSample values directly and verify:

- no current note routes every pulse to free tap;
- muted current note routes every pulse to free tap;
- T equal to or before earliest eligibility routes only to free tap;
- T immediately after the boundary routes only to the current note;
- a wrong control after the boundary does not become free tap;
- distinct pulses on opposite sides of one boundary remain in separate masks;
- repeated pressed, held, held-age, and direction observations are identical;
- every booster-component evaluation sees the same pulse;
- no logical input outside 0 through 9 receives synthetic correction;
- native true always remains true; and
- EndCurrentNote receives handler result only for diagnostics and changes no availability.

Add behavior-driven cases for every note ID 0 through 15 plus free tap:

- IDs 0 and 11 through 14 create no synthetic note input.
- IDs 1, 3, 5, 6, 7, 8, 9, and 15 use their ordinary component button family.
- IDs 2 and 10 use direction matching and authored history.
- ID 4 uses ordinary directional pressed pulses in native query order.
- CRITICAL and DUAL HOLD run both booster components against one sample with no invented concurrent-note state.

Do not merely compare NotePolicyFor against another copied table. Each case must issue the native-shaped queries and assert observable values and selected timestamps.

**Step 2: Define policy without ownership terminology**

Use:

~~~cpp
enum class NoteInputFamily : std::uint8_t {
    None,
    Button,
    Direction,
    Hold,
    Scratch,
    Beat,
    DirectionHold,
    Lifecycle,
};

enum class LateGateInputOrder : std::uint8_t {
    None,
    PreviewButton,
    PreviewDirection,
    SelectedBeforeGate,
};

struct CurrentNoteEvaluation {
    GameplayNoteType type{GameplayNoteType::None};
    std::uintptr_t note_identity{};
    std::uint32_t booster_component{};
    int recognition_milliseconds{};
    SongTime earliest_eligible{};
    bool muted{};
    std::array<int, 3> accepted_target_directions{};
};
~~~

Critical uses Button and DualHold uses Hold. Their booster_component selects the normal base control group; do not add paired policy values.

**Step 3: Implement JudgementInputScope**

JudgementInputScope owns one JudgementInputSample value for one core call. The sample payload is never mutated. The scope may retain only:

- current descriptor metadata;
- current-note and free-tap pulse classifications;
- the selected candidate for the active booster component;
- a compatible non-consuming late-gate preview;
- whether a boundary-crossing free-tap route exists; and
- bounded observation data.

Required public behavior:

~~~cpp
class JudgementInputScope final {
public:
    JudgementInputScope(
        JudgementInputSample sample,
        gc::input::GameplayInputStyle style,
        DirectionNormalizationTable direction_normalization) noexcept;

    void BeginCurrentNote(CurrentNoteEvaluation note) noexcept;
    void EndCurrentNote(bool native_handler_result) noexcept;
    [[nodiscard]] EffectiveInputQuery QueryPressed(
        int logical_input,
        bool native_value,
        PressedQueryPurpose purpose) const noexcept;
    void ObservePressedSelection(
        int requested_input,
        int source_input,
        PressedQueryPurpose purpose,
        bool result,
        bool direction_aliases_enabled) noexcept;
    [[nodiscard]] EffectiveInputQuery QueryHeld(
        int logical_input,
        bool native_value) const noexcept;
    [[nodiscard]] int QueryHeldAge(
        int logical_input,
        int native_value) const noexcept;
    [[nodiscard]] int QueryDirection(
        int booster_component,
        int native_value) const noexcept;
    void CompleteDirectionMatch(bool accepted) noexcept;
    [[nodiscard]] int AdjustLateGateArgument(int native_argument) const noexcept;
    [[nodiscard]] int AdjustGradeArgument(int native_argument) const noexcept;
    [[nodiscard]] bool ShouldForceNativeFreeTap() const noexcept;
};
~~~

Pressed queries use current-note or free-tap masks according to purpose. Held queries use native OR sample held. Direction derives from the same held mask and validated normalization table without waiting for held-age query order.

Preview families choose a compatible current-note edge before the late gate but never grade it. Scratch and Beat select the actual accepted edge before their gate. Grade adjustment uses only an actual selected edge. A history-only or already-held match has no selected edge and keeps the native argument.

Apply:

    forwarded = clamp_to_int(native_argument + round_to_native_ms(T - R))

The duration helper at 0x5D04F0 is outside this class and remains untouched.

**Step 4: Preserve Switch semantics in the sample tests**

Add explicit cases for:

- direction rise as same-booster center-button edge;
- real button or any same-booster direction as held button;
- exact diagonal;
- either adjacent cardinal satisfying a diagonal initially and continuously;
- native successes remaining successes; and
- unrelated booster/component queries remaining unchanged.

Reuse SwitchInputPolicy helpers; do not duplicate its alias tables.

**Step 5: Run the new policy tests**

~~~powershell
cmake --build --preset msvc32-debug --target JudgementInputSampleTests SwitchInputPolicyTests
ctest --preset msvc32-debug -R "^(JudgementInputSampleTests|SwitchInputPolicyTests)$"
~~~

Expected: every note ID and free-tap behavior passes.

**Step 6: Commit**

~~~powershell
git add -- src/Input/HighFps/JudgementInputSample.h src/Input/HighFps/JudgementInputSample.cpp tests/Input/HighFps/JudgementInputSampleTests.cpp src/Input/CMakeLists.txt tests/Input/CMakeLists.txt
git commit -m "Implement immutable judgement sample policy"
~~~

---

### Task 5: Integrate anchors, timeline, and scope into the bridge

**Files:**

- Modify: **src/Input/HighFps/HighFpsInputBridge.h**
- Modify: **src/Input/HighFps/HighFpsInputBridge.cpp**
- Rewrite: **tests/Input/HighFps/HighFpsInputBridgeTests.cpp**

**Step 1: Write bridge-level failure and catch-up tests**

Cover the composed behavior:

- exact and rounded anchors both activate correction;
- no anchor and failed anchor return native behavior;
- a failure clears/reseeds once, not once per query or core;
- playback-generation change resets and seeds held without a pulse;
- inactive playback resets only on the transition to inactive;
- zero-step outer frames preserve queued transitions;
- catch-up R values hide future events and deliver at the first eligible R;
- exact events at 144, 165, and 240 do not depend on target-frame ratios;
- one sample is shared by both booster components;
- sample destruction retires every ready pulse without a commit;
- overflow continues with newest state;
- target FPS 60 remains a correction no-op; and
- capture, BeginJudgement, queries, and EndJudgement allocate zero bytes.

Use PublishSongTimeAnchor explicitly in tests. Do not estimate event time from core-entry QPC.

**Step 2: Replace bridge state**

Remove transaction_, rescue sequence ownership, pending expiry, CommitUses, and judgement_seen_in_outer_frame_. Add:

~~~cpp
std::optional<SongTimeAnchor> song_time_anchor_{};
SongTimedInputTimeline timeline_{};
std::optional<JudgementInputScope> judgement_scope_{};
std::array<InputTransitionRecord, kTransitionCapacity> drain_buffer_{};
std::uint64_t producer_sequence_{};
std::uint64_t producer_cohort_{};
bool anchor_failed_{};
~~~

GameplayJudgementContext contains recognition_milliseconds, gameplay_frame, and an optional core_entry_qpc used only in diagnostics. The mapper must never consume core_entry_qpc.

**Step 3: Implement anchor publication and real resets**

Add:

~~~cpp
void PublishSongTimeAnchor(SongTimeAnchor anchor) noexcept;
void PublishGameplaySongTimeAnchor(SongTimeAnchor anchor) noexcept;
~~~

The free function forwards to the process-lifetime bridge singleton and is the only API used by FrameratePatch.cpp.

Exact and rounded anchors are valid. On an exact playback-generation change, request PlaybackGeneration reset before accepting new input. Inactive requests GameplayInactive only on an active-to-inactive transition. Failed clears/reseeds once, suspends correction, and records a rate-limited anomaly. The next valid anchor begins a new correction epoch.

Activation, deactivation, focus loss, device disconnect, inactive transition, generation change, and shutdown are the complete reset list.

**Step 4: Build one scope per native core call**

BeginJudgement:

1. checks Active mode, high-FPS configuration, and a valid anchor;
2. seeds any requested epoch from latest_fastio_;
3. drains the transport;
4. maps records through the anchor;
5. builds the sample for the supplied R;
6. constructs one JudgementInputScope; and
7. returns true.

EndJudgement records bounded sample-level diagnostics and destroys the scope. It does not commit input usage.

Current-note and query bridge methods delegate only while a scope is active and the local input-device ID matches. Otherwise they return native behavior.

**Step 5: Run the bridge graph**

~~~powershell
cmake --build --preset msvc32-debug --target SongTimeTests InputTransitionJournalTests SongTimedInputTimelineTests JudgementInputSampleTests HighFpsInputBridgeTests
ctest --preset msvc32-debug -R "^(SongTimeTests|InputTransitionJournalTests|SongTimedInputTimelineTests|JudgementInputSampleTests|HighFpsInputBridgeTests)$"
~~~

Expected: all five targets pass.

**Step 6: Commit**

~~~powershell
git add -- src/Input/HighFps/HighFpsInputBridge.h src/Input/HighFps/HighFpsInputBridge.cpp tests/Input/HighFps/HighFpsInputBridgeTests.cpp
git commit -m "Integrate the song-timed input bridge"
~~~

---

### Task 6: Publish game-thread song-time anchors

**Files:**

- Modify: **src/Patches/Framerate/FrameratePatch.h**
- Modify: **src/Patches/Framerate/FrameratePatch.cpp**
- Modify: **tests/Patches/Framerate/FramerateRuntimeTests.cpp**

**Step 1: Add pure anchor-selection tests**

Extend FramerateRuntimeTests with:

- safe midpoint for positive QPC begin/end values;
- exact cursor observation selecting exact source frame/rate/generation;
- missing exact publication with a nonnegative group cursor selecting rounded milliseconds;
- inactive publication selecting inactive;
- failed group cursor selecting failed;
- rejected GameplaySongClock observation selecting failed for input correction while preserving existing step behavior; and
- QPC acquisition failure selecting failed.

The expected midpoint is:

    begin + (end - begin) / 2

after validating end >= begin, avoiding begin + end overflow.

**Step 2: Extend the existing selection result**

Add the query window to the pure selection seam:

~~~cpp
struct GameplaySongClockQueryWindow {
    std::int64_t begin_qpc{};
    std::int64_t end_qpc{};
    bool valid{};
};

[[nodiscard]] SongTimeAnchor BuildSongTimeAnchor(
    const GameplaySongClockInputSelection& input,
    GameplaySongClockQueryWindow window,
    bool observation_rejected) noexcept;
~~~

Exact copies source frame, sample rate, and generation. Rounded copies native group-cursor milliseconds. Inactive and failed carry no usable position.

**Step 3: Bracket only the existing game-thread cursor query**

In HookGameplaySongClock:

1. query QPC immediately before ScopedGameplayAudioCursorQuery and native group cursor acquisition;
2. acquire and consume the cursor exactly as today;
3. query QPC immediately afterward;
4. resolve the existing GameplaySongClock step without changing its native write behavior;
5. build and publish the anchor for input correction; and
6. retain existing fatal handling for the audio step write.

Replace ObserveGameplayPlayback calls with PublishSongTimeAnchor. Remove any bridge-side GameTimeOffset or JudgTimeOffset input; do not alter the existing GameplaySongClock calculation that already receives the game's static GameTimeOffset value.

No code is added to GameplayAudioCursorObservation, DirectSoundFacade, WASAPI, or ASIO callback functions.

**Step 4: Confirm the 60 FPS hook plan remains off**

Keep SwitchInputPatchInit's high_fps_bridge_requested condition at target FPS greater than 60. Framerate plan tests must continue proving native 60 timing does not install the gameplay song-clock replacement for this correction.

**Step 5: Run framerate and bridge integration tests**

~~~powershell
cmake --build --preset msvc32-debug --target FramerateRuntimeTests FrameratePatchPlanTests GameplaySongClockTests HighFpsInputBridgeTests
ctest --preset msvc32-debug -R "^(FramerateRuntimeTests|FrameratePatchPlanTests|GameplaySongClockTests|HighFpsInputBridgeTests)$"
~~~

Expected: all four targets pass and existing audio step cases are unchanged.

**Step 6: Commit**

~~~powershell
git add -- src/Patches/Framerate/FrameratePatch.h src/Patches/Framerate/FrameratePatch.cpp tests/Patches/Framerate/FramerateRuntimeTests.cpp
git commit -m "Publish gameplay song-time anchors"
~~~

---

### Task 7: Wire native note, query, late-gate, and grade hooks to the sample

**Files:**

- Modify: **src/Input/HighFps/HighFpsJudgementHooks.h**
- Modify: **src/Input/HighFps/HighFpsJudgementHooks.cpp**
- Modify: **src/Input/HighFps/HighFpsInputBridge.h**
- Modify: **src/Input/HighFps/HighFpsInputBridge.cpp**
- Modify: **src/Input/Switch/SwitchInputPatch.h**
- Modify: **src/Input/Switch/SwitchInputPatch.cpp**
- Modify: **tests/Input/HighFps/HighFpsJudgementHooksTests.cpp**
- Modify: **tests/Input/HighFps/HighFpsInputBridgeTests.cpp**
- Modify: **tests/Input/Switch/SwitchInputPatchTests.cpp**

**Step 1: Change query composition from acceptance to observation**

Rename GameplayQueryCallbacks.accept_pressed to observe_pressed_selection and bridge AcceptGameplayPressed to ObserveGameplayPressedSelection.

ComposeGameplayQuery still:

1. calls the native wrapper exactly once per requested source;
2. ORs the scoped sample result;
3. applies existing Switch aliases when active;
4. reports the final requested/source pair for timestamp selection and diagnostics.

The observer must never consume, hide, retire, or mutate a pulse. Update SwitchInputPatchTests to repeat the same query twice and prove the result remains visible.

**Step 2: Make core entry use only native R for mapping**

hook_core calls BeginGameplayJudgement with the native recognition_ms and gameplay_frame. A QPC sample may remain for diagnostics, but mapping and selection must not use it. Always call the original core once and end the scope if it began.

**Step 3: Read the current descriptor defensively**

At hook_dispatcher, use guarded reads for:

- uint32 type at +0;
- uint32 mute field at +8, where nonzero is the native mute predicate;
- float earliest eligibility at +152; and
- float target degrees at +240 only for direction families.

Convert earliest eligibility with SongTimeFromNativeMilliseconds. If descriptor access or conversion fails, leave that note on native input and increment one bounded anomaly. Pass lane argument under the name booster_component.

Do not retrieve another or future descriptor.

**Step 4: Forward each existing hook to the scope**

- dispatcher: BeginCurrentNote before the native call and EndCurrentNote after it;
- direction matcher: call CompleteDirectionMatch after the native result;
- held age: native result OR authored-history behavior through the scoped query;
- direction: native result OR sample-derived normalized direction;
- late gate: pass AdjustGameplayLateGateArgument(native_argument);
- grade: pass AdjustGameplayGradeArgument(native_argument), then record the native result for diagnostics only.

Keep the x86 fastcall shim/static-assert contracts and native cleanup sizes. Preserve one native call and catch all C++ exceptions at each hook boundary.

**Step 5: Test every timing selection family**

Bridge and sample tests must prove:

- Normal, Merry, Hidden, Hidden2, Critical, Hold head, Dual Hold head, Flick, and Slide Hold use a non-consuming compatible preview at their pre-query late gate;
- Scratch and Beat use the actually selected pressed edge before their late gate;
- only an actually selected edge adjusts grade;
- Merry's already-adjusted native argument receives only T - R;
- held/history-only direction success leaves both gate and grade native;
- long-form continuation has no new edge; and
- native handler false does not relabel or replay the sample.

**Step 6: Run focused hook/query tests**

~~~powershell
cmake --build --preset msvc32-debug --target JudgementInputSampleTests HighFpsInputBridgeTests HighFpsJudgementHooksTests SwitchInputPatchTests
ctest --preset msvc32-debug -R "^(JudgementInputSampleTests|HighFpsInputBridgeTests|HighFpsJudgementHooksTests|SwitchInputPatchTests)$"
~~~

Expected: all four targets pass.

**Step 7: Commit**

~~~powershell
git add -- src/Input/HighFps/HighFpsJudgementHooks.h src/Input/HighFps/HighFpsJudgementHooks.cpp src/Input/HighFps/HighFpsInputBridge.h src/Input/HighFps/HighFpsInputBridge.cpp src/Input/HighFps/JudgementInputSample.h src/Input/HighFps/JudgementInputSample.cpp src/Input/Switch/SwitchInputPatch.h src/Input/Switch/SwitchInputPatch.cpp tests/Input/HighFps/JudgementInputSampleTests.cpp tests/Input/HighFps/HighFpsInputBridgeTests.cpp tests/Input/HighFps/HighFpsJudgementHooksTests.cpp tests/Input/Switch/SwitchInputPatchTests.cpp
git commit -m "Wire song-timed judgement observations"
~~~

---

### Task 8: Add the guarded native free-tap branch correction

**Files:**

- Modify: **src/Input/HighFps/HighFpsJudgementHooks.h**
- Modify: **src/Input/HighFps/HighFpsJudgementHooks.cpp**
- Modify: **src/Input/HighFps/HighFpsInputBridge.h**
- Modify: **src/Input/HighFps/HighFpsInputBridge.cpp**
- Modify: **tests/Input/HighFps/HighFpsJudgementHooksTests.cpp**
- Modify: **tests/Input/HighFps/HighFpsInputBridgeTests.cpp**
- Modify: **docs/reverse-engineering/high-fps-input-judgement-hook-manifest.md**

**Step 1: Reconfirm the new site read-only**

Connect to the existing daemon, or start/connect through AgentSession.start if it was stopped. Require ida_available and read:

- 12 bytes at 0x5D76CE;
- disassembly through the native call at 0x5D76E4; and
- the function frame showing the permission byte at EBP - 0xA9.

Abort this task if the bytes differ from the verified prefix in this plan. Do not mutate or save the IDB.

**Step 2: Add a failing signature and stack-policy test**

Extend JudgementHookSite with FreeTapBranch, add the RVA and 12-byte prefix, and update the signature fixture. Add a small guarded stack-access helper test proving:

- force false changes nothing;
- force true changes native permission zero to one;
- native permission one stays one;
- an invalid stack accessor returns false without writing; and
- no helper calls native free tap.

Add a bridge case for T <= boundary < R and assert ShouldForceNativeFreeTap is true only for that case. Equality belongs to free tap. A muted note and R <= boundary use the ordinary native branch and do not report a forced branch.

Expose the hook-safe singleton query as:

~~~cpp
[[nodiscard]] bool ShouldForceGameplayFreeTapBranch() noexcept;
~~~

It returns false outside an active correction scope and catches no native calls.

Run the focused hook test. Expected: it fails before the site and hook are implemented.

**Step 3: Install the eighth hook transactionally**

Set:

~~~cpp
inline constexpr std::size_t kHighFpsJudgementHookCount = 8;
inline constexpr std::uintptr_t kJudgementFreeTapBranchRva = 0x001D76CE;
inline constexpr std::ptrdiff_t kFreeTapPermissionStackOffset = -0xA9;
inline constexpr std::array<std::uint8_t, 12>
    kJudgementFreeTapBranchSignature{
        0x0F, 0xB6, 0x85, 0x57, 0xFF, 0xFF,
        0xFF, 0x83, 0xF8, 0x01, 0x75, 0x0F,
    };
~~~

Add a SafetyHook MidHook at the instruction beginning 0x5D76CE. Its callback:

1. asks the active bridge scope whether a boundary-crossing free-tap pulse exists;
2. guarded-reads EBP - 0xA9;
3. changes only zero to one when force is required; and
4. returns to the original instructions.

Do not change EIP, call 0x5D2040, synthesize sound/effects, or force a current-note-classified wrong input.

Include the site in preflight, operation construction, reverse-order rollback, reset, mismatch reporting, and exception containment.

**Step 4: Verify all eight partial-install failures**

Update HighFpsJudgementHooksTests to fail each internal operation index in turn and assert that every already-installed operation is reset in reverse order. Keep the outer Switch hook transaction tests proving a failed high-FPS operation also rolls back pressed/held/diagonal operations as applicable.

**Step 5: Update the binary manifest**

Rewrite the manifest's superseded ownership/lifetime text. Add:

- free-tap branch VA/RVA/prefix;
- EBP - 0xA9 native permission byte;
- descriptor mute +8 and earliest +152 fields;
- the rule T <= earliest < R;
- native 0x5D2040 remains the only free-tap implementation;
- booster_component terminology; and
- the eighth hook's transactional installation.

Do not preserve statements claiming physical pulses have separate pending note/free-tap lifetimes.

**Step 6: Run hook, bridge, and outer transaction tests**

~~~powershell
cmake --build --preset msvc32-debug --target HighFpsJudgementHooksTests HighFpsInputBridgeTests SwitchInputPatchTests GameplayInputHookTransactionTests
ctest --preset msvc32-debug -R "^(HighFpsJudgementHooksTests|HighFpsInputBridgeTests|SwitchInputPatchTests|GameplayInputHookTransactionTests)$"
~~~

Expected: signatures, stack policy, eight-way rollback, boundary routing, and outer rollback all pass.

**Step 7: Commit**

~~~powershell
git add -- src/Input/HighFps/HighFpsJudgementHooks.h src/Input/HighFps/HighFpsJudgementHooks.cpp src/Input/HighFps/HighFpsInputBridge.h src/Input/HighFps/HighFpsInputBridge.cpp tests/Input/HighFps/HighFpsJudgementHooksTests.cpp tests/Input/HighFps/HighFpsInputBridgeTests.cpp docs/reverse-engineering/high-fps-input-judgement-hook-manifest.md
git commit -m "Route boundary free taps through native judgement"
~~~

---

### Task 9: Replace diagnostics and close the superseded documentation

**Files:**

- Modify: **src/Input/HighFps/HighFpsInputBridge.h**
- Modify: **src/Input/HighFps/HighFpsInputBridge.cpp**
- Modify: **src/Patches/Framerate/FrameratePatch.cpp**
- Modify: **tests/Input/HighFps/HighFpsInputBridgeTests.cpp**
- Modify: **docs/reverse-engineering/high-fps-input-judgement-hook-manifest.md**
- Modify: **docs/reverse-engineering/high-fps-input-judgement-decisions.md**

**Step 1: Write bounded formatting/counter assertions**

Replace transaction, pending-expiry, duplicate-presentation, and lane assertions with:

- captured, mapped, deferred, delivered, and coalesced transitions;
- native and forced free-tap presentations;
- exact anchors, rounded anchors, and anchor failures;
- queue depth, maximum depth, and oldest evictions;
- real reset reasons;
- hook/invariant anomalies; and
- diagnostic overwrites.

A representative formatted record must include event T, recognition R, T - R, current note type, booster_component, earliest eligibility, route, source/requested input, match reason, gate/grade deltas, and native handler result as observation.

It must also retain epoch, playback generation, sequence, cohort, anchor source, cursor-query span, and whether the free-tap path was native or forced.

**Step 2: Rename misleading terminology**

Rename every high-FPS diagnostic field and formatted key from lane to booster_component. Remove paired-note, simultaneous-note, and chord wording from current production/test documentation for this feature. Input cohort is the correct term for controls published in one physical snapshot.

Do not rename native dispatcher arguments in reverse-engineering quotations when preserving literal ABI evidence; explain their booster-component meaning instead.

**Step 3: Rate-limit only likely anomalies**

- overflow logs only when the cumulative eviction count crosses the existing reporting policy;
- anchor failure logs once on entry to failed state and summarizes suppressed repeats;
- invariant and hook callback failures use bounded counters;
- no per-core, per-query, held-continuation, or ordinary zero-step log is added.

Keep the diagnostic ring fixed at its existing capacity unless tests show the new record no longer fits.

**Step 4: Mark the old decision record fully superseded**

Change the header of high-fps-input-judgement-decisions.md to point only to the August 16 approved design. Remove its claim that the August 15 one-shot lifetime remains authoritative. Leave historical body text clearly labelled historical; do not edit the approved spec.

**Step 5: Run focused diagnostics tests and static terminology review**

~~~powershell
cmake --build --preset msvc32-debug --target HighFpsInputBridgeTests FramerateRuntimeTests
ctest --preset msvc32-debug -R "^(HighFpsInputBridgeTests|FramerateRuntimeTests)$"
rg -n "lane|paired.note|simultaneous.note|pending.*expir|CommitUses|JudgementInputTransaction|JudgementInputTimeline" src/Input/HighFps tests/Input/HighFps docs/reverse-engineering/high-fps-input-judgement-hook-manifest.md
~~~

Expected: tests pass. The review command has no semantic leftovers; any occurrence of transaction refers only to hook installation transactionality, not input ownership.

**Step 6: Commit**

~~~powershell
git add -- src/Input/HighFps/HighFpsInputBridge.h src/Input/HighFps/HighFpsInputBridge.cpp src/Patches/Framerate/FrameratePatch.cpp tests/Input/HighFps/HighFpsInputBridgeTests.cpp docs/reverse-engineering/high-fps-input-judgement-hook-manifest.md docs/reverse-engineering/high-fps-input-judgement-decisions.md
git commit -m "Finalize song-timed input diagnostics"
~~~

---

### Task 10: Perform complete x86 static verification

**Files:**

- Verify all changed production, test, CMake, and documentation files
- Do not modify H:\gc runtime files

**Step 1: Use the x86 MSVC environment**

Prefix each configure/build/test invocation with the checked x86 environment, as shown in the next two steps. If the existing build cache points at another compiler environment, substitute cmake --fresh for that preset's configure invocation. Do not edit generated cache files.

**Step 2: Run the complete Debug graph**

~~~powershell
cmd.exe /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --preset msvc32-debug'
cmd.exe /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build --preset msvc32-debug'
cmd.exe /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && ctest --preset msvc32-debug -j 4'
~~~

Expected: configure, complete build, and every Debug test pass.

**Step 3: Run the complete Release graph**

~~~powershell
cmd.exe /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --preset msvc32-release'
cmd.exe /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build --preset msvc32-release'
cmd.exe /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && ctest --preset msvc32-release -j 4'
~~~

Expected: configure, complete optimized x86 build, and every Release test pass.

**Step 4: Recheck native and static contracts**

~~~powershell
Get-FileHash H:\gc\game471.exe -Algorithm SHA256
git diff 8e3dd49..HEAD --check
git status --short
~~~

Confirm the supported executable remains 3,691,008 bytes with SHA-256 FEAD3BD4D0E0985F101965EDC417DD2B96522F8716FF789D84618FEB0D7A2522.

Use the existing IDA daemon read-only to compare every production hook/helper prefix, including the new free-tap branch. Inspect the optimized x86 hook shims or disassembly for preserved thiscall receivers and cleanup sizes. Confirm no code path calls duration helper 0x5D04F0 from the loader and no new audio callback dependency exists.

**Step 5: Review the verification matrix**

Before any deployment request, record static evidence for all 22 automated behaviors from the approved spec:

- zero-step delivery;
- catch-up deferral;
- rise/fall sample;
- cohort sharing;
- idempotence;
- same-control newest coalescence;
- exact/rounded anchors at 144/165/240;
- zero/one/multiple catch-up steps;
- both sides of free-tap boundary and forced branch;
- mute and wrong-input routing;
- authored history boundaries;
- all Switch rules;
- note IDs 0 through 15 and free tap;
- Critical and Dual Hold booster components;
- no composite IDs 10 through 19;
- oldest eviction;
- real-only resets;
- invalid-anchor native fallback; and
- 60 FPS correction no-op.

Also record signature rejection, all partial-install rollbacks, x86 calling conventions, and exception containment.

**Step 6: Commit any verification-only documentation correction**

If verification requires no source correction, do not make an empty commit. If a documentation statement was corrected, run git diff --check again and commit only that correction with a precise message.

## Deferred Runtime Acceptance

This plan ends after static verification. Do not deploy as part of execution.

When the user later explicitly requests deployment, runtime acceptance remains:

1. deploy the verified artifact without creating an unsolicited backup;
2. run 240 FPS Switch first;
3. inspect H:\gc\loader-log.txt and stop if input still drops or misassociates;
4. exercise short alternating buttons, short directions, diagonal/cardinal Switch acceptance, Critical, Dual Hold, Hold and Slide Hold heads/continuation, Scratch, Beat, and free tap around the earliest-eligible boundary; and
5. only after acceptable 240 FPS behavior, run the 60 FPS validation session.

Build success, static hook proof, and clean logs do not establish gameplay feel. Final acceptance remains the user's observed cabinet/game behavior.

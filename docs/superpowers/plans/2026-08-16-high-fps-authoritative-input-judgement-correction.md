> **ARCHIVED FAILED ATTEMPT — DO NOT EXECUTE.** “Authoritative” is a historical
> name, not a current authority claim.

# High-FPS Authoritative Input Judgement Correction Implementation Plan

> **Execution status:** Implemented inline. Runtime corrections below supersede
> conflicting task text; static verification does not replace cabinet acceptance.

**Goal:** Replace the defective high-FPS query-composition layer with one authoritative, song-timed input view that covers every native note type and free tap without changing native judgement, long-note, or Switch rules.

**Architecture:** Keep the existing QPC transition journal, song-time anchor, mapped pending queue, rational authored-60 history, immutable core sample, and native handlers. Add an explicit caller-and-frame query contract; make that immutable sample authoritative for every supported gameplay query; bind a physical edge to the late gate and short grade only after the same source is accepted; keep recognition time authoritative for all native lifecycle and long-note mechanics.

**Tech Stack:** C++23, Windows x86, SafetyHook, CMake/Ninja presets, MSVC 18 Insiders x86 toolchain, repository-local executable fixtures, IDA Pro/Hex-Rays through the existing `ida-cli` daemon, and CTest.

## Post-run correction addendum

The first 240 FPS acceptance run disproved three assumptions in the original
task text. Execution therefore includes these mandatory corrections:

- descriptor timing reads cover `+152` mute time, `+156` unmute time, and
  `+160` late limit; diagnostics print all three;
- a muted HIDDEN/HIDDEN2 descriptor exposes its pulse to both its native note
  handler and the native post-note free-tap path; and
- a physical direction rise remains timestamp metadata while that control is
  held, allowing FLICK and SLIDE HOLD heads accepted through native authored-60
  forgiveness to use the original event time in a later high-FPS core.

The pressed pulse itself remains one-shot. Retained direction metadata clears
on release and never replays button, free-tap, scratch, or beat input. The
native candidate loop remains non-consuming: do not add cross-descriptor edge
claims, because native code intentionally continues to the next linear
candidate after some false returns and early-miss state changes.

## Global Constraints

- The authoritative design is [High-FPS Authoritative Input Judgement Correction](../specs/2026-08-16-high-fps-authoritative-input-judgement-correction-design.md). The superseded design, decision record, and plan are historical evidence only.
- Execute in the current worktree at `H:\gc\artifacts\GCLoader\.worktrees\asio-audio-backend`. The user has locked implementation to one inline agent: do not create another worktree or dispatch implementation subagents.
- Source, tests, documentation, and commits stay in the worktree. `H:\gc` is runtime evidence/deployment scope only.
- Do not add a fallback for any audited path. Every supported note/query shape must use its explicit authoritative contract. An unaudited caller shape remains native, increments `contract_anomalies`, and blocks acceptance until investigated.
- At target FPS 60, all authoritative high-FPS correction is inactive. The independent gameplay-only Switch patch keeps its existing 60 FPS semantics.
- Do not read, cache, scale, or patch `HoldSafeFrame`, `SlideHoldSafeFrame`, `ScratchEnableTime`, or `BeatEnableTime`. Their current values remain native constants. Only the existing `GameTimeOffset` clock input and native `JudgTimeOffset` argument remain variable; neither is read by the bridge.
- Do not hook or change native duration helper `0x5D04F0`, chart scheduling, note iteration, miss processing, long-note interval logic, audio callbacks, or ASIO/WASAPI code.
- Preserve fixed-capacity storage and exception containment. Add no heap allocation or logging to ordinary polling, judgement-query, `DllMain`, or audio-callback paths.
- Preserve the exact x86 ABI and install hooks transactionally after CRT startup. A signature or installation failure must unwind the complete gameplay hook set.
- Tests must use an independently written native-consumer model. Tests that only call a production classifier and repeat its table are insufficient.
- Static/CTest/IDA evidence is not cabinet acceptance. Runtime acceptance is 240 FPS first, then 60 FPS.
- Deployment, once every static gate passes, is a direct verified copy of the Release DLL. Do not make a backup DLL and do not terminate a running game process.

## Implementation Map

### Production responsibilities

| File | Responsibility after this plan |
|---|---|
| `src/Input/HighFps/JudgementQueryContract.h/.cpp` (new) | Note IDs, typed query roles, verified call-site classification, frame-shape validation, late-gate/grade caller validation |
| `src/Input/HighFps/SongTimedInputTimeline.h/.cpp` | Map QPC transitions, retain future transitions, build rational authored-60 history, produce one immutable sample and separated queue/history counters |
| `src/Input/HighFps/JudgementInputSample.h/.cpp` | Route ready rises between one current note and free tap, hold component-local candidate/accepted-edge state, answer authoritative typed queries |
| `src/Input/HighFps/HighFpsInputBridge.h/.cpp` | Own the active core transaction, validate query context, expose fixed-capacity diagnostics and reset/lifecycle behavior |
| `src/Input/HighFps/HighFpsJudgementHooks.h/.cpp` | Preserve native handlers and ABI, propagate call-site/matcher context, adjust a validated late gate/grade call once, force only the verified free-tap branch |
| `src/Input/Switch/SwitchInputPatch.h/.cpp` | Apply native source priority and locked Switch aliases to the authoritative sample; retain native-only behavior outside an active high-FPS scope |
| `src/Input/Polling/InputPollingRuntime.cpp` | Publish complete snapshot transitions and request the existing focus/disconnect/shutdown resets |
| `src/Patches/Framerate/FrameratePatch.cpp` | Supply target FPS, song-time anchors and playback lifecycle; drain bounded summaries without per-note flooding |
| `docs/reverse-engineering/high-fps-input-judgement-hook-manifest.md` | Record the final caller/query-role matrix, ABI evidence, all note IDs `0..15`, free tap, and untouched native behavior |

### Test responsibilities

| File | Independent obligation |
|---|---|
| `tests/Input/HighFps/JudgementQueryContractTests.cpp` (new) | Exact supported and rejected caller/frame/note/component shapes from binary evidence |
| `tests/Input/HighFps/NativeDirectionMatcherReference.h` (new) | Test-only native `0x5D2E50` predicate model; must not include or call production policy helpers |
| `tests/Input/HighFps/NativeDirectionMatcherReferenceTests.cpp` (new) | Authored-60 head/continuation results at 60/120/144/165/240/360 FPS, Arcade and Switch |
| `tests/Input/HighFps/HighFpsJudgementPipelineTests.cpp` (new) | Independent native consumer order for note IDs `0..15`, free tap, grade/offset cases, and long-note lifecycle |
| Existing `SongTimedInputTimelineTests`, `JudgementInputSampleTests`, `HighFpsInputBridgeTests`, `HighFpsJudgementHooksTests`, and `SwitchInputPatchTests` | Focused unit, transaction, hook, alias, lifecycle, and diagnostic regressions |

The new query-contract module is justified by four production consumers: the Switch pressed/held wrappers, the high-FPS helper hooks, the bridge, and the immutable sample scope. It is not a second input engine.

---

### Task 1: Introduce the explicit native-query and timing-site contract

**Files:**
- Create: `src/Input/HighFps/JudgementQueryContract.h`
- Create: `src/Input/HighFps/JudgementQueryContract.cpp`
- Create: `tests/Input/HighFps/JudgementQueryContractTests.cpp`
- Modify: `src/Input/HighFps/JudgementInputSample.h:10-70`
- Modify: `src/Input/HighFps/HighFpsJudgementHooks.h:15-296`
- Modify: `src/Input/CMakeLists.txt:22-39`
- Modify: `tests/Input/CMakeLists.txt:91-119`
- Modify: `docs/reverse-engineering/high-fps-input-judgement-hook-manifest.md:55-84`

**Interfaces:**

Move `GameplayNoteType` out of `JudgementInputSample.h` and define the contract without depending on bridge state:

```cpp
enum class JudgementQueryKind : std::uint8_t {
    Pressed,
    Held,
    HeldAge,
    Direction,
};

enum class JudgementQueryRole : std::uint8_t {
    PressedCurrentNote,
    PressedFreeTap,
    HeldCurrent,
    HeldAuthoredMinusTwo,
    HeldAgeAuthored60,
    DirectionCurrent,
};

enum class DirectionMatcherPhase : std::uint8_t {
    None,
    Head,
    Continuation,
};

struct JudgementQueryInvocation {
    JudgementQueryKind kind{};
    std::uint32_t caller_rva{};
    std::uint32_t requested_frame{};
};

struct JudgementQueryRequest {
    JudgementQueryInvocation invocation{};
    std::uint32_t recognition_frame{};
    GameplayNoteType note_type{GameplayNoteType::None};
    std::optional<std::uint32_t> booster_component{};
    DirectionMatcherPhase direction_phase{DirectionMatcherPhase::None};
};

[[nodiscard]] std::optional<JudgementQueryRole>
ClassifyJudgementQuery(const JudgementQueryRequest& request) noexcept;
[[nodiscard]] bool LateGateCallerMatches(
    GameplayNoteType type,
    std::uint32_t caller_rva) noexcept;
[[nodiscard]] bool GradeCallerMatches(
    GameplayNoteType type,
    std::uint32_t caller_rva) noexcept;
```

`caller_rva` is the direct call instruction for the generic pressed/held/late-gate/grade wrapper. Nested held-age/direction helper queries also require the active `0x5D2E50` matcher scope; the classifier must reject them when that verified parent scope is absent.

- [ ] **Step 1: Add an independent binary-derived contract fixture**

In `JudgementQueryContractTests.cpp`, define the expected call-site rows locally with a provenance comment naming `game471.exe.i64` and the manifest. Cover:

- pressed current note: `0x1D1EC0`, `0x1D3A26`, `0x1D3D83`, `0x1D3DA6`, `0x1D3DC9`, `0x1D3DEC`, `0x1D4325`;
- pressed free tap: `0x1D20E0`, `0x1D2176`;
- held current: `0x1D2F93` at `requested_frame == recognition_frame` and `0x1D43B8` at the current frame;
- authored-minus-two: `0x1D303B` only when `requested_frame + 2 == recognition_frame` without unsigned wrap;
- held age: helper caller `0x1D2FC8` only while the direction matcher scope is active;
- current normalized direction: helper caller `0x1D316F` only while the direction matcher scope is active;
- direction matcher: FLICK component/head callers `0x1D3425` and `0x1D3448`; SLIDE HOLD component/head callers `0x1D36D3` and `0x1D36F6`; SLIDE HOLD component/continuation callers `0x1D37A7` and `0x1D37CA`;
- late gates: `0x1D1E41`, `0x1D33F1`, `0x1D369F`, `0x1D3A5D`, `0x1D3E23`, `0x1D42BA` with their matching families; and
- grade callers: `0x1D1F2A` for the normal family and `0x1D34C5` for FLICK.

For each accepted row, mutate one dimension at a time: unknown caller, wrong note type, wrong booster (`2`), wrong current/minus-two frame shape, head versus continuation mismatch, and missing matcher scope. Each mutation must return `std::nullopt`/`false`.

The exact timing-site mapping is: `0x1D1E41` for the NORMAL/MERRY/HIDDEN/HIDDEN2/CRITICAL family, `0x1D33F1` for FLICK, `0x1D369F` for SLIDE HOLD, `0x1D3A5D` for BEAT, `0x1D3E23` for SCRATCH, and `0x1D42BA` for HOLD/DUAL HOLD. `PressedFreeTap` accepts the two verified free-tap callers with note type `None` and no booster component. Every current-note role requires component `0` or `1`. Direction current-held, authored held age, and normalized direction are queried in both matcher phases; continuation acceptance uses current-held state and ignores the age value. Frame-minus-two freshness is head-only.

- [ ] **Step 2: Register and run the RED contract test**

```powershell
$env:GC_ASIO_SDK_DIR='H:\gc\artifacts\ASIOSDK'
& $env:ComSpec /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --preset msvc32-debug && cmake --build --preset msvc32-debug --target JudgementQueryContractTests'
```

Expected: generation or compilation fails because the contract target and interfaces do not exist.

- [ ] **Step 3: Implement the pure classifier**

Use exhaustive `switch` statements over note ID and call site. Normal-family caller `0x1D1EC0` accepts only NORMAL, MERRY GO ROUND, HIDDEN, HIDDEN2, and CRITICAL. Hold caller `0x1D4325` accepts only HOLD and DUAL HOLD. Scratch and Beat callers accept only their own types. Direction roles accept only FLICK and SLIDE HOLD and validate head/continuation semantics. Lifecycle-only IDs `0`, `11`, `12`, `13`, and `14` have no current-note query role.

Do not expose `PressedQueryPurpose::Unscoped` as a third permissive path. Unsupported shapes have no role.

- [ ] **Step 4: Update includes and the hook manifest**

Make `JudgementInputSample.h`, `HighFpsJudgementHooks.h`, and later consumers include the new contract. Record the query-role matrix and the fact that `0x5D04F0` is not a timing-site hook in the manifest. Do not change production hook behavior yet.

- [ ] **Step 5: Run the focused test**

```powershell
& $env:ComSpec /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build --preset msvc32-debug --target JudgementQueryContractTests HighFpsJudgementHooksTests && ctest --preset msvc32-debug -R "^(JudgementQueryContractTests|HighFpsJudgementHooksTests)$" --output-on-failure'
```

Expected: both tests pass; every accepted role has a verified caller/note/frame shape and every mutation is rejected.

- [ ] **Step 6: Commit**

```powershell
git add -- src/Input/HighFps/JudgementQueryContract.h src/Input/HighFps/JudgementQueryContract.cpp src/Input/HighFps/JudgementInputSample.h src/Input/HighFps/HighFpsJudgementHooks.h src/Input/CMakeLists.txt tests/Input/HighFps/JudgementQueryContractTests.cpp tests/Input/CMakeLists.txt docs/reverse-engineering/high-fps-input-judgement-hook-manifest.md
git commit -m "Define authoritative judgement query contracts"
```

### Task 2: Correct rational authored history and separate real loss counters

**Files:**
- Create: `tests/Input/HighFps/NativeDirectionMatcherReference.h`
- Create: `tests/Input/HighFps/NativeDirectionMatcherReferenceTests.cpp`
- Modify: `src/Input/HighFps/SongTimedInputTimeline.h:15-107`
- Modify: `src/Input/HighFps/SongTimedInputTimeline.cpp:65-323`
- Modify: `src/Input/HighFps/InputTransitionJournal.h:12-50`
- Modify: `tests/Input/HighFps/SongTimedInputTimelineTests.cpp`
- Modify: `tests/Input/HighFps/InputTransitionJournalTests.cpp`
- Modify: `tests/Input/CMakeLists.txt`

**Interfaces:**

Replace ambiguous timeline counters with:

```cpp
struct SongTimedTimelineDiagnostics {
    std::uint64_t mapped{};
    std::uint64_t deferred{};
    std::uint64_t delivered{};
    std::uint64_t mapped_pending_evictions{};
    std::uint64_t authored_history_rotations{};
    std::uint64_t same_control_coalesces{};
    std::uint64_t future_head_observations{};
    std::uint64_t ignored_epoch_records{};
    std::uint64_t invariant_anomalies{};
};
```

`InputTransitionJournal::eviction_count()` remains the sole transport eviction count. Timeline history rotation must never contribute to either loss counter.

- [ ] **Step 1: Write the independent direction reference model**

`NativeDirectionMatcherReference.h` must include only standard-library headers and test-owned structures. Reproduce these native predicates independently:

```cpp
struct NativeDirectionInputs {
    std::array<bool, 4> held_current{};
    std::array<std::uint8_t, 4> held_age_authored60{};
    std::array<bool, 4> held_minus_two{};
    int normalized_direction{};
    std::array<int, 3> accepted_targets{};
    bool continuation{};
    bool switch_style{};
};

struct NativeDirectionResult {
    bool accepted{};
    bool fresh_head{};
};
```

The head model requires at least one currently held contributor with age `<= 1` and a false frame-minus-two state, maximum held age across the four booster directions `<= 4`, and target acceptance. Continuation requires any current held contributor and target acceptance, ignores age/history for acceptance, and never produces `fresh_head`. Switch accepts an exact target or either adjacent cardinal for a diagonal; it does not require both cardinals.

- [ ] **Step 2: Add RED timeline/reference cases**

Add tests for all of the following:

- `T > R` remains pending across repeated core calls and increments `future_head_observations` once for that sequence;
- the first `R >= T` exposes one pressed pulse and later calls expose only held/history;
- a press and release entirely between core calls exposes pressed and forced-held only in the delivery sample;
- distinct logical controls in one cohort remain simultaneously visible;
- two rises of the same control coalesce to one boolean pulse with the newest sequence/time and increment `same_control_coalesces` once;
- authored state exactly two rational `1/60` samples ago is independent of target FPS;
- a new direction head reports current held true, authored age in native limits, and minus-two false;
- a pre-held direction reports minus-two true and is not a fresh head;
- history capacity rotation increments only `authored_history_rotations`;
- mapped pending overflow discards the oldest mapped record, keeps the newest complete post-state, and increments only `mapped_pending_evictions`; and
- transport overflow keeps the newest complete transition and increments only `InputTransitionJournal::eviction_count()`.

Run every authored-history direction fixture at `60`, `120`, `144`, `165`, `240`, and `360` target FPS, even though the 60 FPS production bridge will be inactive; this proves the rational projection itself.

- [ ] **Step 3: Run RED**

```powershell
& $env:ComSpec /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build --preset msvc32-debug --target SongTimedInputTimelineTests InputTransitionJournalTests NativeDirectionMatcherReferenceTests'
```

Expected: compile/assertion failure because the separated counters and reference target do not exist and current history rotations are counted as `oldest_evictions`.

- [ ] **Step 4: Implement ordered delivery and counter separation**

Keep the existing rational boundary representation. Change only delivery/history bookkeeping:

- track the first observation of a future head sequence so repeated catch-up calls do not inflate the counter;
- keep `sample.pressed` idempotent and use `newest_rises[input]` for newest-wins same-control coalescing;
- retain the full final `held_after` state even if an earlier record is evicted;
- fill `held_two_authored_samples_ago` from `last_authored_index - 2`; and
- rotate the authored history ring normally without warning or loss semantics.

- [ ] **Step 5: Run focused timeline/reference tests**

```powershell
& $env:ComSpec /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build --preset msvc32-debug --target SongTimedInputTimelineTests InputTransitionJournalTests NativeDirectionMatcherReferenceTests && ctest --preset msvc32-debug -R "^(SongTimedInputTimelineTests|InputTransitionJournalTests|NativeDirectionMatcherReferenceTests)$" --output-on-failure'
```

Expected: all pass at every FPS in the matrix; history rotation never appears as input loss.

- [ ] **Step 6: Commit**

```powershell
git add -- src/Input/HighFps/SongTimedInputTimeline.h src/Input/HighFps/SongTimedInputTimeline.cpp src/Input/HighFps/InputTransitionJournal.h tests/Input/HighFps/SongTimedInputTimelineTests.cpp tests/Input/HighFps/InputTransitionJournalTests.cpp tests/Input/HighFps/NativeDirectionMatcherReference.h tests/Input/HighFps/NativeDirectionMatcherReferenceTests.cpp tests/Input/CMakeLists.txt
git commit -m "Correct authored input history semantics"
```

### Task 3: Make the immutable sample authoritative and bind accepted edges

**Files:**
- Modify: `src/Input/HighFps/JudgementInputSample.h:72-154`
- Modify: `src/Input/HighFps/JudgementInputSample.cpp:13-500`
- Modify: `tests/Input/HighFps/JudgementInputSampleTests.cpp`

**Interfaces:**

Replace `PressedQueryPurpose`, generic current-held methods, and native-first returns with typed requests. Keep the public scope small:

```cpp
struct BoundInputEdge {
    std::uintptr_t note_identity{};
    std::uint32_t booster_component{};
    int requested_input{-1};
    int source_input{-1};
    std::uint64_t cohort{};
    std::uint64_t sequence{};
    SongTime time{};
};

class JudgementInputScope final {
public:
    void BeginCurrentNote(CurrentNoteEvaluation note) noexcept;
    void EndCurrentNote(bool native_handler_result) noexcept;
    void BeginDirectionMatcher(DirectionMatcherPhase phase) noexcept;
    void EndDirectionMatcher(bool accepted) noexcept;
    [[nodiscard]] EffectiveInputQuery QueryBoolean(
        const JudgementQueryRequest& request,
        int logical_input,
        bool native_value) noexcept;
    [[nodiscard]] int QueryScalar(
        const JudgementQueryRequest& request,
        int logical_input_or_booster,
        int native_value) noexcept;
    void ObservePressedSelection(
        const JudgementQueryRequest& request,
        int requested_input,
        int source_input,
        bool accepted) noexcept;
    [[nodiscard]] int SelectLateGateArgument(
        std::uint32_t caller_rva,
        int native_argument) noexcept;
    [[nodiscard]] int SelectGradeArgument(
        std::uint32_t caller_rva,
        int native_argument) noexcept;
};
```

The implementation may keep `RoutedNoteState` and `ComponentEvaluationState` private. `RoutedNoteState` is keyed by `note_identity`; `ComponentEvaluationState` is keyed by booster component. Ending the first CRITICAL or DUAL HOLD component must not consume or clear the immutable sample before the other component evaluates.

- [ ] **Step 1: Replace permissive tests with authoritative-false tests**

Write RED cases proving:

- in an active supported scope, sample false returns `{handled=true, value=false}` even when raw native is true;
- a ready current-note pulse cannot appear through `PressedFreeTap`;
- a free-tap-routed pulse cannot appear through `PressedCurrentNote`;
- a wrong control after `mute_time` remains current-note routed and cannot become a selectable free-tap sound;
- no current note exposes all ready rises only to free tap;
- non-muted `T <= mute_time < R` marks only the verified free-tap branch as forceable;
- current held and authored-minus-two return different values from the same sample;
- held-age is returned in authored-60 units;
- current direction derives from the same held mask and native normalization table;
- unknown caller/note/frame/component shapes return `handled=false` and report one contract anomaly; and
- repeated supported queries are idempotent.

- [ ] **Step 2: Add candidate/accepted-edge RED tests**

Cover these exact rules:

- normal/hold preview chooses center button before Switch aliases;
- Switch aliases use the existing fixed order and bind the actual physical source;
- a late gate may preview an edge, but a rejected later query clears it and grade remains unadjusted;
- an accepted source must match note identity, component, requested input, source, cohort, and sequence before grade can use `T`;
- FLICK/SLIDE HOLD confirm only a rise contributing to the accepted effective direction;
- when more than one ready rise contributes to one effective direction, the newest contributing sequence supplies `T` (same-cohort rises naturally share the same physical timestamp);
- history-only or continuation direction success has no selected edge;
- SCRATCH latches the first successful native-priority direction and later successful probes cannot replace it;
- BEAT binds one ready pulse per judgement opportunity; and
- CRITICAL and DUAL HOLD retain separate component-local bindings over the same immutable sample.

- [ ] **Step 3: Run RED**

```powershell
& $env:ComSpec /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build --preset msvc32-debug --target JudgementInputSampleTests && ctest --preset msvc32-debug -R "^JudgementInputSampleTests$" --output-on-failure'
```

Expected: assertions fail at least at current `QueryPressed`/`QueryHeld`, where raw native true wins, and at historical held, which current code does not query.

- [ ] **Step 4: Implement routing once per logical note**

At the first `BeginCurrentNote` for a new `note_identity`, partition each ready edge:

```text
no note                         -> free tap
muted                            -> current note and free tap
non-muted T <= mute_time         -> free tap
non-muted T > mute_time          -> current note
```

Do not repartition or consume edges when the same descriptor is evaluated for the other booster component. Store source/cohort/sequence/time, not only a bit mask.

- [ ] **Step 5: Implement authoritative typed query results**

For every classified role, set `handled=true` and derive the result only from the immutable sample and locked Switch policy. Raw native values are retained only for diagnostics. For an unsupported request, preserve native behavior and increment the anomaly counter; this path must never be exercised by an accepted matrix test.

Consume `held_two_authored_samples_ago` for `HeldAuthoredMinusTwo`. Do not translate `recognition_frame - 2` into two target-FPS frames.

- [ ] **Step 6: Implement non-consuming candidate confirmation**

Preview compatible gate candidates without consuming the sample. Confirm the candidate only after the native query/matcher accepts the same source. Store a separate accepted edge per component. A direction match may confirm a fresh head only when the matcher phase is `Head`, the minus-two history is false, and the contributing rise belongs to the effective direction. A continuation or history-only success remains accepted native behavior but has no event timestamp.

For an effective direction with multiple contributing rises, bind the newest contributing sequence. This identifies the physical change that completed the accepted held-direction state while preserving all controls in the immutable sample.

- [ ] **Step 7: Run focused sample and contract tests**

```powershell
& $env:ComSpec /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build --preset msvc32-debug --target JudgementInputSampleTests JudgementQueryContractTests NativeDirectionMatcherReferenceTests && ctest --preset msvc32-debug -R "^(JudgementInputSampleTests|JudgementQueryContractTests|NativeDirectionMatcherReferenceTests)$" --output-on-failure'
```

Expected: all authoritative-false, route separation, historical direction, candidate confirmation, first-latch, and shared-sample tests pass.

- [ ] **Step 8: Commit**

```powershell
git add -- src/Input/HighFps/JudgementInputSample.h src/Input/HighFps/JudgementInputSample.cpp tests/Input/HighFps/JudgementInputSampleTests.cpp
git commit -m "Make judgement samples authoritative"
```

### Task 4: Propagate full query context through native hooks and Switch composition

**Files:**
- Modify: `src/Input/HighFps/HighFpsInputBridge.h:180-405`
- Modify: `src/Input/HighFps/HighFpsInputBridge.cpp:433-705, 978-1069`
- Modify: `src/Input/HighFps/HighFpsJudgementHooks.h:15-360`
- Modify: `src/Input/HighFps/HighFpsJudgementHooks.cpp:350-760`
- Modify: `src/Input/Switch/SwitchInputPatch.h:196-228`
- Modify: `src/Input/Switch/SwitchInputPatch.cpp:35-110, 309-482`
- Modify: `tests/Input/HighFps/HighFpsInputBridgeTests.cpp`
- Modify: `tests/Input/HighFps/HighFpsJudgementHooksTests.cpp`
- Modify: `tests/Input/Switch/SwitchInputPatchTests.cpp`
- Modify: `docs/reverse-engineering/high-fps-input-judgement-hook-manifest.md`

**Interfaces:**

The bridge entry points accept one complete request rather than losing caller/frame information:

```cpp
[[nodiscard]] EffectiveInputQuery QueryGameplayBoolean(
    int input_device_id,
    int logical_input,
    JudgementQueryInvocation invocation,
    bool native_value) noexcept;
[[nodiscard]] int QueryGameplayScalar(
    int input_device_id,
    int logical_input_or_booster,
    JudgementQueryInvocation invocation,
    int native_value) noexcept;
void ObserveGameplayPressedSelection(
    JudgementQueryInvocation invocation,
    int requested_input,
    int source_input,
    bool accepted) noexcept;
void BeginGameplayDirectionMatcher(
    std::uint32_t caller_rva,
    DirectionMatcherPhase phase) noexcept;
void EndGameplayDirectionMatcher(bool accepted) noexcept;
[[nodiscard]] int SelectGameplayLateGateTime(
    std::uint32_t caller_rva,
    int recognition_ms) noexcept;
[[nodiscard]] int SelectGameplayGradeArgument(
    std::uint32_t caller_rva,
    int native_argument) noexcept;
```

`HighFpsInputBridge::QueryHeld` must no longer discard `requested_frame` as it currently does at `HighFpsInputBridge.cpp:549-559`.

The generic pressed/held/helper hooks construct only `JudgementQueryInvocation`, because they know the direct caller and requested frame but do not own the chart descriptor. `HighFpsInputBridge` completes `JudgementQueryRequest` from the active core recognition frame, dispatcher note type/identity/component, and active direction-matcher phase. A hook must never infer note type or component from the requested logical input.

- [ ] **Step 1: Write RED hook-context tests**

Extend hook and bridge tests to require:

- pressed and held wrappers forward direct caller RVA and requested frame;
- late-gate and grade wrappers derive their direct call-site RVA from `_ReturnAddress()` and subtract the five-byte call length once;
- the direction matcher forwards its direct caller RVA, installs a nested phase scope before invoking the original, and closes it after the original, including exception paths;
- `continuation == 0` enters `Head`; nonzero enters `Continuation`;
- FLICK accepts only `0x1D3425`/`0x1D3448` as component 0/1 heads; SLIDE HOLD accepts only `0x1D36D3`/`0x1D36F6` as component 0/1 heads and `0x1D37A7`/`0x1D37CA` as component 0/1 continuations;
- held-age caller `0x1D2FC8` and direction caller `0x1D316F` are supported only inside that active matcher scope;
- dispatcher note type, identity, component, recognition time, and target directions reach every nested query;
- each original native helper is invoked exactly once; and
- callback failure preserves ABI-safe native return behavior and increments the bounded hook/contract anomaly count.

- [ ] **Step 2: Write RED Switch-composition tests**

Update `GameplayQueryCallbacks` to receive the full request. Test this order independently with fake native and bridge callbacks:

1. call the native source once for its side effects/value;
2. ask the authoritative bridge for that same source;
3. if the bridge handled the source, use its value even when native was true;
4. for Switch, continue from a false center result through the existing alias order;
5. stop at the first authoritative true source; and
6. report the final requested/source pair once.

Require Arcade to probe only the requested source. Require Switch pressed center aliases only on the same booster, Switch held to accept a real button or any held same-booster direction, and diagonal logic to preserve exact/native matches plus either adjacent cardinal.

- [ ] **Step 3: Run RED**

```powershell
& $env:ComSpec /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build --preset msvc32-debug --target HighFpsInputBridgeTests HighFpsJudgementHooksTests SwitchInputPatchTests && ctest --preset msvc32-debug -R "^(HighFpsInputBridgeTests|HighFpsJudgementHooksTests|SwitchInputPatchTests)$" --output-on-failure'
```

Expected: compile/assertion failure because existing APIs pass only `PressedQueryPurpose`, held drops the frame, and native true short-circuits composition.

- [ ] **Step 4: Wire the pressed/held wrappers**

Keep caller-RVA capture at the outer generic hook where `_ReturnAddress()` identifies the game call site. Pass an invocation to the bridge and let the bridge complete it from active dispatcher context; do not infer note type from input number. Outside an active valid high-FPS judgement scope, return native behavior exactly. At 60 FPS Switch, keep the independent native-plus-alias path and do not construct authoritative requests.

- [ ] **Step 5: Wire direction matcher head/continuation context**

Capture the direct matcher caller and validate it against note type, component, and the native `continuation` argument. Enter the matcher scope before calling native `0x5D2E50` so nested held, held-age, historical-held, and direction queries share that caller, phase, and component. End it with the original result. Preserve the `history_match` output pointer and all original arguments. Do not call or emulate the matcher a second time. Native continuation may still call the held-age helper, but its acceptance remains current-held-driven and it must never query or synthesize frame-minus-two freshness.

- [ ] **Step 6: Validate late-gate and grade call sites**

Capture and validate the direct caller for both helper hooks. A matched late gate may receive a preview candidate's event-adjusted argument. A matched grade caller may receive only the confirmed accepted edge's argument:

```cpp
const int forwarded = native_argument +
    RoundSongTimeDeltaMilliseconds(edge.time, recognition_ms);
return original(self, descriptor, forwarded); // exactly once
```

MERRY GO ROUND adds this delta to the already adjusted native argument. Never replace the native argument with an absolute event timestamp. Never adjust the grade for HOLD, SCRATCH, BEAT, SLIDE HOLD continuation, DUAL HOLD duration, or a rejected/history-only query.

- [ ] **Step 7: Preserve the verified free-tap branch**

Keep inline branch `0x5D76CE` as the only forced path. Force it only for non-muted `T <= mute_time < R` when the edge is routed to free tap. Do not call the free-tap implementation manually. The pressed queries at `0x5D20E0/0x5D2176` must use `PressedFreeTap`; muted descriptors intentionally expose the same pulse to their separate current-note handler as verified in native core `0x5D68E0`.

- [ ] **Step 8: Run the combined query/hook tests**

```powershell
& $env:ComSpec /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build --preset msvc32-debug --target JudgementQueryContractTests JudgementInputSampleTests HighFpsInputBridgeTests HighFpsJudgementHooksTests SwitchInputPatchTests GameplayInputHookTransactionTests && ctest --preset msvc32-debug -R "^(JudgementQueryContractTests|JudgementInputSampleTests|HighFpsInputBridgeTests|HighFpsJudgementHooksTests|SwitchInputPatchTests|GameplayInputHookTransactionTests)$" --output-on-failure'
```

Expected: all pass; no supported query leaks raw native true, every frame shape is validated, matcher phase is scoped, and each native helper runs once.

- [ ] **Step 9: Commit**

```powershell
git add -- src/Input/HighFps/HighFpsInputBridge.h src/Input/HighFps/HighFpsInputBridge.cpp src/Input/HighFps/HighFpsJudgementHooks.h src/Input/HighFps/HighFpsJudgementHooks.cpp src/Input/Switch/SwitchInputPatch.h src/Input/Switch/SwitchInputPatch.cpp tests/Input/HighFps/HighFpsInputBridgeTests.cpp tests/Input/HighFps/HighFpsJudgementHooksTests.cpp tests/Input/Switch/SwitchInputPatchTests.cpp docs/reverse-engineering/high-fps-input-judgement-hook-manifest.md
git commit -m "Enforce authoritative native query context"
```

### Task 5: Verify every note type and preserve native long-note behavior

**Files:**
- Create: `tests/Input/HighFps/HighFpsJudgementPipelineTests.cpp`
- Modify: `tests/Input/HighFps/NativeDirectionMatcherReference.h`
- Modify: `tests/Input/HighFps/HighFpsInputBridgeTests.cpp`
- Modify: `tests/Input/HighFps/HighFpsJudgementHooksTests.cpp`
- Modify: `tests/Input/Switch/SwitchInputPatchTests.cpp`
- Modify: `tests/Input/CMakeLists.txt`
- Modify if a test exposes a defect: `src/Input/HighFps/JudgementInputSample.h/.cpp`
- Modify if a test exposes a defect: `src/Input/HighFps/HighFpsInputBridge.h/.cpp`
- Modify if a test exposes a defect: `src/Input/HighFps/HighFpsJudgementHooks.h/.cpp`

**Independent harness:**

`HighFpsJudgementPipelineTests.cpp` implements a test-owned consumer that invokes production entry points in the native order recorded in the hook manifest. Its expected result logic must not call `NotePolicyFor`, `ClassifyJudgementQuery`, Switch production helpers, or production grade helpers. The harness owns only fixture thresholds/state so it can detect incorrect production routing.

- [ ] **Step 1: Add one explicit row for every ID and free tap**

The test table must name and execute all rows, not loop over an unnamed default:

| ID | Required observation |
|---:|---|
| 0 | no invented current-note query; ready edge remains eligible only for free tap |
| 1 | bound center/Switch pulse, event late gate, one short grade call |
| 2 | current held, authored age, minus-two freshness, effective direction, fresh-head gate/grade |
| 3 | head edge only; body held/release and duration use recognition time |
| 4 | four direction probes, first successful native priority, any-different continuation within native 250 ms, no pulse grade |
| 5 | one ready pulse per opportunity, native 200 ms maintenance, no pulse grade |
| 6 | NORMAL contract with delta added to the already adjusted MERRY argument |
| 7 | NORMAL input/timing with HIDDEN lifecycle untouched |
| 8 | NORMAL input/timing with HIDDEN2 lifecycle untouched |
| 9 | two component evaluations, one immutable sample, component-local accepted edges, native aggregation |
| 10 | FLICK head contract; current-held continuation; native release/duration |
| 11 | no independent query; native marker result unchanged |
| 12 | no independent query; native marker result unchanged |
| 13 | no independent query; native marker result unchanged |
| 14 | no independent query; native marker result unchanged |
| 15 | two HOLD component heads share one sample; no consumption; native coupled completion/duration |
| free tap | free-tap-routed pulse, selectable sound path once, mute/effects/flags left native |

- [ ] **Step 2: Add stable grade and offset fixtures**

For NORMAL-family and FLICK grade callers, independently calculate `round_to_native_ms(T - R)` and call a test-owned grade function once. Include stable GREAT, GOOD, and MISS points away from boundaries and early/late sampling-sensitive points at boundaries.

Repeat with positive and negative `GameTimeOffset` already reflected in both `T` and `R`, and positive and negative `JudgTimeOffset` already present in the native argument. Assert the bridge never reads either offset and never applies either twice. Stable fixtures must retain their original result; boundary fixtures must be deterministic from `T` and may move in either direction.

- [ ] **Step 3: Add complete long-note sequences**

Exercise:

- HOLD: edge head, current-held body, release, native coverage result;
- SLIDE HOLD: fresh direction head, held-direction continuation, release, native coverage result;
- SCRATCH: native-priority initial direction, different-direction refresh, same-direction rejection, 250 ms timeout, final native duration result;
- BEAT: repeated distinct pulses, no replay across catch-up calls, 200 ms timeout, final native duration result; and
- DUAL HOLD: both component heads visible in one sample, independent held/release state, native coupled final result.

Only start/end and maintained interval determine long-note results. Assert no intermediate action calls the short grade hook and all stored lifecycle times remain `R`.

- [ ] **Step 4: Add complete Switch sequences**

At both booster components, prove:

- a direction rise aliases same-booster center pressed;
- any held direction sustains the center;
- exact diagonal remains accepted;
- either adjacent cardinal accepts and continues a diagonal;
- Arcade receives no alias broadening; and
- menu/test/binding/raw FastIO paths are not involved.

- [ ] **Step 5: Run RED against the newly wired pipeline**

```powershell
& $env:ComSpec /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build --preset msvc32-debug --target HighFpsJudgementPipelineTests && ctest --preset msvc32-debug -R "^HighFpsJudgementPipelineTests$" --output-on-failure'
```

Expected: the first run may expose integration defects. Fix production only where an independent expected row fails; do not weaken or route around a note row.

- [ ] **Step 6: Run the full input matrix**

```powershell
& $env:ComSpec /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build --preset msvc32-debug --target HighFpsJudgementPipelineTests NativeDirectionMatcherReferenceTests JudgementQueryContractTests SongTimedInputTimelineTests JudgementInputSampleTests HighFpsInputBridgeTests HighFpsJudgementHooksTests SwitchInputPatchTests && ctest --preset msvc32-debug -R "^(HighFpsJudgementPipelineTests|NativeDirectionMatcherReferenceTests|JudgementQueryContractTests|SongTimedInputTimelineTests|JudgementInputSampleTests|HighFpsInputBridgeTests|HighFpsJudgementHooksTests|SwitchInputPatchTests)$" --output-on-failure'
```

Expected: all pass at 60/120/144/165/240/360 test rates, every ID `0..15` and free tap has a named assertion, and long-note intermediate actions never receive a short grade.

- [ ] **Step 7: Commit**

Stage only files actually changed by this task:

```powershell
git add -- tests/Input/HighFps/HighFpsJudgementPipelineTests.cpp tests/Input/HighFps/NativeDirectionMatcherReference.h tests/Input/HighFps/HighFpsInputBridgeTests.cpp tests/Input/HighFps/HighFpsJudgementHooksTests.cpp tests/Input/Switch/SwitchInputPatchTests.cpp tests/Input/CMakeLists.txt src/Input/HighFps/JudgementInputSample.h src/Input/HighFps/JudgementInputSample.cpp src/Input/HighFps/HighFpsInputBridge.h src/Input/HighFps/HighFpsInputBridge.cpp src/Input/HighFps/HighFpsJudgementHooks.h src/Input/HighFps/HighFpsJudgementHooks.cpp
git commit -m "Cover every native judgement input path"
```

### Task 6: Replace flooded diagnostics with bounded contract evidence

**Files:**
- Modify: `src/Input/HighFps/HighFpsInputBridge.h:40-173, 243-323`
- Modify: `src/Input/HighFps/HighFpsInputBridge.cpp:625-953, 1009-1071`
- Modify: `src/Patches/Framerate/FrameratePatch.cpp:1889-1934, 2119-2127, 2272-2287`
- Modify: `src/Input/Polling/InputPollingRuntime.cpp:53-66, 370-382, 610-670, 875-887`
- Modify: `tests/Input/HighFps/HighFpsInputBridgeTests.cpp`
- Modify: `tests/Patches/Framerate/FramerateRuntimeTests.cpp`
- Modify: `tests/Input/Polling/InputPollingRuntimeStartupTests.cpp`

**Interfaces:**

Expose these unambiguous summary counters:

```cpp
std::uint64_t transport_evictions{};
std::uint64_t mapped_pending_evictions{};
std::uint64_t authored_history_rotations{};
std::uint64_t same_control_coalesces{};
std::uint64_t future_head_observations{};
std::uint64_t contract_anomalies{};
```

Only the first two are possible input loss. Keep the fixed 256-record diagnostic ring, but replace ordinary `NoteObservation` records with meaningful transition, route, gate-candidate, accepted-edge, selected-grade, recovered-input, anomaly, and actual-loss records.

- [ ] **Step 1: Write RED counter and formatting tests**

Require:

- history rotation never triggers `ShouldReportHighFpsEviction` or a warning;
- transport and mapped-pending eviction are separately formatted and rate-limited;
- ordinary empty core calls and ordinary notes with no meaningful event produce no record;
- one physical rise produces at most one transition-delivery record;
- gate preview, accepted association, grade selection, and free-tap routing each produce at most one record for the same sequence/note/component event;
- raw-native disagreement is recorded only when it recovers/suppresses a supported query, not once per repeated probe;
- summary includes exact/rounded anchors, current depths, gate rescues, grade adjustments, Switch aliases, diagonal acceptance, maximum and average `abs(T - R)`, and counts by note family;
- 240 FPS soak retains meaningful records without diagnostic overwrites under ordinary play; and
- target FPS 60 reports passive/no-op correction and emits no authoritative query/grade records.

- [ ] **Step 2: Write RED lifecycle tests**

Require reset/reseed on activation, playback-generation change, focus loss, device disconnect, gameplay exit, and shutdown. Require no reset for zero audio step, one rendered frame without a core call, multiple catch-up core calls, or a core call whose note has no input query. Seed held controls as pre-held without a pressed pulse.

- [ ] **Step 3: Run RED**

```powershell
& $env:ComSpec /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build --preset msvc32-debug --target HighFpsInputBridgeTests FramerateRuntimeTests InputPollingRuntimeStartupTests && ctest --preset msvc32-debug -R "^(HighFpsInputBridgeTests|FramerateRuntimeTests|InputPollingRuntimeStartupTests)$" --output-on-failure'
```

Expected: current `oldest_evictions` and per-note record expectations fail.

- [ ] **Step 4: Implement separated counters and event deduplication**

Accumulate transport loss from `InputTransitionJournal`, mapped loss/history/coalescing/future observations from the timeline, and contract anomalies from query validation. Deduplicate event records with existing note identity/component plus transition sequence; do not add an unbounded set. Track `abs(T - R)` as fixed integer count/sum/max with overflow-safe saturation.

- [ ] **Step 5: Keep lifecycle ownership unchanged**

Retain existing polling reset notifications and playback generation. Remove any reset accidentally tied to frame cadence. `FrameratePatch` supplies anchors/lifecycle but does not inspect note types or offsets.

- [ ] **Step 6: Remove per-note log flooding**

At the existing periodic drain, log bounded meaningful records and one summary. Warn only for transport or mapped-pending loss, hook/contract anomaly, or diagnostic overwrite. Do not log every native miss/note observation and do not perform native/event grade comparison.

- [ ] **Step 7: Run diagnostics/lifecycle tests**

```powershell
& $env:ComSpec /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build --preset msvc32-debug --target HighFpsInputBridgeTests FramerateRuntimeTests InputPollingRuntimeStartupTests && ctest --preset msvc32-debug -R "^(HighFpsInputBridgeTests|FramerateRuntimeTests|InputPollingRuntimeStartupTests)$" --output-on-failure'
```

Expected: all pass; ordinary authored history and notes no longer flood warnings/records, and only real queue loss is reported as input loss.

- [ ] **Step 8: Commit**

```powershell
git add -- src/Input/HighFps/HighFpsInputBridge.h src/Input/HighFps/HighFpsInputBridge.cpp src/Patches/Framerate/FrameratePatch.cpp src/Input/Polling/InputPollingRuntime.cpp tests/Input/HighFps/HighFpsInputBridgeTests.cpp tests/Patches/Framerate/FramerateRuntimeTests.cpp tests/Input/Polling/InputPollingRuntimeStartupTests.cpp
git commit -m "Bound high FPS judgement diagnostics"
```

### Task 7: Audit optimized hooks and run complete static verification

**Files:**
- Verify: every file changed by Tasks 1-6
- Update evidence only if live results differ: `docs/reverse-engineering/high-fps-input-judgement-hook-manifest.md`

- [ ] **Step 1: Run superseded-behavior and forbidden-constant scans**

```powershell
rg -n "PressedQueryPurpose|NativeFallback|native_value\)\s*\{|oldest_evictions|NoteObservation|grade_original|native.*event.*grade" src/Input/HighFps src/Input/Switch tests/Input
rg -n "HoldSafeFrame|SlideHoldSafeFrame|ScratchEnableTime|BeatEnableTime" src/Input/HighFps src/Input/Switch
rg -n "5D04F0|1D04F0" src/Input/HighFps src/Input/Switch
```

Expected: no permissive query-purpose/fallback or ambiguous diagnostic behavior remains; the four fixed config names have zero production hits in the high-FPS/Switch layer; `0x5D04F0` is not hooked or called by correction code. Test/documentation mentions that assert absence are acceptable.

- [ ] **Step 2: Run complete Debug configure/build/test**

```powershell
$env:GC_ASIO_SDK_DIR='H:\gc\artifacts\ASIOSDK'
& $env:ComSpec /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --preset msvc32-debug && cmake --build --preset msvc32-debug && ctest --preset msvc32-debug -j 4 --output-on-failure'
```

Expected: the entire Debug graph builds and every CTest test passes. Do not infer success from a focused target count.

- [ ] **Step 3: Run complete Release configure/build/test**

```powershell
& $env:ComSpec /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --preset msvc32-release && cmake --build --preset msvc32-release && ctest --preset msvc32-release -j 4 --output-on-failure'
```

Expected: the entire optimized graph builds and every CTest test passes.

- [ ] **Step 4: Reuse the existing IDA daemon for a read-only optimized hook audit**

Using the `ida-cli` skill and the already-open daemon for `H:\gc\game471.exe.i64`, verify and export a focused artifact containing:

- hook/helper entry prefixes and RVAs;
- the direct pressed, held, late-gate, grade, and free-tap caller sets;
- nested matcher helper callers `0x5D2FC8` and `0x5D316F`, plus matcher head/continuation callers `0x5D3425`, `0x5D3448`, `0x5D36D3`, `0x5D36F6`, `0x5D37A7`, and `0x5D37CA`;
- exactly two direct callers of `0x5D0E00` (`0x5D1F2A`, `0x5D34C5`);
- dispatcher rows `0..15` and default rows `0`, `11`, `12`, `13`, `14`;
- direction matcher arguments and `continuation` meaning;
- x86 receiver/stack cleanup for every wrapper; and
- confirmation that duration helper `0x5D04F0` remains outside the hook set.

If a live caller/signature differs from the checked manifest, stop before deployment, update the manifest and contract fixture from the binary evidence, and rerun both full graphs.

- [ ] **Step 5: Inspect the Release artifact and initialization boundary**

```powershell
& $env:ComSpec /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && dumpbin /headers build-msvc32-release\dist\iDmacDrv32.dll'
git diff --check
git status --short --branch
```

Expected: PE machine is `14C` x86; no new static object performs runtime work before CRT initialization; input initialization remains post-CRT and does not modify WASAPI/ASIO startup; diff check is clean; only intentional evidence/test changes remain before their commit.

- [ ] **Step 6: Commit any final evidence/test correction**

If Task 7 changes the manifest or tests, commit only those changes:

```powershell
git add -- docs/reverse-engineering/high-fps-input-judgement-hook-manifest.md tests
git commit -m "Verify high FPS judgement hook coverage"
```

If no files changed, do not create an empty commit.

### Task 8: Deploy the verified DLL and apply the two-stage runtime gate

**Files:**
- Deploy: `build-msvc32-release/dist/iDmacDrv32.dll`
- Destination: `H:\gc\iDmacDrv32.dll`
- Inspect after each run: `H:\gc\loader-log.txt`

- [ ] **Step 1: Confirm a clean, fully verified source state**

```powershell
git status --short --branch
git log --oneline --decorate -10
Get-Item 'build-msvc32-release\dist\iDmacDrv32.dll' | Select-Object FullName,Length,LastWriteTime
```

Expected: worktree is clean, task commits are present, and the Release DLL is the artifact from the passing full Release graph.

- [ ] **Step 2: Refuse to overwrite a loaded DLL**

```powershell
$game = Get-Process -Name 'game471' -ErrorAction SilentlyContinue
if ($game) { throw 'game471 is running; stop it before deployment' }
```

Do not terminate the process automatically.

- [ ] **Step 3: Deploy once, without a backup, and verify the hash**

```powershell
$source = (Resolve-Path 'build-msvc32-release\dist\iDmacDrv32.dll').Path
$runtimeRoot = (Resolve-Path 'H:\gc').Path
$destination = Join-Path $runtimeRoot 'iDmacDrv32.dll'
Copy-Item -LiteralPath $source -Destination $destination -Force
$sourceHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $source).Hash
$deployedHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $destination).Hash
if ($sourceHash -ne $deployedHash) { throw 'Deployed DLL hash mismatch' }
"deployed_sha256=$deployedHash"
```

Expected: source and runtime hashes match. Do not create or retain a backup DLL.

- [ ] **Step 4: Hand off the 240 FPS run first**

Report the deployed commit and SHA-256, then ask the user to test 240 FPS before any 60 FPS comparison. Required play coverage is:

- short center-button notes and rapid short notes;
- FLICK and SLIDE HOLD direction heads;
- locked Switch direction-to-center aliases and adjacent-cardinal diagonals;
- available HOLD, SLIDE HOLD, SCRATCH, BEAT, CRITICAL, and DUAL HOLD patterns; and
- free tap/selectable hit sound away from and across the mute-time boundary.

Do not claim gameplay is fixed from static evidence.

- [ ] **Step 5: Inspect bounded 240 FPS evidence**

After the user completes the run:

```powershell
Get-Item 'H:\gc\loader-log.txt' | Select-Object FullName,Length,LastWriteTime
Get-Content -Tail 800 'H:\gc\loader-log.txt'
```

Require:

- complete authoritative hook activation;
- no known-path `contract_anomalies`;
- no transport or mapped-pending eviction in normal play;
- no per-note diagnostic flood/overwrite;
- exactly-once delivered pulse/accepted-edge records;
- bounded `abs(T - R)` consistent with cadence; and
- user acceptance of input registration, direction notes, stable judgement feel, free tap, and long-note behavior.

If any required behavior fails, stop at 240 FPS and diagnose that evidence. Do not proceed to 60 FPS.

- [ ] **Step 6: Run the 60 FPS no-op comparison**

Only after 240 FPS is accepted, have the user repeat representative material at target FPS 60. The log must show the authoritative bridge inactive/passive, no event-time late-gate/grade correction, no authored-history override, and the selected Arcade/Switch style behaving through its independent original 60 FPS path.

- [ ] **Step 7: Record the evidence boundary**

Report static results and the user's 240/60 runtime observations separately. A passing build plus clean logs is not a substitute for the user's judgement/input-feel acceptance.

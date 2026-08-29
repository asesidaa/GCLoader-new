# ASIO Logical-Time Presentation Rewrite Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:executing-plans`
> to execute this plan task by task. Execute inline in the existing repository.
> Do not dispatch subagents and do not create a worktree.

**Goal:** Make absolute judgement and the game-facing DirectSound timeline
depend only on one persistent host-derived logical song coordinate, while a
session-owned ASIO presentation bridge continuously rate-matches that logical
audio stream to the independent hardware clock and replaces only physical
state across focus recovery.

**Architecture:** A backend-lifetime `LogicalPresentationClock` maps captured
multimedia timestamps to exact rational logical frames and implements the
backend-independent judgement-timeline interface. Logical Play/Seek/source
epochs and the DirectSound cursor use that same generation. A single-owner
`LogicalRenderStream` renders every logical interval exactly once. A fresh
`AsioPresentationBridge` for each physical session consumes that stream through
one final-output miniaudio resampler, aligns only while silent, and applies
bounded smooth rate correction while running. A serialized physical-session
controller owns startup, explicit-focus suspension, clean pre-commit recovery
retries, and fatal post-commit failures.

**Tech Stack:** Windows x86, C++23, Steinberg ASIO SDK, miniaudio float-stereo
resampler, checked rational timing, CMake/Ninja, MSVC 18 Insiders, CLion
clangd/clang-tidy.

**Spec:** `docs/superpowers/specs/2026-08-29-asio-logical-time-presentation-rewrite-design.md`

## Execution boundary

This is one coupled vertical rewrite. The judgement boundary, logical playback
history, sequential render stream, presentation bridge, and lifecycle handoff
must all land before a runtime artifact is eligible for testing. Intermediate
commits must compile and pass their focused tests, but they are not deployable
runtime candidates.

- Work on the existing `fix/asio-lifecycle-recovery` branch.
- Do not create a worktree, dispatch agents, deploy a DLL, change driver
  control-panel state, launch the game, or stop/restart/close any process.
- Use CLion MCP for navigation, source edits, formatting, and diagnostics.
  Use the shell for Git, CMake, builds, tests, hashes, and log inspection.
- CLion diagnostics are strictly file by file: open one changed file, wait for
  analysis, request its diagnostics, and then continue. Leave files and CLion
  open. Never batch diagnostics.
- Keep the accepted WASAPI projection, pacing, cursor behavior, and endpoint
  ownership behaviorally unchanged. Its adaptation is naming/interface work,
  not a redesign.
- Do not change `GameTimeOffset` ownership. It remains in the logical
  game/audio-to-source projection exactly once.
- Do not change `JudgTimeOffset`. It remains exclusively in the native grade
  calculation and is never passed to the audio backend or presentation bridge.
- ASIO sample position, callback time, buffer index, submitted tail, physical
  generation, latency, and rate-match ratio must not enter judgement state.
- ASIO never selects WASAPI or DirectSound as a fallback.
- The callback must remain allocation-free, logging-free, lock-free with
  respect to blocking mutexes, and bounded.
- Add only the behavioral tests named below. Each calls production components
  and has an oracle derived independently from the implementation.
- Before every commit, inspect `git status --short`, stage only the exact paths
  named by that task, and inspect `git diff --cached --check` plus
  `git diff --cached --stat`. Never stage a whole directory as a shortcut.
- Task 1's WASAPI test is intentionally green characterization. For each new
  behavior in Tasks 2-7, add the named test first and run that task's focused
  command once before implementation; require a compile failure for the absent
  production API or a behavioral assertion failure for the old contract. Then
  implement and rerun the identical command to green. Never weaken the
  independent oracle to make production pass.

## Required end-state ownership

| State | Persistent logical clock/history | Render-stream owner | Physical session |
|---|---|---|---|
| `Starting` | constructing/advancing | pump once logical state exists | silent, uncommitted |
| `Running` | advancing | current session bridge | audible |
| `Suspended` | advancing | suspension pump | absent |
| `Recovering` | advancing | pump until transactional handoff | priming silence |
| `Fatal` | unusable | none after quiescence | released |
| `Stopping` | quiescing | none after transfer | released |

The persistent objects are:

1. `LogicalPresentationClock` and its one logical generation;
2. `AudioRenderCore`, mixer voices, and logical playback histories;
3. `LogicalRenderStream` and its committed logical tail; and
4. the registered exact judgement-timeline provider.

Every physical attempt owns a new `AsioSession`, callback runtime, physical
generation, sample-position tracker, driver buffers, and
`AsioPresentationBridge`. None survive a release.

---

### Task 1: Establish a backend-independent exact judgement-timeline boundary

**Files:**

- Move: `src/Audio/ExactOutputClock.h` to
  `src/Audio/ExactJudgementTimeline.h`
- Move: `src/Audio/ExactOutputClock.cpp` to
  `src/Audio/ExactJudgementTimeline.cpp`
- Modify: `src/Audio/ExactAudioTime.h`
- Modify: `src/Audio/Wasapi/ExactWasapiClock.h`
- Modify: `src/Audio/Wasapi/ExactWasapiClock.cpp`
- Modify: `src/Audio/Wasapi/ExclusiveAudioEngine.cpp`
- Modify: `src/Audio/Asio/ExactAsioClock.h`
- Modify: `src/Audio/Asio/ExactAsioClock.cpp`
- Modify: `src/Audio/Asio/AsioOutputBackend.cpp`
- Modify: `src/Config/ConfigCompiler.cpp`
- Modify: `src/Patches/AbsoluteJudgement/AbsoluteJudgementRuntime.h`
- Modify: `src/Patches/AbsoluteJudgement/AbsoluteJudgementRuntime.cpp`
- Modify: `src/Patches/AbsoluteJudgement/JudgementClockResolver.h`
- Modify: `src/Patches/AbsoluteJudgement/JudgementClockResolver.cpp`
- Modify: `src/Patches/AbsoluteJudgement/JudgementScheduler.h`
- Modify: `src/Patches/AbsoluteJudgement/JudgementScheduler.cpp`
- Modify: `src/Patches/AbsoluteJudgement/JudgementSettings.h`
- Modify: `src/Audio/CMakeLists.txt`
- Create: `tests/Audio/Wasapi/ExactWasapiClockCompatibilityTests.cpp`
- Modify: `tests/Audio/Asio/ExactAsioClockTests.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Add a characterization test for the accepted WASAPI mapping**

The test must publish a real `ExactWasapiAnchor` and derive the expected output
frame directly from QPC and endpoint-clock arithmetic. Use these values so the
oracle is visibly independent:

```cpp
constexpr std::uint64_t generation = 9;
constexpr std::uint32_t output_rate = 48'000;
constexpr std::uint64_t endpoint_clock_rate = 48'000;
constexpr std::int64_t qpc_rate = 10'000'000;

auto clock = ExactWasapiClock::Create(
    generation, output_rate, endpoint_clock_rate, qpc_rate, 192);
clock->Publish(ExactWasapiAnchor{
    .sequence = 1,
    .endpoint_generation = generation,
    .endpoint_position = 1'480,
    .qpc_100ns = 100'000,
    .mapping = {
        .origin_position = 1'000,
        .clock_frequency = endpoint_clock_rate,
        .origin_output_frame = 0,
        .output_sample_rate = output_rate,
    },
    .submitted_output_tail = 960,
});

const AbsoluteHostTime event{
    .qpc_ticks = 150'000,
    .multimedia_time_ms = 0,
};
// 480 endpoint frames at the anchor plus 5 ms * 48 kHz = 720.
```

Assert `Resolved`, generation 9, exact frame 720, anchor sequence 1, provider
position 1480, and submitted tail 960. Also assert a query at or beyond tail is
`Pending`. Do not use a production projection helper to compute 720.

- [ ] **Step 2: Run the characterization test before the rename**

```powershell
$asioVcvars = 'C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat'
cmd.exe /d /s /c "call `"$asioVcvars`" && set `"GC_ASIO_SDK_DIR=H:\gc\artifacts\ASIOSDK`" && cmake --preset msvc32-debug && cmake --build --preset msvc32-debug --target gc_exact_wasapi_clock_compatibility_tests && ctest --test-dir build-msvc32-debug -R `"^ExactWasapiClockCompatibility$`" --output-on-failure"
```

Expected: PASS against the current implementation. This is a behavior lock for
the accepted WASAPI route, not a test invented from the new ASIO design.

- [ ] **Step 3: Rename the interface around its real consumer**

The public contract becomes:

```cpp
enum class ExactJudgementTimelineDomain : std::uint8_t
{
    WasapiQpc,
    LogicalMultimediaMilliseconds,
};

struct ExactJudgementTimelineInfo final
{
    ExactJudgementTimelineDomain domain{};
    std::uint64_t timeline_generation{};
    std::int64_t qpc_frequency{};
    std::uint32_t logical_output_rate{};
    std::uint32_t provider_period_frames{};
    std::uint32_t provider_output_latency_frames{};
    std::uint32_t timestamp_quantum_ns{};
};

struct ExactJudgementTimelineResult final
{
    ExactClockStatus status{};
    std::uint64_t timeline_generation{};
    std::optional<gc::timing::CheckedRational> logical_output_frame;
    std::uint64_t available_output_tail{};
    std::uint64_t provider_anchor_sequence{};
    std::optional<std::uint64_t> provider_position;
};

class ExactJudgementTimeline
{
public:
    virtual ~ExactJudgementTimeline() = default;
    [[nodiscard]] virtual ExactJudgementTimelineResult Resolve(
        const gc::timing::AbsoluteHostTime&,
        ExactClockResolveIntent) const noexcept = 0;
    [[nodiscard]] virtual ExactJudgementTimelineInfo info() const noexcept = 0;
    [[nodiscard]] virtual ExactJudgementTimelineCounters counters() const noexcept = 0;
    virtual void Invalidate() noexcept = 0;
};

[[nodiscard]] std::shared_ptr<const ExactJudgementTimeline>
AcquireExactJudgementTimeline() noexcept;
```

Rename the counter type, registry, and generation allocator accordingly. Keep the WASAPI
provider's physical fields internally named as endpoint fields; only its
implementation of the generic interface reports them as provider/timeline
metadata. Do not alter `ExactWasapiClock::ResolveQpc` arithmetic, history
selection, submitted-tail gate, or counter behavior.

For this task only, adapt ASIO's existing provider mechanically so the tree
compiles. Task 2 replaces it. Update configuration and absolute-judgement
settings to expect `ExactJudgementTimelineDomain`, using
`LogicalMultimediaMilliseconds` for ASIO.

- [ ] **Step 4: Re-run the WASAPI test and current suite**

Expected: the characterization values remain exactly the same after the
interface rename.

```powershell
$asioVcvars = 'C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat'
cmd.exe /d /s /c "call `"$asioVcvars`" && set `"GC_ASIO_SDK_DIR=H:\gc\artifacts\ASIOSDK`" && cmake --preset msvc32-debug && cmake --build --preset msvc32-debug --target gc_exact_wasapi_clock_compatibility_tests gc_exact_asio_clock_tests gc_audio gc_runtime_patches && ctest --preset msvc32-debug --output-on-failure"
```

- [ ] **Step 5: Commit**

```powershell
git add -- src/Audio/ExactOutputClock.h src/Audio/ExactOutputClock.cpp
git add -- src/Audio/ExactJudgementTimeline.h src/Audio/ExactJudgementTimeline.cpp
git add -- src/Audio/ExactAudioTime.h
git add -- src/Audio/Wasapi/ExactWasapiClock.h src/Audio/Wasapi/ExactWasapiClock.cpp
git add -- src/Audio/Wasapi/ExclusiveAudioEngine.cpp
git add -- src/Audio/Asio/ExactAsioClock.h src/Audio/Asio/ExactAsioClock.cpp
git add -- src/Audio/Asio/AsioOutputBackend.cpp src/Audio/CMakeLists.txt
git add -- src/Config/ConfigCompiler.cpp
git add -- src/Patches/AbsoluteJudgement/AbsoluteJudgementRuntime.h
git add -- src/Patches/AbsoluteJudgement/AbsoluteJudgementRuntime.cpp
git add -- src/Patches/AbsoluteJudgement/JudgementClockResolver.h
git add -- src/Patches/AbsoluteJudgement/JudgementClockResolver.cpp
git add -- src/Patches/AbsoluteJudgement/JudgementScheduler.h
git add -- src/Patches/AbsoluteJudgement/JudgementScheduler.cpp
git add -- src/Patches/AbsoluteJudgement/JudgementSettings.h
git add -- tests/Audio/Wasapi/ExactWasapiClockCompatibilityTests.cpp
git add -- tests/Audio/Asio/ExactAsioClockTests.cpp tests/CMakeLists.txt
git commit -m "Refactor exact judgement timeline boundary"
```

---

### Task 2: Replace the ASIO-owned exact clock with one persistent logical clock

**Files:**

- Create: `src/Audio/Logical/LogicalPresentationClock.h`
- Create: `src/Audio/Logical/LogicalPresentationClock.cpp`
- Create: `src/Audio/Logical/LogicalPresentedOutputClock.h`
- Create: `src/Audio/Logical/LogicalPresentedOutputClock.cpp`
- Delete: `src/Audio/Asio/AsioLogicalTimeline.h`
- Delete: `src/Audio/Asio/AsioLogicalTimeline.cpp`
- Delete: `src/Audio/Asio/ExactAsioClock.h`
- Delete: `src/Audio/Asio/ExactAsioClock.cpp`
- Modify: `src/Audio/Asio/AsioClock.h`
- Modify: `src/Audio/Asio/AsioClock.cpp`
- Modify: `src/Audio/Asio/AsioOutputBackend.cpp`
- Modify: `src/Audio/Asio/CMakeLists.txt`
- Modify: `src/Audio/CMakeLists.txt`
- Rename: `tests/Audio/Asio/AsioLogicalTimelineTests.cpp` to
  `tests/Audio/Logical/LogicalPresentationClockTests.cpp`
- Rename: `tests/Audio/Asio/ExactAsioClockTests.cpp` to
  `tests/Audio/Logical/LogicalPresentedOutputClockTests.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Rewrite the logical-clock tests against the intended common API**

Retain the existing independent 44.1-kHz, 48-kHz, and `uint32_t` wrap oracles.
Add one captured-event immutability case:

```cpp
auto clock = LogicalPresentationClock::Create(
    41, 1'000, 44'100, 10'000'000);
const AbsoluteHostTime event{
    .qpc_ticks = 10'010'000,
    .multimedia_time_ms = 1'001,
};

const auto before = clock->Resolve(
    event, ExactClockResolveIntent::FinalizedTimestamp);
clock->ObserveNow(50'000);
const auto after = clock->Resolve(
    event, ExactClockResolveIntent::FinalizedTimestamp);

// Both must be exactly 441/10 logical frames.
```

Assert that both resolve intents return the same logical coordinate for this
provider, that provider period/latency are zero, and that there is no submitted
tail, anchor sequence, or physical position.

The presented-output test uses a fake `timeGetTime` action and asserts
`CurrentOutputFrame() == floor(L(now))`. It must not construct an ASIO session,
publish an anchor, or supply a submitted tail.

- [ ] **Step 2: Implement `LogicalPresentationClock`**

Merge the useful wrap-safe projection from `AsioLogicalTimeline` with the exact
provider. Its API is:

```cpp
enum class LogicalPresentationClockFailure : std::uint8_t
{
    InvalidConfiguration,
    WriterDeltaAmbiguous,
    TimestampAmbiguous,
    SnapshotUnavailable,
    NegativeCoordinate,
    ArithmeticOverflow,
};

class LogicalPresentationClock final : public ExactJudgementTimeline
{
public:
    [[nodiscard]] static std::shared_ptr<LogicalPresentationClock> Create(
        std::uint64_t timeline_generation,
        std::uint32_t origin_raw_ms,
        std::uint32_t logical_output_rate,
        std::int64_t qpc_frequency) noexcept;

    [[nodiscard]] std::expected<void, LogicalPresentationClockFailure>
    ObserveNow(std::uint32_t raw_ms) noexcept;
    [[nodiscard]] std::expected<gc::timing::CheckedRational,
                                LogicalPresentationClockFailure>
    ProjectMultimediaMilliseconds(std::uint32_t raw_ms) const noexcept;
    [[nodiscard]] std::expected<gc::timing::CheckedRational,
                                LogicalPresentationClockFailure>
    ProjectSystemTimeNanoseconds(std::uint64_t system_time_ns) const noexcept;
    [[nodiscard]] std::expected<std::uint64_t,
                                LogicalPresentationClockFailure>
    WholeFrameAt(std::uint32_t raw_ms) const noexcept;
    [[nodiscard]] std::expected<std::uint64_t,
                                LogicalPresentationClockFailure>
    WholeFrameAtSystemTime(std::uint64_t system_time_ns) const noexcept;

    [[nodiscard]] ExactJudgementTimelineResult Resolve(
        const gc::timing::AbsoluteHostTime&,
        ExactClockResolveIntent) const noexcept override;
    [[nodiscard]] ExactJudgementTimelineInfo info() const noexcept override;
    [[nodiscard]] ExactJudgementTimelineCounters counters() const noexcept override;
    void Invalidate() noexcept override;
};
```

Use the existing checked-rational equation:

```text
L(raw_ms) = unwrapped_ms_since_origin * logical_output_rate / 1000
```

`ObserveNow` only advances wrap bookkeeping; it never changes the origin or an
old result. Call it from the serialized control loop at every normal wake,
including while foreground and while suspended. Do not use callbacks as its
required writer and do not interpret its cadence as focus evidence.

- [ ] **Step 3: Implement the logical DirectSound-facing clock**

`LogicalPresentedOutputClock::CurrentOutputFrame()` reads the injected current
multimedia timestamp and returns `floor(L(now))` monotonically. It has no anchor
storage and no physical-session API. Pass it to `AudioRenderCore::Create` for
ASIO. Remove all calls that publish physical or detached logical presentation
anchors. Until Task 6 removes the old render sequencer, its callback may use
`WholeFrameAtSystemTime()` only to obtain a transitional render origin; that
value must not enter the cursor, judgement provider, or playback generation.
Keep WASAPI's existing `IPresentedOutputClock` implementation unchanged.

- [ ] **Step 4: Replace ASIO provider registration**

At initial ASIO logical-engine construction:

1. allocate one logical generation;
2. create one `LogicalPresentationClock` after adopting the driver's integral
   rate;
3. register that same object as the exact judgement timeline;
4. retain it through every physical release/recovery; and
5. invalidate/unregister it only during final backend teardown.

Do not pass ASIO period, output latency, callback runtime, submitted tail, or
physical generation into the clock constructor.

- [ ] **Step 5: Delete the obsolete ASIO exact-clock and logical-timeline files**

Remove their CMake entries and all includes. Remove
`AsioPresentedClockPublication` at the same time: `AsioClock` retains only the
physical sample-position tracker. The old render sequencer and submitted-tail
counter may remain internally until Task 6, but neither may gate the
DirectSound cursor or judgement.

- [ ] **Step 6: Run focused tests and commit**

```powershell
$asioVcvars = 'C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat'
cmd.exe /d /s /c "call `"$asioVcvars`" && set `"GC_ASIO_SDK_DIR=H:\gc\artifacts\ASIOSDK`" && cmake --preset msvc32-debug && cmake --build --preset msvc32-debug --target gc_logical_presentation_clock_tests gc_logical_presented_output_clock_tests gc_exact_wasapi_clock_compatibility_tests gc_audio && ctest --test-dir build-msvc32-debug -R `"^(LogicalPresentationClock|LogicalPresentedOutputClock|ExactWasapiClockCompatibility)$`" --output-on-failure"
git add -- src/Audio/Logical/LogicalPresentationClock.h
git add -- src/Audio/Logical/LogicalPresentationClock.cpp
git add -- src/Audio/Logical/LogicalPresentedOutputClock.h
git add -- src/Audio/Logical/LogicalPresentedOutputClock.cpp
git add -- src/Audio/Asio/AsioLogicalTimeline.h src/Audio/Asio/AsioLogicalTimeline.cpp
git add -- src/Audio/Asio/ExactAsioClock.h src/Audio/Asio/ExactAsioClock.cpp
git add -- src/Audio/Asio/AsioClock.h src/Audio/Asio/AsioClock.cpp
git add -- src/Audio/Asio/AsioOutputBackend.cpp src/Audio/Asio/CMakeLists.txt
git add -- src/Audio/CMakeLists.txt
git add -- tests/Audio/Asio/AsioLogicalTimelineTests.cpp
git add -- tests/Audio/Asio/ExactAsioClockTests.cpp
git add -- tests/Audio/Logical/LogicalPresentationClockTests.cpp
git add -- tests/Audio/Logical/LogicalPresentedOutputClockTests.cpp tests/CMakeLists.txt
git commit -m "Add persistent logical presentation clock"
```

---

### Task 3: Move playback history and judgement binding to logical generation

**Files:**

- Modify: `src/Audio/ExactAudioTime.h`
- Modify: `src/Audio/Mixer/AudioCursorTimeline.h`
- Modify: `src/Audio/Mixer/AudioCursorTimeline.cpp`
- Modify: `src/Audio/Mixer/MiniaudioMixer.h`
- Modify: `src/Audio/Mixer/MiniaudioMixer.cpp`
- Modify: `src/Audio/DirectSound/GameplayAudioCursorObservation.h`
- Modify: `src/Audio/DirectSound/GameplayAudioCursorObservation.cpp`
- Modify: `src/Audio/DirectSound/DirectSoundFacade.cpp`
- Modify: `src/Audio/Asio/AsioOutputBackend.cpp`
- Modify: `src/Audio/Wasapi/ExclusiveAudioEngine.cpp`
- Modify: `src/Patches/AbsoluteJudgement/JudgementClockResolver.h`
- Modify: `src/Patches/AbsoluteJudgement/JudgementClockResolver.cpp`
- Modify: `src/Patches/AbsoluteJudgement/JudgementStage.h`
- Modify: `src/Patches/AbsoluteJudgement/JudgementStage.cpp`
- Modify: `src/Patches/AbsoluteJudgement/JudgementScheduler.h`
- Modify: `src/Patches/AbsoluteJudgement/JudgementScheduler.cpp`
- Modify: `src/Patches/AbsoluteJudgement/AbsoluteJudgementDiagnostics.h`
- Modify: `src/Patches/AbsoluteJudgement/AbsoluteJudgementDiagnostics.cpp`
- Create: `tests/Patches/AbsoluteJudgement/LogicalJudgementTimelineTests.cpp`
- Create: `tests/Audio/DirectSound/LogicalCursorContinuityTests.cpp`
- Modify: `tests/Audio/Mixer/ExactHistoryIsolationTests.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Add the exact logical judgement test**

Use the real `LogicalPresentationClock`, `AudioCursorTimeline`, and
`JudgementClockResolver`. Build this exact scenario:

```cpp
constexpr std::uint64_t timeline_generation = 41;
constexpr std::uint64_t buffer_instance = 7;
auto timeline = LogicalPresentationClock::Create(
    timeline_generation, 1'000, 48'000, 10'000'000);
auto history = std::make_shared<AudioCursorTimeline>();
history->AssignBufferInstanceId(buffer_instance);
history->ConfigureExactPlaybackHistory(
    buffer_instance, timeline_generation);
history->ExpectExactPlaybackGeneration(1);
history->PublishExactMappedSpan(
    1,
    ExactPlaybackOrigin::Play,
    4'800, // L(1100 ms)
    0,
    48'000,
    44'100,
    9'600,
    false,
    0);
```

Bind at 1100 ms, resolve an input captured at 1150 ms, and independently assert:

```text
L(1150 ms) = 7200 frames
C = (7200 - 4800) / 48000 = 1/20 second
```

Run once with `GameTimeOffset = 0` and once with an arbitrary 7-ms
`GameTimeOffset` to prove it is added exactly once. Do not involve
`JudgTimeOffset`. Advance wrap bookkeeping and publish later logical-history
coverage; the already captured result must remain bit-identical.

- [ ] **Step 2: Rename physical identity out of logical history**

Make these semantic changes throughout the production types:

- `ExactPlaybackEpoch::endpoint_generation` ->
  `timeline_generation`;
- `exact_endpoint_generation()` -> `exact_timeline_generation()`;
- `GameplayAudioCursorObservation::endpoint_generation` ->
  `timeline_generation`;
- resolver/stage `endpoint_generation` -> `timeline_generation`; and
- endpoint-change failure names -> timeline-change failure names.

`ConfigureExactPlaybackHistory(buffer, timeline_generation)` is called once
when a gameplay candidate voice joins a logical backend. ASIO recovery must not
call it again, increment playback generation, close an epoch, or change the
history object.

For WASAPI, the provider's timeline generation remains equal to its accepted
endpoint generation, preserving the old lifetime rule.

- [ ] **Step 3: Refactor the resolver anchor**

The bound anchor must be exactly:

```cpp
struct JudgementStageClockAnchor final
{
    std::uint64_t stage_generation{};
    std::uint64_t timeline_generation{};
    std::uint64_t buffer_instance_id{};
    std::uint64_t playback_generation{};
    std::uint64_t logical_output_origin{};
    std::uint64_t source_origin{};
    std::uint32_t logical_output_rate{};
    std::uint32_t source_rate{};
    std::int32_t game_time_offset_ms{};
    std::shared_ptr<const gc::audio::ExactJudgementTimeline> timeline;
};
```

`Resolve(H)` first calls `timeline->Resolve(H, intent)`, then performs only:

```text
source_seconds =
    source_origin / source_rate
  + (logical_output_frame - logical_output_origin) / logical_output_rate
  + GameTimeOffset / 1000
```

For ASIO this provider is the persistent logical clock. No physical-session
generation, callback availability, submitted tail, or bridge state is checked.
Preserve WASAPI provider status handling and provisional/finalized intent.

- [ ] **Step 4: Make diagnostics use timeline language**

Rename judgement log fields to `timeline_generation`,
`logical_output_origin`, and `logical_output_rate`. For the logical ASIO
provider, period, output latency, publication sequence, and physical position
must be absent or zero; ASIO physical diagnostics are emitted by the audio
observer only.

- [ ] **Step 5: Add the DirectSound facade continuity test**

Drive a real `SecondarySoundBuffer` through an `IAudioEngineServices` harness
backed by the production render core and logical clock. Play and render enough
logical history ahead, stop physical-style output activity while logical time
continues inside that coverage, then seek, loop, reach natural end, and query
`GetCurrentPosition` and `GetStatus`. Expected source positions come from
source rate and logical elapsed time. The cursor, status, and drain result must
continue while no physical presentation event is supplied.

- [ ] **Step 6: Run focused tests and commit**

Expected: exact history isolation, logical judgement, DirectSound continuity,
and WASAPI characterization all pass.

```powershell
$asioVcvars = 'C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat'
cmd.exe /d /s /c "call `"$asioVcvars`" && set `"GC_ASIO_SDK_DIR=H:\gc\artifacts\ASIOSDK`" && cmake --preset msvc32-debug && cmake --build --preset msvc32-debug --target gc_logical_judgement_timeline_tests gc_directsound_logical_cursor_tests gc_exact_history_isolation_tests gc_exact_wasapi_clock_compatibility_tests gc_runtime_patches && ctest --test-dir build-msvc32-debug -R `"^(LogicalJudgementTimeline|DirectSoundLogicalCursor|ExactHistoryIsolation|ExactWasapiClockCompatibility)$`" --output-on-failure"
git add -- src/Audio/ExactAudioTime.h
git add -- src/Audio/Mixer/AudioCursorTimeline.h src/Audio/Mixer/AudioCursorTimeline.cpp
git add -- src/Audio/Mixer/MiniaudioMixer.h src/Audio/Mixer/MiniaudioMixer.cpp
git add -- src/Audio/DirectSound/GameplayAudioCursorObservation.h
git add -- src/Audio/DirectSound/GameplayAudioCursorObservation.cpp
git add -- src/Audio/DirectSound/DirectSoundFacade.cpp
git add -- src/Audio/Asio/AsioOutputBackend.cpp
git add -- src/Audio/Wasapi/ExclusiveAudioEngine.cpp
git add -- src/Patches/AbsoluteJudgement/JudgementClockResolver.h
git add -- src/Patches/AbsoluteJudgement/JudgementClockResolver.cpp
git add -- src/Patches/AbsoluteJudgement/JudgementStage.h
git add -- src/Patches/AbsoluteJudgement/JudgementStage.cpp
git add -- src/Patches/AbsoluteJudgement/JudgementScheduler.h
git add -- src/Patches/AbsoluteJudgement/JudgementScheduler.cpp
git add -- src/Patches/AbsoluteJudgement/AbsoluteJudgementDiagnostics.h
git add -- src/Patches/AbsoluteJudgement/AbsoluteJudgementDiagnostics.cpp
git add -- tests/Patches/AbsoluteJudgement/LogicalJudgementTimelineTests.cpp
git add -- tests/Audio/DirectSound/LogicalCursorContinuityTests.cpp
git add -- tests/Audio/Mixer/ExactHistoryIsolationTests.cpp tests/CMakeLists.txt
git commit -m "Key playback history to logical timeline"
```

---

### Task 4: Introduce the single-owner sequential logical render stream

**Files:**

- Create: `src/Audio/Mixer/LogicalRenderStream.h`
- Create: `src/Audio/Mixer/LogicalRenderStream.cpp`
- Modify: `src/Audio/Mixer/AudioRenderCore.h`
- Modify: `src/Audio/Mixer/AudioRenderCore.cpp`
- Modify: `src/Audio/CMakeLists.txt`
- Create: `tests/Audio/Mixer/LogicalRenderStreamTests.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Add ownership and exact-sequence tests**

Use a real `AudioRenderCore` and mixer voice. Assert:

1. a pump lease starts at tail 0;
2. render plans begin at 0, 192, 384, ... with
   `discontinuity_frames == 0`;
3. a second owner cannot plan while the pump lease is live;
4. abandoning a plan leaves the committed tail unchanged;
5. transfer pump -> bridge at the exact committed tail invalidates the old
   lease and allows the new lease to continue at that same tail; and
6. transfer bridge -> pump behaves identically.

The test must inspect the production mixer's published playback epoch and prove
that transfer does not create a playback generation or change output origin.

- [ ] **Step 2: Implement the stream contract**

```cpp
enum class LogicalRenderOwner : std::uint8_t
{
    Pump,
    AsioBridge,
};

struct LogicalRenderLease final
{
    LogicalRenderOwner owner{};
    std::uint64_t generation{};
    std::uint64_t acquired_tail{};
};

struct LogicalRenderPlan final
{
    MixerRenderTimeline timeline{};
    std::uint64_t committed_tail_after{};
    std::uint64_t lease_generation{};
    std::uint64_t claim_token{};
};

class LogicalRenderStream final
{
public:
    [[nodiscard]] static std::unique_ptr<LogicalRenderStream> Create(
        AudioRenderCore&) noexcept;
    [[nodiscard]] std::expected<LogicalRenderLease, LogicalRenderFailure>
    AcquireInitial(LogicalRenderOwner) noexcept;
    [[nodiscard]] std::expected<LogicalRenderLease, LogicalRenderFailure>
    Transfer(const LogicalRenderLease& from,
             LogicalRenderOwner to,
             std::uint64_t expected_tail) noexcept;
    [[nodiscard]] std::expected<LogicalRenderPlan, LogicalRenderFailure>
    BeginRender(const LogicalRenderLease&) noexcept;
    [[nodiscard]] AudioRenderBlock Render(
        const LogicalRenderPlan&) noexcept;
    [[nodiscard]] bool Commit(const LogicalRenderPlan&) noexcept;
    [[nodiscard]] bool Abandon(const LogicalRenderPlan&) noexcept;
    [[nodiscard]] std::uint64_t committed_tail() const noexcept;
};
```

`BeginRender` always creates
`MixerRenderTimeline{committed_tail, 0}` and advances by exactly the render
core's fixed period on commit. There is no target-coordinate skip, physical
origin, catch-up disposition, or detached/physical planning API.

Use the current atomic claim-token pattern so callback-side planning is
non-blocking. Transfer occurs only on the control thread after the old callback
owner is proven quiescent or before a new bridge is armed.

- [ ] **Step 3: Expose only the render operations needed by the stream**

Do not put lifecycle or ASIO concepts into `AudioRenderCore`. It remains the
owner of the mixer and fixed float block; `LogicalRenderStream` owns sequence
and leases.

- [ ] **Step 4: Run and commit**

```powershell
$asioVcvars = 'C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat'
cmd.exe /d /s /c "call `"$asioVcvars`" && set `"GC_ASIO_SDK_DIR=H:\gc\artifacts\ASIOSDK`" && cmake --preset msvc32-debug && cmake --build --preset msvc32-debug --target gc_logical_render_stream_tests gc_audio && ctest --test-dir build-msvc32-debug -R `"^LogicalRenderStream$`" --output-on-failure"
git add -- src/Audio/Mixer/LogicalRenderStream.h
git add -- src/Audio/Mixer/LogicalRenderStream.cpp
git add -- src/Audio/Mixer/AudioRenderCore.h src/Audio/Mixer/AudioRenderCore.cpp
git add -- src/Audio/CMakeLists.txt
git add -- tests/Audio/Mixer/LogicalRenderStreamTests.cpp tests/CMakeLists.txt
git commit -m "Add sequential logical render stream"
```

---

### Task 5: Build the final-output ASIO presentation rate matcher and bridge

**Files:**

- Create: `src/Audio/Asio/AsioPresentationRateMatcher.h`
- Create: `src/Audio/Asio/AsioPresentationRateMatcher.cpp`
- Create: `src/Audio/Asio/AsioPresentationBridge.h`
- Create: `src/Audio/Asio/AsioPresentationBridge.cpp`
- Modify: `src/Audio/CMakeLists.txt`
- Create: `tests/Audio/Asio/AsioPresentationBridgeTests.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Add a production-bridge impulse and drift test**

The test runs the real logical clock, render stream, rate matcher, and bridge
without IASIO. Feed callback records with monotonically stepped
`samplePosition` and independently calculated `systemTime`.

Run all four cases:

| Logical rate | Driver period | Physical oscillator error |
|---:|---:|---:|
| 44,100 Hz | 192 frames | +250 ppm |
| 44,100 Hz | 192 frames | -250 ppm |
| 48,000 Hz | 192 frames | +250 ppm |
| 48,000 Hz | 192 frames | -250 ppm |

Simulate at least 180 seconds and quantize callback `systemTime` to 1 ms to
cover the Xonar evidence. The current log showed a 192-frame/4-ms period,
1-ms timestamp quantum, and an uncorrected residual moving from roughly 7 ms
to 10 ms over a credit; the test intentionally uses much larger rate error so
convergence is observable quickly.

Independent assertions:

- every physical callback produces exactly one period after running commit;
- logical render origins are contiguous with no skip/repeat;
- phase remains within production policy after priming;
- ratio stays within policy and moves to the correct side of 1.0;
- FIFO underflow/overflow remain zero; and
- resolving fixed captured input timestamps through the logical provider is
  bit-identical across all four physical-clock simulations.

Put a single tagged impulse at a known logical source frame. Locate it in
physical output independently and assert driver latency plus resampler group
delay is applied once. The test must not call the bridge's phase-compensation
helper to compute the expected impulse location.

- [ ] **Step 2: Implement the preallocated final-output rate matcher**

Use one `ma_resampler` configured as:

```cpp
auto config = ma_resampler_config_init(
    ma_format_f32,
    2,
    logical_rate,
    driver_rate,
    ma_resample_algorithm_linear);
config.linear.lpfOrder = 0;
```

The initial logical and driver nominal rates are the same frozen integral
driver rate. Runtime correction uses
`ma_resampler_set_rate_ratio()`; it is never applied to per-voice converters.
Read `ma_resampler_get_input_latency()` and
`ma_resampler_get_output_latency()` once during construction.

Allocate before `ASIOStart`:

```cpp
input_capacity_frames =
    12 * period_frames + 2 * input_latency_frames + 2;
output_capacity_frames = period_frames;
maximum_priming_render_blocks_per_callback = 8;
```

The callback may perform at most the fixed refill count above and at most two
resampler calls for a ring wrap. No allocation or unbounded loop is permitted.
Compile the matcher and bridge into `gc_audio`, not `gc_asio`: they depend on
the logical mixer stream, while `gc_asio` remains the driver/callback transport
layer. Do not introduce a static-library dependency cycle.

- [ ] **Step 3: Implement the tracking controller**

Use this compile-time policy:

```cpp
inline constexpr double kMaximumRateCorrectionPpm = 1'000.0;
inline constexpr double kMaximumRateSlewPpmPerCallback = 25.0;
inline constexpr std::uint32_t kPhaseFilterCallbacks = 32;
inline constexpr std::uint32_t kPhaseCorrectionHorizonSeconds = 2;
inline constexpr std::uint32_t kMinimumPhaseEnvelopeMs = 20;
```

For callback `n`:

```text
target_n = L(systemTime_n)
         + driver_output_latency_in_logical_frames
         + resampler_source_phase_compensation

error_n = target_n - bridge_audible_source_phase

requested_ratio =
    1 + filtered(error_n) / (logical_rate * 2 seconds)
```

Clamp requested ratio to +/-1000 ppm and slew from the previous ratio by at
most 25 ppm per callback. The running phase envelope is:

```text
max(4 * period_frames, ceil(logical_rate * 20 ms))
+ 2 * timestamp_quantum_frames
+ resampler_input_latency_frames
```

Convert exact rational phase error to floating point only after checked bounds
prove it representable. This floating-point value controls audio resampling
only; it is never stored in judgement or playback history.

The miniaudio input-latency convention is represented by one named helper:

```cpp
next_input_phase =
    target_audible_phase + resampler_input_latency_frames;
```

The impulse test is the authority for this sign. If it fails, inspect the
miniaudio source/output phase convention; do not reverse the test to match an
implementation guess.

- [ ] **Step 4: Implement bridge modes and failures**

`AsioPresentationBridge` owns:

- one physical generation;
- `AsioClockTracker` structural sample-position validation;
- driver output latency and period;
- the rate matcher and its preallocated buffers;
- current logical source phase;
- a `LogicalRenderLease` only after handoff; and
- lock-free counters/extrema.

States are `Priming`, `Armed`, `Running`, `Quiescing`, and `Faulted`.

Priming always returns silence. It may reset the resampler, consume sequential
logical pre-roll, and hard-align phase. `Arm(lease, exact_tail)` accepts a
control-thread transfer only at the stream's exact committed tail. The first
audible callback atomically commits `Running` only after a complete aligned
period has been converted.

Running may only slew the ratio. It may not reset, seek, skip/repeat a block,
change logical history, or substitute ongoing silence. These latch a typed
fatal bridge fault:

- invalid callback/time/sample/buffer structure;
- phase envelope violation;
- ratio API/conversion failure;
- input starvation or FIFO overrun;
- non-finite output;
- lost/invalid render lease; or
- a render plan with nonzero discontinuity.

- [ ] **Step 5: Run and commit**

```powershell
$asioVcvars = 'C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat'
cmd.exe /d /s /c "call `"$asioVcvars`" && set `"GC_ASIO_SDK_DIR=H:\gc\artifacts\ASIOSDK`" && cmake --preset msvc32-debug && cmake --build --preset msvc32-debug --target gc_asio_presentation_bridge_tests gc_audio && ctest --test-dir build-msvc32-debug -R `"^AsioPresentationBridge$`" --output-on-failure"
git add -- src/Audio/Asio/AsioPresentationRateMatcher.h
git add -- src/Audio/Asio/AsioPresentationRateMatcher.cpp
git add -- src/Audio/Asio/AsioPresentationBridge.h
git add -- src/Audio/Asio/AsioPresentationBridge.cpp
git add -- src/Audio/CMakeLists.txt
git add -- tests/Audio/Asio/AsioPresentationBridgeTests.cpp tests/CMakeLists.txt
git commit -m "Add ASIO logical presentation bridge"
```

---

### Task 6: Integrate transactional render handoff and logical cursor into ASIO

**Files:**

- Modify: `src/Audio/Asio/AsioOutputBackend.h`
- Modify: `src/Audio/Asio/AsioOutputBackendInternal.h`
- Modify: `src/Audio/Asio/AsioOutputBackend.cpp`
- Modify: `src/Audio/Asio/AsioClock.h`
- Modify: `src/Audio/Asio/AsioClock.cpp`
- Delete: `src/Audio/Asio/AsioLogicalRenderSequencer.h`
- Delete: `src/Audio/Asio/AsioLogicalRenderSequencer.cpp`
- Delete: `src/Audio/Asio/AsioSubmittedOutputTail.h`
- Delete: `src/Audio/Asio/AsioSubmittedOutputTail.cpp`
- Delete: `tests/Audio/Asio/AsioLogicalRenderSequencerTests.cpp`
- Modify: `src/Audio/Asio/CMakeLists.txt`
- Modify: `src/Audio/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Replace persistent state**

Persistent ASIO backend fields become:

```cpp
std::unique_ptr<AudioRenderCore> render_core_;
std::unique_ptr<LogicalRenderStream> logical_render_stream_;
std::shared_ptr<LogicalPresentationClock> logical_clock_;
std::optional<LogicalRenderLease> pump_lease_;
std::uint64_t logical_timeline_generation_{};
bool logical_clock_registered_{};
```

Physical fields become:

```cpp
std::unique_ptr<AsioSession> session_;
std::unique_ptr<AsioCallbackRuntime> callback_runtime_;
std::unique_ptr<AsioPresentationBridge> presentation_bridge_;
std::uint64_t physical_session_generation_{};
```

Remove every physical/logical origin attachment field, submitted-tail object,
physical/logical presented-anchor object, attachment disposition, and
one-time-affine residual counter.

- [ ] **Step 2: Make the suspension pump sequential**

`AdvanceSilentRendering` projects `floor(L(now))` and renders/discards every
missing fixed block through its pump lease until:

```text
logical_render_stream.committed_tail
    > floor(L(now)) + one_period
```

Each control-loop iteration renders at most eight blocks, then rechecks focus,
shutdown, and faults before continuing. It never starts a plan at the target
and never publishes a synthetic presented anchor. A long scheduling gap is
caught up through sequential mixer renders; it is not represented as
`discontinuity_frames`.

- [ ] **Step 3: Implement startup/recovery bridge handoff**

For every physical attempt:

1. create a fresh bridge and all buffers before `Start`;
2. start callbacks in `Priming`; all returned buffers are silent;
3. require the existing finite proof of three valid callbacks;
4. while waiting, keep the pump advancing and service focus/shutdown/fault;
5. transfer pump -> bridge at the exact committed tail;
6. arm the bridge with that lease and tail;
7. wait for the bridge's first complete aligned audible period; and
8. only then mark the physical session and lifecycle `Running`.

If focus is lost at any point, quiesce callbacks, stop/close the session, take
the lease back from the bridge at its exact committed tail when necessary, and
enter `Suspended`. Never discard the logical clock, voices, history, or
registered judgement timeline.

- [ ] **Step 4: Make runtime callback rendering bridge-only**

`RenderAsioBlock` does only:

1. validate basic buffer index/readiness;
2. call `presentation_bridge_->Process(request, float_output)`;
3. convert the returned full float period to the driver format;
4. publish `outputReady` when supported; and
5. latch/signal a typed bridge fault.

It must not directly call `AudioRenderCore::Render`, attach origins, publish
judgement/cursor anchors, advance a physical submitted tail, or perform
lifecycle work.

- [ ] **Step 5: Return the logical DirectSound cursor**

`AsioOutputBackendState::CurrentOutputFrame()` returns the logical presented
clock's `floor(L(now))`. Before returning, require:

```text
logical_render_stream.committed_tail > current_logical_frame
```

If the stream is behind, latch a fatal logical-render fault and return
`nullopt`; do not freeze or clamp the cursor. This makes DirectSound status,
drain, natural end, ranking/demo, and second-song sequencing independent of
physical presentation.

- [ ] **Step 6: Delete the rejected model**

Delete `AsioLogicalRenderSequencer` and `AsioSubmittedOutputTail`. Confirm
`AsioClock` still contains only `AsioClockTracker` for session-local structural
validation. Remove the sequencer test because it asserts the now-rejected
one-time attachment and catch-up-gap behavior; do not retain compatibility
shims.

- [ ] **Step 7: Run all logical/bridge/cursor tests and commit**

```powershell
$asioVcvars = 'C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat'
cmd.exe /d /s /c "call `"$asioVcvars`" && set `"GC_ASIO_SDK_DIR=H:\gc\artifacts\ASIOSDK`" && cmake --preset msvc32-debug && cmake --build --preset msvc32-debug --target gc_logical_presentation_clock_tests gc_logical_presented_output_clock_tests gc_logical_judgement_timeline_tests gc_directsound_logical_cursor_tests gc_logical_render_stream_tests gc_asio_presentation_bridge_tests gc_audio gc_runtime_patches && ctest --test-dir build-msvc32-debug -R `"^(LogicalPresentationClock|LogicalPresentedOutputClock|LogicalJudgementTimeline|DirectSoundLogicalCursor|LogicalRenderStream|AsioPresentationBridge)$`" --output-on-failure"
git add -- src/Audio/Asio/AsioOutputBackend.h
git add -- src/Audio/Asio/AsioOutputBackendInternal.h
git add -- src/Audio/Asio/AsioOutputBackend.cpp
git add -- src/Audio/Asio/AsioClock.h src/Audio/Asio/AsioClock.cpp
git add -- src/Audio/Asio/AsioLogicalRenderSequencer.h
git add -- src/Audio/Asio/AsioLogicalRenderSequencer.cpp
git add -- src/Audio/Asio/AsioSubmittedOutputTail.h
git add -- src/Audio/Asio/AsioSubmittedOutputTail.cpp
git add -- src/Audio/Asio/CMakeLists.txt src/Audio/CMakeLists.txt
git add -- tests/Audio/Asio/AsioLogicalRenderSequencerTests.cpp tests/CMakeLists.txt
git commit -m "Route ASIO through logical presentation bridge"
```

---

### Task 7: Centralize focus lifecycle and bounded recovery policy

**Files:**

- Create: `src/Audio/Asio/AsioPhysicalSessionController.h`
- Create: `src/Audio/Asio/AsioPhysicalSessionController.cpp`
- Modify: `src/Audio/Asio/AsioOutputBackend.cpp`
- Modify: `src/Audio/Asio/AsioOutputBackend.h`
- Modify: `src/Audio/Asio/CMakeLists.txt`
- Create: `tests/Audio/Asio/AsioPhysicalSessionControllerTests.cpp`
- Modify: `tests/Audio/Asio/AsioForegroundStateTests.cpp` only if the
  production state API changes
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Add table-driven production-controller tests**

Feed the real controller explicit events and assert directives for:

1. focus loss during initial preparation;
2. focus loss during priming before render transfer;
3. focus loss after bridge transfer but before running commit;
4. focus loss while running;
5. focus loss during recovery preparation/priming;
6. foreground regain from suspended;
7. a clean pre-commit recovery failure followed by delays 1000 ms and 2000 ms;
8. a third clean pre-commit failure becoming fatal;
9. any committed-running bridge/callback fault becoming immediately fatal; and
10. shutdown interrupting either retry delay.

Assert the directive enum contains no alternate-backend/fallback action.
Retain the existing foreground test proving loss+regain before one consumer
read; do not duplicate it.

- [ ] **Step 2: Implement the controller as pure serialized policy**

```cpp
enum class AsioLifecycleState : std::uint8_t
{
    Starting,
    Running,
    Suspended,
    Recovering,
    Fatal,
    Stopping,
};

enum class AsioPhysicalCommitPhase : std::uint8_t
{
    None,
    Prepared,
    Priming,
    RenderLeaseTransferred,
    Running,
};

enum class AsioControlDirectiveKind : std::uint8_t
{
    ContinuePump,
    BeginPhysicalAttempt,
    ReleaseToSuspended,
    WaitRetry,
    CommitRunning,
    FailFatal,
    Stop,
};
```

The controller stores desired foreground state, consumed focus-loss generation,
attempt number, lifecycle state, and commit phase. It emits directives only;
`AsioOutputBackendState` performs driver and render actions and reports their
typed results back.

Recovery schedule:

```cpp
inline constexpr std::array<DWORD, 2> kRecoveryRetryDelaysMs{
    1'000,
    2'000,
};
inline constexpr std::uint32_t kMaximumRecoveryAttempts = 3;
```

Only a focus-recovery failure before `Running` with complete callback/buffer/
driver/render-lease cleanup is retryable. Initial startup failure is fatal.
Every structural fault after `Running` is fatal. Focus interruption returns to
`Suspended` without consuming a failure retry.

- [ ] **Step 3: Integrate it into the control loop**

All physical construction, start, priming waits, handoff, stop, cleanup, and
retry waits remain on the control thread. Waits are interruptible by focus,
fault, and shutdown events. Time schedules retry only; it never infers focus or
device loss.

Require the recovered driver's exact integral rate to equal the persistent
logical rate. Continue using the existing recovery rate-restore cleanup
contract. A changed rate, buffer size, latency, reset, resync, or sample-rate
message after running commit is fatal.

- [ ] **Step 4: Run and commit**

```powershell
$asioVcvars = 'C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat'
cmd.exe /d /s /c "call `"$asioVcvars`" && set `"GC_ASIO_SDK_DIR=H:\gc\artifacts\ASIOSDK`" && cmake --preset msvc32-debug && cmake --build --preset msvc32-debug --target gc_asio_physical_session_controller_tests gc_asio_foreground_state_tests gc_asio_presentation_bridge_tests gc_audio && ctest --test-dir build-msvc32-debug -R `"^(AsioPhysicalSessionController|AsioForegroundState|AsioPresentationBridge)$`" --output-on-failure"
git add -- src/Audio/Asio/AsioPhysicalSessionController.h
git add -- src/Audio/Asio/AsioPhysicalSessionController.cpp
git add -- src/Audio/Asio/AsioOutputBackend.h
git add -- src/Audio/Asio/AsioOutputBackend.cpp
git add -- src/Audio/Asio/CMakeLists.txt
git add -- tests/Audio/Asio/AsioPhysicalSessionControllerTests.cpp
git add -- tests/Audio/Asio/AsioForegroundStateTests.cpp tests/CMakeLists.txt
git commit -m "Centralize ASIO physical session lifecycle"
```

---

### Task 8: Replace diagnostics with clock-domain and bridge evidence

**Files:**

- Modify: `src/Audio/Asio/AsioOutputBackend.h`
- Modify: `src/Audio/Asio/AsioOutputBackend.cpp`
- Modify: `src/Audio/AudioPatch.cpp`
- Modify: `src/Patches/AbsoluteJudgement/AbsoluteJudgementDiagnostics.h`
- Modify: `src/Patches/AbsoluteJudgement/AbsoluteJudgementDiagnostics.cpp`
- Modify: `src/Patches/AbsoluteJudgement/JudgementScheduler.cpp`

- [ ] **Step 1: Replace rejected counters**

Remove or rename fields that imply the old model:

- `render_gap_frames`;
- `submitted_tail_publications` / physical submitted tail as cursor evidence;
- `detached_discarded_frames` when it includes skipped gaps;
- physical/logical attachment origins and disposition;
- `driver_timeline_residual_diagnostic_only`; and
- ASIO endpoint generation from judgement-stage records.

Add aggregate fields:

- logical timeline generation/rate/current frame/render tail;
- physical session generation/rate/period/output latency;
- priming callbacks and handoff logical tail;
- initial/max/final phase error in frames and nanoseconds;
- min/max/final rate ratio in ppm;
- bridge input high-water mark;
- underflow/overflow/conversion/phase-envelope counts;
- sequential pump-rendered frames;
- recovery count and current lifecycle state; and
- judgement resolved/pending/failure counts using timeline terminology.

- [ ] **Step 2: Keep the hot path bounded**

The callback updates lock-free counters and extrema only. Startup, 30-second
summary, lifecycle transition, fatal, stage-end, and shutdown logs format the
snapshots outside the callback. Do not add per-callback logging.

- [ ] **Step 3: Add static dependency audits**

These are review commands, not tests:

```powershell
rg -n "Asio(Render|Clock|Session|Callback|Submitted|sample_position|system_time|physical_session)" src/Patches/AbsoluteJudgement src/Audio/Mixer/AudioCursorTimeline.*
rg -n "Wasapi|DirectSoundSettings|alternate_backend" src/Audio/Asio src/Audio/AudioBackendController.cpp
rg -n "ma_resampler_set_rate_ratio|ma_resampler_process_pcm_frames" src/Audio/Asio
```

Expected:

- no ASIO physical type or field occurs in judgement/history;
- alternate backend appears only as an explicit false diagnostic or outside
  the configured-ASIO path;
- one final-output rate matcher exists, and no voice converter is retuned.

- [ ] **Step 4: Commit**

```powershell
$asioVcvars = 'C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat'
cmd.exe /d /s /c "call `"$asioVcvars`" && set `"GC_ASIO_SDK_DIR=H:\gc\artifacts\ASIOSDK`" && cmake --preset msvc32-debug && cmake --build --preset msvc32-debug --target gc_audio gc_runtime_patches iDmacDrv32"
git add -- src/Audio/Asio/AsioOutputBackend.h
git add -- src/Audio/Asio/AsioOutputBackend.cpp
git add -- src/Audio/AudioPatch.cpp
git add -- src/Patches/AbsoluteJudgement/AbsoluteJudgementDiagnostics.h
git add -- src/Patches/AbsoluteJudgement/AbsoluteJudgementDiagnostics.cpp
git add -- src/Patches/AbsoluteJudgement/JudgementScheduler.cpp
git commit -m "Report ASIO presentation bridge diagnostics"
```

---

### Task 9: Format, diagnose, build, and inspect the complete candidate

**Files:** Every changed `.h`/`.cpp`, CMake file, and the complete Git diff.

- [ ] **Step 1: Use CLion's formatter on changed C++ files only**

Reformat each changed C++ file through CLion MCP. Accept formatter changes in
the touched files. Do not run a repository-wide formatter and do not manually
restyle unrelated code.

- [ ] **Step 2: Run CLion diagnostics one file at a time**

For each changed C++ file:

1. open that file;
2. wait for its analysis to load;
3. request errors and warnings for that file;
4. fix or explicitly justify every new diagnostic; and
5. continue without closing the file or CLion.

Do not batch calls. Informational inspections are not errors.

- [ ] **Step 3: Run the focused Debug contract set**

```powershell
$asioVcvars = 'C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat'
cmd.exe /d /s /c "call `"$asioVcvars`" && set `"GC_ASIO_SDK_DIR=H:\gc\artifacts\ASIOSDK`" && cmake --preset msvc32-debug && cmake --build --preset msvc32-debug --target gc_exact_wasapi_clock_compatibility_tests gc_logical_presentation_clock_tests gc_logical_presented_output_clock_tests gc_logical_judgement_timeline_tests gc_directsound_logical_cursor_tests gc_logical_render_stream_tests gc_asio_presentation_bridge_tests gc_asio_physical_session_controller_tests gc_asio_foreground_state_tests gc_audio gc_runtime_patches iDmacDrv32 && ctest --test-dir build-msvc32-debug -R `"^(ExactWasapiClockCompatibility|LogicalPresentationClock|LogicalPresentedOutputClock|LogicalJudgementTimeline|DirectSoundLogicalCursor|LogicalRenderStream|AsioPresentationBridge|AsioPhysicalSessionController|AsioForegroundState)$`" --output-on-failure"
```

Expected: all focused independent contracts pass.

- [ ] **Step 4: Run complete x86 Debug build/test**

```powershell
$asioVcvars = 'C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat'
cmd.exe /d /s /c "call `"$asioVcvars`" && set `"GC_ASIO_SDK_DIR=H:\gc\artifacts\ASIOSDK`" && cmake --preset msvc32-debug && cmake --build --preset msvc32-debug && ctest --preset msvc32-debug -j 4 --output-on-failure"
```

- [ ] **Step 5: Run complete x86 Release build/test**

```powershell
$asioVcvars = 'C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat'
cmd.exe /d /s /c "call `"$asioVcvars`" && set `"GC_ASIO_SDK_DIR=H:\gc\artifacts\ASIOSDK`" && cmake --preset msvc32-release && cmake --build --preset msvc32-release && ctest --preset msvc32-release -j 4 --output-on-failure"
```

- [ ] **Step 6: Review static invariants**

Inspect:

- no physical ASIO dependency in the judgement/history include graph;
- no callback allocation, logging, blocking mutex, lifecycle action, or
  unbounded loop;
- no old attachment, detached anchor, submitted-tail cursor, gap skip, or
  callback interpolation code;
- no backend fallback;
- no fixed 48-kHz assumption;
- no new configuration timing knob;
- exact caller-output initialization and DirectSound COM ABI preserved; and
- every running fault either succeeds normally or reaches the fatal path.

- [ ] **Step 7: Inspect the artifact and repository state**

```powershell
git diff --check
git status --short --branch
git diff --stat
Get-Item -LiteralPath 'build-msvc32-release\dist\iDmacDrv32.dll' |
    Select-Object FullName, Length, LastWriteTime
Get-FileHash -Algorithm SHA256 -LiteralPath 'build-msvc32-release\dist\iDmacDrv32.dll'
```

Do not copy the DLL into `H:\gc`. Report the candidate hash. Runtime logs are
not attributable to this candidate until an explicitly authorized deployment
has made the deployed `H:\gc\iDmacDrv32.dll` hash identical.

- [ ] **Step 8: Commit accepted formatting or verification fixes**

```powershell
# If verification changed files, stage each task-owned path explicitly after
# reviewing git diff --name-only and the complete diff for that path.
git commit -m "Finalize ASIO logical presentation rewrite"
```

Skip this commit if verification produced no changes.

---

### Task 10: Runtime acceptance handoff

No automated result completes this task. After the user explicitly authorizes
deployment and the candidate/deployed hashes match, request runs in this order:

1. foreground startup and a complete two-song credit;
2. startup, shortly background, then foreground recovery;
3. focus loss/regain after startup before gameplay;
4. focus loss/regain in menus;
5. focus loss/regain during gameplay, accepting temporary silence;
6. an all-foreground two-song credit; and
7. accepted WASAPI comparison on the same physical listening chain with
   unchanged configuration offsets.

For each run, inspect the latest log only after artifact identity is proven.
Require:

- one persistent logical timeline generation for the backend lifetime;
- physical generations change only on explicit focus recovery;
- no fallback;
- bridge underflow/overflow, skipped/repeated logical intervals, and running
  hard resets all remain zero;
- phase and ratio remain within policy;
- judgement has no physical pending/failure reason;
- no loader-caused frame drop around callback/recovery work;
- stable judgement across songs without an ASIO-specific compensating offset;
- correct song end, ranking/demo, and next-credit sequence; and
- the user's acceptance of audio/judgement feel.

Song-end callback cost may be recorded but is not a gameplay judgement failure
by itself. Static tests and logs cannot replace the user's runtime judgement.

## Completion gate

Do not call the rewrite complete until:

1. captured input and authored notes meet in one exact logical source timeline;
2. the ASIO physical clock has no path into judgement, cursor, or logical
   playback generation;
3. every logical mixer interval is rendered exactly once by one owner;
4. final-output ASRC follows independent device drift at both 44.1 and 48 kHz;
5. priming is silent and recovery transfers at one exact logical tail;
6. focus loss preserves logical time/history/cursor/judgement;
7. retries are bounded to immediate, +1 s, +2 s and only before running commit;
8. every post-commit instability is fatal and no fallback exists;
9. focused and complete Debug/Release verification pass;
10. deployed/candidate identity is proven; and
11. the user accepts the full runtime matrix.

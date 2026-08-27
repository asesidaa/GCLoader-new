# ASIO Persistent Timeline Recovery Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.
>
> **Repository execution choice:** The user selected inline execution. Do not
> dispatch subagents and do not create a worktree. Work on the existing
> `fix/asio-lifecycle-recovery` branch.

**Goal:** Replace callback-defined ASIO judgement/recovery timing with one
driver-rate-aware logical timeline that survives focus suspension, while each
replaceable IASIO session attaches once and every post-`Start` instability
fails closed.

**Architecture:** Initial acquisition adopts the driver's current integral
sample rate and freezes the complete logical endpoint contract. A persistent
`AsioLogicalTimeline`, mixer, submitted-tail publication, presented clock,
exact provider, render sequencer, and endpoint generation survive focus loss.
Each physical IASIO session owns only driver/buffer/callback state and receives
one affine mapping from raw sample position into the persistent logical render
domain. Multimedia time advances logical time in every lifecycle state, but
only the coherent foreground snapshot authorizes suspension or recovery.

**Tech Stack:** Windows x86, C++23, Steinberg ASIO SDK, miniaudio, checked
rational timing, CMake/Ninja, MSVC 18 Insiders, CLion clangd/clang-tidy.

**Spec:**
`docs/superpowers/specs/2026-08-28-asio-persistent-timeline-recovery-design.md`

## Global Constraints

- The approved spec above supersedes the 2026-08-25 focus-recovery plan and
  all fixed-48-kHz/callback-anchor assumptions in older ASIO plans.
- Configured ASIO never instantiates or falls back to WASAPI, DirectSound, or
  another logical backend. The WASAPI backend and exact provider are unchanged.
- Initial acquisition calls `GetSampleRate`, accepts a finite positive exact
  whole-Hz `uint32_t` rate, and does not call `CanSampleRate` or `SetSampleRate`.
- Recovery must reproduce the frozen initial rate. Only a recovery attempt may
  call `CanSampleRate(R)`/`SetSampleRate(R)`, and it must restore the rate it
  observed before the attempt when that attempt changed it.
- Initial startup has no retry. Recovery permits one immediate attempt, one
  retry after 1 second, and one final retry after 2 additional seconds.
- Only a completely cleaned pre-`Start` recovery failure is retryable. Every
  failure after `Start` is invoked is fatal.
- Focus state alone selects suspension and recovery. No timeout, silence,
  callback interval, audio-clock residual, frame drop, or window movement may
  infer focus ownership.
- The logical timeline advances while physical audio is absent. Judgement time
  and voice state are never paused or recreated by focus recovery.
- Later ASIO `systemTime` values are diagnostic only. They cannot re-anchor,
  correct, slew, pause, or invalidate time merely because their residual is
  large.
- The callback path remains allocation-free, non-blocking, and exception-free.
  It never logs, opens/closes a driver, changes a rate, retries, or reads focus.
- Preserve value ownership for long-lived platform/callback action tables and
  observers. Do not reintroduce references to initialization-local callback
  tables.
- A callback contract fault zero-fills the current valid buffer, latches one
  typed fault, signals the control thread, and returns. Gameplay may not
  continue on an uncertain clock.
- Preserve the current coherent foreground publication. The existing
  `AsioForegroundStateTests` already proves loss followed by regain before one
  consumer read; do not add another focus test.
- Preserve the current uncommitted edits in
  `src/Audio/Asio/ExactAsioClock.cpp` and
  `tests/Audio/Asio/ExactAsioClockTests.cpp` until Task 3 replaces both files'
  callback-anchor model. Do not reset them, commit them separately, or retain
  their newest-past-anchor behavior.
- Do not modify judgement, input, WASAPI, configuration schema, ConfigGUI
  behavior, native song cadence, score behavior, or unrelated formatting.
- Do not add a fake IASIO suite, sleep-dependent timing test, source-text
  assertion, log-string test, test-only production hook, or simulated gameplay
  oracle.
- Do not deploy to or mutate `H:\gc`. Runtime files and logs there are evidence
  only until the user explicitly performs the runtime step.
- Never stop, terminate, kill, restart, close, or otherwise alter the lifetime
  of a process, service, IDE, application, window, or editor tab.
- If a required IDE/MCP operation is unavailable or unclear, stop and ask the
  user; do not invent a UI, keystroke, terminal, or process-lifecycle workaround.
- Use CLion MCP for source navigation, formatting, and diagnostics. For
  diagnostics, open one file, allow it to analyze, request its diagnostics, and
  leave it open before proceeding to the next file. Never batch diagnostics.
- Use the normal shell for Git, CMake, builds, tests, hashes, and diff review.
- Every build/test command must enter the x86 developer environment through
  `C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat`
  and set `GC_ASIO_SDK_DIR=H:\gc\artifacts\ASIOSDK`.
- If a preset cache is stale, use `cmake --fresh --preset <preset>`; do not
  edit/delete generated CMake state or restart CLion.
- Static/build evidence is not audible, focus-transfer, gameplay, or judgement
  acceptance. Only the user can provide those runtime observations.

## Approved Automated-Test Budget

Only these independently derived contracts justify automated changes:

1. A new pure `AsioLogicalTimelineTests` binary proves 44.1/48-kHz rational
   projection, exact 32-bit wrap handling, and snapshot-rebase invariance.
2. The existing `ExactAsioClockTests` binary is rewritten to prove direct
   timeline projection, submitted-tail gating, stable endpoint identity, and
   invalidation. Its callback-anchor tests are removed, not retained.
3. The existing `AsioLogicalRenderSequencerTests` binary is rewritten to prove
   one-time attachment, raw-sample-delta mapping, exclusive claims, and
   one-time wait/catch-up behavior.

There is no new ASIO-session fake. Rate acquisition, restoration, actual focus
transfer, audible recovery, multi-song stability, and judgement feel remain
runtime acceptance because a local fake would restate our implementation rather
than model the installed driver.

In the concrete test files, every `std::expected`, optional value, and allocated
pointer shown in the abbreviated snippets below must be checked and returned
from before dereference. A failed expectation must report a test failure, not
turn into a test crash.

## File Responsibility Map

- Create `src/Audio/Asio/AsioLogicalTimeline.h/.cpp`: immutable-origin,
  generic-rate, checked rational projection plus control-thread wrap snapshot.
- Create `src/Audio/Asio/AsioSubmittedOutputTail.h/.cpp`: the one shared,
  monotonic committed-tail publication consumed by both logical clocks.
- Create `tests/Audio/Asio/AsioLogicalTimelineTests.cpp`: the hand-derived
  rational/wrap oracle.
- Modify `src/Audio/Asio/CMakeLists.txt`: compile the new primitives and link
  `gc_asio` to `gc_timing`.
- Modify `tests/CMakeLists.txt`: register only the new focused timeline test;
  keep the existing exact, sequencer, and foreground targets.
- Modify `src/Audio/Asio/AsioSession.h/.cpp` (rate negotiation around current
  lines 217-275 and cleanup around 551-604): explicit adopt/require policies,
  preparation cleanup proof, and verified restoration.
- Modify `src/Audio/Asio/AsioCapabilityProbe.cpp:67-87`: use adopt-current
  inspection without rate mutation.
- Modify `src/Audio/Asio/AsioCallbackRuntime.h/.cpp` (constructor/prepare and
  dispatch validation): retain the expected session rate, validate against it
  instead of 48 kHz, and separate required raw sample position from the first
  attachment's optional timestamp observation.
- Modify `src/Audio/Asio/AsioClock.h/.cpp`: keep physical sample continuity
  validation, remove later timestamp authority, and adapt the presented clock
  to the persistent timeline/shared tail.
- Replace the callback ring in `src/Audio/Asio/ExactAsioClock.h/.cpp` with a
  persistent-timeline/shared-tail adapter.
- Replace `tests/Audio/Asio/ExactAsioClockTests.cpp` with the persistent exact
  projection/tail oracle, superseding the dirty newest-past-anchor test.
- Modify `src/Audio/Asio/AsioLogicalRenderSequencer.h/.cpp`: absolute targets,
  one-time physical attachment, and transactionally committed render claims.
- Replace `tests/Audio/Asio/AsioLogicalRenderSequencerTests.cpp` with the
  attachment/delta/wait/catch-up oracle.
- Modify `src/Audio/Asio/AsioOutputBackend.cpp` (initialization around
  862-1183, lifecycle around 1374-2140, render paths around 2315-2605, state
  around 2860-2925): stage startup around driver rate/latency, integrate the
  persistent lifetime, and preserve the strict fatal boundary.
- Modify `src/Audio/Asio/AsioOutputBackend.h`: logical/physical lifecycle and
  bounded aggregate diagnostic records.
- Modify `src/Audio/AudioPatch.cpp` (ASIO startup/counter/lifecycle formatting):
  emit the new logical-contract, physical-generation, restoration, attachment,
  tail, and residual fields; remove anchor terminology.

## Contract-to-Task Map

| Approved contract | Implemented in |
|---|---|
| Driver-current initial rate and frozen recovery rate | Tasks 2 and 5 |
| Exact process-lifetime multimedia timeline | Tasks 1 and 3 |
| Persistent endpoint/exact-provider identity | Tasks 3 and 5 |
| One-time physical sample-position attachment | Tasks 4 and 5 |
| Absolute detached rendering and one-time handoff | Tasks 4 and 5 |
| Focus-only bounded recovery, post-`Start` fatality | Task 5 |
| Transition-oriented bounded diagnostics | Task 6 |
| Static proof and ordered real-game acceptance | Task 7 |

---

### Task 1: Add the Persistent Logical Timeline and Shared Tail

**Files:**

- Create: `src/Audio/Asio/AsioLogicalTimeline.h`
- Create: `src/Audio/Asio/AsioLogicalTimeline.cpp`
- Create: `src/Audio/Asio/AsioSubmittedOutputTail.h`
- Create: `src/Audio/Asio/AsioSubmittedOutputTail.cpp`
- Create: `tests/Audio/Asio/AsioLogicalTimelineTests.cpp`
- Modify: `src/Audio/Asio/CMakeLists.txt:1-27`
- Modify: `tests/CMakeLists.txt:1-36`

**Interfaces:**

```cpp
enum class AsioLogicalTimelineFailure : std::uint8_t
{
    InvalidConfiguration,
    WriterDeltaAmbiguous,
    TimestampAmbiguous,
    SnapshotUnavailable,
    NegativeCoordinate,
    ArithmeticOverflow,
};

class AsioLogicalTimeline final
{
public:
    [[nodiscard]] static std::shared_ptr<AsioLogicalTimeline> Create(
        std::uint32_t origin_raw_ms,
        std::uint32_t output_sample_rate) noexcept;

    [[nodiscard]] std::expected<void, AsioLogicalTimelineFailure>
    AdvanceNow(std::uint32_t observed_raw_ms) noexcept;
    [[nodiscard]] std::expected<gc::timing::CheckedRational,
                                AsioLogicalTimelineFailure>
    ProjectMultimediaMilliseconds(std::uint32_t raw_ms) const noexcept;
    [[nodiscard]] std::expected<gc::timing::CheckedRational,
                                AsioLogicalTimelineFailure>
    ProjectSystemTimeNanoseconds(std::uint64_t system_time_ns) const noexcept;
    [[nodiscard]] std::expected<std::uint64_t,
                                AsioLogicalTimelineFailure>
    WholePresentedFrameAt(std::uint32_t raw_ms) const noexcept;
    [[nodiscard]] std::expected<std::uint64_t,
                                AsioLogicalTimelineFailure>
    WholePresentedFrameAtSystemTime(std::uint64_t system_time_ns) const noexcept;

    [[nodiscard]] std::uint32_t origin_raw_ms() const noexcept;
    [[nodiscard]] std::uint32_t output_sample_rate() const noexcept;
};
```

```cpp
struct AsioSubmittedOutputTailSnapshot final
{
    std::uint64_t submitted_output_tail{};
    std::uint64_t publication_sequence{};
    bool stable{};
    bool available{};
    bool valid{};
};

class AsioSubmittedOutputTail final
{
public:
    [[nodiscard]] bool Publish(std::uint64_t submitted_output_tail) noexcept;
    [[nodiscard]] AsioSubmittedOutputTailSnapshot Read() const noexcept;
    void Invalidate() noexcept;
};
```

- [ ] **Step 1: Invoke the test-driven-development skill for the three approved
  test seams**

Use `superpowers:test-driven-development` before writing the first test. Its
red/green discipline applies only to Tasks 1, 3, and 4. It does not authorize
extra tests outside the approved budget above.

- [ ] **Step 2: Register and write the failing rational/wrap oracle**

Add `gc_asio_logical_timeline_tests` to `tests/CMakeLists.txt`, link it to
`gc_asio`, and register `NAME AsioLogicalTimeline`. Do not add the new
production `.cpp` files to `gc_asio` until after the red build below.

The test must derive expectations independently:

```cpp
void ProjectsFromOneOriginWithoutFractionalDrift()
{
    auto timeline = AsioLogicalTimeline::Create(1'000, 44'100);
    Expect(timeline != nullptr, "44.1-kHz timeline is valid");
    if (!timeline)
    {
        return;
    }

    ExpectRational(timeline->ProjectMultimediaMilliseconds(1'001),
                   441, 10, "1 ms is exactly 441/10 frames");
    ExpectRational(timeline->ProjectMultimediaMilliseconds(1'010),
                   441, 1, "10 ms is exactly 441 frames");
    ExpectRational(timeline->ProjectMultimediaMilliseconds(2'000),
                   44'100, 1, "1000 ms is exactly 44100 frames");

    for (std::uint32_t raw = 1'001; raw <= 2'000; ++raw)
    {
        const auto direct = CheckedRational::Create(
            static_cast<std::int64_t>(raw - 1'000) * 44'100,
            1'000);
        ExpectEquivalent(
            timeline->ProjectMultimediaMilliseconds(raw),
            direct,
            "repeated queries equal direct origin projection");
    }

    auto forty_eight = AsioLogicalTimeline::Create(50, 48'000);
    ExpectRational(forty_eight->ProjectMultimediaMilliseconds(51),
                   48, 1, "48-kHz projection remains exact");
}

void WrapAndSnapshotAdvanceDoNotRebaseAnOldEvent()
{
    constexpr auto origin = (std::numeric_limits<std::uint32_t>::max)() - 5;
    auto timeline = AsioLogicalTimeline::Create(origin, 44'100);

    Expect(timeline->AdvanceNow(3).has_value(),
           "writer crosses UINT32_MAX exactly");
    const auto before = timeline->ProjectMultimediaMilliseconds(origin + 3);
    ExpectRational(before, 1'323, 10,
                   "three pre-wrap milliseconds are 1323/10 frames");

    Expect(timeline->AdvanceNow(20).has_value(),
           "writer advances the stable snapshot after wrap");
    const auto after = timeline->ProjectMultimediaMilliseconds(origin + 3);
    ExpectEquivalent(after, before,
                     "later wrap bookkeeping cannot rebase an old event");
}
```

Build the test target before adding the production headers. Expected red result:
compilation fails because `Audio/Asio/AsioLogicalTimeline.h` does not exist. An
unrelated configure/compiler failure is not the red proof.

- [ ] **Step 3: Implement exact unwrapping and rational projection**

Create `AsioLogicalTimeline.h/.cpp`, add `AsioLogicalTimeline.cpp` to
`gc_asio`, and link `gc_asio PUBLIC gc_timing`.

Use one control-thread writer and a versioned stable atomic snapshot
`{observed_raw_ms, observed_unwrapped_ms}`. `AdvanceNow` must:

1. calculate the unsigned 32-bit forward delta from the writer's previous raw
   value;
2. reject deltas greater than `INT32_MAX` as `WriterDeltaAmbiguous`;
3. checked-add that delta to the unwrapped `uint64_t` value; and
4. publish raw and unwrapped members under an odd/even version sequence.

Readers make at most three stable-snapshot attempts. For an event raw tick,
bit-cast `uint32_t(raw - observed_raw)` to `int32_t`; reject exactly
`0x80000000` as ambiguous; checked-add that signed delta to observed unwrapped
time; then calculate only from the immutable origin:

```cpp
P(t) = CheckedRational::Whole(unwrapped_ms)
           .Multiply(output_sample_rate, 1'000)
```

For ASIO nanoseconds, retain the remainder:

```cpp
const auto whole_ms = system_time_ns / 1'000'000;
const auto raw_ms = static_cast<std::uint32_t>(whole_ms);
const auto remainder_ns = system_time_ns % 1'000'000;

P(systemTime) = P(raw_ms) + remainder_ns * R / 1'000'000'000
```

Return a whole frame only after the rational coordinate is nonnegative and
`Floor()` succeeds. Do not maintain an incrementally rounded frame cursor.

- [ ] **Step 4: Implement the shared monotonic submitted tail**

Create `AsioSubmittedOutputTail.h/.cpp` and add its `.cpp` to `gc_asio`.
`Publish` accepts only a nonzero tail strictly greater than the previously
published tail. It publishes one coherent tail/sequence snapshot and returns
false on regression, sequence overflow, or invalidation. `Read` distinguishes
stable publication, transient read collision, never-published state, and
explicit invalidation. The callback and detached paths will share this
instance, but the sequencer's exclusive claim remains the primary single-writer
guarantee.

Do not put time, focus, IASIO, mixer, endpoint generation, or retry policy in
either new primitive.

- [ ] **Step 5: Format, build, and run the green timeline test**

Use CLion's formatter on the four new production files and the test. Then run:

```powershell
$asioVcvars = 'C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat'
cmd.exe /d /s /c "call `"$asioVcvars`" && set `"GC_ASIO_SDK_DIR=H:\gc\artifacts\ASIOSDK`" && cmake --preset msvc32-debug && cmake --build --preset msvc32-debug --target gc_asio_logical_timeline_tests && ctest --test-dir build-msvc32-debug -R `"^AsioLogicalTimeline$`" --output-on-failure"
```

Expected: the target builds and the hand-derived 44.1/48-kHz and wrap cases
pass.

- [ ] **Step 6: Commit the pure logical primitives**

```powershell
git add -- src/Audio/Asio/AsioLogicalTimeline.h src/Audio/Asio/AsioLogicalTimeline.cpp src/Audio/Asio/AsioSubmittedOutputTail.h src/Audio/Asio/AsioSubmittedOutputTail.cpp src/Audio/Asio/CMakeLists.txt tests/Audio/Asio/AsioLogicalTimelineTests.cpp tests/CMakeLists.txt
git commit -m "Add persistent ASIO logical timeline"
```

### Task 2: Adopt the Driver's Initial Rate and Restore the Frozen Rate on Recovery

**Files:**

- Modify: `src/Audio/Asio/AsioSession.h:17-64`
- Modify: `src/Audio/Asio/AsioSession.cpp:21-275,551-604`
- Modify: `src/Audio/Asio/AsioCapabilityProbe.cpp:67-87`
- Modify: `src/Audio/Asio/AsioCallbackRuntime.h:58-61,130-136,190-198`
- Modify: `src/Audio/Asio/AsioCallbackRuntime.cpp:290-370,720-765`
- Modify: `src/Audio/Asio/AsioOutputBackend.cpp:514-536,862-1183,2373-2390`

**Interfaces:**

```cpp
struct AsioAdoptCurrentRate final {};

struct AsioRequireFrozenRate final
{
    std::uint32_t sample_rate{};
};

using AsioSampleRatePolicy =
    std::variant<AsioAdoptCurrentRate, AsioRequireFrozenRate>;

struct AsioSessionPreparationFailure final
{
    AsioFailure failure;
    bool cleanup_complete{};
};

struct AsioSessionCleanupReport final
{
    bool stop_complete{};
    bool buffers_disposed{};
    bool sample_rate_restoration_attempted{};
    bool sample_rate_restored{};
};

struct AsioLogicalOutputContract final
{
    AsioDriverRegistration registration;
    std::uint32_t sample_rate{};
    std::uint32_t period_frames{};
    std::uint32_t output_base_channel{};
    std::array<ASIOSampleType, 2> channel_types{};
    std::uint32_t output_latency_frames{};
    bool output_ready_supported{};
};
```

`AsioSession::Prepare` accepts an `AsioSampleRatePolicy` and returns
`std::expected<std::unique_ptr<AsioSession>,
AsioSessionPreparationFailure>`. `cleanup_report()` exposes bounded facts after
`Close`; it does not perform logging.

- [ ] **Step 1: Replace forced 48-kHz negotiation with explicit policies**

After `GetSampleRate`, validate that the value is finite, positive, equal to its
mathematical truncation, and no greater than `UINT32_MAX`. Preserve
`report_.original_sample_rate` as the value observed on open.

For `AsioAdoptCurrentRate`:

- set `report_.sample_rate` to the validated current rate;
- do not call `CanSampleRate`;
- do not call `SetSampleRate`; and
- create no restoration obligation.

For `AsioRequireFrozenRate{R}`:

- reject `R == 0`;
- if current `C == R`, set the report without mutation;
- otherwise call `CanSampleRate(R)`, establish a restoration obligation for
  `C`, call `SetSampleRate(R)`, and verify a second `GetSampleRate` equals `R`;
- if `SetSampleRate` reports failure, query the current rate and clear the
  restoration obligation only when that query proves the driver stayed at
  `C`; and
- never update the frozen logical rate from the recovery report.

Use rate text derived from the value; remove `kAsioSampleRate` and every
hard-coded “48000 Hz” failure message.

- [ ] **Step 2: Make preparation cleanup part of the returned contract**

When `PrepareDriver` fails after constructing an `AsioSession`, call `Close`
explicitly before returning. Return `cleanup_complete=true` only if that close
succeeds. When cleanup fails, make the cleanup error primary and retain the
acquisition error as secondary diagnostic text.

Rewrite `Close` as an ordered transaction:

1. stop only when started;
2. dispose buffers only after stop succeeds;
3. restore the remembered pre-attempt rate only after buffers are disposed;
4. query and verify the restored rate exactly; and
5. release the driver only after the ordered cleanup completes.

Do not set a successful cleanup flag optimistically. An inability to stop,
dispose, restore, or verify restoration remains visible so the backend can make
the recovery failure fatal.

- [ ] **Step 3: Make preflight inspection non-mutating**

Change `ProbeAsioCapabilities` to pass `AsioAdoptCurrentRate{}`. On preparation
failure, return `prepared.error().failure`. ConfigGUI/runtime preflight may read
and report 44.1 or 48 kHz but may not change it.

- [ ] **Step 4: Make callback rate validation generic**

Store `timing_config.sample_rate` in `AsioCallbackRuntime` as
`expected_sample_rate_`. Pass it through the private constructor and compare a
valid callback `sampleRate` against `static_cast<double>(expected_sample_rate_)`
instead of `48'000.0`. Keep `ComputeExpectedPeriodNanoseconds` based on exact
`B/R` dimensions.

No new unit test is added: a local callback fake would only echo the configured
number. The installed driver's actual callback rate remains runtime evidence.

- [ ] **Step 5: Stage backend construction after the initial rate is known**

Introduce:

```cpp
enum class PhysicalSessionPurpose : std::uint8_t
{
    InitialStartup,
    FocusRecovery,
};
```

Remove the value-constructed `AsioLogicalRenderSequencer{B, 48'000}` from the
state constructor. `PreparePhysicalSession(InitialStartup)` passes
`AsioAdoptCurrentRate{}`; after it obtains the prepared session, freeze
`logical_output_sample_rate_` from the validated report and construct the
rate-dependent render core/callback state with that value. Convert the current
sequencer member to `std::unique_ptr` and, until Task 4 removes rate from that
API, construct it once as
`AsioLogicalRenderSequencer{request_.buffer_frames,
logical_output_sample_rate_}`.

`PreparePhysicalSession(FocusRecovery)` passes
`AsioRequireFrozenRate{logical_output_sample_rate_}` and never reconstructs the
mixer. Thread the frozen rate through:

- `AudioRenderCore::Create(request_.buffer_frames,
  logical_output_sample_rate_, std::move(mixer_allocations_),
  std::move(clock), &mixer_result)`;
- `AsioCallbackRuntime::Prepare(*this, legacy,
  {request_.buffer_frames, logical_output_sample_rate_},
  actions_.callback_runtime_actions)`;
- the existing exact-clock construction until Task 3 replaces it;
- `output_sample_rate_`; and
- `SilentPollIntervalMs = max(1, ceil(B * 1000 / R))` using checked `uint64_t`
  arithmetic.

Freeze and compare registration identity, `R`, `B`, selected channel indices
and types, `L`, and `outputReady` capability. A recovery mismatch does not
update the logical contract.

- [ ] **Step 6: Classify cleanup proof at the recovery boundary**

Initial preparation failure remains fatal regardless of
`cleanup_complete`. During recovery, a preparation/contract/buffer/callback
failure is `retryable_before_start` only after `ClosePhysicalSession` succeeds
and the session/callback pointers are empty. A failed stop, join, uninstall,
dispose, rate restore, or restoration verification is fatal.

Do not change the existing one-immediate/1-second/2-second retry schedule and do
not make `Start` or stability failure retryable.

- [ ] **Step 7: Format and compile the generic-rate boundary**

Use CLion formatting on the changed files. Build `gc_asio`, `gc_audio`, and
`ConfigGUI`; run the existing ASIO tests. Expected: compilation/tests pass at
the existing test rates. This is not 44.1-kHz driver acceptance yet.

- [ ] **Step 8: Commit the rate-ownership transaction**

```powershell
git add -- src/Audio/Asio/AsioSession.h src/Audio/Asio/AsioSession.cpp src/Audio/Asio/AsioCapabilityProbe.cpp src/Audio/Asio/AsioCallbackRuntime.h src/Audio/Asio/AsioCallbackRuntime.cpp src/Audio/Asio/AsioOutputBackend.cpp
git commit -m "Adopt driver-owned ASIO sample rate"
```

### Task 3: Replace Callback Anchors with Persistent Timeline Projection

**Files:**

- Modify: `src/Audio/Asio/ExactAsioClock.h`
- Replace: `src/Audio/Asio/ExactAsioClock.cpp`
- Replace: `tests/Audio/Asio/ExactAsioClockTests.cpp`
- Modify: `src/Audio/Asio/AsioClock.h:46-88`
- Modify: `src/Audio/Asio/AsioClock.cpp:111-242`
- Modify: `src/Audio/Asio/AsioOutputBackend.cpp` (logical construction,
  render publication, teardown, and exact counters)

**Interfaces:**

```cpp
class ExactAsioClock final : public ExactOutputClock
{
public:
    [[nodiscard]] static std::shared_ptr<ExactAsioClock> Create(
        std::uint64_t endpoint_generation,
        std::shared_ptr<const AsioLogicalTimeline> timeline,
        std::shared_ptr<const AsioSubmittedOutputTail> submitted_tail,
        std::int64_t qpc_frequency,
        std::uint32_t period_frames,
        std::uint32_t output_latency_frames) noexcept;

    [[nodiscard]] ExactOutputClockResult Resolve(
        const gc::timing::AbsoluteHostTime&,
        ExactClockResolveIntent) const noexcept override;
    [[nodiscard]] ExactOutputClockInfo info() const noexcept override;
    [[nodiscard]] ExactOutputClockCounters counters() const noexcept override;
    void Invalidate() noexcept override;
};
```

```cpp
class AsioPresentedClockPublication final : public IPresentedOutputClock
{
public:
    AsioPresentedClockPublication(
        AsioClockNowActions,
        std::shared_ptr<const AsioLogicalTimeline>,
        std::shared_ptr<const AsioSubmittedOutputTail>) noexcept;

    [[nodiscard]] std::optional<std::uint64_t>
    CurrentOutputFrame() noexcept override;
    void Invalidate() noexcept override;
};
```

- [ ] **Step 1: Replace the dirty anchor test with a failing persistent-clock
  oracle**

Do not revert the two dirty files. Replace the current newest-past-anchor tests
in place. The new test constructs one timeline and one shared submitted tail:

```cpp
void ResolvesDirectlyAndOnlyBehindCommittedTail()
{
    auto timeline = AsioLogicalTimeline::Create(1'000, 44'100);
    auto tail = std::make_shared<AsioSubmittedOutputTail>();
    auto clock = ExactAsioClock::Create(
        7, timeline, tail, 10'000'000, 192, 384);

    const auto before_commit = clock->Resolve(
        AtMillisecond(1'001),
        ExactClockResolveIntent::FinalizedTimestamp);
    Expect(before_commit.status == ExactClockStatus::Pending,
           "projection waits only for committed render evidence");

    Expect(tail->Publish(1'000), "first submitted tail commits");
    const auto finalized = clock->Resolve(
        AtMillisecond(1'001),
        ExactClockResolveIntent::FinalizedTimestamp);
    ExpectFrame(finalized, 441, 10,
                "finalized timestamp uses the immutable timeline");

    const auto horizon = clock->Resolve(
        AtMillisecond(1'001),
        ExactClockResolveIntent::ProvisionalHorizon);
    ExpectSameFrame(horizon, finalized,
                    "intent does not select different callback evidence");

    Expect(timeline->AdvanceNow(2'000).has_value(),
           "writer advances the wrap snapshot");
    const auto after_snapshot = clock->Resolve(
        AtMillisecond(1'001),
        ExactClockResolveIntent::FinalizedTimestamp);
    ExpectSameFrame(after_snapshot, finalized,
                    "later observations cannot rewrite a finalized event");
}

void TailAndInvalidationAreExplicit()
{
    auto timeline = AsioLogicalTimeline::Create(100, 48'000);
    auto tail = std::make_shared<AsioSubmittedOutputTail>();
    auto clock = ExactAsioClock::Create(
        7, timeline, tail, 10'000'000, 192, 384);

    Expect(tail->Publish(48), "one millisecond of output commits");
    const auto at_tail = clock->Resolve(
        AtMillisecond(101),
        ExactClockResolveIntent::FinalizedTimestamp);
    Expect(at_tail.status == ExactClockStatus::Pending,
           "a frame at the exclusive submitted tail is pending");
    Expect(at_tail.endpoint_generation == 7,
           "logical endpoint identity is persistent");
    Expect(at_tail.anchor_sequence == 1,
           "tail publication sequence remains observable");
    Expect(!at_tail.anchor_endpoint_position.has_value(),
           "there is no physical callback anchor");
    Expect(clock->counters().history_lost_queries == 0,
           "a non-existent callback history cannot be lost");

    clock->Invalidate();
    const auto invalid = clock->Resolve(
        AtMillisecond(100),
        ExactClockResolveIntent::FinalizedTimestamp);
    Expect(invalid.status == ExactClockStatus::Discontinuous,
           "explicit final invalidation is visible");
}
```

Expected red result: compilation fails because the new `Create` signature and
shared-tail clock do not exist. Delete the old bracket/newest-past/future-anchor
cases; do not keep them as extra coverage.

- [ ] **Step 2: Reduce `ExactAsioClock` to a timeline/tail adapter**

Delete `ExactAsioAnchor`, `Slot`, capacity/retention logic, callback history,
writer previous-anchor state, and `Publish`.

`Resolve` must:

1. reject an unknown resolve intent or explicit invalidation as
   `Discontinuous`;
2. read one stable submitted-tail snapshot;
3. return `TemporarilyUnavailable` for a transient tail read collision,
   `Discontinuous` for an invalidated tail, or `Pending` if no tail has
   committed;
4. project `timestamp.multimedia_time_ms` directly through
   `AsioLogicalTimeline`;
5. return `TemporarilyUnavailable` only for a transient stable-snapshot read
   collision, and `Discontinuous` for ambiguity/arithmetic/logical failure;
6. return `Pending` when the rational frame is greater than or equal to the
   committed tail; and
7. otherwise return `Resolved` with the rational frame, persistent endpoint
   generation, tail publication sequence, and no callback anchor position.

Both resolve intents use the same path. `info().output_sample_rate` comes from
the timeline. `counters().publication_count` comes from the shared tail;
`history_lost_queries` remains zero.

- [ ] **Step 3: Make the presented cursor read the same timeline and tail**

Delete `Publish`, `PublishContinuityAnchor`, callback anchor storage, and the
hard-coded 48-frames/ms projection from `AsioPresentedClockPublication`.

`CurrentOutputFrame` reads `timeGetTime`, floors the logical timeline
projection, clamps it to the committed submitted tail, and retains the existing
monotonic-last-returned behavior. If no tail has committed or a stable snapshot
is momentarily unavailable, return the previous value (or `nullopt` before the
first value). Arithmetic/domain failure invalidates this presented clock.

- [ ] **Step 4: Create one persistent timeline/tail/provider lifetime in the
  backend**

After initial `R` is known, capture `T0 = timeGetTime()`, construct one
`AsioLogicalTimeline`, one `AsioSubmittedOutputTail`, the presented adapter, and
the render core. After buffers provide `L`, allocate/register one
`ExactAsioClock` with one logical endpoint generation `E`.

On recovery, reuse all of those objects and `E`. Remove callback and detached
exact-anchor publication. After each successful mixer/sequencer commit,
publish exactly one new submitted tail to the shared object. Final teardown
invalidates the presented clock, exact provider, timeline consumers, and tail;
focus release invalidates none of them.

At the top of each post-initialization control-loop iteration, call
`timeline_->AdvanceNow(timeGetTime())`. Failure is a logical runtime fault; it
does not change or infer focus state.

- [ ] **Step 5: Build the rewritten exact contract and `gc_audio`**

Format with CLion, build `gc_exact_asio_clock_tests` and `gc_audio`, then run
`ExactAsioClock` and `AsioLogicalTimeline`. Expected: both pass and no
production ASIO exact-clock code contains a callback ring or fixed 48-kHz
guard.

- [ ] **Step 6: Commit the persistent clock migration**

This commit intentionally consumes the two pre-existing dirty files:

```powershell
git add -- src/Audio/Asio/ExactAsioClock.h src/Audio/Asio/ExactAsioClock.cpp src/Audio/Asio/AsioClock.h src/Audio/Asio/AsioClock.cpp src/Audio/Asio/AsioOutputBackend.cpp tests/Audio/Asio/ExactAsioClockTests.cpp
git commit -m "Project ASIO clocks from persistent timeline"
```

### Task 4: Make the Render Sequencer Own One-Time Physical Attachment

**Files:**

- Replace: `src/Audio/Asio/AsioLogicalRenderSequencer.h`
- Replace: `src/Audio/Asio/AsioLogicalRenderSequencer.cpp`
- Replace: `tests/Audio/Asio/AsioLogicalRenderSequencerTests.cpp`
- Modify: `src/Audio/Asio/AsioClock.h:12-44`
- Modify: `src/Audio/Asio/AsioClock.cpp:47-109`
- Modify: `src/Audio/Asio/AsioCallbackRuntime.h:15-21,157-166`
- Modify: `src/Audio/Asio/AsioCallbackRuntime.cpp` (`DispatchTimeInfo` and
  `DispatchLegacy` request construction)
- Modify: `src/Audio/Asio/AsioOutputBackend.cpp` (sequencer construction and
  render call sites)

**Interfaces:**

`AsioRenderRequest` adds `bool has_system_time`. A callback always requires a
valid raw sample position. A timestamp is required only until physical
attachment; after attachment it is optional diagnostic evidence.

```cpp
enum class AsioClockDecisionKind : std::uint8_t
{
    valid,
    invalid,
};

struct AsioClockDecision final
{
    AsioClockDecisionKind kind{AsioClockDecisionKind::invalid};
    std::uint64_t presented_output_frame{};
    std::uint64_t render_output_frame_begin{};
};
```

The backend, not `AsioClockTracker`, owns the post-attachment three-callback
priming count.

```cpp
enum class AsioLogicalRenderPlanFailure : std::uint8_t
{
    Busy,
    NotDue,
    InvalidConfiguration,
    InvalidPhysicalSession,
    PhysicalSessionAlreadyAttached,
    PhysicalSessionNotAttached,
    CoordinateRegressed,
    ArithmeticOverflow,
    GenerationOverflow,
};

enum class AsioPhysicalAttachmentDisposition : std::uint8_t
{
    Aligned,
    WaitForPhysical,
    CatchUpLogical,
};

struct AsioPhysicalAttachment final
{
    std::uint64_t physical_session_generation{};
    std::uint64_t logical_render_origin{};
    std::uint64_t physical_render_origin{};
    AsioPhysicalAttachmentDisposition disposition{};
    std::uint64_t interval_frames{};
};

class AsioLogicalRenderSequencer final
{
public:
    explicit AsioLogicalRenderSequencer(
        std::uint32_t period_frames) noexcept;

    [[nodiscard]] std::expected<std::uint64_t,
                                AsioLogicalRenderPlanFailure>
    BeginPhysicalSession() noexcept;
    [[nodiscard]] std::expected<AsioPhysicalAttachment,
                                AsioLogicalRenderPlanFailure>
    AttachPhysicalSession(
        std::uint64_t generation,
        std::uint64_t logical_render_origin,
        std::uint64_t physical_render_origin) noexcept;
    [[nodiscard]] std::expected<AsioLogicalRenderPlan,
                                AsioLogicalRenderPlanFailure>
    TryPlanPhysical(
        std::uint64_t generation,
        std::uint64_t physical_render_begin) noexcept;
    [[nodiscard]] std::expected<AsioLogicalRenderPlan,
                                AsioLogicalRenderPlanFailure>
    TryPlanDetached(std::uint64_t logical_render_begin) noexcept;
    bool Commit(const AsioLogicalRenderPlan&) noexcept;
    bool Abandon(const AsioLogicalRenderPlan&) noexcept;
    bool EndPhysicalSession(std::uint64_t generation) noexcept;
};
```

`AsioLogicalRenderPlan` retains only `MixerRenderTimeline`, submitted tail,
physical generation, and its private claim token. Remove `system_time_ns` and
callback-derived presented-frame fields.

- [ ] **Step 1: Write the failing one-time attachment/delta oracle**

Replace the current time-anchor test with scenarios whose expected coordinates
come only from affine sample math:

```cpp
void OneAttachmentOwnsEveryLaterPhysicalCoordinate()
{
    AsioLogicalRenderSequencer sequencer{192};
    const auto generation = sequencer.BeginPhysicalSession();
    const auto attached = sequencer.AttachPhysicalSession(
        *generation,
        0, 500);
    Expect(attached.has_value(), "first mapping attaches");

    auto first = sequencer.TryPlanPhysical(*generation, 500);
    ExpectPlan(first, 0, 0, 192,
               "origin maps exactly");
    Expect(sequencer.Commit(*first), "origin block commits");

    auto second = sequencer.TryPlanPhysical(*generation, 692);
    ExpectPlan(second, 192, 0, 384,
               "later mapping uses only the 192-sample delta");
    Expect(sequencer.Commit(*second), "second block commits");

    auto duplicate_attach = sequencer.AttachPhysicalSession(
        *generation,
        99'000, 692);
    Expect(!duplicate_attach &&
           duplicate_attach.error() ==
               AsioLogicalRenderPlanFailure::PhysicalSessionAlreadyAttached,
           "later callback evidence cannot re-anchor the session");
}

void DetachedCoverageProducesOneWaitOrCatchUp()
{
    AsioLogicalRenderSequencer sequencer{192};
    const auto first_generation = sequencer.BeginPhysicalSession();
    Expect(sequencer.AttachPhysicalSession(
               *first_generation, 0, 384).has_value(),
           "initial physical session attaches");
    auto initial = sequencer.TryPlanPhysical(*first_generation, 384);
    ExpectPlan(initial, 0, 0, 192, "initial block starts at zero");
    Expect(sequencer.Commit(*initial), "initial block commits");
    Expect(sequencer.EndPhysicalSession(*first_generation),
           "initial physical session ends");

    auto detached = sequencer.TryPlanDetached(960);
    ExpectPlan(detached, 960, 768, 1'152,
               "detached absolute target catches up once");
    Expect(sequencer.Commit(*detached), "detached block commits");

    const auto second_generation = sequencer.BeginPhysicalSession();
    const auto attachment = sequencer.AttachPhysicalSession(
        *second_generation, 1'056, 384);
    Expect(attachment &&
           attachment->disposition ==
               AsioPhysicalAttachmentDisposition::WaitForPhysical &&
           attachment->interval_frames == 96,
           "replacement reports its one wait interval");

    const auto behind = sequencer.TryPlanPhysical(
        *second_generation, 384);
    Expect(!behind &&
           behind.error() == AsioLogicalRenderPlanFailure::NotDue,
           "covered physical output waits without replay");

    auto caught_up = sequencer.TryPlanPhysical(
        *second_generation, 576);
    ExpectPlan(caught_up, 1'248, 96, 1'440,
               "first ahead callback catches the remaining interval once");
    Expect(sequencer.Commit(*caught_up), "catch-up block commits");

    auto contiguous = sequencer.TryPlanPhysical(
        *second_generation, 768);
    ExpectPlan(contiguous, 1'440, 0, 1'632,
               "next callback is contiguous rather than re-corrected");
}

void ClaimsRemainExclusiveAndAbandonIsTransactional()
{
    AsioLogicalRenderSequencer sequencer{192};
    const auto generation = sequencer.BeginPhysicalSession();
    Expect(sequencer.AttachPhysicalSession(*generation, 0, 0).has_value(),
           "claim test attaches");
    auto held = sequencer.TryPlanPhysical(*generation, 0);
    const auto competing = sequencer.TryPlanPhysical(*generation, 0);
    Expect(!competing &&
           competing.error() == AsioLogicalRenderPlanFailure::Busy,
           "only one renderer owns the claim");
    Expect(sequencer.Abandon(*held), "abandon releases the claim");
    auto retried = sequencer.TryPlanPhysical(*generation, 0);
    ExpectPlan(retried, 0, 0, 192,
               "abandon does not advance hidden state");
}
```

The sequencer API intentionally has no later timestamp parameter. That compile-
time boundary is how irregular later callback timestamps are prevented from
changing the mapping. Expected red result: the constructor, attach call, and
absolute-target APIs do not exist.

- [ ] **Step 2: Implement exclusive attachment and absolute planning**

`BeginPhysicalSession` creates a new nonzero generation and clears attachment
state. `AttachPhysicalSession` acquires the same exclusive claim used by render
planning, validates the active generation, installs the mapping exactly once,
and reports:

- `Aligned` when `logical_render_origin == next_logical_tail`;
- `WaitForPhysical` with `next_tail - origin` when the new physical stream is
  behind already-detached logical work; or
- `CatchUpLogical` with `origin - next_tail` when logical work must advance.

For later raw physical render coordinate `S_render`:

```cpp
logical_target = logical_render_origin
               + (S_render - physical_render_origin)
```

Reject physical regression or overflow. If `logical_target < next_tail`, return
`NotDue`, not `CoordinateRegressed`; the callback will emit silence while the
new physical stream catches up. Otherwise produce one plan whose
`discontinuity_frames = logical_target - next_tail` and whose submitted tail is
`logical_target + B`.

`TryPlanDetached` receives the already-projected absolute logical render target
and uses the same `< next_tail => NotDue` / ahead => one discontinuity rule. It
does not read time or count wakes. `Commit` alone advances the sequencer;
`Abandon` releases the claim without changing the tail or attachment.

- [ ] **Step 3: Remove later timestamp validation from physical sample
  continuity**

Keep `AsioClockTracker` responsible for:

- accepting an arbitrary first raw sample origin `S0`;
- exactly `B` raw sample-position progression after `S0`;
- checked `S + L`; and
- returning only structurally valid or invalid physical coordinates.

Remove the current `sample_position % B == 0` requirement and the
millisecond-delta/`elapsed_ms == 0` rules. A replacement driver epoch may begin
at any sample value, and later system time is not a structural continuity
oracle. Sub-millisecond periods must not fail because two callbacks share the
same truncated millisecond.

Change `AsioClockTracker::Observe` to consume raw sample position only. In
`AsioCallbackRuntime`, require and convert `samplePosition` on every callback,
but set `has_system_time=true` only when a valid timestamp was supplied and
converted. A missing later timestamp does not fault. Before attachment, a
request without a valid timestamp is zero-filled, submitted with
`OutputReady` when supported, and cannot signal stability. If no valid `A0`
arrives, the existing finite startup/recovery stability deadline fails fatally.
Call `RecordDriverCadence` and the logical residual recorder only when
`has_system_time` is true.

Move the finite priming proof to backend state and start its counter only when
attachment succeeds. The attachment callback is proof callback 1; the next two
structurally continuous callbacks are 2 and 3. Earlier callbacks lacking `A0`
do not count. Only proof callback 3 or later may signal `stable_render_event_`.

- [ ] **Step 4: Adapt backend call sites without adding clock authority**

Construct the sequencer once with `B`. On the first structurally valid callback
for generation `G`, calculate `floor(P(A0)) + L` and `S0 + L` with the logical
timeline and call `AttachPhysicalSession` exactly once. A transient
`SnapshotUnavailable` or `Busy` result zero-fills, submits `OutputReady` when
supported, does not signal stability, and retries attachment on the next valid
callback; every arithmetic, ambiguity, duplicate-attachment, or generation
failure is fatal. Later callbacks call
`TryPlanPhysical(G, S + L)` and never pass `systemTime` to the sequencer.

Detached control work calls `TryPlanDetached(floor(P(now)) + L)`. Treat `Busy`
and `NotDue` as zero-filled, non-committing outcomes; every other planning error
latches the typed runtime-clock fault.

- [ ] **Step 5: Format, build, and run the sequencer contract**

Build `gc_asio_logical_render_sequencer_tests` and `gc_audio`; run
`AsioLogicalRenderSequencer`, `AsioLogicalTimeline`, and `ExactAsioClock`.
Expected: all pass, and no sequencer source accepts a timestamp or sample rate.

- [ ] **Step 6: Commit one-time physical mapping**

```powershell
git add -- src/Audio/Asio/AsioLogicalRenderSequencer.h src/Audio/Asio/AsioLogicalRenderSequencer.cpp src/Audio/Asio/AsioClock.h src/Audio/Asio/AsioClock.cpp src/Audio/Asio/AsioCallbackRuntime.h src/Audio/Asio/AsioCallbackRuntime.cpp src/Audio/Asio/AsioOutputBackend.cpp tests/Audio/Asio/AsioLogicalRenderSequencerTests.cpp
git commit -m "Map ASIO sessions into logical render time"
```

### Task 5: Integrate Persistent Logical and Replaceable Physical Lifetimes

**Files:**

- Modify: `src/Audio/Asio/AsioOutputBackend.cpp:511-2925`
- Modify: `src/Audio/Asio/AsioOutputBackend.h:17-120`

**Persistent state shape:**

```cpp
std::uint32_t logical_output_sample_rate_{};
std::uint32_t logical_output_latency_frames_{};
AsioLogicalOutputContract logical_contract_{};
std::shared_ptr<AsioLogicalTimeline> logical_timeline_;
std::shared_ptr<AsioSubmittedOutputTail> submitted_tail_;
std::unique_ptr<AudioRenderCore> render_core_;
std::unique_ptr<AsioLogicalRenderSequencer> logical_render_sequencer_;
std::shared_ptr<ExactAsioClock> exact_clock_;
AsioPresentedClockPublication* presented_clock_{};

// Replaceable only:
std::unique_ptr<AsioSession> session_;
std::unique_ptr<AsioCallbackRuntime> callback_runtime_;
std::array<std::array<std::span<std::byte>, 2>, 2> driver_buffers_{};
std::atomic_uint64_t active_physical_session_generation_{};
```

- [ ] **Step 1: Make initial startup follow the staged contract exactly**

`InitializeBackend` must execute this order:

1. start foreground publication;
2. start the 1-ms multimedia timer period only when absolute judgement uses it;
3. resolve/create/init one physical session with `AsioAdoptCurrentRate`;
4. freeze `R` and create the candidate timeline, shared submitted tail,
   presented clock, render core, and callback runtime using `R` (none is
   registered as an endpoint yet);
5. install callbacks, create driver buffers, configure the selected pair, and
   read `L`;
6. freeze driver identity, `R`, `B`, channel indices/types, `L`, and
   `outputReady`; create the sequencer, persistent endpoint generation `E`, and
   exact provider over the existing timeline/tail; register `E` exactly once;
7. begin physical generation 1, call `Start`, zero-fill/prime, and require the
   existing finite sequence of valid continuous callbacks; and
8. publish startup success, enter lifecycle monitoring, then consume any
   background state or unconsumed loss generation observed during startup.

Any failure in these steps is fatal. Do not retry initial startup and do not
construct another backend.

- [ ] **Step 2: Keep the Task 4 attachment immutable and add diagnostics-only
  residual observation**

Retain the Task 4 first timestamped, structurally valid callback calculation:

```cpp
const auto logical_presented =
    logical_timeline_->WholePresentedFrameAtSystemTime(request.system_time_ns);
const auto logical_render_origin = *logical_presented + L;
const auto physical_render_origin = request.sample_position + L;

attachment = logical_render_sequencer_->AttachPhysicalSession(
    G,
    logical_render_origin,
    physical_render_origin);
```

Store the raw sample origin and returned attachment record for transition
diagnostics. Never invoke attachment again for `G`. A transient
`SnapshotUnavailable` or `Busy` result zero-fills and tries the next
structurally valid callback without signaling stability. Duplicate attachment,
ambiguity, arithmetic error, or generation mismatch latches a fatal
runtime-clock fault.

Later callbacks derive their target only from `S + L` through the installed
mapping. They may compare `P(systemTime)` with the mapped presented coordinate
for aggregate residual diagnostics, but the residual magnitude has no branch
to attachment, focus, recovery, or fatal classification.

- [ ] **Step 3: Make callback rendering commit in one direction**

For every valid callback:

1. validate buffer/sample structure and record raw discontinuity diagnostics;
2. attach once if necessary;
3. ask the sequencer for the absolute physical plan;
4. on `Busy`, zero-fill, call `OutputReady` when supported, and do not signal
   stability; on `NotDue`, perform the same zero-filled submission and signal
   stability only when the post-attachment proof counter has reached callback
   3;
5. render the mixer plan;
6. zero-fill during priming, otherwise convert into the current driver buffer;
7. commit the sequencer plan;
8. publish the committed submitted tail once to the shared tail;
9. call required `OutputReady`; and
10. signal the stable-render event only after post-attachment proof callback 3.

A mixer/conversion/commit/tail/outputReady failure zero-fills when the current
buffer is valid, latches one typed fault, signals the control thread, and
returns. Once the mixer has mutated, any later publication failure is fatal;
there is no attempt to continue from a partially known state.

- [ ] **Step 4: Render suspended time from the absolute timeline**

`AdvanceSilentRendering` must first call
`logical_timeline_->AdvanceNow(timeGetTime())`, then calculate:

```cpp
logical_render_target = floor(P(now)) + L
```

Pass that target to `TryPlanDetached`. `NotDue`/`Busy` performs no state change.
For a plan, render/discard the block, commit the sequencer, publish the shared
tail, and count all logically advanced frames. A late control-thread wake
therefore produces one exact discontinuity interval rather than accumulating
`R / 1000` truncation or one period per wake.

Call this path only in `Suspended` and clean pre-`Start` recovery work. After a
successful recovery preparation and immediately before `Start`, call it once
more so driver-open time is included at a committed boundary. Then quiesce the
control-thread path before enabling callback rendering.

- [ ] **Step 5: Preserve the focus-only lifecycle and strict retry boundary**

Retain the coherent `{is_foreground, loss_generation}` rules:

- startup completes `Start`/stability even if background, then suspends;
- loss+regain coalescing still invalidates the old physical generation;
- loss before recovery `Start` closes that clean attempt and returns to
  `Suspended` without counting a failed retry;
- loss after `Start` waits for proof, then consumes pending focus state;
- regain during release waits for proven release;
- recovery attempt 1 is immediate, attempt 2 waits 1 second, and attempt 3
  waits 2 more seconds; and
- retry waits remain interruptible while absolute detached rendering continues.

Recovery calls `AsioRequireFrozenRate{R}` and validates the full immutable
contract. A clean mismatch or acquisition failure before `Start` may retry; a
cleanup failure, `Start` failure, stability failure, callback fault, logical
fault, or runtime ASIO change request is fatal.

- [ ] **Step 6: Keep physical close narrow and logical teardown final**

`ClosePhysicalSession` performs, in order:

1. clear `render_ready_`;
2. tell callback runtime to stop accepting work;
3. stop IASIO;
4. join/uninstall callback ownership;
5. end the matching sequencer physical generation;
6. dispose buffers and restore/verify a temporary recovery rate through
   `AsioSession::Close`; and
7. clear only physical pointers/spans/sample-position state.

It must not destroy the timeline, submitted tail, render core, voices,
sequencer, presented clock, exact provider, or endpoint generation.

Final teardown closes the physical session, invalidates presentation and exact
consumers, snapshots exact counters, unregisters `E`, invalidates the shared
tail, releases the foreground monitor/timer period, and reports the typed final
result.

- [ ] **Step 7: Review callback safety and compile the complete integration**

Use CLion navigation—not a source-grep test—to verify `RenderAsioBlock`,
`ClearAsioBlock`, `OnAsioRuntimeFault`, and callback-runtime dispatch contain no
allocation, formatting, logging, wait, driver lifecycle, rate mutation, focus
query, exception escape, or direct process-fatal call.

Build `gc_audio` and run all four ASIO contract tests. Expected: compilation and
tests pass. No driver or gameplay claim follows.

- [ ] **Step 8: Commit the persistent/replaceable lifetime integration**

```powershell
git add -- src/Audio/Asio/AsioOutputBackend.cpp src/Audio/Asio/AsioOutputBackend.h
git commit -m "Integrate persistent ASIO session recovery"
```

### Task 6: Replace Anchor Diagnostics with Logical and Physical Contract Records

**Files:**

- Modify: `src/Audio/Asio/AsioOutputBackend.h:17-120`
- Modify: `src/Audio/Asio/AsioOutputBackend.cpp` (counters, lifecycle records,
  startup record, attachment and cleanup facts)
- Modify: `src/Audio/AudioPatch.cpp:600-780,921-1065,1470-1625`

**Interfaces:**

```cpp
enum class AsioPhysicalSessionReason : std::uint8_t
{
    startup,
    focus_recovery,
};

enum class AsioSessionLifecycleEvent : std::uint8_t
{
    foreground_lost,
    session_released,
    foreground_regained,
    recovery_attempt_started,
    recovery_attempt_failed,
    physical_session_started,
    session_recovered,
};

struct AsioLogicalBackendRecord final
{
    std::uint32_t origin_raw_ms{};
    std::uint64_t origin_unwrapped_ms{};   // zero by contract
    std::uint64_t origin_presented_frame{}; // zero by contract
    std::uint64_t endpoint_generation{};
    std::uint32_t sample_rate{};
    std::uint32_t period_frames{};
    std::uint32_t output_latency_frames{};
    bool alternate_backend_selected{}; // always false
};
```

Extend `AsioSessionLifecycleRecord` with bounded transition facts rather than a
free-form callback log:

```cpp
AsioPhysicalSessionReason reason{};
double observed_sample_rate{};
double active_sample_rate{};
bool frozen_rate_requested{};
bool sample_rate_changed{};
bool restoration_attempted{};
bool restoration_succeeded{};
std::uint64_t raw_sample_origin{};
AsioPhysicalAttachmentDisposition attachment_disposition{};
std::uint64_t attachment_interval_frames{};
std::uint64_t silent_priming_callbacks{};
bool callback_quiesced{};
bool buffers_disposed{};
```

- [ ] **Step 1: Publish one complete logical-backend startup record**

Change `IAsioOutputObserver::StartupSucceeded` to receive both the capability
report and `AsioLogicalBackendRecord`. The production log must include:

- requested/active backend `asio` and `alternate_backend_selected=false`;
- registration/reported driver identity;
- `sample_rate_source=driver_current`, `R`, `B`, `L`, channel indices/types,
  and `outputReady` capability;
- raw `T0`, unwrapped origin 0, presented origin 0, and endpoint generation `E`;
  and
- exact domain `asio_multimedia_ms` and 1-ms timestamp quantum.

Keep this as one startup transition record. Do not log per callback or input.

- [ ] **Step 2: Record one bounded physical-generation lifecycle**

Populate the existing transition events with the fields relevant to each
event. Add `physical_session_started` and emit it once after initial attachment
and stability with `reason=startup`. It records physical generation, raw sample
origin, both attachment origins, attachment disposition/interval, and silent
priming count.

`session_recovered` records the open/active rate, whether frozen-rate
restoration was needed, the same physical attachment facts, and
`reason=focus_recovery`.

`session_released` records successful callback quiescence, buffer disposal, and
rate-restoration result. A failed cleanup is carried in the typed failure and
enters `Fatal`; do not emit a success-shaped release record.

Startup uses `reason=startup`; later physical generations use
`reason=focus_recovery`.

- [ ] **Step 3: Replace obsolete aggregate counters**

Remove `exact_anchor_publications` and
`detached_exact_anchor_publications`. Add and log:

- `submitted_tail_publications` and current `submitted_output_tail`;
- total logically advanced frames;
- detached discarded frames and priming discarded frames;
- driver-timeline residual sample count; and
- maximum absolute driver-timeline residual in nanoseconds, labelled
  `diagnostic_only`.

Retain callback cadence, overload/slow observations, raw sample/buffer faults,
focus/physical generations, recovery counters, mixer counters, and exact
resolved/pending/temporary/discontinuous counters. `exact_history_lost_queries`
may remain in the backend-neutral counter shape but must stay zero for ASIO
because there is no retained callback history.

Residual magnitude is never a failure predicate. Only inability to perform
required checked logical arithmetic is a logical fault.

- [ ] **Step 4: Perform the fixed-rate and authority sweep with CLion**

Use CLion regex/navigation over `src/Audio/Asio/**` and inspect every production
match for `48'000`, `48000`, `kOutputFramesPerMillisecond`, callback anchor,
reanchor, fallback, and `PreparePhysicalSession`. Confirm:

- no production rate calculation assumes 48 kHz;
- 44.1 and 48 kHz remain legitimate test values;
- no callback timestamp after attachment changes logical coordinates;
- time never changes foreground state;
- preparation occurs only once at startup and from `Recovering` afterward;
- no ASIO failure reaches the WASAPI/DirectSound startup path; and
- every post-`Start` fault reaches controlled fatal teardown.

This is a focused code review, not an automated source-text test.

- [ ] **Step 5: Format, build, and commit transition diagnostics**

Use CLion formatting on the modified files, build `gc_audio`, and run the ASIO
contract tests. Then commit:

```powershell
git add -- src/Audio/Asio/AsioOutputBackend.h src/Audio/Asio/AsioOutputBackend.cpp src/Audio/AudioPatch.cpp
git commit -m "Report persistent ASIO lifecycle diagnostics"
```

### Task 7: Format, Diagnose, Verify, and Hand Off Ordered Runtime Acceptance

**Files:** Every source/header/CMake/test file changed by Tasks 1-6.

- [ ] **Step 1: Apply CLion formatting one changed file at a time**

Use CLion `reformat_file` for changed `.h`/`.cpp` files, in dependency order:

1. logical timeline and submitted tail;
2. ASIO session and callback runtime;
3. ASIO clocks;
4. render sequencer;
5. output backend headers/source;
6. `AudioPatch.cpp`; and
7. ASIO test sources.

Accept formatter-only changes within these files. Do not run a repository-wide
formatter and do not manually restyle unrelated sections.

- [ ] **Step 2: Run CLion diagnostics file by file without closing anything**

For each changed `.h`/`.cpp` file in the same order:

1. open that one file in the editor;
2. allow CLion analysis to load;
3. request errors and warnings for that file;
4. fix or explicitly justify every new diagnostic; and
5. proceed to the next file while leaving prior files open.

Do not batch diagnostic calls. Informational inspections are not warnings or
errors. Never close CLion, its project, or editor files.

- [ ] **Step 3: Run the focused Debug contract build**

```powershell
$asioVcvars = 'C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat'
cmd.exe /d /s /c "call `"$asioVcvars`" && set `"GC_ASIO_SDK_DIR=H:\gc\artifacts\ASIOSDK`" && cmake --preset msvc32-debug && cmake --build --preset msvc32-debug --target gc_asio_logical_timeline_tests gc_exact_asio_clock_tests gc_asio_logical_render_sequencer_tests gc_asio_foreground_state_tests gc_audio && ctest --test-dir build-msvc32-debug -R `"^(AsioLogicalTimeline|ExactAsioClock|AsioLogicalRenderSequencer|AsioForegroundState)$`" --output-on-failure"
```

Expected: all four independently meaningful ASIO contract tests pass.

- [ ] **Step 4: Run complete x86 Debug configure/build/test**

```powershell
$asioVcvars = 'C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat'
cmd.exe /d /s /c "call `"$asioVcvars`" && set `"GC_ASIO_SDK_DIR=H:\gc\artifacts\ASIOSDK`" && cmake --preset msvc32-debug && cmake --build --preset msvc32-debug && ctest --preset msvc32-debug -j 4 --output-on-failure"
```

Expected: the complete Debug build and every registered test pass.

- [ ] **Step 5: Run complete x86 Release configure/build/test**

```powershell
$asioVcvars = 'C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat'
cmd.exe /d /s /c "call `"$asioVcvars`" && set `"GC_ASIO_SDK_DIR=H:\gc\artifacts\ASIOSDK`" && cmake --preset msvc32-release && cmake --build --preset msvc32-release && ctest --preset msvc32-release -j 4 --output-on-failure"
```

Expected: the complete Release build and every registered test pass.

- [ ] **Step 6: Review the complete diff and commit only accepted residue**

Run:

```powershell
git diff --check
git status --short --branch
git diff --stat
git diff -- src/Audio/Asio src/Audio/AudioPatch.cpp tests/Audio/Asio tests/CMakeLists.txt
```

Review specifically for fixed-rate assumptions, callback-time authority,
logical-object recreation, tail publication before commit, focus inferred from
time, unbounded retry, post-`Start` continuation, fallback, callback logging or
allocation, and unsafe cleanup/restoration.

If CLion formatting or diagnostic fixes produced reviewed residue, stage only
the listed task paths and create one `Format ASIO persistent timeline recovery`
commit. If there is no residue, do not create an empty commit.

- [ ] **Step 7: Record candidate/deployed identity without deploying**

Hash:

```powershell
Get-FileHash -Algorithm SHA256 -LiteralPath 'H:\gc\artifacts\GCLoader\build-msvc32-release\dist\iDmacDrv32.dll'
Get-FileHash -Algorithm SHA256 -LiteralPath 'H:\gc\iDmacDrv32.dll'
```

Report both hashes and the candidate path. A mismatch means runtime logs do not
yet describe the candidate. Do not copy, deploy, restart, close, or launch
anything; ask the user to use their normal deployment/run procedure.

- [ ] **Step 8: Hand off Run 1 — lose focus shortly after startup**

After the user confirms deployment and completes the run, inspect only the new
log interval. Require:

- initial `sample_rate_source=driver_current`, one persistent `R`/`E`, and no
  alternate backend;
- initial `Start` and stable priming before exactly one background release;
- continued logical/tail/discard advancement while physical audio is absent;
- immediate recovery attempt 1 under normal conditions;
- the same `R` and `E`, a new physical generation, and one attachment;
- successful quiescence/disposal facts plus restoration either not required or
  verified successful; and
- no fatal, fallback, repeated retry, re-anchor, or logical-time pause.

Only the user accepts audible recovery.

- [ ] **Step 9: Hand off Run 2 — all-foreground multi-song stability**

Only after Run 1 passes, ask the user for a fresh all-foreground run containing
at least two complete songs. Require exactly one physical generation and zero
recovery, rate/buffer/latency/reset/resync/sample-position/buffer-alternation
faults, exact history loss/discontinuity, or endpoint changes. A frame drop may
appear as a game observation but may not alter ASIO attachment or judgement
projection.

Only the user accepts stable audio and judgement feel relative to WASAPI.

- [ ] **Step 10: Hand off Run 3 — post-start focus transfer and menu activity**

Ask the user to reproduce the prior sequence: stable startup, focus out, focus
back, enter the menu, and scroll long enough to cover the deterministic silence
case. Require one release/recovery generation, normally immediate acquisition,
one attachment, sustained tail/callback progress, and no later menu silence,
retry, re-anchor, or fatal transition.

- [ ] **Step 11: Hand off Run 4 — window movement/transient frame drop**

Ask the user to move the game window or observe a transient main-thread frame
drop. Confirm the log preserves endpoint identity, physical attachment, and
logical projection. No time/residual/frame-duration evidence may select
recovery. The user decides whether audio and judgement remain correct.

- [ ] **Step 12: Optionally verify another driver-owned current rate**

Only when the user can place the installed driver at another supported current
rate before launch, repeat startup and the all-foreground session. Require
adoption without a configuration change or forced 48-kHz mutation; recovery in
that backend lifetime must retain the adopted rate. Do not change the driver's
control-panel state on the user's behalf.

## Completion Gate

Do not call the redesign complete until all of these are true:

- the persistent timeline is the only ASIO judgement-time authority;
- callback timestamps after first attachment cannot alter it;
- the initial driver rate is adopted and recovery preserves/restores it;
- focus loss removes only physical audio ownership, not judgement time;
- recovery is focus-driven, bounded, and normally succeeds immediately;
- every post-`Start` instability fails closed;
- ASIO never falls back to WASAPI or DirectSound;
- focused and full x86 Debug/Release verification pass;
- candidate/deployed identity is proven before log attribution; and
- the user accepts the ordered actual-game startup, multi-song, menu, frame-
  drop, audio, and judgement behavior.

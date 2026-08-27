# ASIO Lifecycle Recovery Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use
> `superpowers:executing-plans` to implement this plan task-by-task. The user
> selected inline execution; do not dispatch subagents or create a worktree.

**Goal:** Make configured ASIO start reliably across early focus changes,
suspend and recover only for explicit focus ownership changes, keep audio and
judgement on one logical timeline, and fail immediately on real ASIO contract
violations.

**Architecture:** A coherent foreground publication preserves both the latest
state and every loss edge. `AsioOutputBackendState` owns an explicit lifecycle
around a persistent logical engine and replaceable physical IASIO session;
detached renders publish the same presented and exact clock domain, while only
clean pre-`Start` recovery acquisition failures receive a bounded retry.

**Tech Stack:** Windows x86, C++23, ASIO SDK, miniaudio, CMake/Ninja, MSVC 18
Insiders, CLion clangd/clang-tidy.

**Spec:**
`docs/superpowers/specs/2026-08-25-asio-focus-recovery-design.md`

## Global Constraints

- Configured ASIO never instantiates or falls back to WASAPI.
- Startup has one acquisition attempt and must invoke `Start` and prove stable
  callbacks even if focus changes during startup.
- Recovery has one immediate pre-`Start` attempt, one retry after 1 second, and
  one final retry after 2 additional seconds.
- Only clean recovery failures before `Start` are retryable. `Start`, callback
  stability, callback, clock, render, endpoint-contract, and unsafe-cleanup
  failures are fatal.
- Time advances an already-confirmed suspended logical timeline; it never
  determines foreground ownership or classifies a failure.
- The mixer, voices, logical output cursor, presented clock,
  `ExactAsioClock`, endpoint generation, and active judgement binding survive
  focus suspension and recovery.
- The callback path remains non-blocking and allocation-free. It never queries
  focus, waits, retries, opens, closes, or logs.
- An overload notification or one slow callback remains diagnostic only; it is
  not a fault without a separately violated callback/clock/render contract.
- No exception may cross an ASIO callback, COM method, exported DirectSound
  method, WinEvent callback, or thread entry point.
- Do not add a fake IASIO suite, source-text assertion, sleep-based timing
  test, or test-only production API.
- Do not create a worktree or use agents. Preserve unrelated user changes.
- Never stop, close, restart, terminate, or kill a process, IDE, or service.
- Use CLion MCP for source navigation, formatting, and diagnostics. Diagnose
  one file at a time: open the file, request diagnostics, finish that file, and
  do not close it. Use the normal shell for Git, CMake, builds, and tests.
- Use
  `C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat`
  and `GC_ASIO_SDK_DIR=H:\gc\artifacts\ASIOSDK` for every build/test command.
- Do not deploy a DLL or mutate `H:\gc` runtime files. Static/build proof and
  in-game acceptance are separate checkpoints.

## File Responsibility Map

- Create `src/Audio/Asio/AsioForegroundState.h`: lock-free coherent
  foreground/current-loss publication with no Windows dependency.
- Modify `src/Audio/Asio/AsioForegroundMonitor.h`: expose one coherent
  snapshot instead of exposing a second direct foreground query to consumers.
- Modify `src/Audio/Asio/AsioForegroundMonitor.cpp`: publish WinEvent changes
  into the coherent state and fail the monitor if its generation cannot
  advance.
- Create `tests/Audio/Asio/AsioForegroundStateTests.cpp`: the one new
  regression test, exercising the real publication object without sleeps or
  mocks.
- Modify `tests/CMakeLists.txt`: register the focused publication test.
- Modify `src/Audio/Asio/AsioOutputBackend.cpp`: split preparation from
  `Start`, implement the lifecycle and retry boundary, publish detached exact
  anchors, preserve handoff coordinates, and make runtime faults fatal.
- Modify `src/Audio/Asio/AsioOutputBackend.h`: carry lifecycle diagnostics and
  the detached exact-anchor counter across the observer boundary.
- Modify `src/Audio/AudioPatch.cpp`: render the new lifecycle records and
  counters into transition-oriented logs.
- Modify
  `src/Patches/AbsoluteJudgement/JudgementScheduler.cpp`: stop resolving the
  immutable stage-entry timestamp after stage activation while preserving
  endpoint identity validation.

---

### Task 1: Preserve Fast Foreground Loss and Regain

**Files:**

- Create: `src/Audio/Asio/AsioForegroundState.h`
- Create: `tests/Audio/Asio/AsioForegroundStateTests.cpp`
- Modify: `src/Audio/Asio/AsioForegroundMonitor.h:26-63`
- Modify: `src/Audio/Asio/AsioForegroundMonitor.cpp:261-316`
- Modify: `tests/CMakeLists.txt:13-24`

**Interfaces:**

- Produces:
  `AsioForegroundSnapshot { bool is_foreground; std::uint64_t loss_generation; }`.
- Produces:
  `AsioForegroundPublishResult AsioForegroundState::Publish(bool) noexcept`.
- Produces:
  `AsioForegroundSnapshot AsioForegroundState::Read() const noexcept`.
- Produces:
  `AsioForegroundSnapshot AsioForegroundMonitor::snapshot() const noexcept`.
- Task 4 consumes the snapshot and records its last consumed loss generation.

- [ ] **Step 1: Add the focused test target and failing production test**

Read `superpowers:test-driven-development/writing-good-tests.md` before
editing the test. Add this target to `tests/CMakeLists.txt`:

```cmake
add_executable(gc_asio_foreground_state_tests
        Audio/Asio/AsioForegroundStateTests.cpp
)
target_link_libraries(gc_asio_foreground_state_tests PRIVATE
        gc_asio
)

add_test(
        NAME AsioForegroundState
        COMMAND gc_asio_foreground_state_tests
)
```

Create the test with one producer and one consumer. The consumer must read only
once, after both transitions were published:

```cpp
#include "Audio/Asio/AsioForegroundState.h"

#include <cstdlib>
#include <iostream>
#include <semaphore>
#include <thread>

int main()
{
    gc::audio::AsioForegroundState state;
    if (state.Publish(true) !=
        gc::audio::AsioForegroundPublishResult::changed)
    {
        std::cerr << "FAIL: initial foreground publication\n";
        return EXIT_FAILURE;
    }

    std::binary_semaphore consumer_ready{0};
    std::binary_semaphore publications_complete{0};
    gc::audio::AsioForegroundSnapshot observed{};
    std::jthread consumer([&]
    {
        consumer_ready.release();
        publications_complete.acquire();
        observed = state.Read();
    });

    consumer_ready.acquire();
    const auto loss = state.Publish(false);
    const auto regain = state.Publish(true);
    publications_complete.release();
    consumer.join();

    if (loss != gc::audio::AsioForegroundPublishResult::changed ||
        regain != gc::audio::AsioForegroundPublishResult::changed ||
        !observed.is_foreground || observed.loss_generation != 1)
    {
        std::cerr << "FAIL: loss followed by regain was not preserved\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
```

The hand-derived oracle is `is_foreground == true` and
`loss_generation == 1`: two publications end foreground but contain exactly
one true-to-false edge. Removing the generation increment or storing only the
latest boolean must make the test fail.

- [ ] **Step 2: Build the target and verify the red failure**

Run from PowerShell:

```powershell
$asioVcvars = 'C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat'
cmd.exe /d /s /c "call `"$asioVcvars`" && set `"GC_ASIO_SDK_DIR=H:\gc\artifacts\ASIOSDK`" && cmake --preset msvc32-debug && cmake --build --preset msvc32-debug --target gc_asio_foreground_state_tests"
```

Expected: compilation fails because
`Audio/Asio/AsioForegroundState.h` does not exist. A configure-only failure or
an unrelated compiler failure does not count as the red proof.

- [ ] **Step 3: Implement the coherent production publication**

Create `AsioForegroundState.h` as a header-only production primitive. Pack the
foreground bit and generation into one lock-free `std::atomic_uint64_t` so a
reader cannot observe fields from different publications:

```cpp
enum class AsioForegroundPublishResult : std::uint8_t
{
    unchanged,
    changed,
    generation_overflow,
};

struct AsioForegroundSnapshot final
{
    bool is_foreground{};
    std::uint64_t loss_generation{};
};

class AsioForegroundState final
{
public:
    [[nodiscard]] AsioForegroundPublishResult Publish(bool foreground) noexcept;
    [[nodiscard]] AsioForegroundSnapshot Read() const noexcept;

private:
    static constexpr std::uint64_t kForegroundBit = 1;
    static constexpr std::uint64_t kMaximumGeneration =
        (std::numeric_limits<std::uint64_t>::max)() >> 1;
    std::atomic_uint64_t encoded_{};
};
```

`Publish` uses a compare/exchange loop. A false-to-true change preserves the
generation; a true-to-false change increments it before storing. On overflow,
leave the encoded state unchanged and return `generation_overflow`. `Read`
performs one acquire load and decodes both fields from that value.

- [ ] **Step 4: Route the monitor through the publication object**

Replace `foreground_` in `AsioForegroundMonitor` with
`AsioForegroundState foreground_state_`. `PublishForeground` must:

```cpp
const auto result = foreground_state_.Publish(foreground);
if (result == AsioForegroundPublishResult::generation_overflow)
{
    PublishThreadFailure(ERROR_ARITHMETIC_OVERFLOW);
    return;
}
if (result == AsioForegroundPublishResult::changed && change_event_ != nullptr)
{
    SetEvent(change_event_);
}
```

Expose `snapshot()` by returning `foreground_state_.Read()`. Keep the direct
Win32 query private as `QueryForegroundWindow()`; only
`PublishCurrentForeground` calls it. The backend will consume only snapshots.

- [ ] **Step 5: Format and prove the focused test passes**

Use CLion `reformat_file` on the new header, test, and modified monitor files.
Then rerun the target and:

```powershell
$asioVcvars = 'C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat'
cmd.exe /d /s /c "call `"$asioVcvars`" && set `"GC_ASIO_SDK_DIR=H:\gc\artifacts\ASIOSDK`" && cmake --build --preset msvc32-debug --target gc_asio_foreground_state_tests && ctest --preset msvc32-debug -R `"^AsioForegroundState$`""
```

Expected: the target builds and the single test passes without sleeps.

- [ ] **Step 6: Commit the coherent foreground publication**

```powershell
git add -- src/Audio/Asio/AsioForegroundState.h src/Audio/Asio/AsioForegroundMonitor.h src/Audio/Asio/AsioForegroundMonitor.cpp tests/Audio/Asio/AsioForegroundStateTests.cpp tests/CMakeLists.txt
git commit -m "Fix ASIO foreground edge publication"
```

### Task 2: Separate Physical Preparation, Start, and Stability

**Files:**

- Modify: `src/Audio/Asio/AsioOutputBackend.cpp:690-1750`

**Interfaces:**

- Consumes: `AsioForegroundMonitor::snapshot()` from Task 1.
- Produces:
  `std::expected<void, PhysicalPreparationFailure> PreparePhysicalSession() noexcept`.
- Produces:
  `std::expected<void, AsioFailure> StartPreparedPhysicalSession() noexcept`.
- Produces:
  `std::expected<StableRenderOutcome, AsioFailure> WaitForStableRender() noexcept`.
- Produces: `std::optional<AsioFailure> ClosePhysicalSession() noexcept`, where
  every returned cleanup failure is fatal.
- Task 4 consumes the preparation failure disposition to implement bounded
  retries before `Start`.

- [ ] **Step 1: Record why no fake-driver test is added**

The behavior being changed is actual IASIO acquisition, `Start`, callback
arrival, and cleanup. A fake IASIO object would make assertions about the fake
rather than the hardware/driver boundary, and exposing private lifecycle
methods only for a test would violate the production API rule. Use the Task 1
test for the deterministic concurrency break, existing clock/sequencer tests
for arithmetic, and the ordered runtime run for this external contract.

- [ ] **Step 2: Split preparation from the irreversible Start boundary**

Replace `PhysicalSessionStartupOutcome` and `ScopedRuntimeFailure` with:

```cpp
enum class PhysicalPreparationFailureKind : std::uint8_t
{
    retryable_before_start,
    fatal,
};

struct PhysicalPreparationFailure final
{
    PhysicalPreparationFailureKind kind{
        PhysicalPreparationFailureKind::fatal};
    AsioFailure failure;
};

enum class StableRenderOutcome : std::uint8_t
{
    stable,
    shutdown,
};
```

`PreparePhysicalSession` performs driver resolution/creation,
`AsioSession::Prepare`, callback-runtime preparation/install, buffer creation,
driver-buffer views, and persistent-contract validation. Driver open,
negotiation, callback installation, and buffer creation failures are
`retryable_before_start`; allocation, arithmetic, existing-resource,
sequencer, monitor, and changed persistent-contract failures are `fatal`.

Do not call `AdvanceSilentRendering`, `BeginPhysicalSession`, or
`AsioSession::Start` from preparation.

- [ ] **Step 3: Make Start and stability one strict contract**

`StartPreparedPhysicalSession` must:

```cpp
const auto generation = logical_render_sequencer_.BeginPhysicalSession();
if (!generation)
{
    return std::unexpected(Failure(
        AsioFailureStage::runtime_clock,
        std::format(
            "Could not begin ASIO physical-session generation: {}",
            static_cast<unsigned>(generation.error()))));
}
active_physical_session_generation_.store(*generation, std::memory_order_release);
clock_tracker_.Reset(request_.buffer_frames,
                     session_->report().output_latency_frames);
has_previous_sample_position_ = false;
render_ready_.store(true, std::memory_order_release);
const auto started = session_->Start();
if (!started)
{
    render_ready_.store(false, std::memory_order_release);
    return std::unexpected(std::move(started.error()));
}
return {};
```

`WaitForStableRender` waits for stable render, fault, shutdown, monitor change,
or deadline. A monitor change only checks monitor health and continues waiting;
it never returns a focus outcome. A latched fault or stability timeout is
fatal. Shutdown returns `StableRenderOutcome::shutdown`.

- [ ] **Step 4: Make startup ignore focus until stability is proven**

`InitializeBackend` creates the monitor, timer, presented clock, render core,
then calls `PreparePhysicalSession` and `StartPreparedPhysicalSession` exactly
once. `ControlThreadMain` always calls `WaitForStableRender` before setting
`committed_`, invoking `StartupSucceeded`, or signaling successful startup.

After successful startup signaling, pass the current coherent foreground
snapshot into the committed lifecycle. Do not close the physical session or
report startup success before the stable callback proof.

- [ ] **Step 5: Make every running fault and every unsafe close fatal**

Remove `RuntimeFaultScope`, `PlanFailureScope`, `first_fault_scope_`, and the
physical-fault recovery branch. `LatchRuntimeFault` stores only the first
typed stage/domain/result. `MonitorCommittedRuntime` returns that failure
immediately whenever `HasPublishedFault()` becomes true.

Keep the existing overload counter advisory: overload publication alone must
not call `LatchRuntimeFault`. A measured long callback remains a summary datum
unless another checked invariant fails.

Change `ClosePhysicalSession` to one fatal result. It still executes this
order: clear `render_ready_`, begin callback stopping, call `Stop`, join and
uninstall the callback worker, end the sequencer physical generation, call
`AsioSession::Close`, clear buffer views, and reset session objects. Preserve
the first failure and append later failures, but never continue into another
session after any close failure.

- [ ] **Step 6: Format, compile, and run existing deterministic tests**

Use CLion formatting for the modified source/header. Build `gc_audio` and run
the existing ASIO clock/sequencer tests plus Task 1:

```powershell
$asioVcvars = 'C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat'
cmd.exe /d /s /c "call `"$asioVcvars`" && set `"GC_ASIO_SDK_DIR=H:\gc\artifacts\ASIOSDK`" && cmake --build --preset msvc32-debug --target gc_audio gc_exact_asio_clock_tests gc_asio_logical_render_sequencer_tests gc_asio_foreground_state_tests && ctest --preset msvc32-debug -R `"^(ExactAsioClock|AsioLogicalRenderSequencer|AsioForegroundState)$`""
```

Expected: compilation succeeds and all three deterministic tests pass.

- [ ] **Step 7: Commit the strict physical-session boundary**

```powershell
git add -- src/Audio/Asio/AsioOutputBackend.cpp
git commit -m "Make ASIO start and runtime faults strict"
```

### Task 3: Publish Exact Time Through Confirmed Suspension

**Files:**

- Modify: `src/Audio/Asio/AsioOutputBackend.cpp:1860-2135`
- Modify: `src/Audio/Asio/AsioOutputBackend.h:17-80`
- Modify: `src/Audio/AudioPatch.cpp:690-770`

**Interfaces:**

- Consumes: `AsioLogicalRenderPlan` and the persistent `ExactAsioClock`.
- Produces:
  `std::optional<AsioFailure> PublishExactAnchor(const AsioLogicalRenderPlan&, std::uint64_t presented_output_frame, bool detached) noexcept`.
- Produces: `detached_exact_anchor_publications` in
  `AsioRuntimeCountersSnapshot`.
- Task 4 consumes the same helper during suspended lifecycle advancement.

- [ ] **Step 1: Preserve the existing test boundary**

Do not add an `ExactAsioClock` test that merely submits arbitrary anchors: it
would already pass and would not exercise the missing backend publication.
Run `ExactAsioClock` and `AsioLogicalRenderSequencer` before editing and retain
their results as the arithmetic baseline.

- [ ] **Step 2: Separate mixer consumption from sequencer commit**

Change `RenderLogicalBlock` so it renders and records diagnostics but does not
call `logical_render_sequencer_.Commit`. Each caller owns this fixed sequence:

```text
claim plan -> consume mixer block -> publish required clocks -> commit plan
```

If anything fails after mixer consumption, release the claim with `Abandon`,
latch/return a fatal logical failure, and stop the backend. Never retry a
partially consumed logical block.

- [ ] **Step 3: Centralize exact-anchor publication**

Add `PublishExactAnchor` with this behavior:

```cpp
if (!enable_absolute_time_judgement_)
    return std::nullopt;
if (exact_clock_ == nullptr || exact_endpoint_generation_ == 0 ||
    exact_anchor_sequence_ == (std::numeric_limits<std::uint64_t>::max)())
    return Failure(AsioFailureStage::runtime_clock,
                   "ASIO exact clock publication contract is invalid");

const auto next_sequence = exact_anchor_sequence_ + 1;
if (!exact_clock_->Publish({
        .sequence = next_sequence,
        .endpoint_generation = exact_endpoint_generation_,
        .presented_output_frame = presented_output_frame,
        .system_time_ns = plan.system_time_ns,
        .submitted_output_tail = plan.submitted_output_tail,
    }))
    return Failure(AsioFailureStage::runtime_clock,
                   "ASIO exact clock rejected a logical continuity anchor");
exact_anchor_sequence_ = next_sequence;
if (detached)
    SaturatingIncrementCounter(detached_exact_anchor_publications_);
return std::nullopt;
```

Only stable physical callbacks and detached renders publish exact anchors.
Startup/recovery priming callbacks continue publishing presented continuity
but do not submit duplicate zero-latency exact positions.

- [ ] **Step 4: Publish clocks before committing each logical plan**

For a stable physical callback: render, convert, publish the exact anchor,
publish the presented clock, commit the sequencer plan, then call
`OutputReady` and signal stability.

For a priming callback: render, publish presented continuity, commit the plan,
clear the hardware block, and call `OutputReady`; do not publish exact time.

For `AdvanceSilentRendering`: compute
`presented_output_frame = max(0, output_frame_begin - logical_output_latency_frames_)`,
render, publish an exact anchor with `detached=true`, publish presented
continuity, commit, then increment `silent_advance_frames_`.

`AdvanceSilentRendering` is called only from `Suspended`/`Recovering`; neither
the helper nor a timer may put the backend into those states.

- [ ] **Step 5: Expose detached exact evidence in runtime summaries**

Add `detached_exact_anchor_publications` next to
`exact_anchor_publications` in `AsioRuntimeCountersSnapshot`, fill it in
`SnapshotCounters`, and log it in `ReportAsioRuntimeSummary`. Total exact
publication count remains the provider's counter; the detached field is its
suspension subset.

- [ ] **Step 6: Format, build, and rerun exact/sequencer tests**

Use CLion formatting on all three modified files. Build `gc_audio`, then run
`ExactAsioClock` and `AsioLogicalRenderSequencer` with the x86 Debug command
from Task 2. Expected: both pass, and no new fake/backend test exists.

- [ ] **Step 7: Commit logical and exact suspension continuity**

```powershell
git add -- src/Audio/Asio/AsioOutputBackend.cpp src/Audio/Asio/AsioOutputBackend.h src/Audio/AudioPatch.cpp
git commit -m "Preserve exact time during ASIO suspension"
```

### Task 4: Implement the Explicit Focus Lifecycle and Bounded Recovery

**Files:**

- Modify: `src/Audio/Asio/AsioOutputBackend.cpp:788-1810,2393-2456`
- Modify: `src/Audio/Asio/AsioOutputBackend.h:82-110`
- Modify: `src/Audio/AudioPatch.cpp:960-1050`

**Interfaces:**

- Consumes: coherent foreground snapshots from Task 1, split preparation and
  Start from Task 2, and detached exact publication from Task 3.
- Produces: the control-thread-only states `starting`, `running`,
  `suspending`, `suspended`, `recovering`, `fatal`, and `stopping`.
- Produces: `AsioSessionLifecycleRecord`, passed to
  `IAsioOutputObserver::SessionLifecycleChanged`.

- [ ] **Step 1: Define transition diagnostics without callback logging**

Replace the observer's loose `(event, recovery_attempt, failure)` parameters
with:

```cpp
struct AsioSessionLifecycleRecord final
{
    AsioSessionLifecycleEvent event{};
    bool foreground{};
    std::uint64_t focus_loss_generation{};
    std::uint64_t physical_session_generation{};
    std::uint64_t recovery_attempt{};
    std::uint32_t retry_delay_ms{};
    std::uint64_t logical_render_origin{};
    std::uint64_t physical_render_origin{};
};
```

Add `recovery_attempt_started` to `AsioSessionLifecycleEvent`; retain
`foreground_lost`, `session_released`, `foreground_regained`,
`recovery_attempt_failed`, and `session_recovered`. Remove
`physical_session_lost` as a recoverable transition: a running physical fault
now goes directly to `RuntimeFailed`.

The first callback of a new physical generation stores raw/logical handoff
origins in atomics only. The control thread includes them in
`session_recovered` after stability; the callback never invokes the observer.

- [ ] **Step 2: Replace boolean control flow with one control-thread state**

Define:

```cpp
enum class LifecycleState : std::uint8_t
{
    starting,
    running,
    suspending,
    suspended,
    recovering,
    fatal,
    stopping,
};
```

`ControlThreadMain` completes strict startup, publishes startup success, then
enters one `switch (state)` loop. Keep `state` and
`consumed_focus_loss_generation` control-thread-local. A snapshot whose
generation is lower than the consumed generation is fatal. A higher
generation is consumed exactly once.

After startup stability:

```cpp
const auto focus = foreground_monitor_->snapshot();
state = (!focus.is_foreground ||
         focus.loss_generation > consumed_focus_loss_generation)
    ? LifecycleState::suspending
    : LifecycleState::running;
```

Do not initialize the consumed generation from the post-start snapshot before
this check; doing so would erase a loss that occurred during startup.

- [ ] **Step 3: Implement Running, Suspending, and Suspended**

`Running` message-waits for fault, shutdown, foreground change, message input,
or summary deadline. It returns a latched fault immediately. It enters
`Suspending` when the latest snapshot is background or has a newer loss
generation, including loss+regain coalesced into current foreground true.

`Suspending` logs the loss generation, captures the active physical
generation, calls `ClosePhysicalSession`, and treats any returned failure as
fatal. Only a successful close enters `Suspended` and logs `session_released`.

`Suspended` calls `AdvanceSilentRendering` at `SilentPollIntervalMs`, checks
monitor health, consumes later loss generations while no physical session
exists, and enters `Recovering` only when the coherent snapshot is foreground.
It logs `foreground_regained` once per recovery cycle.

- [ ] **Step 4: Implement the bounded pre-Start recovery cycle**

Use named constants:

```cpp
constexpr std::array<DWORD, 2> kRecoveryRetryDelaysMs{1'000, 2'000};
constexpr std::uint64_t kMaximumRecoveryAttempts = 3;
```

Attempt 1 is immediate. After retryable failure 1, wait 1 second; after
retryable failure 2, wait 2 additional seconds; retryable failure 3 is fatal.
Each wait message-waits in intervals no longer than `SilentPollIntervalMs`,
continues detached rendering, and is interrupted by shutdown or a foreground
snapshot change.

For each attempt:

1. Recheck foreground and loss generation.
2. Log `recovery_attempt_started`.
3. Call `PreparePhysicalSession`.
4. On a retryable preparation failure, call `ClosePhysicalSession`; any close
   failure is fatal. Log `recovery_attempt_failed` with the next delay.
5. After successful preparation, recheck foreground/loss before `Start`. If a
   loss occurred, cleanly close the unstarted session and return to
   `Suspended` without using another attempt.
6. Call `StartPreparedPhysicalSession`. Any failure is fatal.
7. Call `WaitForStableRender`. Fault/timeout is fatal; shutdown stops.
8. Log `session_recovered` with handoff origins and enter `Running`, unless a
   post-stability snapshot requires immediate `Suspending`.

Delete `kRecoveryRetryMs`, `next_recovery_ms`, and every path that retries a
running fault, `Start` failure, stability failure, or cleanup failure.

- [ ] **Step 5: Keep retries diagnostic and normally cold**

Reset the per-cycle attempt number only after a new foreground regain. Keep
the aggregate counters `recovery_attempts`, `recovery_failures`, and
`session_recoveries`. Add the latest consumed focus generation to runtime
summary diagnostics. A normal Run 1 is expected to recover on attempt 1; a
delayed retry is evidence to investigate, not normal timing noise.

Update `ProductionAsioObserver` to log every field in
`AsioSessionLifecycleRecord` plus typed failure fields when present. Continue
using the explicit one-off stream because this variable record is not a
reusable formatting abstraction.

- [ ] **Step 6: Review the no-fallback and no-time-inference boundaries**

Use CLion navigation to inspect every caller of `PreparePhysicalSession`,
`StartPreparedPhysicalSession`, `AdvanceSilentRendering`, and
`SessionLifecycleChanged`. Confirm:

- preparation is called once in startup and only from `Recovering` afterward;
- `AdvanceSilentRendering` is reachable only from `Suspended`/`Recovering`;
- no elapsed-time branch changes foreground state;
- no ASIO failure reaches WASAPI startup; and
- every post-`Start` failure ends in the fatal path.

This is a focused code review, not a source-grep test.

- [ ] **Step 7: Format, compile, and run deterministic tests**

Use CLion formatting on the modified files. Build `gc_audio` and run all three
ASIO deterministic tests from Task 2. Expected: build and tests pass; no
runtime acceptance claim is made.

- [ ] **Step 8: Commit the explicit lifecycle**

```powershell
git add -- src/Audio/Asio/AsioOutputBackend.cpp src/Audio/Asio/AsioOutputBackend.h src/Audio/AudioPatch.cpp
git commit -m "Implement bounded ASIO focus recovery"
```

### Task 5: Stop Re-resolving an Active Stage Entry

**Files:**

- Modify:
  `src/Patches/AbsoluteJudgement/JudgementScheduler.cpp:252-326`

**Interfaces:**

- Consumes: the existing `JudgementClockResolver` binding and
  `JudgementStage::active()` state.
- Produces no new public type. It preserves existing provider/generation/QPC
  validation on every outer call.

- [ ] **Step 1: Retain the existing verification boundary**

Do not introduce a trivial policy helper or fake `JudgementScheduler` harness
only to count calls. The scheduler requires native stage, transport, cursor,
and fatal-invariant state; a partial fake would not exercise the real contract.
The observed `exact_history_lost_queries` counter after the 60-second retention
window is the integration oracle for this regression.

- [ ] **Step 2: Resolve stage entry only until activation**

Keep `ValidateStageBindingOrFatal(probe)` before this logic. Change the bound
resolver branch to validate identity unconditionally but resolve the immutable
entry only while inactive:

```cpp
else
{
    const auto endpoint_info = probe.endpoint->info();
    if (endpoint_info.endpoint_generation !=
            clock_resolver_.anchor().endpoint_generation ||
        probe.endpoint.get() != clock_resolver_.anchor().endpoint.get())
    {
        const auto expected_provider = reinterpret_cast<std::uintptr_t>(
            clock_resolver_.anchor().endpoint.get());
        const auto actual_provider =
            reinterpret_cast<std::uintptr_t>(probe.endpoint.get());
        Fatal(
            endpoint_info.endpoint_generation !=
                    clock_resolver_.anchor().endpoint_generation
                ? AbsoluteJudgementFatalPredicate::EndpointGenerationChanged
                : AbsoluteJudgementFatalPredicate::EndpointProviderIdentityChanged,
            AbsoluteJudgementFatalReason::EndpointGenerationChanged,
            {
                clock_resolver_.anchor().endpoint_generation,
                endpoint_info.endpoint_generation,
                expected_provider,
                actual_provider,
            });
    }
    if (!stage_.active())
    {
        entry_clock = clock_resolver_.Resolve(
            stage_.cutoff().stage_entry_time,
            gc::audio::ExactClockResolveIntent::FinalizedTimestamp);
    }
}

TryActivateOrWait(entry_clock);
```

`TryActivateOrWait` already returns immediately for an active stage, so the
default pending `entry_clock` is not consumed after activation. Do not weaken
provider identity, endpoint generation, QPC frequency, transport, history, or
native binding checks.

- [ ] **Step 3: Format, diagnose, and compile the scheduler**

Use CLion formatting on `JudgementScheduler.cpp`. Open that file, request
diagnostics, and resolve any new error/warning before moving on. Build the
loader Debug target; no runtime success claim follows from compilation.

- [ ] **Step 4: Commit the bounded judgement workload**

```powershell
git add -- src/Patches/AbsoluteJudgement/JudgementScheduler.cpp
git commit -m "Stop resolving active judgement stage entry"
```

### Task 6: Format, Diagnose, Build, and Hand Off Runtime Acceptance

**Files:** Every C++/header/CMake file changed by Tasks 1-5.

**Interfaces:**

- Consumes the completed implementation and all deterministic tests.
- Produces static evidence and an ordered runtime handoff; it does not deploy.

- [ ] **Step 1: Apply CLion formatting to every changed C++ file**

Use `reformat_file` on changed `.cpp`/`.h` files. Accept formatter-only changes
inside those files. Do not manually restyle unrelated code and do not run a
repository-wide formatter.

- [ ] **Step 2: Run CLion diagnostics one file at a time**

For each changed source/header, in this order:

1. open the file with `open_file_in_editor`;
2. request `get_file_problems` with errors and warnings;
3. resolve or explicitly justify each new diagnostic;
4. proceed to the next file without closing the prior file.

Do not batch diagnostic calls. Informational inspections are not errors.

- [ ] **Step 3: Review the focused diff and whitespace**

Run:

```powershell
git diff --check
git status --short --branch
git diff --stat
git diff -- src/Audio/Asio src/Audio/AudioPatch.cpp src/Patches/AbsoluteJudgement/JudgementScheduler.cpp tests/Audio/Asio tests/CMakeLists.txt
```

Review specifically for fallback, unbounded retry, focus-by-time inference,
post-`Start` continuation, stale fault reset, endpoint-generation replacement,
and callbacks that log or block.

- [ ] **Step 4: Configure, build, and test complete x86 Debug**

```powershell
$asioVcvars = 'C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat'
cmd.exe /d /s /c "call `"$asioVcvars`" && set `"GC_ASIO_SDK_DIR=H:\gc\artifacts\ASIOSDK`" && cmake --preset msvc32-debug && cmake --build --preset msvc32-debug && ctest --preset msvc32-debug"
```

Expected: configure, complete build, and every registered test pass.

- [ ] **Step 5: Configure, build, and test complete x86 Release**

```powershell
$asioVcvars = 'C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat'
cmd.exe /d /s /c "call `"$asioVcvars`" && set `"GC_ASIO_SDK_DIR=H:\gc\artifacts\ASIOSDK`" && cmake --preset msvc32-release && cmake --build --preset msvc32-release && ctest --preset msvc32-release"
```

Expected: configure, complete build, and every registered test pass.

- [ ] **Step 6: Commit any accepted formatter-only residue**

If CLion formatting changed tracked implementation files after their task
commits, review and commit only those paths:

```powershell
git add -- src/Audio/Asio/AsioForegroundState.h src/Audio/Asio/AsioForegroundMonitor.h src/Audio/Asio/AsioForegroundMonitor.cpp src/Audio/Asio/AsioOutputBackend.cpp src/Audio/Asio/AsioOutputBackend.h src/Audio/AudioPatch.cpp src/Patches/AbsoluteJudgement/JudgementScheduler.cpp tests/Audio/Asio/AsioForegroundStateTests.cpp tests/CMakeLists.txt
git commit -m "Format ASIO lifecycle recovery"
```

If formatting produced no residue, do not create an empty commit.

- [ ] **Step 7: Hand off Run 1 without deploying**

Report the exact Release DLL path and SHA-256. Ask the user to deploy/run it
using their normal runtime procedure, then perform only this acceptance run:

1. start normally;
2. shortly move the game to background;
3. return to foreground after suspension;
4. verify audible recovery and stop the run.

Analyze the resulting `H:\gc\loader-log.txt` only after the user confirms the
run is complete. Require startup `Start`/stability before release, one consumed
loss generation, detached logical and exact advancement, recovery attempt 1,
the same endpoint generation, a new physical generation, coherent handoff
origins, no WASAPI, no fatal, and no repeated retries.

- [ ] **Step 8: Hand off Run 2 only after Run 1 passes**

Ask for an all-foreground session containing at least two songs and one song
longer than 60 seconds. Analyze the new log for zero recovery, zero exact
`HistoryLost`/discontinuous results, zero render gaps and sample-position
discontinuities, no reset/resync/rate/buffer/latency request, stable callback
progression, and no workload growth with stage age.

Only the user can accept audio quality and judgement behavior. A transient
game-frame drop is not an ASIO loss and must not change the judgement clock.

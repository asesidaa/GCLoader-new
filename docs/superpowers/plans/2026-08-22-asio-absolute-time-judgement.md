# ASIO Absolute-Time Judgement Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use
> `superpowers:executing-plans` to implement this plan task-by-task inline.
> Do not dispatch subagents. Keep the checkboxes and execution record current
> so the work survives context compaction.

**Goal:** Extend the corrected absolute-time judgement path to the existing
ASIO backend so every retained input transition is projected from its captured
multimedia-clock timestamp, while WASAPI retains its accepted QPC behavior and
the game's native recognition, grading, sound, held-state, and score logic stay
unchanged.

**Architecture:** Capture one paired host timestamp (`QPC`, `timeGetTime`) at
each input observation and semantic-stage entry. A backend-neutral exact-output
provider selects the member belonging to the active audio domain: existing
WASAPI history resolves QPC, while a new preallocated ASIO history resolves the
multimedia tick against driver `systemTime + samplePosition` anchors. Both
produce the same checked rational output-frame coordinate consumed by the
current continuous stage-clock formula and scheduler.

**Tech stack:** C++23, MSVC x86, CMake/Ninja, Win32 QPC, WinMM multimedia
timers, Steinberg ASIO SDK, existing checked-rational timing and lock-free
publication patterns, PowerShell 7 build/inspection/deployment scripts.

**Design authority:**

- `docs/superpowers/specs/2026-08-22-asio-absolute-time-judgement-design.md`
- `docs/superpowers/specs/2026-08-22-absolute-time-judgement-correction-design.md`
- `docs/superpowers/audits/2026-08-21-absolute-judgement-authoritative-full-audit.md`
- Completed binary evidence E-042 through E-046 under
  `H:\gc\artifacts\GCLoader\.planning\debug\high-fps-timing-domains\evidence`

The August 19 design and its failed implementations remain historical warning
evidence. They are not implementation authority and must not be deleted.

## Mandatory execution rules

- [x] Work inline in
  `H:\gc\artifacts\GCLoader\.worktrees\asio-audio-backend`; use no agents.
- [x] Preserve the user-owned modification to
  `docs/superpowers/plans/2026-08-20-absolute-time-judgement.md`. Never edit,
  stage, revert, or reformat it.
- [ ] Use `apply_patch` for source and script edits. Use PowerShell 7 directly;
  do not wrap commands in another PowerShell process and do not use `cmd.exe`.
- [ ] Add no CTest, replay, emulated chart, arbitrary expected-value test, or
  synthetic judgement oracle. Build/static proof and user-run game behavior
  are separate; only the latter is runtime acceptance.
- [ ] Add no judgement timeout, stage timeout, provider-data timeout, or
  stock/native fallback. `Pending` and `TemporarilyUnavailable` wait without a
  deadline. Do not alter the ASIO backend's pre-existing audio-startup deadline.
- [ ] Do not touch `src/Patches/Framerate/**`, including the independent
  `0x664DB2` hook, and do not change the game's render/update frame unit.
- [ ] Do not change judgement windows, `JudgTimeOffset`, held-age semantics,
  native recognition/score/sound calls, event/heartbeat topology, the
  newest-32 overload policy, or the accepted loss policies.
- [ ] All new formatting uses `std::format`. Add no new C++ `try`/`catch`.
  Do not broaden this work into removal of unrelated legacy exception handling.
- [ ] Add no allocation, locking, formatting, or logging in an ASIO callback;
  add no allocation per input transition or judgement query. The exact ASIO
  ring may allocate once with `std::nothrow` before `ASIOStart`.
- [ ] Treat success-only API failures as directly observed fatal contracts with
  a clear stage, API/result domain, numeric result, and detail. Do not silently
  continue and do not add recovery branches.
- [ ] The raw multimedia value `0` is valid. Never use it as a missing-value
  sentinel.
- [ ] Keep all timestamp-to-frame calculations checked and rational. Never
  accumulate rounded milliseconds or frames, including at 144, 165, or 240 FPS.
- [ ] Stage only explicitly named files in each commit.
- [ ] Keep ConfigGUI synchronized with runtime validation: its label, help,
  inline warning, Debug/Release artifact, and deployed executable must all
  accept and describe both supported exact-clock backends.

## Lifecycle clarification required by the live code

`AudioPatchInit()` only commits the `DirectSoundCreate8` hook during DLL attach.
The game instantiates the actual WASAPI or ASIO backend later, through the first
hooked DirectSound creation. Therefore:

1. DLL attach validates the configured backend, the 1000-Hz input requirement,
   and audio-hook commitment only.
2. The first loader-owned state-18 call acquires the actual exact provider and
   compares its domain with the configured backend before querying gameplay
   cursor history, draining input, or executing any native judgement scope.
3. A missing provider is immediately fatal. It is not allowed to remain
   `Pending` until stage exit.
4. A configured-ASIO run that took the existing pre-commit WASAPI audio fallback
   sees `expected=asio_multimedia_ms`, `actual=wasapi_qpc` and stops before the
   first judgement. It can never be reported as an ASIO experiment.
5. Provider identity, generation, and domain continue to be validated on every
   owned call; no mid-stage provider switch is accepted.

This is an observation-point decision, not a time-derived lifecycle decision.
The semantic lifecycle stays exactly
`NON_STAGE -> STAGE_ENTRY -> ACTIVE_STAGE -> STAGE_EXIT -> NON_STAGE` for any
number of songs, with Test Mode entry remaining an explicit valid stage exit.

---

### Task 1: Introduce the backend-neutral exact-output contract without changing WASAPI math

**Files:**

- Create: `src/Timing/AbsoluteHostTime.h`
- Create: `src/Audio/ExactOutputClock.h`
- Create: `src/Audio/ExactOutputClock.cpp`
- Modify: `src/Audio/Wasapi/ExactWasapiClock.h`
- Modify: `src/Audio/Wasapi/ExactWasapiClock.cpp`
- Modify: `src/Audio/Wasapi/ExclusiveAudioEngine.cpp`
- Modify: `src/Audio/CMakeLists.txt`
- Record: `docs/superpowers/plans/2026-08-22-asio-absolute-time-judgement.md`

- [x] **Step 1: Capture the implementation baseline**

Run from the worktree:

```powershell
$repo = 'H:\gc\artifacts\GCLoader\.worktrees\asio-audio-backend'
git -C $repo status --short
git -C $repo rev-parse HEAD
```

Expected source baseline: the plan commit created from this document. Record the
hash in the Execution Record. The only unrelated dirty path must remain the
August 20 plan named above.

- [x] **Step 2: Add one paired host-time value type**

Create `src/Timing/AbsoluteHostTime.h` as a header-only value type so Input and
Audio depend on Timing rather than on each other:

```cpp
#pragma once

#include <cstdint>

namespace gc::timing {

struct AbsoluteHostTime final {
    std::int64_t qpc_ticks{};
    std::uint32_t multimedia_time_ms{};
};

} // namespace gc::timing
```

Do not add a validity helper that rejects `multimedia_time_ms == 0`. QPC retains
its existing positive-value contract.

- [x] **Step 3: Define the provider domain, immutable information, counters, and registry**

Create `src/Audio/ExactOutputClock.h` with these public concepts:

```cpp
enum class ExactOutputClockDomain : std::uint8_t {
    WasapiQpc,
    AsioMultimediaMilliseconds,
};

std::string_view ExactOutputClockDomainName(
    ExactOutputClockDomain domain) noexcept;

struct ExactOutputClockInfo final {
    ExactOutputClockDomain domain{};
    std::uint64_t endpoint_generation{};
    std::int64_t qpc_frequency{};
    std::uint32_t output_sample_rate{};
    std::uint32_t period_frames{};
    std::uint32_t output_latency_frames{};
    std::uint32_t timestamp_quantum_ns{};
};

struct ExactOutputClockCounters final {
    std::uint64_t publication_count{};
    std::uint64_t resolved_queries{};
    std::uint64_t pending_queries{};
    std::uint64_t temporarily_unavailable_queries{};
    std::uint64_t history_lost_queries{};
    std::uint64_t discontinuous_queries{};
};

class ExactOutputClock {
public:
    virtual ~ExactOutputClock() = default;
    virtual ExactOutputClockResult Resolve(
        const gc::timing::AbsoluteHostTime& timestamp) const noexcept = 0;
    virtual ExactOutputClockInfo info() const noexcept = 0;
    virtual ExactOutputClockCounters counters() const noexcept = 0;
    virtual void Invalidate() noexcept = 0;
};

std::shared_ptr<const ExactOutputClock>
AcquireExactOutputClock() noexcept;

namespace detail {
std::uint64_t NextExactOutputClockGeneration() noexcept;
bool RegisterExactOutputClock(
    const std::shared_ptr<ExactOutputClock>& provider) noexcept;
void UnregisterExactOutputClock(
    std::uint64_t expected_generation) noexcept;
} // namespace detail
```

Implement the names and the current weak-provider registry in
`ExactOutputClock.cpp`. Move the single global endpoint-generation counter here
so WASAPI and ASIO cannot accidentally reuse independent generation spaces.
Registration remains outside callbacks and preserves the current generation
matching/invalidation behavior.

`ExactOutputClockDomainName` returns the stable log tokens `wasapi_qpc` and
`asio_multimedia_ms`; any out-of-range enum formats as `invalid`.

`ExactClockStatus::NoPlayback` and `OutsidePlayback` remain in
`ExactAudioTime.h` because playback-history code still uses the shared enum.
Neither exact-output provider may emit either status. Do not revive
`OutsidePlayback` as an input disposition.

- [x] **Step 4: Adapt `ExactWasapiClock` as a behavior-preserving provider**

Derive `ExactWasapiClock` from `ExactOutputClock` and add:

```cpp
ExactOutputClockResult Resolve(
    const gc::timing::AbsoluteHostTime& timestamp) const noexcept override;
ExactOutputClockInfo info() const noexcept override;
ExactOutputClockCounters counters() const noexcept override;
void Invalidate() noexcept override;
```

`Resolve()` must call the existing `ResolveQpc(timestamp.qpc_ticks)` and update
only diagnostic counters from its returned status. Keep `ResolveQpc()` and all
of its QPC splitting, anchor selection, submitted-tail checks, rational math,
history capacity, and return semantics functionally unchanged. This prevents
ASIO support from perturbing the accepted WASAPI result.

Move only the registry and generation code out of
`ExactWasapiClock.cpp`. Replace the three WASAPI-specific registry function
names in `ExclusiveAudioEngine.cpp` with the generic names; keep concrete
WASAPI ownership and publication unchanged.

For WASAPI `info()` report:

- `domain = WasapiQpc`;
- the existing endpoint generation, QPC frequency, output rate, and period;
- `output_latency_frames = 0` and `timestamp_quantum_ns = 0`, because neither
  field participates in WASAPI resolution.

Retain `period_frames` as a new immutable metadata member in
`ExactWasapiClock`; it is diagnostic-only and must not enter the accepted
resolution calculation.

- [x] **Step 5: Add the registry source to `gc_audio` and build**

Add `ExactOutputClock.cpp` to `src/Audio/CMakeLists.txt`, then run:

```powershell
& 'H:\gc\temp\build-asio-audio-backend.ps1' `
    -Preset msvc32-debug -Target iDmacDrv32
```

Expected proof: CMake/Ninja exits 0 and produces
`build-msvc32-debug\dist\iDmacDrv32.dll`. Do not run CTest.

- [x] **Step 6: Record and commit Task 1**

Update the Execution Record with the build result. Stage only the files listed
for Task 1 and commit:

```powershell
git add -- `
  src/Timing/AbsoluteHostTime.h `
  src/Audio/ExactOutputClock.h `
  src/Audio/ExactOutputClock.cpp `
  src/Audio/Wasapi/ExactWasapiClock.h `
  src/Audio/Wasapi/ExactWasapiClock.cpp `
  src/Audio/Wasapi/ExclusiveAudioEngine.cpp `
  src/Audio/CMakeLists.txt `
  docs/superpowers/plans/2026-08-22-asio-absolute-time-judgement.md
git commit -m "Refactor exact output clock provider"
```

---

### Task 2: Carry adjacent QPC and multimedia observations through input and stage entry

**Files:**

- Modify: `src/Input/CMakeLists.txt`
- Modify: `src/Input/Polling/GameplayTransitionJournal.h`
- Modify: `src/Input/Polling/GameplayTransitionJournal.cpp`
- Modify: `src/Input/Polling/InputPollingRuntime.cpp`
- Modify: `src/Patches/AbsoluteJudgement/JudgementStage.h`
- Modify: `src/Patches/AbsoluteJudgement/JudgementStage.cpp`
- Modify: `src/Patches/AbsoluteJudgement/JudgementScheduler.h`
- Modify: `src/Patches/AbsoluteJudgement/JudgementScheduler.cpp`
- Modify: `src/Patches/AbsoluteJudgement/AbsoluteJudgementRuntime.cpp`
- Modify: `src/Patches/AbsoluteJudgement/AbsoluteJudgementDiagnostics.h`
- Modify: `src/Patches/AbsoluteJudgement/AbsoluteJudgementDiagnostics.cpp`
- Record: `docs/superpowers/plans/2026-08-22-asio-absolute-time-judgement.md`

- [x] **Step 1: Make each journal record carry one inseparable observation**

In `GameplayTransitionJournal.h` replace the standalone record QPC field with:

```cpp
gc::timing::AbsoluteHostTime observed_time{};
```

Replace `GameplayTransitionCutoff::stage_entry_qpc` with:

```cpp
gc::timing::AbsoluteHostTime stage_entry_time{};
```

Change `PublishGameplayTransition` and `CaptureGameplayTransitionCutoff` to
accept `AbsoluteHostTime`. Update every QPC comparison and diagnostic access to
use `.qpc_ticks`. The stage-entry handoff count remains based on QPC exactly as
today; multimedia time does not introduce a second cutoff or loss policy.

- [x] **Step 2: Capture the two clocks adjacently at the existing input observation point**

In `InputPollingRuntime::Publish`, when absolute publication is enabled:

1. call `QueryPerformanceCounter` and retain the existing fatal check;
2. immediately call `timeGetTime()`;
3. keep the existing atomic published-input exchange;
4. publish the paired value only when the held mask changed.

Capture both values on every enabled poll at the same existing observation
point so branch timing does not retimestamp only changed polls. Do not reject a
multimedia value of zero. Add `winmm` to `gc_input`'s explicit link libraries.

- [x] **Step 3: Carry the paired stage-entry watermark**

Capture QPC and `timeGetTime()` adjacently in
`BeginAbsoluteJudgementSemanticStage` immediately before the synchronized
journal cutoff. Change `JudgementStage::Begin` and
`JudgementScheduler::BeginSemanticStage` to carry the full value. Store it in
the cutoff and continue giving the current QPC member to the still-WASAPI-only
resolver until Task 3.

Update reset, cleanup cutoff recapture, delivery-delay, and fatal-snapshot code
to read `.qpc_ticks`. Add `stage_entry_multimedia_time_ms` beside
`stage_entry_qpc` in the bounded semantic-stage-open diagnostic.

- [x] **Step 4: Build without changing judgement behavior**

```powershell
& 'H:\gc\temp\build-asio-audio-backend.ps1' `
    -Preset msvc32-debug -Target iDmacDrv32
```

Expected proof: build exits 0. At this task boundary WASAPI still selects only
the QPC member, so judgement behavior is unchanged.

- [x] **Step 5: Record and commit Task 2**

Stage only the Task 2 files plus this plan and commit:

```powershell
git add -- `
  src/Input/CMakeLists.txt `
  src/Input/Polling/GameplayTransitionJournal.h `
  src/Input/Polling/GameplayTransitionJournal.cpp `
  src/Input/Polling/InputPollingRuntime.cpp `
  src/Patches/AbsoluteJudgement/JudgementStage.h `
  src/Patches/AbsoluteJudgement/JudgementStage.cpp `
  src/Patches/AbsoluteJudgement/JudgementScheduler.h `
  src/Patches/AbsoluteJudgement/JudgementScheduler.cpp `
  src/Patches/AbsoluteJudgement/AbsoluteJudgementRuntime.cpp `
  src/Patches/AbsoluteJudgement/AbsoluteJudgementDiagnostics.h `
  src/Patches/AbsoluteJudgement/AbsoluteJudgementDiagnostics.cpp `
  docs/superpowers/plans/2026-08-22-asio-absolute-time-judgement.md
git commit -m "Capture dual-domain judgement timestamps"
```

---

### Task 3: Make the resolver and scheduler consume the backend-neutral provider

**Files:**

- Modify: `src/Patches/AbsoluteJudgement/JudgementClockResolver.h`
- Modify: `src/Patches/AbsoluteJudgement/JudgementClockResolver.cpp`
- Modify: `src/Patches/AbsoluteJudgement/JudgementScheduler.h`
- Modify: `src/Patches/AbsoluteJudgement/JudgementScheduler.cpp`
- Modify: `src/Patches/AbsoluteJudgement/AbsoluteJudgementRuntime.cpp`
- Record: `docs/superpowers/plans/2026-08-22-asio-absolute-time-judgement.md`

- [x] **Step 1: Generalize resolver ownership and signatures**

Replace all concrete `shared_ptr<const ExactWasapiClock>` members with
`shared_ptr<const ExactOutputClock>`. Change the resolver contract to:

```cpp
void Reset(
    std::uint64_t stage_generation,
    gc::timing::AbsoluteHostTime stage_entry_time,
    std::int32_t game_time_offset_ms) noexcept;

JudgementClockResult TryBind(
    const gc::audio::GameplayAudioCursorObservation& selected,
    std::shared_ptr<const gc::audio::ExactOutputClock> endpoint,
    std::span<gc::audio::ExactPlaybackEpoch> scratch) noexcept;

JudgementClockResult Resolve(
    const gc::timing::AbsoluteHostTime& timestamp) const noexcept;
```

Every exact endpoint lookup calls `endpoint->Resolve(timestamp)`. Do not alter
playback-epoch selection, the first qualifying group-2 `Play`, the continuous
stage formula, signed pre-origin time, or the checked rational conversion:

```text
J(T) = S0/Fs + GameTimeOffset/1000 + (O(T) - O0)/Fo
```

At every former concrete accessor, read endpoint generation/QPC frequency from
`endpoint->info()` and publication totals from `endpoint->counters()`. Provider
object identity remains the `shared_ptr` target address already compared by the
scheduler.

- [x] **Step 2: Generalize scheduler probes and all three resolution sites**

Change `AbsoluteJudgementOuterProbe` to carry:

```cpp
std::shared_ptr<const gc::audio::ExactOutputClock> endpoint;
gc::timing::AbsoluteHostTime now{};
```

Resolve:

- stage entry from `cutoff.stage_entry_time`;
- each unresolved event from `record.observed_time`;
- the current ready horizon from `probe.now`.

Continue using QPC members for queue-age, delivery-delay, monotonic diagnostic,
and fatal operands. Do not use multimedia timestamps for sequence ordering;
journal sequence remains authoritative when two transitions share one
millisecond tick.

- [x] **Step 3: Capture both clocks for each outer horizon**

In `AbsoluteJudgementRuntime.cpp`, replace repeated raw QPC snippets with one
small `noexcept` helper that captures a positive QPC or emits the existing
formal fatal, then reads `timeGetTime()` without treating zero as an error.
Use it at stage entry and at each outer probe. Acquire the generic provider with
`AcquireExactOutputClock()`.

Do not add provider-domain validation in this task; configuration still rejects
ASIO, and Task 6 adds the explicit lazy-start validation point after the ASIO
provider exists.

- [x] **Step 4: Build the preserved WASAPI route**

```powershell
& 'H:\gc\temp\build-asio-audio-backend.ps1' `
    -Preset msvc32-debug -Target iDmacDrv32
```

Expected proof: build exits 0; all judgement scheduling now uses the neutral
contract, while the only constructible provider is still WASAPI and delegates
to unchanged `ResolveQpc` math.

- [x] **Step 5: Record and commit Task 3**

```powershell
git add -- `
  src/Patches/AbsoluteJudgement/JudgementClockResolver.h `
  src/Patches/AbsoluteJudgement/JudgementClockResolver.cpp `
  src/Patches/AbsoluteJudgement/JudgementScheduler.h `
  src/Patches/AbsoluteJudgement/JudgementScheduler.cpp `
  src/Patches/AbsoluteJudgement/AbsoluteJudgementRuntime.cpp `
  docs/superpowers/plans/2026-08-22-asio-absolute-time-judgement.md
git commit -m "Drive judgement through exact output clock interface"
```

---

### Task 4: Implement the preallocated exact ASIO history and rational resolver

**Files:**

- Create: `src/Audio/Asio/ExactAsioClock.h`
- Create: `src/Audio/Asio/ExactAsioClock.cpp`
- Modify: `src/Audio/CMakeLists.txt`
- Record: `docs/superpowers/plans/2026-08-22-asio-absolute-time-judgement.md`

- [x] **Step 1: Define the ASIO anchor and provider**

Use a concrete class deriving from `ExactOutputClock` and an anchor containing:

```cpp
struct ExactAsioAnchor final {
    std::uint64_t sequence{};
    std::uint64_t endpoint_generation{};
    std::uint64_t presented_output_frame{};
    std::uint64_t system_time_ns{};
    std::uint64_t submitted_output_tail{};
};
```

`Create()` accepts endpoint generation, 48-kHz output rate, positive QPC
frequency, buffer/period frames, and reported output-latency frames. Allocate a
60-second ring plus two slots with `new (std::nothrow)` before stream commit.
Use the same versioned-slot, three-attempt stable-read pattern as
`ExactWasapiClock`; all slot fields and counters read by another thread must be
lock-free atomics.

- [x] **Step 2: Publish only stable, successfully rendered ASIO anchors**

`Publish` must verify the fixed generation, nonzero strictly increasing
sequence, strictly increasing presented frame, nondecreasing submitted tail,
and a valid tail not behind the presented frame. It must not require raw
`system_time_ns` to increase as an ordinary 64-bit integer across multimedia
clock wrap. Existing `AsioClockTracker` remains the authority for callback and
sample-position continuity before publication.

Return `false` after invalidating on any publication-contract failure so the
ASIO owner can latch `AsioFailureStage::runtime_clock`. Return `true` only after
the slot is fully published. Do not log or allocate here.

- [x] **Step 3: Resolve a multimedia event tick with modular signed arithmetic**

For each retained anchor, compute:

```text
anchor_ms       = low32(system_time_ns / 1,000,000)
anchor_remainder= system_time_ns % 1,000,000
delta_bits      = uint32(event_multimedia_ms - anchor_ms)
delta_ms        = signed two's-complement interpretation of delta_bits
delta_ns        = delta_ms * 1,000,000 - anchor_remainder
```

Use `std::bit_cast<std::int32_t>(delta_bits)` for the signed modular
interpretation. The raw event value zero is valid. Scan newest to oldest and
select the newest stable anchor with `delta_ns >= 0`. Then calculate without a
whole-frame rounding step:

```text
output_frame = presented_output_frame
             + delta_ns * 48,000 / 1,000,000,000
```

Implement the second term as `CheckedRational`, not as an integer division or
floating-point value. Return:

- `Pending` when there is no publication yet, when the event precedes the first
  not-yet-wrapped history, or when the rational result reaches/exceeds the
  latest submitted tail;
- `TemporarilyUnavailable` when relevant slots cannot be read stably;
- `HistoryLost` only when the ring has wrapped and the event is earlier than
  the oldest retained anchor;
- `Discontinuous` for invalidation, generation/anchor contradiction, or failed
  checked arithmetic;
- `Resolved` with rational output frame, submitted tail, anchor sequence, and
  anchor sample position otherwise.

Never emit `NoPlayback` or `OutsidePlayback`. The playback timeline—not this
provider—binds the stage origin, and `O(T) < O0` remains a valid signed result.

- [x] **Step 4: Expose immutable provider information and cumulative counters**

Report `domain=AsioMultimediaMilliseconds`, `timestamp_quantum_ns=1'000'000`,
and the exact generation, QPC frequency, 48-kHz output rate, period frames, and
reported output latency. Count publications and each returned query status with
atomics; do not add per-query log records.

- [x] **Step 5: Add the provider to `gc_audio` and build it unused**

Add `Asio/ExactAsioClock.cpp` to `src/Audio/CMakeLists.txt` (not the lower-level
`gc_asio` target, which would create the wrong dependency direction), then run:

```powershell
& 'H:\gc\temp\build-asio-audio-backend.ps1' `
    -Preset msvc32-debug -Target iDmacDrv32
```

Expected proof: build exits 0. The provider exists but is not registered or
published yet, so the active runtime route remains unchanged.

- [ ] **Step 6: Record and commit Task 4**

```powershell
git add -- `
  src/Audio/Asio/ExactAsioClock.h `
  src/Audio/Asio/ExactAsioClock.cpp `
  src/Audio/CMakeLists.txt `
  docs/superpowers/plans/2026-08-22-asio-absolute-time-judgement.md
git commit -m "Implement exact ASIO output clock history"
```

---

### Task 5: Own the ASIO timer period, provider, publication, and playback history in one lifecycle

**Files:**

- Modify: `src/Audio/Asio/AsioTypes.h`
- Modify: `src/Audio/Asio/AsioCallbackRuntime.cpp`
- Modify: `src/Audio/Asio/AsioProbeProtocol.cpp`
- Modify: `src/Audio/Asio/AsioOutputBackendInternal.h`
- Modify: `src/Audio/Asio/AsioOutputBackend.h`
- Modify: `src/Audio/Asio/AsioOutputBackend.cpp`
- Modify: `src/Audio/AudioPatch.cpp`
- Record: `docs/superpowers/plans/2026-08-22-asio-absolute-time-judgement.md`

- [ ] **Step 1: Add explicit WinMM failure vocabulary**

Append (do not renumber existing values) `multimedia_timer` to
`AsioFailureStage` and `winmm` to `AsioResultDomain`. Update:

- `asio_failure_stage_name` and `asio_result_domain_name` in `AudioPatch.cpp`;
- the uint8 range assertion in `AsioCallbackRuntime.cpp`;
- the closed enum bounds in `AsioProbeProtocol.cpp`; and
- `RuntimeFailureDetail` so teardown reports
  `timeEndPeriod(1) failed during ASIO absolute-clock teardown`.

This is shared diagnostic vocabulary only; it does not change ASIO probe or
control-panel behavior.

- [ ] **Step 2: Inject and pair the multimedia timer APIs**

Add these actions to `AsioOutputBackendActions` and production wrappers around
the corresponding WinMM calls:

```cpp
MMRESULT (*begin_timer_period)(void*, UINT) noexcept{};
MMRESULT (*end_timer_period)(void*, UINT) noexcept{};
```

Pass `enable_absolute_time_judgement` from `ProductionDetourState` through
`ProductionAsioOutputBackendFactory`, `AsioOutputBackend::StartAndWait`, the
detail startup function, and `AsioOutputBackendState`.

When the feature is off, missing timer actions are irrelevant and ASIO behavior
stays unchanged. When it is on, require both actions and call
`timeBeginPeriod(1)` once at the beginning of backend initialization, before
callback creation. A non-`TIMERR_NOERROR` result returns a direct ASIO startup
failure with stage `multimedia_timer`, domain `winmm`, numeric result, and the
exact API name.

Track one `timer_period_active_` bit. Teardown calls `timeEndPeriod(1)` at most
once and clears ownership regardless of result. Non-success latches the same
explicit failure stage/domain and is not ignored. If teardown follows another
ASIO startup failure, append the `timeEndPeriod(1)` stage/domain/result as a
bounded `std::format` secondary detail on the startup `AsioFailure`; otherwise
report it as the primary teardown/runtime failure.

- [ ] **Step 3: Create and register the exact provider before `ASIOStart`**

After session latency, callback-runtime QPC frequency, driver buffers, and
render core are known—but before `render_ready_` and `session_->Start()`:

1. get the next generic endpoint generation;
2. create `ExactAsioClock` with 48 kHz, the callback runtime's QPC frequency,
   requested buffer frames, and `session_->report().output_latency_frames`;
3. register it in the generic exact-provider registry;
4. fail ASIO startup explicitly if any step returns its failure value.

The existing pre-commit audio fallback may then start WASAPI. The exact ASIO
provider must be invalidated/unregistered during the failed ASIO teardown before
WASAPI registers its own provider.

- [ ] **Step 4: Publish beside the existing presented cursor only after successful render**

For each stable callback, after successful render/conversion and after proving
the submitted tail addition representable:

1. increment a nonzero exact-anchor sequence with an overflow check;
2. publish `{sequence, generation, decision.presented_output_frame,
   decision.system_time_ns, submitted_tail}` to `ExactAsioClock`;
3. if publication returns false, clear/latch `runtime_clock` and do not continue;
4. keep the existing `AsioPresentedClockPublication::Publish` for the
   DirectSound-compatible current cursor;
5. call the existing `outputReady` and stable-render signal.

Do not use callback-entry QPC. Do not log, lock, allocate, or format in this
path.

- [ ] **Step 5: Configure exact gameplay playback history in ASIO `CreateVoice`**

Mirror the accepted WASAPI rule when the feature is enabled and
`usage == GameplayNativeCandidate`:

- require the exact ASIO provider, timeline, and nonzero buffer-instance id;
- call `ConfigureExactPlaybackHistory(buffer_instance_id,
  endpoint_generation)` before creating the voice;
- on a directly observed failure, set the output result, latch an explicit ASIO
  runtime failure, and return null.

Do not add any sound call or note policy. This step only exposes the existing
output/source playback history to the existing judgement clock binder.

- [ ] **Step 6: Enforce teardown ordering**

Preserve the ASIO control-thread owner and order teardown as:

```text
stop accepting renders
-> ASIOStop
-> join/uninstall callback worker
-> invalidate exact provider
-> unregister its exact generation
-> reset provider ownership
-> close/dispose the existing ASIO session and render state
-> timeEndPeriod(1)
```

Every initialization failure after timer acquisition must pass through the same
idempotent teardown. Have the timer-release helper return its optional failure
so the startup paths can preserve the secondary result described above instead
of losing it behind the original failure. Card scans, songs, results, and Test
Mode do not touch this backend/process lifetime.

- [ ] **Step 7: Add provider counters to bounded ASIO summaries**

Extend `AsioRuntimeCountersSnapshot` and the existing ASIO summary formatter
with exact-anchor publications and resolved/pending/temporarily-unavailable/
history-lost/discontinuous query counts. Snapshot the provider before resetting
it on teardown. Do not add per-callback or per-query logging.

Do not add stream-insertion formatting for these fields. Either append one
`std::format`-produced suffix to the legacy summary or convert the touched
formatter wholly to `std::format`; no new `ostringstream` expression is
allowed.

- [ ] **Step 8: Build the wired ASIO backend**

```powershell
& 'H:\gc\temp\build-asio-audio-backend.ps1' `
    -Preset msvc32-debug -Target iDmacDrv32
```

Expected proof: build exits 0. Configuration still prevents enabling the new
judgement route until Task 6.

- [ ] **Step 9: Record and commit Task 5**

```powershell
git add -- `
  src/Audio/Asio/AsioTypes.h `
  src/Audio/Asio/AsioCallbackRuntime.cpp `
  src/Audio/Asio/AsioProbeProtocol.cpp `
  src/Audio/Asio/AsioOutputBackendInternal.h `
  src/Audio/Asio/AsioOutputBackend.h `
  src/Audio/Asio/AsioOutputBackend.cpp `
  src/Audio/AudioPatch.cpp `
  docs/superpowers/plans/2026-08-22-asio-absolute-time-judgement.md
git commit -m "Wire exact judgement clock into ASIO lifecycle"
```

---

### Task 6: Enable ASIO configuration and make actual provider selection explicit before judgement

**Files:**

- Modify: `src/Config/config.cpp`
- Modify: `src/Patches/AbsoluteJudgement/AbsoluteJudgementPatch.cpp`
- Modify: `src/Patches/AbsoluteJudgement/AbsoluteJudgementRuntime.h`
- Modify: `src/Patches/AbsoluteJudgement/AbsoluteJudgementRuntime.cpp`
- Modify: `src/Patches/AbsoluteJudgement/JudgementScheduler.cpp`
- Modify: `src/Patches/AbsoluteJudgement/AbsoluteJudgementDiagnostics.h`
- Modify: `src/Patches/AbsoluteJudgement/AbsoluteJudgementDiagnostics.cpp`
- Modify: `tools/ConfigGUI/Main.cpp`
- Record: `docs/superpowers/plans/2026-08-22-asio-absolute-time-judgement.md`

- [ ] **Step 1: Accept exactly WASAPI or ASIO in configuration**

Change validation to accept absolute judgement only for
`wasapi_exclusive` or `asio`; keep DirectSound rejected and keep exactly
`input_poll_hz = 1000`. Update the error text to name both supported backends.
With the feature disabled, all three audio backends retain existing behavior.

Update `tools/ConfigGUI/Main.cpp` at the same boundary:

- rename the checkbox from `Absolute-time judgement (WASAPI)` to the
  backend-neutral `Absolute-time judgement`;
- make its tooltip state that WASAPI exclusive or ASIO is required;
- show the inline save warning only when the selected backend is neither
  WASAPI exclusive nor ASIO; and
- name both accepted backends in that warning.

The GUI already gates saving through the shared `ValidateInputConfig`; do not
duplicate another semantic validator in the editor model.

- [ ] **Step 2: Separate hook capability from the lazily created actual provider**

At DLL attach:

- replace `AudioBackendNotWasapiExclusive` with a backend-neutral unsupported
  backend predicate;
- replace `ExactWasapiRouteUnavailable` with a backend-neutral audio-hook route
  predicate;
- map configured WASAPI to `WasapiQpc` and configured ASIO to
  `AsioMultimediaMilliseconds`;
- pass that expected domain into `InitializeAbsoluteJudgementRuntime`;
- log configured backend and `audio_hook_committed`, not a fabricated active
  provider.

Do not call `AcquireExactOutputClock()` at DLL attach; the engine has not yet
been created there.

- [ ] **Step 3: Validate the actual provider on the first and every owned call**

At the start of `DispatchOuterCall`, before the gameplay group cursor query and
before `scheduler_.PrepareOuterCall`:

```cpp
auto endpoint = gc::audio::AcquireExactOutputClock();
if (!endpoint) {
    // fatal: expected domain observed, actual provider absent
}
const auto endpoint_info = endpoint->info();
if (endpoint_info.domain != expected_domain_) {
    // fatal: log expected and actual domain
}
```

Add closed fatal predicates for actual-provider missing and provider-domain
mismatch. Give both explicit descriptions/operands and classify them as
explicitly unsupported endpoint capability. They must reach the existing
logged fatal/message-box/dump boundary. Do not replace them with `assert()` or
an unlogged abort.

Pass the already validated provider into the outer probe. Remove the old
"null means wait until stage exit" behavior; `Pending` applies to provider data,
not provider existence.

- [ ] **Step 4: Log the bound provider facts at stage activation**

Extend the bounded activation record with:

- provider-domain name;
- endpoint generation and QPC frequency;
- output rate and period frames;
- output-latency frames;
- timestamp quantum in nanoseconds; and
- provider publication count.

The first activation record is the first point where both the lazy audio route
and exact gameplay playback origin are proven. Existing ASIO startup logging
already supplies driver name, selected channels, buffer/callback period, and
reported latency; do not duplicate verbose driver metadata.

- [ ] **Step 5: Distinguish scheduler `Pending` from temporary unavailability**

Add a `pending_clock_reads` stage counter and include it in periodic/final
summaries. At the unresolved-event and current-horizon sites that already
increment `exact_clock_reads`, increment `pending_clock_reads` when that same
call returns `Pending`; retain the existing unavailable counter for unstable
concurrent reads. Provider-owned counters separately include stage-origin bind
queries. History-lost and discontinuous results remain immediate, fully logged
fatal records rather than recoverable counters.

- [ ] **Step 6: Build the complete route**

```powershell
& 'H:\gc\temp\build-asio-audio-backend.ps1' `
    -Preset msvc32-debug -Target iDmacDrv32

& 'H:\gc\temp\build-asio-audio-backend.ps1' `
    -Preset msvc32-debug -Target ConfigGUI
```

Expected proof: both builds exit 0; the DLL accepts both configured exact
domains and the GUI presents the same rule.

- [ ] **Step 7: Record and commit Task 6**

```powershell
git add -- `
  src/Config/config.cpp `
  src/Patches/AbsoluteJudgement/AbsoluteJudgementPatch.cpp `
  src/Patches/AbsoluteJudgement/AbsoluteJudgementRuntime.h `
  src/Patches/AbsoluteJudgement/AbsoluteJudgementRuntime.cpp `
  src/Patches/AbsoluteJudgement/JudgementScheduler.cpp `
  src/Patches/AbsoluteJudgement/AbsoluteJudgementDiagnostics.h `
  src/Patches/AbsoluteJudgement/AbsoluteJudgementDiagnostics.cpp `
  tools/ConfigGUI/Main.cpp `
  docs/superpowers/plans/2026-08-22-asio-absolute-time-judgement.md
git commit -m "Enable ASIO absolute-time judgement route"
```

---

### Task 7: Perform source review, Release build, ABI/import inspection, and deployment

**Files:**

- Modify outside repo: `H:\gc\temp\build-asio-audio-backend.ps1` only if the
  current environment path or target support needs correction
- Modify outside repo: `H:\gc\temp\inspect-asio-audio-backend-abi.ps1`
- Create outside repo: `H:\gc\temp\deploy-asio-absolute-judgement.ps1`
- Record: `docs/superpowers/plans/2026-08-22-asio-absolute-time-judgement.md`

- [ ] **Step 1: Review the complete source diff inline**

Run:

```powershell
$repo = 'H:\gc\artifacts\GCLoader\.worktrees\asio-audio-backend'
$implementationBaseline = '<hash recorded before Task 1>'
git -C $repo status --short
git -C $repo diff --stat $implementationBaseline -- src
git -C $repo diff $implementationBaseline -- src
```

Review every changed source line directly; do not use agents. Confirm:

- the WASAPI resolver math was not rewritten;
- no provider emits `OutsidePlayback` or deletes pre-origin events;
- the ASIO provider uses driver system time, never callback-entry QPC;
- QPC and multimedia ticks are captured at input observation and stage entry;
- sequence, held masks, held ages, event/heartbeat cadence, native calls, and
  accepted loss policies are unchanged;
- provider absence/domain mismatch is fatal before any native judgement scope;
- no stage or judgement timeout/fallback exists;
- no callback allocation, lock, log, or format exists;
- every newly touched failure is directly observed and clearly logged; and
- `src/Patches/Framerate/**` has no diff; and
- ConfigGUI's visible rule and shared save validation agree exactly.

Fix any issue inline, rebuild Debug, and amend the owning task with a new focused
commit. Do not proceed on a known static issue.

- [ ] **Step 2: Extend the persisted ABI inspection script**

Keep the existing x86/PE32/subsystem, 15 exports, hook symbols, and x86 return
cleanup checks. Add a required `-BaselineCommit` parameter and make the script
fail if the added source diff contains:

- a new C++ `try` or `catch` line;
- a new `ostringstream`/`stringstream` formatter;
- any changed file under `src/Patches/Framerate`; or
- any change mentioning the independent `0x664DB2` patch site.

Add `dumpbin /imports` assertions for `WINMM.dll` and the imported symbols
`timeGetTime`, `timeBeginPeriod`, and `timeEndPeriod`. These checks prove the
compiled platform route, not runtime correctness.

- [ ] **Step 3: Run a fresh x86 Release build and static inspection**

```powershell
& 'H:\gc\temp\build-asio-audio-backend.ps1' `
    -Preset msvc32-release -Target iDmacDrv32 -Fresh

& 'H:\gc\temp\build-asio-audio-backend.ps1' `
    -Preset msvc32-release -Target ConfigGUI

& 'H:\gc\temp\inspect-asio-audio-backend-abi.ps1' `
    -BaselineCommit $implementationBaseline
```

Expected proof: all commands exit 0; the DLL candidate is PE32/x86, preserves
all 15 exports and hook calling conventions, imports the three WinMM functions,
the matching ConfigGUI executable is freshly built, and the scoped source-diff
constraints pass. Do not run CTest.

- [ ] **Step 4: Persist a guarded PowerShell 7 deployment script**

Create `H:\gc\temp\deploy-asio-absolute-judgement.ps1` that:

1. resolves both candidates
   `build-msvc32-release\dist\iDmacDrv32.dll` and
   `build-msvc32-release\dist\ConfigGUI.exe`, plus runtime destinations
   `H:\gc\iDmacDrv32.dll` and `H:\gc\ConfigGUI.exe`, to explicit absolute
   paths;
2. refuses to continue while `game471` or `ConfigGUI` is running; it does not
   terminate either process;
3. creates
   `H:\gc\deploy-backups\asio-absolute-judgement-<timestamp>`;
4. copies the currently deployed DLL and GUI there as
   `iDmacDrv32.pre-asio.dll` and `ConfigGUI.pre-asio.exe`;
5. copies both verified candidates over their runtime destinations with
   `Copy-Item -Force`;
6. computes candidate/deployed SHA-256 pairs for both artifacts and throws if
   either pair differs; and
7. prints both candidate hashes, both deployed hashes, and the backup path.

The script must use no recursive delete, wildcard target, unresolved
environment-variable destination, or nested shell.

- [ ] **Step 5: Deploy the verified DLL**

The user has already authorized deployment after implementation:

```powershell
& 'H:\gc\temp\deploy-asio-absolute-judgement.ps1'
```

Expected proof: script exits 0, both candidate/deployed SHA-256 pairs match,
and timestamped rollback copies of the DLL and GUI exist. Do not edit
`H:\gc\config.toml` automatically.

- [ ] **Step 6: Record static completion without claiming runtime acceptance**

Record the task commit hashes, both Release/deployed hash pairs, and backup path
in the Execution Record. Commit only this plan update if it changed:

```powershell
git add -- docs/superpowers/plans/2026-08-22-asio-absolute-time-judgement.md
git commit -m "Record ASIO absolute judgement deployment"
```

State explicitly that runtime acceptance remains pending the user's game run.

---

## User-run runtime acceptance after deployment

This section is a handoff checklist, not an automated test.

- [ ] Select `audio_backend = 'asio'`, keep
  `enable_absolute_time_judgement = true`, `input_poll_hz = 1000`, target FPS
  `240`, XONAR buffer `192`, output channels `0/1`, and
  `JudgTimeOffset = -18` unchanged.
- [ ] Run a normal full song. Exercise another song/card session if convenient;
  arbitrary stage transitions and Test Mode exit must remain valid.
- [ ] Supply the new `H:\gc\loader-log.txt`.
- [ ] Confirm the log reports configured backend `asio`, active exact provider
  `asio_multimedia_ms`, one-millisecond timestamp quantum, 192-frame period,
  reported 384-frame latency on the current setup, and no WASAPI fallback.
- [ ] Confirm semantic stage opens/activations/ends pair correctly; no provider,
  domain, history, continuity, arithmetic, input-transport, native-call, or
  final-accounting fatal occurs.
- [ ] Require zero stage-entry handoff, overload, and cleanup drops for the
  comparison run. If any accepted loss counter is nonzero, repeat the run rather
  than treating it as ordinary timing data.
- [ ] Inspect ASIO callback/mixer/exact-provider summaries for sane cumulative
  counts without per-note/callback log spam.
- [ ] Reconstruct raw timing errors by subtracting the unchanged `-18 ms`
  configured offset and compare the robust median/MAD with the established
  WASAPI raw median region of approximately `+13..+17 ms`.

Interpret only the comparable full-song data:

- a substantial stable movement toward zero supports an audio-path component;
- a similar raw median argues against WASAPI buffering as the main source;
- the accepted one-millisecond ASIO timestamp quantum cannot explain a stable
  13-to-17-millisecond bias; and
- no static/build result is grounds to claim runtime acceptance before this run.

## Execution Record

Update this table during inline execution; do not record imagined results.

| Task | Status | Commit | Evidence / remaining acceptance |
|---|---|---|---|
| Plan baseline | Complete | `1ed5ee2` | Isolated worktree; only the user-owned August 20 plan was dirty |
| 1. Neutral provider/WASAPI preservation | Complete | `8a7a2b5` | Debug x86 `iDmacDrv32` build exited 0; `ResolveQpc` body unchanged |
| 2. Dual-domain capture | Complete | `ff83f6a` | Debug x86 `iDmacDrv32` build exited 0; QPC remains ordering/loss authority |
| 3. Generic resolver/scheduler | Complete | `0079b5b` | Debug x86 `iDmacDrv32` build exited 0 after one stale reset-call correction; judgement formula diff unchanged |
| 4. Exact ASIO history | Complete | Pending commit | Unwired provider built in Debug x86; 60-second preallocated ring and rational modular resolver reviewed inline |
| 5. ASIO lifecycle wiring | Pending | — | Debug x86 build pending |
| 6. Runtime/GUI config, route, diagnostics | Pending | — | Debug x86 DLL and ConfigGUI builds pending |
| 7. Release/static/deploy | Pending | — | DLL/ConfigGUI Release hashes and backups pending |
| Runtime acceptance | Pending user run | — | 240-FPS ASIO log and offset comparison pending |

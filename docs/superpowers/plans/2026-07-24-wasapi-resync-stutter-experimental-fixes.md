# WASAPI Resync Stutter Experimental Fixes Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Stop harmless periodic BGM watchdog re-anchors from becoming hard WASAPI seeks and remove synchronous endpoint-clock access from the gameplay thread.

**Architecture:** Publish the already-committed WASAPI backend state into framerate initialization, install a silent seek-policy hook only for that backend, and skip the original seek block only when drift remains inside `SkipMargin`. Move presentation-clock ownership entirely to the audio thread by publishing a sequence-protected frame/QPC/submitted-tail snapshot that `CurrentOutputFrame` projects with QPC and lock-free atomics.

**Tech Stack:** C++23, Win32 x86, SafetyHook, MinHook, DirectSound, WASAPI `IAudioClock`, miniaudio, CMake/Ninja, MSVC x86.

## Global Constraints

- The approved design is
  `docs/superpowers/specs/2026-07-23-wasapi-resync-stutter-experimental-fixes-design.md`.
- Work and commits belong in `H:\gc\artifacts\GCLoader`; `H:\gc` is the
  runtime/deployment tree.
- The current root worktree contains investigation-only audio diagnostic
  edits. They are owned by this experiment and must be removed, not preserved
  or committed.
- Do not add a test source, test target, fixture, synthetic seek suite, or
  debug-only test case. Mechanically adapt existing tests only where they
  encode the deliberately removed synchronous-clock or renamed-hook contract.
- Do not change `experimental.wasapi_exclusive_buffer_ms`,
  `GameTimeOffset`, `SkipMargin`, judgement timing, endpoint negotiation,
  sample-rate conversion, or `SkipInterval` wall-time scaling.
- Do not add a new config key. The policy is active only when the existing
  WASAPI `DirectSoundCreate8` detour actually committed.
- Passthrough DirectSound must not install the policy hook.
- An out-of-margin seek must still execute the original game setter.
- An in-margin arrival at RVA `0x002401C4` must jump to the original epilogue
  at RVA `0x002401D4`.
- The exact supported instruction window is:
  `8B 55 F8 52 E8 33 02 FD FF 8B C8 E8 2C 12 FD FF 5E 8B E5 5D C3`.
- The endpoint/render thread remains the only `IAudioClock` caller.
- `CurrentOutputFrame` must contain no endpoint dereference, project mutex,
  COM/driver call, allocation, logging, or failure publication.
- Remove temporary diagnostics without adding replacement counters or log
  fields.
- Use the normal `msvc32-release` preset inside
  `C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat`.
- Runtime acceptance is one focused 240 FPS WASAPI run of at least 90 seconds;
  the existing capture is the baseline and is not repeated unless its
  environment changes.

---

## File and responsibility map

| File | Responsibility in this change |
|---|---|
| `src/Audio/Wasapi/WasapiAudioPatch.h/.cpp` | Expose whether the DirectSound detour actually committed. |
| `src/Loader/DllMain.cpp` | Pass committed backend state into framerate initialization. |
| `src/Patches/Framerate/FrameratePatchPlan.h/.cpp` | Select backend-aware hook sets and guard the complete seek/epilogue byte window. |
| `src/Patches/Framerate/FrameratePatchTransaction.h` | Increase fixed byte-pattern capacity from 16 to 32 bytes. |
| `src/Patches/Framerate/FrameratePatch.h/.cpp` | Install the selected hook plan and implement the silent interval-only bypass. |
| `src/Audio/Mixer/AudioCursorTimeline.h/.cpp` | Add coherent endpoint-clock publication and bounded monotonic projection beside the existing cursor timeline primitives. |
| `src/Audio/Wasapi/ExclusiveAudioEngine.h/.cpp` | Publish clock snapshots after successful submissions and make `CurrentOutputFrame` snapshot-only. |
| `src/Audio/CMakeLists.txt`, `src/Audio/DirectSound/DirectSoundFacade.h/.cpp` | Remove uncommitted investigation-only diagnostic plumbing. |
| `tests/Patches/Framerate/FrameratePatchPlanTests.cpp` | Mechanically update the existing renamed hook and exact byte expectation. |
| `tests/Patches/Framerate/FramerateRuntimeTests.cpp` | Mechanically update the public initialization signature check. |
| `tests/Audio/ExclusiveAudioEngineTests.cpp` | Remove old synchronous-clock assumptions and assert against already-published submitted spans. |
| `.planning/debug/high-fps-timing-domains/evidence/E-041-wasapi-resync-stutter-experiment.md` | Record build/deployment identity and the eventual operator verdict without overclaiming micro-freeze causality. |

### Task 1: Remove investigation diagnostics and expose committed WASAPI state

**Files:**

- Modify: `src/Audio/CMakeLists.txt`
- Modify: `src/Audio/DirectSound/DirectSoundFacade.h`
- Modify: `src/Audio/DirectSound/DirectSoundFacade.cpp`
- Modify: `src/Audio/Wasapi/ExclusiveAudioEngine.h`
- Modify: `src/Audio/Wasapi/ExclusiveAudioEngine.cpp`
- Modify: `src/Audio/Wasapi/WasapiAudioPatch.h`
- Modify: `src/Audio/Wasapi/WasapiAudioPatch.cpp`
- Delete: `src/Audio/Wasapi/AudioDiagnostics.h`
- Delete: `src/Audio/Wasapi/AudioDiagnostics.cpp`

**Interfaces:**

- Consumes: Existing one-shot `DirectSoundCreate8` MinHook transaction and
  `g_committed_target`.
- Produces:
  `[[nodiscard]] bool gc::audio::IsWasapiAudioHookCommitted() noexcept`.

- [ ] **Step 1: Confirm the dirty source is exactly the known diagnostic set**

Run:

```powershell
git status --short
git diff -- src/Audio/CMakeLists.txt src/Audio/DirectSound/DirectSoundFacade.cpp src/Audio/DirectSound/DirectSoundFacade.h src/Audio/Wasapi/ExclusiveAudioEngine.cpp src/Audio/Wasapi/ExclusiveAudioEngine.h src/Audio/Wasapi/WasapiAudioPatch.cpp
```

Expected: the six tracked files contain only timing instrumentation described
in E-040, and `AudioDiagnostics.cpp/.h` are the only untracked audio sources.
Stop if any unrelated source edit appears.

- [ ] **Step 2: Remove the uncommitted detailed diagnostics**

Delete `AudioDiagnostics.cpp/.h` and remove their CMake/include references.
Restore `IAudioEngineServices` to this tail:

```cpp
virtual std::uint32_t endpoint_buffer_frames() const noexcept = 0;
virtual std::uint32_t output_sample_rate() const noexcept = 0;
virtual void CountPendingCursorQuery() noexcept = 0;
virtual void CountUnmappedCursorFailure() noexcept = 0;
```

Restore secondary-buffer lock/unlock to direct snapshot calls:

```cpp
const auto result = snapshot_->Lock(
    offset,
    byte_count,
    flags,
    &regions);
```

```cpp
return snapshot_->Unlock(
    first,
    first_bytes,
    second,
    second_bytes);
```

Restore the render loop to direct endpoint/mixer operations:

```cpp
EndpointClockPosition clock{};
if (FAILED(endpoint_->ReadClock(&clock, &failure))) {
    static_cast<void>(endpoint_->TrySubmitSilence());
    RecordRuntimeFailure(failure);
    break;
}
```

```cpp
auto rendered = mixer_->Render(
    float_mix_,
    MixerRenderTimeline{
        decision.block_begin,
        decision.discontinuity_frames,
    });
```

```cpp
if (FAILED(endpoint_->SubmitPcm16(pcm16_mix_, &failure))) {
    RecordRuntimeFailure(failure);
    break;
}
```

Remove `AudioPerformanceCountersSnapshot`, `performance_diagnostics_`,
`RecordBufferLockDiagnostic`, `RecordBufferUnlockDiagnostic`, and every
`counters.performance.*` formatter field. `counters_text` must again end with:

```cpp
<< " active_voices=" << counters.mixer.active_voices
<< " maximum_simultaneous_voices="
<< counters.mixer.maximum_simultaneous_voices;
```

- [ ] **Step 3: Expose the actual committed backend state**

Add to `WasapiAudioPatch.h`:

```cpp
[[nodiscard]] bool IsWasapiAudioHookCommitted() noexcept;
```

Add beside the public install/init functions in `WasapiAudioPatch.cpp`:

```cpp
bool IsWasapiAudioHookCommitted() noexcept {
    return g_committed_target != nullptr;
}
```

Do not derive this value from config. `g_committed_target` remains null on
disabled and failed/rolled-back installation paths and is assigned only after
`MH_ApplyQueued` succeeds.

- [ ] **Step 4: Prove all temporary diagnostic code is gone**

Run:

```powershell
rg -n 'AudioDiagnostics|AudioDiagnosticClock|AudioDiagnosticElapsedMicroseconds|performance_diagnostics_|RecordBufferLockDiagnostic|RecordBufferUnlockDiagnostic|counters\.performance' src/Audio
```

Expected: no matches.

`HookAudioResyncDiagnostic` is intentionally still present until Task 2.

- [ ] **Step 5: Reconfigure and build the production DLL**

Run:

```powershell
& $env:ComSpec /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --preset msvc32-release && cmake --build --preset msvc32-release --target iDmacDrv32'
```

Expected: configure and `iDmacDrv32` build exit zero.

- [ ] **Step 6: Commit the backend handoff**

Run:

```powershell
git diff --check
git add -- src/Audio/CMakeLists.txt src/Audio/DirectSound/DirectSoundFacade.h src/Audio/DirectSound/DirectSoundFacade.cpp src/Audio/Wasapi/ExclusiveAudioEngine.h src/Audio/Wasapi/ExclusiveAudioEngine.cpp src/Audio/Wasapi/WasapiAudioPatch.h src/Audio/Wasapi/WasapiAudioPatch.cpp
git commit -m "refactor: publish committed WASAPI backend state"
```

Expected: the commit contains the public state query; the temporary uncommitted
diagnostic edits have disappeared rather than becoming part of the commit.

### Task 2: Replace the resync diagnostic with a WASAPI-only seek policy

**Files:**

- Modify: `src/Loader/DllMain.cpp`
- Modify: `src/Patches/Framerate/FrameratePatch.h`
- Modify: `src/Patches/Framerate/FrameratePatch.cpp`
- Modify: `src/Patches/Framerate/FrameratePatchPlan.h`
- Modify: `src/Patches/Framerate/FrameratePatchPlan.cpp`
- Modify: `src/Patches/Framerate/FrameratePatchTransaction.h`
- Modify: `tests/Patches/Framerate/FrameratePatchPlanTests.cpp`
- Modify: `tests/Patches/Framerate/FramerateRuntimeTests.cpp`

**Interfaces:**

- Consumes:
  `gc::audio::IsWasapiAudioHookCommitted() noexcept`.
- Produces:
  `bool gc::framerate::FrameratePatchInit(bool wasapi_audio_committed)`;
  `FramerateHookPlan BuildFramerateHookPlan(bool transformed_timing,
  bool wasapi_audio_committed) noexcept`;
  `FramerateHookId::AudioResyncPolicy`.

- [ ] **Step 1: Expand the fixed preflight pattern capacity**

In `FrameratePatchTransaction.h`, make the only capacity change:

```cpp
inline constexpr std::size_t kMaximumPatternBytes = 32;
```

Do not change transaction allocation, preflight order, rollback, or maximum
hook/write counts.

- [ ] **Step 2: Add a backend-aware fixed-capacity hook plan**

In `FrameratePatchPlan.h`, rename the enum member:

```cpp
AudioResyncPolicy,
```

Add:

```cpp
struct FramerateHookPlan {
    std::array<FramerateHookContract, kMaximumFramerateHooks> contracts{};
    std::size_t count{};

    [[nodiscard]] std::span<const FramerateHookContract>
    view() const noexcept {
        return {contracts.data(), count};
    }
};

[[nodiscard]] FramerateHookPlan BuildFramerateHookPlan(
    bool transformed_timing,
    bool wasapi_audio_committed) noexcept;
```

Retain `FramerateHookContracts(bool)` as the static complete-contract inventory
used by existing tests and runtime-binding audits.

In `FrameratePatchPlan.cpp`, implement:

```cpp
FramerateHookPlan BuildFramerateHookPlan(
    bool transformed_timing,
    bool wasapi_audio_committed) noexcept {
    FramerateHookPlan plan{};
    for (const auto& contract : kHookContracts) {
        const bool selected =
            contract.id == FramerateHookId::OuterFrame ||
            (contract.id == FramerateHookId::AudioResyncPolicy
                 ? wasapi_audio_committed
                 : transformed_timing);
        if (selected) {
            plan.contracts[plan.count++] = contract;
        }
    }
    return plan;
}
```

This produces exactly:

- native DirectSound: outer-frame hook only;
- native WASAPI: resync policy plus outer-frame hook;
- transformed DirectSound: every transformed hook except resync policy;
- transformed WASAPI: all 42 hooks.

- [ ] **Step 3: Guard the exact seek block and epilogue**

Replace the old three-byte diagnostic contract with the daemon-confirmed
21-byte window:

```cpp
{FramerateHookId::AudioResyncPolicy, 0x002401C4,
    Pattern(
        0x8B, 0x55, 0xF8,
        0x52,
        0xE8, 0x33, 0x02, 0xFD, 0xFF,
        0x8B, 0xC8,
        0xE8, 0x2C, 0x12, 0xFD, 0xFF,
        0x5E, 0x8B, 0xE5, 0x5D, 0xC3),
    "WASAPI interval-only audio resync policy"},
```

This one existing hook preflight now verifies:

- the hook instruction at RVA `0x002401C4`;
- both original calls in the seek block; and
- the exact `pop esi; mov esp, ebp; pop ebp; ret` destination at
  RVA `0x002401D4`.

No transaction API or no-op executable write is added.

- [ ] **Step 4: Implement the silent interval-only bypass**

In `FrameratePatch.cpp`:

1. Rename storage `audio_resync_diagnostic` to `audio_resync_policy`.
2. Rename the callback and switch case to `HookAudioResyncPolicy` and
   `AudioResyncPolicy`.
3. Remove `audio_resync_seeks`, `audio_resync_margin_seeks`, and
   `audio_resync_interval_seeks` from `FramerateRuntimeCounters`.
4. Remove the `audio_resync=.../margin=.../interval=...` fields from periodic
   framerate status output.
5. Replace the diagnostic callback with:

```cpp
constexpr std::uintptr_t kAudioResyncEpilogueRva = 0x002401D4;

void HookAudioResyncPolicy(safetyhook::Context& context) {
    std::int32_t drift_ms{};
    std::int32_t margin_ms{};
    if (!ReadI32StackSafe(context, -0x0C, drift_ms) ||
        !ReadI32StackSafe(context, -0x24, margin_ms) ||
        margin_ms < 0) {
        return;
    }

    const auto abs_drift_ms = drift_ms < 0
        ? -static_cast<std::int64_t>(drift_ms)
        : static_cast<std::int64_t>(drift_ms);
    if (abs_drift_ms <= static_cast<std::int64_t>(margin_ms)) {
        context.eip = static_cast<std::uint32_t>(
            ExecutableBase() + kAudioResyncEpilogueRva);
    }
}
```

Returning without changing `eip` executes the original seek block. This is the
required fail-open behavior for unreadable stack state, invalid margin, and
real out-of-margin drift. The widened negation safely handles `INT32_MIN`.

The hook body must contain no PLOG call, stream, allocation, counter, endpoint
query, file I/O, or backend query.

- [ ] **Step 5: Wire committed backend state into hook selection**

Change `FrameratePatch.h` and its definition to:

```cpp
[[nodiscard]] bool FrameratePatchInit(bool wasapi_audio_committed);
```

Inside `FrameratePatchInit`, replace the raw contract selection with:

```cpp
const auto hook_plan = BuildFramerateHookPlan(
    !g_runtime->profile.native_timing(),
    wasapi_audio_committed);
const auto hook_operations = BuildHookOperations(
    hook_plan.view(), *g_runtime);
```

In `DllMain.cpp`, call:

```cpp
if (!gc::framerate::FrameratePatchInit(
        gc::audio::IsWasapiAudioHookCommitted())) {
    PLOG_ERROR
        << "FrameratePatch: fail-closed DLL attach";
    return FALSE;
}
```

Do not make `gc_runtime_patches` link against `gc_audio`; the loader is the
one-way handoff boundary.

- [ ] **Step 6: Mechanically update existing contract tests**

Do not add a new test or case.

In `FrameratePatchPlanTests.cpp`:

- rename `AudioResyncDiagnostic` to `AudioResyncPolicy`;
- replace its expected three-byte pattern with the exact 21-byte pattern from
  Step 3.

In `FramerateRuntimeTests.cpp`, change only the signature check:

```cpp
static_assert(std::same_as<
    decltype(gc::framerate::FrameratePatchInit(false)), bool>);
```

- [ ] **Step 7: Build the affected existing targets**

Run:

```powershell
& $env:ComSpec /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build --preset msvc32-release --target FrameratePatchPlanTests FrameratePatchTransactionTests FramerateRuntimeTests iDmacDrv32'
```

Expected: all four targets build successfully.

- [ ] **Step 8: Perform the static policy audit**

Run:

```powershell
rg -n 'AudioResyncDiagnostic|HookAudioResyncDiagnostic|audio_resync_(seeks|margin_seeks|interval_seeks)|audio_resync=|resync_seek=' src tests
rg -n 'AudioResyncPolicy|002401C4|002401D4|IsWasapiAudioHookCommitted|FrameratePatchInit\(' src tests
git diff --check
```

Expected:

- first command: no matches;
- second command: only the new policy, exact RVAs, backend handoff, public
  initialization, and mechanically updated existing tests;
- diff check: exit zero.

- [ ] **Step 9: Commit the seek policy**

Run:

```powershell
git add -- src/Loader/DllMain.cpp src/Patches/Framerate/FrameratePatch.h src/Patches/Framerate/FrameratePatch.cpp src/Patches/Framerate/FrameratePatchPlan.h src/Patches/Framerate/FrameratePatchPlan.cpp src/Patches/Framerate/FrameratePatchTransaction.h tests/Patches/Framerate/FrameratePatchPlanTests.cpp tests/Patches/Framerate/FramerateRuntimeTests.cpp
git commit -m "fix: suppress harmless WASAPI interval seeks"
```

### Task 3: Publish endpoint presentation time for lock-free cursor reads

**Files:**

- Modify: `src/Audio/Mixer/AudioCursorTimeline.h`
- Modify: `src/Audio/Mixer/AudioCursorTimeline.cpp`
- Modify: `src/Audio/Wasapi/ExclusiveAudioEngine.h`
- Modify: `src/Audio/Wasapi/ExclusiveAudioEngine.cpp`
- Modify: `tests/Audio/ExclusiveAudioEngineTests.cpp`

**Interfaces:**

- Produces:
  `PresentedClockPublication::Publish`,
  `PresentedClockPublication::Invalidate`, and
  `PresentedClockPublication::Project`.
- Consumes: mapped presentation frame, endpoint-correlated QPC in 100 ns
  units, committed submitted tail, process QPC ticks/frequency, and selected
  output sample rate.

- [ ] **Step 1: Declare the sequence-protected clock publication**

Add to `AudioCursorTimeline.h` after `EndpointClockMapper`:

```cpp
struct PresentedClockSnapshot {
    std::uint64_t presented_output_frame{};
    std::uint64_t sample_qpc_100ns{};
    std::uint64_t submitted_output_frame_end{};
};

class PresentedClockPublication {
public:
    void Publish(
        std::uint64_t presented_output_frame,
        std::uint64_t sample_qpc_100ns,
        std::uint64_t submitted_output_frame_end) noexcept;
    void Invalidate() noexcept;

    std::optional<std::uint64_t> Project(
        std::uint64_t now_qpc_ticks,
        std::uint64_t qpc_frequency,
        std::uint32_t output_sample_rate) noexcept;

private:
    [[nodiscard]] std::optional<PresentedClockSnapshot>
        ReadStable() const noexcept;
    [[nodiscard]] std::optional<std::uint64_t>
        LastReturned() const noexcept;
    std::uint64_t RememberMonotonic(std::uint64_t frame) noexcept;

    std::atomic_uint64_t sequence_{};
    std::atomic_bool valid_{};
    std::atomic_uint64_t presented_output_frame_{};
    std::atomic_uint64_t sample_qpc_100ns_{};
    std::atomic_uint64_t submitted_output_frame_end_{};
    std::uint64_t writer_generation_{};
    std::atomic_uint64_t last_returned_{};
    std::atomic_bool has_last_returned_{};
};
```

Keep the class beside `EndpointClockMapper` because both express the endpoint
clock in the mixer output-frame domain. Do not create another thread, timer,
queue, or allocation.

- [ ] **Step 2: Implement coherent publication and bounded projection**

In `AudioCursorTimeline.cpp`, reuse the existing internal `ScaleFloor`,
`CheckedAdd`, and `detail::kRenderSpanAtomicOrder`. Add:

```cpp
void PresentedClockPublication::Publish(
    std::uint64_t presented_output_frame,
    std::uint64_t sample_qpc_100ns,
    std::uint64_t submitted_output_frame_end) noexcept {
    if (submitted_output_frame_end == 0 ||
        presented_output_frame >= submitted_output_frame_end) {
        Invalidate();
        return;
    }

    const auto generation = writer_generation_++;
    const auto writing = generation * 2 + 1;
    sequence_.store(writing, detail::kRenderSpanAtomicOrder);
    presented_output_frame_.store(
        presented_output_frame, detail::kRenderSpanAtomicOrder);
    sample_qpc_100ns_.store(
        sample_qpc_100ns, detail::kRenderSpanAtomicOrder);
    submitted_output_frame_end_.store(
        submitted_output_frame_end, detail::kRenderSpanAtomicOrder);
    valid_.store(true, detail::kRenderSpanAtomicOrder);
    sequence_.store(writing + 1, detail::kRenderSpanAtomicOrder);
}

void PresentedClockPublication::Invalidate() noexcept {
    const auto generation = writer_generation_++;
    const auto writing = generation * 2 + 1;
    sequence_.store(writing, detail::kRenderSpanAtomicOrder);
    valid_.store(false, detail::kRenderSpanAtomicOrder);
    sequence_.store(writing + 1, detail::kRenderSpanAtomicOrder);
}

std::optional<PresentedClockSnapshot>
PresentedClockPublication::ReadStable() const noexcept {
    for (int attempt = 0; attempt < 3; ++attempt) {
        const auto before =
            sequence_.load(detail::kRenderSpanAtomicOrder);
        if ((before & 1U) != 0) {
            continue;
        }

        const auto is_valid =
            valid_.load(detail::kRenderSpanAtomicOrder);
        const PresentedClockSnapshot snapshot{
            presented_output_frame_.load(
                detail::kRenderSpanAtomicOrder),
            sample_qpc_100ns_.load(
                detail::kRenderSpanAtomicOrder),
            submitted_output_frame_end_.load(
                detail::kRenderSpanAtomicOrder),
        };
        const auto after =
            sequence_.load(detail::kRenderSpanAtomicOrder);
        if (before == after && (after & 1U) == 0) {
            return is_valid
                ? std::optional<PresentedClockSnapshot>{snapshot}
                : std::nullopt;
        }
    }
    return std::nullopt;
}

std::optional<std::uint64_t>
PresentedClockPublication::LastReturned() const noexcept {
    if (!has_last_returned_.load(std::memory_order_acquire)) {
        return std::nullopt;
    }
    return last_returned_.load(std::memory_order_acquire);
}

std::uint64_t PresentedClockPublication::RememberMonotonic(
    std::uint64_t frame) noexcept {
    auto observed = last_returned_.load(std::memory_order_acquire);
    while (observed < frame &&
           !last_returned_.compare_exchange_weak(
               observed,
               frame,
               std::memory_order_acq_rel,
               std::memory_order_acquire)) {
    }
    has_last_returned_.store(true, std::memory_order_release);
    return std::max(observed, frame);
}

std::optional<std::uint64_t>
PresentedClockPublication::Project(
    std::uint64_t now_qpc_ticks,
    std::uint64_t qpc_frequency,
    std::uint32_t output_sample_rate) noexcept {
    constexpr std::uint64_t kReferenceTimePerSecond = 10'000'000;
    const auto snapshot = ReadStable();
    if (!snapshot.has_value() || qpc_frequency == 0 ||
        output_sample_rate == 0) {
        return LastReturned();
    }

    const auto now_qpc_100ns = ScaleFloor(
        now_qpc_ticks,
        kReferenceTimePerSecond,
        qpc_frequency);
    if (!now_qpc_100ns.has_value() ||
        *now_qpc_100ns < snapshot->sample_qpc_100ns) {
        return LastReturned();
    }

    const auto elapsed_frames = ScaleFloor(
        *now_qpc_100ns - snapshot->sample_qpc_100ns,
        output_sample_rate,
        kReferenceTimePerSecond);
    std::uint64_t projected{};
    if (!elapsed_frames.has_value() ||
        !CheckedAdd(
            snapshot->presented_output_frame,
            *elapsed_frames,
            &projected) ||
        snapshot->submitted_output_frame_end == 0) {
        return LastReturned();
    }

    const auto bounded = std::min(
        projected,
        snapshot->submitted_output_frame_end - 1);
    return RememberMonotonic(bounded);
}
```

Retain the existing compile-time lock-free assertions for `atomic<uint64_t>`,
`atomic_ref<uint64_t>`, and `atomic_ref<bool>`. Add the direct field assertion:

```cpp
static_assert(std::atomic<bool>::is_always_lock_free);
```

- [ ] **Step 3: Make the engine own QPC frequency and publication**

In `ExclusiveAudioEngine.h`:

- remove `<mutex>`;
- remove `endpoint_service_mutex_`;
- remove `submitted_frames_`;
- add:

```cpp
PresentedClockPublication presented_clock_;
std::uint64_t qpc_frequency_{};
```

Change the constructor body in `ExclusiveAudioEngine.cpp`:

```cpp
ExclusiveAudioEngine::ExclusiveAudioEngine(
    std::unique_ptr<IWasapiApi> api,
    std::shared_ptr<IAudioEngineObserver> observer,
    REFERENCE_TIME configured_duration,
    std::shared_ptr<const ma_allocation_callbacks> mixer_allocations,
    DWORD summary_interval_ms) noexcept
    : pending_api_(std::move(api)),
      configured_duration_(configured_duration),
      observer_(std::move(observer)),
      mixer_allocations_(std::move(mixer_allocations)),
      summary_interval_ms_(summary_interval_ms) {
    LARGE_INTEGER frequency{};
    if (QueryPerformanceFrequency(&frequency) &&
        frequency.QuadPart > 0) {
        qpc_frequency_ =
            static_cast<std::uint64_t>(frequency.QuadPart);
    }
}
```

QPC unavailability is a cursor fallback condition, not a new startup-fatal
path.

- [ ] **Step 4: Publish only after successful endpoint submission**

Remove both `submitted_frames_.store(...)` calls.

After `pacing_tracker_->Commit(decision)` succeeds, publish the same validated
clock sample with the newly committed tail:

```cpp
const auto submitted_tail = pacing_tracker_->submitted_tail();
presented_clock_.Publish(
    *presented,
    clock.qpc_100ns,
    submitted_tail);
RecordPacingDecision(decision);
render_callbacks_.fetch_add(1, std::memory_order_relaxed);
```

A failed render, submission, or pacing commit must not call `Publish`.

- [ ] **Step 5: Remove endpoint access from the gameplay-thread cursor path**

Replace `CurrentOutputFrame` with:

```cpp
std::optional<std::uint64_t>
ExclusiveAudioEngine::CurrentOutputFrame() noexcept {
    LARGE_INTEGER now{};
    std::uint64_t now_qpc_ticks{};
    std::uint64_t qpc_frequency{};
    if (qpc_frequency_ != 0 &&
        QueryPerformanceCounter(&now) &&
        now.QuadPart >= 0) {
        now_qpc_ticks =
            static_cast<std::uint64_t>(now.QuadPart);
        qpc_frequency = qpc_frequency_;
    }

    return presented_clock_.Project(
        now_qpc_ticks,
        qpc_frequency,
        output_sample_rate_.load(std::memory_order_acquire));
}
```

Replace endpoint cleanup with:

```cpp
void ExclusiveAudioEngine::CleanupEndpointOnAudioThread() noexcept {
    presented_clock_.Invalidate();
    if (endpoint_ == nullptr) {
        return;
    }
    static_cast<void>(endpoint_->ShutdownOnInitializingThread());
    endpoint_.reset();
}
```

The only remaining `ExclusiveAudioEngine.cpp` calls to
`endpoint_->ReadClock` must be:

1. the audio-thread initialization origin read; and
2. the audio-thread render-event read.

- [ ] **Step 6: Remove obsolete synchronous-clock assumptions from existing tests**

Do not create a publication test file or add a new test case.

In `ExclusiveAudioEngineTests.cpp`:

1. Delete `TestEndpointServiceGateProtectsCallerClockRead` and its call from
   `main`; that test exists solely to enforce the mutex/COM behavior this
   design removes.
2. In `TestVoiceClockSummaryAndRenderSafety`, remove each `PushClock` used only
   immediately before a direct `CurrentOutputFrame` call.
3. After the first render publication, assert the submitted-span bound:

```cpp
const auto first_rendered_frame = engine->CurrentOutputFrame();
failures += Expect(
    first_rendered_frame == 2 * kFrames - 1 &&
        source.timeline->ResolveSourceFrame(
            *first_rendered_frame,
            1,
            kFrames).kind == AudioCursorResolutionKind::Resolved &&
        source.timeline->ResolveSourceFrame(
            *first_rendered_frame,
            1,
            kFrames).source_frame == kFrames - 1,
    "published clock projects inside the first submitted source span");
```

4. After four submitted packets, use:

```cpp
const auto mapped = engine->CurrentOutputFrame();
failures += Expect(
    mapped.has_value() && *mapped == 5 * kFrames - 1,
    "published cursor stays inside the latest submitted 44.1 kHz span");
```

5. In `TestSelected48kEngineRate`, remove the direct-query `PushClock` and use:

```cpp
const auto mapped = engine->CurrentOutputFrame();
failures += Expect(
    mapped.has_value() && *mapped == 3 * kFrames - 1,
    "published cursor stays inside the latest submitted 48 kHz span");
```

6. Delete `RuntimeFailureCase::current_output_frame`, delete the final
   `GetClockPosition`/`E_FAIL` case whose last initializer is `true`, and
   remove the `if (test.current_output_frame)` branch from the case loop.
   Retain the render-thread `GetClockPosition` failure case for
   `AUDCLNT_E_DEVICE_INVALIDATED` and its exact fatal-stage checks.

These are adaptations of old integration assumptions, not new debug tests.

- [ ] **Step 7: Build the affected existing targets**

Run:

```powershell
& $env:ComSpec /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build --preset msvc32-release --target AudioCursorTimelineTests ExclusiveAudioEngineTests iDmacDrv32'
```

Expected: all three targets build successfully.

- [ ] **Step 8: Perform the static clock-ownership audit**

Run:

```powershell
rg -n 'endpoint_service_mutex_|submitted_frames_|performance_diagnostics_|AudioDiagnostics' src/Audio tests/Audio
rg -n 'ReadClock\(' src/Audio/Wasapi/ExclusiveAudioEngine.cpp
rg -n -A 24 'ExclusiveAudioEngine::CurrentOutputFrame\(\) noexcept' src/Audio/Wasapi/ExclusiveAudioEngine.cpp
git diff --check
```

Expected:

- first command: no matches;
- second command: exactly the audio-thread initialization and render-loop
  reads;
- third command: the complete `CurrentOutputFrame` body uses only QPC, output
  rate, and `presented_clock_`; it contains no endpoint dereference, clock
  read, mutex, logging, allocation, or failure publication;
- diff check: exit zero.

- [ ] **Step 9: Commit the published clock**

Run:

```powershell
git add -- src/Audio/Mixer/AudioCursorTimeline.h src/Audio/Mixer/AudioCursorTimeline.cpp src/Audio/Wasapi/ExclusiveAudioEngine.h src/Audio/Wasapi/ExclusiveAudioEngine.cpp tests/Audio/ExclusiveAudioEngineTests.cpp
git commit -m "fix: publish WASAPI presentation clock"
```

### Task 4: Verify, back up, and deploy the experimental production DLL

**Files:**

- Build artifact:
  `build-msvc32-release/dist/iDmacDrv32.dll`
- Runtime destination: `H:\gc\iDmacDrv32.dll`
- Rollback artifact:
  `H:\gc\deploy-backups\wasapi-resync-experiment-8ca8607\iDmacDrv32.pre-experiment.dll`

**Interfaces:**

- Consumes: committed Tasks 1-3.
- Produces: one hashed production DLL deployed with a persistent rollback copy.

- [ ] **Step 1: Run the one focused existing verification slice**

Run:

```powershell
& $env:ComSpec /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --preset msvc32-release && cmake --build --preset msvc32-release --target iDmacDrv32 FrameratePatchPlanTests FrameratePatchTransactionTests FramerateRuntimeTests AudioCursorTimelineTests ExclusiveAudioEngineTests && ctest --preset msvc32-release -R "^(FrameratePatchPlanTests|FrameratePatchTransactionTests|FramerateRuntimeTests|AudioCursorTimelineTests|ExclusiveAudioEngineTests)$"'
```

Expected: production DLL builds and the five already-existing focused tests
pass. Do not run or create a broader debug test phase.

- [ ] **Step 2: Verify the final source boundary**

Run:

```powershell
rg -n 'AudioDiagnostics|performance_diagnostics_|endpoint_service_mutex_|AudioResyncDiagnostic|HookAudioResyncDiagnostic|audio_resync_(seeks|margin_seeks|interval_seeks)|audio_resync=|resync_seek=' src tests
rg -n 'ReadClock\(' src/Audio/Wasapi/ExclusiveAudioEngine.cpp
git diff --name-status 8ca8607..HEAD
git diff --check
git status --short
```

Expected:

- removed diagnostics/old mutex/old hook: no matches;
- `ReadClock`: two audio-thread calls;
- committed diff since the approved design: this plan plus only the owned
  Tasks 1-3 source and mechanically adapted test files;
- diff check: exit zero;
- worktree: clean.

- [ ] **Step 3: Hash the candidate**

Run:

```powershell
$candidate = 'H:\gc\artifacts\GCLoader\build-msvc32-release\dist\iDmacDrv32.dll'
$candidateHash = Get-FileHash -Algorithm SHA256 -LiteralPath $candidate
$candidateHash | Format-List Path,Hash
```

Expected: one SHA256 for the release DLL. Preserve the printed hash for
E-041.

- [ ] **Step 4: Refuse deployment while the game is running**

Run:

```powershell
if (Get-Process -Name game471 -ErrorAction SilentlyContinue) {
    throw 'game471.exe is running; close it before DLL backup/deployment.'
}
```

Expected: no output. If it throws, stop and ask the user to close the game; do
not terminate the process automatically.

- [ ] **Step 5: Create the persistent rollback copy and deploy**

Run:

```powershell
$candidate = 'H:\gc\artifacts\GCLoader\build-msvc32-release\dist\iDmacDrv32.dll'
$runtime = 'H:\gc\iDmacDrv32.dll'
$backupDirectory = 'H:\gc\deploy-backups\wasapi-resync-experiment-8ca8607'
$rollback = Join-Path $backupDirectory 'iDmacDrv32.pre-experiment.dll'

if (Test-Path -LiteralPath $backupDirectory) {
    throw "Rollback directory already exists: $backupDirectory"
}

New-Item -ItemType Directory -Path $backupDirectory | Out-Null
Copy-Item -LiteralPath $runtime -Destination $rollback
Copy-Item -LiteralPath $candidate -Destination $runtime -Force

$candidateHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $candidate).Hash
$runtimeHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $runtime).Hash
if ($candidateHash -ne $runtimeHash) {
    throw 'Deployed DLL hash does not match the release candidate.'
}

[pscustomobject]@{
    CandidateHash = $candidateHash
    RuntimeHash = $runtimeHash
    Rollback = $rollback
} | Format-List
```

Expected: candidate and runtime hashes match, and the rollback path is printed.

The exact rollback command is:

```powershell
Copy-Item -LiteralPath 'H:\gc\deploy-backups\wasapi-resync-experiment-8ca8607\iDmacDrv32.pre-experiment.dll' -Destination 'H:\gc\iDmacDrv32.dll' -Force
```

### Task 5: Run the focused operator checkpoint and record the outcome

**Files:**

- Create:
  `.planning/debug/high-fps-timing-domains/evidence/E-041-wasapi-resync-stutter-experiment.md`
- Modify:
  `.planning/debug/high-fps-timing-domains/evidence/INDEX.md`
- Modify:
  `.planning/debug/high-fps-timing-domains/FINDINGS.md`

**Interfaces:**

- Consumes: deployed candidate hash, implementation commit IDs, and the
  operator's 240 FPS observation.
- Produces: an evidence-backed verdict that reports audio stutter and visual
  micro-freezes separately.

- [ ] **Step 1: Ask the operator to run the existing baseline scenario once**

Use the same endpoint, 10 ms buffer, config, song, and exact 240 FPS external
cap as E-040. Play for at least 90 seconds.

Ask for separate answers:

1. Did the approximately three-second repeating/skipping audio disappear?
2. Did BGM remain synchronized with the chart throughout the run?
3. Did the reported visual micro-freezes disappear, remain, or change?
4. Was there any endpoint startup/runtime failure or other audible regression?

Do not ask for a buffer matrix, 60 FPS matrix, DirectSound run, synthetic
underrun, or another diagnostic log.

- [ ] **Step 2: Stop at the runtime checkpoint**

After the game exits, verify the existing startup records:

```powershell
rg -n 'WASAPI audio config .*active_backend=wasapi_exclusive.*hook_installed=true|FrameratePatch: transaction committed|WASAPI audio startup .*actual_buffer_ms=10\.000' H:\gc\loader-log.txt
rg -n 'WasapiAudioPatch: fail-closed|FrameratePatch: fail-closed|WASAPI audio (startup|runtime) fatal' H:\gc\loader-log.txt
```

Expected: the first command finds the committed WASAPI backend, successful
framerate transaction, and 10 ms endpoint startup for this run; the second
command finds no failure. This is the supported-executable preflight evidence
and uses existing production startup reporting, not new telemetry.

Do not claim the clock path fixed micro-freezes before the user's answer.

If the audio stutter remains, restore the rollback DLL after `game471.exe` has
exited and record the experiment as rejected.

If audio continuity is accepted but micro-freezes remain, keep the seek fix and
record that the published-clock change did not fully explain the visual
symptom.

If both disappear, record both operator observations while retaining the
distinction between high-confidence seek causality and runtime acceptance of
the clock experiment.

- [ ] **Step 3: Record E-041 with exact evidence**

The evidence file must contain:

- the three implementation commit IDs;
- the exact SHA256 printed in Task 4;
- the exact rollback path;
- the production build command and five-test result;
- the static facts that passthrough DirectSound omits the policy hook,
  in-margin WASAPI seeks jump to RVA `0x002401D4`, real margin seeks execute
  the original block, and `CurrentOutputFrame` has no endpoint call/mutex;
- the user's observation for audio continuity;
- the user's observation for BGM/chart sync;
- the user's separate observation for visual micro-freezes;
- a verdict of accepted, partially accepted, or rejected without extrapolating
  beyond that run.

Add E-041 to `INDEX.md`. Add only evidence-supported findings to
`FINDINGS.md`.

- [ ] **Step 4: Verify and commit the runtime record**

Run:

```powershell
git diff --check
git diff -- .planning/debug/high-fps-timing-domains/FINDINGS.md .planning/debug/high-fps-timing-domains/evidence/INDEX.md .planning/debug/high-fps-timing-domains/evidence/E-041-wasapi-resync-stutter-experiment.md
git add -- .planning/debug/high-fps-timing-domains/FINDINGS.md .planning/debug/high-fps-timing-domains/evidence/INDEX.md .planning/debug/high-fps-timing-domains/evidence/E-041-wasapi-resync-stutter-experiment.md
git commit -m "docs: record WASAPI resync experiment result"
```

Expected: the closeout commit contains only the three debug-evidence files and
matches the operator's actual result.

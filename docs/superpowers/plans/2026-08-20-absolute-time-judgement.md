# Absolute-Time Judgement Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Status:** Complete implementation plan; execution requires separate user authorization.

**Goal:** Build an opt-in WASAPI-exclusive judgement path that feeds every successfully observed physical transition to native recognition/score at its exact audio-derived song time, with render-independent results at 60, 144, 165, and 240 FPS.

**Architecture:** Keep the current input worker/FastIO path, existing high-FPS framerate/shared-`Tune` hooks, and all native note policy. Use the audited `CTuneGameManager` state-construction/cleanup pair as the explicit stage boundary. Add a gameplay-only QPC transition journal, exact WASAPI endpoint and multi-epoch playback history, retained causal history, and a private event/60-Hz-heartbeat scheduler that replaces only the native uniform judgement loop and answers five lower CBooster queries inside immutable scopes.

**Tech Stack:** Windows x86 C++23, CMake presets, reflect-cpp/TOML, Raw Input/XInput, DirectSound facade, miniaudio mixer, WASAPI `IAudioClock`, SafetyHook, plog, checked integer/rational arithmetic.

**Spec:** `docs/superpowers/specs/2026-08-20-absolute-time-judgement-spec.md`

## Global Constraints

- Use no GSD command, skill, artifact, or workflow. This is a Superpowers plan.
- Start from the post-ASIO clean implementation plus design commit `0484300`; failed high-FPS judgement code is negative evidence, not source to restore wholesale.
- Do not change any file under `src/Patches/Framerate/`; do not change `GameplaySongClock::Create(target_fps, 1)`, `HookGameplaySongClock`, `Tune+0x10`, or `Tune+0x14` behavior.
- Do not install or use RVA `0x23FA0C`/VA `0x63FA0C`.
- The only new game-image interception sites are native stage begin RVA `0x2629A0`, native stage end RVA `0x262080`, scheduler RVA `0x240239`, and CBooster RVAs `0x22DFB0`, `0x22DF50`, `0x22DD30`, `0x22E480`, and `0x22DAA0`.
- Preserve native candidate order, effective-type routing, component order, handlers, lifecycle, late/grade/score/effect policy, and free input. Add no loader note-type table.
- The feature is startup-only and defaults off at `[experimental].enable_absolute_time_judgement = false`.
- Enabled mode requires `audio_backend = 'wasapi_exclusive'`, `input_poll_hz = 1000`, live `HoldSafeFrame == 0`, and live `SlideHoldSafeFrame == 0`. DirectSound and ASIO are rejected for enabled mode.
- Enabled mode uses the same implementation at 60, 144, 165, and 240 FPS.
- Stage lifecycle comes only from successful native gameplay-state construction
  at RVA `0x2629A0` and native cleanup entry at RVA `0x262080`. Playback
  `Play`, seek, stop, drain, generation, buffer identity, elapsed time,
  inactivity, render count, and pointer reuse never delimit a stage.
- Use exact checked rational/integer arithmetic. No render-frame term, rounded `16.67 ms` accumulation, current-cursor fallback, midpoint, monotonic clamp, or arbitrary origin correction may produce judgement time.
- Verify all eight signatures before creating the first hook. Any expected-success setup/invariant result that fails takes an always-on startup or active-stage fatal path; do not build fallback/retry machinery. Partial operational activation and active-stage native fallback are forbidden.
- Do not rely on C/C++ `assert()` alone: every fatal invariant check remains active in Release. Explicit operational statuses (`NoPlayback`, `Pending`, `TemporarilyUnavailable`) are not assertion failures.
- Do not restore the test suite, add test targets, add gameplay emulation, run CTest, or invoke TDD. Each review/build/runtime step below states the evidence it can actually establish.
- Build success is compilation/static evidence only. Actual game behavior is required for gameplay acceptance.
- Do not mutate or deploy into `H:\gc` during implementation tasks. Runtime deployment requires a separate explicit user authorization at Task 11.
- Commit each task only after its listed review/build gate succeeds. Preserve unrelated worktree changes.

---

## File and responsibility map

### Configuration

- Modify `src/Config/config.h`: required Boolean field and `ConfigManager` getter.
- Modify `src/Config/config.cpp`: exact enabled-mode backend/poll validation.
- Modify `config.toml`: repository default `false` key only.
- Modify `tools/ConfigGUI/Main.cpp`: startup-only checkbox, dependency warning, and tooltip.

### Input transport

- Create `src/Input/Polling/GameplayTransitionJournal.h`: 10-bit record, epoch/status, fixed-capacity API.
- Create `src/Input/Polling/GameplayTransitionJournal.cpp`: synchronized 65,536-record ring and QPC publication.
- Modify `src/Input/Polling/InputSnapshotState.{h,cpp}`: publish and consume the existing logical-action-to-FastIO map for journal projection.
- Modify `src/Input/Polling/InputPollingRuntime.{h,cpp}`: prepare/begin/end/drain transport and publish one record per gameplay mask transition.
- Modify `src/Input/CMakeLists.txt`: compile the journal.

### Exact arithmetic

- Create `src/Timing/CheckedRational.{h,cpp}`: normalized signed rational, overflow-checked arithmetic, exact compare/floor/ceil/truncation.
- Create `src/Timing/CMakeLists.txt` and modify `src/CMakeLists.txt`: `gc_timing` library used by audio and judgement.

### WASAPI and playback authority

- Create `src/Audio/ExactAudioTime.h`: statuses and shared exact-audio identity types without circular ownership.
- Create `src/Audio/Wasapi/ExactWasapiClock.h`: anchor/status/provider/registry contracts.
- Create `src/Audio/Wasapi/ExactWasapiClock.cpp`: preallocated SPSC anchor history and exact QPC projection.
- Modify `src/Audio/AudioPatch.cpp` and `src/Audio/AudioPatchInternal.h`: pass the startup-only exact-provider enable bit into WASAPI engine creation.
- Modify `src/Audio/Mixer/AudioCursorTimeline.{h,cpp}`: independent preallocated exact playback-epoch history across `Play` and seek generations; keep the existing 32-span cursor behavior unchanged.
- Modify `src/Audio/Mixer/MiniaudioMixer.cpp`: publish exact origin/tail from the existing cumulative mapping.
- Modify `src/Audio/DirectSound/GameplayAudioCursorObservation.{h,cpp}`: preserve the native group getter's existing `Consume()` channel authority while adding a lifetime-safe exact-history handle.
- Modify `src/Audio/DirectSound/DirectSoundFacade.{h,cpp}`: unique buffer instance IDs and lifetime-safe exact mapping handle in group-2 observations.
- Modify `src/Audio/Wasapi/ExclusiveAudioEngine.{h,cpp}`: create/register/invalidate the endpoint generation and publish anchors after successful output commit.
- Modify `src/Audio/CMakeLists.txt`: compile/link exact timing publications.

### Judgement feature

- Create `src/Patches/AbsoluteJudgement/AbsoluteJudgementDiagnostics.{h,cpp}`: stage-open/activation/end records, counters, summaries, Verbose scope records, first-fatal latch/snapshot.
- Modify `src/Logging/SessionLog.{h,cpp}` and `src/Loader/DllMain.cpp`: explicit active-log flush used by the fatal path.
- Create `src/Patches/AbsoluteJudgement/JudgementHistory.{h,cpp}`: retained resolved transitions and logical query algebra.
- Create `src/Patches/AbsoluteJudgement/JudgementScope.{h,cpp}`: immutable TLS scope and five query entry points.
- Create `src/Patches/AbsoluteJudgement/JudgementClockResolver.{h,cpp}`: endpoint output -> bound source -> `J = R + GameTimeOffset`.
- Create `src/Patches/AbsoluteJudgement/JudgementStage.{h,cpp}`: explicit native stage generation, clock-wait/active identities, baseline/cutoff, discontinuity rules.
- Create `src/Patches/AbsoluteJudgement/JudgementScheduler.{h,cpp}`: ordered event/heartbeat merge, private boundary/frontier, bounded catch-up.
- Create `src/Patches/AbsoluteJudgement/NativeJudgementAbi.h`: audited RVAs, bytes, layouts, and x86 function types.
- Create `src/Patches/AbsoluteJudgement/AbsoluteJudgementRuntime.{h,cpp}`: game-thread orchestration and native pair invocation.
- Create `src/Patches/AbsoluteJudgement/AbsoluteJudgementPatch.{h,cpp}`: eight SafetyHook handlers and fail-fast all-or-none installation.
- Modify `src/Patches/CMakeLists.txt`: compile feature sources and link `gc_input`, `gc_audio`, `gc_timing`, `gc_logging`, `gc_system_path`, and SafetyHook.
- Modify `src/Loader/DllMain.cpp`: initialize after audio interception and before framerate/Switch initialization; fail closed when enabled.

The cross-audio/input/native feature is intentionally one ordered plan rather
than independent deployable plans: no subset is allowed to activate. The task
boundaries are review/commit boundaries; Task 9 is the first point at which the
runtime feature becomes reachable.

---

### Task 1: Add the strict startup-only configuration contract

**Files:**

- Modify: `src/Config/config.h`
- Modify: `src/Config/config.cpp`
- Modify: `config.toml`
- Modify: `tools/ConfigGUI/Main.cpp`

**Interfaces:**

- Produces: `bool ConfigManager::GetEnableAbsoluteTimeJudgement() const`.
- Enforces: enabled implies `AudioBackend::wasapi_exclusive` and `input_poll_hz == 1000` before any patch initialization.

- [ ] **Step 1: Add the required reflected field and getter**

Add beside the other experimental Booleans in `ExperimentalConfig`:

```cpp
rfl::Rename<"enable_absolute_time_judgement", bool>
    enable_absolute_time_judgement = false;
```

Add beside the existing experimental getters:

```cpp
[[nodiscard]] bool GetEnableAbsoluteTimeJudgement() const
{
    return config.experimental().enable_absolute_time_judgement();
}
```

Do not use `rfl::DefaultIfMissing`: runtime configuration remains strict and complete.

- [ ] **Step 2: Enforce the exact enabled-mode dependencies**

In `ValidateInputConfig`, after the existing audio and native-input validations, return these exact errors:

```cpp
if (value.experimental().enable_absolute_time_judgement() &&
    value.experimental().audio_backend() != AudioBackend::wasapi_exclusive) {
    return std::unexpected(
        "Absolute-time judgement requires "
        "[experimental].audio_backend = 'wasapi_exclusive'");
}
if (value.experimental().enable_absolute_time_judgement() &&
    value.input_poll_hz() != 1000) {
    return std::unexpected(
        "Absolute-time judgement requires input_poll_hz = 1000");
}
```

Leave feature-off backend and poll-rate validation unchanged.

- [ ] **Step 3: Add the repository default**

Under `[experimental]` in `config.toml`, add only:

```toml
enable_absolute_time_judgement = false
```

Do not edit `H:\gc\config.toml`.

- [ ] **Step 4: Add the ConfigGUI control**

In `DrawExperimental`, directly after Target FPS, bind a checkbox to the reflected Boolean with label `Absolute-time judgement (WASAPI)` and this tooltip:

```text
Uses exact audio time for gameplay judgement at every FPS.
Requires WASAPI exclusive, input_poll_hz = 1000, and restart.
Only HoldSafeFrame = 0 and SlideHoldSafeFrame = 0 are supported.
```

When checked while another backend is selected, show an amber inline message: `Select WASAPI exclusive before saving.` The existing complete validation remains the save gate.

- [ ] **Step 5: Review schema and feature-off behavior**

Confirm by inspection that:

- missing key remains a strict parse failure;
- `false` imposes no backend restriction;
- `true` rejects DirectSound, ASIO, and poll rates 125/250/500; and
- neither the config getter nor GUI changes any runtime hook state.

- [ ] **Step 6: Build the directly affected products**

Run from the x86 MSVC environment:

```powershell
cmake --preset msvc32-debug
cmake --build --preset msvc32-debug --target iDmacDrv32 ConfigGUI
```

Expected evidence: both targets compile and link. This does not prove parsing or gameplay behavior.

- [ ] **Step 7: Commit**

```powershell
git add -- src/Config/config.h src/Config/config.cpp config.toml tools/ConfigGUI/Main.cpp
git commit -m "Add absolute-time judgement setting"
```

---

### Task 2: Add the clean gameplay transition journal

**Files:**

- Create: `src/Input/Polling/GameplayTransitionJournal.h`
- Create: `src/Input/Polling/GameplayTransitionJournal.cpp`
- Modify: `src/Input/Polling/InputSnapshotState.h`
- Modify: `src/Input/Polling/InputSnapshotState.cpp`
- Modify: `src/Input/Polling/InputPollingRuntime.h`
- Modify: `src/Input/Polling/InputPollingRuntime.cpp`
- Modify: `src/Input/CMakeLists.txt`

**Interfaces:**

- Produces:

```cpp
namespace gc::input {
using GameplayHeldMask = std::uint16_t;
inline constexpr std::size_t kGameplayTransitionCapacity = 65'536;

struct GameplayTransitionRecord {
    std::uint64_t transport_epoch{};
    std::uint64_t sequence{};
    std::int64_t qpc_ticks{};
    GameplayHeldMask held_before{};
    GameplayHeldMask held_after{};
    GameplayHeldMask rising{};
    GameplayHeldMask falling{};
};

struct GameplayTransitionStatus {
    bool enabled{};
    bool active{};
    std::uint64_t transport_epoch{};
    std::uint64_t next_sequence{};
    std::uint64_t eviction_count{};
    std::uint32_t depth{};
    GameplayHeldMask published_held{};
    std::int64_t qpc_frequency{};
};

struct GameplayTransitionCutoff {
    std::uint64_t transport_epoch{};
    std::uint64_t first_stage_sequence{};
    std::uint64_t eviction_count{};
    GameplayHeldMask held_baseline{};
    std::int64_t qpc_frequency{};
};

bool PrepareGameplayTransitionTransport(bool enabled) noexcept;
void BeginGameplayTransitionEpoch(GameplayHeldMask baseline) noexcept;
void EndGameplayTransitionEpoch() noexcept;
bool CaptureGameplayTransitionCutoff(
    GameplayTransitionCutoff* output) noexcept;
void PublishGameplayTransition(
    std::uint32_t previous_fastio,
    std::uint32_t next_fastio,
    std::int64_t observed_qpc_ticks) noexcept;
std::size_t DrainGameplayTransitions(
    std::span<GameplayTransitionRecord> output,
    GameplayTransitionStatus* status) noexcept;
GameplayTransitionStatus ReadGameplayTransitionStatus() noexcept;
GameplayHeldMask GameplayMaskFromFastIo(std::uint32_t word) noexcept;
}
```

- Consumes: the current worker's aggregate `g_published_input` and public logical-action-to-FastIO masks.

- [ ] **Step 1: Expose the existing ten-control projection**

Move the current `kFastIoBits` mapping into `InputSnapshotState.h` as an inline constexpr array named `kFastIoMaskByLogicalAction`, preserving its values and order exactly. Add `kGameplayLogicalInputCount = 10`. Make `InputSnapshotState.cpp` consume that same array so FastIO and the journal cannot drift.

The journal's compact bits are logical IDs `0..9`, not the numeric FastIO bit
positions. Preserve this source mapping exactly:

| Logical ID/action | Existing FastIO mask |
|---|---|
| `0 LeftBoosterUp` | `P1_UP` |
| `1 LeftBoosterDown` | `P2_UP` |
| `2 LeftBoosterLeft` | `P1_DOWN` |
| `3 LeftBoosterRight` | `P2_DOWN` |
| `4 LeftBoosterButton` | `P1_BUTTON_1` |
| `5 RightBoosterUp` | `P1_LEFT` |
| `6 RightBoosterDown` | `P2_LEFT` |
| `7 RightBoosterLeft` | `P1_RIGHT` |
| `8 RightBoosterRight` | `P2_RIGHT` |
| `9 RightBoosterButton` | `P2_BUTTON_1` |

Do not “correct” the surprising FastIO names; current aggregate behavior and
native logical IDs are the contract.

- [ ] **Step 2: Define a fixed-capacity synchronized ring**

Implement one process-owned journal using `std::array<GameplayTransitionRecord, 65'536>`, one mutex, read slot, size, epoch, sequence, eviction count, enabled/active flags, baseline, and cached QPC frequency. Do not allocate in `Push`, `Drain`, or the worker path.

`PrepareGameplayTransitionTransport(true)` calls `QueryPerformanceFrequency`,
requires a positive result, clears storage, and arms publication. `false`
clears/disables publication and returns true. This low-level library does not
own startup UI or process policy: Task 9 checks the result exactly once and
immediately calls the startup-fatal path on false. There is no fallback or
retry state. Epoch and sequence overflow latch an internal fault represented
by an incremented eviction/error count; they are never wrapped into apparently
valid history.

- [ ] **Step 3: Build records only for the ten gameplay controls**

Use the shared FastIO array to project `previous_fastio` and `next_fastio`. When the two 10-bit masks are equal, publish no record even if a service/system bit changed. The worker samples QPC once immediately before its aggregate exchange whenever absolute publication is armed, then passes that captured value to the journal after the exchange. Store:

```cpp
record.rising = next & ~previous;
record.falling = previous & ~next;
```

Keep a multi-bit snapshot change in one record. Push after the existing aggregate exchange so the accepted rare aggregate/journal handoff window remains explicit rather than hidden by a new watermark protocol. Sampling QPC before that exchange means a producer delay can make the record arrive late, but cannot silently retimestamp the physical observation to the end of the delay. Check `QueryPerformanceCounter` once and hard-abort on failure; do not add another clock or retry.

- [ ] **Step 4: Bind epochs to the real input worker lifecycle**

After the worker initializes its mapper and clears state, call `BeginGameplayTransitionEpoch(GameplayMaskFromFastIo(0))` before the initial `Publish()`. In `Shutdown`, replace the direct aggregate clear with `mapper_->ClearAll(); Publish();`, then call `EndGameplayTransitionEpoch()` after that final cleared record. Ensure every normal and exception shutdown path ends an armed epoch exactly once before any outer defensive zero store. A new first open gets a new epoch; additional reference-counted opens do not.

- [ ] **Step 5: Extend `Publish()` without changing FastIO**

Retain the existing `g_published_input.exchange(next, acq_rel)` as the FastIO
authority. Cache the startup-only feature bit in the worker as
`bool absolute_publication_enabled_{}`; when armed, capture QPC into
`observed_qpc_ticks` immediately before that exchange. On `previous != next`, make the journal
publisher the first operation after the exchange, then run the existing Debug
snapshot log:

```cpp
if (previous != next) {
    if (absolute_publication_enabled_) {
        PublishGameplayTransition(previous, next, observed_qpc_ticks);
    }
    PLOG_DEBUG << "Input snapshot fastio=0x" << std::hex << next
               << std::dec;
}
```

Do not place logging, formatting, status inspection, or unrelated work between
the exchange and journal push. The journal mutex may still create the explicitly
accepted rare late handoff, but Debug logging must not enlarge that window. Add
no per-transition Info log.

- [ ] **Step 6: Add bounded drain/status access**

`DrainGameplayTransitions` copies up to the caller's span in strict sequence order and returns one status snapshot taken under the same mutex. It never discards additional queued records. The scheduler will repeatedly drain fixed-size batches in Task 8.

`CaptureGameplayTransitionCutoff` is the sole stage-start cutoff operation. Under
the same mutex it requires enabled/active transport, returns epoch,
`first_stage_sequence = next_sequence`, current held baseline, QPC frequency,
and current eviction count, then discards the queued pre-stage prefix. A record
whose aggregate exchange won the race but whose journal push has not yet taken
the mutex arrives after the cutoff and follows the explicitly accepted late
record rule. Task 8 checks the Boolean once and enters the active-stage fatal
path on false; there is no retry state.

- [ ] **Step 7: Review transport invariants against current source**

Inspect the diff and establish only these source facts:

- FastIO still reads `g_published_input` unchanged;
- Raw Input still publishes on keyboard events;
- XInput still polls on the configured 1000-Hz worker timer;
- system-only changes cannot enter judgement history;
- 65,536 records cover more than 60 seconds at 1000 records/s; and
- overflow is observable rather than overwriting an unknown prefix silently.

- [ ] **Step 8: Build**

```powershell
cmake --build --preset msvc32-debug --target iDmacDrv32
```

Expected evidence: journal types and worker integration compile/link. Do not claim transition behavior without a later real process run.

- [ ] **Step 9: Commit**

```powershell
git add -- src/Input/Polling/GameplayTransitionJournal.h src/Input/Polling/GameplayTransitionJournal.cpp src/Input/Polling/InputSnapshotState.h src/Input/Polling/InputSnapshotState.cpp src/Input/Polling/InputPollingRuntime.h src/Input/Polling/InputPollingRuntime.cpp src/Input/CMakeLists.txt
git commit -m "Publish timestamped gameplay transitions"
```

---

### Task 3: Add the checked exact-rational foundation

**Files:**

- Create: `src/Timing/CheckedRational.h`
- Create: `src/Timing/CheckedRational.cpp`
- Create: `src/Timing/CMakeLists.txt`
- Modify: `src/CMakeLists.txt`

**Interfaces:**

- Produces:

```cpp
namespace gc::timing {
enum class RationalError : std::uint8_t {
    ZeroDenominator,
    Overflow,
    DivisionByZero,
};

class CheckedRational final {
public:
    static std::expected<CheckedRational, RationalError> Create(
        std::int64_t numerator,
        std::uint64_t denominator) noexcept;
    static CheckedRational Whole(std::int64_t value) noexcept;

    [[nodiscard]] std::int64_t numerator() const noexcept;
    [[nodiscard]] std::uint64_t denominator() const noexcept;
    [[nodiscard]] int Compare(const CheckedRational&) const noexcept;
    [[nodiscard]] std::expected<CheckedRational, RationalError>
        Add(const CheckedRational&) const noexcept;
    [[nodiscard]] std::expected<CheckedRational, RationalError>
        Subtract(const CheckedRational&) const noexcept;
    [[nodiscard]] std::expected<CheckedRational, RationalError>
        Multiply(std::int64_t numerator,
                 std::uint64_t denominator) const noexcept;
    [[nodiscard]] std::expected<std::int64_t, RationalError>
        Floor() const noexcept;
    [[nodiscard]] std::expected<std::int64_t, RationalError>
        Ceil() const noexcept;
    [[nodiscard]] std::expected<std::int64_t, RationalError>
        Truncate() const noexcept;
};
}
```

- Consumed by: exact WASAPI output projection, playback-source mapping, judgement `J`, heartbeat/index arithmetic, held age, and lookback.

- [ ] **Step 1: Lock representation invariants**

Every constructed value has denominator greater than zero and is reduced by the
GCD of the numerator's unsigned magnitude and the denominator. Compute negative
magnitude in unsigned arithmetic so `INT64_MIN` is valid and never negated in
the signed domain. Reapply the sign with an explicit `2^63 -> INT64_MIN` case;
all other negative magnitudes must fit below `2^63`. Zero is canonical `0/1`.
`Whole` therefore accepts every `int64_t` value without contradicting the
normalization invariant.

- [ ] **Step 2: Implement overflow-free comparison**

Do not cross-multiply two arbitrary 64-bit fractions. Compare sign first, then use the Euclidean/continued-fraction quotient-and-remainder algorithm: compare integer quotients; when equal, recurse/iterate on inverted positive remainders while reversing comparison direction. Document this identity immediately above `Compare`.

- [ ] **Step 3: Implement arithmetic with cross-cancellation**

For multiplication, cancel numerator/denominator factors with `gcd` before checked multiplication. For addition/subtraction, first reduce denominators by their `gcd`, checked-multiply only the reduced factors, checked-add the signed numerators, then normalize. Every failed checked operation returns `RationalError::Overflow`.

- [ ] **Step 4: Implement signed projections explicitly**

`Truncate()` uses C++ signed division (toward zero). `Floor()` returns the quotient unchanged for nonnegative/exact values and subtracts one only when a negative value has a nonzero remainder. `Ceil()` is the exact symmetric operation: add one only for a positive non-integral value. These distinctions are required for negative pre-song `native_ms`, signed heartbeat/frame indices, and the first boundary at or after a playback origin.

- [ ] **Step 5: Record the formal review note in source**

At the top of `CheckedRational.cpp`, cite the representation invariants and the Euclidean comparison identity. This is code-review evidence for a mathematical primitive, not a loader-authored gameplay oracle. Add no executable test.

- [ ] **Step 6: Wire `gc_timing` and build**

`src/Timing/CMakeLists.txt` creates a static `gc_timing` target exposing `${PROJECT_SOURCE_DIR}/src`. Add `add_subdirectory(Timing)` before Audio/Input/Patches in `src/CMakeLists.txt`.

```powershell
cmake --preset msvc32-debug
cmake --build --preset msvc32-debug --target gc_timing
```

Expected evidence: the exact arithmetic implementation compiles under the supported x86 MSVC toolchain.

- [ ] **Step 7: Commit**

```powershell
git add -- src/Timing/CheckedRational.h src/Timing/CheckedRational.cpp src/Timing/CMakeLists.txt src/CMakeLists.txt
git commit -m "Add checked rational timing arithmetic"
```

---

### Task 4: Publish exact WASAPI endpoint history

**Files:**

- Create: `src/Audio/ExactAudioTime.h`
- Create: `src/Audio/Wasapi/ExactWasapiClock.h`
- Create: `src/Audio/Wasapi/ExactWasapiClock.cpp`
- Modify: `src/Audio/AudioPatch.cpp`
- Modify: `src/Audio/AudioPatchInternal.h`
- Modify: `src/Audio/Mixer/AudioCursorTimeline.h`
- Modify: `src/Audio/Mixer/AudioCursorTimeline.cpp`
- Modify: `src/Audio/Wasapi/ExclusiveAudioEngine.h`
- Modify: `src/Audio/Wasapi/ExclusiveAudioEngine.cpp`
- Modify: `src/Audio/CMakeLists.txt`

**Interfaces:**

- Produces:

```cpp
enum class ExactClockStatus : std::uint8_t {
    NoPlayback,
    Pending,
    OutsidePlayback,
    Resolved,
    TemporarilyUnavailable,
    HistoryLost,
    Discontinuous,
};

struct EndpointClockMapping {
    std::uint64_t origin_position{};
    std::uint64_t clock_frequency{};
    std::uint64_t origin_output_frame{};
    std::uint32_t output_sample_rate{};
};

struct ExactWasapiAnchor {
    std::uint64_t sequence{};
    std::uint64_t endpoint_generation{};
    std::uint64_t endpoint_position{};
    std::uint64_t qpc_100ns{};
    EndpointClockMapping mapping{};
    std::uint64_t submitted_output_tail{};
};

struct ExactOutputClockResult {
    ExactClockStatus status{};
    std::uint64_t endpoint_generation{};
    std::optional<gc::timing::CheckedRational> output_frame;
    std::uint64_t submitted_output_tail{};
};

class ExactWasapiClock final {
public:
    static std::shared_ptr<ExactWasapiClock> Create(
        std::uint64_t endpoint_generation,
        std::uint32_t output_sample_rate,
        std::uint64_t clock_frequency,
        std::int64_t qpc_frequency,
        std::uint32_t period_frames) noexcept;
    void Publish(const ExactWasapiAnchor&) noexcept;
    void Invalidate() noexcept;
    ExactOutputClockResult ResolveQpc(std::int64_t raw_qpc_ticks) const noexcept;
    [[nodiscard]] std::uint64_t endpoint_generation() const noexcept;
    [[nodiscard]] std::int64_t qpc_frequency() const noexcept;
};

std::shared_ptr<const ExactWasapiClock> AcquireExactWasapiClock() noexcept;
```

Place `ExactClockStatus` and `EndpointClockMapping` in
`src/Audio/ExactAudioTime.h`. Both the existing mapper/timeline header and the
new WASAPI provider include that one dependency; neither includes the other.
This prevents a circular `AudioCursorTimeline`/`ExactWasapiClock` ownership
relationship.

`output_frame` is engaged only for `Resolved`; every other status carries
`std::nullopt`. Do not default-construct a fake zero frame.

- [ ] **Step 1: Expose the existing endpoint mapper identity without changing projection**

Add `EndpointClockMapper::mapping() const` returning its four immutable origin/rate fields. Keep `ToOutputFrame` byte-for-byte behaviorally unchanged for the existing presented-output clock.

- [ ] **Step 2: Allocate a period-derived SPSC history before rendering**

Pass `enable_absolute_time_judgement` explicitly from the already parsed
configuration through `ProductionWasapiOutputBackendFactory`,
`StartProductionExclusiveAudioEngine`, and `ExclusiveAudioEngine::StartAndWait`.
When false, do not allocate or register an exact provider; all existing WASAPI
calls remain on their old path.

When true, `ExactWasapiClock::Create` computes:

```cpp
capacity = ceil(60 * output_sample_rate / period_frames) + 2;
```

using checked integer arithmetic directly from the endpoint's validated frame
period and sample rate; do not round through `REFERENCE_TIME`. Allocate all
slots once before `RenderLoop`. Each slot uses a seqlock generation plus scalar
atomics so the audio thread is the sole writer and the game thread never waits.
Use `std::memory_order_seq_cst` for both the version and every payload field in
the first implementation: odd version, complete scalar payload, next
even version; a reader accepts only matching surrounding even versions. Do not
weaken these orders without a separate C++ memory-model proof. Creation failure
prevents enabled-mode capability from becoming usable; it never allocates
inside the render loop.

- [ ] **Step 3: Implement exact QPC-domain comparison and delta**

Compare raw QPC `ticks/frequency` with anchor `qpc_100ns/10,000,000` by whole-second quotient plus reduced remainder comparison; do not multiply absolute uptime counters. Once an anchor at/before the input is selected, form the at-most-retention-window delta from whole seconds and remainders, then use `CheckedRational` for:

```text
endpoint(q) = anchor.endpoint_position + delta_seconds * clock_frequency
output(q)   = mapping.origin_output_frame
            + (endpoint(q) - mapping.origin_position)
              * output_sample_rate / clock_frequency
```

Require `0 <= output(q) < submitted_output_tail`. A value at/after the tail is `Pending`, not clamped.

- [ ] **Step 4: Implement explicit provider statuses**

- no anchor old enough or projected output not submitted yet: `Pending`;
- one stable same-generation projection inside tail: `Resolved`;
- a bounded reader attempt cannot obtain a coherent same-generation
  publication while the provider remains registered: `TemporarilyUnavailable`;
- required input QPC older than the oldest retained same-generation anchor: `HistoryLost`;
- invalidated/replaced generation or decreasing anchor identity: `Discontinuous`.

`ExactWasapiClock` itself never returns `NoPlayback` or `OutsidePlayback`;
those shared statuses are reserved for the native group-2 probe and the
voice-history resolver respectively. `AcquireExactWasapiClock()` returning null
after enabled audio startup is a caller-level capability/invariant failure, not
a state on which a native stage waits.

Do not return the last output frame for any non-Resolved status.

- [ ] **Step 5: Add a lifetime-safe active-provider registry**

The engine owns a `shared_ptr<ExactWasapiClock>`. Implement one process-owned
registry containing a mutex, `std::weak_ptr<ExactWasapiClock>`, and endpoint
generation. Treat the weak handle and generation as one mutex-protected state:

- registration stores both under the mutex before the render loop starts;
- `AcquireExactWasapiClock` takes the mutex, promotes the weak handle while
  still locked, verifies that the strong handle reports the stored generation,
  and returns it;
- unregistration takes the expected generation and clears the registry only
  when it still matches, so delayed cleanup cannot remove a newer provider.

The registry mutex is used only by register/acquire/unregister. Neither
`RenderLoop` nor `ExactWasapiClock::Publish` may take it. Assign endpoint
generations from a process-wide monotonic atomic and treat zero or wrap as
creation failure.

- [ ] **Step 6: Publish only after successful output commit**

In `ExclusiveAudioEngine::AudioThreadMain`, pass the already validated endpoint
period frames and output rate directly, then create/register the provider after
endpoint/mixer/mapper initialization and before `Start`. This registration is
complete before the engine signals successful initialization. In `RenderLoop`,
after `SubmitPcm16` and `pacing_tracker_->Commit(decision)` succeed, publish raw
`clock.position`, `clock.qpc_100ns`, mapper identity, and `submitted_tail`.
Keep the existing `WasapiPresentedOutputClock::Publish` call unchanged beside
it.

On a failed `ReadClock`, publish the failure context before following the
existing audio runtime-fatal path. Do not make the WASAPI render loop retry for
this feature. On cleanup/destruction, call `Invalidate` and unregister the
exact provider. No logging, lock, allocation, or wait is added to the
successful audio-thread publication path.

- [ ] **Step 7: Confirm the old clock is not repurposed**

Inspect the diff and verify `PresentedClockPublication::Project`, its monotonic last-value behavior, `WasapiPresentedOutputClock::CurrentOutputFrame`, and the existing 32-span resolution are unchanged. The new provider is a side-by-side authority used only by absolute judgement.

Link `gc_audio` publicly to `gc_timing`; do not copy rational helpers into the
audio target.

- [ ] **Step 8: Build**

```powershell
cmake --build --preset msvc32-debug --target gc_audio iDmacDrv32
```

Expected evidence: audio/provider code compiles and the existing facade still links. No callback or gameplay claim is made yet.

- [ ] **Step 9: Commit**

```powershell
git add -- src/Audio/ExactAudioTime.h src/Audio/Wasapi/ExactWasapiClock.h src/Audio/Wasapi/ExactWasapiClock.cpp src/Audio/AudioPatch.cpp src/Audio/AudioPatchInternal.h src/Audio/Mixer/AudioCursorTimeline.h src/Audio/Mixer/AudioCursorTimeline.cpp src/Audio/Wasapi/ExclusiveAudioEngine.h src/Audio/Wasapi/ExclusiveAudioEngine.cpp src/Audio/CMakeLists.txt
git commit -m "Publish exact WASAPI clock history"
```

---

### Task 5: Publish exact multi-epoch voice history through the native group-2 choice

**Files:**

- Modify: `src/Audio/Mixer/AudioCursorTimeline.h`
- Modify: `src/Audio/Mixer/AudioCursorTimeline.cpp`
- Modify: `src/Audio/Mixer/MiniaudioMixer.cpp`
- Modify: `src/Audio/Wasapi/ExclusiveAudioEngine.h`
- Modify: `src/Audio/Wasapi/ExclusiveAudioEngine.cpp`
- Modify: `src/Audio/DirectSound/GameplayAudioCursorObservation.h`
- Modify: `src/Audio/DirectSound/GameplayAudioCursorObservation.cpp`
- Modify: `src/Audio/DirectSound/DirectSoundFacade.h`
- Modify: `src/Audio/DirectSound/DirectSoundFacade.cpp`

**Interfaces:**

- Produces:

```cpp
enum class ExactPlaybackOrigin : std::uint8_t {
    Play,
    Seek,
};

enum class ExactPlaybackClosure : std::uint8_t {
    LaterEpoch,
    NaturalEnd,
    WriterQuiescedRelease,
};

struct ExactPlaybackEpoch {
    std::uint64_t buffer_instance_id{};
    std::uint64_t endpoint_generation{};
    std::uint64_t playback_generation{};
    ExactPlaybackOrigin origin{};
    std::uint64_t output_origin{}; // O0
    std::uint64_t source_origin{}; // S0
    std::uint32_t output_rate{};   // Fo
    std::uint32_t source_rate{};   // Fs
    std::uint64_t mapped_output_tail{};
    std::optional<ExactPlaybackClosure> closure;
    std::optional<gc::timing::CheckedRational> closed_source_tail;
};

struct ExactSourceCoordinate {
    gc::timing::CheckedRational source_frame;
    std::uint32_t source_rate{};
};

struct ExactSourceFrameResult {
    ExactClockStatus status{};
    std::uint64_t buffer_instance_id{};
    std::uint64_t playback_generation{};
    std::optional<ExactSourceCoordinate> resolved;
    std::optional<ExactSourceCoordinate> closed_frontier;
};

struct ExactPlaybackHistoryStatus {
    ExactClockStatus status{};
    std::uint64_t publication_sequence{};
    bool prefix_evicted{};
};

// New AudioCursorTimeline members:
ExactSourceFrameResult ResolveExactSourceFrame(
    const gc::timing::CheckedRational& output) const noexcept;
std::size_t CopyExactPlaybackEpochs(
    std::span<ExactPlaybackEpoch> output,
    ExactPlaybackHistoryStatus* status) const noexcept;
```

`resolved` is engaged only for `Resolved`; every other status carries
`std::nullopt`. `closed_frontier` is engaged only for
`OutsidePlayback` when a preceding epoch tail is exactly closed; it is absent
before the first origin and for every other status. Do not expose a fake zero
source position or a last-known-current value.

`AudioCursorTimeline::ResolveExactSourceFrame(output)` selects the retained
epoch whose coverage contains `output`, then applies
`S(O) = S0 + (O-O0)*Fs/Fo` only within that epoch's published mapped tail.
`CopyExactPlaybackEpochs(span, status)` copies one bounded coherent snapshot
into caller-owned 256-entry scratch storage. It returns the copied count plus
publication sequence/prefix-eviction state, reports `Pending` before the first
epoch and `TemporarilyUnavailable` after a bounded failed coherent read, and
never allocates. Task 8 uses this snapshot for first-origin binding, exact
overlap validation, and diagnostics; it never reaches into ring slots directly.

- [ ] **Step 1: Add an independent exact epoch history**

Inside `AudioCursorTimeline`, add a separately optional, preallocated 256-slot
exact epoch ring. Assign the secondary buffer's process-unique ID before voice
creation. When the feature is enabled, `ExclusiveAudioEngine::CreateVoice`
configures the ring exactly once with that ID and the active exact endpoint
generation, but only for `VoiceUsage::GameplayNativeCandidate`; general voices
keep no exact ring. Check that expected-success operation once and enter the
existing audio fatal path if it fails. Do not retry and do not fall back to a
rounded/general history.

Do not store the public `ExactPlaybackEpoch` structure as the shared slot
payload. Define an internal slot with an atomic publication version and scalar
atomics for every field: identities, origin/closure enums, output/source
origins and rates, mapped tail, closure-engaged and closed-tail-engaged flags,
and the closed rational numerator/denominator. Ring metadata read across
threads is atomic as well. The sole writer uses this protocol for every origin,
tail, and closure update:

1. publish an odd slot version;
2. store the scalar fields;
3. publish the next even version.

Use `std::memory_order_seq_cst` for every version and scalar payload load/store
in the first implementation. A reader loads an even version, loads
every scalar field, then accepts the snapshot only if a second version load sees
the same even value. It reconstructs `ExactPlaybackEpoch` only in caller-owned
scratch storage. Retry a fixed bounded number of times and report
`TemporarilyUnavailable` on failure. Do not weaken the memory orders without a
separate C++ memory-model proof. Do not use a seqlock version around a plain
non-atomic struct; the audio thread's in-place tail update would make that a C++
data race even when a reader later rejects the version.

E-040 proves the game's periodic seek-request cadence is once per three seconds
(and E-041 suppresses the harmless in-margin requests), so 256 epochs retain
far beyond the required 60 seconds without allocating a large ring for every
ordinary sound-effect buffer. A new playback generation claims one slot;
subsequent render spans for that generation monotonically extend that slot's
tail in place. The audio thread is the sole origin/tail/natural-end writer and
readers use bounded coherent reads. Expose oldest/newest exact output coverage
so any exceptional faster-generation eviction is explicit `HistoryLost`. Do
not increase or reinterpret `kRenderSpanCapacity = 32`; existing DirectSound
cursor resolution continues to use the old spans.

After `MixerVoice` destruction has quiesced the node, buffer Release becomes the
next sequential writer only for `WriterQuiescedRelease` and uses the same atomic
slot protocol. It must never overlap the audio writer.

- [ ] **Step 2: Publish the cumulative mixer origin and tail**

In `PublishMappedSpans`, derive the generation origin without a new approximation:

```text
cumulative_begin = voice.epoch_output_frames + output_offset
global_begin     = output_begin + output_offset
O0               = global_begin - cumulative_begin
S0               = voice.epoch_source_start
```

Carry `ExactPlaybackOrigin::Play` through the existing play mailbox and
`ExactPlaybackOrigin::Seek` through the existing seek mailbox. The first
successfully represented span for a generation publishes
`(buffer ID,endpoint generation,playback generation,origin,O0,S0,Fo,Fs)`;
later spans extend its `mapped_output_tail`. The first span of a later
generation closes the prior epoch's output coverage at that exact global output
boundary. A changed identity/origin/rate inside one generation invalidates the
history rather than silently replacing it. `Stop` stops future extension but
does not guess a cross-thread closing tail. A later epoch closes the prior
generation at its stable mapped tail. Natural drain is observed by the
audio-thread writer and publishes the exact source-length coordinate in
`closed_source_tail` at its final output tail; do not derive that terminal
source value by rounding a resampler ratio.

For buffer Release, replace the default destructor with an explicit order:
destroy/reset `MixerVoice` first, require that the existing miniaudio node
destruction has quiesced the render writer, then call
`AudioCursorTimeline::CloseExactWriterAfterQuiescence()` to close the last
stable mapped tail with `WriterQuiescedRelease`. This call is forbidden while
the voice still exists. It transfers sole-writer ownership only after
quiescence, so it adds no audio-thread lock. No closure publishes a native
stage-end event.

Record the pinned miniaudio proof beside that call: `ma_node_uninit` first
performs full detach, and its source states that detach waits for local node
processing to finish before uninitialization continues. Recheck the resolved
dependency source during implementation; this is the writer-quiescence
authority, not a timing delay or test fixture.

For an enabled exact candidate, check every origin/tail/closure publication
once. Failure enters the existing audio runtime-fatal path immediately; the
renderer never continues while omitting exact history or retries with the
legacy 32-span result.

- [ ] **Step 3: Resolve source position exactly**

`ResolveExactSourceFrame` accepts both `Play` and `Seek` epochs, requires
`output >= O0` and `output < mapped_output_tail`, and uses checked rational
subtraction/multiplication without floor-rounding the source coordinate. An
output earlier than the current generation's first span but still covered by a
retained prior epoch resolves through that prior epoch. A current generation
with no span yet is `Pending`; output older than retained coverage is
`HistoryLost`; overlapping epochs that disagree are `Discontinuous`.

Return `OutsidePlayback` only when complete retained history proves that
`output` is before the first-ever epoch origin, in a gap bounded by two retained
origins, or at/after an audio-thread natural-end tail. Return `Pending` when an
unclosed current epoch may still extend over `output`, including after a
control-thread Stop with no later bounding origin or writer-quiesced Release.
Track whether the
retained prefix has ever been evicted so an output before the oldest retained
epoch becomes `HistoryLost`, never a false `OutsidePlayback`.

For an outside coordinate after at least one exactly closed epoch, also return
that immediately preceding epoch's exact source-frame tail/rate as
`closed_frontier`. Historical event resolution uses only the outside
status and remains baseline-only. Task 8 may use the frontier only as a fixed
current-ready catch-up limit; it must never turn the outside input itself into
an event or advance beyond the closed tail.

- [ ] **Step 4: Give every secondary buffer a non-reusable identity**

Add `buffer_instance_id_` initialized from a process-wide monotonic atomic in
`SecondarySoundBuffer::Create`. Zero and wrap take the always-on fatal path.
The ID identifies one lifetime-safe audio history only; it is not a stage
identity. Do not use `this` pointer as identity. In enabled exact mode, change
`NextPlaybackGeneration` exhaustion from its legacy wrap-to-one behavior to the
same fatal identity rule; no retained epoch may reuse a generation.

- [ ] **Step 5: Preserve the native getter's existing channel authority**

Extend `GameplayAudioCursorState` with `Pending`. Extend
`GameplayAudioCursorObservation` with `buffer_instance_id`, endpoint
generation, current origin, and a
`std::shared_ptr<AudioCursorTimeline> exact_history`. Keep
`ScopedGameplayAudioCursorQuery::Consume()` and its current overwrite semantics
unchanged: it continues to return the observation associated with the native
group getter's chosen/returned cursor. Do not enumerate every active group-2
voice and do not add `ConsumeExactBinding()` or a second voice-selection policy.

Record the binary proof beside the code: native VA `0x6122B0` iterates the
group's ordered channel list, calls the cursor method with one output slot, and
breaks on the first successful call (`0x61233F..0x612368`). Thus only the
successful returned channel publishes the authoritative scoped observation.
Keep the native integer result only as a sign gate: negative means
`NoPlayback`; nonnegative permits the exact observation. Never use its rounded
millisecond magnitude in the new resolver.

- [ ] **Step 6: Publish only active/draining exact candidates**

In `ResolveCurrentSourceFrameLocked`, retain all existing legacy return values.
For every scoped native-cursor path, publish the exact observation independently
of whether the legacy current output/frame or the 32-span compatibility lookup
succeeds. Every observation includes the instance ID, endpoint generation, and
lifetime-safe history handle. Publish `Pending` when exact history exists but
the current compatibility query cannot yet identify a represented epoch,
`Exact` when current output resolves, and `Inactive` when the voice is neither
mixing nor audibly draining. Both Play and Seek generations can publish
`Exact`. Existing framerate code remains source-unchanged and may continue its
own rounded compatibility behavior; the new judgement resolver never uses that
rounded result.

- [ ] **Step 7: Review binding cases**

Establish from the source diff:

- a negative native group-2 result yields `NoPlayback` and requires no exact
  observation;
- a nonnegative native group-2 result requires one exact observation with a
  non-null history handle and the active endpoint generation, otherwise the
  enabled active-stage path is fatal;
- Pending withholds recognition without starting a timer;
- Play and Seek generations resolve through the same retained audio history and
  neither changes native stage generation;
- the two native stage-BGM channels are not treated as an ambiguity merely
  because they exist;
- Stop/drain/Release never closes native stage; only a later epoch,
  audio-thread natural end, or Release after writer quiescence proves a closed
  playback tail; and
- `HookGameplaySongClock` still calls legacy `Consume()` without source change.

- [ ] **Step 8: Build**

```powershell
cmake --build --preset msvc32-debug --target gc_audio iDmacDrv32
```

Expected evidence: the new publication and backward-compatible query API compile/link.

- [ ] **Step 9: Commit**

```powershell
git add -- src/Audio/Mixer/AudioCursorTimeline.h src/Audio/Mixer/AudioCursorTimeline.cpp src/Audio/Mixer/MiniaudioMixer.cpp src/Audio/Wasapi/ExclusiveAudioEngine.h src/Audio/Wasapi/ExclusiveAudioEngine.cpp src/Audio/DirectSound/GameplayAudioCursorObservation.h src/Audio/DirectSound/GameplayAudioCursorObservation.cpp src/Audio/DirectSound/DirectSoundFacade.h src/Audio/DirectSound/DirectSoundFacade.cpp
git commit -m "Publish exact gameplay playback history"
```

---

### Task 6: Add fail-closed diagnostics before runtime dispatch

**Files:**

- Create: `src/Patches/AbsoluteJudgement/AbsoluteJudgementDiagnostics.h`
- Create: `src/Patches/AbsoluteJudgement/AbsoluteJudgementDiagnostics.cpp`
- Modify: `src/Logging/SessionLog.h`
- Modify: `src/Logging/SessionLog.cpp`
- Modify: `src/Loader/DllMain.cpp`
- Modify: `src/Patches/CMakeLists.txt`

**Interfaces:**

- Produces `AbsoluteJudgementDiagnostics& JudgementDiagnostics() noexcept` with startup/stage/scope/query/clock/transport/score counters, roughly five-second diagnostic summary scheduling, and:

```cpp
[[noreturn]] void FatalActiveStage(
    AbsoluteJudgementFatalReason reason,
    const AbsoluteJudgementFatalSnapshot& snapshot) noexcept;
```

- Produces `void gc::session_log::FlushActiveProcessLog() noexcept`; the
  terminal fatal path has no recovery decision to propagate.

- [ ] **Step 1: Define monotonic counter groups**

Define exact fields from spec section 14 for native stage open/activation/end,
transport, endpoint/playback epochs including Play-versus-Seek counts, schedule, native
execution, five queries, score deltas, late/eviction/error counts, and maximum
backlog/delivery delay. Include `outside_playback_baseline_records` and
`closed_frontier_catchups` counters separate from accepted late records. Use atomics only for fields written across
input/audio/game threads; keep game-thread stage counters plain inside the
diagnostics owner. Include `StorageAllocationFailure` and
`UnexpectedInternalException` in `AbsoluteJudgementFatalReason`; Task 9 uses
those exact reasons at the hook exception boundary.

- [ ] **Step 2: Define lifecycle records and summary cadence**

Add methods `LogStartup`, `LogNativeStageOpen`,
`LogAbsoluteStageActivation`, `MaybeLogFiveSecondSummary`, and
`LogNativeStageEnd`. `Info` emits only those records. The open record contains
the loader generation, native manager, input generation, atomic cutoff/first
eligible sequence, held baseline, and transport-fault baseline. The activation
record adds the complete native state identity, endpoint generation, every
observed authoritative buffer-history ID and its Play/Seek epoch counts, exact
origin/rates, initial `J`, committed-boundary seed, offset, safe values, and
accumulated waits. Summaries/end contain every required counter. Hard-code
`rounded_fallback=0` in the schema and never increment a fallback counter. The
roughly five-second check is diagnostics only and never drives lifecycle,
waiting, or failure.

- [ ] **Step 3: Define Verbose scope records**

`LogScopeVerbose` checks the active plog severity before formatting and emits the exact scope fields from spec section 14.2. Do not add a `Trace` level or compile-time logging feature flag.

- [ ] **Step 4: Expose synchronous log flushing**

Add `BoundedSessionFile::Flush()` using `FlushFileBuffers` under its existing mutex and `SessionLogAppender::Flush()`. Register the process appender from `InitProcessLog` in one atomic active-appender pointer, and implement `FlushActiveProcessLog()` as a no-status, best-effort call on that process-lifetime object. It already runs inside a terminal path; do not add retries or nested failure recovery.

- [ ] **Step 5: Implement the one-way fatal path**

`FatalActiveStage` must atomically latch the first reason, set a `recognition_stopped` flag before formatting, write one structured `PLOG_FATAL` snapshot, call `FlushActiveProcessLog`, then call:

```cpp
TerminateProcess(GetCurrentProcess(), 0xA7);
RaiseFailFastException(nullptr, nullptr, 0);
std::abort();
```

The latter calls are unreachable fallbacks. Do not show a message box during an active song. Startup/preflight failures continue to use the existing startup-fatal presentation before gameplay.

- [ ] **Step 6: Add invariant helpers**

Provide checked helpers for:

```text
recognition_calls == score_calls == event_scopes + heartbeat_scopes
strictly increasing committed (time,sequence)
nondecreasing private heartbeat index with no skipped due boundary
nondecreasing native score counters
```

Each helper either records success or enters the shared fatal path; no warning-only invariant exists.

Add the diagnostics source files to `gc_runtime_patches` and link that target
explicitly to `gc_logging` for the flush API.

- [ ] **Step 7: Build**

```powershell
cmake --build --preset msvc32-debug --target iDmacDrv32
```

Expected evidence: diagnostics and flush infrastructure compile/link. There is still no reachable judgement patch.

- [ ] **Step 8: Commit**

```powershell
git add -- src/Patches/AbsoluteJudgement/AbsoluteJudgementDiagnostics.h src/Patches/AbsoluteJudgement/AbsoluteJudgementDiagnostics.cpp src/Logging/SessionLog.h src/Logging/SessionLog.cpp src/Loader/DllMain.cpp src/Patches/CMakeLists.txt
git commit -m "Add absolute judgement diagnostics"
```

---

### Task 7: Implement retained history and the immutable five-query view

**Files:**

- Create: `src/Patches/AbsoluteJudgement/JudgementHistory.h`
- Create: `src/Patches/AbsoluteJudgement/JudgementHistory.cpp`
- Create: `src/Patches/AbsoluteJudgement/JudgementScope.h`
- Create: `src/Patches/AbsoluteJudgement/JudgementScope.cpp`
- Modify: `src/Patches/CMakeLists.txt`

**Interfaces:**

- Produces:

```cpp
struct ResolvedGameplayTransition {
    gc::input::GameplayTransitionRecord transport{};
    gc::timing::CheckedRational judgement_seconds;
};

struct JudgementScopeCoordinate {
    gc::timing::CheckedRational judgement_seconds;
    std::uint64_t sequence{};
};

enum class BaselineOnlyReason : std::uint8_t {
    OutsidePlayback,
    AcceptedLate,
};

enum class JudgementScopeKind : std::uint8_t {
    Event,
    Heartbeat,
};

class JudgementHistory final {
public:
    void Reset(std::uint64_t transport_epoch,
               std::uint64_t cutoff_sequence,
               gc::input::GameplayHeldMask baseline) noexcept;
    std::expected<void, JudgementHistoryError> Append(
        const ResolvedGameplayTransition&) noexcept;
    std::expected<void, JudgementHistoryError> ApplyBaselineOnly(
        const gc::input::GameplayTransitionRecord&,
        BaselineOnlyReason) noexcept;
    // Pure logical queries used only by JudgementScope.
};

class ScopedJudgementQueryView final {
public:
    explicit ScopedJudgementQueryView(
        const JudgementScopeData&) noexcept;
    ~ScopedJudgementQueryView();
};
```

- [ ] **Step 1: Use bounded retained storage**

Store at most 65,536 resolved records in a preallocated ring. `Append` requires
exact nondecreasing `(time,sequence)` order. Both `Append` and
`ApplyBaselineOnly` consume exactly the next transport sequence; this preserves
continuity even though an `OutsidePlayback` or accepted-late record intentionally
creates no resolved event entry. Do not overwrite: capacity exhaustion is
`HistoryLost`. Prune only records older than every pending scope and every
supported relative query. Before removing a prefix, fold it into a compact
causal base containing the ordinary held mask plus each logical control's most
recent false-to-true coordinate/freshness state. This preserves arbitrarily old
holds and exact held age without retaining every old transition. Keep the full
event suffix required by inclusive `4Q` paired lookback; never prune an event
that a current or future scope can still address.

- [ ] **Step 2: Implement ordinary and logical held predicates**

For `k=0..4`, pair `P_k=(k,k+5)`:

```text
Held(0..9)  = ordinary post-state bit
Held(10+k)  = Held(k) OR Held(k+5)
Held(15+k)  = Held(k) AND Held(k+5)
```

State lookup at time `t` uses only the history prefix through the scope sequence. A future relative time carries current state forward but cannot see a later record already drained in the same outer update.

- [ ] **Step 3: Implement current-edge algebra exactly**

Ordinary press/release is true only in the current event's rising/falling mask. A heartbeat has no edge. Composite `10+k` is constituent OR. Paired `15+k` requires at least one current constituent edge and is true when both are current or the other constituent has a same-stage edge in the inclusive preceding `4Q`. Implement release with the same falling-edge algebra established by the E-046 CBooster decompile. Two historical edges never produce a current edge.

- [ ] **Step 4: Implement scope-aware held age**

For a true logical held predicate with most recent false-to-true time `s`:

```text
A_time = 1 + floor((t-s)/Q), Q=1/60 second
return 1 only if the current event itself creates that logical rise
return max(2,A_time) in every later held scope
return 0 when not held
```

Pre-held baseline has no accepted rise coordinate and returns stale age `5`,
the first value outside the native `<=4` direction window. Equal-time later
sequence sees an earlier accepted rise as age 2; a genuine release/repress
record creates a new age 1.

`ApplyBaselineOnly` folds `held_after` into that same causal base and records
whether the reason was `OutsidePlayback` or `AcceptedLate`. A false-to-true
change is marked already-held/never-fresh (age 5) and is not inserted
into the paired-edge suffix; a true-to-false change clears the logical held
state. A later on-time release or repress remains an ordinary current event.
This is the exact baseline rule for pre-playback input and the accepted handoff
miss, not replay.

- [ ] **Step 5: Implement relative frame translation**

For held/direction requests, checked-subtract `requested_frame - scope.native_frame`, then query at `scope_time + delta*Q`. Reject an active-scope pressed/released request for any frame other than `scope.native_frame`; the audit proves ordinary consumers request current and paired history is implemented internally.

Here `Reject` means return the scope layer's `InvariantFailure` disposition so
the owning hook enters the active-stage fatal path; it never fabricates false
or trampolines into the native ring.

- [ ] **Step 6: Reproduce only the audited direction mask primitive**

Use retained held facts to write `(x,y)` exactly as native helper `0x62E290`:

- booster 0: IDs `0/1` are up/down with up priority and write `y=-1/+1`; IDs `2/3` are left/right with left priority and write `x=-1/+1`;
- booster 1: IDs `5/6` are up/down with up priority and write `y=-1/+1`; IDs `7/8` are left/right with left priority and write `x=-1/+1`;
- booster 2: logical composite IDs `10/11` independently add `y=-1/+1`, and IDs `12/13` independently add `x=-1/+1`, so opposing directions cancel.

Use `-1.0F/+1.0F` and initialize both outputs to zero. Buttons `4/9` do not affect direction. Do not add angle or note-type logic.

Also preserve the audited helper's EAX behavior instead of inventing a generic
success flag: after a valid lookup the return is the final horizontal held
predicate (`left` short-circuits `right` for boosters 0/1; booster 2 returns
the ID-13 predicate). This value is normally incidental to the X/Y outputs,
but the inline hook must remain ABI-compatible.

- [ ] **Step 7: Make the active view TLS, immutable, and non-consuming**

The RAII scope stores stage generation, expected CBooster pointer, game-thread ID, exact coordinate, native ms/frame, event masks, history prefix, and diagnostic accumulator. All query functions return an `Inactive` disposition when no scope exists, an `Answered` value on exact identity match, or `InvariantFailure` on active thread/receiver mismatch. Repeated calls never mutate edge/history truth.

- [ ] **Step 8: Review against the completed native audit**

Read, without regenerating, E-045/E-046 and the retained `audit-input-helper-decompile-2026-08-17.txt`. Check each algebra branch against the recorded CBooster code and record the artifact paths in source comments. Do not create expected-value fixtures from this implementation.

In `src/Patches/CMakeLists.txt`, link the runtime-patch target explicitly to
`gc_input` and `gc_timing`; do not duplicate the journal or rational types in
the patch target.

- [ ] **Step 9: Build**

```powershell
cmake --build --preset msvc32-debug --target gc_runtime_patches
```

Expected evidence: bounded history and query scope compile. Gameplay correctness remains unclaimed.

- [ ] **Step 10: Commit**

```powershell
git add -- src/Patches/AbsoluteJudgement/JudgementHistory.h src/Patches/AbsoluteJudgement/JudgementHistory.cpp src/Patches/AbsoluteJudgement/JudgementScope.h src/Patches/AbsoluteJudgement/JudgementScope.cpp src/Patches/CMakeLists.txt
git commit -m "Add scoped absolute input history"
```

---

### Task 8: Implement exact stage binding and private scheduling

**Files:**

- Create: `src/Patches/AbsoluteJudgement/JudgementClockResolver.h`
- Create: `src/Patches/AbsoluteJudgement/JudgementClockResolver.cpp`
- Create: `src/Patches/AbsoluteJudgement/JudgementStage.h`
- Create: `src/Patches/AbsoluteJudgement/JudgementStage.cpp`
- Create: `src/Patches/AbsoluteJudgement/JudgementScheduler.h`
- Create: `src/Patches/AbsoluteJudgement/JudgementScheduler.cpp`
- Modify: `src/Patches/CMakeLists.txt`

**Interfaces:**

- Produces:

```cpp
struct NativeJudgementIdentity {
    std::uint64_t stage_generation{};
    std::uintptr_t tune_manager{};
    std::uintptr_t tune{};
    std::uintptr_t judgement_state{};
    std::uintptr_t score_state{};
    std::uintptr_t booster{};
    std::uint32_t player{};
    std::int32_t game_time_offset_ms{};
    std::int32_t hold_safe_frame{};
    std::int32_t slide_hold_safe_frame{};
};

struct ObservedPlaybackHistory {
    std::uint64_t buffer_instance_id{};
    std::uint64_t endpoint_generation{};
    std::uint64_t last_validated_publication{};
    std::shared_ptr<gc::audio::AudioCursorTimeline> history;
};

struct JudgementClockBinding {
    std::uint64_t endpoint_generation{};
    std::shared_ptr<const gc::audio::ExactWasapiClock> endpoint;
    std::vector<ObservedPlaybackHistory> observed_stage_bgm_histories;
};

struct AbsoluteJudgementOuterProbe {
    NativeJudgementIdentity native{};
    bool group2_playing{};
    std::optional<GameplayAudioCursorObservation> group2_observation;
    std::shared_ptr<const gc::audio::ExactWasapiClock> endpoint;
    std::int64_t now_qpc{};
};

struct ScheduledJudgementScope {
    JudgementScopeKind kind{};
    JudgementScopeCoordinate coordinate{};
    std::int32_t native_ms{};
    std::int32_t native_frame{};
    const ResolvedGameplayTransition* event{};
    bool commits_boundary{};
};

class JudgementScheduler final {
public:
    void BeginNativeStage(std::uintptr_t tune_manager) noexcept;
    void EndNativeStage(std::uintptr_t tune_manager) noexcept;
    [[nodiscard]] bool NativeStageOpen() const noexcept;
    void PrepareOuterCall(const AbsoluteJudgementOuterProbe&);
    std::optional<ScheduledJudgementScope> NextScope() noexcept;
    void CommitScope(const ScheduledJudgementScope&) noexcept;
    void FinishOuterCall() noexcept;
};
```

`PrepareOuterCall` is intentionally not `noexcept`: observing a new authoritative
buffer can grow `observed_stage_bgm_histories`. It is called only from inside the
loop-hook handler's immediate exception boundary. The other shown scheduler
methods perform no allocation and remain `noexcept`.

- [ ] **Step 1: Implement the exact clock chain**

For an input QPC or current QPC:

```text
endpoint QPC -> exact global output O
O -> bound exact source S(O)
R = S / source_rate seconds
J = R + GameTimeOffset/1000 seconds
```

Require matching endpoint generation and output inside both the submitted tail
and one retained authoritative playback epoch. The current observation may
introduce any newly observed authoritative stage-BGM history handle. Register
it once by process-unique buffer instance ID and retain it through native
cleanup; historical resolution remains valid through every earlier retained
Play/Seek epoch. The audited two-channel native group limits current channel
choice, not buffer lifetimes over the stage, so there is no two-history cap.
If multiple retained histories cover the same endpoint output, map all of them
to exact source seconds: agreement is one coordinate, disagreement is
`Discontinuous`. Return every explicit provider status without substitution;
never floor source frames or use the legacy group-cursor millisecond return.

Use the spec's explicit historical-status precedence: any
`Discontinuous`/`HistoryLost` fails, any `Pending` blocks, otherwise
all `Resolved` values must agree and win over proven `OutsidePlayback`; all
outside yields `OutsidePlayback`. Current ready time uses only the exact history
selected by that outer call's native group getter. When a newly observed
history or epoch publication appears, compare its affine source-time mapping
over every overlapping global-output interval with all other stage-retained
histories. Equal slope and value at one overlap point prove agreement across
that interval; any difference is `Discontinuous`. Track the last validated
publication per history and repeat before using newer epochs. This validates
the mappings themselves even after individual committed events are pruned;
late discovery of disagreement is still fatal.

Use two scheduler-owned `std::array<ExactPlaybackEpoch, 256>` scratch buffers
and `CopyExactPlaybackEpochs` for nested comparisons; reuse them for every
history pair. A temporarily incoherent snapshot freezes the outer call. Any
exact-epoch prefix eviction during an open stage is `HistoryLost`. Do not cache
raw ring pointers or allocate per comparison.

- [ ] **Step 2: Derive both native arguments from `J`**

```text
native_ms    = trunc_toward_zero(J * 1000)
native_frame = floor(J / (1/60)) = floor(J * 60)
```

Require each result to fit signed 32-bit. Distinct exact/sequence scopes stay distinct even if both integers match.

- [ ] **Step 3: Implement the explicit native stage boundary**

`BeginNativeStage(tune_manager)` is called only after the original native
initializer at RVA `0x2629A0` returns true. It increments a process-local stage
generation and resets every prior stage field. Call
`CaptureGameplayTransitionCutoff()` exactly once. That single journal-locked
operation returns the transport epoch, `first_stage_sequence`, held baseline,
QPC frequency, and transport eviction/fault count, then discards the pre-stage
queued prefix. Store the returned fault count as the stage baseline; only a
later change is stage-local loss. Failure means the required enabled input
transport is absent and enters the active-stage fatal path immediately, with no
retry or alternate snapshot. Do not pair a separately read
`ReadPublishedInput()` value with the cutoff.

Record the audited caller proof beside the hook: the state machine invokes RVA
`0x2629A0` only in state `5`; false stays in state `5`, and true advances to
state `6`. The hook therefore observes one successful native transition per
stage rather than inferring lifecycle from time or audio.

While the stage is open but the first exact BGM epoch is unavailable, drain
post-cutoff records into a separate preallocated unresolved-record queue in
strict sequence; do not put raw QPC records into `JudgementHistory`. Once exact
history exists, project the unresolved prefix to endpoint output first. Apply
only records proven `OutsidePlayback` to the held baseline, with no edge,
paired companion, freshness, or scope; keep a `Pending` predecessor retained
because later publication may cover it. Once the first authoritative epoch
origin and immutable endpoint/native/input binding are exact, let `J_begin` be
that epoch's origin coordinate and initialize committed boundary
`c = ceil(J_begin/Q)-1`. Current output may still be before the origin;
activation itself emits no scope. This emits a boundary
exactly at the origin but never invents one before a non-boundary origin. The
seeded baseline has no edge and stale held age 5. No stock recognition may
run between successful native stage begin and this activation.

`EndNativeStage(tune_manager)` is called at entry to native cleanup RVA
`0x262080`. It emits the final summary and clears every stage-owned field.
Cleanup with no successful begin is an idempotent no-op. A later successful
begin always creates a new stage generation even if all addresses repeat.

- [ ] **Step 4: Validate the complete immutable stage identity**

Activation requires an open loader stage generation, the same
Tune-manager/Tune/judgement/score/booster/player identities, the same
endpoint/input generations, the same positive cutoff/endpoint QPC frequency,
unchanged `GameTimeOffset`, and both live safe values exactly zero. Apply this
outer-probe truth table without timeout:

- negative group-2 sign (`group2_playing=false`) is `NoPlayback`; it introduces
  no new history, but the scheduler still checks the last native-selected
  retained history for a newly proven closed frontier and may finish bounded
  catch-up through that exact tail;
- nonnegative sign requires one consumed observation with non-null exact
  history and the current endpoint generation; absence/mismatch is fatal;
- `Pending` exact voice coverage yields no scopes; it may complete activation
  only when an exact first epoch origin already exists;
- current endpoint output proven `OutsidePlayback` uses its exact
  `closed_frontier`, if present, as a fixed catch-up horizon and then yields no
  more scopes until the same coordinate chain enters retained playback
  coverage; before the first origin it has no frontier and yields none, but an
  already published exact first origin may still complete stage activation;
- `TemporarilyUnavailable` freezes work with retained state;
- `Resolved` permits ready-time scheduling and completes activation once the
  first exact origin exists; and
- `HistoryLost` or `Discontinuous` is fatal once the native stage is open.

A Play or Seek generation, stop/drain, or the native getter choosing the other
audited BGM channel does not alter stage generation. On each nonnegative
observation, register a newly seen lifetime-safe history by buffer instance ID
on the game thread and retain it until native cleanup. The vector may grow
there; allow allocation failure to leave `PrepareOuterCall` and let the
immediate `noexcept` hook boundary enter `FatalActiveStage`. Do not catch it and
continue, return a reduced binding, impose a two-history cap, or mark
`PrepareOuterCall` `noexcept`. There is no two-history lifetime cap.
Resolve only through exact output coverage; overlapping histories must produce
the same exact source-seconds coordinate or the result is `Discontinuous`.

These bullets govern only the current delivery horizon. Every outer call still
drains and classifies post-cutoff transport in sequence using retained endpoint
and voice history, so input inside prior exact coverage is not stranded merely
because group 2 has since become `NoPlayback`.

- [ ] **Step 5: Drain and resolve transport in sequence**

Drain fixed 1024-record batches until the transport queue is empty, verify the
captured epoch, consecutive sequence from `first_stage_sequence`, and unchanged
stage-local eviction/fault count, and append raw records to the separate
preallocated unresolved queue. Resolve only its prefix in sequence. Move a
record into `JudgementHistory` only after its QPC maps `Resolved`; keep a
`Pending` predecessor in the raw queue and do not resolve/deliver later records
around it. Apply a proven `OutsidePlayback` prefix record in sequence to the
held baseline only, increment `outside_playback_baseline_records`, and expose no
edge or scope. Capacity exhaustion in either queue is fatal rather than a drop.

For each resolved record, require its exact coordinate to be nondecreasing
against every earlier-sequence retained record. Equal exact time is ordered by
transport sequence. A backward seek is acceptable only if this event ordering
and the committed delivery frontier both remain valid.

If a newly resolved `(time,sequence)` sorts behind the committed delivery
frontier under the rule below, call
`ApplyBaselineOnly(..., AcceptedLate)`, increment `late_records`, and expose no
scope/edge/freshness. A proven outside-playback prefix uses
`ApplyBaselineOnly(..., OutsidePlayback)`. Unknown sequence loss or eviction is
fatal.

Represent the frontier as exact time, last event sequence at that time, and a
`boundary_committed` flag. An ordinary event permits a higher later sequence
at the same exact time. A committed heartbeat or boundary event sorts after
all possible sequences at that time, so a subsequently visible equal-time
record takes the accepted late-baseline-only path instead of running after its
boundary.

- [ ] **Step 6: Compute the non-accumulating delivery horizon**

Let signed `c` be last committed heartbeat and `target=floor(ready/Q)`:

```text
if target-c > 3: horizon = (c+3)Q
else:            horizon = exact ready
```

Compute every boundary directly as `n/60`; never add the previous boundary or use target FPS. Keep both committed `(time,sequence)` delivery frontier and committed heartbeat index.

- [ ] **Step 7: Stream-merge events and heartbeats**

`NextScope` performs a no-allocation merge of sorted retained events and derived boundaries through the horizon:

- event before boundary: Event;
- boundary before event: Heartbeat;
- one or more events exactly at boundary: each remains an Event, the last has `commits_boundary=true`, and there is no extra heartbeat.

Do not cap event count and do not split an equal-time group. Events beyond the horizon remain pending. `CommitScope` advances state only after both native calls return.

- [ ] **Step 8: Handle unavailability and end state**

`TemporarilyUnavailable` freezes horizon and retains work with zero scopes.
An exact closed frontier remains a fixed ready coordinate across as many
at-most-three-boundary outer calls as are needed; after catch-up it produces
zero scopes and never follows the endpoint beyond the tail. Render lag or a
forward seek with advancing exact time performs bounded
catch-up. Play/Seek generation changes resolve through the multi-epoch history.
A backward seek is accepted only when its ready coordinate remains at/after the
committed frontier and every later-sequence resolved event remains at/after all
earlier-sequence retained events. If either ordering would reverse, or exact
coverage is lost/conflicting, return `Discontinuous`/`HistoryLost` and take the
active fatal path because already-issued native work and physical order cannot
both be undone.

Audio inactivity does not end the stage. Only `EndNativeStage` from native
cleanup emits the end summary and clears pending work, history, cutoff,
bindings, and private indices. No elapsed time, inactivity counter, timeout,
render count, audio pointer, or playback generation participates in that
decision.

- [ ] **Step 9: Review the worked arithmetic paths**

By hand and in code review, trace the spec's `10,005.400 ms` event and 200-ms hitch examples through the formulas. This is a review calculation, not an executable expected-value fixture. Verify no function signature accepts `target_fps` or render frame as an input to time/history policy.

- [ ] **Step 10: Build**

```powershell
cmake --build --preset msvc32-debug --target gc_runtime_patches
```

Expected evidence: the exact resolver/stage/scheduler compile. The scheduler is still not called by the game.

- [ ] **Step 11: Commit**

```powershell
git add -- src/Patches/AbsoluteJudgement/JudgementClockResolver.h src/Patches/AbsoluteJudgement/JudgementClockResolver.cpp src/Patches/AbsoluteJudgement/JudgementStage.h src/Patches/AbsoluteJudgement/JudgementStage.cpp src/Patches/AbsoluteJudgement/JudgementScheduler.h src/Patches/AbsoluteJudgement/JudgementScheduler.cpp src/Patches/CMakeLists.txt
git commit -m "Schedule exact judgement scopes"
```

---

### Task 9: Install the preflighted eight-site native set and dispatch native work

**Files:**

- Create: `src/Patches/AbsoluteJudgement/NativeJudgementAbi.h`
- Create: `src/Patches/AbsoluteJudgement/AbsoluteJudgementRuntime.h`
- Create: `src/Patches/AbsoluteJudgement/AbsoluteJudgementRuntime.cpp`
- Create: `src/Patches/AbsoluteJudgement/AbsoluteJudgementPatch.h`
- Create: `src/Patches/AbsoluteJudgement/AbsoluteJudgementPatch.cpp`
- Modify: `src/Patches/CMakeLists.txt`
- Modify: `src/Loader/DllMain.cpp`

**Interfaces:**

- Produces `void InitializeAbsoluteJudgementOrFatal() noexcept` and eight hook
  handlers. Disabled mode returns normally; enabled-mode failure does not
  return.
- Consumes all Task 1-8 publications and policy; this is the first task that makes enabled mode reachable.

- [ ] **Step 1: Record the audited native constants and ABIs**

Use these exact RVAs and supported-executable prefixes:

The two lifecycle prefixes below were read from the supported
`H:\gc\game471.exe` `.text` section (RVA-to-file mappings `0x2629A0 ->
0x261DA0` and `0x262080 -> 0x261480`) and agree with the audited IDB function
starts. Recheck all eight against the loaded executable before mutation.

| Site | RVA | Expected prefix |
|---|---:|---|
| native stage begin | `0x2629A0` | `55 8B EC 6A FF 68 67 AA 67 00 64 A1 00 00 00 00` |
| native stage end | `0x262080` | `55 8B EC 81 EC 98 00 00 00 89 4D 8C E8 8F F1 D9` |
| loop guard | `0x240239` | `0F 8E 91 00 00 00` |
| pressed | `0x22DFB0` | `55 8B EC 83 EC 28 89 4D D8 C6 45 FF 00 8B 4D D8` |
| held | `0x22DF50` | `55 8B EC 83 EC 0C 89 4D F4 C6 45 FF 00 8B 4D F4` |
| released | `0x22DD30` | `55 8B EC 83 EC 28 89 4D D8 C6 45 FF 00 8B 4D D8` |
| direction | `0x22E480` | `55 8B EC 83 EC 08 89 4D F8 8B 45 0C D9 EE D9 18` |
| held age | `0x22DAA0` | `55 8B EC 83 EC 08 89 4D F8 C7 45 FC 00 00 00 00` |

Also record:

```cpp
inline constexpr std::uintptr_t kLoopTailRva = 0x2402D0;
inline constexpr std::uintptr_t kRecognitionRva = 0x1D68E0;
inline constexpr std::uintptr_t kScoreRva = 0x1CF930;
inline constexpr std::uintptr_t kGetInputManagerRva = 0x001040;
inline constexpr std::uintptr_t kGetGlobalRva = 0x0011D0;
inline constexpr std::uintptr_t kGetConfigRva = 0x0011E0;
inline constexpr std::uintptr_t kGetSoundManagerRva = 0x210400;
inline constexpr std::uintptr_t kGetGroupCursorRva = 0x2122B0;
inline constexpr std::ptrdiff_t kTuneStackOffset = -0x32C;
inline constexpr std::size_t kTuneJudgementStatesOffset = 0x254;
inline constexpr std::size_t kTuneScoreStatesOffset = 0x26C;
inline constexpr std::size_t kGlobalPlayerIndexOffset = 0xCB4;
inline constexpr std::size_t kInputManagerBoosterOffset = 4;
inline constexpr std::size_t kGameTimeOffsetOffset = 0x2C;
inline constexpr std::size_t kHoldSafeFrameOffset = 0x64;
inline constexpr std::size_t kSlideHoldSafeFrameOffset = 0x68;
```

Function types:

```cpp
using RecognitionFn = void(__thiscall*)(void*, int, int);
using ScoreFn = void(__thiscall*)(void*, int);
using StageBeginFn = std::uint8_t(__thiscall*)(void*);
using StageEndFn = int(__thiscall*)(void*);
using PressedFn = std::uint8_t(__thiscall*)(void*, int, int);
using HeldFn = std::uint8_t(__thiscall*)(void*, int, int);
using ReleasedFn = std::uint8_t(__thiscall*)(void*, int, int);
using DirectionFn = int(__thiscall*)(void*, int, float*, float*, int);
using HeldAgeFn = int(__thiscall*)(void*, unsigned int);
```

- [ ] **Step 2: Implement preflight-before-mutation installation**

Read and compare every prefix before creating the first hook. Then create one
MidHook for the loop guard and seven InlineHooks for native stage begin/end plus
the five query methods. Disabled startup creates zero hooks. Any enabled-mode
mismatch or creation failure publishes the exact startup-fatal reason and
terminates immediately; do not return `false`, continue with a partial set,
fall back to stock judgement, or build a custom retry/rollback state machine.
Ordinary local RAII ownership is sufficient cleanup for paths that unwind
before the terminal call.

Do not reuse `FrameratePatchTransaction` or mutate the framerate transaction;
this feature owns its own eight descriptors.

- [ ] **Step 3: Implement the two explicit lifecycle shims**

Use `__fastcall` shims with ignored EDX and no stack arguments:

```cpp
std::uint8_t __fastcall HookStageBegin(void* self, void*) noexcept;
int __fastcall HookStageEnd(void* self, void*) noexcept;
```

`HookStageBegin` calls the original first and returns its byte unchanged. A zero
return is the native function's ordinary not-ready result and creates no stage;
it is not an invariant failure. On a nonzero return, call
`BeginNativeStage(reinterpret_cast<std::uintptr_t>(self))` before returning.
A successful begin is authoritative and starts a fresh loader generation even
if an old address is reused.

`HookStageEnd` calls
`EndNativeStage(reinterpret_cast<std::uintptr_t>(self))` at entry, then calls
the original exactly once and returns its integer unchanged. Cleanup with no
successful begin is an idempotent feature no-op. Neither shim examines time,
audio state, pointer novelty, or inactivity.

- [ ] **Step 4: Implement the five ABI-correct query shims**

Use `__fastcall` shims with ignored EDX:

```cpp
std::uint8_t __fastcall HookPressed(void* self, void*, int id, int frame) noexcept;
std::uint8_t __fastcall HookHeld(void* self, void*, int id, int frame) noexcept;
std::uint8_t __fastcall HookReleased(void* self, void*, int id, int frame) noexcept;
int __fastcall HookDirection(
    void* self, void*, int booster, float* x, float* y, int frame) noexcept;
int __fastcall HookHeldAge(void* self, void*, unsigned int id) noexcept;
```

When `JudgementScope` is inactive, each calls its own `InlineHook::unsafe_thiscall` exactly once with unchanged arguments and return/output behavior. When active and identity matches, answer only from the scope and update query diagnostics. An active mismatch enters the fatal path; it never trampolines to a mixed history.

- [ ] **Step 5: Resolve one native stage probe at the loop seam**

At RVA `0x240239`, first query `NativeStageOpen()`. If false, return from the
handler without reading a gameplay identity or changing the hook context. If
true, read Tune from `[EBP-0x32C]`, resolve player through
`GetGlobal()+0xCB4`, read judgement/score vector elements, resolve the input
manager's booster, call the audited config accessor, and read
`GameTimeOffset`, `HoldSafeFrame`, and `SlideHoldSafeFrame`. Guard every
pointer/read with SEH-safe helpers.

Open `ScopedGameplayAudioCursorQuery`, call the group-2 cursor getter once,
use only its sign, then consume the existing `Consume()` observation with its
added exact-history handle. A negative sign sets `group2_playing=false` and
discards any incidental failed observation. A nonnegative sign sets it true and
passes the consumed observation for mandatory validation. Ignore the
nonnegative rounded magnitude for absolute judgement. Acquire the registered
exact WASAPI endpoint and current QPC once; check QPC failure once and take the
fatal path with no alternate clock. Package native identity, group sign,
observation, endpoint handle, and QPC into one
`AbsoluteJudgementOuterProbe` for `PrepareOuterCall`. Probe construction,
`PrepareOuterCall`, and the complete scope loop all remain inside the loop-hook
handler's `try` region described in Step 9.

- [ ] **Step 6: Invoke the native pair under one immutable scope**

For every `NextScope()` result:

1. construct `ScopedJudgementQueryView` with the expected CBooster and scope facts;
2. snapshot score DWORDs at `+120/+124/+128/+132`;
3. call `RecognitionFn(judgement_state, native_ms, native_frame)`;
4. call `ScoreFn(score_state, native_ms)` immediately;
5. snapshot/validate score deltas and diagnostics;
6. destroy the scope; and
7. call `CommitScope` only after both originals return.

Queries remain pure throughout both calls. No descriptor/note routing is added.

- [ ] **Step 7: Skip only the native uniform loop while the explicit stage is open**

If no successful native stage is open, leave the loop-guard context untouched;
the original native code remains responsible outside gameplay-stage ownership,
and the five query hooks are inactive trampolines. This is not an active-stage
fallback and uses no timer or heuristic.

When a native stage is open, after all due scopes (including any fixed
closed-frontier catch-up), or after an explicit zero-scope state such as
pre-origin `NoPlayback`/`OutsidePlayback`, `Pending`, or
`TemporarilyUnavailable`, call `FinishOuterCall()` and set:

```cpp
context.eip = static_cast<std::uint32_t>(
    executable_base + kLoopTailRva);
```

This bypasses the native `m=1..Tune+0x14` loop while running the original once-per-update tail exactly once. Do not write Tune or invoke the native input capture/fill methods.

Thus no stock CBooster judgement can run between the successful native begin
and native cleanup, even before absolute activation. Any stage-open identity or
clock failure is fatal instead of falling through.

- [ ] **Step 8: Wire startup in the game process only**

In `DllMain`, after `AudioPatchInit()` succeeds and before `FrameratePatchInit(...)`, call `InitializeAbsoluteJudgementOrFatal()`. Inside init:

- read the Task 1 setting;
- call `PrepareGameplayTransitionTransport(enabled)` once and enter the
  startup-fatal path immediately if it returns false;
- in disabled mode log `mode=stock sites=0` and warn at target FPS other than 60;
- in enabled mode recheck WASAPI/1000 capability, initialize diagnostics/storage, and install all eight sites;
- log `mode=absolute sites=8` only after commit.

On enabled failure, publish a startup fatal and terminate; control does not return to `DllMain`. Do not initialize in the NESYS process. Leave the later Framerate and Switch init calls and source files unchanged.

Add all AbsoluteJudgement sources to `gc_runtime_patches` and link the target
explicitly to `gc_system_path`; its existing and Task 7 dependencies supply
audio, config, input, timing, logging, and SafetyHook.

- [ ] **Step 9: Make active exceptions fail closed**

All eight handlers are `noexcept`. Catch internal exceptions at their immediate
boundary. Internal helpers that may allocate are deliberately not `noexcept`;
in particular, keep `PrepareOuterCall` non-`noexcept` so a vector allocation
failure reaches the loop-hook catch instead of invoking `std::terminate` first.
The loop-hook handler catches `std::bad_alloc` and enters the active-stage fatal
path with `AbsoluteJudgementFatalReason::StorageAllocationFailure`; its final
catch-all enters the same path with
`AbsoluteJudgementFatalReason::UnexpectedInternalException`. Neither catch returns.
Do not catch an allocation failure inside the scheduler and continue with a
partial binding.

During process installation, before any successful native stage begin, a
creation/preflight problem is startup fatal. From successful native begin
through matching cleanup—including the no-scope interval before absolute
activation—arithmetic, provider, sequence, clock-authority, native-identity, or
native-call invariant failure routes to `FatalActiveStage`; no handler returns
a fabricated input or silently resumes stock recognition. An ordinary zero
stage-begin result, an explicit no-scope provider status, and cleanup without an
open stage retain their nonfatal meanings.

- [ ] **Step 10: Build and inspect x86 cleanup**

```powershell
cmake --build --preset msvc32-debug --target iDmacDrv32
```

Use the x86 MSVC `dumpbin /disasm` on the Release object/DLL after Task 10 and
verify shim stack cleanup matches native stack arguments: stage begin/end have
no stack-argument cleanup, pressed/held/released `ret 8`, direction `ret 10h`,
held age `ret 4`. Record the exact object symbols inspected in the
implementation handoff.

- [ ] **Step 11: Commit**

```powershell
git add -- src/Patches/AbsoluteJudgement/NativeJudgementAbi.h src/Patches/AbsoluteJudgement/AbsoluteJudgementRuntime.h src/Patches/AbsoluteJudgement/AbsoluteJudgementRuntime.cpp src/Patches/AbsoluteJudgement/AbsoluteJudgementPatch.h src/Patches/AbsoluteJudgement/AbsoluteJudgementPatch.cpp src/Patches/CMakeLists.txt src/Loader/DllMain.cpp
git commit -m "Drive native judgement from absolute time"
```

---

### Task 10: Perform the static ownership and full-build gate

**Files:**

- Review: every file changed since `0484300`
- Do not create tests or gameplay fixtures
- Record: implementation handoff/checkpoint in the task execution notes, not a new design variant

- [ ] **Step 1: Verify the worktree and formatting**

```powershell
git status --short
git diff --check 0484300..HEAD
```

Expected: no unintended unstaged files and no whitespace errors.

- [ ] **Step 2: Prove the framerate ownership boundary statically**

```powershell
git diff --exit-code 0484300..HEAD -- src/Patches/Framerate
```

Expected: empty diff and exit code 0. Also inspect `git diff 0484300..HEAD` and confirm no new reference installs RVA `0x23FA0C`, writes Tune offsets `0x10/0x14`, or changes `GameplaySongClock::Create`.

- [ ] **Step 3: Prove lifecycle has only the two native authorities**

Trace the lifecycle source and hook descriptors:

```text
native 0x6629A0 returns 0 -> no loader transition
native 0x6629A0 returns nonzero -> BeginNativeStage -> new generation/reset
native 0x662080 entry -> EndNativeStage -> clear -> original cleanup
```

Confirm that no Play/Seek/Stop/drain callback, playback generation, address
comparison, elapsed duration, inactivity counter, render count, or provider
status can call either lifecycle transition.

- [ ] **Step 4: Review the exact-time dependency graph**

Trace one event in source:

```text
GameplayTransitionRecord.qpc_ticks
 -> ExactWasapiClock::ResolveQpc
 -> AudioCursorTimeline::ResolveExactSourceFrame
 -> JudgementClockResolver J
 -> native_ms/native_frame
 -> ScheduledJudgementScope
 -> RecognitionFn and ScoreFn
```

Confirm target FPS and outer/render frame do not enter any arrow. Confirm all non-Resolved provider states are explicit and no legacy rounded cursor is called by the resolver.

- [ ] **Step 5: Review the input-policy dependency graph**

Trace before/after/rise/fall masks from publication through retained history and all five hooks. Confirm no code pairs a historical QPC with `ReadPublishedInput()` at drain time, no generic edge pulse exists, and query truth is non-consuming across the recognition/score pair.

- [ ] **Step 6: Review allocation/locking boundaries**

Confirm:

- WASAPI successful render publication performs no allocation, lock, wait, or logging;
- the active-provider registry mutex is used only for register/acquire/
  generation-matched unregister and never by `RenderLoop` or
  `ExactWasapiClock::Publish`;
- exact playback ring slots contain only scalar atomic shared fields plus their
  atomic publication versions; public `ExactPlaybackEpoch` values are
  reconstructed in reader-owned storage;
- input publication uses only its bounded mutex/ring and no allocation, and its
  journal push is the first operation after a changed FastIO exchange, before
  the existing Debug log;
- game-thread unresolved/resolved history and scope-scheduler storage is
  preallocated before activation;
- the authoritative-history vector may grow only on the game thread when a new
  buffer instance is first observed, never inside scope delivery; allocation
  failure reaches the immediate `noexcept` hook catch through a non-`noexcept`
  internal helper and is fatal rather than a two-history cap or fallback;
- no per-scope Info logging occurs; and
- provider handles make engine/timeline lifetime explicit.

- [ ] **Step 7: Configure and build complete Debug and Release graphs**

From the x86 MSVC developer environment:

```powershell
cmake --preset msvc32-debug
cmake --build --preset msvc32-debug
cmake --preset msvc32-release
cmake --build --preset msvc32-release
```

Expected evidence: both complete preset graphs compile/link. Do not run CTest.

- [ ] **Step 8: Inspect supported x86 binary shape**

Use `dumpbin /headers` on the Release `iDmacDrv32.dll` to confirm machine `14C
(x86)` and inspect exports against `src/Driver/iDmac/iDmacDrv32.def`. Use
`dumpbin /disasm` for the seven InlineHook shims and record their cleanup sizes
from Task 9. This proves compiled ABI shape only, not correct runtime detouring.

- [ ] **Step 9: Review configuration modes**

Inspect the canonical serialized repo config and source paths:

- false -> transition transport disabled and site count 0;
- true + non-WASAPI -> validation failure;
- true + poll rate not 1000 -> validation failure;
- true + WASAPI/1000 -> eight-site preflight attempted at all target rates including 60.

Do not edit the runtime config for this review.

- [ ] **Step 10: Commit any review corrections atomically**

If the review required source corrections, commit each coherent correction with a message naming the invariant fixed. If no correction was needed, create no empty commit.

- [ ] **Step 11: Stop at the runtime authorization checkpoint**

Report Debug/Release commands and results, ABI inspection, static ownership result, and remaining fact: actual game behavior is untested. Ask for explicit authorization before deployment into `H:\gc`.

---

### Task 11: Run staged native-game acceptance after explicit deployment authorization

**Files/evidence:**

- Runtime only after authorization: `H:\gc\iDmacDrv32.dll`, `H:\gc\config.toml`
- Inspect/preserve: `H:\gc\loader-log.txt`
- Preserve operator notes and chart/type coverage in an authorized runtime evidence location chosen at execution time

**Checkpoint:** Do not begin this task, copy a DLL, or edit runtime configuration until the user explicitly authorizes deployment and game runs.

- [ ] **Step 1: Resolve exact deployment targets and make recoverable backups**

Read the current runtime DLL/config paths, hashes, and relevant `[experimental]`, `[logging]`, and `input_poll_hz` values. Back up only the exact files that will be changed to a timestamped directory under `H:\gc\artifacts\runtime-backups\`; preserve every unrelated operator setting.

- [ ] **Step 2: Deploy the authorized Release DLL and minimum config edits**

Change only:

```toml
input_poll_hz = 1000
[logging]
level = 'Info'
[experimental]
audio_backend = 'wasapi_exclusive'
enable_absolute_time_judgement = true
```

Retain the operator's target FPS and WASAPI buffer setting for each run. Confirm live game `HoldSafeFrame`/`SlideHoldSafeFrame` through the feature's absolute-stage-activation log; do not edit those values through this task.

- [ ] **Step 3: Gate A — 240 FPS with no input**

Run one ordinary full chart without touching controls. Required result:

- `mode=absolute sites=8`, one native-stage-open record, one matching
  absolute-stage-activation record, unique endpoint/input generations, and
  exact playback-epoch coverage;
- heartbeat scopes, recognition calls, score calls, and native MISS deltas advance;
- `recognition == score == scopes` in each summary;
- the song reaches normal result/lifecycle; and
- eviction, sequence error, rounded fallback, fatal invariant, and final backlog are zero.

If any item fails, stop and localize from `journal/scopes/recognition/score/grades`; do not proceed to mechanic coverage.

- [ ] **Step 4: Gate B — 240 FPS with real input**

Run the chart with ordinary timed presses. For at least one physical rise, use Info counters and one `Verbose` diagnostic run to establish:

```text
journal rise -> Resolved event J -> event scope -> pressed=true
-> recognition -> score -> visible non-MISS grade/counter
```

The logged `native_ms` must equal truncation of event `J`, not delivery/outer time. If the chain dies or all reasonable inputs remain MISS, stop.

- [ ] **Step 5: Gate C — two consecutive native stages without restart**

Complete two ordinary charts in the same game process. Require exactly one
successful native-stage-open, one absolute-stage-activation, and one matching
cleanup per chart, strictly
increasing loader stage generation, and zero history/frontier/input-age state
carried from the first stage into the second. Native manager, judgement, score,
buffer, or voice addresses may repeat without affecting the result. Playback
Play/Seek generation counts may change within either chart without creating a
native-stage-open/end record. This is lifecycle acceptance, not an input replay or
same-result oracle.

- [ ] **Step 6: Cover rapid, paired, direction, and long-form mechanics**

With real keyboard/controller input and named charts, record:

- press/release shorter than `Q` and physically achieved multiple transitions inside one render interval;
- multi-bit/same-time ordering;
- composite and paired current/current, current/prior within inclusive `4Q`, and outside-window rejection;
- one-shot flick head with no replay from unrelated transitions;
- slide-hold head/continuation/direction/release;
- hold/dual-hold start/sustain/immediate zero-grace release/result;
- scratch, beat, turn, hidden, free input, and pre-held start; and
- a controlled render hitch whose pending work catches up in original order.

Map the real charts to raw/effective types `0..15` from E-046. Do not mark a type covered from wrapper sharing alone.

- [ ] **Step 7: Run the mandatory FPS matrix**

At 60, 144, 165, and 240 FPS, run a complete real-input chart and retain its log/operator result. Require fresh exact activation, sensible visible judgement/score, nonzero journal/scope/query/native/grade activity, count invariants, song completion, and zero end backlog/fatal loss.

For full-song 144 and 165 runs, require summaries after catch-up to show:

```text
committed_boundary == floor(ready / Q)
pending_boundaries = 0
exact_boundary_phase_error = 0
skipped_boundaries = 0
duplicate_boundaries = 0
```

A late accepted transport miss makes that run diagnostic; repeat it for acceptance rather than hiding the counter.

- [ ] **Step 8: Check feature-off isolation**

Set only `enable_absolute_time_judgement = false`, restart once, and verify `mode=stock sites=0`. At non-60 FPS require the explicit no-guarantee warning. Confirm no enabled-mode stage/scope records appear and the configured audio backend is no longer restricted by this feature.

- [ ] **Step 9: Preserve evidence and state the result honestly**

Preserve one log/operator result for every mandatory FPS, one Verbose rapid/direction/paired run, and one hitch run. Report separately:

1. static/build proof;
2. native-process structural chain proof; and
3. actual observed game behavior.

Only if all three categories pass may the handoff say the patch is sane in play. Do not claim replay equality, 120/360 support, or ASIO support.

- [ ] **Step 10: Commit only source-side runtime corrections**

If runtime evidence finds a source defect, return to the relevant implementation task, make the smallest contract-preserving correction, rebuild both presets, and repeat the failed gate. Never commit runtime DLLs, runtime config, or logs as source unless the user separately chooses an evidence destination inside the repository.

---

## Completion checklist

- [ ] Tasks 1-9 each have a focused source commit.
- [ ] Task 10 Debug and Release full builds pass without CTest.
- [ ] `src/Patches/Framerate/` is unchanged from `0484300`.
- [ ] Exactly eight enabled-mode sites are guarded, fully preflighted before mutation, and installed before operational mode is exposed.
- [ ] Feature-off installs zero sites and leaves audio backends unrestricted.
- [ ] No test suite, replay source, loader note routing, rounded fallback, CBooster-ring materialization, or mid-stage native fallback exists.
- [ ] A changed FastIO exchange pushes its journal record before Debug logging or unrelated work.
- [ ] The provider registry is synchronized outside the render path, and stale generation cleanup cannot clear a newer provider.
- [ ] Exact playback slots use atomic scalar payloads; readers reconstruct ordinary epoch snapshots only after a stable version read.
- [ ] Allocation-capable outer preparation is not `noexcept`, and its immediate `noexcept` hook boundary converts failure to the active-stage fatal path.
- [ ] Runtime deployment was explicitly authorized before Task 11.
- [ ] 240-FPS no-input and real-input gates pass before broad testing.
- [ ] Two consecutive charts in one process show one explicit begin/end pair
  plus one activation per native stage and no cross-stage state.
- [ ] Actual mechanics/types and full-song 60/144/165/240 runs pass.
- [ ] Full-song 144/165 summaries prove zero accumulated boundary drift.
- [ ] Final report keeps build/static, structural runtime, and actual game evidence separate.

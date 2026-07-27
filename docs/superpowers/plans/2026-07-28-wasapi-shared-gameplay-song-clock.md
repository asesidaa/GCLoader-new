# WASAPI Shared Gameplay Song Clock Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the WASAPI gameplay audio-reseek watchdog with an exact, rate-independent shared song clock so external-cap drift advances integral gameplay ticks without rewinding BGM or `_SHOT`.

**Architecture:** `AudioCursorTimeline` preserves both wrapped DirectSound position and its exact unwrapped source frame. A thread-local query scope publishes that exact cursor only while the game asks for sound-group 2. A pure `GameplaySongClock` converts the absolute cursor plus `GameTimeOffset` to an absolute logical tick with checked rational arithmetic, then bounds each update to 50 ms of catch-up. `FrameratePatch` installs the clock at the watchdog callsite and makes the seven event-bearing gameplay consumers range-aware. Native DirectSound and unvalidated targets retain their existing watchdog plans.

**Tech Stack:** C++23, Win32 x86, DirectSound 8 facade, WASAPI exclusive PCM16/48 kHz, miniaudio 0.11.25, SafetyHook, CMake/Ninja presets, CTest, MSVC x86, Python replay analyzer, IDA-backed checked hook contracts.

## Global Constraints

- The approved design is
  `docs/superpowers/specs/2026-07-28-wasapi-shared-gameplay-song-clock-design.md`.
- Execute this plan inline without subagents, as requested by the user.
- Work and commits belong in
  `H:\gc\artifacts\GCLoader\.worktrees\audio-replay-diagnostics-stage-a`.
  `H:\gc` is the runtime/deployment tree.
- Preserve the 48,000 Hz, PCM16 stereo endpoint contract and the existing
  linear 44.1-to-48 kHz resampler. Do not time-stretch, pitch-shift, crossfade,
  duplicate PCM, or alter endpoint negotiation.
- The exact source cursor is the primary clock. The existing nonnegative
  whole-millisecond group result is a non-seeking fallback. An inactive,
  failed, or invalid observation leaves the binary's initialized step of one
  in place for that outer update.
- Shared-clock mode must always skip the call at VA `0x00664DB2`; no failure
  path may invoke `GC120FPS_GameplayAudioSync_CheckAndSeek` as recovery.
- Preserve explicit game-owned load, Play, Stop, restart, preview, and
  stage-transition seeks. Only the normal gameplay watchdog call is replaced.
- Shared-clock activation requires committed WASAPI and a
  `FramerateProfile::gameplay_validated()` target. Native DirectSound and an
  unvalidated target retain their defined pre-existing plan.
- Target 60 is a first-class shared-clock target. It receives the clock root
  and exactly seven audited gameplay consumers, but zero direct timing writes
  and no unrelated transformed-timing hooks.
- Support at least 60, 120, 144, 165, 240, and 360 target FPS. Never derive an
  integer `sample_rate / target_fps` period.
- Gameplay and judgement remain integral. Do not add fractional judgement or
  rendering interpolation in this implementation.
- A step greater than one must preserve every judgement tick and every
  event-bearing authored-60 crossing. A step of zero must not advance those
  consumers.
- Maximum catch-up per outer update is 50 ms converted through the target
  rate, with a minimum of one logical tick.
- Use checked signed 64-bit arithmetic and mathematical floor division for
  all absolute time conversion. Overflow invalidates the observation.
- Preserve transaction preflight and rollback: failure to verify or install
  any selected contract rolls back the complete framerate transaction.
- The current high-volume audio flight recorder remains enabled through the
  corrected diagnostic run. Remove it, its analyzer, its capture output, and
  all shared-clock validation events/counters only after user acceptance.
- Build/static proof and runtime gameplay acceptance are separate. Do not
  claim the issue fixed until the user accepts complete-song runs.
- Adjust target FPS through the existing live in-game control during runtime
  validation. Do not edit `H:\gc\data\system.cfg`.
- Do not modify or commit `H:\gc\data`, `H:\gc\game471.exe`,
  `H:\gc\game471.exe.i64`, runtime DLLs, diagnostic sessions, or deployment
  backups.
- Keep the IDA daemon running. Any later binary verification must connect to
  the existing default Windows daemon and disconnect only the client.
- Use the `msvc32-release` preset through
  `C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat`.

---

## Binary and Runtime Contracts

| Contract | Verified value |
|---|---|
| Expected image base | `0x00400000` |
| Shared-clock callsite | VA `0x00664DB2`, RVA `0x00264DB2` |
| Expected call bytes | `E8 B9 B2 FD FF` |
| Original watchdog target | VA `0x00640070` |
| Preceding Tune load | `mov ecx, [ebp-0x2B4]` at VA `0x00664DAC` |
| Tune current tick | `Tune+0x10` |
| Tune step | `Tune+0x14` |
| Sound-manager singleton | VA `0x00610400`, RVA `0x00210400`, `void* __cdecl()` |
| Group cursor getter | VA `0x006122B0`, RVA `0x002122B0`, `int __thiscall(void*, int)` |
| Gameplay sound group | `2` |
| Config accessor | VA `0x004011E0`, RVA `0x000011E0`, `void* __cdecl()` |
| `GameTimeOffset` | signed milliseconds at config `+0x2C` |
| Effect-manager advance | VA `0x005F08A0`, RVA `0x001F08A0`, `void __thiscall(void*)` |
| Effect-manager callsite | VA `0x00664E2D`, RVA `0x00264E2D` |
| Cadence sites | RVAs `0x0024063B`, `0x002408D7`, `0x00240C9C`, `0x00241213`, `0x0024122F`, `0x00241268` |

The root hook executes before `Tune+0x10` is committed. The six cadence hooks
also execute before commit and inspect `[current_tick, current_tick + step)`.
The effect-manager call executes after commit and counts authored-60
boundaries in `(old_tick, committed_tick]`.

## File and Responsibility Map

| File | Responsibility |
|---|---|
| `docs/reverse-engineering/wasapi-audio-replay-runtime-validation.md` | Confirmed Stage A capture, corrected-build identity, per-rate acceptance, diagnostic cleanup, and final production identity. |
| `src/Audio/Mixer/AudioCursorTimeline.h/.cpp` | Return wrapped DirectSound position and exact unwrapped source position from the same resolution. |
| `tests/Audio/AudioCursorTimelineTests.cpp` | Non-loop, loop-wrap, pending-generation, and unmapped resolution proof. |
| `src/Audio/DirectSound/GameplayAudioCursorObservation.h/.cpp` | Thread-local scoped query serial and exact/inactive cursor publication. |
| `src/Audio/DirectSound/DirectSoundFacade.cpp` | Publish exact cursor metadata from the active/draining secondary buffer without changing DirectSound results. |
| `src/Audio/CMakeLists.txt` | Compile the permanent cursor observation source; later remove only the temporary recorder source. |
| `tests/Audio/GameplayAudioCursorObservationTests.cpp` | Scope ownership, freshness, single consumption, nesting, and thread isolation. |
| `tests/Audio/SecondarySoundBufferTests.cpp` | Facade publication for exact, inactive, pending, unmapped, Play, Seek, and Stop cases. |
| `tests/Audio/CMakeLists.txt` | Register the new observation test; later remove only recorder tests. |
| `src/Patches/Framerate/GameplaySongClock.h/.cpp` | Checked absolute cursor conversion, generation validation, bounded step policy, and authored-60 range helpers. |
| `tests/Patches/Framerate/GameplaySongClockTests.cpp` | Rate matrix, offsets, generations, overflow, correction sequences, and ten-minute no-drift simulations. |
| `src/Patches/CMakeLists.txt` | Compile the shared clock and link `gc_runtime_patches` to `gc_audio`. |
| `tests/Patches/CMakeLists.txt` | Register `GameplaySongClockTests`. |
| `src/Patches/Framerate/FrameratePatchPlan.h/.cpp` | New hook ID/contract and explicit original, legacy-WASAPI, and shared-clock selection modes. |
| `src/Patches/Framerate/FrameratePatchTransaction.h` | Raise checked-hook capacity from 52 to 53. |
| `tests/Patches/Framerate/FrameratePatchPlanTests.cpp` | Exact contract, mode membership/counts, target-60 isolation, and legacy exclusion. |
| `tests/Patches/Framerate/FrameratePatchTransactionTests.cpp` | Capacity and rollback coverage for 53 hooks. |
| `src/Patches/Framerate/FrameratePatch.h/.cpp` | Root ABI hook, mode selection, Tune reads/writes, range-aware consumers, temporary validation events/counters. |
| `tests/Patches/Framerate/FramerateRuntimeTests.cpp` | Runtime binding, observation selection, consumer range semantics, and temporary diagnostic schema. |
| `src/Audio/Diagnostics/AudioFlightRecorder.h/.cpp` | Temporarily serialize shared-clock decisions into the existing Stage A capture; delete after acceptance. |
| `tests/Audio/AudioFlightRecorderTests.cpp` | Temporarily verify shared-clock event persistence; delete after acceptance. |
| `tools/analysis/audio_replay_analyzer.py` | Temporarily summarize shared-clock source/step/error data and detect the prior replay signature; delete after acceptance. |
| `tools/analysis/tests/test_audio_replay_analyzer.py` | Temporary shared-clock report fixtures; delete after acceptance. |

## Plan Selection Matrix

Use this explicit enum in `FrameratePatchPlan.h`:

```cpp
enum class GameplayAudioClockPlan : std::uint8_t {
    OriginalWatchdog,
    WasapiLegacyResync,
    WasapiSharedSongClock,
};
```

| Transformed timing | Audio plan | Selected hooks | Expected count |
|---:|---|---|---:|
| No | `OriginalWatchdog` | `OuterFrame` | 1 |
| No | `WasapiSharedSongClock` | `OuterFrame`, `GameplaySongClock`, effect-manager advance, six cadence hooks | 9 |
| Yes | `OriginalWatchdog` | Existing transformed plan excluding `AudioResyncPolicy` | 51 |
| Yes | `WasapiLegacyResync` | Existing transformed WASAPI plan | 52 |
| Yes | `WasapiSharedSongClock` | All transformed non-legacy-audio hooks plus `GameplaySongClock` | 50 |

`AudioSkipMargin`, `AudioSkipInterval`, and `AudioResyncPolicy` are absent from
every shared-clock plan. At target 60, no direct write or unrelated
transformed hook is selected.

---

### Task 1: Record the Confirmed Stage A Evidence and Planning Baseline

**Files:**

- Modify:
  `docs/reverse-engineering/wasapi-audio-replay-runtime-validation.md`
- Add:
  `docs/superpowers/specs/2026-07-28-wasapi-shared-gameplay-song-clock-design.md`
- Add:
  `docs/superpowers/plans/2026-07-28-wasapi-shared-gameplay-song-clock.md`

**Contract:**

- Replace the stale “capture unexercised” state with the already verified
  causal evidence.
- Keep the confirmed faulty build separate from the future corrected build.
- Preserve the user's auditory verdict verbatim enough to distinguish
  automated waveform evidence from acceptance.

- [ ] **Step 1: Rewrite the Stage A capture section**

Record these exact facts:

```text
Session: H:\gc\audio-diagnostics\20260727-213445
Conclusive duration: 233.95 seconds
Incomplete ranges: none
First discontinuity: capture 104.430 s, source voices 86 and 87
Second discontinuity: capture 160.220 s
Backward source movement: 3,043 frames at 44,100 Hz = 69.002 ms
Submitted-PCM correlation to 69 ms earlier:
  first event approximately 0.9993
  second event approximately 0.9995
BGM plus _SHOT reconstruction correlation: 0.999995864
Listening file:
  H:\gc\tmp\audio-issue-identification\confirmed-capture-20260727-213445\01_first_captured_tight.wav
User verdict: the first listening file is the exact runtime issue
```

State that both long-form voices received the same rewind, so `_SHOT` can make
the repeated transient more audible but did not create an independent
discontinuity.

- [ ] **Step 2: Record the root-cause classification**

Classify the event as:

```text
The game's gameplay-audio watchdog compared an integer nominal gameplay clock
against the endpoint-backed DirectSound cursor, then issued a backward
SetCurrentPosition for sound group 2. The mixer and resampler followed that
request correctly. Every 480 endpoint frames consumed exactly 441 source
frames before the seek, so ordinary 44.1-to-48 kHz resampling was not drifting.
```

Also record that reducing the margin to 10 ms would make smaller but more
frequent rewinds and is not the selected correction.

- [ ] **Step 3: Add empty corrected-run sections**

Append these headings without claiming results:

```markdown
## Shared-clock diagnostic build

Not built yet.

## Shared-clock runtime matrix

Not exercised yet.

## Diagnostic removal and production build

Not started. Removal is gated on user acceptance of the corrected diagnostic
run.
```

- [ ] **Step 4: Validate the documentation diff**

Run:

```powershell
rg -n "20260727-213445|3,043|69.002|0.999995864|exact runtime issue|Not built yet" `
  docs/reverse-engineering/wasapi-audio-replay-runtime-validation.md
git diff --check
```

Expected: every evidence anchor is present and diff check exits zero.

- [ ] **Step 5: Commit the approved baseline**

Run:

```powershell
git add -- `
  docs/superpowers/specs/2026-07-28-wasapi-shared-gameplay-song-clock-design.md `
  docs/superpowers/plans/2026-07-28-wasapi-shared-gameplay-song-clock.md `
  docs/reverse-engineering/wasapi-audio-replay-runtime-validation.md
git commit -m "docs: define WASAPI shared gameplay song clock"
```

---

### Task 2: Preserve the Exact Unwrapped Source Cursor

**Files:**

- Modify: `src/Audio/Mixer/AudioCursorTimeline.h`
- Modify: `src/Audio/Mixer/AudioCursorTimeline.cpp`
- Modify: `tests/Audio/AudioCursorTimelineTests.cpp`

**Contract:**

`AudioCursorTimeline` must return the wrapped frame needed by DirectSound and
the exact unwrapped frame needed by the shared clock from one stable span
resolution:

```cpp
struct AudioCursorResolution {
    AudioCursorResolutionKind kind{};
    std::uint64_t source_frame{};
    std::uint64_t source_frame_unwrapped{};
};
```

- [ ] **Step 1: Add failing unwrapped-position tests**

Extend `AudioCursorTimelineTests.cpp` with assertions that:

1. a non-loop span returns identical wrapped and unwrapped frames;
2. a loop-crossing span from unwrapped source frame 95 through 105 with source
   length 100 returns unwrapped frame 100 and wrapped frame 0 at the crossing;
3. a later loop returns a value greater than the source length while the
   wrapped value remains inside the buffer;
4. `PendingGeneration` and `Unmapped` return zero for both numeric fields; and
5. existing DirectSound byte projection still uses the wrapped field.

- [ ] **Step 2: Run the test and confirm the expected failure**

Run:

```powershell
& $env:ComSpec /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --preset msvc32-release && cmake --build --preset msvc32-release --target AudioCursorTimelineTests'
```

Expected: compilation fails because
`AudioCursorResolution::source_frame_unwrapped` does not exist.

- [ ] **Step 3: Return both cursor domains**

In `AudioCursorTimeline::ResolveSourceFrame`:

1. retain the existing stable-span selection and interpolation;
2. keep the interpolated frame unwrapped until the result object is built;
3. set `source_frame` to `unwrapped % source_length_frames`;
4. set `source_frame_unwrapped` to the exact interpolated value; and
5. keep pending/unmapped behavior unchanged except for zeroing the new field.

Do not infer an epoch from a wrapped DirectSound byte cursor. The render span
is already the authoritative unwrapped timeline.

- [ ] **Step 4: Run focused tests**

Run:

```powershell
& $env:ComSpec /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build --preset msvc32-release --target AudioCursorTimelineTests SecondarySoundBufferTests && ctest --preset msvc32-release -R "^(AudioCursorTimelineTests|SecondarySoundBufferTests)$" --output-on-failure'
```

Expected: both tests pass and existing wrapped DirectSound cursor assertions
remain unchanged.

- [ ] **Step 5: Commit**

Run:

```powershell
git add -- `
  src/Audio/Mixer/AudioCursorTimeline.h `
  src/Audio/Mixer/AudioCursorTimeline.cpp `
  tests/Audio/AudioCursorTimelineTests.cpp
git commit -m "feat: preserve unwrapped audio source cursor"
```

---

### Task 3: Publish Only the Scoped Gameplay Cursor Observation

**Files:**

- Create:
  `src/Audio/DirectSound/GameplayAudioCursorObservation.h`
- Create:
  `src/Audio/DirectSound/GameplayAudioCursorObservation.cpp`
- Modify: `src/Audio/DirectSound/DirectSoundFacade.cpp`
- Modify: `src/Audio/CMakeLists.txt`
- Create:
  `tests/Audio/GameplayAudioCursorObservationTests.cpp`
- Modify: `tests/Audio/SecondarySoundBufferTests.cpp`
- Modify: `tests/Audio/CMakeLists.txt`

**Contract:**

Use this permanent API:

```cpp
namespace gc::audio {

enum class GameplayAudioCursorState : std::uint8_t {
    Exact,
    Inactive,
};

struct GameplayAudioCursorObservation {
    std::uint64_t query_serial{};
    GameplayAudioCursorState state{};
    std::uint64_t source_frame_unwrapped{};
    std::uint32_t source_sample_rate{};
    std::uint64_t playback_generation{};
    std::uint64_t output_frame{};
};

class ScopedGameplayAudioCursorQuery final {
public:
    ScopedGameplayAudioCursorQuery() noexcept;
    ~ScopedGameplayAudioCursorQuery();

    ScopedGameplayAudioCursorQuery(
        const ScopedGameplayAudioCursorQuery&) = delete;
    ScopedGameplayAudioCursorQuery& operator=(
        const ScopedGameplayAudioCursorQuery&) = delete;
    ScopedGameplayAudioCursorQuery(
        ScopedGameplayAudioCursorQuery&&) = delete;
    ScopedGameplayAudioCursorQuery& operator=(
        ScopedGameplayAudioCursorQuery&&) = delete;

    [[nodiscard]] std::optional<GameplayAudioCursorObservation>
    Consume() noexcept;

private:
    std::uint64_t serial_{};
    bool owns_scope_{};
};

void PublishGameplayAudioCursorObservation(
    GameplayAudioCursorObservation observation) noexcept;

} // namespace gc::audio
```

The publisher stamps the active query serial. Callers do not fabricate one.
An unscoped publisher is ignored. A nested query does not take ownership and
cannot consume the outer query's result.

- [ ] **Step 1: Add failing scope tests**

Create `GameplayAudioCursorObservationTests.cpp` covering:

- publication outside a scope is ignored;
- the owning scope consumes one exact publication once;
- the observation includes exact frame, source rate, generation, and output
  frame;
- a stale publication cannot be consumed by a later scope;
- the most recent publication inside the same owned scope is returned;
- a nested scope cannot consume or clear the outer result;
- a publisher on another thread cannot see or overwrite this thread's scope;
  and
- destruction clears an unconsumed result.

Register the target in `tests/Audio/CMakeLists.txt`.

- [ ] **Step 2: Run the test and confirm the expected failure**

Run:

```powershell
& $env:ComSpec /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --preset msvc32-release && cmake --build --preset msvc32-release --target GameplayAudioCursorObservationTests'
```

Expected: configuration or compilation fails because the new source/API does
not exist.

- [ ] **Step 3: Implement the thread-local scope**

Implement one private `thread_local` state containing:

```cpp
struct GameplayCursorQueryState {
    std::uint64_t next_serial{};
    std::uint64_t active_serial{};
    bool active{};
    std::optional<GameplayAudioCursorObservation> publication;
};
```

Construction increments `next_serial` with zero skipped, clears the
publication, and takes ownership only when no scope is active. `Consume`
requires ownership, matching serial, and one fresh publication. Destruction
clears only the owned scope.

Add the new `.cpp` to `src/Audio/CMakeLists.txt`.

- [ ] **Step 4: Add failing facade-publication tests**

Extend `SecondarySoundBufferTests.cpp` to prove:

- a scoped, playing buffer with a resolved timeline publishes `Exact`;
- the exact publication is unwrapped across a loop while the returned
  DirectSound cursor remains wrapped;
- an audible draining buffer remains exact;
- a stopped/inactive buffer publishes `Inactive`;
- missing endpoint position, `PendingGeneration`, and `Unmapped` publish no
  exact result, leaving the successful DirectSound result unchanged;
- accepted `Play` and `SetCurrentPosition` generations appear in subsequent
  exact publications; and
- no query scope changes existing `GetCurrentPosition` HRESULT or byte cursor.

- [ ] **Step 5: Publish from `SecondarySoundBuffer`**

Keep `ResolveCurrentSourceFrameLocked()` returning the wrapped frame for
DirectSound. Add publication at these decision points:

```text
not playing and not draining:
    publish Inactive with source rate and current generation

active/draining plus resolved timeline:
    publish Exact with unwrapped source frame, source rate, generation,
    and CurrentOutputFrame

no endpoint frame, pending generation, or unmapped span:
    publish nothing
```

Do not publish from the primary buffer. Do not introduce a permanent BGM voice
tag or group number into the DirectSound facade; the runtime query scope gives
the publication its group-2 meaning.

- [ ] **Step 6: Run focused audio tests**

Run:

```powershell
& $env:ComSpec /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build --preset msvc32-release --target GameplayAudioCursorObservationTests SecondarySoundBufferTests AudioCursorTimelineTests && ctest --preset msvc32-release -R "^(GameplayAudioCursorObservationTests|SecondarySoundBufferTests|AudioCursorTimelineTests)$" --output-on-failure'
```

Expected: all three tests pass.

- [ ] **Step 7: Commit**

Run:

```powershell
git add -- `
  src/Audio/DirectSound/GameplayAudioCursorObservation.h `
  src/Audio/DirectSound/GameplayAudioCursorObservation.cpp `
  src/Audio/DirectSound/DirectSoundFacade.cpp `
  src/Audio/CMakeLists.txt `
  tests/Audio/GameplayAudioCursorObservationTests.cpp `
  tests/Audio/SecondarySoundBufferTests.cpp `
  tests/Audio/CMakeLists.txt
git commit -m "feat: publish scoped gameplay audio cursor"
```

---

### Task 4: Implement the Checked Rational Gameplay Song Clock

**Files:**

- Create: `src/Patches/Framerate/GameplaySongClock.h`
- Create: `src/Patches/Framerate/GameplaySongClock.cpp`
- Modify: `src/Patches/CMakeLists.txt`
- Create: `tests/Patches/Framerate/GameplaySongClockTests.cpp`
- Modify: `tests/Patches/CMakeLists.txt`

**Contract:**

Use this pure API:

```cpp
namespace gc::framerate {

enum class SongClockObservationKind : std::uint8_t {
    ExactSourceFrame,
    RoundedMilliseconds,
};

struct SongClockObservation {
    SongClockObservationKind kind{};
    std::uint64_t position{};
    std::uint32_t source_sample_rate{};
    std::uint64_t playback_generation{};
};

enum class GameplaySongClockError : std::uint8_t {
    InvalidRate,
    InvalidObservation,
    ArithmeticOverflow,
    DestinationOverflow,
    BackwardsObservation,
};

struct GameplaySongClockDecision {
    std::int64_t desired_tick{};
    std::int64_t delta_ticks{};
    std::uint32_t step{};
    std::uint32_t remaining_backlog{};
    bool new_generation{};
};

class GameplaySongClock final {
public:
    [[nodiscard]] static std::expected<
        GameplaySongClock,
        GameplaySongClockError>
    Create(
        std::uint32_t rate_numerator,
        std::uint32_t rate_denominator,
        std::uint32_t catchup_milliseconds = 50) noexcept;

    [[nodiscard]] std::expected<
        GameplaySongClockDecision,
        GameplaySongClockError>
    Observe(
        std::uint32_t current_tick,
        std::int32_t game_time_offset_ms,
        const SongClockObservation& observation) noexcept;

private:
    std::uint32_t rate_numerator_{};
    std::uint32_t rate_denominator_{};
    std::uint32_t maximum_step_{};
    bool has_exact_generation_{};
    std::uint64_t exact_generation_{};
    std::uint64_t last_exact_source_frame_{};
};

[[nodiscard]] std::expected<std::uint32_t, FramerateProfileError>
CountCrossedAuthored60Ticks(
    const FramerateProfile& profile,
    std::uint32_t current_tick,
    std::uint32_t step) noexcept;

[[nodiscard]] std::expected<bool, FramerateProfileError>
CrossesAuthored60Cadence(
    const FramerateProfile& profile,
    std::uint32_t current_tick,
    std::uint32_t step,
    std::int32_t phase,
    std::uint32_t authored_period) noexcept;

} // namespace gc::framerate
```

- [ ] **Step 1: Add failing construction and conversion tests**

Test:

- zero rate numerator or denominator returns `InvalidRate`;
- the maximum step is
  `max(1, floor(catchup_ms * rate_numerator /
  (1000 * rate_denominator)))`;
- exact observations use:

```text
floor(
  (source_frame * 1000 + offset_ms * source_rate) * rate_numerator
  / (source_rate * 1000 * rate_denominator)
)
```

- rounded observations use:

```text
floor(
  (cursor_ms + offset_ms) * rate_numerator
  / (1000 * rate_denominator)
)
```

- negative adjusted time uses mathematical floor, not C++ truncation;
- 44,100 Hz and 48,000 Hz produce the same tick for the same absolute time;
  and
- PCM block alignment and the 48 kHz endpoint format do not enter the source
  frame calculation.

- [ ] **Step 2: Add failing step and generation tests**

Test:

- negative and zero delta choose step zero;
- positive delta chooses `min(delta, maximum_step)`;
- `remaining_backlog` is the unscheduled positive delta;
- ordinary sequences contain step zero, one, and two as appropriate;
- a large forward jump drains over multiple updates without an audio action;
- a new exact playback generation accepts a lower absolute source frame and
  reports `new_generation`;
- a same-generation lower exact frame returns `BackwardsObservation`;
- rounded observations do not mutate exact-generation monotonic state; and
- a rejected observation leaves the clock's prior generation state unchanged.

- [ ] **Step 3: Add failing authored-range tests**

For profiles 60, 120, 144, 165, 240, and 360, prove:

- `CountCrossedAuthored60Ticks` counts boundaries in
  `(current_tick, current_tick + step]`;
- step zero counts zero;
- target 60 step two counts two;
- target 240 current 7 step one counts one, while current 6 step one counts
  zero;
- `CrossesAuthored60Cadence` scans the half-open pre-commit interval
  `[current_tick, current_tick + step)`;
- step one exactly preserves the prior point-test behavior;
- step two sees an event on its skipped intermediate tick; and
- periods 4, 5, 6, 8, and 16 with signed phase all match a tick-by-tick
  rational oracle.

- [ ] **Step 4: Add failing long-run simulations**

Use integer/rational test oracles, not floating accumulators:

- run ten minutes at targets 60, 120, 144, 165, 240, and 360 with exact
  44.1 kHz observations;
- simulate a 59.94 outer cadence against target 60 and verify approximately 36
  step-two corrections over ten minutes with no ending logical-tick drift;
- simulate 239.703 outer FPS against target 240 and verify an extra logical
  tick approximately every 3.4 seconds;
- simulate a slightly fast outer cadence and verify bounded step-zero
  corrections;
- prove 144 and 165 match the absolute rational oracle even though neither
  uses an integer sample period; and
- assert every step stays within the 50 ms bound.

- [ ] **Step 5: Run the test and confirm the expected failure**

Register `GameplaySongClockTests` in CMake, then run:

```powershell
& $env:ComSpec /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --preset msvc32-release && cmake --build --preset msvc32-release --target GameplaySongClockTests'
```

Expected: compilation fails because the pure clock API does not exist.

- [ ] **Step 6: Implement checked arithmetic**

Implement small private helpers for:

- checked signed addition and multiplication;
- conversion of bounded unsigned operands into signed 64-bit values;
- mathematical floor division for positive denominators; and
- checked conversion of the positive selected step/backlog into
  `std::uint32_t`.

Calculate an absolute desired tick on every observation. Do not accumulate a
fractional delta or elapsed floating-point time.

Validate exact observation monotonicity only after its arithmetic succeeds.
Generation changes clear the previous exact monotonic baseline. Rounded
fallbacks have no generation authority.

- [ ] **Step 7: Implement range helpers**

Implement `CountCrossedAuthored60Ticks` with checked end-tick addition and the
difference between `MapToAuthored60(end)` and
`MapToAuthored60(current_tick)`.

Implement `CrossesAuthored60Cadence` by visiting each target tick in the
bounded half-open interval and delegating each point to
`ShouldRunAuthored60Cadence`. The 50 ms clock bound limits this scan to at most
25 ticks at the current maximum 500 FPS.

- [ ] **Step 8: Run focused policy tests**

Run:

```powershell
& $env:ComSpec /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build --preset msvc32-release --target GameplaySongClockTests FramerateAuthoredClockTests FramerateProfileTests && ctest --preset msvc32-release -R "^(GameplaySongClockTests|FramerateAuthoredClockTests|FramerateProfileTests)$" --output-on-failure'
```

Expected: all three targets pass.

- [ ] **Step 9: Commit**

Run:

```powershell
git add -- `
  src/Patches/Framerate/GameplaySongClock.h `
  src/Patches/Framerate/GameplaySongClock.cpp `
  src/Patches/CMakeLists.txt `
  tests/Patches/Framerate/GameplaySongClockTests.cpp `
  tests/Patches/CMakeLists.txt
git commit -m "feat: add rational gameplay song clock"
```

---

### Task 5: Select the Shared-Clock Hook Family Transactionally

**Files:**

- Modify: `src/Patches/Framerate/FrameratePatchPlan.h`
- Modify: `src/Patches/Framerate/FrameratePatchPlan.cpp`
- Modify: `src/Patches/Framerate/FrameratePatchTransaction.h`
- Modify:
  `tests/Patches/Framerate/FrameratePatchPlanTests.cpp`
- Modify:
  `tests/Patches/Framerate/FrameratePatchTransactionTests.cpp`

**Contract:**

Change the plan builder to:

```cpp
[[nodiscard]] FramerateHookPlan BuildFramerateHookPlan(
    bool transformed_timing,
    GameplayAudioClockPlan audio_clock_plan) noexcept;
```

Add `FramerateHookId::GameplaySongClock` with:

```cpp
FramerateHookContract{
    .id = FramerateHookId::GameplaySongClock,
    .rva = 0x00264DB2,
    .expected = Pattern({0xE8, 0xB9, 0xB2, 0xFD, 0xFF}),
    .name = "gameplay shared song clock",
}
```

Raise `kMaximumFramerateHooks` from 52 to 53.

- [ ] **Step 1: Add failing exact-contract tests**

Update the complete expected-contract array to 53 entries and assert:

- `GameplaySongClock` has RVA `0x00264DB2`;
- its expected bytes are exactly `E8 B9 B2 FD FF`;
- every hook ID is unique;
- every RVA is unique; and
- `OuterFrame` remains the final contract so
  `FramerateHookContracts(false)` retains its current one-contract behavior.

- [ ] **Step 2: Add failing mode-matrix tests**

Assert exact membership and counts from the plan-selection matrix:

```text
native original:       1
native shared:         9
transformed original: 51
transformed legacy:   52
transformed shared:   50
```

For native shared, compare the complete ID set:

```text
OuterFrame
GameplaySongClock
GameplayEffectAdvance
EffectCadence6
EffectCadence5
EffectCadence4
EffectCadence16A
EffectCadence16B
EffectCadence8
```

For transformed shared, assert all three legacy audio IDs are absent. For
legacy mode, assert existing membership remains byte-for-byte equivalent to
the pre-change WASAPI plan.

- [ ] **Step 3: Add failing transaction-capacity tests**

Update the capacity assertion to 53 and prove:

- exactly 53 valid hook operations can commit;
- operation 54 fails with `FramerateInstallStage::Capacity`;
- a mismatch at the new root contract causes no direct write or hook install;
  and
- failure of the final installed hook resets all prior 52 hooks and restores
  every direct write.

- [ ] **Step 4: Run the tests and confirm the expected failure**

Run:

```powershell
& $env:ComSpec /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build --preset msvc32-release --target FrameratePatchPlanTests FrameratePatchTransactionTests'
```

Expected: the old boolean plan API, missing hook ID, and capacity 52 make the
new assertions fail.

- [ ] **Step 5: Implement explicit mode selection**

In `BuildFramerateHookPlan`, classify IDs with named predicates:

```cpp
const bool legacy_audio =
    id == FramerateHookId::AudioSkipMargin ||
    id == FramerateHookId::AudioSkipInterval ||
    id == FramerateHookId::AudioResyncPolicy;

const bool shared_native_consumer =
    id == FramerateHookId::GameplayEffectAdvance ||
    id == FramerateHookId::EffectCadence6 ||
    id == FramerateHookId::EffectCadence5 ||
    id == FramerateHookId::EffectCadence4 ||
    id == FramerateHookId::EffectCadence16A ||
    id == FramerateHookId::EffectCadence16B ||
    id == FramerateHookId::EffectCadence8;
```

Use those predicates to implement the matrix directly. Do not infer shared
mode from transformed timing and do not enable every transformed contract at
target 60.

- [ ] **Step 6: Run focused plan/transaction tests**

Run:

```powershell
& $env:ComSpec /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build --preset msvc32-release --target FrameratePatchPlanTests FrameratePatchTransactionTests && ctest --preset msvc32-release -R "^(FrameratePatchPlanTests|FrameratePatchTransactionTests)$" --output-on-failure'
```

Expected: both targets pass with exact counts 1, 9, 51, 52, and 50.

- [ ] **Step 7: Commit**

Run:

```powershell
git add -- `
  src/Patches/Framerate/FrameratePatchPlan.h `
  src/Patches/Framerate/FrameratePatchPlan.cpp `
  src/Patches/Framerate/FrameratePatchTransaction.h `
  tests/Patches/Framerate/FrameratePatchPlanTests.cpp `
  tests/Patches/Framerate/FrameratePatchTransactionTests.cpp
git commit -m "feat: plan shared song clock hooks"
```

---

### Task 6: Install the Shared-Clock Root Hook

**Files:**

- Modify: `src/Patches/Framerate/FrameratePatch.cpp`
- Modify: `src/Patches/Framerate/FrameratePatch.h`
- Modify: `src/Patches/CMakeLists.txt`
- Modify:
  `tests/Patches/Framerate/FramerateRuntimeTests.cpp`

**Contract:**

The root hook:

1. skips the original five-byte call immediately;
2. opens a cursor scope only around sound-manager singleton plus group-2
   getter;
3. reads current tick, signed `GameTimeOffset`, and exact publication safely;
4. chooses exact, rounded, inactive, or failed input;
5. writes only `Tune+0x14` when the pure decision succeeds; and
6. never changes audio position.

Use these ABI aliases in `FrameratePatch.cpp`:

```cpp
using GetSoundManager = void* (__cdecl*)();
using GetGroupPlayCursorMs = int (__thiscall*)(void*, int);
using GetConfig = void* (__cdecl*)();
using AdvanceGameplayEffect = void (__thiscall*)(void*);
```

- [ ] **Step 1: Add failing runtime-binding and source-selection tests**

Extend `FramerateRuntimeTests.cpp` to assert:

- `FramerateHookHasRuntimeBinding(FramerateHookId::GameplaySongClock)`;
- the global maximum is 53;
- a successful group result plus fresh `Exact` publication selects an exact
  source-frame observation;
- a successful group result with no publication selects rounded milliseconds;
- a negative group result plus `Inactive` publication is classified inactive;
- a negative result without publication is classified failed;
- exact generation and output frame survive selection for diagnostics; and
- inactive/failed/invalid selection preserves initialized step one.

Expose only the small deterministic observation-selection adapter needed by
the tests in `FrameratePatch.h::detail`; game function calls and raw pointers
remain private to `FrameratePatch.cpp`.

- [ ] **Step 2: Run the test and confirm the expected failure**

Run:

```powershell
& $env:ComSpec /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build --preset msvc32-release --target FramerateRuntimeTests'
```

Expected: the root hook has no runtime binding and the selection adapter does
not exist.

- [ ] **Step 3: Add runtime ownership and CMake dependency**

Add to `FramerateRuntimeState`:

```cpp
GameplayAudioClockPlan audio_clock_plan{
    GameplayAudioClockPlan::OriginalWatchdog};
std::optional<GameplaySongClock> gameplay_song_clock;
```

Initialize the mode with:

```cpp
const auto audio_clock_plan =
    !wasapi_audio_committed
        ? GameplayAudioClockPlan::OriginalWatchdog
        : profile.gameplay_validated()
            ? GameplayAudioClockPlan::WasapiSharedSongClock
            : GameplayAudioClockPlan::WasapiLegacyResync;
```

Construct the clock as `target_fps / 1` only in shared mode. Pass the explicit
mode to `BuildFramerateHookPlan`.

Add `GameplaySongClock.cpp` to `gc_runtime_patches` and link
`gc_runtime_patches` publicly to `gc_audio` so the patch can use the permanent
cursor scope API.

- [ ] **Step 4: Add storage, callback, and checked binding**

Add:

- `gameplay_song_clock` to `FramerateHookStorage`;
- `HookGameplaySongClock(safetyhook::Context&)`;
- the `AssignHookCallbacks` case using `InstallMidHook` at RVA `0x00264DB2`;
  and
- its matching reset callback.

All plan contracts must have a runtime binding before installation.

- [ ] **Step 5: Implement the root hook**

At hook entry:

```cpp
context.eip += 5;
```

That unconditional increment is the watchdog-bypass invariant.

Then:

1. Treat `context.ecx` as the Tune pointer established at `0x00664DAC`.
2. Safely read `Tune+0x10`. `Tune+0x14` is already one and remains unchanged
   on fallback.
3. Create `ScopedGameplayAudioCursorQuery`.
4. Call the singleton at RVA `0x00210400`; if non-null, call the group getter
   at RVA `0x002122B0` with group `2`.
5. Consume the scope before any unrelated DirectSound query can occur.
6. Call the config accessor at RVA `0x000011E0` and safely read signed
   `GameTimeOffset` at `+0x2C`.
7. Select exact only when the group getter returned a nonnegative result and
   the matching publication is `Exact`.
8. Otherwise select rounded for a nonnegative getter result, inactive for the
   inactive publication, or failed.
9. Pass exact/rounded input to `GameplaySongClock::Observe`.
10. On success, safely write only the selected step to `Tune+0x14`.
11. On inactive, failed, backwards, or arithmetic-invalid input, preserve step
    one and return with the watchdog still bypassed.
12. Use the existing fatal runtime path for an unreadable Tune/config pointer;
    do not invoke the watchdog.

No branch calls `SetCurrentPosition`, the watchdog function, or a resampler
reset.

- [ ] **Step 6: Run focused runtime tests**

Run:

```powershell
& $env:ComSpec /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build --preset msvc32-release --target FramerateRuntimeTests FrameratePatchPlanTests GameplaySongClockTests GameplayAudioCursorObservationTests && ctest --preset msvc32-release -R "^(FramerateRuntimeTests|FrameratePatchPlanTests|GameplaySongClockTests|GameplayAudioCursorObservationTests)$" --output-on-failure'
```

Expected: all four targets pass and every one of the 53 contracts has a
runtime binding.

- [ ] **Step 7: Commit**

Run:

```powershell
git add -- `
  src/Patches/Framerate/FrameratePatch.cpp `
  src/Patches/Framerate/FrameratePatch.h `
  src/Patches/CMakeLists.txt `
  tests/Patches/Framerate/FramerateRuntimeTests.cpp
git commit -m "feat: drive gameplay from WASAPI song clock"
```

---

### Task 7: Make the Seven Gameplay Consumers Range-Aware and Add Temporary Validation Telemetry

**Files:**

- Modify: `src/Patches/Framerate/FrameratePatch.cpp`
- Modify: `src/Patches/Framerate/FrameratePatch.h`
- Modify:
  `tests/Patches/Framerate/FramerateRuntimeTests.cpp`
- Modify: `src/Audio/Diagnostics/AudioFlightRecorder.h`
- Modify: `src/Audio/Diagnostics/AudioFlightRecorder.cpp`
- Modify: `tests/Audio/AudioFlightRecorderTests.cpp`
- Modify: `tools/analysis/audio_replay_analyzer.py`
- Modify: `tools/analysis/tests/test_audio_replay_analyzer.py`

**Contract:**

- The six cadence branches test the pre-commit interval
  `[current_tick, current_tick + step)`.
- The effect manager advances once for every authored-60 boundary in
  `(old_tick, committed_tick]`.
- Non-shared modes preserve their existing point-test behavior.
- Temporary telemetry distinguishes exact, rounded, inactive, failed, and
  invalid observations and is fully removed in Task 10.

- [ ] **Step 1: Add failing consumer-range tests**

Extend `FramerateRuntimeTests.cpp` with deterministic adapters for the two
consumer decisions and assert:

- shared target 60 step zero runs no cadence/effect update;
- shared target 60 step one preserves one normal event-bearing tick;
- shared target 60 step two sees both authored boundaries and does not lose an
  intermediate cadence event;
- target 144 and 165 intervals match tick-by-tick rational oracles;
- target 240 normal step one advances the effect manager only on each fourth
  target tick;
- a bounded multi-step update invokes the effect manager the exact crossed
  count;
- non-shared mode retains `IsAuthored60FrameBoundary` and
  `ShouldRunAuthored60Cadence` point semantics; and
- the six register destinations and signed phase inputs remain unchanged.

- [ ] **Step 2: Run the runtime test and confirm the expected failure**

Run:

```powershell
& $env:ComSpec /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build --preset msvc32-release --target FramerateRuntimeTests'
```

Expected: current point-only consumer logic fails step-zero/step-two cases.

- [ ] **Step 3: Change the six cadence hooks**

In `ApplyEffectCadence`:

1. read current Tune frame and signed phase exactly as today;
2. in shared mode, also read `Tune+0x14`;
3. call `CrossesAuthored60Cadence`;
4. set the existing test register to zero when any tick in the interval runs;
5. set it to one for an empty/no-event interval; and
6. retain the existing point helper unchanged outside shared mode.

Because the smallest cadence period is four authored ticks and catch-up is
bounded to 50 ms, one outer update cannot contain two events for the same
cadence site.

- [ ] **Step 4: Change the effect-manager hook**

At VA `0x00664E2D`, Tune current has already been committed. In shared mode:

1. safely read committed `Tune+0x10` and `Tune+0x14`;
2. reject underflow when deriving `old_tick = committed_tick - step`;
3. call `CountCrossedAuthored60Ticks(profile, old_tick, step)`;
4. for count zero, skip the five-byte original call;
5. for count one, fall through to the original call;
6. for count greater than one, call the original target at RVA `0x001F08A0`
   with the original `context.ecx` exactly `count - 1` times, then fall through
   for the final call; and
7. preserve the existing point-only implementation outside shared mode.

Do not replay the entire outer gameplay loop. Only this effect-manager
consumer needs repeated calls.

- [ ] **Step 5: Add failing temporary telemetry tests**

Extend the diagnostic schema with:

```cpp
AudioDiagnosticEventKind::GameplaySongClock

enum class GameplaySongClockCursorSource : std::uint8_t {
    Exact,
    Rounded,
    Inactive,
    Failed,
    Invalid,
};
```

Use the existing fixed-size event without allocation:

```text
decision              cursor source enum
flags bit 0           new playback generation
flags bit 1           pure policy rejected the observation
generation            playback generation
output_frame_begin    observed endpoint output frame
source_frame_begin    exact unwrapped source frame
value0                current logical tick
value1                desired tick bit-cast to uint64
value2 high 32 bits   selected step
value2 low 32 bits    remaining backlog
value3                crossed authored-60 effect ticks
```

Tests must round-trip signed desired ticks and every source enum through
JSONL.

- [ ] **Step 6: Publish temporary clock telemetry and counters**

For each shared-clock root observation, publish one fixed event and increment
temporary counters:

```text
exact
rounded
inactive
failed
invalid
step_zero
step_one
step_multi
maximum_absolute_tick_error
maximum_remaining_backlog
```

Include these fields in the existing periodic framerate runtime summary only
while diagnostics remain. The hook may construct and publish a POD event; it
must not allocate, format text, lock, wait, or perform file I/O.

- [ ] **Step 7: Extend the temporary analyzer**

Add a `Shared song clock` section to `report.md` containing:

- count and percentage of exact, rounded, inactive, failed, and invalid
  observations;
- step-zero, step-one, and multi-step counts;
- maximum absolute tick error and backlog;
- generation transitions;
- same-generation backwards-cursor findings;
- watchdog-origin backward seek findings during the song; and
- replay candidates matching the confirmed 33–100 ms signature.

Add synthetic tests with target 60 step-two corrections, target 165 mixed
zero/one/two steps, an invalid backwards exact observation, and an unexpected
69 ms source rewind.

- [ ] **Step 8: Run telemetry, analyzer, and consumer tests**

Run:

```powershell
python -m unittest tools.analysis.tests.test_audio_replay_analyzer -v

& $env:ComSpec /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build --preset msvc32-release --target FramerateRuntimeTests GameplaySongClockTests AudioFlightRecorderTests && ctest --preset msvc32-release -R "^(FramerateRuntimeTests|GameplaySongClockTests|AudioFlightRecorderTests)$" --output-on-failure'
```

Expected: Python tests and all three C++ targets pass.

- [ ] **Step 9: Commit**

Run:

```powershell
git add -- `
  src/Patches/Framerate/FrameratePatch.cpp `
  src/Patches/Framerate/FrameratePatch.h `
  tests/Patches/Framerate/FramerateRuntimeTests.cpp `
  src/Audio/Diagnostics/AudioFlightRecorder.h `
  src/Audio/Diagnostics/AudioFlightRecorder.cpp `
  tests/Audio/AudioFlightRecorderTests.cpp `
  tools/analysis/audio_replay_analyzer.py `
  tools/analysis/tests/test_audio_replay_analyzer.py
git commit -m "test: trace shared song clock corrections"
```

---

### Task 8: Build, Audit, Archive, and Deploy the Corrected Diagnostic DLL

**Files and artifacts:**

- Modify:
  `docs/reverse-engineering/wasapi-audio-replay-runtime-validation.md`
- Verify:
  `build-msvc32-release/dist/iDmacDrv32.dll`
- Archive root:
  `H:\gc\artifacts\runtime-builds\wasapi-shared-clock\diagnostic`
- Runtime destination:
  `H:\gc\iDmacDrv32.dll`
- Rollback root:
  `H:\gc\deploy-backups`

**Contract:**

- The deployment is an x86 DLL built from the recorded commit.
- Candidate, immutable archive, and runtime hashes must match.
- Deployment must refuse to proceed while `game471.exe` is running.
- No configuration or game-data file is changed.

- [ ] **Step 1: Run the full x86 build and test suite**

Run:

```powershell
& $env:ComSpec /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --preset msvc32-release && cmake --build --preset msvc32-release --target iDmacDrv32'

ctest --preset msvc32-release --output-on-failure
python -m unittest tools.analysis.tests.test_audio_replay_analyzer -v
```

Expected: the DLL links, every registered CTest passes, and all analyzer tests
pass.

- [ ] **Step 2: Audit behavior boundaries**

Run:

```powershell
rg -n "SetCurrentPosition|GameplayAudioSync|AudioSkipMargin|AudioSkipInterval|AudioResyncPolicy|GameplaySongClock" `
  src/Patches/Framerate src/Audio

rg -n "sample_rate.*/|/.*target_fps|target_fps.*/" `
  src/Patches/Framerate/GameplaySongClock.cpp

rg -n "ma_resample_algorithm_linear|lpfOrder = 0" `
  src/Audio/Mixer/MiniaudioMixer.cpp

git diff --check
git status --short
```

Expected:

- the shared root has no audio-seek call;
- legacy audio hooks remain only for non-shared plans;
- clock conversion has no rounded integer sample period;
- the linear resampler remains unchanged; and
- only intended files differ.

- [ ] **Step 3: Verify target-60 isolation**

Use the plan tests plus a focused source review:

```powershell
ctest --preset msvc32-release -R "^(FrameratePatchPlanTests|FrameratePatchTransactionTests|FramerateRuntimeTests)$" --output-on-failure
rg -n "WasapiSharedSongClock|shared_native_consumer|BuildFramerateDirectPatchPlan" `
  src/Patches/Framerate/FrameratePatchPlan.cpp `
  src/Patches/Framerate/FrameratePatch.cpp
```

Expected: target 60 shared mode has zero direct writes and exactly nine hooks;
no menu, movie, remote cadence, frame-duration, or operand transform is
selected merely because WASAPI is active.

- [ ] **Step 4: Verify PE identity**

Run:

```powershell
$candidate = (Resolve-Path -LiteralPath 'build-msvc32-release\dist\iDmacDrv32.dll').Path
& $env:ComSpec /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && dumpbin /headers build-msvc32-release\dist\iDmacDrv32.dll | findstr /i "machine x86"'
Get-Item -LiteralPath $candidate | Select-Object FullName,Length,LastWriteTime
Get-FileHash -Algorithm SHA256 -LiteralPath $candidate
git rev-parse HEAD
```

Expected: PE machine is `14C`/x86, and the exact candidate hash and source
commit are printed.

- [ ] **Step 5: Refuse deployment while the game is running**

Run:

```powershell
if (Get-Process -Name game471 -ErrorAction SilentlyContinue) {
    throw 'game471.exe is running; close it before DLL backup/deployment.'
}
```

If this throws, ask the user to close the game. Do not terminate it
automatically.

- [ ] **Step 6: Archive, back up, and deploy**

Run from the worktree:

```powershell
$candidate = (Resolve-Path -LiteralPath 'build-msvc32-release\dist\iDmacDrv32.dll').Path
$candidateHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $candidate).Hash
$archiveDirectory = Join-Path `
  'H:\gc\artifacts\runtime-builds\wasapi-shared-clock\diagnostic' `
  $candidateHash
$archivedDll = Join-Path $archiveDirectory 'iDmacDrv32.dll'
$runtime = (Resolve-Path -LiteralPath 'H:\gc\iDmacDrv32.dll').Path
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$backupDirectory = Join-Path `
  'H:\gc\deploy-backups' `
  "wasapi-shared-clock-diagnostic-$stamp"
$rollback = Join-Path $backupDirectory 'iDmacDrv32.pre-shared-clock.dll'

if (Test-Path -LiteralPath $backupDirectory) {
    throw "Rollback directory already exists: $backupDirectory"
}
New-Item -ItemType Directory -Path $backupDirectory | Out-Null
Copy-Item -LiteralPath $runtime -Destination $rollback

if (-not (Test-Path -LiteralPath $archiveDirectory)) {
    New-Item -ItemType Directory -Path $archiveDirectory | Out-Null
    Copy-Item -LiteralPath $candidate -Destination $archivedDll
}
if (-not (Test-Path -LiteralPath $archivedDll)) {
    throw "Diagnostic archive lacks its DLL: $archivedDll"
}

$archiveHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $archivedDll).Hash
if ($archiveHash -ne $candidateHash) {
    throw 'Archived diagnostic DLL hash mismatch.'
}

Copy-Item -LiteralPath $candidate -Destination $runtime -Force
$runtimeHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $runtime).Hash
if ($runtimeHash -ne $candidateHash) {
    throw 'Deployed diagnostic DLL hash mismatch.'
}

[pscustomobject]@{
    Candidate = $candidate
    CandidateHash = $candidateHash
    Archive = $archivedDll
    Runtime = $runtime
    RuntimeHash = $runtimeHash
    Rollback = $rollback
} | Format-List
```

- [ ] **Step 7: Record and commit corrected-build identity**

Append the source commit, test totals, candidate length/time/hash, archive
path, runtime path/hash, and rollback path beneath
`## Shared-clock diagnostic build`.

Run:

```powershell
git add -- docs/reverse-engineering/wasapi-audio-replay-runtime-validation.md
git commit -m "docs: record shared clock diagnostic build"
```

---

### Task 9: Run the 60/59.94, 144, 165, and 240 FPS Acceptance Matrix

**Runtime artifacts:**

- Generated sessions: `H:\gc\audio-diagnostics\<session>`
- Supplied song:
  `H:\gc\data\stage\sound\bgm_b-516_happysyn2_BGM.wav`
- Modify after evidence:
  `docs/reverse-engineering/wasapi-audio-replay-runtime-validation.md`

**Contract:**

- One complete-song diagnostic session per required rate.
- The measured external rate is recorded separately from the configured target.
- Automated evidence proves clock/cursor behavior; the user separately accepts
  sound, chart motion, song-driven effects, and judgement.
- Task 10 cannot begin until the user accepts the corrected diagnostic build.

- [ ] **Step 1: Exercise target 60 near 59.94 measured FPS**

Use the existing in-game target-FPS control to select target 60 and configure
the external limiter near 59.94. Do not edit `data\system.cfg`.

User-owned run:

1. start the game normally;
2. verify the loader reports target 60, zero direct timing writes, nine shared
   hooks, WASAPI PCM16 stereo 48 kHz, and 480-frame periods;
3. play the complete supplied song;
4. observe whether any repeat/scratch artifact occurs;
5. observe chart/judgement and pseudo-key-sound alignment; and
6. wait five seconds after the song, then exit normally.

- [ ] **Step 2: Analyze the target-60 session**

Resolve the session directory from the last diagnostic startup line, then run:

```powershell
python tools/analysis/audio_replay_analyzer.py $sessionDirectory
if ($LASTEXITCODE -ne 0) {
    throw "Target-60 capture is incomplete or invalid: $LASTEXITCODE"
}
Get-Content -LiteralPath (Join-Path $sessionDirectory 'report.md')
rg -n "WASAPI audio runtime summary|FrameratePatch" 'H:\gc\loader-log.txt'
```

Acceptance for this run:

- exact cursor is the dominant observation source;
- rounded/inactive/failed/invalid is not chronic;
- step two occurs at the expected low frequency for measured drift;
- step zero is not chronic;
- backlog drains and ordinary phase error stays below one target tick;
- no watchdog-origin backward seek occurs during the song;
- no submitted-PCM replay matches the confirmed 69 ms signature;
- BGM and `_SHOT` remain aligned; and
- the user hears no repeat and accepts native-60 gameplay behavior.

- [ ] **Step 3: Repeat at target 144**

Select 144 live, restart only as required by the loader's existing FPS control,
and repeat the complete-song procedure and analyzer.

Additionally verify:

- the plan is transformed shared mode with count 50;
- no legacy audio watchdog hook is installed;
- exact source-to-tick conversion remains stable despite
  `48,000 / 144 = 333 1/3`; and
- consumer crossings match the shared Tune step rather than the independent
  outer-frame phase clock.

- [ ] **Step 4: Repeat at target 165**

Repeat the complete-song procedure and analyzer at 165.

Additionally verify:

- no periodic drift pattern derives from rounding `48,000 / 165` or
  `44,100 / 165`;
- step zero/two corrections remain bounded and sparse;
- effect cadence and chart/judgement remain aligned; and
- the user accepts audio and gameplay behavior.

- [ ] **Step 5: Repeat at target 240**

Repeat at target 240 with the normal external limiter.

Additionally verify:

- measured rate is recorded and may differ slightly from 240;
- at approximately 239.703 measured FPS, step-two corrections occur around
  once per 3.4 seconds rather than causing a seek;
- source spans remain monotonic within each generation;
- explicit load/start seeks still create expected generations; and
- the two previously observed 69 ms replay sites do not recur.

- [ ] **Step 6: Investigate any failed rate before cleanup**

If any run has:

- a user-heard repeat;
- a backward same-generation source cursor;
- a watchdog-origin seek;
- chronic rounded/inactive/failed observations;
- unbounded backlog;
- a missed authored-60 effect event; or
- chart/judgement/song misalignment,

keep the diagnostic build and recorder unchanged. Correlate its exact
session, events, submitted PCM, and loader log before changing code. Do not
proceed to Task 10 on partial acceptance.

- [ ] **Step 7: Record the matrix and user verdict**

For each target, append:

- configured target and measured external rate;
- session directory and conclusive duration;
- exact/rounded/inactive/failed/invalid counts;
- step-zero/one/multi counts;
- maximum phase error and backlog;
- generation and seek findings;
- replay analyzer result;
- BGM/`_SHOT` alignment finding; and
- the user's auditory/gameplay verdict.

State explicitly whether all four required targets are accepted.

Run:

```powershell
git add -- docs/reverse-engineering/wasapi-audio-replay-runtime-validation.md
git commit -m "docs: record shared clock runtime acceptance"
```

---

### Task 10: Remove Every Temporary Diagnostic and Build the Production DLL

**Files to delete:**

- `src/Audio/Diagnostics/AudioFlightRecorder.cpp`
- `src/Audio/Diagnostics/AudioFlightRecorder.h`
- `tests/Audio/AudioFlightRecorderTests.cpp`
- `tools/analysis/audio_replay_analyzer.py`
- `tools/analysis/tests/test_audio_replay_analyzer.py`

**Files to modify mechanically:**

- `src/Audio/CMakeLists.txt`
- `src/Audio/DirectSound/DirectSoundFacade.cpp`
- `src/Audio/Mixer/MiniaudioMixer.cpp`
- `src/Audio/Mixer/MiniaudioMixer.h`
- `src/Audio/Wasapi/ExclusiveAudioEngine.cpp`
- `src/Audio/Wasapi/ExclusiveAudioEngine.h`
- `src/Audio/Wasapi/WasapiAudioPatch.cpp`
- `src/Audio/Wasapi/WasapiAudioPatchInternal.h`
- `src/Patches/Framerate/FrameratePatch.cpp`
- `src/Patches/Framerate/FrameratePatch.h`
- `tests/Audio/CMakeLists.txt`
- `tests/Audio/ExclusiveAudioEngineTests.cpp`
- `tests/Audio/MiniaudioMixerTests.cpp`
- `tests/Audio/SecondarySoundBufferTests.cpp`
- `tests/Audio/WasapiAudioPatchTests.cpp`
- `tests/Patches/Framerate/FramerateRuntimeTests.cpp`
- `docs/reverse-engineering/wasapi-audio-replay-runtime-validation.md`

**Permanent code that must remain:**

- unwrapped `AudioCursorResolution`;
- `GameplayAudioCursorObservation`;
- `GameplaySongClock`;
- explicit hook-plan modes and 53-hook capacity;
- the root shared-clock hook;
- the seven range-aware gameplay consumers;
- all permanent unit tests for those components; and
- ordinary startup/fatal logging.

- [ ] **Step 1: Capture the exact temporary-removal manifest**

Run:

```powershell
rg -n "AudioFlightRecorder|AudioDiagnostic|audio-diagnostics|GameplaySongClockCursorSource|PublishAudioResyncDiagnostic|shared.*clock.*counter" `
  src tests tools
```

Save the result in the validation record before removal. Classify each match
as temporary diagnostic or permanent shared-clock behavior.

- [ ] **Step 2: Remove recorder ownership and real-time publications**

Remove:

- the recorder source from `src/Audio/CMakeLists.txt`;
- recorder construction/ownership from the WASAPI patch and exclusive engine;
- submitted-PCM, voice lifecycle, seek, converter-reset, render-span,
  audio-resync, and shared-clock event publication;
- diagnostic-only voice IDs and event metadata;
- the writer thread, bounded recorder queues, capture limit, checkpoint files,
  and temporary startup line; and
- their tests and injected sink signatures.

Retain endpoint pacing, cursor timelines, source spans needed by normal audio,
and the permanent scoped cursor publication.

- [ ] **Step 3: Remove temporary framerate telemetry**

Remove:

- `AudioDiagnosticEventKind::GameplaySongClock`;
- `GameplaySongClockCursorSource`;
- `PublishAudioResyncDiagnostic`;
- exact/rounded/inactive/failed/invalid and step-distribution counters;
- maximum phase-error/backlog counters;
- diagnostic event packing; and
- temporary runtime-summary fields.

Retain `GameplaySongClockDecision` fields used by pure tests and runtime
behavior. Restore the ordinary runtime summary schema.

- [ ] **Step 4: Delete the recorder and analyzer sources**

Delete the five files listed under **Files to delete** with `apply_patch`, then
remove their CMake/test registration. Do not delete the permanent cursor or
clock tests.

- [ ] **Step 5: Prove the removal is complete**

Run:

```powershell
rg -n "AudioFlightRecorder|AudioDiagnostic|audio-diagnostics|GameplaySongClockCursorSource|PublishAudioResyncDiagnostic" `
  src tests tools

git diff --check
```

Expected: `rg` returns no matches and diff check exits zero.

- [ ] **Step 6: Run the clean full build and tests**

Run:

```powershell
& $env:ComSpec /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --preset msvc32-release && cmake --build --preset msvc32-release --target iDmacDrv32'

ctest --preset msvc32-release --output-on-failure
```

Expected: the production DLL links and every remaining CTest passes.

- [ ] **Step 7: Commit the diagnostic removal**

Review the staged file list before committing so unrelated files are not
included:

```powershell
git add --all -- `
  src/Audio `
  src/Patches/Framerate `
  tests/Audio `
  tests/Patches/Framerate `
  tools/analysis `
  docs/reverse-engineering/wasapi-audio-replay-runtime-validation.md
git diff --cached --name-status
git commit -m "chore: remove temporary audio diagnostics"
```

Expected: the commit contains only the mechanical diagnostic removal,
permanent shared-clock test adjustments, and the saved removal manifest.

- [ ] **Step 8: Rebuild and verify the committed production identity**

Run:

```powershell
git status --short
& $env:ComSpec /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build --preset msvc32-release --target iDmacDrv32'

$candidate = (Resolve-Path -LiteralPath 'build-msvc32-release\dist\iDmacDrv32.dll').Path
& $env:ComSpec /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && dumpbin /headers build-msvc32-release\dist\iDmacDrv32.dll | findstr /i "machine x86"'
Get-Item -LiteralPath $candidate | Select-Object FullName,Length,LastWriteTime
Get-FileHash -Algorithm SHA256 -LiteralPath $candidate
git rev-parse HEAD
```

Expected: status is clean before the build; PE machine is x86; and the printed
HEAD is the production binary's exact code commit.

- [ ] **Step 9: Archive and deploy the production DLL**

First refuse deployment while `game471.exe` is running. Then archive under:

```text
H:\gc\artifacts\runtime-builds\wasapi-shared-clock\production\<SHA256>\iDmacDrv32.dll
```

Back up the current live diagnostic DLL under:

```text
H:\gc\deploy-backups\wasapi-shared-clock-production-<timestamp>\iDmacDrv32.pre-production.dll
```

Deploy only after candidate/archive hash equality, then verify runtime hash
equality exactly as in Task 8. Do not alter a configuration file.

- [ ] **Step 10: Run the production smoke test**

At target 60 and one non-integral target, preferably 165:

- start and complete at least one song;
- confirm normal startup/runtime health lines;
- confirm no `audio-diagnostics` session is created;
- confirm no recorder/shared-clock diagnostic line appears;
- confirm audio, chart, effects, and judgement remain aligned; and
- obtain the user's final production verdict.

- [ ] **Step 11: Remove generated diagnostic sessions after evidence is durable**

Resolve each accepted Stage A/shared-clock session to an absolute path and
verify it is a direct child of `H:\gc\audio-diagnostics`. List the exact
directories in the validation record. Only then remove those explicit session
directories. Never target `H:\gc`, `H:\gc\audio-diagnostics`, a wildcard, or
an unresolved variable recursively.

Also remove analyzer-generated candidate directories that belong to those
exact sessions. Retain the compact numeric evidence and hashes in the
validation document.

- [ ] **Step 12: Record cleanup and commit the final evidence**

Append:

- accepted diagnostic commit/hash;
- complete cleanup manifest;
- production source commit/hash/path;
- archive/runtime/rollback paths and matching hashes;
- full CTest result;
- proof that no diagnostic output was created; and
- the user's final production verdict.

Run:

```powershell
git add -- docs/reverse-engineering/wasapi-audio-replay-runtime-validation.md
git commit -m "docs: record shared clock production acceptance"
```

This final documentation-only commit does not change the code used to build
the production DLL. Record both the production code commit from Step 8 and
this evidence commit.

---

### Task 11: Final Verification and Accepted-Branch Integration

**Repositories/worktrees:**

- Tested worktree:
  `H:\gc\artifacts\GCLoader\.worktrees\audio-replay-diagnostics-stage-a`
- Main repository:
  `H:\gc\artifacts\GCLoader`
- Branch:
  `audio-replay-diagnostics-stage-a`

**Contract:**

- Integrate only after Task 10's production smoke test and user acceptance.
- Use a fast-forward merge so the tested commit identity is preserved.
- Remove the worktree only after the main repository points to the accepted
  commit and remains clean.

- [ ] **Step 1: Run final verification from the tested worktree**

Run:

```powershell
git status --short
git log -1 --oneline
ctest --preset msvc32-release --output-on-failure
git diff --check
```

Expected: clean worktree, all tests pass, and diff check exits zero.

- [ ] **Step 2: Record the accepted commit**

Run:

```powershell
$acceptedCommit = git rev-parse HEAD
$acceptedCommit
```

Confirm the validation record names the last code-affecting commit used to
build the production DLL. If HEAD is a later documentation-only evidence
commit, verify that no `src`, `tests`, build-system, or tool file differs
between those two commits.

- [ ] **Step 3: Fast-forward the main branch**

From `H:\gc\artifacts\GCLoader`, first verify its current branch and status.
If it contains unrelated changes or cannot fast-forward, stop and report the
exact condition. Otherwise run:

```powershell
git merge --ff-only audio-replay-diagnostics-stage-a
git rev-parse HEAD
git status --short
```

Expected: main repository HEAD equals `$acceptedCommit` and status is clean.

- [ ] **Step 4: Remove the accepted worktree**

After resolving and verifying that the exact target is
`H:\gc\artifacts\GCLoader\.worktrees\audio-replay-diagnostics-stage-a`, remove
that worktree through Git and prune stale worktree metadata. Do not delete the
directory with a broad recursive filesystem command.

Run from `H:\gc\artifacts\GCLoader`:

```powershell
git worktree remove -- 'H:\gc\artifacts\GCLoader\.worktrees\audio-replay-diagnostics-stage-a'
git worktree prune
git worktree list
```

- [ ] **Step 5: Report final evidence**

Report:

- accepted source commit;
- production DLL SHA-256 and runtime equality;
- full test result;
- runtime targets accepted by the user;
- confirmed absence of temporary diagnostics; and
- fast-forward integration result.

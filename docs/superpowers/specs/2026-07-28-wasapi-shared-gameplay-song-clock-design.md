# WASAPI Shared Gameplay Song Clock Design

**Date:** 2026-07-28

**Status:** Approved

## Summary

Replace Groove Coaster's normal gameplay audio-reseek watchdog with a shared
song clock when the WASAPI exclusive backend is active.

The clock uses the exact presented BGM source cursor as its time observation.
It converts that cursor to the desired integer gameplay tick with checked
rational arithmetic. The game's existing per-update step field then advances
chart processing, judgement, gameplay timelines, and the authoritative frame
counter together. Normal render-rate error is corrected with occasional
zero-step or multi-step gameplay updates instead of rewinding or advancing
audio.

The first implementation does not add render interpolation. Gameplay and
judgement steps remain integral at every supported target rate.

## Related Work

This design builds on, and does not repeat, the implementation contracts in:

- `docs/superpowers/specs/2026-07-14-wasapi-fixed-period-clock-pacing-design.md`
- `docs/superpowers/specs/2026-07-23-wasapi-resync-stutter-experimental-fixes-design.md`
- `docs/superpowers/specs/2026-07-27-temporary-wasapi-audio-replay-diagnostics-design.md`
- `docs/reverse-engineering/wasapi-audio-replay-runtime-validation.md`

Those documents remain authoritative for endpoint-clock publication, cursor
timeline generations, fixed-period rendering, and the temporary diagnostic
capture format.

## Confirmed Problem

The runtime capture for
`data/stage/sound/bgm_b-516_happysyn2_BGM.wav` contains two independently
matching discontinuities:

- the mixer source cursor moves backwards by 3,043 source frames;
- at 44.1 kHz, that is 69.002 ms;
- submitted PCM after each event correlates almost exactly with PCM presented
  69 ms earlier;
- both the BGM and `_SHOT` stems receive the same seek;
- every 480 endpoint frames consumes exactly 441 source frames before the
  seek, ruling out accumulating resampler error.

The game compares two different clocks:

1. gameplay time is an integer frame count multiplied by the configured
   nominal frame duration;
2. audio time is the DirectSound play cursor backed by the 48 kHz endpoint
   presentation clock.

The externally limited process ran at approximately 239.70 FPS while gameplay
time assumed exactly 240 FPS. The clocks therefore separated until the game
requested a backwards audio seek.

At the first confirmed event, the reported audio cursor was approximately
49 ms ahead of the frame-derived target. Roughly 20 ms of render lead made the
actual mixer discontinuity approximately 69 ms.

Reducing `SkipMargin` to 10 ms would not make this continuous. It would produce
approximately 30 ms backwards jumps and would do so much more frequently.
Changing `SkipInterval` does not bound the size of a correction.

## Binary-Backed Clock Topology

The current `game471.exe.i64` establishes the following gameplay path:

| Address | Current behavior |
|---|---|
| `0x0066312A` | Writes `1` to `Tune+0x14`, the logical step for this update |
| `0x00664DB2` | Calls `GC120FPS_GameplayAudioSync_CheckAndSeek` |
| `0x0063F9E0` | Initializes current gameplay/render milliseconds from `Tune+0x10 + Tune+0x14` |
| `0x00664DDC` | Publishes that same next frame through the frame-range dispatcher |
| `0x006401E0` | Loops from `1` through `Tune+0x14` and invokes the millisecond-domain judgement core for every intermediate logical frame |
| `0x00664E23` | Commits `Tune+0x10 += Tune+0x14` |
| `0x00612D70` | Independently drives the BGM/VIB effect timeline from the audio play cursor |

`GC120FPS_GameplayAudioSync_CheckAndSeek` is at `0x00640070` and has only the
gameplay call at `0x00664DB2`.

The judgement loop is the key compatibility fact. A step greater than one does
not discard intermediate judgement timestamps. A step of zero skips logical
advancement while allowing the outer render/update call to continue.

`GC120FPS_DSoundChannel_GetPlayCursorMs` at `0x00614550` obtains a DirectSound
byte cursor and then converts it to whole milliseconds. The shared clock must
observe the exact source frame before this whole-millisecond rounding.
`AudioCursorTimeline` already resolves an unwrapped source position internally;
the DirectSound facade continues returning the wrapped byte cursor while the
scoped observation publishes the unwrapped position.

## Goals

- Keep audio, chart progression, judgement time, and song-driven gameplay
  rendering on one timeline.
- Eliminate normal watchdog-origin `SetCurrentPosition` calls.
- Preserve the original song tempo and pitch.
- Preserve all explicit load, start, stop, restart, and stage-transition seeks.
- Work correctly at, at minimum, 60, 120, 144, 165, and 240 FPS.
- Remain correct when the measured external cap differs slightly from the
  configured integer target.
- Support a 48 kHz, 16-bit endpoint and resampled 44.1 kHz BGM without assuming
  that either sample rate divides the target FPS.
- Preserve native DirectSound behavior when the WASAPI backend is not active.
- Remove the temporary high-volume diagnostic hooks and logs after runtime
  acceptance.

## Non-Goals

- Changing judgement windows.
- Changing `GameTimeOffset` semantics.
- Time-stretching or pitch-shifting audio.
- Making gameplay steps fractional.
- Adding render interpolation in the first implementation.
- Reworking non-gameplay menu timing.
- Treating `_SHOT` as a separately triggered key-sound clock.

## Terminology

### Endpoint output frame

One stereo frame presented by the 48 kHz WASAPI endpoint. Bit depth and channel
count affect byte layout, not time conversion.

### Source frame

One frame in a DirectSound secondary buffer. The confirmed BGM stems use
44.1 kHz source frames.

### Song time

The exact presented BGM source position, adjusted by the existing
`GameTimeOffset`.

### Logical gameplay tick

The integral target-rate frame used by the chart and judgement code. Its rate
comes from `FramerateProfile::target_fps()`.

### Fractional render phase

The rational remainder between two logical ticks. A future renderer could use
it to interpolate visual state. It is not consumed by judgement and is outside
this design's first implementation.

## Decision

The endpoint presentation timeline is the physical time observation for the
shared song clock. Gameplay remains fixed-step, but the number of logical ticks
processed by each outer update is derived from presented song time.

Audio is not independently chosen over chart or judgement. Instead, chart,
judgement, and song-driven rendering consume a gameplay tick selected from the
same time that the user is actually hearing.

Shared-clock activation is independent of transformed high-FPS timing:

- transformed timing depends on `target_fps != 60`;
- shared song-clock ownership depends on committed WASAPI plus a
  gameplay-validated target, including target 60.

## Rate-Independent Conversion

The conversion must never calculate an integer `sample_rate / target_fps`
period. Such a period does not exist for common combinations:

- 48,000 / 144 is 333 and 1/3 endpoint frames;
- 48,000 / 165 is 3,200 / 11 endpoint frames;
- 44,100 / 165 is 2,940 / 11 source frames.

For a source cursor `source_frame`, source rate `source_rate`, signed
`GameTimeOffset` in milliseconds, and integer target rate `target_fps`, the
absolute desired tick is:

```text
adjusted_time_numerator =
    source_frame * 1000
    + GameTimeOffsetMs * source_rate

desired_tick =
    floor(
        adjusted_time_numerator * target_fps
        / (source_rate * 1000)
    )
```

The implementation uses checked signed 64-bit operations and a defined
floor-division helper. It must not use accumulated floating-point deltas.
Conversion from an absolute observation prevents long-run drift.

The policy helper accepts a rate numerator and denominator internally even
though the current `FramerateProfile` supplies `target_fps / 1`. This keeps the
clock arithmetic free of divisibility assumptions without expanding the
current configuration format.

## Exact BGM Cursor Observation

The game-level group getter is retained as the authority for selecting the
active buffer in sound group 2. Its exact cursor publication is the primary
clock input; its rounded millisecond result is used only as the non-seeking
fallback described below.

The loader adds a narrowly scoped exact-cursor observation:

1. The gameplay song-clock hook opens a thread-local observation scope.
2. It invokes the existing sound-manager singleton and
   `CSoundManager_GetGroupPlayCursorMs(2)`.
3. During that call, `SecondarySoundBuffer::GetCurrentPosition` resolves the
   exact source frame through the existing `AudioCursorTimeline`.
4. While the scope is active, it publishes:
   - a monotonically increasing query serial;
   - exact source frame;
   - source sample rate;
   - playback generation;
   - current output-frame observation;
   - playing/draining validity.
5. The scope consumes only a publication created by its own query serial.

This avoids assigning a permanent BGM identity to every
`GameplayNativeCandidate` voice. It also prevents unrelated cursor calls on
other threads or sound groups from becoming song-clock input.

The existing whole-millisecond result remains the game's success signal and is
also the safe fallback clock input. If the getter succeeds but an exact
publication is unavailable, the policy converts the absolute millisecond
cursor to a desired tick. This has at most one millisecond of phase
quantization but cannot accumulate drift.

Once the WASAPI shared-clock hook is active, a missing exact publication never
returns control to the hard-reseek watchdog:

- fresh exact publication: use the source-frame calculation;
- successful group getter without an exact publication: use its absolute
  whole-millisecond result;
- inactive or failed group query: retain the already initialized step of one
  for that update and retry on the next update.

The temporary validation counters distinguish exact, rounded, inactive, and
failed observations. Chronic rounded or failed observations prevent runtime
acceptance even though they do not reintroduce an audio seek.

## Gameplay Step Policy

At `0x00664DB2`, a new checked mid-hook observes the exact group-2 cursor and
computes:

```text
delta = desired_tick - current_tick
```

The normal policy is:

- `delta < 0`: write step `0`; never move gameplay backwards;
- `delta == 0`: write step `0`;
- `delta > 0`: write the positive delta, subject to the catch-up bound.

Typical operation is therefore:

- step `1` for most updates;
- occasional step `2` when the measured cap is slightly below target;
- occasional step `0` when the measured cap is slightly above target.

At the observed 239.703 FPS against a 240 FPS target, one extra logical tick is
needed approximately every 3.4 seconds. That becomes a step of two, not an
audio discontinuity.

The catch-up bound is a duration converted through `FramerateProfile`, not a
fixed frame count. The initial bound is 50 ms of logical work per outer
update, with a minimum of one tick. A larger same-epoch backlog is processed
over subsequent updates. No logical tick inside the accepted step is omitted
from the existing judgement loop.

When the shared-clock hook is active, it always skips the call to
`GameplayAudioSync_CheckAndSeek`, including rounded-cursor and inactive-query
fallbacks. All following original instructions execute unchanged.

## Epochs and Discontinuities

Playback generation remains the primary discontinuity identifier:

- `Play` creates a new generation;
- an accepted explicit `SetCurrentPosition` creates a new generation;
- `Stop` makes the current observation inactive.

On a new generation, the shared clock clears previous monotonicity and backlog
state. It then resumes from an absolute source-frame observation; it does not
carry a delta across generations.

Expected explicit seeks remain owned by the existing stage BGM state machine.
The shared clock does not suppress calls to
`CSoundManager_SetStageBgmGroupPositionMs` from load, start, restart, preview,
or stage-transition paths.

Within one generation:

- a normal source cursor must be monotonic;
- a detected loop may be unwrapped only when buffer length and loop state make
  the wrap unambiguous;
- an unexplained backwards cursor invalidates that observation;
- invalid observations do not write a negative step or request an audio seek.

## Clock Consumers

After the hook selects `Tune+0x14`, the original binary distributes the shared
time:

1. gameplay clock initialization uses `current + step`;
2. frame-range dispatchers receive `current + step`;
3. the judgement update evaluates every intermediate logical tick;
4. gameplay rendering reads the resulting current-millisecond state;
5. the authoritative frame counter advances by the same step.

The BGM/VIB timeline already reads the audio cursor directly, so it converges
with the newly audio-anchored gameplay tick.

Gameplay-specific authored-60 mappings continue to derive from the Tune
logical frame through `FramerateProfile::MapToAuthored60`. The shared-clock
implementation must audit every gameplay hook that still uses the independent
outer-frame `Authored60PhaseClock`:

- gameplay cadence uses shared Tune-frame crossings;
- menus and non-gameplay navigation retain the outer-frame phase clock;
- a multi-step update must account for every crossed authored-60 boundary.

This audit is required specifically for non-integral target ratios such as
144:60 and 165:60.

At native target 60, every logical tick is also an authored-60 boundary. A
multi-step update must therefore preserve every event-bearing tick even though
the display cannot present every intermediate visual state. The binary-backed
audit classifies native-60 consumers as follows:

- the judgement loop at `0x006401E0` is range-aware and invokes the judgement
  core for every tick from one through `Tune+0x14`;
- the frame-range dispatch path at `0x00664DDC` receives
  `Tune+0x10 + Tune+0x14`;
- final-state render and chart-tail consumers use the selected absolute
  gameplay milliseconds or monotonic range comparisons;
- the six effect cadence sites at `0x0064063B`, `0x006408D7`,
  `0x00640C9C`, `0x00641213`, `0x0064122F`, and `0x00641268` are
  event-bearing and must test every target tick in the half-open pre-commit
  range `[current_tick, current_tick + step)`;
- the effect manager call at `0x00664E2D` advances effect state exactly once
  per call and must execute once for every authored-60 boundary crossed in
  `(current_tick, current_tick + step]`.

Those seven existing consumer hooks become shared-clock dependencies at target
60. They do not enable unrelated high-FPS transforms.

## Fractional Render Interpolation

Fractional interpolation is deliberately deferred.

Without interpolation, a normal clock correction can cause one rendered state
to be held or one update to process two logical ticks. The maximum ordinary
phase granularity is:

- 16.667 ms at 60 FPS;
- 6.944 ms at 144 FPS;
- 6.061 ms at 165 FPS;
- 4.167 ms at 240 FPS.

This is substantially smaller than the confirmed 69 ms repeated-audio event,
does not alter audio, and does not skip judgement ticks.

If runtime testing finds the rare visual hold/jump objectionable, a later stage
may expose:

```text
render_alpha =
    exact desired-tick numerator remainder
    / exact desired-tick denominator
```

That value may interpolate rendering only. It must never change the integral
chart or judgement sequence.

## `_SHOT` Mixing

The `_SHOT` stem is not an independent clock and is not the cause of the
discontinuity.

Runtime analysis established that the BGM and `_SHOT` voices:

- have synchronized long-form buffers;
- receive the same group seek;
- remain sample-aligned;
- are mixed at matching gain during uncontaminated blocks.

Sharp `_SHOT` transients can make a repeated region more audible. Both stems
continue to follow the same group-2 playback generation and source position.

## Hook and Ownership Boundaries

### Activation matrix

Timing transformation and WASAPI song-clock ownership are separate plan axes:

| Target/backend | Direct timing writes | Hook selection | Audio behavior |
|---|---:|---|---|
| 60 + native DirectSound | 0 | `OuterFrame` only | Original watchdog |
| 60 + committed WASAPI | 0 | `OuterFrame`, `GameplaySongClock`, and seven shared-clock gameplay-consumer hooks | Shared clock; watchdog bypassed |
| Above 60 + native DirectSound | Existing transformed writes | Existing transformed plan, including native watchdog scaling where applicable | Original watchdog |
| Above 60 + committed WASAPI at a validated target | Existing transformed writes | Transformed non-legacy-audio hooks plus `GameplaySongClock` | Shared clock; watchdog bypassed |

Installing `GameplaySongClock` at target 60 does not enable frame-duration
writes, authored-phase hooks, effect scaling hooks, menu hooks, or any other
transformed-timing behavior. It does select the six gameplay cadence hooks and
the gameplay effect-manager advance hook identified by the native-60 audit,
because those sites must consume step zero and step two correctly.

At an unvalidated target, committed WASAPI retains the pre-existing fallback
plan rather than enabling an untested shared gameplay clock.

### Native 60 FPS behavior

The current source already treats WASAPI audio as independent from transformed
timing: the target-60 WASAPI plan selects `OuterFrame` plus
`AudioResyncPolicy`, while target-60 native DirectSound selects only
`OuterFrame`.

The shared-clock plan replaces that WASAPI-specific resync selection with
`GameplaySongClock` plus the seven audited gameplay-consumer hooks.

At a 60 FPS target:

- an exact 60 FPS outer cadence normally selects step one;
- a 59.94 FPS cadence needs one step-two update approximately every
  16.7 seconds;
- a slightly fast cadence occasionally selects step zero.

The existing judgement loop and frame-range dispatcher are already range-aware.
The six effect cadence hooks use the pre-commit half-open range, and the effect
manager hook replays the exact number of crossed authored-60 boundaries. This
must not be implemented by enabling unrelated transformed-timing hooks.

### `GameplaySongClock`

A new pure policy component owns:

- checked source-frame-to-tick conversion;
- signed offset handling;
- epoch transitions;
- monotonic observation validation;
- step and backlog calculation;
- crossed authored-60 boundary calculation;
- rate-independent diagnostics snapshots used only during validation.

It has no direct dependency on SafetyHook, IDirectSound, or the executable
image.

### DirectSound exact-cursor observation

The DirectSound facade owns:

- the thread-local query scope;
- exact cursor publication from
  `SecondarySoundBuffer::GetCurrentPosition`;
- freshness and generation metadata.

It does not know about Tune objects, target FPS, or chart time.

### `FrameratePatch`

`FrameratePatch` owns:

- the checked hook contract at RVA `0x00264DB2`;
- game ABI calls to the sound-manager singleton and group cursor getter;
- safe reads and writes of `Tune+0x10` and `Tune+0x14`;
- choosing shared-clock versus original-watchdog execution;
- gameplay authored-cadence integration.

The hook is selected only when:

- the WASAPI audio transaction committed;
- the target profile is gameplay-validated;
- every new checked contract passed preflight.

Native DirectSound and an uncommitted WASAPI backend retain the original
watchdog behavior.

### Existing watchdog hooks

In shared-clock mode, the new hook bypasses the only call to
`GameplayAudioSync_CheckAndSeek`. `AudioSkipMargin`, `AudioSkipInterval`, and
`AudioResyncPolicy` are therefore excluded from the shared-clock plan.

They remain available only for a non-shared plan, including native DirectSound
or committed WASAPI at a target that is not gameplay-validated.

## Failure Handling

- Arithmetic overflow fails policy construction or invalidates the
  observation; it never wraps a tick.
- An unreadable Tune pointer reports through the existing fatal runtime path;
  it never deliberately invokes the hard-reseek watchdog as recovery.
- A missing exact cursor uses the successful whole-millisecond group cursor.
- An inactive or failed group query preserves the initialized one-tick step
  and retries without seeking audio.
- A new playback generation clears prior delta and backlog state.
- A same-generation backwards cursor is rejected.
- A large forward backlog is bounded and drained; it does not seek audio.
- Failure to install any new hook or direct dependency rolls back the complete
  framerate transaction.

## Diagnostics Lifecycle

The implementation phase may temporarily record:

- source frame and generation;
- desired/current tick;
- selected step and remaining backlog;
- cursor-to-game phase error;
- crossed authored-60 boundaries;
- exact, rounded, inactive, or failed cursor-source selection.

These diagnostics exist only for runtime validation. After acceptance:

- remove per-update and per-correction logs;
- remove temporary capture-only hooks;
- retain only existing normal startup/fatal logging conventions;
- retain unit-test-visible policy snapshots only if they are part of the pure
  API rather than runtime logging.

## Verification

### Pure policy tests

Test at least:

- target rates 60, 120, 144, 165, 240, and 360;
- source rates 44,100 and 48,000;
- endpoint rate 48,000;
- positive, zero, and negative `GameTimeOffset`;
- outer rates slightly above and below target;
- step sequences containing zero, one, two, and bounded catch-up;
- new Play and Seek generations;
- Stop/inactive observations;
- same-generation backwards observations;
- checked arithmetic limits;
- ten-minute simulations with no accumulated tick drift.

For 144 and 165 specifically, tests must prove the implementation never uses a
rounded integer sample period.

### Exact cursor observation tests

Prove:

- only a scoped query publishes;
- a publication carries source frame, rate, output frame, and generation;
- a stale serial cannot be consumed;
- unrelated groups and threads cannot overwrite a scoped result;
- a failed DirectSound cursor query produces no valid publication;
- PCM16 block alignment does not enter time conversion after resolution to
  source frames.

### Hook plan and transaction tests

Prove:

- exact RVA and expected bytes for the `0x00664DB2` call;
- the hook is selected only for committed WASAPI plus a validated profile;
- target 60 plus native DirectSound selects only `OuterFrame`;
- target 60 plus committed WASAPI selects `OuterFrame` and
  `GameplaySongClock` plus the seven audited gameplay-consumer hooks, with zero
  direct timing writes;
- target 60 never gains unrelated transformed-timing hooks;
- transformed WASAPI plans exclude all three legacy audio watchdog hooks;
- native, failed-WASAPI, and unvalidated-target plans preserve their defined
  original-watchdog fallback;
- every installation failure rolls back all writes and hooks;
- all gameplay cadence hooks use Tune-frame crossings where required.

### Runtime acceptance

Run the diagnostic build with a 48 kHz, 16-bit endpoint at:

- 60 FPS, including a measured cadence close to 59.94 FPS;
- 144 FPS;
- 165 FPS;
- 240 FPS.

For each rate:

- verify the configured and measured external rates;
- capture a complete song;
- confirm no watchdog-origin `SetCurrentPosition`;
- confirm no repeated submitted-PCM region matching the prior 69 ms signature;
- keep game/song phase error below one logical target tick during ordinary
  operation;
- verify BGM and `_SHOT` remain aligned;
- verify chart motion, song-driven rendering, and judgement remain aligned by
  user gameplay acceptance.

Build/static proof and runtime gameplay acceptance remain separate.

## Acceptance Criteria

The design is complete when:

1. normal external-cap drift produces logical step corrections rather than
   audio seeks;
2. no confirmed repeated-audio event appears in full-song captures;
3. chart, judgement, and gameplay time use the same audio-anchored logical
   tick;
4. target 60 uses the shared clock under WASAPI without enabling unrelated
   transformed-timing work;
5. 144 and 165 FPS pass the same correctness contract as 120 and 240 FPS;
6. explicit game-owned seeks still work;
7. no audio tempo or pitch correction is introduced;
8. temporary diagnostic logging is removed after runtime acceptance.

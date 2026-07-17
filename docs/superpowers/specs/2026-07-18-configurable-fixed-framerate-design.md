# Configurable Fixed Framerate Design

Date: 2026-07-18

## Context

GCLoader currently contains an opt-in runtime patch specialized for 120 FPS.
The patch changes Groove Coaster's gameplay frame duration, frame-derived
timers, visual smoothing, chart conversion, input repeat timing, audio resync
intervals, and selected 60 Hz-authored animation domains. Presentation remains
externally limited: the loader deliberately leaves the game's low-resolution
sleep-based limiter unchanged and relies on a driver limiter or RTSS.

The existing implementation is correct only for a two-to-one relationship
between the 120 FPS runtime and the original 60 Hz domains. It contains direct
`120.0`, `1 / 120`, `1000 / 120`, `* 2`, and `/ 2` assumptions. Those
assumptions cannot represent rates such as 144 or 165 FPS. The current palette
smoothing patch also replaces a signed eight-bit comparison immediate; values
above 127 cannot be encoded at that site.

Binary-backed analysis of `game471.exe` established the boundaries that must
remain intact:

- The outer GW loop reaches the existing hook at RVA `0x00058B70` once per
  rendered frame.
- Gameplay, input polling, render, and presentation continue at the externally
  limited target rate.
- MovieClip, news, notice, and selected preload behavior consume a separate
  wall-clock-driven 60 Hz tick.
- Stage 3D transform and color timelines are millisecond based, while
  `*_clip.dat` masks use a 60 Hz-authored frame index patched at RVA
  `0x00244054`.
- Gameplay judgement remains millisecond based even though the authoritative
  gameplay frame and chart-derived frame fields advance at the target rate.
- The game's built-in present limiter at `0x0045B490` uses coarse `Sleep`
  pacing and is not part of the selected solution.

This design generalizes the existing timing transformations to one immutable,
startup-configured target while retaining the proven timing-domain separation.

## Goals

- Replace the 120-FPS boolean with one required, fixed-for-the-process
  `target_fps` setting.
- Accept every whole-number target from 60 through 500 FPS.
- Explicitly validate 120, 144, 165, 240, and 360 FPS in gameplay.
- Preserve native timing at 60 FPS.
- Support non-integral relationships with 60 Hz without assuming that
  `target_fps / 60` is an integer.
- Keep frame pacing entirely external to GCLoader.
- Detect a missing, incorrect, or unsustainable external cap before gameplay
  proceeds far with a mismatched timing model.
- Fail closed if runtime patches cannot be installed as one complete set.
- Preserve current 120 FPS behavior while keeping 2D animation, stage 3D,
  input, audio, countdown, and judgement domains separate.
- Make rate selection, derived timing values, validation results, and failures
  visible in logs.

## Non-goals

- Adding a limiter, sleep loop, busy wait, D3D present detour, or driver-setting
  integration to GCLoader.
- Hot-changing the target FPS while the game is running.
- Modifying `game471.exe` on disk.
- Replacing millisecond judgement windows with frame-based judgement.
- Rewriting all frame counters into wall-clock timers.
- Broadly gating the GW update, render, input, or animation dispatchers.
- Claiming gameplay validation for every integer from 60 through 500.
- Automatically identifying or configuring a particular driver limiter or
  RTSS profile.
- Treating successful builds or unit tests as proof of in-game behavior.

## Configuration contract

Replace:

```toml
[experimental]
enable_120fps_timer_patches = true
```

with one required field:

```toml
[experimental]
target_fps = 120
```

The contract is strict:

- `target_fps` must be an integer from 60 through 500, inclusive.
- Missing, fractional, lower, or higher values fail configuration loading.
- The removed `enable_120fps_timer_patches` key is not accepted as a
  compatibility alias. Its presence is a configuration error even when
  `target_fps` is also present. Existing configurations must be upgraded.
- `target_fps = 60` selects native timing: no high-framerate timing or
  animation transformation is installed.
- The outer-frame cadence validator remains active at 60 FPS so the configured
  target still describes the expected external cap.
- `60`, `120`, `144`, `165`, `240`, and `360` do not produce a support warning.
- Any other valid value logs one startup warning that the value uses the same
  formula-driven implementation but has not been individually accepted in
  gameplay.
- Changing the value requires a game restart.

ConfigGUI replaces the checkbox with a bounded integer control. Its help text
states that GCLoader does not limit presentation, that the external driver or
RTSS cap must equal `target_fps`, and that a restart is required. It does not
recommend a general `IntervalMode` value.

## Selected architecture

### Immutable framerate profile

Introduce a small, independently testable `FramerateProfile` in
`src/Patches/Framerate`. Construct and validate it once before any executable
memory is changed. The successful profile remains immutable for the process
lifetime and is the only source of target-rate values used by framerate hooks.

The profile exposes explicit operations rather than a generic floating-point
multiplier:

```text
frame_milliseconds       = 1000 / target_fps
frame_seconds            = 1 / target_fps
scale_per_frame_delta(x) = x * 60 / target_fps
scale_duration_frames(n) = round(n * target_fps / 60)
map_to_authored_60(n)    = floor(n * 60 / target_fps)
```

Each operation reflects a different semantic:

- Per-frame deltas use floating-point scaling so their wall-clock rate is
  preserved.
- Positive duration counters use nearest-integer scaling. Their temporal error
  is bounded to half of one target frame.
- Target-frame indices mapped into authored 60 Hz assets use floor division so
  the renderer never selects a future authored frame.

Integer products use checked 64-bit arithmetic. Positive results that cannot
be represented by the destination field cause initialization or conversion to
fail; they are not silently wrapped. Nonpositive counter values retain their
original sentinel meaning. Round-half-up is used for positive duration counts
so conversion is deterministic across compilers.

The profile also owns the target-rate float used by rewritten x87 operands.
The shared `60.0` value inside `game471.exe` is never modified globally because
it is used by unrelated 60 Hz-authored effects and diagnostics.

### Patch ownership and transaction

Split framerate startup into preflight and commit phases.

Preflight verifies:

- the executable image and expected original bytes or values at every required
  site;
- every derived profile value and destination representation;
- availability of each required hook target;
- QPC frequency for cadence and authored-60-Hz timing.

Commit installs checked data patches and SafetyHook objects through one owned
transaction. The transaction records original bytes and owns every hook. If
any required operation fails, it removes installed hooks, restores all changed
bytes, reports a fatal initialization error, and terminates the process.

The process must never continue with only some target-rate domains converted.
The successful transaction has static process lifetime so hooks and
profile-owned operand values remain valid.

At `target_fps = 60`, the transaction installs only the outer-frame hook used
for cadence validation. It does not touch gameplay constants, counters,
animation functions, or authored asset indices.

## Timing-domain conversions

### Target-rate gameplay frame domain

For targets above 60 FPS:

- Both proven gameplay/visual frame-millisecond constants become
  `1000 / target_fps`.
- The proven gameplay frame-seconds constant becomes `1 / target_fps`.
- The authoritative gameplay frame continues to advance by one each outer
  frame.
- Frame-to-millisecond calculations, chart-derived frame fields, visual lane
  mapping, and audio expected-position calculations use the target frame
  duration.
- Judgement windows, note source times, audio cursor positions, and stage
  transform/color timelines remain in milliseconds.
- The local chart seconds-to-frames operand is redirected from `60.0` to the
  profile's target-rate float. Other references to the shared `60.0` constant
  remain unchanged.

This preserves song and judgement time while increasing the integer frame grid
used by the proven gameplay paths.

### Per-render visual values

The player-position smoothing step changes from `4.0` to
`4.0 * 60 / target_fps`. The render-offset decay step changes from `5.0` to
`5.0 * 60 / target_fps`.

The palette fade counter runs for `target_fps` frames and both local palette
normalizer operands use the profile's target-rate float. The current
`cmp [eax+0x0C], 0x3C` immediate cannot encode 144 through 500 as a signed
eight-bit value. Replace that direct immediate patch with a guarded mid-hook
that performs the target-aware comparison, reproduces the required flags, and
skips only the original compare instruction.

### Countdown and frame-duration counters

The proven two-second gameplay and render countdown initializers become
`2 * target_fps`. Their target-aware compare hook uses the same derived value.

Positive GW input repeat delays, IFBL waits, IFBL loop counts, and gameplay
audio skip intervals use `scale_duration_frames`. Nonpositive values remain
unchanged. The audio skip-margin floor remains 48 milliseconds because it is
already a time-domain value.

The existing audio resync observation hook remains diagnostic. Its logs use
generic framerate terminology and include the configured target instead of
120-specific labels.

### Authored 60 Hz domains

Retain the current QPC accumulator as the source of an authored 60 Hz tick.
It is independent of whether the target has an integral relationship with 60.
At 144 or 165 FPS, the number of outer frames between authored ticks naturally
varies while the authored behavior remains near 60 Hz in wall-clock time.

The following existing boundaries continue to consult that tick:

- ordinary MovieClip timeline advance, while preserving nested goto-frame and
  frame-action traversal;
- news/banner task updates;
- notice task updates;
- stage BGM preload-delay increments.

No generic GW update, render, input, audio, or animation dispatcher is skipped.
Input continues to be observed by the game's normal target-rate update path.

### Stage 3D clip-mask domain

Stage transform and color timelines remain millisecond driven. At the existing
clip-frame index store, replace the 120-specific division by two with:

```text
authored_clip_frame = floor(target_frame * 60 / target_fps)
```

The calculation uses checked 64-bit arithmetic. This preserves the authored
60 Hz `*_clip.dat` selection pattern for integral and non-integral target
ratios without changing the millisecond timeline selector or mesh-motion path.

## External-cap validation

### Measurement source

Add a pure, deterministic `FramerateMonitor` and feed it one QPC timestamp from
the existing outer-frame hook. Do not add a D3D present hook and do not use the
game's rolling FPS overlay as the validation authority.

The monitor performs no sleeping, pacing, file I/O, allocation, or per-frame
logging. It stores QPC deltas in fixed-capacity process-lifetime storage. The
capacity must hold at least two seconds at the maximum matching rate plus
tolerance. Overflow caused by a clearly uncapped rate is mismatch evidence,
not a reason to allocate.

### Confirmation policy

Validation begins at the first outer-frame callback:

1. Ignore the first five seconds as startup warm-up.
2. Collect a two-second window of positive QPC frame intervals.
3. Calculate measured FPS from the median interval so isolated loading stalls
   do not dominate the result.
4. Classify the window as matching when measured FPS is within plus or minus
   three percent of `target_fps`.
5. Increment the matching streak and reset the mismatching streak for a
   matching window.
6. Increment the mismatching streak and reset the matching streak for a
   mismatching window.
7. After three consecutive matching windows, log validation success and
   permanently disable the monitor for the remainder of the process.
8. After three consecutive mismatching windows, report a fatal mismatch.

The ordinary worst-case decision time is approximately eleven seconds from the
first outer frame: five seconds of warm-up and three two-second windows.
Validation is intentionally startup-only. Later gameplay stalls do not reopen
the decision or terminate an already validated run.

If QPC cannot provide a usable frequency or timestamps, initialization fails
closed because the loader cannot verify the external pacing contract.

### Mismatch reporting

Before termination, log at least:

- configured target FPS;
- measured median FPS;
- relative error;
- number of intervals in the final window;
- consecutive failed-window count;
- whether interval storage overflowed.

Display one modal error explaining that GCLoader does not impose a cap and that
the externally limited rate must equal `target_fps`. The recovery guidance
mentions configuring the driver or RTSS cap, ensuring the system can sustain
the selected target, and restarting the game.

Do not generally recommend `IntervalMode = 1`. Include that hint only when:

- the configured target is above 60 FPS; and
- the measured median is itself within plus or minus three percent of 60 FPS.

That narrow condition indicates that the game's built-in 60 FPS limiter is a
plausible cause. A mismatch such as target 144 and measured 120 must not mention
`IntervalMode`.

The fatal path logs first, shows the dialog once, and terminates after the user
dismisses it. An atomic latch prevents duplicate dialogs. Process termination
uses the existing MessageBox/terminate/fail-fast pattern so failure remains
fatal even if the ordinary termination call unexpectedly returns.

## Diagnostics

Replace 120-specific log labels with generic framerate-patch terminology.
Startup reports:

- configured target and frame duration;
- native versus transformed mode;
- validated-target status or the one-time nonvalidated warning;
- every relevant derived counter and per-render value;
- successful preflight and transaction commit;
- external-cap validation warm-up and final result.

Do not emit one log line per frame or per sample. Existing periodic timing
statistics may remain, but their labels and target-dependent counters must no
longer imply a fixed 120 FPS rate.

## Component changes

### Configuration

- Replace the experimental boolean with required `target_fps` data and an
  accessor that returns the validated integer.
- Add strict range validation shared by production parsing and ConfigGUI.
- Update sample configuration and every config fixture.
- Remove 120-specific GUI labels and persisted keys.

### `FramerateProfile`

- Own all target-rate calculations and validation.
- Provide named operations for duration, index, and per-frame conversions.
- Keep arithmetic deterministic, overflow checked, and unit testable without
  loading the game binary.

### Framerate patch transaction

- Preflight all original bytes and data values before mutation.
- Record and restore direct patches.
- Own SafetyHook lifetimes.
- Install only the cadence hook at 60 FPS and the full proven patch set above
  60 FPS.
- Treat missing sites, unexpected bytes, invalid derived values, and hook
  creation failures as fatal initialization failures.

### `FramerateMonitor`

- Consume synthetic or production QPC ticks through the same interface.
- Implement warm-up, fixed windows, median calculation, tolerance, streaks,
  overflow handling, success shutdown, and one-shot failure publication.
- Return data describing the decision; keep logging, dialogs, and termination
  in the platform integration layer.

## Automated verification

Automated checks establish configuration, arithmetic, patch integrity, and
failure behavior. They do not establish gameplay acceptance.

### Configuration and GUI

- Parse and round-trip `60`, `120`, `144`, `165`, `240`, `360`, `61`, and
  `500`.
- Reject `59`, `501`, fractional, missing, obsolete-boolean-only, and
  mixed-new-and-obsolete configs.
- Verify that ConfigGUI cannot persist an out-of-range value.
- Verify no warning for the native and explicitly validated values.
- Verify exactly one warning for another valid value such as 200.

### Profile arithmetic

- Verify exact frame durations, countdowns, smoothing values, palette limits,
  and proven frame conversions for every explicitly validated target.
- Exercise boundary targets 60, 61, and 500.
- Across every integer target from 60 through 500, verify that authored-frame
  mapping is monotonic, never selects a future frame, and maps the one-second
  boundary exactly: `map_to_authored_60(target_fps) == 60`.
- Verify duration conversion has at most half a target-frame error.
- Verify deterministic tie rounding, sentinel preservation, and overflow
  rejection.
- Verify that no helper performs integer `target_fps / 60` truncation.

### Patch behavior and transactionality

- Exercise checked direct patches against synthetic original instruction/data
  images.
- Verify the palette comparison for targets below, equal to, and above 128.
- Verify countdown compare flags for every explicitly validated target.
- Verify stage clip mapping sequences at 120, 144, 165, 240, and 360.
- Inject failure at every patch and hook installation step and verify complete
  rollback with no surviving hook or changed byte.
- Verify that 60 FPS installs only cadence validation and leaves all timing
  sites untouched.

### Cadence monitor and fatal policy

Use synthetic QPC sequences to verify:

- five-second warm-up and two-second window boundaries;
- exact targets and the plus/minus-three-percent edges;
- three matching windows complete validation;
- three mismatching windows publish one fatal decision;
- matching and mismatching streak resets;
- isolated long stalls do not dominate the median;
- sample overflow at an extreme uncapped rate is a mismatch;
- successful validation permanently disables further monitoring;
- fatal publication and dialog requests are one-shot;
- target 144 measured near 120 omits `IntervalMode` guidance;
- target 144 measured near 60 includes `IntervalMode = 1` guidance;
- target 60 mismatch does not receive the high-target 60-FPS hint.

### Build verification

- Build the 32-bit production DLL and ConfigGUI through the repository's
  supported MSVC/CMake presets.
- Build every new focused framerate/config test.
- Run the complete CTest suite.
- Check the owned diff for whitespace errors and unintended source changes.

## Manual runtime acceptance

Runtime acceptance is performed with `game471.exe`, `IntervalMode` configured
appropriately by the operator, and an external driver or RTSS fixed cap. The
loader never claims to have applied the cap.

### Native 60 FPS regression

- `target_fps = 60` leaves all high-framerate timing sites native.
- An external 60 FPS run completes cadence validation.
- Menus, input, countdowns, gameplay, stage 3D, and audio retain native
  behavior.

### Explicitly validated high rates

Repeat the following at 120, 144, 165, 240, and 360 FPS with a matching
external cap:

- Startup logs the expected target-derived values and completes cadence
  validation without a support warning.
- 2D attract and menu animations retain normal wall-clock speed.
- Stage 3D models/effects remain visible, and clip-mask animation advances
  without skipped or disappearing content.
- Booster input remains responsive without missed, duplicated, or stuck edges.
- Repeat input retains approximately the original wall-clock cadence.
- Countdown and overall gameplay duration remain correct.
- Notes, effects, scrolling, and judgement remain synchronized to the music.
- Millisecond judgement windows do not become tighter or wider with target FPS.
- Gameplay audio introduces no new resync storm, crackling, chopping, or drift.

Build and test success does not satisfy this gate. Every explicitly validated
high rate requires operator-observed gameplay acceptance before it is described
as validated.

### Mismatch acceptance

- Configure target 144 with an external 120 FPS cap. The game reports the
  measured rate and aborts after sustained mismatch without mentioning
  `IntervalMode`.
- Configure target 144 while the measured rate remains approximately 60 FPS.
  The game reports the measured rate, includes the conditional
  `IntervalMode = 1` hint, and aborts.
- Introduce one isolated startup stall during an otherwise matching run. It
  does not cause a fatal result.
- Use a nonvalidated but valid rate such as 200. Startup emits one support
  warning, applies formula-derived timing, and still enforces cadence matching.

## Support statement

The implementation accepts every integer target from 60 through 500. Native
60 FPS and the explicitly accepted high rates have named verification paths.
Other values are intentionally available because the implementation is
formula driven, but their startup warning prevents the project from implying
that every value has received equivalent gameplay testing.

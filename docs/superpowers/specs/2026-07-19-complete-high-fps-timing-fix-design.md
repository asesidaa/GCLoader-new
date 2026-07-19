# Complete High-FPS Timing Fix Design

Date: 2026-07-19

Status: design approved in conversation; implementation remains pending written
spec review.

## Authority and supersession

This document is the authoritative timing-domain correction for the
configurable fixed-framerate feature. It supersedes the following parts of
`2026-07-18-configurable-fixed-framerate-design.md` and the implementation at
commits `b5b2100` and `90ac662`:

- gating the complete News and Notice task updates to an authored 60 Hz tick;
- scaling IFBL loop counts as durations;
- gating the player-position countdown decrement;
- using the target gameplay-frame duration directly for 60 Hz-authored
  gameplay effect assets;
- using a QPC accumulator as the shared authored UI/BGM cadence.

The earlier specification remains authoritative for configuration,
`FramerateProfile`, external-cap validation, startup transactionality, failure
policy, and every timing path explicitly retained below.

Binary facts in this design come from the existing IDA-CLI daemon attached to
`H:\gc\game471.exe.i64`. The persistent investigation is under
`.planning/debug/high-fps-timing-domains`.

## Context

The current loader no longer crashes after relocating `EffectCadence16B`, but
the resulting high-framerate runtime is not behaviorally correct. At 240 FPS:

- the legal-information scene remains visible much longer before the TAITO
  logo;
- News and other transitions are prolonged;
- non-song menus scroll too quickly;
- the card-scan result screen feels difficult to operate;
- GREAT/GOOD and related in-game effects still have incorrect lifetimes and
  playback speed;
- player-position and other render-side visuals feel stepped or mistimed.

These are not one missing scale factor. The executable contains independent
absolute-time, target-frame, integer-duration, authored-asset, cadence, input,
and control-flow domains. The current patch crosses those domains in both
directions: it throttles some absolute-time tasks while feeding target-frame
durations into authored assets.

## Goals

- Preserve the original 60 FPS wall-clock duration and interaction behavior at
  every supported target.
- Run gameplay judgement, chart update/render, stage transforms, input
  sampling, sequence tasks, and elapsed-time waits at the native target rate.
- Keep judgement windows, offsets, note times, BGM positions, and IFBL float
  waits in milliseconds or seconds.
- Scale only integer values that represent wall-clock durations in native
  update ticks.
- Preserve MovieClip, effect-resource, clip-mask, and similar authored asset
  indices at 60 frames per second.
- Preserve IFBL loop cardinality.
- Restore the legal-to-TAITO and News transition timings.
- Restore non-song menu repeat timing without reducing input polling cadence.
- Restore GREAT/GOOD and every classified gameplay-effect lifetime/index sink.
- Make player-position duration and ratio state progress at the target rate
  while retaining authored asset indices.
- Keep the entire transformed patch transactional and fail closed on any byte,
  hook, conversion, or installation mismatch.
- Provide automated structural proof and a 60/120/144/240 FPS runtime
  acceptance procedure.

## Non-goals

- Adding an internal frame limiter or changing external-cap validation.
- Hot-changing `target_fps`.
- Modifying `game471.exe` on disk.
- Changing judgement windows or offsets.
- Interpolating discrete 60 Hz artwork into newly generated intermediate
  artwork.
- Rewriting every binary-owned counter into wall-clock floating point.
- Gating the complete GW update, render, input, sequence-task, News, Notice, or
  stage renderer.
- Adding a global pressed-edge latch or loader-wide transition queue.
- Treating a successful build as proof of runtime acceptance.

## Alternatives considered

### Restore a 60 Hz simulation and interpolate rendering

This would naturally preserve old tick counts but would return judgement and
input opportunity to 60 Hz unless a second input/judgement path were created.
It would also require broad render interpolation across chart, stage, effect,
and player state. The surface area and latency risk are too high.

### Convert all timing to wall-clock values

This would remove many frame-domain distinctions in principle, but the binary
owns numerous integer counters, resource indices, IFBL opcodes, and cadence
predicates. Replacing them all would be an invasive engine rewrite and would
obscure authored-frame semantics.

### Correct only proven domain boundaries

This is the selected approach. Native target-rate simulation and absolute time
remain intact. Integer durations are scaled at initialization, authored frames
are mapped only at final asset sinks, and control-flow counts are preserved.
It fixes each proven regression with the smallest binary surface.

## Timing-domain contract

| Domain | Examples | Required high-FPS treatment |
|---|---|---|
| Absolute elapsed time | `CAppTimer` delta, IFBL float waits, integer ms timestamps | Run every native update; never scale seconds or milliseconds |
| Target simulation frame | `Tune+0x10`, chart-derived frames, native render counters | Advance every target update and use `1000 / target_fps` or `1 / target_fps` |
| Native-tick duration | IFBL integer waits greater than 1, menu repeat thresholds, countdowns | Scale initialization/limit with `ScaleDurationFrames`, then update every native frame |
| Cooperative poll yield | IFBL integer wait 0 or 1 before a polling-loop back-edge | Preserve exactly so timer, input, and status callbacks remain native-rate |
| Authored asset frame | MovieClip, effect `+0x08`, `*_clip.dat` index | Advance at authored cadence or map the final target count with `MapToAuthored60` |
| Authored cadence | period-4/5/6/8/16 effect predicates | Evaluate only on mapped authored-frame boundaries |
| Control-flow cardinality | IFBL loop push/decrement | Preserve the descriptor count exactly |
| Input | held level, pressed/released edge, UI repeat | Poll natively; scale repeat duration; do not globally extend edges |

The key rule is that a value is converted at the boundary where its consumer's
domain changes. A shared constant is not changed merely because one consumer
needs a different domain.

## Selected architecture

### Pure timing primitives

Retain the existing `FramerateProfile` operations:

```text
frame_milliseconds       = 1000 / target_fps
frame_seconds            = 1 / target_fps
scale_duration_frames(n) = round_half_up(n * target_fps / 60)
map_to_authored_60(n)    = floor(n * 60 / target_fps)
```

Add two pure helpers with explicit semantics:

```text
scale_positive_duration(raw)
    preserve signed nonpositive sentinels
    otherwise call scale_duration_frames(raw)

map_player_position_elapsed(raw_total, scaled_remaining)
    scaled_total = scale_positive_duration(raw_total)
    elapsed_target = scaled_total - scaled_remaining
    preserve nonpositive signed results
    otherwise return map_to_authored_60(elapsed_target)
```

All multiplication uses checked 64-bit intermediates. A conversion failure is
fatal and follows the existing one-shot runtime failure path.

### Deterministic authored cadence

Replace the QPC-derived `authored_60hz_tick` accumulator with a pure integer
`Authored60PhaseClock`. The external-cap monitor still uses QPC; only the
authored UI/BGM cadence changes.

The clock is constructed with a validated `target_fps`. Its first call returns
an authored tick, matching the current startup behavior. Subsequent calls use:

```text
phase += 60
if phase >= target_fps:
    phase -= target_fps
    tick = true
else:
    tick = false
```

Initialization sets `phase = target_fps - 60`, so the first call reaches the
threshold. At 240 FPS, authored ticks are exactly four native updates apart.
At 144 or 165 FPS, the spacing follows a deterministic rational pattern. Over
exactly `target_fps` calls, the clock emits exactly 60 ticks.

This shared clock is used only by ordinary MovieClip advance and the stage-BGM
preload increment. Gameplay cadence predicates continue to derive boundaries
from the Tune target-frame counter through `IsAuthored60FrameBoundary`.

The former QPC accumulator fields, clamp, and QPC-only UI constants are
removed. QPC frequency and timestamps remain owned by `FramerateMonitor`.

### Stable authored-frame operand

Add one process-lifetime immutable operand object whose float at offset
`+0x18` is exactly `1000.0F / 60.0F`:

```text
AuthoredFrameOperand
    padding[0x18]
    frame_milliseconds = 16.666666...
```

`offsetof(AuthoredFrameOperand, frame_milliseconds) == 0x18` is a compile-time
assertion. Selected x87 mid-hooks redirect a dead base register to this object,
then allow the original three-byte x87 instruction to execute. No x87 stack is
emulated and no instruction is widened in place.

Each site has a checked expected-byte contract. IDA liveness must remain
documented in the hook name: the redirected EAX, ECX, or EDX is dead or
overwritten immediately after the original x87 instruction.

### Dynamic player-position operand

Add a process-lifetime operand with an integer at offset `+0xC4`. At each
player-position ratio-denominator hook:

1. Read the current raw `Scheduler+0xC4` through the original EAX.
2. Scale the positive duration through the active profile.
3. Store the scaled value in the runtime operand.
4. Redirect EAX to the operand and execute the original `fild [eax+0xC4]`.

The next scheduler call overwrites EAX, so the redirection does not affect
later code. This preserves dynamic scheduler values instead of snapshotting a
startup constant.

## Patch-set changes

### Remove semantically invalid hooks

| Hook | EA | RVA | Required change |
|---|---:|---:|---|
| `NewsUpdate` | `0x00618A50` | `0x00218A50` | Remove complete-task gate; task runs every native update |
| `NoticeUpdate` | `0x006544D0` | `0x002544D0` | Remove complete-task gate; task runs every native update |
| `IfblLoop` | `0x00630AB6` | `0x00230AB6` | Remove loop-count scaling; original store executes unchanged |
| `PlayerPositionCountdown` | `0x0064F0C6` | `0x0024F0C6` | Remove QPC gate; original decrement executes every native render |

Remove their hook storage, callbacks, counters, runtime-stat fields, binding
switch cases, hook IDs, contracts, and tests. There must be no dead compatibility
path that can reinstall them.

### Keep sequence tasks native and adapt only inner frame operations

- `CNoticeTask` and `CNewsTask` execute on every native update.
- IFBL type `0x10` float waits continue subtracting the current global delta on
  every task update.
- IFBL type `0x11` integer waits retain the hook at `0x006309D4`. Values 0 and
  1 remain unchanged; only values greater than 1 are duration-scaled.
- IFBL type `0x17`/`0x18` loop counts and decrements remain original.
- One-update callback retry/yield values remain one native update. The complete
  static audit in E-033 proves all 22 value-1 descriptors are polling-loop
  yields, while the only two positive values above 1 are authored 15-frame
  pauses.
- MovieClip ordinary advance at `0x004DF940` retains the authored tick and the
  goto-depth bypass.
- Stage-BGM pre-state increment at `0x0061001A` retains the authored tick; the
  complete BGM state machine remains native.

This makes the `signature` XFL's 2.0-second legal notice wait approximately
2.0 real seconds at every target while retaining the MovieClip timeline's
authored playback rate.

### Add non-song menu duration patches

The higher-level menu repeat helper at `0x00659110` uses two `.data` globals
outside the existing XIO/native-keyboard repeat sites.

| Data | EA | RVA | Expected | Replacement |
|---|---:|---:|---:|---:|
| Initial held delay | `0x00782CE8` | `0x00382CE8` | `16` | `ScaleDurationFrames(16)` |
| Subsequent repeat interval | `0x00782CEC` | `0x00382CEC` | `3` | `ScaleDurationFrames(3)` |

Both are checked four-byte direct writes and participate in the existing
transaction. At 240 FPS the replacements are 64 and 12, preserving about
266.7 ms initial delay and 50 ms repeats. The song-selection elapsed-time
repeat scheduler remains unchanged.

The transformed direct-write plan grows from 15 to 17 entries. Transaction
capacity grows from 16 to at least 17 and remains compile-time bounded.

### Restore authored gameplay-effect lifetimes and indices

Keep `Tune+0x18 = 1000 / target_fps` globally. Redirect only these proven
authored consumers to the immutable 60 Hz operand:

| Purpose | EA | RVA | Expected bytes | Redirected register |
|---|---:|---:|---|---|
| GREAT/GOOD lifetime | `0x006464A8` | `0x002464A8` | `D8 48 18` | EAX |
| GREAT/GOOD frame | `0x00646528` | `0x00246528` | `D8 71 18` | ECX |
| Effect lifetime A | `0x00648F00` | `0x00248F00` | `D8 49 18` | ECX |
| Effect frame A | `0x00648F8C` | `0x00248F8C` | `D8 72 18` | EDX |
| Effect lifetime B | `0x0064912B` | `0x0024912B` | `D8 49 18` | ECX |
| Effect frame B | `0x006491E0` | `0x002491E0` | `D8 72 18` | EDX |
| Direct effect frame | `0x00649C14` | `0x00249C14` | `D8 72 18` | EDX |
| Chart effect frame A | `0x0064BC8B` | `0x0024BC8B` | `D8 71 18` | ECX |
| Chart effect frame B | `0x0064CC8A` | `0x0024CC8A` | `D8 71 18` | ECX |
| Chart effect frame C | `0x0064CCBE` | `0x0024CCBE` | `D8 72 18` | EDX |
| Chart effect frame D | `0x0064D836` | `0x0024D836` | `D8 70 18` | EAX |
| Fixed 50-frame visual | `0x00650AD5` | `0x00250AD5` | `D8 71 18` | ECX |

The earlier target-frame multiplications at `0x0064BC69`, `0x0064CC7B`,
`0x0064CCAC`, and `0x0064D827` remain unchanged because they reconstruct
absolute current milliseconds. Only the final elapsed-ms-to-asset-frame
division uses 16.6667 ms.

The other 14 visual consumers of `Tune+0x18` remain target-domain or already
have a final authored mapping. No global rollback of `Tune+0x18` is allowed.

Retain the existing managed-effect boundary gate at `0x00664E2D` and the
period-4/5/6/8/16 Tune-based cadence hooks. The new hooks cover render paths
that directly overwrite effect `+0x08` and therefore bypass that manager.

### Map the gameplay countdown at its asset sink

Retain the target-rate two-second countdown initialization and comparison. Add
a mid-hook immediately before:

| EA | RVA | Expected bytes | Behavior |
|---:|---:|---|---|
| `0x00649A9C` | `0x00249A9C` | `89 48 08` | Replace positive ECX target count with `MapToAuthored60(ECX)`; execute original store |

At 240 FPS the countdown remains 480 native updates, while effect `+0x08`
advances through the same authored frames as the 120-update 60 FPS baseline.

### Convert player-position state coherently

Scale EAX at each positive-duration initialization and execute the original
store:

| EA | RVA | Expected bytes |
|---:|---:|---|
| `0x00663240` | `0x00263240` | `89 84 91 54 1D 00 00` |
| `0x006632B2` | `0x002632B2` | `89 84 8A 54 1D 00 00` |
| `0x0066359B` | `0x0026359B` | `89 84 8A 54 1D 00 00` |
| `0x00663615` | `0x00263615` | `89 84 8A 54 1D 00 00` |

The original decrement at `0x0064F0C6` runs every native render without a
hook.

Replace the asset-frame subtraction with a checked conversion:

| EA | RVA | Expected bytes | Behavior |
|---:|---:|---|---|
| `0x0064EF43` | `0x0024EF43` | `2B 84 8A 54 1D 00 00` | Read scaled remaining, compute scaled total from raw EAX, map positive elapsed target frames to authored 60, set EAX, skip original subtraction |

Use the dynamic scaled-duration operand at both ratio denominator loads:

| EA | RVA | Expected bytes | Behavior |
|---:|---:|---|---|
| `0x0064F76D` | `0x0024F76D` | `DB 80 C4 00 00 00` | Scale current `Scheduler+0xC4`, redirect EAX, execute original `fild` |
| `0x0064FD40` | `0x0024FD40` | `DB 80 C4 00 00 00` | Scale current `Scheduler+0xC4`, redirect EAX, execute original `fild` |

This produces target-rate remaining/ratio state, preserves the original ratio,
and feeds only authored frames to the asset. The multiplication sites at
`0x0064F75B` and `0x0064FD2E` remain original.

### Retain correctly classified paths

The following remain unchanged except for removal of obsolete terminology or
counters:

- target gameplay frame and `1000 / target_fps` judgement/current-ms paths;
- millisecond judgement windows, note times, and offsets;
- chart-derived target frames and chart seconds-to-frame operand;
- stage transform/color millisecond timelines;
- stage clip-mask final mapping at `0x00644054`;
- `0x006F4604 = 1000 / target_fps` palette/lane consumers;
- `0x006FC280 = 1 / target_fps` target-counter-to-seconds consumer;
- scaled palette cap and normalizers;
- scaled XIO and native-keyboard 16/8 repeat delays;
- audio skip-margin, scaled skip interval, and diagnostic resync hooks;
- scaled gameplay countdown initialization/comparison;
- Tune-based gameplay effect, blink, and remote cadence mappings;
- external-cap monitor and fatal mismatch policy.

## Hook and transaction ownership

The transformed hook set becomes 41 explicit checked contracts:

- 20 retained existing timing hooks;
- the modified outer-frame hook;
- 12 authored-effect operand hooks;
- one countdown asset-frame hook;
- four player-position initialization hooks;
- one player-position asset-frame hook;
- two player-position denominator hooks.

The removed News, Notice, IFBL loop, and player decrement hooks are not counted.
`kMaximumFramerateHooks` increases from 32 to at least 41. The direct-write
capacity increases to at least 17. Compile-time assertions and transaction
tests must use the new exact counts.

Preflight checks every direct-write value and hook byte pattern before any
mutation. Any mismatch or installation failure rolls back all direct writes
and installed hooks. Runtime conversion failures use the existing fatal latch;
they do not continue with a partially converted state.

At `target_fps = 60`, no transformed direct write or authored/gameplay hook is
installed. Only the existing outer-frame cap validator remains, and native
binary behavior is preserved.

## Input behavior and evidence gate

The loader's 1000 Hz worker continues publishing a held-level snapshot. The
game continues deriving pressed/released edges at native update rate. This
design does not stretch edge lifetime, change accessors, gate input, or add a
global queue.

The non-song repeat correction and removal of prolonged transition states are
expected to address the proven high-FPS interaction regressions. Card-result
input remains a release acceptance gate:

- capture edge production at `0x00455C80` and confirmation callback entry at
  `0x005A5E80` during 60 and 240 FPS runs;
- record active `CStartTask`/IFBL action, held/pressed/released/repeat masks,
  and monotonic time;
- compare identical deliberate presses made after the confirmation prompt is
  active;
- require no missing or duplicate accepted actions at 240 FPS relative to 60.

If this acceptance gate still fails after the timing corrections, the patch is
not complete and must not be described or deployed as complete. Implementation
pauses for a binary-backed spec amendment identifying the exact non-accepting
action and a scene-scoped consume-once policy. This document does not authorize
a speculative edge latch.

## Diagnostics

Startup logging reports:

- target rate and native/transformed mode;
- deterministic authored-clock mode;
- 17 direct-write and 41 hook preflight/commit counts;
- scaled menu repeat values;
- authored operand value;
- scaled countdown and player-position conversions;
- removal of complete News/Notice gating.

Periodic runtime statistics remain bounded and include grouped counters for:

- authored MovieClip ticks/skips;
- stage-BGM preload ticks/skips;
- IFBL integer-wait stores;
- authored gameplay operand redirects;
- countdown asset mappings;
- player-position initialization, asset mapping, and denominator redirects;
- existing gameplay cadence and audio diagnostics.

Remove News/Notice skip and IFBL loop-store counters. Do not log once per
effect, input poll, or rendered frame. Tick-spacing histograms may use fixed
buckets and the existing five-second stats interval.

## Component changes

### `FramerateAuthoredClock.*`

- Add the pure `Authored60PhaseClock`.
- Retain Tune-frame boundary/cadence helpers.
- Add pure player-position elapsed conversion if it fits this domain module;
  otherwise place it beside the profile conversions with an explicit name.

### `FrameratePatchPlan.*`

- Grow the direct plan to 17 entries.
- Add the two menu data writes.
- Remove four invalid hook IDs/contracts.
- Add 20 new explicit hook IDs/contracts.
- Preserve exact ordering with `OuterFrame` installed last.
- Retain complete expected-byte coverage.

### `FrameratePatchTransaction.*`

- Raise bounded write/hook capacities to cover 17/41 exactly.
- Retain preflight-before-mutation and complete rollback.

### `FrameratePatch.cpp`

- Replace QPC UI accumulator state with `Authored60PhaseClock`.
- Remove complete News/Notice, IFBL loop, and player decrement callbacks/state.
- Add immutable and dynamic operand objects.
- Add register-specific authored-operand callbacks.
- Add countdown and player-position callbacks.
- Update diagnostics without introducing hot-path allocation or logging.

### Tests

Extend the existing focused framerate executables; do not add a second patch
framework or source-text inspection tests.

## Automated verification

### Timing arithmetic

For 60, 61, 120, 144, 165, 240, 360, and 500 FPS:

- verify 16/3 menu scaling and half-frame maximum duration error;
- verify positive-duration scaling and sentinel preservation;
- verify player-position elapsed conversion, including zero, completion, and
  signed nonpositive cases;
- verify countdown asset mapping is monotonic and maps `2 * target_fps` to
  120 authored frames;
- verify no helper uses truncated `target_fps / 60` arithmetic.

### Authored phase clock

- first advance emits a tick;
- exactly 60 ticks occur over each subsequent `target_fps` calls;
- 120 alternates one tick and one non-tick;
- 240 emits one tick every four calls;
- 144, 165, and 500 produce deterministic rational sequences;
- reset/reconstruction produces the same sequence;
- invalid targets are rejected by the profile before clock construction.

### Direct patch plan

- transformed mode has exactly 17 direct writes;
- native 60 mode has zero timing writes;
- menu globals require exact original values 16 and 3;
- replacements are 32/6 at 120, 38/7 at 144, 44/8 at 165, 64/12 at 240,
  and 96/18 at 360;
- all prior 15 direct-write contracts remain exact.

### Hook plan and callbacks

- transformed mode has exactly 41 hooks and `OuterFrame` is last;
- native mode has only `OuterFrame`;
- removed hook IDs have no contracts or runtime bindings;
- every new hook has the exact RVA and bytes specified above;
- authored operand callbacks change only the designated register;
- operand objects have compile-time-verified offsets and process lifetime;
- countdown mapping changes ECX and executes the original store;
- each player initializer scales positive EAX and preserves sentinels;
- player asset conversion safely reads the indexed remaining counter, writes
  mapped EAX, and skips exactly seven original bytes;
- denominator hooks read current raw duration, update the dynamic operand, and
  redirect only EAX;
- injected conversion/read failures publish the one-shot fatal path.

### Transactionality

- inject failure at every one of the 17 writes and 41 hooks;
- verify complete rollback and no surviving hook;
- verify capacity rejection below required counts;
- verify preflight mismatch performs no mutation;
- verify successful rollback restores the two menu globals as well as all
  previous direct writes.

### Build verification

- build the supported 32-bit Debug and RelWithDebInfo targets;
- run all focused framerate tests;
- run the complete CTest suite;
- run whitespace/diff checks;
- verify only owned framerate source, tests, and documentation changed.

Automated verification proves arithmetic, byte contracts, hook coverage, and
rollback. It does not prove gameplay acceptance.

## Manual runtime acceptance

Use a fresh Release DLL, a matching external cap, and a clean log for each
target. Record DLL hash, configuration, cap source, and run time. The current
60 FPS log is not evidence for 240 FPS acceptance.

### 60 FPS baseline

- Only cap validation is installed; no transformed timing write/hook appears.
- Record legal notice, News, menu repeat, card result, judgement, GREAT/GOOD,
  countdown, player-position, chart, stage, and audio behavior.
- Confirm no regression from the unpatched executable behavior.

### 120, 144, and 240 FPS

For each target:

- external-cap validation succeeds;
- legal notice start-to-end remains approximately 2.0 seconds and matches the
  60 FPS baseline within 20 ms or one native frame, whichever is larger;
- News 0.5/15/0.5-second waits match baseline wall time rather than scaling by
  `target_fps / 60`;
- MovieClip frame progression matches 60 authored frames per real second;
- non-song repeat begins around 266.7 ms and repeats around every 50 ms;
- song-selection repeat remains unchanged;
- deliberate card-result presses after prompt activation are accepted once,
  without missing, duplicate, or stuck input;
- judgement current time and windows remain millisecond-equivalent to baseline;
- chart positions advance at the native target cadence and remain aligned with
  song milliseconds;
- GREAT/GOOD and representative effect resources visit the same authored frame
  sequence for the same wall duration as baseline;
- gameplay countdown lasts approximately two seconds and uses the same
  authored visual sequence;
- player-position ratios move smoothly at the target cadence while the asset
  frame remains authored-rate;
- stage transforms remain smooth and `*_clip.dat` elements do not disappear or
  skip due to future-frame selection;
- audio does not add crackle, premature BGM transitions, or resync storms.

### Evidence capture

Runtime logs must make it possible to compare:

- outer/native calls and authored ticks;
- Notice/News task calls and IFBL wait progression;
- input edge production and card callback consumption;
- Tune frame/current ms/note ms/judgement result;
- judgement timestamp, effect resource length, and effect `+0x08`;
- countdown target frame and mapped asset frame;
- player-position raw/scaled total, remaining, ratio, and mapped asset frame;
- stage current ms and mapped clip index.

Temporary high-volume tracing is allowed only in a diagnostic build or under
WinDbg and must not remain enabled in the release hot path.

## Deployment boundary

Source, tests, specifications, and commits belong in
`H:\gc\artifacts\GCLoader`. `H:\gc` is the runtime/deployment tree. Deployment
occurs only after the game is stopped, uses a fresh successful Release build,
and preserves operator configuration and unrelated artifacts.

Do not deploy or claim completion until automated verification passes and the
user accepts the 60/120/144/240 runtime matrix. Build success and static IDA
proof remain separate from in-game acceptance.

## Completion definition

The fix is complete only when all of the following are true:

- all specified code/test changes are committed with no unrelated files;
- 17 direct writes and 41 hooks pass preflight and transaction tests;
- complete-task News/Notice gates, IFBL loop scaling, and player decrement gate
  are absent;
- all 12 authored gameplay-effect sinks use 60 Hz milliseconds locally;
- the countdown and player-position sinks map to authored frames correctly;
- all automated tests and full builds pass;
- the 60/120/144/240 runtime matrix passes;
- card-result input shows no missing or duplicate accepted action;
- the user confirms transitions, menu navigation, 2D animation, gameplay
  effects, chart/stage smoothness, judgement, and input feel correct.

## Evidence references

- `.planning/debug/high-fps-timing-domains/FINDINGS.md`
- `.planning/debug/high-fps-timing-domains/RESULTS.md`
- `.planning/debug/high-fps-timing-domains/evidence/INDEX.md`
- `H:\gc\artifacts\2d_boost\signature_xfl\DOMDocument.xml`
- `H:\gc\artifacts\framerate_120fps`
- IDA database `H:\gc\game471.exe.i64`

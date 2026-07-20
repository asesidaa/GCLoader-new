# Results and proposed design

Status: static diagnosis complete; correction design is ready for review. No
production patch has been applied.

## Root-cause model

There is no single game-wide "frame" domain. The current patch correctly moves
the native gameplay clock to the target rate, but then applies several global
conversions to data that remain authored at 60 Hz. It also throttles complete
tasks that own elapsed-time waits. These opposite mistakes explain why the same
240 FPS build contains both prolonged transitions and accelerated visuals.

| Domain | Original representation | High-FPS invariant | Current failure |
|---|---|---|---|
| Absolute elapsed time | `CAppTimer` delta or integer milliseconds | Run every native update; retain seconds/ms | Complete Notice/News tasks run only 60 times/sec but subtract a roughly 1/240 delta, stretching waits by 4x |
| Target simulation frame | Native counter with `1000 / target_fps` or `1 / target_fps` | Advance every native update | This part is mostly correct and is required for fine judgement/chart motion |
| Duration in native ticks | Integer countdown/repeat threshold and IFBL waits greater than 1 | Scale initial/limit by `target_fps / 60`, then update natively | Non-song repeat still uses 16/3 native ticks; player-position state is instead QPC-gated |
| Cooperative polling yield | IFBL wait 0/1 before a loop back-edge | Preserve exactly | Scaling SELECT_NOCARD's wait 1 to 4 throttles its absolute timer and edge input to one-quarter rate |
| Authored asset frame | MovieClip/effect/clip resource index | Derive `floor(elapsed_ms * 60 / 1000)` or map a target count to 60 Hz at the final sink | GREAT/GOOD and several render paths divide by target-frame ms, so asset frames advance 4x at 240 FPS |
| Authored cadence predicate | Every N frames in a 60 Hz sequence | Evaluate against a mapped authored frame | Existing Tune-frame cadence hooks follow this model and should remain |
| Script/control-flow count | IFBL loop iteration count | Preserve cardinality exactly | `IfblLoop` scales the number of loop-body executions as though it were a delay |
| Input sample/edge | Held level plus one-update pressed/released edge | Sample every native update; make UI repeat wall-time invariant | The physical edge path is native, but non-song repeat is 4x too fast; card-result edge loss still needs a targeted runtime trace |

### Confirmed symptom mapping

- Legal notice to TAITO: whole `CNoticeTask` gate plus native delta; the 2.0 s
  IFBL wait predicts about 8.0 s at 240 FPS.
- News transitions: the same mismatch turns 0.5/15/0.5 s into approximately
  2/60/2 s at 240 FPS.
- Non-song menus: 16/3 update ticks become 66.7/12.5 ms instead of
  266.7/50 ms.
- GREAT/GOOD: `0x006463F0` uses 4.1667 ms as an authored-frame duration, so
  both lifetime and displayed frame run four times fast.
- Other in-game animation: multiple direct effect-frame writers have the same
  target-frame/asset-frame leak and bypass the gated effect manager.

## Correction architecture

### 1. Keep the native update and absolute-time backbone

- Keep the outer update, input sampling, `CSeqTask`, IFBL execution, Tune
  judgement, chart update, stage transform, and audio cursor paths running on
  every target-rate update.
- Keep `Tune+0x18 = 1000 / target_fps`, `0x006F4604 = 1000 / target_fps`, and
  `0x006FC280 = 1 / target_fps` for their proven target-domain consumers.
- Keep judgement windows, offsets, note times, BGM positions, and IFBL float
  waits in milliseconds/seconds. Never scale those values.

### 2. Fix sequence tasks without throttling them

- Remove the complete `NewsUpdate` and `NoticeUpdate` authored-60-Hz gates.
- Retain the MovieClip primitive gate: `0x004DF940` advances exactly one
  authored timeline frame per call, so it is the correct narrow boundary.
- Retain IFBL case-17 integer-wait scaling only for values greater than 1.
  Preserve values 0 and 1: the complete static audit proves every value-1
  descriptor is a cooperative polling-loop yield.
- Remove IFBL loop-count scaling at `0x00630AB6`; decimal cases 23/24
  (`0x17`/`0x18`) are control flow, not a timer.
- Leave one-update callback retry/yield values unscaled so timer, input, and
  status callbacks ahead of the yield continue at native cadence.

### 3. Make UI navigation duration-based while keeping input native

- Scale the non-song menu helper globals at `0x00782CE8` and `0x00782CEC`
  from 16/3 to `ScaleDurationFrames(16/3)`. At 240 FPS this is 64/12.
- Retain the existing native input polling and pressed/released construction.
- Leave the song-selection elapsed-time repeat scheduler unchanged.
- Do not globally stretch pressed edges or gate input; either would create
  duplicate actions or reduce responsiveness.

### 4. Split gameplay simulation time from authored effect time

Keep target-frame milliseconds for current-time/chart calculations, but use a
loader-owned immutable `1000/60` value only at the proven authored sinks:

- GREAT/GOOD lifetime/index: `0x006464A8`, `0x00646528`.
- Other effect lifetime/index pairs: `0x00648F00`, `0x00648F8C`,
  `0x0064912B`, `0x006491E0`.
- Direct effect frame: `0x00649C14`.
- Chart-render effect frames: `0x0064BC8B`, `0x0064CC8A`, `0x0064CCBE`,
  `0x0064D836`.
- Fixed 50-authored-frame visual timer: `0x00650AD5`.

At the paired chart sites, the earlier multiplication by target-frame ms must
remain unchanged because it reconstructs absolute current milliseconds; only
the final conversion from elapsed milliseconds to an asset frame uses 60-Hz
milliseconds.

The three-byte x87 instructions cannot be safely widened into absolute-memory
forms in place. The least invasive implementation is a guarded mid-hook that,
only where register liveness proves the base register dead after the x87
instruction, redirects that base to a loader-owned structure whose `+0x18`
field is 16.666666 ms. Each site must retain an expected-byte contract.

Also map the raw target countdown frame in `ECX` to an authored frame just
before `mov [eax+8], ecx` at `0x00649A9C`.

Retain the existing `GameplayEffectAdvance` boundary gate and Tune-based
period-4/5/6/8/16 cadence mappings. They control discrete authored assets and
do not replace the direct-writer fixes above.

### 5. Convert player-position state coherently

Replace the QPC gate at `0x0064F0C6` with one target-domain conversion:

1. Scale the initial `Scheduler+0xC4` value at the stores around
   `0x00663240`, `0x006632B2`, `0x0066359B`, and `0x00663615`.
2. Decrement `+0x1D54` on every native update.
3. Use the correspondingly scaled denominator at ratio consumers
   `0x0064F75B` and `0x0064FD2E`.
4. Map only the elapsed asset-frame value at `0x0064EF43` back to authored
   60 Hz.

This preserves wall duration, makes position/ratio changes smooth at the
target rate, and prevents the asset index from skipping frames.

### 6. Retain the already-correct narrow adaptations

- Stage transforms/colors stay native and absolute-ms; retain only the clip
  mask mapping at `0x00644054`.
- Keep palette cap/normalizer scaling.
- Keep the stage-BGM pre-`0x12` increment gate; do not gate the whole task.
- Keep scaled gameplay countdown initialization/comparison, audio interval
  conversion, target-frame judgement clock, and chart seconds-to-frame
  operand.
- Keep MovieClip goto-depth bypass so explicit frame positioning is not
  suppressed.

### 7. Make the shared authored cadence deterministic

For MovieClip and the narrow UI/BGM authored gates, replace the QPC boolean
with an integer rational phase tied to the validated target rate:

```text
phase += 60
if phase >= target_fps:
    phase -= target_fps
    authored_tick = true
else:
    authored_tick = false
```

At 240 FPS this is exactly one authored tick every four native updates; at
non-multiples such as 144 FPS it produces a deterministic 2/3-update pattern.
The engine's own elapsed delta remains the authority for all absolute-time
tasks. Runtime logging should record tick-spacing histograms so this design can
be compared with the current QPC cadence before removing the old path.

### 8. Preserve IFBL polling yields

The mandatory login page is the `SELECT_NOCARD` opcode-`0x27` loop, not the
previously assumed callback at `0x005A5E80`. Its absolute timer and input checks
run before an opcode-`0x11` wait of 1 and a branch back to the loop label.
Scaling that 1 to 4 at 240 FPS caused both the quarter-rate countdown and the
dropped-feeling input.

The complete descriptor audit found that all 22 value-1 waits are polling
yields and the only two positive values above 1 are authored 15-frame pauses.
Preserve 0/1 and scale only values greater than 1. No input-edge latch or new
diagnostic hook is required.

## Proposed implementation order

1. Remove the three semantically wrong hooks: whole News task, whole Notice
   task, and IFBL loop scaling; add the 16/3 menu-repeat scaling.
2. Add the authored gameplay-effect sinks, countdown mapping, and focused
   expected-byte/unit tests.
3. Convert player-position state and replace the shared authored clock with a
   rational phase.
4. Preserve IFBL polling yields 0/1, retain scaling for waits above 1, remove
   temporary diagnostics, build, deploy, and run the acceptance matrix.

## Required acceptance matrix

| Area | 60 FPS baseline | 120 FPS | 240 FPS | Evidence required |
|---|---|---|---|---|
| Card-scan/menu press edges | Baseline capture | Same accepted-press count | Same accepted-press count | Edge production/action/callback trace plus manual acceptance |
| Menu held navigation/repeat | 266.7 ms initial, 50 ms repeat for the 16/3 helper | Within one native frame of baseline | Within one native frame of baseline | Timestamped repeat events plus manual navigation |
| Screen transitions | Record notice and News label durations | Within 1 native frame or 20 ms of baseline | Within 1 native frame or 20 ms of baseline | Real-time task/IFBL-label timestamps; notice wait approximately 2.0 s |
| 2D animation | Record MovieClip frame/time sequence | Same authored frames over wall time | Same authored frames over wall time | MovieClip call/advance counters and visual acceptance |
| Gameplay judgement | Record frame, current ms, note ms, result | Same millisecond result/window | Same millisecond result/window with finer sampling | Clock/window trace and manual chart test |
| Gameplay effects | Record GREAT/GOOD and representative effect frame/lifetime | Same authored sequence/lifetime | Same authored sequence/lifetime | Result timestamp, resource length, effect `+0x08`, and wall-time trace |
| Stage/chart rendering | 60 update/render samples | Approximately 2x samples with same ms positions | Approximately 4x samples with same ms positions | Render/update counts, chart position by song ms, clip index, and visual acceptance |

## Implementation status

The reviewed implementation was applied in commits `4ab9984` through
`424c0b3` and passed static/build verification. Runtime acceptance remains
open: the loading crash is gone, but the 240 FPS card-confirm countdown is
reported approximately 4x slow and its input remains difficult to register.

E-029 through E-033 corrected the screen identity and established the actual
root cause: global IFBL scaling converted every cooperative wait 1 into a
target-scaled delay. E-034 records the production fix, successful x86 build and
focused test, removal of both probes, and deployment. The active DLL SHA256 is
`3EA2BF5238E1F9795EC99B91AA8EF1531D80740C0DB7ADD61B574CC11BB9628E`.
E-035 records the operator's 240 FPS runtime acceptance and request to commit
and merge. The correction is accepted for integration; a quantitative
60/120/144/240 matrix was not captured and is not inferred from that report.

## Open shared-navigator defect

After integration, the operator identified one remaining defect: the
bottom-right navigator character animates about 4x fast at 240 FPS. It is
visible in song selection, mode selection, and other menus. E-037 establishes
that this is not a MovieClip defect. The character is a manual
`base.dds`/`face.dds` renderer whose state routine `0x005B6310` advances once
per draw callback, making every fixed count expire four times faster at 240
FPS. E-038 proves that ten task classes, including `CSelectGameTask` and
`CSelectMusicTask`, register this same global callback.

E-039 implements the complete narrow design: the callback and draw routine
remain native-rate, while only `0x005B6310` executes on the existing
authored-60-Hz clock. This globally covers every mapped navigator-bearing scene
and avoids touching the shared MovieClip path, individual task methods, or the
renderer's interdependent 2/5/10/11/17/30/180..307 counts.

The production DLL was built through the checked-in `msvc32-release` preset,
passed all three focused framerate tests, and is deployed with SHA256
`3001A110B4A69AF0E675EC03ACCCBE3F7B918E72ABB3AAAC818EEFA14C78B3F8`.
Runtime acceptance at high FPS remains pending.

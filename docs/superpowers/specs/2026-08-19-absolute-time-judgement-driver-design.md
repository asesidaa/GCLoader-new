> **ARCHIVED FAILED DESIGN — NOT AUTHORITATIVE.** Its goal remains relevant,
> but its mechanism was implemented and failed catastrophically at runtime.
>
> **HARD GOAL: Gameplay judgement must be driven by absolute song time and must not depend on render or update framerate, whatever native-boundary changes are required.**

# Absolute-Time Judgement Driver Design

**Date:** 2026-08-19
**Status:** Proposed — awaiting user review
**Supersedes:** all input/judgement designs listed in
`docs/reverse-engineering/high-fps-absolute-time-redesign-failure-index.md`
(those are failure records, not design authority)

## 1. Core identity

A note at 10000 ms pressed at 9999 ms must judge with timing error
**|10000 − 9999| = 1 ms**, at every framerate, during stutter, and after
catch-up. This identity is the acceptance test of the architecture. Any
mechanism that does not produce it is wrong.

Resolution floor: the native recognition argument is an integer millisecond
(`0x5D68E0` arg 2 is `int`), so judgement resolution is 1 ms. A press at
9999.6 ms judges as 9999. This is the authored game's own ms domain; the
design does not and cannot exceed it.

## 2. Requirements

- **R1 — 60 FPS is untouched.** At `target_fps == 60` no patch of this
  design is installed. Install is transactional and gated; there is no
  runtime mode switch into or out of the design.
- **R2 — Every note family works.** Section 7 walks the complete
  E-046 raw/canonical/effective matrix, both modes, long-form mechanics,
  hidden chart notes, and post-descriptor free input.
- **R3 — Non-multiple-of-60 framerates introduce no additional error.**
  144 and 165 FPS (and any other rate) produce the same judgement values
  as 240 FPS or any other rate for the same physical input; update cadence
  affects batching latency only (Section 8).
- **R4 — Simple failure policy.** Bool-returning loader helpers are checked
  with assertion + hard abort. No fallback, no retry, no degraded mode, no
  latches. Install-time transaction failure aborts before activation.
- **R5 — No invented tests.** Automated tests cover only loader-owned
  arithmetic, transactions, and assertion behavior (Section 10). Gameplay
  correctness is proven only by native-process instrumentation and
  operator/cabinet runs.

## 3. Baseline and cleanup

- Source baseline: the existing worktree
  `H:\gc\artifacts\GCLoader\.worktrees\asio-audio-backend`, branch
  `asio-audio-backend` at `a6f7ed1`, after the user-authorized cleanup of
  its dirty state (2026-08-19: the user declared the dirty working tree to
  be failed-attempt garbage): all tracked modifications and deletions are
  restored to `a6f7ed1`, the untracked failure-artifact code
  (`src/Input/AbsoluteTime/`, `tests/Input/AbsoluteTime/`, the withdrawn
  2026-08-18 spec/plan, and the `tools/analysis/ida_game471.py` bridge) is
  deleted, and the reverse-engineering records
  (`docs/reverse-engineering/high-fps-absolute-time-redesign-failure-index.md`,
  `high-fps-input-judgement-pipeline.md`) are committed as history before
  the implementation begins.
- Deleted on the same branch as the first implementation task:
  `src/Input/HighFps/` (the 8 judgement hooks and the bridge that answers
  native queries with loader-computed values — the F-003 architecture) and
  `tests/Input/HighFps/`.
- Retained and modified: `src/Patches/Framerate/` (cadence and
  `GameplaySongClock` step semantics), `src/Input/Switch/SwitchInputPatch`
  (transport seam; `GameplayInputHookTransaction` is deleted with HighFps —
  its hook sites are not used by this design), `src/Input/Polling/` (QPC
  journal survives as transport).
- Salvaged as pure loader utilities (rewritten, not moved blindly):
  the QPC-transition journal and the QPC→song-ms anchor arithmetic.

## 4. Verified native contract (design inputs)

All facts below are proven by E-042…E-046 artifacts plus the 2026-08-19
raw-disassembly probes recorded in this design session (daemon:
`H:\gc\game471.exe.i64`).

| Fact | Evidence |
|---|---|
| `0x6401E0` loop: `m = 1..Tune+0x14`; `frame = Tune+0x10+m`; `recognition_ms = trunc(frame × 0x6FC0A0)`; calls `0x5D68E0(jstate[Tune+0x254][player], ms, frame)` then `0x5CF930(sstate[Tune+0x26C][player], ms)`; then a once-per-update tail (sound triggers, effect spawns with `Tune+0x10 % {4,5,6,8,16}` cadence gates, chart-row cursor advance vs baked frame fields `desc+0xC0/+0xC8`, `Tune+0x1D0` row cursors) | decompile + `0x6401EF`, `0x6402A9`, `0x6402C9` disasm |
| Judgement core `0x5D68E0` is ms-domain: judged time = `sub_659270(per-player lookup) + arg_ms` (per-player offset applied natively); windows compare baked note-ms fields; frame arg is stored at `this+0xA0` and used only as the history-query index for every pressed/held/released/held-age/direction wrapper call and free-input | IDB annotation + argument-provenance artifact |
| `0x5CF930` score: thiscall, one stack arg (the same ms), `retn 4`; consumes resolved grades into counters `+120/+124/+128/+132` | E-044 + entry/exit disasm |
| Full proven ABI of the driver's calls (raw disasm, 2026-08-19): core `0x5D68E0` = thiscall, stack args `[ms, frame]` pushed frame-then-ms (`0x64028D`), `retn 8`; score `0x5CF930` = thiscall, stack `[ms]` (`0x6402B1`), `retn 4`; capture = virtual thiscall `(booster, frame)` via CBooster vtable `+0x0C` (`0x65995C`), target `0x62CFB0`; loop-exit/tail entry inside `0x6401E0` = **`0x6402D0`** (`jle` at `0x640239`, `jg` at `0x64026B`) | raw disasm |
| Both core and score add a **native base** to the passed ms before grading: the core adds a per-player value from the `sub_659270` lookup; the score adds the member's audio-group base (`this+0xAC` ← `GetGroup(member149)`). Passed ms is therefore in the frame-derived timeline (0 at frame 0); bases translate it into the song timeline natively | decompiles |
| Judged-time arithmetic: grade error = `note_ms − (base + passed_ms)`; every additive term except `passed_ms` is native | decompiles |
| Input history: one 16-bit mask per frame slot; ring index `(frame+δ) % capacity`; pressed = `mask(f) & ~mask(f−1)`; composite 10–14 = same-frame OR; paired 15–19 = both-pressed, else one-pressed + `j=1..4` prior-frame lookback | `0x62DFB0` decompile |
| Recapture semantics: a capture at a **fresh** frame overwrites the slot (`sub_62D940` = `*p = m`); a capture at a **same/old** frame **ORs** into it (`sub_62D920` = `*p |= m`). Within one authored frame, once pressed the slot stays pressed; a release becomes visible at the next frame's capture — the authored one-mask-per-frame fusion, produced by native code | decompiles |
| Consecutive-held: 20 counters, incremented per capture while held (`0x62DC60`, called unconditionally from the end of `0x62CFB0`), read by index (`0x62DAA0`); sole consumer chain: the direction matcher `0x5D2E50`, whose thresholds are `age <= 1` (fresh/head phase) and running-max `age <= 4` (continuation grace) over each side's four direction controls | decompiles |
| Live capture (`0x659920`: input-frame `++`, virtual CBooster capture, then per-device updates) is called **only** from `LoopLastTask_CaptureInputAndAdvanceDevices` (`0x66CB60`, every update tick — render cadence) and the device-reset path (`0x6599B0`). The gameplay SM never calls it; the SM's `0x659860` only held-fills and jumps the input frame. **This per-tick capture is the mechanism that quantizes edges to update boundaries.** | xrefs + decompiles |
| Outer order per gameplay update: `0x664DB2` audio-sync watchdog → `0x664DBD` clock init (Tune+0x18/0x1C/0x20/0x24 from `(Tune+0x10+Tune+0x14) × 0x6FC0A0`) → `0x664DDC` fill/jump → `0x664E06` process → `0x664E23` commit `Tune+0x10 += Tune+0x14` | disasm |
| The watchdog (`0x640070`) computes its expected ms independently from `Tune+0x10/+0x14`, compares against the live BGM cursor with a tolerance (global `+0x34`) plus a periodic per-N-frames resync (global `+0x3C`), and **seeks audio to the frame-derived time** (frame clock is master). It never reads the `Tune+0x1C` clock fields | decompile |
| `0x6FC0A0` (16.666666 ms/frame) is consumed at 8 sites: judgement init, chart baker `0x5EB210`, audio-effect timers, history capture, clock init, audio watchdog, recognition ms, SM elapsed-seconds | xref sweep |
| Only one caller of `0x6401E0` (the SM at `0x664E06`) | xref |
| The loader already owns the step seam: `FrameratePatch.cpp:1638` writes `Tune+0x14` (`kTuneStepOffset`), and the judgement song clock is constructed at `FrameratePatch.cpp:2210` (`GameplaySongClock::Create(target, 1)`) | clean-source audit |
| Score accumulation (`0x5CE990`) walks chart rows via its own consume-advance cursors (`this+176/+200/+224`) and is grade-event-driven; native calls it 60×/s with no new grades on most calls, so no-new-event calls are inert by native construction | decompile |

Design consequence: the frame domain is **not** reinterpreted. It stays
authored (1 frame = 16.666666 ms of song time) everywhere native code uses
it — baker, cursors, forgiveness, age counters, effect cadence, watchdog.
The **operand is never rewritten**. What changes is only *when* recognition
steps run and *what ms* they receive.

## 5. Architecture — the absolute-time step driver

One new runtime patch replaces the uniform frame loop of `0x6401E0`. The
gameplay state machine still runs once per render update (as today); the
operand, the baker, the capture code, the query code, the handlers, the
score path, and the tail all remain native.

### 5.1 Step scheduler

Per gameplay update, with `now_ms` = authoritative song time (existing
audio-anchored `GameplaySongClock` observation) and `last_ms` = last
processed step time, the driver builds a strictly ascending step list:

1. every authored frame boundary `b` with `last_ms < b ≤ now_ms`
   (`b` ranges over multiples of 16.666666 ms; a boundary is "crossed"
   when the containing frame number advances);
2. every journalled input transition (press or release of any of the ten
   controls; direction changes are derivations of these, not separate
   events) whose mapped song-ms lies in `(last_ms, now_ms]`;

deduplicated by exact ms. For each step at time `t`:

- `f = ⌊t / 16.666666⌋` — the authored frame containing `t`, identical to
  the native `trunc(f × 16.666666)` slot arithmetic;
- bring the input history to `f` (Section 5.2);
- call `0x5D68E0(jstate, trunc(t), f)` then `0x5CF930(sstate, trunc(t))`
  at the proven ABI.

No-input progression is boundary steps — the authored cadence — so no
"settlement" policy exists to invent (failure-index Unknown #4). Updates
with no boundary and no edge run zero steps. A stutter batches its steps
after the stall with each step's **own** exact time; nothing is re-stamped.

Tune frame accounting stays native and authored: the existing cadence seam
sets `Tune+0x14 = ⌊now_ms / 16.666666⌋ − Tune+0x10` (authored frames
crossed, backlog cap = 3). Clock init, fill, commit, and the `0x6401E0`
tail then observe exactly authored values. The driver never writes
`Tune+0x10`/`Tune+0x14`.

### 5.2 Capture seam — live ring writes and capture ownership

> **Correction (2026-08-19, post-runtime-review):** the original §5.2
> specified an "as-of song-ms" capture context answered through the
> `SwitchInputPatch` query hooks. That seam is deleted. The proven native
> facts: the note handlers and the judgement core read the `CBooster`
> history ring through `0x659640`/`0x659570`, while the capture itself
> (`0x62CFB0`) samples the physical devices through
> `0x633620`/`0x6335B0` (GWInputXio aggregate + keyboard fallback) — the
> capture path never consults the query hooks, so an as-of branch on those
> hooks only ever intercepted the recognition reads and starved them of
> every pressed edge (the observed total input/grading blackout). The
> corrected seam below replaces it.

Before each step's core calls, the history slot `f` must reflect the
physical state the step judges at:

- Captures always sample the live native device state; nothing intercepts
  the sampler. The journal is the timing source for **step scheduling**
  only — a transition at `t` becomes an edge step at exactly `t`, and that
  step's recognition reads the ring after the recapture below has ORed the
  live press into the current slot.
- Frame advance uses the native capture itself: `0x659920` for each
  not-yet-captured frame up to `f`, and a direct
  `UpdateInputHistoryAtFrame(booster, f)` (`0x62CFB0`, thiscall
  `(booster, frame)`) for a same-frame recapture when a second transition
  lands in an already-captured frame. The native recapture branch then
  **ORs** the new mask into the slot (proven): a second control's press
  joins the frame's mask; a release cannot clear a bit mid-frame and
  becomes visible at the next frame — the authored one-mask-per-frame
  fusion, produced entirely by native code.

**Capture-ownership seam (new, discovered while closing the gates).** The
per-tick live capture runs from `LoopLastTask` (`0x66CB60` → `0x659920`)
at render cadence — including during gameplay — and is what stamps edges
onto update boundaries today. The design detours `0x659920` **at entry**:
while a gameplay session is active, the detour advances the input frame
only to the next driver-scheduled authored frame boundary whose time has
arrived (one capture per scheduled frame, seam answering state-at that
boundary); when no boundary is due it performs no capture. Outside
gameplay the original body runs untouched (menus native). Session
liveness is a deterministic counter state machine: a session is active
from the first judgement-frame stub invocation until
`ceil(target_fps/60) + 2` consecutive `0x659920` ticks occur with no
judgement update in between (handles gated/ungated gameplay cadences at
any FPS without timers). On session start after a gap, an alignment reset
restores input-frame/CBooster bookkeeping (two guarded native writes plus
one fresh capture) so stray render-cadence captures from the gap cannot
pollute slots via the OR branch. The one-tick transition tolerance at
gameplay start is the spec's accepted bound.

Representation limits, stated honestly: two same-control transitions
inside one 16.67 ms frame fuse into one slot state (the authored machine's
own limit); multi-event frames accumulate by OR (authored single-capture
would sample one instant) — both bounded to sub-frame events.

### 5.3 Consecutive-held cadence guard (locked)

`0x62DC60` increments the held counters once per capture, unconditionally.
Driver recaptures (second event in a frame) would inflate held ages, and
the sole consumer — the direction matcher `0x5D2E50` — classifies
fresh/head at `age <= 1` and continuation grace at `max_age <= 4`: small
frame counts on the flick/slide hot path, not a benign ±1. The design
therefore locks in an **entry detour on `0x62DC60`**: if the incoming
frame equals the last frame this function counted (loader-side cached
`(booster, frame)`; single-booster instance asserted), return immediately;
otherwise cache and run the original. Fresh-frame captures behave
identically to native; recapture frames skip counter churn — exactly the
authored once-per-frame cadence. This is the one place the design adds a
native behavioral guard rather than accepting a deviation, and the
evidence above is why.

### 5.4 Clock init exact-now (locked in, watchdog-independent)

With the frame domain authored, `Tune+0x1C`-family clock fields would
advance in 16.67 ms steps — authored look, but a visible regression at
high FPS versus today's operand-rewritten smooth clock. Locked decision:
one MidHook at `0x63FA0C` (the instruction that stores the frame-domain
value `Tune+0x10 + Tune+0x14` into the local the rest of the function
consumes) that rewrites that local to `lround(now_ms / 16.666666)`. The
native body then derives all clock fields from a virtual frame that
tracks exact song time, keeping the operand, the stored ms-per-frame
field `Tune+0x18`, the audio-table lookups, and all downstream math
native and authored-valued; clock steps follow update cadence (≈4.2 ms at
240 FPS) instead of frame cadence (16.67 ms). This is safe with respect
to the audio-sync watchdog because the watchdog (`0x640070`) computes its
expected time independently from `Tune+0x10/+0x14` and never reads the
clock fields; the driver preserves the authored frame↔audio relationship
the watchdog compares against. Watchdog tolerance/resync constants
(globals `+0x34`, `+0x3C`) are native-process instrumentation
checkpoints, not design inputs.

### 5.5 Patch surface and install gating

All guarded, transactional, with expected original bytes and rollback,
per existing patch infrastructure. `target_fps == 60` ⇒ the transaction
never begins (R1). Addresses below are RVAs (image base 0x400000).

| # | Site (VA / RVA) | Kind | Purpose |
|---|---|---|---|
| 1 | `0x640239` / `0x240239` (loop-guard `jle` before the native frame loop) | MidHook | Run the absolute step list, then set `eip` to native tail `0x6402D0`; the native uniform loop never executes |
| 2 | `0x62DC60` / `0x22DC60` entry | MidHook | Consecutive-held cadence guard (§5.3): skip when frame unchanged |
| 3 | `0x659920` / `0x259920` entry | MidHook | Capture ownership (§5.2): advance-to-scheduled-frame during active sessions; original otherwise |
| 4 | `0x63FA0C` / `0x23FA0C` | MidHook | Clock fields from exact song time (§5.4) |

The input seam is the existing `SwitchInputPatch` hook surface
(`0x659640` held / `0x659570` pressed-edge) answering the native ring
through the original wrappers — the driver never intercepts a query
answer; it owns the ring's content through the capture/recapture sites
above. The step seam is the existing `FrameratePatch.cpp:1638` write
with the clock constructed at authored rate (`GameplaySongClock::Create(60, 1)`,
backlog cap 3 authored frames). Native calls made by the driver use
`__thiscall` function-pointer types proven in §4 (recognition step, score
frame, capture, fill) — never IDA-inferred prototypes. The eight HighFps
judgement hooks and the Switch query-hook transaction are not installed
by this design — no loader code answers a native input query.

## 6. Why the core identity holds

For the press at 9999 ms: 9999 is a journalled transition ⇒ it is a step.
Its slot `f = 599` is captured with state-at(9999) ⇒ the pressed edge is
visible at exactly that step. The core receives `recognition_ms = 9999`.
Judged time = native base (per-player offset + audio-group base, both read
natively inside the core/score) **+ 9999**; the tap family's grade error is
`note_ms − judged_ms` = 1 ms. Both base terms are additive and native; the
only loader-supplied term is the exact press time, in the same
frame-derived timeline the authored game uses (0 at frame 0). No
frame-derived quantity enters the grade. The frame argument decided only
*that* the handler fired at this step — the identical role it plays in the
authored game.

## 7. Note-family coverage (R2)

Judgement values always come from the ms argument; families differ in
which queries gate and fire them. Per family, under this design:

| Family (effective) | Native queries used | Behavior under the driver |
|---|---|---|
| Tap `1`, HIDDEN `7`, HIDDEN2 `8`, variant `6` | pressed edge, late gate, grade | Edge visible at its own step; grade = \|note − t_edge\| exact. Overdue MISS fires at the first boundary/edge step past the deadline (≤ 16.67 ms publication, authored cadence) |
| Critical `9` (raw `9/C/E`, mode-17 tap) | paired control 15–19 pressed | Both-press or one-press + `j=1..4` prior-frame lookback. Slot domain is authored ⇒ the forgiveness window is exactly the authored ≈66.7 ms at every FPS (today it collapses to 1000/fps × 4). Grade exact |
| Flick `2` (tap in mode 2) | direction matcher head, late gate, grade | Direction vectors are built natively from ring slots; the head reads slots captured at the edge's own step. Selector 2 and diagonal preservation are native code, untouched |
| Hold `3` (mode-2 `2/4/5/A/F`) | pressed start, held continuation, duration grade | Start grade exact (press step). Continuation queries hit boundary-step captures holding the held mask. Release is a journalled transition ⇒ its own step ⇒ end/duration exact |
| Scratch `4` (raw `D`; mode 2→hold) | four directional pressed, late gate | Directional bits are mask bits at the edge step; queries read slots captured at state-at(t_edge) |
| Beat `5` (mode 2→hold) | pressed edge, late gate | As tap |
| Slide-hold `A` (raw `B`) | direction head/continuation, held, held-age, direction | Held-age = consecutive-held counters in authored frames (60 Hz units preserved — no rescaling, ever). Direction from slots at step captures |
| Dual-hold `F` | pressed start, held continuation | As hold |
| Composite IDs 10–14 | same-frame OR of constituents | Mask bits; both constituents' states present at the step's single capture |
| Post-descriptor free input | controls 4 and 9 pressed at the step frame; `FreeTapDisableTimeAfterMark` 200 ms and miss-mark gates | Same step machinery; gates are ms-domain vs the step's exact ms. Conflict/eligibility ordering native (E-045/E-046) |
| Normalization, mode rewrite, equal-time suppression | none (load-time `0x5EB210`) | Untouched; judgement timing is orthogonal |

The design introduces **no** loader-side descriptor classification, note
routing, edge claiming, or grade adjustment. If a family misbehaves at
runtime, the fix belongs in the step/capture machinery, never in a
per-family loader branch.

## 8. Non-multiple-of-60 framerates (R3)

Everything below is derived in song-time, so update cadence enters only as
batching latency (≤ one render update — 6.9 ms at 144, 6.1 ms at 165),
never as a value:

- Frame boundaries are multiples of 16.666666 ms of **song time**. At 144
  FPS, `Tune+0x14` alternates 0/0/1…; at 165, similar; at 240, exactly 1
  per 4 updates. The replaced loop makes per-update frame counts
  irrelevant to correctness.
- Edge steps carry their own ms regardless of which update batches them.
- Effect-cadence gates (`frame % k`) fire on frame crossings: authored
  rate at any FPS (this also repairs the current 240 FPS effect-cadence
  bug where they fire 4× too fast).
- Forgiveness, held-age, ring coverage: authored-frame units, FPS-free.
- Stutter/backlog: the existing 50 ms backlog cap (3 authored frames)
  bounds catch-up; edges inside the backlog keep exact times.

## 9. Error handling (R4)

- Every loader helper that can fail returns `bool`/`std::expected`; every
  caller asserts success. Assertion = hard abort (existing abort path).
- Impossible runtime states (clock going backwards past tolerance, journal
  inconsistency, anchor invalid mid-gameplay, capture alignment write
  failure) are assertions, not recoveries.
- Patch install: one transaction; any guard/mismatch aborts before any
  activation write. No partial-install recovery logic.
- No watermarks, epochs, disable-latches, retry queues, or fallback
  judgement modes exist in this design.

## 10. Testing and proof (R5)

> **Superseded in part (2026-08-19):** the user ordered the entire
> automated suite deleted (commit 4883a19) and forbade new test lines
> unless every expectation is formally proven from the native contract or
> runtime evidence. The enumeration below is retained as history; the
> live instrument is the runtime proof obligations that follow (trace
> builds, loader log, operator runs).

Automated tests (loader-owned only, each with an independent oracle):

1. Step-list construction: boundary enumeration across arbitrary
   `(last, now]` windows, edge merging, dedup, ordering, frame assignment
   `⌊t/16.666666⌋` — expected values derived by hand-written fixtures of
   the arithmetic, not by the scheduler.
2. QPC→song-ms anchor arithmetic: overflow/backwards checks with
   independently computed expectations.
3. Journal ordering/losslessness invariants under concurrent publish.
4. Patch transaction: byte guards reject, rollback on induced failure,
   60-FPS gate refuses to install.
5. Assertion/abort behavior for the impossible states of Section 9.

Explicitly invalid as gameplay evidence (not written): fake recognition or
score callbacks, copied native matcher/descriptor models, loader-side note
routing in tests, FPS matrices whose expectations come from the same
policy under test, source-grep tests.

Runtime proof obligations (separate claims, kept separate):

- **Native-process instrumentation** (developer run): log per step —
  caller, exact ms, frame, slot masks written, pair-call order, grade and
  score publication — and verify: the 10000/9999 identity; per-family
  behavior for a scripted chart covering every effective type; boundary
  cadence; recapture behavior; watchdog tolerance with Section 5.4.
- **Cabinet/operator**: 60 (unpatched path), 144, 165, 240 FPS; input feel
  at nominal and stuttered play; comparison of per-note results across
  FPS for the same recorded input sequence.

## 11. Gate resolutions (closed 2026-08-19, disassembly session)

The six gates named in the first draft are resolved; nothing in this
section is an unknown anymore. What remains for the implementation plan
are **engineering** items, not design decisions:

1. **ABI + tail entry — closed.** Core thiscall `[ms, frame]` `retn 8`;
   score thiscall `[ms]` `retn 4`; capture virtual thiscall `(booster,
   frame)`; tail entry `0x6402D0`. Byte-exact guards and trampolines are
   plan work.
2. **Recapture + ring — closed.** Fresh frame overwrites; same/old frame
   ORs (`0x62D940`/`0x62D920`). Ring capacity is authored and unchanged
   because frame rate never changes. Plan work: byte guards at the two
   detour sites.
3. **Step writer — closed.** Existing seam (`FrameratePatch.cpp:1638`,
   clock at `:2210`); authored conversion = `Create(60, 1)`, backlog
   cap 3. Plan work: the rate change and its config validation.
4. **Watchdog vs clock patch — closed.** Independent (watchdog reads
   `Tune+0x10/+0x14`, never the clock fields); clock-init exact-now is in.
   Instrumentation checkpoint: watchdog tolerance/resync constants behave
   as authored.
5. **Consecutive-held — closed.** Guard locked in (§5.3) on the evidence
   of matcher thresholds 1 and 4. Plan work: the `0x62DC60` detour and its
   cached-state assertions.
6. **Call-count sensitivity — closed statically.** Core per-call
   mutations are caches and condition-driven work; score accumulation is
   grade-event-driven with consume-advance cursors, inert without new
   grades by native construction (the 60 FPS game depends on the same
   property). Extra pair-calls at edge times are the native catch-up
   pattern (E-043). Runtime instrumentation validates per §10 — as
   evidence, not as an open question.

Plan-phase engineering checklist (no design content): trampoline bytes
and guard values for sites 1–4 of §5.5; gameplay-active suppression flag
lifecycle; journal→song-ms anchor module layout; CMake wiring; worktree and
branch mechanics per §3.

## 12. Out of scope

- Render interpolation, scroll visuals beyond Section 5.4, audio backend
  work, and the unrelated ASIO changes (preserved untouched).
- Any change to judgement window values, `FreeTapDisable*` gates, note
  normalization, or any authored gameplay constant.
- Any loader answer to a native input query.

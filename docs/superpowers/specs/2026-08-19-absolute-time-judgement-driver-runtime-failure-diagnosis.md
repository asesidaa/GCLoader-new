> **RETAINED FAILURE EVIDENCE — NOT AN IMPLEMENTATION DESIGN.** Use this to
> prevent recurrence of the diagnosed failure modes.

# Absolute-Time Judgement Driver Runtime-Failure Diagnosis

**Date:** 2026-08-19
**Scope:** Design and implementation review only. No source change, build,
deployment, or gameplay-acceptance claim is part of this record.
**Runtime evidence:** `H:\gc\loader-log.txt`
**Native pipeline evidence:**
`H:\gc\artifacts\GCLoader\.planning\debug\high-fps-timing-domains\evidence`

**Evidence reuse rule:** The audit in that directory is already complete.
E-042 through E-046 and the evidence index are the authoritative native
pipeline contract for this redesign. Do not repeat the broad input/judgement
audit or broadly re-decompile the same pipeline. Consult those records first;
use new focused IDA work only if design discussion exposes a specific question
that the existing evidence demonstrably does not answer, and record that gap
before investigating it.

## Verdict

The current corrected design is not coherent enough to implement as written.
This is not only a local hook defect:

1. the plan retains two mutually exclusive input contracts (historical
   state-as-of capture versus later live capture);
2. the corrected live-capture contract cannot reconstruct the physical state
   at a journalled historical timestamp;
3. the driver and the native Tune cadence use timelines with different phase;
4. native fill advances the input-manager frame before the driver decides
   whether a frame still needs a physical capture.

The last two implementation mechanisms can directly suppress boundary
judgement work and live ring writes. The first two mean that fixing those
mechanisms alone still would not establish the required
`10000 ms note / 9999 ms input = 1 ms` contract.

## What the latest log proves

`loader-log.txt` proves only startup and coarse runtime health:

- target 240 FPS and authored 16.6667 ms gameplay timing were selected;
- the framerate transaction committed;
- all four judgement-driver hooks installed;
- the Switch query hooks installed;
- the 1000 Hz input worker started, XInput slot 0 was unavailable, and
  keyboard/system input explicitly remained enabled;
- WASAPI ran at 48 kHz with a 10 ms buffer, and the external cap validated at
  240.168 FPS;
- no fatal conversion or journal-overflow message was emitted.

The deployed `data/system.cfg` has both `JudgTimeOffset` and
`GameTimeOffset` set to zero, so a configured judgement/song offset does not
explain this run.

It does **not** prove that any judgement step was built, that a native ring
slot received a fresh sample, or that the recognition/score pair ran. The
per-step trace is compile-gated and OFF by default. Gameplay-specific counters
first become nonzero at 21:28:04 and foreground is lost at 21:28:07, so this
log contains only a short gameplay observation window.

## Finding 1: the authoritative plan is internally contradictory

The plan's architecture summary and Task 6 still require setting an as-of
context around capture. Task 4 is then marked superseded and says the as-of
seam was deleted; the replacement uses the native ring and live captures. The
current design spec repeats the replacement contract.

These are not equivalent implementations. One requires materializing
`state(t)` from journal history. The other samples `state(now)` when the game
thread eventually processes an event. The plan therefore no longer defines a
single input-state contract that the implementation can satisfy.

The plan also contains an arithmetic contradiction: it first locks the native
float value `16.6666660308837890625`, then calls that value exactly `50/3 ms`.
Those numbers are different. The current scheduler implementation correctly
uses the native-float rational `1092266625/65536` in 1/1000-ms units, while
`GameplaySongClock::Create(60, 1)` advances on exact 60 Hz. That rate difference
is small (about 0.137 ms per hour), but integer truncation already differs at
frame 3 (`49` versus `50` ms). It is not the immediate blackout mechanism; it
is further proof that the design must explicitly define which grid governs
crossing, containing-frame classification, and the ms passed to native code.

## Finding 2: a live sample cannot satisfy a historical edge step

Each journal record already contains `held_before`, `held_after`, `rising`,
and `falling`, plus the transition QPC. The current driver maps only the QPC
to a step time and discards all four state masks. At that later step it calls
the native live sampler.

Therefore, for a transition at `t` processed at `p > t`:

- a short press-and-release completed before `p` samples released state;
- a worker transition published after the current outer update's native input
  aggregate poll can sample the aggregate from before the transition;
- after that one edge step is consumed, there need not be another journal
  record while the control remains held, so a later fresh sample is not
  guaranteed.

This violates the spec's own requirement that the ring slot reflect the
physical state **the step judges at**. A timestamp can schedule historical
work, but it cannot make a later live sample historical. Consequently, the
corrected design cannot prove lossless transitions or the exact-time identity
even if all hooks execute in the intended order.

## Finding 3: native fill pre-empts the driver's capture advance

The proven native Tune order is:

1. `0x664DDC` calls `0x659860` to set the input-manager current frame to
   `Tune+0x10 + Tune+0x14`, filling skipped slots;
2. `0x664E06` calls judgement (`0x6401E0`), where the driver hook runs;
3. `0x664E23` commits `Tune+0x10 += Tune+0x14`.

The fill path propagates the prior held mask and creates no new pressed edge.
By the time `RunJudgementFrame` examines a scheduled boundary
`base+1 ... base+frames_crossed`, the input-manager frame is already
`base+frames_crossed`. Its capture loop is guarded by
`while (input_frame < step.frame)`, so it normally performs zero calls for
every one of those boundary steps.

The driver-owned `LoopLast` path has the same predicate against the already
advanced manager frame and then skips the original `0x659920` body. Thus the
manager frame can say a slot is current while CBooster contains only propagated
held masks, not a fresh physical sample. Alignment-reset capture and explicit
same-frame recapture are exceptions; they do not restore the promised
per-boundary live-capture invariant.

`ScheduledStep::recapture` also cannot repair this. It is classified from
`frame <= Tune base` or duplicate scheduled frames, rather than from the
actual CBooster last-captured frame/ring ownership after native fill.

This is the strongest static cause of the reported input blackout.

## Finding 4: timeline-origin capture destroys authored-frame phase

`CaptureFrameTimelineOrigin(anchor, base_frame)` computes:

```text
correction = song_time(anchor) - authored_boundary(base_frame)
mapped(qpc) = song_time(qpc) - correction
```

It therefore forces the arbitrary first anchor observation to be the exact
start of `base_frame`, discarding its fractional position inside the authored
16.666666 ms frame.

The native song clock independently derives the Tune step from the absolute
audio cursor (`desired = floor(song_time / F)`). If the first anchor is
`phi` milliseconds after its frame boundary, at the next real audio boundary:

```text
native Tune now: boundary(g+1)
driver mapped now: boundary(g+1) - phi
```

The scheduler rejects frame `g+1` because its boundary is greater than the
driver's mapped `now`. Native Tune nevertheless commits that frame after
judgement returns. On the following update, the scheduler enumerates only the
new `base+1 ... base+step` range, so the omitted frame is never replayed. The
two clocks then advance at the same rate with a permanent phase offset, and
the same rejection can repeat at every boundary.

Session start makes the first loss easier: the driver sets
`last_processed_t = now` before building the first step list, so every current
boundary and drained edge is intentionally outside the new empty window while
native Tune can still commit `frames_crossed`.

This is the strongest static cause of the reported absence of judgement and
miss progression. The latest log does not expose the first anchor, Tune base,
step, mapped now, or built-step count, so the exact runtime values remain to be
confirmed rather than claimed from the log.

## Combined failure chain

```text
audio cursor crosses authored boundary
  -> native clock sets Tune+0x14
  -> native 0x659860 advances input-manager frame with held-only fill
  -> driver compares the same boundary against a phase-shifted mapped now
  -> boundary can be filtered and is still committed by native Tune
  -> no recognition/score call for that boundary, hence no miss progression

worker journals transition at exact QPC
  -> driver retains timestamp but discards transition state
  -> any edge step samples current/stale live native aggregate
  -> normal capture advance is already suppressed by the manager-frame value
  -> CBooster pressed slot may never contain the transition
  -> note handlers observe no pressed edge
```

The two chains reinforce each other but are independently invalid invariants.

## Design requirements before any code fix

This record deliberately does not select an implementation. A redesign must,
at minimum:

1. define one authoritative, phase-preserving song-time coordinate shared by
   Tune step production and driver step filtering;
2. define how journalled `held_before/held_after/rising/falling` becomes the
   exact state observed by the native CBooster capture/ring at `t` (or provide
   a proof-equivalent mechanism); sampling live state later is insufficient;
3. base capture ownership on actual CBooster history state, explicitly
   accounting for the earlier `0x659860` held-only fill, rather than treating
   the input-manager current frame as proof of a physical capture;
4. preserve the proven native non-replay rule: a pressed edge exists only in
   its actual captured history frame, and fill cannot synthesize it;
5. reconcile or supersede the stale as-of requirements still present in the
   authoritative plan before implementation resumes.

## Confidence boundary

- **Confirmed statically / by existing IDA evidence:** native call order;
  held-only fill semantics; pressed-edge non-replay; plan/spec contradiction;
  discarded journal masks; capture-loop predicate; phase-removing origin
  arithmetic; scheduler filtering and non-replay of omitted boundaries.
- **Correlated with the runtime report, not yet directly traced:** the exact
  first-anchor phase, number of built steps, capture counts, pair-call counts,
  and the precise transition lost in the 2026-08-19 run.
- **Not claimed:** a fixed design, a code fix, build correctness after a fix,
  or cabinet/gameplay acceptance.

## Primary evidence pointers

- `docs/superpowers/plans/2026-08-19-absolute-time-judgement-driver.md`:
  architecture summary; superseded Task 4 note; still-stale Task 6 capture
  acceptance criteria.
- `docs/superpowers/specs/2026-08-19-absolute-time-judgement-driver-design.md`:
  Sections 5.1 and 5.2, especially the corrected live-capture contract.
- `src/Patches/JudgementTiming/JudgementTimingDriver.cpp`:
  `RunJudgementFrame` origin/window setup, journal-to-time mapping, capture
  advance loop, and `OnAdvanceCapture`.
- `src/Patches/JudgementTiming/JudgementTimeline.cpp`:
  `CaptureFrameTimelineOrigin`.
- `src/Patches/JudgementTiming/JudgementStepScheduler.cpp`:
  boundary filtering and recapture classification in `BuildStepList`.
- `.planning/debug/high-fps-timing-domains/evidence/E-043-native-catchup-loader-scope-audit.md`:
  "Native frame and catch-up order."
- `.planning/debug/high-fps-timing-domains/evidence/E-046-native-normalization-progression-closure.md`:
  "Multiple catch-up steps and edge uniqueness."

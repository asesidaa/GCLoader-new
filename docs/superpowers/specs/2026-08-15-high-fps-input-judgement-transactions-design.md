> **ARCHIVED FAILED DESIGN — NOT AUTHORITATIVE.** Later corrections did not
> rescue this architecture in actual gameplay.

# High-FPS Input Judgement Transactions

**Date:** 2026-08-15
**Status:** Approved design; ready for implementation planning
**Binary evidence target:** H:\gc\game471.exe.i64

> **Runtime supersession:**
> [High-FPS One-Shot Input Lifetime Correction](2026-08-15-high-fps-one-shot-input-lifetime-correction-design.md)
> supersedes this document's independent note/free-tap lifetime,
> handler-result commit, and physical-grade-retiming contracts. The transition
> journal, immutable snapshot, exact history, late-gate, Switch, capacity, and
> 60 FPS no-op contracts remain applicable.

## Supersession

This design supersedes
[2026-08-10-high-fps-input-transition-bridge-design.md](2026-08-10-high-fps-input-transition-bridge-design.md).

The old exact-game-frame mapper, query-driven multi-pass carry, and its expiry
policy are runtime-disproved designs. They must be removed or replaced rather
than left underneath this design. The following pieces may be retained after
their contracts are updated:

- physical transition capture;
- QPC timestamps, sequence numbers, and full held/rising/falling masks;
- guarded transactional hook installation;
- fixed-capacity storage and bounded diagnostics.

## Problem

At 60 FPS, a physical press can occur anywhere between two game samples and
remain observable at the next sample, up to approximately 16.667 milliseconds
later. At high target rates the game performs more judgement passes, but its
input and note logic still contains assumptions tied to the original 60 Hz
sampling model. Short presses can therefore be observed by the input stack yet
not be used by a relevant note handler.

Runtime traces disproved the narrower theory that input was merely assigned to
an unqueried high-FPS frame:

- all 428 post-seed rises in the representative trace became effective
  frame edges;
- no overflow, fallback, missing lookup, or bridge invariant failure occurred;
- keeping an edge visible for four high-FPS passes helped only slightly;
- that carry shifted recognition by 0 to 12.5 milliseconds at 240 FPS and made
  judgement grades visibly unstable.

The game also has two conceptually different uses of time:

1. recognition time drives candidate selection, timing-window gates, note
   state, miss processing, holds, repeats, and duration mechanics;
2. the physical event time should grade a newly pressed input once the native
   logic accepts it.

Replacing the core song time globally would retime the chart and every active
note. Preserving an edge without preserving its event time would recreate the
unstable carry experiment. The correction must therefore preserve native
recognition semantics while making a physical edge reliably available and
using its original timestamp only at the narrow seams where it is valid.

## Goals

1. Eliminate high-FPS short-input drops without changing the chart or global
   song timeline.
2. Preserve original 60 FPS acceptance and forgiveness rules.
3. Grade a newly observed edge from its physical event time, independent of
   which high-FPS pass consumes it.
4. Give every input query within one judgement transaction a coherent,
   immutable view.
5. Preserve all existing Arcade and Switch gameplay semantics, including
   paired notes, diagonals, and selectable free-tap hit sounds.
6. Give frame-history predicates their exact original 60 Hz duration at
   integer and non-integer target-rate ratios.
7. Keep the hot path bounded, allocation-free, and operationally simple.
8. Make target_fps = 60 behaviorally identical to the native timing path,
   apart from passive validation diagnostics.

## Non-Goals

- Rewriting the chart scheduler, note state machines, or global judgement
  system.
- Globally replacing the core time passed through 0x5D68E0.
- Changing judgement windows or making the game stricter.
- FPS-scaling millisecond mechanics.
- Adding new system.cfg controls, watchers, or reload handling.
- Changing menus, test mode, raw FastIO data, physical bindings, or input
  backends.
- Perfect recovery from hypothetical clock corruption or impossible transition
  rates.
- Inferring transition order that the physical input backend did not publish.

## Binary Model

### Core flow

The core judgement loop at 0x5D68E0 receives the current song milliseconds and
gameplay frame. It applies JudgTimeOffset, performs an outer early-eligibility
gate, dispatches eligible notes through 0x5D5720, and later invokes free-tap
processing at 0x5D2040 when the native suppression state permits it.

The outer gate is intentionally left on recognition time. A press that occurs
slightly before a note becomes dispatchable waits in bounded pending
availability until a later native pass opens the gate.

The shared helper at 0x5D0BE0 is a late/miss gate used by normal, flick, hold,
scratch, beat, slide-hold, and dual-hold paths. It has an observable side
effect through 0x43C820 when it reports the late/miss condition. A
transaction-scoped event-time-valid decision may suppress that late result,
but native recognition-time success can never be turned into failure.

### Input primitives

The audited gameplay handlers use four input primitives:

- pressed at 0x659640;
- held at 0x659570;
- held age at 0x6594D0;
- direction at 0x659390.

The high-FPS overrides apply only while a scoped gameplay judgement
transaction is active. Calls from menus, test mode, and unrelated systems
continue to observe native input behavior.

### Grade and duration seams

Timing grade helper 0x5D0E00 has exactly two direct callers in the audited
binary:

- 0x5D1F2A in normal-button handler 0x5D1D50;
- 0x5D34C5 in flick handler 0x5D3320.

Only those timing-graded paths receive the physical-event delta.

Duration helper 0x5D04F0 is used by HOLD, SCRATCH, BEAT, SLIDE HOLD, and DUAL
HOLD. It remains entirely on recognition/lifecycle time. The correction does
not retime hold length, repeat cadence, scratch duration, or slide
continuation.

## Selected Architecture

The selected design is a type-aware immutable judgement transaction backed by
a fixed-capacity transition history and bounded physical-edge availability.
It has eight cooperating parts:

1. A transition producer records monotonic QPC, sequence, changed masks, and
   the complete post-transition logical held state.
2. A newest-wins fixed-capacity ring provides exact state-at-time and
   edge-at-time queries without hot-path allocation.
3. A core judgement scope freezes one immutable transaction snapshot.
4. An effective input policy composes native Arcade input, high-FPS temporal
   correction, and the already-defined Switch gameplay aliases.
5. A pending-edge view exposes a physical rise to note handlers for no more
   than 1 / 60 second while presenting it to free tap at most once.
6. A non-shrinking eligibility policy combines native recognition acceptance
   with event-time validity.
7. A grade override applies an event-versus-recognition delta only after an
   edge-triggered hit is accepted.
8. Activation, epoch reset, transactional hook installation, and bounded
   diagnostics contain lifecycle and failure behavior.

This is one correction system. There is no secondary frame-carry layer.

## Authoritative Invariants

### Recognition clock and physical clock

Recognition time remains authoritative for:

- candidate selection and the outer early gate;
- native timing-window acceptance;
- input suppression;
- held and history matching;
- miss processing and note state transitions;
- paired-note aggregation;
- duration and repeat mechanics;
- chart, video, and audio presentation.

A qualifying physical edge may affect only:

- bounded pressed-edge availability;
- non-shrinking late eligibility;
- the final timing-grade argument after acceptance;
- diagnostic association.

The physical event time is represented as a QPC delta from the transaction's
recognition QPC. The delta is applied to the game's already-adjusted time value,
not used as a replacement absolute clock. This preserves live JudgTimeOffset,
GameTimeOffset, and MERRY GO ROUND segment adjustment without loader-side
offset caching.

Physical grading is subordinate to accepted-hit state. The implementation must
not allow the grade helper's representation of an outlying physical delta to
turn a natively accepted hit into a miss. The supported binary's exact grade
encoding must be characterized before the override is written; when native
recognition supplied the acceptance, the final result must remain within its
successful result domain.

### Non-shrinking eligibility

The acceptance rule is:

native recognition-time acceptance OR a retained physical edge was valid at
its event time.

This is deliberately one-way:

- native success always remains success;
- an event-time-valid edge may rescue a later recognition that crossed a late
  boundary;
- event time never tightens a native window;
- a historical held-state match with no qualifying new edge remains graded at
  recognition time.

The outer early gate continues to use recognition time. The 1 / 60-second
pending interval lets a physically early input survive until that gate opens.
The shared late/miss seam uses the selected edge's event time when necessary so
the later dispatch does not convert the preserved input into a miss.

### Original forgiveness

Timestamp selection follows these rules:

1. A newly observed edge that directly triggers a match uses that edge's
   physical time.
2. A multi-component input completed by a new component uses the completion
   edge: the latest required new edge.
3. A native already-held or recent-history acceptance with no new qualifying
   edge uses recognition time.
4. Hold and slide continuation use current authoritative song time.
5. A journal edge is never substituted merely because it exists if doing so
   would remove native pre-hold forgiveness or move a history-accepted result
   earlier.

### Immutable transaction

Every query during one core judgement call observes:

- one recognition QPC and game-time context;
- one current full held snapshot;
- one reconstructed historical snapshot;
- one fixed set of current and pending rises;
- one edge timestamp/cohort selection per logical component;
- one Arcade or Switch policy mode.

Input queries do not mutate this view. They record proposed edge associations
in a separate commit record. Associations and note-consumption marks are
committed only after the whole transaction completes. Consequently, all
components of a chord, paired note, or nested handler see the same state even
when one component is queried first.

Pending eligibility and expiry are sampled when the transaction begins. An
edge cannot disappear halfway through a transaction merely because QPC crosses
its 1 / 60-second boundary while nested handlers are running.

Native note-state and suppression side effects are not deferred; only the
loader's pending-edge consumption is transactional.

## Transition History

Each fixed-size ring entry contains:

- monotonically increasing sequence number;
- monotonic QPC timestamp;
- full post-transition held mask;
- rising mask;
- falling mask;
- gameplay epoch.

The producer appends only published logical-state changes. Multiple changed
bits in one published sample form one cohort and share a timestamp. Separate
published samples retain their real QPC order.

The design cannot recover transitions that the underlying backend itself
coalesced before publication. For example, a down-up-down sequence of the same
logical control entirely between two backend publications may appear as no
change or one final change. Simultaneous keys published in one cohort are
intentionally simultaneous; this is correct for chords, dual-direction notes,
and both-button notes.

History retains the order of every transition that was published. The pending
pressed view nevertheless exposes at most one still-unconsumed rise per
logical control during one 1 / 60-second sampling opportunity. If the same
control rises more than once before either rise is consumed, the newest rise
supersedes the older pending opportunity and the coalescence is counted.
Different logical controls never coalesce. This matches the representational
limit of one native rising edge per logical control per 60 Hz sample while
preserving simultaneous chords and paired boosters.

### Frame-history reconstruction

- Current held queries use the transaction's immutable current snapshot.
- Native current_frame - 2 history is reconstructed from the state at
  recognition QPC minus exactly 2 / 60 second.
- Held age is monotonic elapsed time expressed in a synthetic 60 Hz domain.
  The native age <= 4 rule expires after exactly 4 / 60 second.
- Arithmetic uses QPC ticks and rational comparisons. It never rounds to a
  number of 144, 165, 240, or other target-rate render frames.
- Startup state is seeded as pre-held and does not generate a pressed edge.
- Song/gameplay epoch changes clear and reseed history.

### Capacity and eviction

The ring is sized to exceed the required 4 / 60-second history interval at
supported input rates. If full:

1. append the newest transition;
2. evict the oldest transition;
3. increment a cumulative counter;
4. emit only a rate-limited anomaly log.

There is no mode switch, rollback, delayed new input, or patch shutdown. A
query older than retained history uses the oldest available full state. An
evicted rise is considered older than the recent window. The visible abnormal
result may be lost forgiveness or an actual missed input, which makes the
capacity fault apparent while continuing to prioritize current input.

## Bounded Physical-Edge Availability

A newly published physical rise has two independent observation states.

### Note availability

The rise remains eligible for note-handler pressed queries until the earliest
of:

- it is associated with a relevant note handler and committed consumed after
  that immutable transaction;
- exactly 1 / 60 second has elapsed since its QPC event timestamp;
- the gameplay epoch resets;
- its history entry is evicted.

The interval is the maximum sampling delay of the original 60 FPS loop. It is
not a future-note input buffer and does not search arbitrarily ahead.

Only edges actually selected by a relevant handler are marked for consumption.
An unrelated query or rejected component does not consume other pending edges.
All selected edges stay visible through the end of the current transaction.

### Free-tap presentation

The rise is presentation-eligible at the first native free-tap opportunity and
at most once. This does not bypass the core's native free-tap suppression or
force a sound when note processing already suppresses it.

Presentation does not consume note availability. Conversely, keeping a rise
available to note judgement does not replay its selectable hit sound on later
high-FPS passes.

### Why this differs from carry

The failed carry experiment treated a later high-FPS pass as the hit time. The
resulting grade moved with recognition.

Bounded availability retains the original event QPC. A later eligible note
query uses the same event-time delta it would have used immediately. Render
rate and the number of intervening passes therefore do not move the grade.

## Effective Input Policy

The effective gameplay view composes in this order:

1. native physical/logical Arcade state;
2. high-FPS transaction history and bounded edge correction, when target FPS
   is greater than 60;
3. existing Switch gameplay aliases, when Switch style is selected.

The correction never writes transformed values back into raw FastIO state.

### Four operating modes

| Target | Style | Behavior |
|---|---|---|
| 60 FPS | Arcade | Native judgement behavior; passive validation only |
| 60 FPS | Switch | Native timing plus the existing Switch gameplay aliases |
| Above 60 FPS | Arcade | Corrected transaction, history, availability, eligibility, and grade |
| Above 60 FPS | Switch | The same temporal correction, followed by the locked Switch aliases |

At target_fps = 60 the new high-FPS correction performs no input override,
pending-edge rescue, eligibility override, or grade adjustment. Passive
diagnostics may observe results. Switch style remains independently selectable
and is not disabled by this no-op rule.

### Locked Switch rules

1. Every newly pressed direction may act as a center-button pressed edge on
   the same booster. Pressing a second direction while one remains held may
   create another same-booster button edge.
2. A booster button is held while its real button or any direction on that
   booster is held.
3. Either adjacent cardinal direction satisfies a diagonal target for both
   initial and continuation judgement.
4. Real buttons, exact diagonals, native cardinal matches, and all unrelated
   Arcade successes remain valid.
5. These transformations are gameplay-only.

Each booster applies the rule independently. A CRITICAL may be completed by
the eligible real button or direction alias on each booster. A Switch diagonal
does not become a requirement to press both physical cardinal components.

## Note-Type Coverage Matrix

All audited note IDs and the free-tap path are in scope. Shared seams are used
where possible, but each row is an explicit acceptance obligation.

| ID | Path and input | Transaction and timestamp rule | History/mechanic rule | Switch and verification |
|---:|---|---|---|---|
| 0 NONE | No dispatcher input | No edge association or override | Native lifecycle only | Prove no query/hook effect |
| 1 NORMAL | 0x5D5720 to 0x5D1FA0 to 0x5D1D50; buttons 4/9 pressed | Bounded edge, non-shrinking eligibility, physical grade at 0x5D0E00 | Recognition drives candidate/state | Same-booster direction alias; unit and cabinet tap tests |
| 2 FLICK | 0x5D5720 to 0x5D3320 to 0x5D2E50; held, age, frame-2 state, direction | New completion edge uses physical grade; history-only success uses recognition | Exact 2/60 and 4/60 history rules | Either adjacent cardinal for diagonal; direction/chord tests |
| 3 HOLD | 0x5D5720 to 0x5D41B0; pressed head, held body | Head uses bounded selected edge; body uses current state | Duration remains recognition time; HoldSafeFrame stays 0 | Button/direction head and sustain aliases; head/release tests |
| 4 SCRATCH | 0x5D5720 to 0x5D3C60; four directional pressed inputs | Relevant rises are bounded and may rescue late eligibility; no grade-helper retime | ScratchEnableTime remains native 250 ms; duration on recognition | No added center alias; direction and repeat tests |
| 5 BEAT | 0x5D5720 to 0x5D3920; repeated button pressed | Each distinct selected rise retains event identity; no grade-helper retime | BeatEnableTime remains native 200 ms; cadence on recognition | Same-booster direction aliases; repeat and no-replay tests |
| 6 MERRY GO ROUND | 0x5D5720 to 0x5D5660 to normal handler; offset button pressed | Normal bounded edge and grade delta applied to the already segment-adjusted argument | Segment selection remains recognition driven | Same-booster alias; segment-offset grade test |
| 7 HIDDEN | Normal handler; button pressed | Same as NORMAL | Native hidden lifecycle | Same as NORMAL; explicit type-ID test |
| 8 HIDDEN2 | Normal handler; button pressed | Same as NORMAL | Native hidden lifecycle | Same as NORMAL; explicit type-ID test |
| 9 CRITICAL | 0x5D5720 to 0x5D1F70 to normal handler; paired booster components | One immutable snapshot; when both complete in one transaction use latest selected completion edge; across transactions preserve native stored component state without retroactive rewrite | Paired aggregation/miss remains recognition driven | Alias independently per booster; real, mixed, simultaneous, and staggered tests |
| 10 SLIDE HOLD | 0x5D5720 to 0x5D35C0 to direction matcher; head and continuation | New head/completion edge may use physical association; held/history-only acceptance remains recognition based | Exact direction history; lifecycle on recognition; SlideHoldSafeFrame stays 0 | Adjacent cardinal satisfies diagonal head and continuation; sustain tests |
| 11 SLIDE COUNTER | No independent input query in dispatcher | No direct edge association | Native lifecycle marker | Whole-binary query audit and regression observation |
| 12 TURN | No independent input query in dispatcher | No direct edge association | Native lifecycle marker | Whole-binary query audit and regression observation |
| 13 SPIN | No independent input query in dispatcher | No direct edge association | Native lifecycle marker | Whole-binary query audit and regression observation |
| 14 FINISH | No independent input query in dispatcher | No direct edge association | Native lifecycle marker | Whole-binary query audit and regression observation |
| 15 DUAL HOLD | 0x5D5720 to 0x5D5540 to HOLD path; paired pressed heads and held bodies | Full immutable paired snapshot; selected heads consumed together after transaction; no duration retime | Recognition lifecycle; HoldSafeFrame stays 0 | Alias independently per booster; mixed head/sustain/release tests |
| - Free tap | 0x5D2040 after note processing; buttons 4/9 pressed | One-shot presentation view separate from note consumption; no grade override | Native suppression and sound selection remain authoritative | Same-booster aliases; prove no duplicate sound while edge is pending |

Types 11 through 14 remain classified as lifecycle-only because the audited
dispatcher and whole-binary input-wrapper references show no independent input
query. Implementation verification must repeat that static assertion against
the supported binary before enabling hooks.

## Multi-Component and Paired Inputs

For any multi-component match:

- all components are evaluated from the same immutable snapshot;
- real held state, current rises, retained pending rises, and history are
  distinguished rather than flattened;
- when new edges complete the input, the selected event time is the latest
  required edge;
- components accepted only through native held/history forgiveness do not gain
  a historical physical grade;
- a single cohort keeps equal timestamps for genuinely simultaneous inputs.

For CRITICAL and DUAL HOLD, both booster subqueries finish before pending
consumption commits. If one component was already stored by native logic in a
prior transaction, the later completion preserves that native state flow. The
loader does not revisit or regrade the earlier partial component.

## Hook and Runtime Boundaries

Implementation must establish scoped, nest-safe context around the core
judgement operation and relevant handler/type dispatch. The context carries
the immutable transaction and currently evaluated note/component identity.
Only calls made inside that scope consult corrected input.

Required seams are:

- transition publication or existing journal append;
- core transaction begin/end around the 0x5D68E0 judgement operation;
- type/handler scope around 0x5D5720 and nested paired handlers;
- pressed, held, held-age, and direction primitives;
- shared late/miss helper 0x5D0BE0 with selected-edge context;
- grade helper 0x5D0E00 with caller/type validation;
- free-tap scope at 0x5D2040;
- song/gameplay epoch activation, reset, and deactivation.

Every binary mutation remains signature-guarded and transactional. A missing or
ambiguous signature aborts activation before any partial hook set becomes
visible. Clean and already-patched states must both be recognized according to
existing patch conventions.

No correction is active merely because an input primitive is called. Gameplay
scope, supported binary validation, high-FPS activation, and a valid current
transaction are all required.

## Static Configuration Boundary

Only JudgTimeOffset and GameTimeOffset may change at runtime through the
existing test-menu patch. The design does not read, cache, reload, or expose
the other system.cfg values.

Supported cabinet constants are:

- HoldSafeFrame = 0;
- SlideHoldSafeFrame = 0;
- ScratchEnableTime = 250 milliseconds;
- BeatEnableTime = 200 milliseconds.

Zero safe-frame values require no scaling or emulation. Scratch and beat times
are already milliseconds and remain untouched.

## Diagnostics

Normal logging is deliberately bounded:

- one activation line with target FPS, style, capacity, and hook status;
- one end-of-song summary;
- rate-limited anomaly lines for eviction or invariant failure;
- no per-frame or per-query spam.

The summary contains at least:

- captured transitions and rises;
- note-associated and free-tap-presented edges;
- physical-grade and history-only acceptances;
- event-time eligibility rescues;
- pending expiries;
- duplicate-presentation suppressions;
- ring evictions.

An explicit diagnostic mode may emit:

1. one compact line per physical rise;
2. one compact association line when that rise is used by a note or free tap;
3. one miss-context line when a relevant recent edge existed.

Association fields include sequence/cohort, note type and component, physical
source, effective Arcade/Switch source, reason (edge, held, or history),
event-recognition delta, applied grade delta, eligibility path, and result.
Miss context lists recent relevant retained edges and the transaction snapshot
exposure. Logs must make physical capture, presentation, note consumption, and
grade selection distinguishable.

At 60 FPS, diagnostics may passively record native observations but must not
alter query results or timing.

## Failure Handling

The implementation handles common failures directly:

- startup or activation seeds held controls without synthetic presses;
- epoch reset clears pending associations and reseeds state;
- fixed-ring overflow evicts oldest and reports it;
- missing transaction context returns the native result;
- unsupported or mismatched patch sites prevent activation transactionally.

It does not add complex recovery modes, background repair, dynamic allocation,
or alternative judgement algorithms. If the ring is overflowing in normal
play, the counter and rate-limited log make the abnormal input loss visible and
the capacity must be corrected.

## Automated Verification

### Pure transition/history tests

- ordered QPC transitions and full post-state reconstruction;
- simultaneous multi-bit cohorts;
- state-at-time queries at boundaries;
- exact 2/60 and 4/60 rational comparisons;
- startup pre-held seeding;
- epoch reset;
- newest-wins eviction and visible counter;
- no hot-path allocation.

### Pending-edge tests

- visible immediately and through, but not beyond, exactly 1/60 second;
- identical behavior at 120, 144, 165, 240, 360, and representative arbitrary
  rates above 60;
- note consumption commits after the full immutable transaction;
- unselected edges remain pending;
- free tap presents once without consuming note availability;
- pending note availability never replays free-tap sound;
- expiry and eviction remove eligibility;
- event timestamp remains fixed across later high-FPS passes.

### Policy and note tests

- every matrix row: IDs 0 through 15 plus free tap;
- pressed, held, held-age, direction, and frame-2 query coherence;
- Arcade native success preservation;
- every locked Switch alias and gameplay-only boundary;
- same-booster second-direction edge while another direction is held;
- exact diagonal and either-adjacent-cardinal matching;
- simultaneous and staggered CRITICAL components;
- DUAL HOLD paired head, sustain, and release;
- latest-completion-edge selection;
- history-only forgiveness graded at recognition time;
- one published cohort for multi-key chords;
- same-logical down-up-down within one pending sampling opportunity
  coalescing to the newest single pressed opportunity;
- backend-coalescing limitation documented, not falsely reconstructed.

### Timing and eligibility tests

- native recognition success remains success;
- event-time validity rescues only a late recognition;
- event time never rejects a native acceptance;
- an early edge waits for the recognition-time outer gate and expires after
  the bounded interval;
- grade delta is applied to the existing helper argument;
- physical grading cannot turn an already accepted native hit into a miss;
- JudgTimeOffset and GameTimeOffset compose without cached copies;
- MERRY GO ROUND segment adjustment is preserved;
- duration and repeat helpers receive unchanged recognition time;
- target_fps = 60 produces native results and zero correction.

### Hook and build tests

- supported signatures and caller inventory;
- clean, already-patched, and mismatched binary sites;
- transactional rollback on activation failure;
- nested handler/type context restoration;
- scope exclusion for menu, test mode, and raw input paths;
- x86 Debug and Release full preset graph;
- existing input, Switch, framerate, audio, and patch transaction tests.

## Runtime Acceptance

Static analysis, unit tests, and successful builds prove implementation
integrity but not cabinet behavior.

Runtime validation order is:

1. deploy a verified build to H:\gc;
2. run at 240 FPS first with bounded diagnostic mode;
3. stop and investigate before the 60 FPS comparison if any miss, duplicate
   sound, unstable grade, hold/repeat regression, or anomaly appears;
4. after 240 FPS is acceptable, run the same material at 60 FPS;
5. compare Arcade and Switch paths, short and long notes, paired notes,
   diagonals, free taps, repeated inputs, and representative offsets.

Success requires:

- no observed short-input drops at high FPS;
- original forgiving acceptance retained;
- physical grades stable across which high-FPS pass consumes the edge;
- no obvious judgement-window shift;
- no duplicate selectable hit sounds;
- unchanged hold, slide, scratch, beat, and paired-note lifecycle behavior;
- smoother presentation at high FPS;
- no normal-play eviction or invariant anomalies;
- 60 FPS behavior matching the native timing path.

## Rejected Alternatives

### Global physical-time substitution

Replacing the core time at 0x5D68E0 would retime candidate selection, chart
state, misses, duration mechanics, and every active note. It is far broader
than the defect and risks audio/chart divergence.

### Fixed 60 Hz judgement island

Running judgement only at synthetic 60 Hz would preserve quantization rather
than provide exact physical grading. It would require broad scheduler and
presentation separation and would underuse the high-FPS loop.

### Exact-frame mapping or multi-pass carry

Runtime evidence already showed exact-frame visibility was insufficient.
Multi-pass carry shifted the effective hit time and destabilized grade. Keeping
that mechanism under the new transaction would make the timestamp contract
ambiguous and is prohibited.

### Grade-only timestamp correction

Changing only 0x5D0E00 cannot help an input that never reaches Pressed or is
rejected by an earlier eligibility gate. Reliable bounded availability and
non-shrinking acceptance are prerequisites for physical grading.

### Broad future-note queue

An arbitrary queue could attach an old press to a later note and change the
game's feel. The exact 1/60-second bound preserves the original sampling
opportunity and expires deterministically.

## Final Design Contract

The implementation is correct only if it treats a high-FPS press as one
timestamped physical event that:

1. is captured once;
2. is presented to free tap no more than once;
3. remains available to note judgement for at most the original 1/60-second
   sampling opportunity;
4. is evaluated through one immutable Arcade/Switch-aware transaction;
5. can expand but never shrink native eligibility;
6. grades from its physical time only when it actually triggers the accepted
   edge match;
7. never retimes the chart, long mechanics, or history-only forgiveness.

Any implementation that moves the event to a later frame, changes global song
time, weakens Switch compatibility, or layers on the failed carry design does
not satisfy this specification.

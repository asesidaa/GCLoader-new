> **ARCHIVED FAILED CORRECTION — NOT AUTHORITATIVE.** Runtime acceptance was
> never established and the complete implementation chain was rolled back.

# High-FPS One-Shot Input Lifetime Correction

**Date:** 2026-08-15

**Status:** Approved correction; runtime acceptance pending

**Binary evidence target:** `H:\gc\game471.exe.i64`

## Supersession

This correction supersedes three contracts from the earlier high-FPS
judgement designs:

- note availability and free-tap presentation are not independent;
- a native note-handler return value does not decide whether input was used;
- physical QPC does not retime the native grade-helper argument.

It retains the transition journal, immutable per-core view, exact rational
60 Hz history, bounded `1 / 60` edge interval, type-aware late-gate preview,
native note state machines, locked Switch rules, and the 60 FPS no-op.

## Runtime-disproved behavior

The representative 240 FPS run contained 70 successful note records. Thirty
of those physical sequences appeared again as `free_tap` approximately two
high-FPS frames later, about 8.3 milliseconds. The journal reported no
transport or history eviction and no invariant anomaly. The duplicate was
therefore created after capture by the loader's independent note/free-tap
lifetime.

Read-only decompilation through the existing IDA-CLI daemon also disproved the
transaction's handler-result ownership rule. The dispatcher return is an
eventual note result, not a generic `input consumed` flag. Long-form heads can
accept input while returning false, and short handlers can mutate native state
before a false result. Committing only when the dispatcher returns true leaves
an already-presented physical pulse available for another note or free tap.

The same run showed that applying small physical-QPC deltas to the grade helper
still changed judgement feel. The correction therefore restores the original
grade argument. Physical QPC remains relevant only to bounded pulse
availability, exact history, late-gate rescue, and diagnostics.

## Corrected lifetime contract

One published physical rising edge represents one native 60 Hz pressed pulse
and has one shared lifetime:

1. The timeline may expose it for no longer than exactly `1 / 60` second.
2. All queries inside the same active note observe the immutable edge.
3. When a pressed query presents the edge, the active note stages retirement.
4. When a direction matcher observes the edge through its current-state query,
   the active note stages retirement even if the direction is rejected.
5. Ending the note merges staged retirement regardless of the native handler
   result.
6. Later notes and the free-tap fallback in the same core call cannot reuse it.
7. Timeline commit makes it unavailable to every purpose in later core calls.
8. If free tap presents it first, a later note cannot use it.

A type-aware late-gate preview is not presentation. Preview alone never
retires an edge. It exists only because NORMAL, FLICK, HOLD, SLIDE HOLD, and
their shared families call the native late gate before querying input.

Newest-wins coalescing, expiry, epoch reset, and oldest-record eviction remain
unchanged. Different logical inputs in one cohort remain independently
available so chords, CRITICAL, DUAL HOLD, and Switch direction combinations
retain one immutable simultaneous snapshot.

## Native grade and timing contract

- `0x5D0E00` receives exactly the grade argument supplied by the native caller.
- `JudgTimeOffset`, `GameTimeOffset`, and MERRY GO ROUND segment adjustment
  therefore remain entirely native.
- The grade hook remains observational for bounded diagnostics; its original
  and forwarded arguments must be equal and the summary's `grade_retime`
  counter must remain zero.
- The selected or previewed physical edge may still move only the argument to
  late gate `0x5D0BE0`, preserving the narrow non-shrinking late rescue.
- Chart time, candidate selection, duration, repeats, holds, and continuation
  remain on native recognition time.

## Complete note-type and free-tap review

| ID | Type | Native input/result behavior | Corrected edge retirement |
|---:|---|---|---|
| 0 | NONE | No independent input query | None |
| 1 | NORMAL | Button pressed; state may change before a false return | Retire a presented button edge at note end, independent of return |
| 2 | FLICK | Held, age, history, and direction matcher; false is not an ownership signal | Retire every current direction edge actually exposed to the matcher, accepted or rejected |
| 3 | HOLD | Pressed head and held body; accepted head normally returns false until completion | Retire the presented head edge; held body creates no new edge |
| 4 | SCRATCH | Directional pressed pulses drive a long-form state machine | Retire each pressed pulse selected by native query order, independent of eventual completion |
| 5 | BEAT | Repeated button pulses drive a long-form state machine | Retire each presented repeat pulse, independent of eventual completion |
| 6 | MERRY GO ROUND | NORMAL-family offset button path | Same one-shot button lifetime as NORMAL; native grade argument unchanged |
| 7 | HIDDEN | NORMAL-family button path | Same as NORMAL |
| 8 | HIDDEN2 | NORMAL-family button path | Same as NORMAL |
| 9 | CRITICAL | Paired NORMAL-family booster components; outer result depends on aggregation | Retire each presented component independently while preserving one immutable paired snapshot |
| 10 | SLIDE HOLD | Direction head plus held continuation; head acceptance need not return true | Retire exposed head direction edges even on a false result; continuation creates no new edge |
| 11 | SLIDE COUNTER | Dispatcher lifecycle marker with no independent input query | None |
| 12 | TURN | Dispatcher lifecycle marker with no independent input query | None |
| 13 | SPIN | Dispatcher lifecycle marker with no independent input query | None |
| 14 | FINISH | Dispatcher lifecycle marker with no independent input query | None |
| 15 | DUAL HOLD | Coupled HOLD components; accepted heads return false until long-form completion | Retire each presented head independently; held bodies remain native |
| - | Free tap | Runs after note processing and uses button pressed queries | Shares the same lifetime; it can present only an edge not already presented to a note |

The existing Switch policy remains locked beneath every applicable row:

- a new direction may be a same-booster button edge;
- any held direction may sustain that booster;
- either adjacent cardinal may satisfy a diagonal initially and continuously;
- real buttons, exact directions, native success, Arcade behavior, and
  gameplay-only scoping remain unchanged.

## Diagnostics

Bounded diagnostic records describe input presentation, not note completion.
A `kind=note_presented` record may therefore contain `handler=0`; that is not
labelled a miss. Rejected FLICK and SLIDE HOLD direction evaluation records the exposed
source edge in the same way. Grade fields are `grade_original_ms` and
`grade_forwarded_ms`, which should be equal under this correction.

## Verification and runtime gate

Automated coverage must include:

- note then free tap, and free tap then note, sharing one physical lifetime;
- false native handler results for every pressed-query family;
- rejected FLICK and SLIDE HOLD direction evaluation;
- all IDs `0..15` plus free tap;
- CRITICAL, DUAL HOLD, simultaneous cohorts, and locked Switch aliases;
- unchanged grade arguments and a zero `grade_retime` counter;
- exact `1 / 60`, `2 / 60`, and `4 / 60` QPC boundaries;
- target FPS 60 remaining a correction no-op.

Build and unit evidence cannot establish gameplay feel. Runtime acceptance is
a new 240 FPS Switch play test. Its log must be checked for duplicate physical
sequences, `kind=note_presented handler=0` long-form/direction presentations, free-tap
reuse, grade retiming, evictions, and anomalies before making any claim that
the input-drop or judgement-feel issue is fixed.

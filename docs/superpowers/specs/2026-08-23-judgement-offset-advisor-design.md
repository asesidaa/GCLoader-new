# Judgement Offset Advisor Design

**Date:** 2026-08-23

**Status:** Approved design contract

## Purpose

Add a read-only utility to ConfigGUI that analyzes the latest complete game run
in `loader-log.txt` and suggests an absolute `JudgTimeOffset` value. The
suggestion calibrates the combined player, input, display, and audio setup; it
does not claim to identify which part of that chain contributes the bias.

The advisor exists because a fixed native GREAT window can produce a broad
finite-sample maximum-GREAT plateau. Returning that whole plateau is not a
useful calibration. The advisor must robustly estimate the timing distribution,
maximize projected GREAT, and then center the surviving distribution so its
single recommendation preserves margin to both sides of the native window.

## Authority and Preservation Rules

This feature consumes diagnostics from the approved absolute-time judgement
implementation. It does not change that implementation.

The following remain authoritative and unchanged:

- `2026-08-22-absolute-time-judgement-correction-design.md` for the corrected
  absolute judgement runtime;
- `2026-08-22-asio-absolute-time-judgement-design.md` for WASAPI/ASIO clock
  behavior and the judgement-offset attribution evidence; and
- the completed high-FPS binary/input-pipeline audit and E-042 through E-046.

In particular, the advisor must not change input timestamps, note selection,
held behavior, native recognition, native score processing, judgement windows,
or stage lifecycle.

## Goals

- Analyze every naturally completed absolute-time stage in the latest game-run
  log.
- Use the exact native score outcome as ground truth for observed
  MISS/GOOD/COOL/GREAT statistics.
- Reconstruct pre-offset timing errors for one-to-one scored ordinary press
  judgements.
- Exclude input mechanics whose GREAT boundary cannot be reconstructed from the
  existing log.
- Resist isolated mistakes and sustained human-error tails without fitting a
  loader-side adaptive offset.
- Produce one absolute integer `JudgTimeOffset` value when independent robust
  estimators agree within 3 ms.
- Display compact aggregate and per-song statistics in ConfigGUI.
- Keep the utility read-only. The existing native Test Mode timing form remains
  the only live editor.

## Non-Goals

- Changing `JudgTimeOffset`, `GameTimeOffset`, `system.cfg`, or `config.toml`.
- Adding an adjustment delta such as `+2 ms`; Test Mode accepts an absolute
  value.
- Identifying human, keyboard, USB, display, driver, DAC, or acoustic latency
  separately.
- Combining archived logs or inferring that different files used the same
  player/setup.
- Showing individual note records.
- Predicting held/direction, component, duration, ad-lib, or free-input grades
  from an ordinary-tap window.
- Replaying gameplay, replacing native grade logic, or adding a gameplay
  emulator/test oracle.
- Changing runtime diagnostic volume or adding a second runtime log stream.

## Dataset Contract

The current logging setup replaces `loader-log.txt` for each game process.
Therefore one file contains only the latest run and is treated as one player and
one complete setup. The setup includes the input method, keyboard mode, display,
audio backend/device, and physical output path.

The first version analyzes only:

```text
<directory containing the active config.toml>\loader-log.txt
```

It does not offer archived-log selection or automatic multi-file merging.

Every naturally completed song in that run remains a separate statistical
stage. The advisor pools eligible evidence only after computing each stage's
filter boundaries independently. Per-stage variation is displayed but is not
subject to the 3 ms estimator-agreement rule; different chart sections and
human performance can move a single song's center without changing the fixed
setup bias.

## Native Result and Timing Semantics

### Authoritative scored result

The runtime snapshots the native score-state counters before and after the
original score processor. These scope deltas and their stage cumulative totals
are the authoritative scored outcomes:

- `scope_score_miss_delta` / `cumulative_score_miss_delta`;
- `scope_score_good_delta` / `cumulative_score_good_delta`;
- `scope_score_cool_delta` / `cumulative_score_cool_delta`; and
- `scope_score_great_delta` / `cumulative_score_great_delta`.

`native_grade` is the return from the native timing-grade helper. It is not by
itself proof that the descriptor finalized a score: component paths may make a
timing-grade call without incrementing a score counter in that scope. The
advisor reports stage results from score-state deltas, never by counting all
`native_grade` fields.

### Error coordinates

For a timing observation:

```text
signed_error_ms = recognition_ms - note_target_ms
applied_offset  = recognition_ms - native_ms
raw_error_ms    = native_ms - note_target_ms
                = signed_error_ms - applied_offset
```

A positive raw error is late-side/SLOW; a negative raw error is
early-side/FAST. FAST/SLOW direction is separate from the native grade. A
`+15 ms` error is late-side but remains GREAT under a symmetric
`[-33,+33] ms` window.

The UI calls the location statistic `Median error before offset`, not `raw
center`, to avoid confusing it with the zero center of the GREAT window.

### Supported GREAT boundary

For the supported binary with the project's default judgement settings, a
one-to-one scored ordinary press timing is GREAT exactly when:

```text
-33 <= raw_error_ms + candidate_offset_ms <= +33
```

The absolute-time patch already supports only the default judgement contract;
the advisor does not read or approximate alternate judgement windows.

## Complete-Stage Eligibility

A stage is usable only when the log contains, in order, one matching
`semantic-stage-open` and one matching `semantic-stage-end` whose
`activated=1`. The end record is the authoritative proof that absolute
judgement activated and the song completed. A provider-backed run may also
contain `absolute-stage-activation`, which is still validated when present but
is not required because ASIO logical-clock stages have no provider-timeline
activation record.

The stage is rejected when followed by
`semantic-stage-termination source=test_mode_entry`, because entering Test Mode
terminates rather than completes the song. An open/active stage at end of file
is incomplete and rejected.

The stage-end cumulative record must also prove zero for conditions that can
make the statistical population incomplete or untrustworthy:

- timing-grade drops;
- scope-trace drops;
- score-observation read failures;
- score-counter regressions;
- final-accounting mismatches;
- unavailable clock reads;
- sequence errors;
- overload drops;
- cleanup drops; and
- rounded fallback.

Late delivery records are allowed: judgement retains their original absolute
timestamp. A stage with no eligible timing samples can contribute native result
statistics but cannot contribute to offset estimation.

## Timing-Sample Eligibility

A scope contributes one calibration sample only when all of the following are
true:

- it is an event scope;
- exactly one ordinary pressed query returned true;
- no held query returned true;
- no direction query returned nonzero;
- exactly one timing-grade observation was recorded and none was dropped;
- the four native score deltas sum to exactly one; and
- the incremented score counter corresponds to that timing observation's
  `native_grade`.

This deliberately excludes multi-component settlement, paired/direction
forgiveness, held/duration mechanics, scoreless timing-helper calls, multiple
score results in one scope, ad-libs, and free taps. Those native results remain
visible in the aggregate score statistics but are not assigned an invented
ordinary-tap window.

Natural MISS results without an input transition have no timing observation and
cannot estimate a constant input phase. A pressed timing MISS that meets the
one-to-one contract can enter the population, but robust filtering prevents its
magnitude from dominating the result.

## Robust Estimator

### Per-stage populations

For each complete stage, sort its eligible `raw_error_ms` values and compute:

- sample count;
- median error before offset; and
- median absolute deviation (MAD).

Create four independent retained populations per stage:

1. remove 5% from each sorted tail;
2. remove 7.5% from each sorted tail;
3. remove 10% from each sorted tail; and
4. retain values whose distance from the stage median is at most two MAD.

Tail counts use `floor(stage_sample_count * fraction)` on each side. The MAD
comparison is inclusive. Each rule is applied per stage before its retained
values are pooled, so one song's tails do not set another song's boundaries.

### Maximum-GREAT calculation

For each retained raw error `e`, candidate integer offset `o` produces GREAT
when:

```text
-33 - e <= o <= +33 - e
```

Each sample therefore contributes one inclusive integer interval. The
implementation finds the exact maximum overlap of those intervals with 64-bit
boundary arithmetic. It does not impose an arbitrary search range.

The finite sample may produce one or more maximum-overlap plateaus. The plateau
is diagnostic only; it is not the recommendation.

### Centered choice per estimator

Within the maximum-GREAT candidate set, select the integer offset minimizing:

```text
sum(abs(raw_error_ms + candidate_offset_ms))
```

This lexicographic rule first maximizes projected GREAT and then centers the
retained distribution, preserving future timing margin on both sides. It also
prevents the current offset from being retained merely because it happens to
lie somewhere inside a broad finite-sample plateau.

If one connected minimum-loss candidate interval contains more than one integer,
select its arithmetic midpoint and round an exact half away from zero. If the
same minimum loss occurs in disconnected maximum-GREAT candidate intervals,
the estimator is ambiguous rather than evidence for either timing mode. The
advisor retains its statistics but produces no cross-estimator suggestion for
that run. This is the conservative handling for a theoretically possible,
balanced bimodal population; ordinary single-player runs are expected to have
one connected winning interval.

### Cross-estimator recommendation

Collect the four centered integer choices, one from each unambiguous estimator.
All four estimators must be unambiguous before a suggestion can be produced.

- `Estimator range` is their minimum through maximum.
- The estimator spread is `maximum - minimum` in milliseconds.
- When spread is at most 3 ms, the suggested absolute offset is their median,
  rounded to the nearest integer with an exact half rounded away from zero.
- When spread exceeds 3 ms, no suggested value is displayed; the estimator
  range and song statistics remain available.

One complete song produces a provisional suggestion. Two or more complete
songs with acceptable estimator agreement produce a stable suggestion. The UI
does not turn per-song center variation into a separate hard threshold.

### Projections

`Observed eligible GREAT` counts the eligible one-to-one samples whose actual
native grade was GREAT.

`Projected eligible GREAT` applies the suggested offset to every unfiltered
eligible raw error and tests the fixed native GREAT interval. It is explicitly a
projection over the observed note/input pairing; changing an offset can also
affect native candidate gates at extreme boundaries, so the next real game run
remains acceptance authority.

Aggregate native MISS/GOOD/COOL/GREAT totals are reported separately and are
never presented as fully replayable projections.

## ConfigGUI Experience

Place a compact `Judgement offset advisor` block immediately below the existing
`Absolute-time judgement` control in the Experimental section.

The block contains one explicit `Analyze latest run` button. Analysis is not a
background watcher and does not run repeatedly.

For a stable result, show statistics only:

```text
Suggested JudgTimeOffset       -9 ms
Estimator range                -10..-8 ms
Last observed gameplay offset   0 ms

Complete songs                  2
Eligible judgements             710
Observed eligible GREAT         642 / 710
Projected eligible GREAT        659 / 710

Native results
MISS 0   GOOD 8   COOL 76   GREAT 873
```

The suggestion is an absolute value. The UI does not show a relative
`Suggested adjustment` field.

The per-song table contains aggregate statistics only:

```text
Song  Samples  Median error before offset  MAD  MISS  GOOD  COOL  GREAT
1     349      +12 ms                      12   0     3     41    405
2     361      +7 ms                       12   0     5     35    468
```

No individual timing records or explanatory notes are displayed.

Result states are:

- missing/unreadable/malformed log: a concise ordinary GUI error;
- no complete natural song: no recommendation;
- one usable song: provisional suggestion when estimator spread permits;
- two or more usable songs: stable suggestion when estimator spread permits;
- estimator spread above 3 ms: statistics and range without a suggestion; and
- a disconnected equal optimum in any estimator: no suggestion and the concise
  result `Data is too diverse to give a suggestion.`; and
- varied applied offsets within one run: allowed, normalized per observation,
  and displayed as varied rather than as one last observed value.

## Source Architecture

Add one focused ConfigGUI module:

```text
tools/ConfigGUI/
  JudgementOffsetAdvisor.h
  JudgementOffsetAdvisor.cpp
```

The module owns:

- streaming parsing of the existing log schema;
- lifecycle/stage validation;
- native result aggregation;
- timing-sample eligibility;
- robust population construction;
- exact interval-overlap and absolute-error tie breaking; and
- a value-type analysis result for presentation.

`Main.cpp` owns only the small ImGui presentation state and rendering block.
The new source is added to the existing ConfigGUI model/static-library target.

The parser reads line-by-line and uses exact token lookup plus `std::from_chars`
for numeric fields. It does not use exceptions or `try`/`catch`. Dynamic
containers are acceptable for the offline utility; ownership remains automatic
and bounded by the selected log.

Ordinary external failures use `std::expected` with a concise error enum and
message data. A missing or malformed user log is not a runtime invariant and
must never abort ConfigGUI. No new exception translation, fallback judgement,
or timeout is introduced.

No iDmacDrv32 source, runtime hook, diagnostic producer, configuration schema,
or persistence code changes.

## Independent Validation Evidence

### Prior nonzero-offset run

The prior two-song ASIO run used `JudgTimeOffset=-12 ms` and produced:

- 810 eligible ordinary scored press timings;
- pooled median error before offset `+10 ms` and MAD `12 ms`;
- robust centered choices in `-10..-9 ms`; and
- a centered suggestion of approximately `-10 ms`.

### Zero-offset validation run

`H:\gc\loader-log.txt`, last written `2026-08-23 23:06:09`, SHA-256
`A3144B251BACB05711781EC21FC0A459C9B7556DC4CE3476F0B1959CC0B94275`,
is an independent two-song run at zero offset.

Lifecycle and diagnostic evidence:

- two semantic opens, two absolute activations, and two natural semantic ends;
- no Test Mode termination;
- all 1,036 timing records have `recognition_ms - native_ms = 0`; and
- timing/scope drops, score read failures/regressions, accounting mismatches,
  clock unavailability, sequence errors, overload/cleanup drops, and rounded
  fallback are all zero.

Stage statistics:

| Stage | Eligible | Median before offset | MAD | Native MISS | Native GOOD | Native COOL | Native GREAT |
|---|---:|---:|---:|---:|---:|---:|---:|
| 1 | 349 | `+12 ms` | `12 ms` | 0 | 3 | 41 | 405 |
| 2 | 361 | `+7 ms` | `12 ms` | 0 | 5 | 35 | 468 |

Pooled estimator results:

| Rule | Retained | Maximum GREAT | Plateau | Centered choice |
|---|---:|---:|---:|---:|
| 5% tail trim | 640 | 640 | `-8..-4 ms` | `-8 ms` |
| 7.5% tail trim | 604 | 604 | `-13..-2 ms` | `-9 ms` |
| 10% tail trim | 570 | 570 | `-17..0 ms` | `-9 ms` |
| Two MAD | 598 | 598 | `-16..-3 ms` | `-10 ms` |

The resulting estimator range is `-10..-8 ms` and the centered suggestion is
`-9 ms`. On all 710 unfiltered eligible samples, the fixed-window projection is
642 GREAT at zero offset and 659 GREAT at `-9 ms`.

The new suggestion overlaps the prior run's `-10..-9 ms` result. The centered
estimate changes by only 1 ms between independent offset conditions, which is
the repeatability required to finalize this design.

## Verification and Acceptance

Agent-owned implementation verification is limited to:

- complete MSVC x86 Debug and Release builds of ConfigGUI through a persisted
  environment build script;
- CLion diagnostics for every added or touched ConfigGUI source;
- review of the functional diff without spending work on whitespace/newline
  normalization; and
- running the production advisor against the real zero-offset log and checking
  the exact lifecycle, score, sample, estimator, and projection figures above.

No synthetic gameplay test, replay, arbitrary expected-value suite, or emulated
native judgement model is authorized. The interval-overlap and absolute-loss
rules are specified mathematically here; gameplay truth remains the supported
binary and runtime log.

User-owned acceptance is:

1. Open ConfigGUI after a game run containing complete songs.
2. Analyze the latest log and confirm the compact statistics are readable.
3. Enter the suggested absolute value through the existing Test Mode timing
   form.
4. Play another full session.
5. Confirm the next run produces a similar estimator and sane native result
   distribution.

## Acceptance Criteria

The design is satisfied when:

1. ConfigGUI reads only the latest run's adjacent `loader-log.txt`.
2. Only complete, diagnostically trustworthy natural stages contribute.
3. Native result totals come from authoritative score-state deltas.
4. Only one-to-one scored ordinary press timings enter calibration.
5. Raw errors are reconstructed independently of the offset used during play.
6. Four per-stage robust rules independently maximize GREAT and center the
   selected population.
7. A suggestion appears only when estimator spread is at most 3 ms.
8. The UI shows an absolute value and aggregate/per-song statistics only.
9. The utility never writes configuration or changes runtime judgement.
10. Real-log output matches the recorded zero-offset evidence, while final
    gameplay acceptance remains user-owned.

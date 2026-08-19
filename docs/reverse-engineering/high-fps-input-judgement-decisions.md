> **HISTORICAL DECISIONS ONLY.** The later design formerly named below also
> failed; no design in this chain is currently authoritative.

# High-FPS Input Judgement Decision Record

Date: 2026-08-15
Status: Entire decision chain failed and was rolled back to the post-ASIO
baseline on 2026-08-20. The formerly “authoritative” correction is retained as
negative evidence only.

Everything below is retained as historical discussion and rejected design
context. It must not be used as a current implementation contract. The binary
evidence target was `H:\gc\game471.exe.i64`.

## Locked Switch Compatibility Principle

The existing `Switch` gameplay input style is an immutable compatibility
contract for this work. High-FPS input capture, event timestamps, input
history, chord handling, and judgement changes must compose underneath these
rules and must not reinterpret or weaken them:

1. Every newly pressed direction may act as a pressed edge for the center
   button on the same booster. Pressing a second direction while another
   direction remains held may therefore create another same-booster button
   edge.
2. A booster button is considered held while its real button or any direction
   on that booster is held.
3. For a diagonal chart target, either adjacent cardinal direction may satisfy
   the target in both initial and continuation judgement.
4. Native successes remain successes: real booster buttons, exact diagonals,
   native cardinal matches, and unrelated Arcade behavior are preserved.
5. Switch transformations remain gameplay-only. Menus, test mode, the raw
   FastIO word, physical bindings, and input backends retain their existing
   behavior.

For paired notes, the rules apply independently to both boosters while every
query in one judgement transaction observes one immutable full input snapshot.
In particular, a `CRITICAL` may be completed by the eligible button or
direction alias on each booster, while a Switch diagonal must not be changed
into a requirement to press both physical cardinal components.

If a proposed high-FPS timing architecture conflicts with any rule above, the
architecture is incomplete. The Switch rules are not available as a trade-off
to simplify timestamping or frame-history implementation.

## Locked Original-Forgiveness Principle

High-FPS timestamping must preserve every input acceptance intentionally
provided by the original 60 FPS held-state and recent-history rules. A more
precise physical timestamp may refine the timing of a newly observed edge, but
it must not reinterpret an input accepted from existing state as an older hit
and thereby change its grade or reject it.

Timestamp selection follows these rules:

1. When a newly observed edge directly triggers a match, use that edge's
   physical song-timeline timestamp.
2. When a multi-component input becomes valid because of a newly observed
   final component, use the completion edge: the latest required new edge.
3. When the native matcher accepts an already-held or recent-history state and
   there is no new qualifying edge in the current transaction, use the current
   recognition time, as the original game does.
4. Hold and slide continuation state uses the current authoritative song time;
   sustaining a state is not a new physical hit.
5. A historical physical edge must never be substituted merely because it can
   be found in the journal if doing so would remove native pre-hold forgiveness
   or move an accepted judgement earlier.

Consequently, exact physical timestamping applies to edge-triggered judgement,
while the original recognition-time semantics remain authoritative for
state-triggered acceptance. This distinction is required for compatibility,
not treated as a fallback approximation.

### Acceptance Clock and Grade Clock

The original recognition time remains authoritative throughout candidate
selection, timing-window eligibility, input suppression, held/history matching,
miss processing, state transitions, paired-note aggregation, duration scoring,
and repeat-interval logic. The loader must not replace the dispatcher or note
handler's general time argument with a physical input timestamp.

For timing-graded hits only, a physical edge may refine the timestamp passed to
the shared timing-grade helper at `0x5D0E00`, after the original handler has
already accepted the input. The live binary has exactly two direct callers:

- `0x5D1F2A` in the normal-button handler `0x5D1D50`, which also serves
  NORMAL, HIDDEN, HIDDEN2, CRITICAL, and MERRY GO ROUND paths;
- `0x5D34C5` in the FLICK handler `0x5D3320`.

The physical-versus-recognition delta is applied to the grade helper's existing
argument rather than replacing it with an absolute timestamp. This preserves
JudgTimeOffset and MERRY GO ROUND's per-segment adjustment. If the accepted
input has no qualifying new edge, the helper receives its original argument.

Long-form mechanic scoring through `0x5D04F0` remains on original recognition
and lifecycle time. This covers HOLD, SCRATCH, BEAT, SLIDE HOLD, and DUAL HOLD;
their input reliability is corrected through the coherent snapshot/history
layer, not by retiming duration or repetition mechanics.

## Mandatory Judgement-Coverage Gate

Before implementation planning, the design must contain an explicit matrix for
all binary note type IDs `0..15` plus free-tap processing. Every row must record:

- native dispatcher and handler path;
- pressed, held, held-age, direction, or paired-state inputs;
- single-component, multi-direction, or paired-booster semantics;
- physical timestamp/cohort selection;
- millisecond-domain and frame-history rules;
- Arcade behavior;
- the applicable Switch transformation;
- whether a shared seam is sufficient or a targeted correction is required;
- automated evidence and required in-game acceptance.

The current binary audit maps the IDs as follows:

| ID | Name | Direct input path |
|---:|---|---|
| 0 | NONE | None |
| 1 | NORMAL | Button pressed edge |
| 2 | FLICK | Direction held, age, history, and direction match |
| 3 | HOLD | Button pressed head and held body |
| 4 | SCRATCH | Four directional pressed edges |
| 5 | BEAT | Repeated button pressed edges |
| 6 | MERRY GO ROUND | Offset button pressed edge |
| 7 | HIDDEN | Button pressed edge |
| 8 | HIDDEN2 | Button pressed edge |
| 9 | CRITICAL | Paired left/right button components |
| 10 | SLIDE HOLD | Direction head and continuation state |
| 11 | SLIDE COUNTER | No independent dispatcher input query |
| 12 | TURN | No independent dispatcher input query |
| 13 | SPIN | No independent dispatcher input query |
| 14 | FINISH | No independent dispatcher input query |
| 15 | DUAL HOLD | Coupled left/right HOLD components |
| - | Free tap | Left/right button pressed edges outside note judgement |

Types `11..14` may be classified as lifecycle markers only after the final
design retains the dispatcher and whole-binary input-wrapper evidence proving
that they have no independent input judgement path.

## Timing Rules Already Requiring Explicit Treatment

- Direction matching uses held age `<= 4` and a `current_frame - 2` lookup.
- `HoldSafeFrame` is a frame-counted HOLD release grace.
- `SlideHoldSafeFrame` is a frame-counted SLIDE HOLD release grace.
- `ScratchEnableTime` and `BeatEnableTime` are already milliseconds and must not
  be FPS-scaled.

These rules must retain their original 60 FPS time meaning at 144, 165, 240,
and other supported target rates. `target_fps = 60` remains a behavioral no-op
for the high-FPS correction; the independently selected Switch style continues
to operate at 60 FPS.

### Approved Frame-History Treatment

- Current held queries use the immutable current judgement snapshot.
- The native `current_frame - 2` lookup is reconstructed from input state at
  recognition time minus exactly `2 / 60` seconds.
- Native held age is derived from monotonic elapsed time in a synthetic 60 Hz
  domain, so the `<= 4` rule expires after `4 / 60` seconds independently of
  target FPS.
- All conversions use integer/rational clock arithmetic. Target rates such as
  144 and 165 FPS do not use rounded render-frame counts for direction history.
- State already held when the history layer starts is seeded as pre-held state
  and does not synthesize a pressed edge.
- Published transitions retain their real order for state reconstruction, but
  more than one still-unconsumed rise of the same logical control inside one
  `1 / 60`-second sampling opportunity is exposed as one newest pending press.
  Different logical controls never coalesce, so chords and paired inputs keep
  their complete simultaneous cohort.

The supported cabinet configuration fixes both `HoldSafeFrame` and
`SlideHoldSafeFrame` at `0`. The high-FPS layer does not read, scale, patch, or
emulate either value. Native release grace therefore remains disabled exactly
as configured, and the two native release state machines are left unchanged.

### Configuration-Lifetime Boundary

`JudgTimeOffset` and `GameTimeOffset` are the only timing values in this scope
that may change at runtime, through the existing test-menu patch. Every other
relevant `system.cfg` value is a fixed supported-cabinet constant and is not an
input to the loader design:

- `HoldSafeFrame = 0`;
- `SlideHoldSafeFrame = 0`;
- `ScratchEnableTime = 250` milliseconds;
- `BeatEnableTime = 200` milliseconds.

The high-FPS layer performs no file read, runtime lookup, reload, change
observation, or derived calculation for these values. It leaves the game's
existing static millisecond mechanics and zero release-grace behavior
untouched. No configuration watchers, generations, locks, callbacks, or new
test-menu controls are added.

The physical grade correction applies an event-versus-recognition delta to the
game's existing grade argument, so live `JudgTimeOffset` and `GameTimeOffset`
behavior composes automatically without loader-side caching or special update
handling.

## Bounded History and Ordinary Failure Handling

The input journal is a fixed-capacity ring whose entries contain a timestamped
transition and the complete post-transition input state. It is sized to retain
more than the required `4 / 60`-second compatibility window during supported
operation and performs no hot-path allocation.

If the ring is nevertheless full, the newest transition is appended and the
oldest retained transition is discarded. Processing continues without
disabling the patch, changing judgement modes, delaying the new input, or
attempting transaction rollback. A cumulative eviction counter and
rate-limited diagnostic make the abnormal condition visible.

Eviction may temporarily reduce the available historical interval. A query
older than the oldest retained full-state entry uses the oldest available state;
an exact rise time that has been evicted is treated as older than the recent
input window. The natural consequence may be lost history forgiveness or an
observable miss. New input remains preferred over stale history.

Normal lifecycle cases receive only direct handling:

- journal initialization seeds currently held controls as pre-held state and
  does not synthesize pressed edges;
- a gameplay/song epoch reset clears and reseeds the journal;
- buffer eviction is handled as described above.

The design does not add elaborate recovery for hypothetical clock corruption or
other spacecraft-grade failures. Existing patch-install transactionality remains
responsible for preventing partially installed hooks.

## Bounded Edge Availability and Non-Shrinking Eligibility

Runtime validation disproved the exact-frame bridge as a complete solution.
Making every physical rise visible for one selected high-FPS game pass still
allowed short inputs to miss. A temporary four-pass carry improved the symptom
only slightly and made grades unstable because it moved recognition by up to
12.5 milliseconds at 240 FPS.

The replacement design therefore preserves a newly observed physical edge as a
pending note input for at most exactly 1 / 60 second. This duration is the
original maximum delay between a physical press and the next 60 FPS input
sample. It is a bounded sampling-compatibility interval, not an arbitrary
future-note queue:

- free-tap and key-sound presentation observes the edge once immediately;
- note judgement may still observe the same edge during the bounded interval;
- the first relevant note-handler transaction that uses the edge consumes it
  only after the immutable transaction completes, so every query in that
  transaction sees the same input;
- expiry is measured by monotonic QPC duration and is exact at non-divisor
  target rates such as 144 and 165 FPS;
- free-tap presentation does not repeat while note availability remains
  pending.

Eligibility is non-shrinking:

native recognition-time acceptance OR a retained physical edge was valid at
its event time.

Recognition-time success always remains success. Event time may rescue a
physically valid edge whose later high-FPS recognition has crossed a late
boundary, but event time never rejects or tightens a native acceptance. Once
accepted, a newly observed edge uses its physical time for timing grade;
history-only acceptance uses recognition time, and hold/repeat/lifecycle
mechanics remain on the authoritative current song timeline.

This rule covers both timing gates established in the live binary:

- the outer early-eligibility gate in the core judgement loop at 0x5D68E0,
  before dispatch through 0x5D5720;
- the shared late/miss gate at 0x5D0BE0, called by normal, flick, hold,
  scratch, beat, slide-hold, and dual-hold paths.

The pending interval lets an early edge survive until the native outer gate can
dispatch the note. Transaction context at the shared late/miss seam prevents a
later recognition time from turning an event-time-valid input into a miss.
Grade refinement remains isolated to the shared grade helper at 0x5D0E00.

Unlike the failed carry experiment, bounded availability retains the original
event timestamp. A later eligible query does not move the hit or its grade to
the later render/game pass.

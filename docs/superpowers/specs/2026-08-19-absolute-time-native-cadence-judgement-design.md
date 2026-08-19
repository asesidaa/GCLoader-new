> **APPROVED POST-FAILURE DISCUSSION RECORD.** The 2026-08-20 code rollback
> removed failed implementation code; it did not discard the decisions reached
> in this document. These decisions are the input to the consolidated clean
> specification requested after the rollback.

# Absolute-Time Native-Cadence Judgement Design

**Status:** Historical approved discussion record. Its decisions and the
erratum below are consolidated into the authoritative
[2026-08-20 specification](2026-08-20-absolute-time-judgement-spec.md).
Use that specification for implementation and this record for rationale.

## 2026-08-20 scope erratum

The rollback and the final clock discussion establish a narrower ownership
boundary than the tail of this record originally proposed:

- the existing high-FPS framerate, shared `Tune` clock, and visual hooks retain
  their current behavior and ownership;
- the judgement/input patch must not reconfigure their target-rate domain or
  install `0x63FA0C` as compensation for a clock change it introduced;
- the final “preserve high-FPS visual clock separately” proposal is withdrawn;
- the approved requirement for one exact time decision per outer update may be
  implemented through a read-only exact-time publication/provider, but it may
  not change the high-FPS hook's native output or progression; and
- any tail wording that assigns authored judgement-boundary counts to
  `Tune+0x14` is superseded by this separation. Authored judgement cadence, if
  required by the consolidated design, is private judgement state.

Every other approved behavioral decision—including timestamped transition
history, recognition scopes, held-age semantics, native candidate/result
ownership, catch-up, ordering, WASAPI scope, activation/failure policy,
observability, and runtime acceptance—remains established discussion input.

### 2026-08-20 fail-fast simplicity addendum

The user subsequently tightened the implementation error-handling rule.
Internal Boolean/results that represent operations expected to succeed do not
justify fallback modes, retry state machines, or propagated “maybe continue”
branches. Check them once and take the appropriate startup or active-session
fatal path on failure. The check must remain active in Release; a C/C++
`assert()` that compiles out is not sufficient. Expected operational states
such as armed `NoPlayback`/`Pending` and same-generation temporary clock
unavailability remain explicit statuses because they are not invariant
failures.

## Architecture reopening — Resolved

The user rejected recreating a 16.67 ms CBooster sampling window at high FPS
and asked why the complete timestamped input history should not replace native
CBooster history for gameplay judgement.

The reopening retained these earlier locked boundaries:

- one absolute song-time authority;
- non-accumulating cadence arithmetic;
- the evidence/workflow boundary; and
- native ownership of candidate order, note routing, handlers, lifecycle,
  grade policy, score policy, effects, and free-input policy.

Earlier clauses that specifically require authored-60 CBooster
materialization, one recognition call only at each authored boundary, or a
minimum one-authored-frame input pulse are reopened and must not drive an
implementation plan.

Approved resolution: complete timestamped history replaces native CBooster
history as the authoritative gameplay-judgement input view. The later approved
event/heartbeat schedule, query surface, exact WASAPI provider, and activation
contract resolve recognition cadence and exact-time delivery.

## Hard goal — Approved

Judgement uses absolute song time and is independent of render framerate.
Given the same chart, physical input-event times, and timing settings, judgement
results must be the same at 60, 144, 165, and 240 FPS, including after a render
hitch. Render cadence may change delivery latency but must not change event
timestamps, ordering, or results.

This guarantee applies while the approved session remains valid and required
transport/clock history is retained. The explicitly accepted sub-poll transport
miss and the explicit fatal paths for lost history or discontinuity are not
silently converted into different judgement results.

A fixed update is an available implementation mechanism, not the goal itself.

## Evidence and workflow authority — Approved

- The completed native audit at
  `H:\gc\artifacts\GCLoader\.planning\debug\high-fps-timing-domains\evidence`
  is authoritative, especially E-042 through E-046. Do not regenerate it.
- Use the completed audit for native behavior and current source for practical
  integration questions.
- New IDA work is permitted only for a specific question demonstrably absent
  from the audit, recorded before investigation.
- Prior implementations and rejected designs are failure evidence, not an
  architectural foundation.
- Record each approved discussion section here before continuing.

## Simplicity and native-reuse policy — Approved

Avoid reimplementing gameplay policy, but do not assume that calling a larger
native routine is simpler. Choose the option with the smallest semantic and
proof surface:

1. reuse native logic when it accepts the required state/time without unwanted
   side effects;
2. otherwise mimic only the smallest transport or state primitive beneath it;
3. never move note-type routing, candidate ordering, edge ownership, handler
   policy, lifecycle, grade policy, score policy, or free-input policy into the
   loader.

Implementation clarity takes priority over minimizing changed lines, files,
or hook count. The current failed implementation is disposable: retain a piece
only when its contract is independently clear and it cleanly fits the approved
architecture. Starting the judgement patch again from empty, focused modules
is explicitly allowed. Prefer explicit state and small named transformations
over clever reuse, shared mutable machinery, or compatibility layers whose
only purpose is to preserve old code.

"Smallest" in this document therefore means the smallest **behavioral and
proof surface**, not the fewest lines. A slightly larger implementation with
separate transport, history, scope, query, and scheduling responsibilities is
preferred when those boundaries make failures easier to isolate and tests
easier to state.

The previously selected direction, **native authored cadence plus an
absolute-time history/timestamp layer materialized into CBooster**, is
superseded. The approved design keeps native gameplay ownership but replaces
judgement-facing CBooster history with immutable timestamped query scopes.

## Section 1: Time authority — Approved

### Single authority

There is one authoritative absolute song clock. Both authored-frame progression
and physical input timestamps derive from it. There is no second independently
rounded timeline and no correction that pins an arbitrary observation to the
start of the current frame.

```text
physical transition QPC
        -> map once to absolute song time
        -> retain the ordered timestamped transition history
        -> open one immutable native-recognition view at the requested time
        -> original native recognition, lifecycle, grade, score, and free input
```

### Earlier authored-CBooster schedule — Superseded

The former per-boundary materialization sequence is no longer an approved
schedule. In particular, an authored-60 boundary must not fuse all physical
transitions since the preceding boundary into one input fact. An authored
boundary may still remain as the native no-input/lifecycle heartbeat, but it
is not the input sampling interval. The exact recognition schedule is approved
below as the event/heartbeat patch-layer contract.

## Non-accumulating cadence arithmetic — Approved

144 and 165 FPS must not accumulate phase or rounding error relative to the
authored-60 judgement cadence.

- Derive the absolute authored target index directly from the current absolute
  song position on every update using checked integer/rational arithmetic.
- Compute a boundary from its absolute authored index, never by repeatedly
  adding a rounded `16.666...` value.
- Compute due work as `absolute_target_index - last_committed_index`, never by
  accumulating `60 / render_fps` in floating point.
- Map input QPC to song time once with checked rational arithmetic; do not map
  through a separately rounded render-frame phase.
- At 144 or 165 FPS, zero-step/one-step render-update patterns may vary, but
  their cumulative authored index must exactly equal the absolute song-clock
  target. A hitch may create multiple due authored steps without changing
  their original boundary times.
- Any native float-derived millisecond operand that must remain for native
  progression is computed directly from the absolute authored index. It is not
  an accumulated clock and must not become the authority for event time.

## Section 2: Input-history model — Resolved

### Same-control transition collapse — Withdrawn

The earlier proposal collapsed every same-control sequence inside one
authored-60 interval to one final CBooster fact. Complete timestamped-history
replacement removes that storage constraint. Press -> release -> press remains
three ordered state transitions and produces three ordered recognition scopes,
including two distinct rising-edge scopes, even when all three transitions
occur inside 16.67 ms. No transition is dropped merely because another
transition has the same containing authored frame.

### Device polling versus gameplay sampling — Finding recorded

Do not describe the native game as simply having a "60 Hz input device polling
rate." The completed audit proves three separate layers:

1. the physical/FIO or loader input layer produces an aggregate snapshot;
2. the gameplay input-frame entry captures a snapshot into CBooster history;
3. native recognition and held-history semantics use authored game frames whose
   stock frame-to-time conversion is approximately 60 Hz.

The physical device or an asynchronous loader worker may poll much faster than
60 Hz. CBooster nevertheless stores frame history, not a complete transition
queue. Unless a lower layer latches an edge, two consecutive CBooster snapshots
can both be released even though a high-rate journal observed a press and
release between them.

Modern rapid-trigger keyboards make such an enclosed press -> release a valid
input, not an impossible human edge case. The redesign must therefore not
discard it merely to imitate final-state sampling. This refines the input
transport requirement without changing the hard goal of absolute-time,
render-independent judgement.

### Rapid-trigger pulse representation — Withdrawn

Recommended: latch at most one observed rising transition per canonical
control into the next authored recognition step, even if the control was
released before that step's boundary. Materialize one native-valid pressed
frame, bind it to the exact rising timestamp, and expose the physical release
on the following authored history step if necessary. This retains native
pressed/composite/free-input query behavior and gives the pulse the minimum
one-frame representation that native CBooster can express.

The cost is one authored frame of logical held state for a pulse physically
shorter than that frame. A separate pressed-edge overlay could keep the final
held mask released, but would let native callers observe pressed without the
corresponding CBooster held state and would require broader query-semantic
changes. Extra event-time recognition calls remain rejected because they alter
native lifecycle and score cadence.

This proposal was withdrawn when the user rejected imposing an authored-60
input window at high FPS. It is retained only to explain the discarded branch.

### 1000 Hz input-only architecture — Rejected

A proposed 1000 Hz fixed update scoped to input does **not** redefine the
whole game's frame unit. Keep these cases distinct:

- The loader's device polling, transition journal, and loader-owned physical
  state can run at 1000 Hz without changing Tune, chart, render, judgement, or
  score frame domains. Much of this layer already exists.
- Native CBooster is not an independent 1000 Hz store: its ring slots are
  indexed by the gameplay frame supplied to native pressed/held/released and
  history queries, and its held-age counters advance per capture. Advancing
  that native ring with a separate 1000 Hz index would require frame mapping
  and age/lookback conversion at its query boundary.
- Calling native recognition and score at 1000 Hz would cross out of the
  input-only scope and would redefine broader game cadence. That is a separate
  proposal and is not assumed here.

The viable input-only interpretation is therefore: maintain the complete
high-rate transition/state timeline in loader-owned storage, then present its
facts on the game thread through the authored native input-query view.
Do not mutate CBooster or other native game objects from the polling worker.

This input-only fixed update prevents short transitions from disappearing,
but by itself does not make judgement absolute-time: native recognition still
needs either an exact-time sidecar at its timing comparisons or a separately
approved event-time call schedule.

Decision: do not build a second 1000 Hz logical input-history engine as the
primary gameplay architecture. Its native integration still requires frame,
held-age, history, and exact-time bridging, so it is harder without removing
the difficult judgement boundary.

This rejection does **not** remove the existing 1000 Hz polling worker or
transition journal. They remain the timestamped physical-event transport for
whichever architecture is selected; they do not independently schedule native
gameplay or judgement calls.

### Complete timestamped-history replacement — Approved boundary

This design does not add a second 1000 Hz fixed-update engine. The existing
transition journal is already an event-sourced history. During one gameplay
recognition scope, the loader would freeze an immutable view of that history at
the requested absolute time and use it instead of the native CBooster ring.

The replacement boundary is limited to judgement-facing input facts:

- pressed, held, and released state;
- historical held state;
- consecutive-held age;
- direction; and
- composite/paired control queries.

All callers in the recognition scope must share the same non-consuming view.
The loader must not route by descriptor or decide which note owns an edge.
Native candidate construction, component order, handlers, lifecycle, grading,
score, effects, and post-descriptor free input remain untouched.

Apply the existing native-reuse policy before implementing query behavior:

1. first check whether native query composition can operate over a
   loader-supplied base-state/history seam;
2. if not, reproduce only the smallest audited query primitives from E-042
   through E-046; and
3. never copy note-family policy into the input-history replacement.

This boundary removes the 16.67 ms CBooster sampling/fusion limit and gives
the exact edge timestamp directly to the judgement timing layer. The approved
sections below resolve recognition-scope cadence, held-age and lookback time,
and the five-method native query seam.

### Patch-layer architecture — Approved

#### Why event scopes are required

The native query ABI returns one pressed/held value to one recognition call.
A complete history cannot expose press -> release -> press through one such
view without collapsing at least one transition or moving note ownership into
the loader. Therefore the minimal policy-preserving schedule is:

1. retain native authored boundaries as no-input/lifecycle progression calls;
2. add one immutable recognition scope for every ordered journal transition;
3. preserve journal sequence when two transitions map to the same absolute
   time instead of deduplicating them; and
4. after a render hitch, deliver all due boundary and transition scopes in
   `(absolute time, sequence)` order.

Each scope calls the original `0x5D68E0` recognition core and `0x5CF930` score
consumer with that scope's absolute integer song millisecond. The native ABI is
integer-millisecond, so sub-millisecond QPC fractions determine ordering but
not a finer grade value. This direct timing argument removes the need for a
timestamp sidecar at individual late-gate, grade, duration, dispatcher, or
free-input sites.

#### Recommended patch boundary

| Layer | Treatment | Native patch sites |
|---|---|---:|
| Physical polling, FIO publication, transition journal | Reuse unchanged as the timestamped producer. Drain into a game-thread retained history; never run native gameplay on the worker. | `0` |
| Absolute schedule and native call pair | Replace the uniform loop at its existing guard. Build boundary/transition scopes from absolute time, call the proven native pair for each scope, then resume the native once-per-update tail. | `0x640239` (`1`) |
| Immutable active input view | Loader-owned game-thread/TLS scope around each native pair call. Outside a scope every query falls through to native. | `0` |
| CBooster judgement query surface | During a scope, answer from timestamped history; otherwise call the original method. Keep gameplay wrappers above this layer native. | `0x62DFB0`, `0x62DF50`, `0x62DD30`, `0x62E480`, `0x62DAA0` (`5`) |
| Native candidate, handler, grade, duration, score, lifecycle, and free-input policy | Leave untouched. Exact time arrives through the original core/score millisecond arguments. | `0` |
| Outer exact clock and Tune advance plan | Rework the existing shared song-clock hook to bind the group-2 voice, obtain exact ready time once, write the authored boundary count, and publish the immutable plan consumed by the scheduler. | existing `0x664DB2` dependency (`0` new) |
| Optional exact-now visual clock smoothing | Outside judgement ownership. If retained for independently audited high-FPS visual smoothness, consume exact outer time one-way; never write Tune, schedule scopes, or answer input. | `0x63FA0C` (`0` or `1` visual-only) |

The five CBooster methods are the smallest coherent replacement surface:

- pressed and released implement ordinary edges plus composite/paired query
  algebra over the retained history;
- held also answers historical frame arguments instead of discarding them;
- consecutive-held age comes from the retained absolute history in the native
  unit contract selected later; and
- direction uses the retained historical mask while reusing the audited native
  mask-to-vector and vector-to-direction helpers, rather than fabricating a
  second direction policy.

Keeping the gameplay wrappers native preserves device selection, `frame == -1`
resolution, and the existing Switch alias layer. Native pressed/released
composition cannot safely be called as a whole because those methods validate
the CBooster ring before reaching their recursive composite/paired logic. That
ring is no longer authoritative, so only the small audited control algebra is
reproduced.

The current capture-ownership hook at `0x659920` and duplicate held-counter
guard at `0x62DC60` become unnecessary. Native capture may continue for
non-judgement consumers, while scoped judgement queries ignore its ring and
counters.

The approved mandatory absolute-judgement interception surface is therefore
**six native sites**: one schedule seam plus all five query methods. It also has
one required existing integration dependency, the shared song-clock hook at
`0x664DB2`, which publishes the single outer plan rather than adding a second
judgement clock. Static judgement call-graph evidence shows no
result-affecting released query, but `0x62DD30` remains required so the approved
pressed/held/released replacement surface is coherent and the plan has no
special omitted-query mode. The independent Switch pressed/held alias hooks
and diagonal-match hook remain existing Switch policy, not new absolute-time
policy. Optional visual-only `0x63FA0C` does not join the judgement transaction
or result authority.

#### Activation conflict — Resolved

The hard goal requires the same results for the same event times at 60, 144,
165, and 240 FPS. A `target_fps != 60` activation gate cannot satisfy that
contract because unpatched 60 FPS still samples through CBooster and can lose
or quantize sub-frame transitions. The approved resolution is that explicitly
enabled absolute-time judgement installs the same scheduler and five query
providers at 60 FPS as at every other feature-supported rate. Feature-off stock
60-FPS behavior is not the parity reference.

### Held-age and historical-lookback contract — Approved

#### Native behavior that must survive

The sealed audit establishes three distinct frame-count policies:

- `0x62DC60` updates held counters for all logical controls `0..19`; a held
  control increments once per history capture, a released control resets to
  zero, and the first held capture returns age `1`;
- the direction matcher accepts a fresh contributor only when age `<= 1` and
  the same control was not held two history frames earlier, while its initial
  direction pass also requires the maximum contributing age to be `<= 4`; and
- paired pressed IDs `15..19` accept one current constituent edge when the
  other constituent edge occurred within the preceding four history frames.

These are gameplay policies expressed in native frame counts. Returning raw
milliseconds from the held-age hook is invalid because the native caller still
compares the result with integer thresholds `1` and `4`.

#### Recommended time-domain translation

Define one exact authored input-policy quantum as
`Q = 1 / 60 second`. It is rational arithmetic, never repeated floating-point
addition and never `1 / target_fps`. Translate the native frame-count policies
to absolute time while preserving their integer query ABI:

- `Held(id, t)` evaluates the complete state history at absolute time `t`.
  IDs `0..9` are basic bits, `10..14` are the native constituent OR, and
  `15..19` are the native constituent AND.
- If `Held(id, t)` is false, `HeldAge(id, t) = 0`. Otherwise let `s` be the
  most recent absolute time at which that logical held predicate changed from
  false to true and compute `A_time = 1 + floor((t - s) / Q)`. Return `1`
  only in the immutable event scope whose current record created that logical
  rise; in every later scope while held, return `max(2, A_time)`. Freshness is
  therefore one transition fact, while the elapsed `<= 4` window retains its
  original absolute duration.
- A native held or direction query for `current_frame + delta` evaluates state
  at `t + delta * Q`, preserving the event's phase instead of snapping it to a
  global 60-Hz slot. The audited `current_frame - 2` test therefore means
  exactly `t - 2Q`.
- Paired-edge forgiveness is an explicit absolute interval: a current edge may
  pair with the other constituent's edge in the inclusive preceding duration
  `4Q`. It is not four render frames and does not search four reconstructed
  CBooster slots.
- Composite pressed/released IDs retain native edge algebra over their two
  constituents. They are not reduced to a single composite held-predicate
  transition.

The phase anchor is the relevant physical transition, not the nearest global
authored boundary. Thus a hold remains age `1` for one complete `Q` after its
rise regardless of whether it began just before or just after an authored
progression boundary. This avoids replacing render-dependent quantization with
a new global-grid phase dependency.

All calculations derive directly from `(query_time - predicate_start_time)` or
the requested relative offset. At 144, 165, or any other render rate there is
no accumulated counter or fractional-frame remainder.

#### Alternatives not recommended

1. **Global authored-slot emulation:** assign events to 60-Hz slots and retain
   native counter operations. This recreates the sampling boundary the
   full-history design was selected to remove and makes edge behavior depend on
   phase within a slot.
2. **Target-FPS frame units:** keep thresholds `1`, `2`, and `4` but let each
   unit mean one render frame. This shrinks established direction and paired
   forgiveness as FPS rises and changes gameplay policy even though the goal is
   judgement-time independence.
3. **Elapsed milliseconds returned directly:** clearer internally but ABI
   incompatible; the native matcher would still compare milliseconds against
   frame-count constants `1` and `4`.

### Recognition cadence and immutable-scope contract — Approved

#### Why the schedule must contain both events and heartbeats

Neither half of the schedule is sufficient by itself:

- authored-boundary calls alone collapse transitions occurring inside one
  `Q` interval because one native query view cannot represent multiple ordered
  states;
- event calls alone stop no-input miss progression, long-note lifecycle, and
  other native time-based work while the player is idle; and
- a uniform 1000-Hz native gameplay loop performs unnecessary work and changes
  the game's judgement cadence even when no input changes.

The approved schedule therefore contains exactly two kinds of recognition
scope:

1. one **event scope** for every retained journal transition record; and
2. one **heartbeat scope** at every exact authored boundary
   `B_n = session_origin + n * Q` that is not already represented by one or
   more event scopes at exactly `B_n`.

Both kinds call the original `0x5D68E0` recognition core followed immediately
by the original `0x5CF930` score consumer. Each event and each unsatisfied
boundary is delivered exactly once. A held key with no further transitions
does not cause 1000-Hz calls; only the 60-Hz heartbeat continues.

Scheduling, comparison, and deduplication use the exact rational song-time
coordinate, never the projected integer millisecond argument. Thus an event
at `16.2 ms` remains ordered before a boundary at `16 2/3 ms` even if both
native calls receive integer millisecond `16`.

#### Atomic transition and boundary ties

One journal record is one atomic transition, even if several canonical control
bits changed in that record. It is not split into one recognition call per bit.
The scope records:

- exact mapped song time and journal sequence;
- held state immediately before and after the record; and
- the record's rising and falling masks.

The event scope exposes the post-transition held state and that record's edge
masks. Two records with the same exact mapped time remain two scopes in journal
sequence. For a `(time, sequence)` scope, history includes records only through
that sequence; a later equal-time record is not visible early.

A heartbeat scope has no current rising or falling edge. It exposes state after
all records strictly earlier than its boundary. If one or more records occur at
exactly the boundary, their event scopes run in sequence and no duplicate
no-edge heartbeat call is added. The boundary is marked committed only after
the last equal-time event scope, whose state therefore includes the complete
equal-time transition group. This preserves every rapid-trigger transition
without running native lifecycle once more at the same timestamp merely for
bookkeeping.

#### Immutable, causal query view

Before each native recognition/score pair, the game-thread driver installs one
immutable active scope. Every judgement-facing CBooster query during that pair
uses the same scope:

- pressed and released are pure edge-algebra queries over the current record;
- held, held age, direction, and historical lookback use the retained history
  prefix through the scope's `(time, sequence)` coordinate; and
- both booster-component passes, descriptor lifecycle, and post-descriptor
  free input observe that same non-consuming fact.

The scope is causal. A relative query whose translated time is later than the
scope time may carry the current held state forward, but it cannot see a later
journal record merely because that record was already drained during the same
render update. A historical query at or before the scope coordinate uses the
approved `delta * Q` contract and the retained prefix. This sequence cutoff is
required to make equal-time press -> release -> press deterministic.

The scope remains installed across both `0x5D68E0` and `0x5CF930` and is cleared
after the pair through an explicit lifetime guard. Outside an active scope, all
five query hooks trampoline to the original native methods. Native callers
unrelated to this judgement driver therefore keep their existing CBooster
behavior.

#### Native arguments without a second timeline

Each scope retains its exact rational time for ordering and history. Its native
arguments are derived directly from that coordinate:

- the frame token is the containing authored index, computed from the absolute
  session origin and `Q`, never from repeated addition or render frames; an
  event exactly at `B_n` carries index `n`; and
- `recognition_ms` is one deterministic integer projection of the same exact
  coordinate for both the core and score calls.

The signed rounding rule, offsets, and pre-song convention for that integer
projection must be locked in the later session/time-conversion section. They
may not depend on render FPS. Distinct exact-time or sequence scopes remain
distinct calls even when their integer millisecond and authored-index arguments
are equal.

The frame argument does not restore native-ring authority and does not redefine
the game's global frame unit. Within an active scope, judgement history queries
interpret it through the approved absolute-time relative-offset contract.

#### Per-render delivery

At a render update the driver:

1. drains new journal records and maps each QPC timestamp to song time once;
2. derives the latest due authored boundary directly from the current absolute
   song time;
3. merges due event scopes and heartbeat scopes by exact time, applying the
   equal-boundary rule above;
4. installs each scope and invokes the native recognition/score pair; and
5. resumes the original once-per-update tail exactly once after all due work.

A render hitch changes when this ordered work is delivered, not its timestamps
or order. Events are never replayed on later heartbeat scopes. Backlog limits,
clock discontinuities, session reset, and failure behavior are deliberately
left for their dedicated section.

#### Deliberate native-cadence consequence

An event scope invokes the complete native core, not only the note handler that
might consume that event. Native miss progression, long-note lifecycle,
aggregation, and free-input policy can therefore also run at the event's exact
time before the next authored heartbeat. This is the sole intentional cadence
expansion: extra calls occur only for real input transitions, and native policy
inside each call remains untouched.

Selectively suppressing lifecycle during an event call would require splitting
or reproducing the native core and would make the loader note-aware. That is a
substantially larger semantic surface and is not proposed. The same ordered
event schedule is used at 60, 144, 165, and 240 FPS, so this consequence is
render-rate independent.

#### Alternatives not recommended

1. **Boundary-only recognition:** loses enclosed rapid-trigger transitions.
2. **Render-only recognition with a queued overlay:** still has only one
   boolean query view per call and forces the loader to decide edge ownership.
3. **Event-only recognition:** stalls time-based native work when no input
   changes.
4. **Uniform 1000-Hz recognition:** changes the entire judgement cadence and
   cost without adding information between physical transitions.

### Release-grace cadence finding — Approved

The completed native handler dump linked from E-046 exposes one additional
frame-counted policy that the query-only analysis does not absorb:

- hold handler `0x5D41B0` reloads `HoldSafeFrame` while the control is held and
  decrements the stored grace once per later recognition call while released;
- slide-hold handler `0x5D35C0` does the same with
  `SlideHoldSafeFrame`; and
- scratch and beat continuation limits compare integer millisecond
  differences against `ScratchEnableTime` and `BeatEnableTime`; they are not
  per-call counters.

If either safe-frame value is nonzero, the approved event-scope schedule would
make its grace expire according to the number of unrelated physical
transitions as well as authored heartbeats. That would not preserve the
setting's original 60-Hz meaning. This is separate from CBooster held age and
cannot be fixed by the five input-query hooks.

The supported cabinet configuration already fixes both safe-frame settings at
zero. With zero, there is no countdown to accelerate: the first scope that
observes an active hold as released follows the native immediate-release path,
using that scope's absolute time. The six-site patch boundary therefore remains
coherent for the supported configuration.

The existing E-046 handler artifact also makes the requested **live** check
practical without trusting the text file as runtime truth. Both handlers call
the same native configuration accessor at the point of use. `0x5D41B0` reads
its returned object's DWORD field `25` (byte offset `0x64`) for
`HoldSafeFrame`; `0x5D35C0` reads DWORD field `26` (byte offset `0x68`) for
`SlideHoldSafeFrame`. The loader may call that already-audited accessor and
read those same two fields at fresh-session activation. The deployed
`system.cfg` values of zero remain useful startup diagnostics, but parsing the
file alone is not the activation proof.

Recommended current scope:

1. make `HoldSafeFrame == 0` and `SlideHoldSafeFrame == 0` explicit activation
   preconditions;
2. validate the live native values before an absolute-time gameplay session
   becomes active and never silently run this design with nonzero values; and
3. defer nonzero grace support to a separate extension, which would need two
   narrowly scoped policy-tick guards so only an authored heartbeat (or its
   equal-time event substitute) advances those countdown state machines.

Converting nonzero grace to loader-owned absolute expiry timestamps is not
recommended for the current patch. It would duplicate per-component long-note
state and be harder to prove than either the zero-only contract or two future
native branch guards.

Approved scope: only `HoldSafeFrame == 0` and `SlideHoldSafeFrame == 0` are
accepted. A nonzero live value is an unsupported configuration and must not
silently activate the absolute-time judgement path.

### Direction-head freshness refinement — Approved

The consumer-level review exposes a replay problem in the earlier pure elapsed
held-age formula. Native direction matcher `0x5D2E50` uses
`consecutive_held_age <= 1` as an implicit first-sampled-frame marker for flick
and slide-hold heads. Stock native cadence calls recognition only once for that
sampled frame. Under the approved event schedule, several unrelated event
scopes may occur during the first `Q` after a direction rises. Returning age
`1` throughout that duration would let the same physical rise qualify as a
fresh direction head more than once.

Refine the active-scope held-age ABI as follows. First compute the already
approved elapsed value

`A_time(id, t) = 1 + floor((t - s) / Q)`

for a held logical predicate whose most recent false-to-true time is `s`.
Then return:

- `0` when the logical control is not held;
- `1` only when the current event record itself changes that logical held
  predicate from false to true; and
- `max(2, A_time(id, t))` in every later scope while it remains held.

This makes freshness a property of one immutable transition scope rather than
an interval. It does not shorten the native `<= 4` companion-age policy:

- the rising scope has age `1`;
- later scopes before `s + 2Q` report age `2`;
- ages `3` and `4` cover the next two quanta; and
- age becomes `5` at exactly `s + 4Q`.

Thus a newly rising contributor can combine with another held direction for
the same absolute four-quantum duration, but an older contributor cannot
pretend to be a second head merely because another key changed. Multiple bits
rising in one atomic journal record are all fresh in that one scope. Equal-time
records remain sequence-aware: an earlier record's rise is age `2` in a later
record at the same mapped time, while a release -> repress record becomes a
new age-`1` rise.

State seeded as already held at session start has no rising event scope and
therefore starts at age at least `2`; it cannot synthesize a flick/slide head.
The native audit identifies the direction matcher as the judgement consumer of
this counter, so the refinement stays at the existing held-age query hook and
does not add note-family knowledge.

#### Alternatives not recommended

1. **Keep age `1` for all of the first `Q`:** replays one direction rise on
   later event scopes and contradicts native edge uniqueness.
2. **Add a direction-matcher head hook:** an explicit current-rise check at
   `0x5D2E50` can also prevent replay, but adds another native patch site and
   couples the loader to matcher head/continuation arguments unnecessarily.
3. **Run direction notes only at heartbeats:** recreates the sampling window
   and can lose short rapid-trigger direction transitions.

#### Judgement-impact verification

The completed wrapper and handler dumps establish this result statically:

- `GameplayInput_GetConsecutiveHeldFrameCount` has one judgement consumer,
  direction matcher `0x5D2E50`;
- matcher head mode (`a8 == 0`) uses age `<= 1` to create the fresh-contributor
  flag, then requires the maximum contributing age to be `<= 4` before testing
  the native direction vector;
- matcher continuation mode (`a8 == 1`) accepts any currently held
  contributor and does not use age to decide freshness;
- flick handler `0x5D3320` calls head mode, and an accepted match records the
  current `recognition_ms` before native timing grade and publication; and
- slide-hold handler `0x5D35C0` calls head mode before start and continuation
  mode after start.

Consequently:

1. **The intended head is unchanged.** In the actual rising event scope the
   contributor still returns age `1`, the native vector is unchanged, and the
   exact event-time millisecond still drives late gating and grade.
2. **Same-scope native sharing is unchanged.** Every matcher invocation inside
   that one core call sees the same age-`1` fact; no descriptor- or
   component-level consumption is introduced.
3. **Only cross-scope replay is removed.** A later unrelated transition while
   the direction remains held now reports age `2`, so it cannot make that old
   rise qualify as another flick/slide head or reach grading for a newly
   eligible candidate.
4. **A genuinely new direction is unchanged.** Its current rise reports age
   `1`; earlier held contributors report age `2..4`, remain inside the native
   companion window, and still contribute to the same direction vector.
5. **Slide continuation and duration are unchanged.** Continuation tests held
   state rather than head freshness. Normal taps, holds, scratch, beat,
   composite/paired taps, and free input do not consume this held-age value.

Verdict: the refinement does affect judgement only by preventing an old
physical direction rise from producing an additional result in a later scope.
It does not alter the result or grade of the intended rise. This is static
native-control-flow proof; final runtime/cabinet acceptance remains required.

### Judgement-consumer contract — Approved

#### Scope supplies facts; native code chooses their meaning

The loader does not receive a note descriptor, effective type, component, or
candidate identity when answering input. It exposes one control fact from the
active immutable scope and leaves all note-family interpretation above the
query seam.

This deliberately removes the old loader note-type routing table. Native
normalization (`B -> A`, `C/E -> 9`, `D -> 4`), mode rewrites, equal-time
suppression, effective-type dispatch, candidate order, component order,
handler return behavior, lifecycle, grades, and publication remain untouched.

The current Switch gameplay-wrapper hooks are compatible with this boundary.
For each requested button or direction alias they call the original gameplay
wrapper, which in turn reaches the lower CBooster method. During an active
scope that lower method answers from absolute history; outside a scope it
trampolines to native. Switch retains alias selection and its independent
diagonal-match policy without owning a second input timeline.

#### Exact logical-control algebra

For `k in 0..4`, define constituent pair `P_k = (k, k + 5)`. IDs `0..9`
are ordinary controls, `10 + k` is the composite form of `P_k`, and `15 + k`
is the paired form.

For an event scope at coordinate `(t, sequence)`:

- an ordinary `Pressed(c)` is true exactly when `c` is in that record's rising
  mask;
- ordinary `Released(c)` is true exactly when `c` is in that record's falling
  mask; and
- ordinary `Held(c)` is the post-transition state in the retained causal
  history prefix.

A heartbeat has no current pressed or released edge. No ordinary edge is
replayed merely because it occurred inside the preceding `Q`; current-edge
truth belongs exclusively to its event record.

Logical IDs retain native algebra:

| Query | Composite `10 + k` | Paired `15 + k` |
|---|---|---|
| Held | either constituent held | both constituents held |
| Pressed | either constituent rises in the current record | both rise in the current record, or exactly one rises now and the other has a rising record in the inclusive preceding `4Q` |
| Released | either constituent falls in the current record | native-symmetric rule using current and preceding falling records |

A paired edge always requires at least one constituent edge in the current
scope. Two merely historical edges cannot create a new current paired edge.
The companion search is exact-time and sequence-aware: an earlier record at the
same mapped time is eligible, while a later equal-time record is not visible.
It does not consume the companion and it does not expose that old edge as an
ordinary current press.

Held age follows the approved scope-aware formula over the logical held
predicate. Direction queries evaluate the retained held mask at the translated
query time and reuse the native mask-to-vector and vector-to-direction helpers,
including native opposing-direction priority. The loader does not introduce a
second direction normalization table.

Relative held and direction queries use `scope_time + delta * Q` within the
scope's causal history prefix. Result-affecting historical pressed-edge search
is limited to the explicit paired rule above; it is never implemented as a
generic one-quantum pulse that could replay on a heartbeat.

#### Native consumer matrix

| Effective family | Scoped input fact | Native behavior retained |
|---|---|---|
| Tap `1`, variant `6`, `HIDDEN` `7`, `HIDDEN2` `8` | Current pressed edge and the scope's exact time | Candidate selection, late gate, timing grade, result/effect publication |
| Flick `2` | Current direction rise through age-`1` freshness, held mask, and native direction vector | Target-angle tolerance, late gate, grade, handler success |
| Hold `3` and dual-hold `F` | Current pressed start; post-transition held state thereafter | Component conflicts, start/end state, duration grade, aggregation; zero safe-frame immediate release |
| Scratch `4` | The four native directional current-edge queries | Native direction priority/sequence, `ScratchEnableTime` millisecond interval, duration/result |
| Beat `5` | Each physical repeat edge in its own event scope | `BeatEnableTime` millisecond interval, duration/result |
| Critical/paired tap `9` | Current composite/paired edge algebra shared by both booster components | Native paired/component aggregation and grade/result policy |
| Slide-hold `A` (including raw `B`) | One fresh direction head; held state and direction vector for continuation | Target matching, continuation, duration grade; zero safe-frame immediate release |
| Suppressed effective `0` | No invented input behavior | Native candidate construction skips it |
| Post-descriptor free input | Current controls `4`/`9` pressed fact from the same scope | `IsMute`, active-component conflicts, 200-ms/miss-mark gates, effect type `4`; no chart grade or score |

Mode conversions remain native. For example, a mode that converts scratch or
slide-hold to effective hold automatically reaches the hold handler and consumes
the same generic scoped facts; the loader never branches on the raw type.

#### Edge ownership and result timing

Queries are pure and non-consuming. Both booster-component passes, every
candidate already present in their fixed lists, descriptor lifecycle, and
post-descriptor free input may observe the same current scope fact. Native
control flow—not a loader claim table—decides which result or effect is
produced.

A following descriptor in the same native chart row is absent from the fixed
candidate list for that scope. When it becomes eligible in a later scope, the
earlier ordinary edge and direction-head freshness are gone. Only a distinct
new physical rise can supply another ordinary head; paired companion lookback
remains the explicit native exception.

The scope time passed to the native core is also passed to score. Therefore an
accepted tap, flick, hold head, scratch step, beat step, or slide head reaches
native late gating and grading at the event's deterministic integer
millisecond projection. No per-handler timestamp sidecar or late/grade hook is
needed. Heartbeats provide no-input progression, while long-form held/release
state and native millisecond interval policies observe the exact ordered event
history.

#### Alternatives not recommended

1. **Loader note-family routing:** repeats the prior raw/effective-type,
   descriptor-lifetime, and free-input ownership failures.
2. **Descriptor-level edge consumption:** contradicts native non-consuming
   component and candidate behavior.
3. **Generic one-quantum edge pulses:** replay an already delivered edge on a
   later heartbeat or unrelated event scope.
4. **Calling native composite/paired methods wholesale:** those methods reject
   or index through the obsolete native ring before reaching their small
   recursive algebra.
5. **A separate free-input overlay:** duplicates native `IsMute`, conflict, and
   timing gates and can disagree with chart-note handling in the same scope.

### Catch-up, clock epoch, and session contract — Approved

#### Two progress values, one time coordinate

“Catch-up” means delivery of recognition scopes whose original absolute times
passed while the render/game thread was stalled. It does **not** mean polling
the device again, replaying an edge, stamping old input with the resume time, or
inventing render frames.

The design distinguishes:

- **ready time**: the latest exact judgement time known from the audio clock;
  and
- **committed time/index**: the latest scope and authored boundary already
  delivered to native judgement.

Both values use one judgement coordinate. Let `R(q)` be the audio-source
position at QPC `q`, supplied by one audio-owned playback/clock epoch, and let
`G` be the session's immutable `GameTimeOffset`. Define

`J(q) = R(q) + G`.

`J` is the same frame-derived coordinate that the native caller normally passes
to `0x5D68E0` and `0x5CF930`: zero is native authored frame zero. The
`session_origin` named in the earlier boundary formulas means this native zero,
not the render update on which the loader first happened to see an anchor.
Accordingly:

- heartbeat boundaries are `B_n = n * Q`, with `Q = 1/60 second`;
- an event's containing frame token is the mathematical
  `floor(J(event) / Q)`;
- the integer argument passed to both native functions is checked truncation
  toward zero of `J(event)` in milliseconds, matching the native integer
  conversion convention; and
- native per-player/audio-group bases, `JudgTimeOffset`, windows, and every
  other native additive term remain native and are not added a second time by
  the loader.

The exact audio clock must expose a playback epoch and a render-independent
QPC-to-source-time mapping. For the first implementation, the approved WASAPI
endpoint observations maintain that mapping; the event result cannot depend on
which render update drained it. The current implementation's fresh render-side
cursor midpoint,
25-ms regression tolerance, monotonic clamp, and arbitrary
`current-frame <-> current-anchor` correction are rejected. They can map the
same physical QPC differently according to observation cadence and can silently
move an event in time.

An exact source-frame observation with a playback/clock epoch is therefore an
activation requirement. The current rounded-millisecond fallback has neither
the stable identity nor the precision needed for this contract and may not
silently drive absolute judgement. Normal clock-calibration samples may update
an audio-owned mapping only when continuity is proven; a reset, seek, or
unprovable replacement creates a new epoch.

#### Rare transport misses — Approved exception

The current transport is memory-safe: it uses an atomic aggregate mask and a
mutex-protected journal. It nevertheless has a very small ordering window. The
producer publishes the new aggregate mask, obtains a transition QPC, and only
then locks and pushes the journal record. The game thread can drain between
those operations, advance judgement beyond that QPC, and receive the older
record on its next update.

This is not a failed hardware read, a C++ data race, or the cause of the current
total input blackout. It requires a real transition plus a very narrow thread
interleaving, unless the producer is preempted in that window. It is expected to
be rare in normal operation, but that expectation is not a measured guarantee.

The user accepts this rare miss to keep the transport and judgement design
simple. Do not add a completed-poll watermark, a combined producer/consumer
transaction, or an extra poll-delay protocol for this case. The hard
render-independence guarantee applies to transitions the 1000-Hz transport
successfully publishes before judgement commits past their mapped time.

When a drained record maps at or before the already committed coordinate:

1. do not replay native judgement in the past;
2. do not move the edge to the current time, because that could hit a different
   note and changes its grade;
3. count and log one late-transition anomaly;
4. apply records in sequence only to resynchronize the retained current held
   mask at the committed coordinate; and
5. expose no pressed/released edge, paired companion, age-`1` freshness, or
   direction head for the missed record.

Thus the one edge is intentionally missed, but later held/released state does
not remain stuck. A late press may be held in later scopes but cannot start a
tap/hold or direction head; a late release makes later scopes unheld and, under
the approved zero-safe-frame configuration, may finalize an active long note at
the next delivered scope rather than its exact release time. These are accepted
consequences of the transport exception.

A physical press and release that both occur entirely between two 1000-Hz polls
produce no record at all and are another inherent transport miss. At nominal
polling this requires a pulse shorter than roughly one millisecond with the
unfortunate poll phase. Judgement cannot recover or log an event the input
worker never observed. Journal eviction, sequence corruption, or transport
epoch loss is not this one-edge exception: it can lose an unknown amount of
history and still invalidates the active session.

#### Bounded native catch-up, lossless event retention

Reuse the native song-clock catch-up shape in the authored domain. Let `c` be
the last committed authored boundary index and
`target = floor(ready / Q)`. A normal outer update may advance at most three
authored boundaries (`3Q = 50 ms`), matching the existing 60-Hz
`GameplaySongClock` catch-up limit.

The delivery horizon is:

- `B_(c+3)` when `target - c > 3`; or
- the exact `ready` coordinate when the remaining authored-boundary backlog is
  at most three.

This distinction matters in steady high-FPS play. When no new boundary is due,
the horizon is still exact `ready`, so a physical transition receives an event
scope on the next render delivery; it does not wait for the next 60-Hz
heartbeat. During a large backlog, later events remain queued until the native
authored state reaches their time.

For each outer update:

1. retain every newly drained record and map its QPC once through its clock
   epoch;
2. compute the horizon directly from absolute indices, never by accumulated
   fractional steps;
3. deliver every event and boundary scope at or before the horizon in the
   already approved exact-time/sequence order, without splitting an equal-time
   group;
4. set native `Tune+0x14` to the number of authored boundaries crossed in this
   batch, not the number of event scopes; and
5. run the original once-per-update tail once, after the complete batch, so the
   existing native Tune commit and outer progression remain in their authored
   order.

There is no normal semantic cap on the number of physical event scopes inside
the selected time batch: every retained transition is delivered. Boundary
backlog consumes no queue capacity because boundaries are derived from their
absolute indices. Event backlog remains explicitly retained across updates;
the current behavior that drains a record and then forgets it merely because it
lies beyond this update's horizon is forbidden.

The three-boundary cap changes only recovery latency. A ten-frame hitch, for
example, is delivered as successive batches of at most three authored
boundaries, while all intervening transitions retain their original times and
order. It does not collapse ten frames to three, and it does not discard the
remaining seven. This preserves the native outer catch-up/tail shape instead of
running judgement arbitrarily far ahead of `Tune` state in one render update.

A finite transport allocation is still necessary. Exhaustion, arithmetic
overflow, or the existing journal's oldest-record eviction is not a gameplay
policy and cannot be repaired after the fact. It invalidates the active session
rather than silently dropping old work, synthesizing a final state, or changing
to native CBooster judgement mid-song.

#### Explicit session identity and boundaries

An absolute-judgement session is tied to all of the following:

- one native Tune/judgement/score-state identity and lifecycle generation;
- one compatible audio playback/clock epoch;
- one input transport epoch and session-start sequence cutoff;
- one immutable `GameTimeOffset` and validated zero-safe-frame configuration;
  and
- one committed authored boundary index that agrees with native Tune state.

Session start is a native lifecycle event, not “the first update that happened
to have an anchor” and not a timeout heuristic. It is permitted only for a new
native judgement state before absolute and native histories have been mixed.
At start the driver drains and discards all currently queued pre-session
records, records their last sequence as its cutoff, and seeds the currently
published held mask as baseline state with no rising or falling edge and held
age at least `2`. A key already down before gameplay can therefore continue as
held state but cannot synthesize a tap, free-input effect, flick head, or slide
head. A producer record that crosses this start operation is governed by the
approved rare-late policy above.

The current-frame/anchor alignment is not captured as an arbitrary correction.
The session validates native `Tune` progression against the absolute
`J = audio source time + GameTimeOffset` target. Failure to establish that
relationship at the fresh native lifecycle boundary prevents activation; the
driver must not attach halfway through a song after native CBooster judgement
has already processed earlier time.

Session end clears the active scope, pending records, retained-history baseline,
clock mapping, sequence cutoff, and committed indices together. A following
Tune/judgement state or restart receives a new session identity. No transition,
held age, paired-edge lookback, or clock correction crosses that boundary.

#### Hitch, temporary clock unavailability, and discontinuity

These cases are deliberately different:

- **Render hitch:** the clock epoch remains the same and `J` advances
  monotonically. The gap may be arbitrarily larger than one render interval;
  it is catch-up work and never causes a rebase merely because it is large.
- **Temporary clock-read unavailability:** freeze the delivery horizon, retain
  and drain transport records, and do not call native fallback judgement. If
  the same epoch and continuous mapping return before history is lost, resume
  with ordered catch-up.
- **Discontinuity:** a backward clock result, seek/reset epoch, changed
  `GameTimeOffset`, unexplained playback-generation replacement, input epoch
  replacement, or native-state replacement that does not form a fresh matched
  lifecycle boundary. Native judgement already published before the break
  cannot be undone, so the driver must not rebase and continue the same
  session.

The game has no active-gameplay pause state in scope for this patch. Do not add
one. A render/game-thread lag is the first case above: the audio clock continues
and all missed scopes catch up. Normal song end/restart ends the judgement
session. If the gameplay audio clock unexpectedly stops or becomes inactive
while the same native judgement session remains active, that is clock
unavailability or a discontinuity—not a pause whose input should be suppressed.
The exact externally visible failure action is deferred to the error-handling
and activation section; silent native fallback within an already-active session
is already ruled out.

#### Worked examples in plain terms

**Example 1 — a press between 60-Hz boundaries is not delayed to the
boundary.** Assume the last heartbeat was exactly `10,000.000 ms`, the next one
is `10,016.667 ms`, and the player presses at `10,005.400 ms`. At the next
render update the exact audio clock has reached `10,008.000 ms` and the press
record is present in the drained journal.

There is no new heartbeat yet, but the event is ready. The driver immediately
runs one event scope at exact time `10,005.400 ms`; native core and score both
receive integer `10005`. At 240 FPS that call will normally be delivered within
one roughly 4.17-ms render update. At 60 FPS it may not be delivered until the
next roughly 16.67-ms render update, but it still carries `10005`, so the grade
is the same. Only visible response latency differs.

**Example 2 — a 200-ms render hitch does not merge rapid-trigger input.** The
last committed boundary is `10,000.000 ms`. Rendering freezes while audio and
the input worker continue. During the freeze the journal records:

```text
10,023.400 ms  press A
10,031.000 ms  release A
10,044.800 ms  press A again
```

Rendering resumes when ready time is `10,200.000 ms`. The first catch-up batch
ends at `10,050.000 ms`, three authored boundaries later. Its native calls are
ordered like this:

```text
10,016.667  heartbeat, no edge
10,023.400  first A press
10,031.000  A release
10,033.333  heartbeat, no edge
10,044.800  second A press
10,050.000  heartbeat, no edge
```

The two presses remain two presses even though both occurred inside 50 ms. The
native integer arguments are respectively `10016`, `10023`, `10031`, `10033`,
`10044`, and `10050`; none is replaced with resume time `10200`.

The remaining `150 ms` is **not dropped**. Later render updates deliver batches
through `10,100`, `10,150`, and finally exact ready time `10,200`, including
every transition that belongs in those ranges. “50-ms batching” therefore
means at most 50 ms of native Tune progression per outer update. It is not a
50-ms input sampling or fusion window.

**Example 3 — the current handoff race and the simpler correction.** Suppose
the current input thread observes a press, changes the shared aggregate mask,
obtains QPC corresponding to `10,005.400 ms`, and is then preempted before
pushing the journal record. The game thread can do this:

```text
drain journal              -> empty
observe audio at 10,008    -> process judgement through 10,008
input thread resumes       -> inserts the older 10,005.400 press
```

The driver has already passed the press and native results cannot be rolled
back. Under the approved simplification it logs one late transition and does
**not** run a press scope. It updates only its retained held baseline to
`A = held`. Consequently the press cannot hit a tap/hold/flick at `10,008`, but
later scopes do not incorrectly believe A is released forever. This is the
accepted rare missed edge; no extra synchronization protocol is added.

**Example 4 — why a per-render clock anchor is unsafe.** Consider one physical
press with one QPC timestamp. A render-side cursor observation taken just before
an audio-buffer update might map it to illustrative time `9,998 ms`; an
observation taken just after that update might map the same QPC to `10,008 ms`.
Which observation is used can differ at 60 and 240 FPS. A 25-ms tolerance and
clamp only hide the contradiction by choosing one shifted value.

The required audio-owned clock instead says, for playback epoch `17`, how any
QPC maps onto the presented source-frame timeline. For example, that one press
maps to `10,003.400 ms` in epoch `17` regardless of whether a render update
drains it before or after an audio callback. Both event time and current ready
time use that same mapping.

**Example 5 — session start does not invent an edge.** The player is already
holding A when a new song's judgement state is created. The session seeds
`A = held`, but supplies no pressed edge and held age is at least `2`. Therefore
the held key cannot hit the first tap, create free input, or look like a fresh
flick/slide head. If the player releases and presses again after session start,
those two new records are ordinary event scopes and the new press can judge.

**Example 6 — a seek cannot be repaired by rebasing.** Native judgement has
already published results through song time `30,000 ms`, then the audio cursor
seeks backward to `10,000 ms` without recreating the native judgement state.
Continuing would either judge the `10–30 s` notes twice or require undoing
native score/lifecycle state. The session therefore fails instead of pretending
that `10,000` is a new origin. A genuine song restart is allowed because it
creates new native state, a new playback epoch, and a new input cutoff together.

#### Alternatives not recommended

1. **Completed-poll watermark or atomic producer/consumer cutoff:** closes the
   rare handoff window, but adds transport coordination and/or delivery delay
   the user explicitly chose not to carry for this rare case.
2. **Move a late transition to current time:** avoids losing an edge but can hit
   a different note and supplies a knowingly false grade timestamp.
3. **Drain the entire observed-time backlog before native Tune catches up:**
   makes judgement progression run arbitrarily ahead of the native outer state
   and discards the existing 50-ms catch-up discipline for no required timing
   benefit.
4. **Drop work beyond three authored frames:** confuses a per-update delivery
   cap with a retention cap and makes a hitch change results.
5. **Clamp a backwards clock or rebuild an origin from the current frame:**
   hides an invalid clock relationship by moving input timestamps.
6. **Fall back to native CBooster mid-session:** mixes two different histories
   after some results have already been published and cannot preserve the hard
   goal.

### Activation evidence audit — Recorded before proposal

The current source's `target_fps != 60` gates are leftovers from the failed
hybrid design, not a valid boundary for this redesign:

- `JudgementTimingPatchRequired`, `JudgementTimingPatchInit`, and the judgement
  query-plan gate in `SwitchInputPatch` all omit the judgement mechanism at
  `60 FPS`;
- the repository nevertheless classifies `60`, `120`, `144`, `165`, `240`,
  and `360 FPS` as gameplay-validated target rates; and
- the approved hard goal compares one physical event schedule across
  `60/144/165/240`, while the approved event-scope model deliberately recognizes
  sub-`Q` pulses that stock once-per-render 60-FPS sampling can lose.

Therefore leaving the redesign disabled at 60 would compare two different
input/judgement mechanisms and could not satisfy the stated invariant. Render
timing may remain native at 60, but an enabled absolute-time judgement feature
must install and run its scheduler and five query providers there as well.

Static hook installation and gameplay-session activation are separate gates.
The present `IsAudioHookCommitted()` flag proves only that an audio interception
was installed; it does not prove that the active BGM exposes the stable exact
source-frame/playback-epoch mapping required above. Installation can preflight
the executable, hook sites, backend capability, input journal, and native live
configuration accessor. A fresh gameplay session may activate only after the
actual playback epoch and native judgement identity can be matched before the
first recognition step. Rounded-millisecond observations and mid-song attach
remain forbidden.

### Feature activation and 60-FPS behavior — Approved with explicit toggle

When absolute-time judgement is enabled, use the same mechanism at every
supported target rate, including `60 FPS`:

1. install the scheduler seam and all five CBooster query hooks at 60 just as
   at 144/165/240; a later additional rate uses the same mechanism only after
   passing the approved feature-specific acceptance gate;
2. leave unrelated render/framerate transforms in their existing native-60
   mode;
3. activate event and heartbeat scopes only inside a valid fresh gameplay
   judgement session; and
4. let all five query hooks fall through to the original native functions when
   there is no active scope, so menus, attract-mode code, and unrelated callers
   keep native behavior.

The existing higher-level Switch alias hooks remain owned by the Switch-input
feature. They are installed only when that input style needs them. The five
lower CBooster history-query hooks are instead part of absolute-time judgement
and must not inherit `SwitchInputPatch`'s current `target_fps != 60` gate.

#### Concrete 60-FPS example

Suppose one render interval spans `10,000.000` through `10,016.667 ms` and the
1000-Hz transport observes:

```text
10,005.400  press A
10,012.100  release A
```

At the next 60-FPS outer update the instantaneous held state is already false,
so stock once-per-render input can miss the tap entirely. The proposed feature
instead invokes native recognition at `10,005.400` with the pressed edge, again
at `10,012.100` with the released edge, then commits the `10,016.667`
heartbeat. The press can judge at native integer time `10005` even though its
visual/effect result is not displayed until the next render.

At 240 FPS the same records will usually be delivered sooner, but they carry
the same times and produce the same judgement inputs. Thus higher FPS improves
delivery/visual latency without changing the grade. Leaving the feature off at
60 would make this input register at 240 and disappear at 60, directly
contradicting the hard goal.

#### Intentional compatibility consequence

Absolute-time mode at 60 is not byte-for-byte stock input behavior. It fixes
stock's render-sampling loss for sub-frame pulses and grades recognized edges
at their event time. Native candidate order, handler policy, scoring, effects,
and idle 60-Hz lifecycle cadence remain in place. This is the smallest coherent
meaning of one render-independent judgement mechanism.

It is permissible to retain a separately labelled **stock compatibility mode**
for deployments that do not enable or cannot support absolute-time judgement.
That mode makes no cross-FPS absolute-time guarantee. The loader must never
silently fall back to it after reporting that absolute-time mode is active.

#### Settings toggle contract

Add one startup-only setting under `[experimental]`:

```toml
enable_absolute_time_judgement = false
```

Its two states are deliberately complete modes, not degrees of activation:

- **`false` — stock compatibility mode.** Do not install the scheduler seam,
  the five CBooster query hooks, or an absolute judgement session. Native
  CBooster sampling and judgement remain authoritative. Independently enabled
  render/audio patches may still run, but judgement is frame-dependent and no
  `60/144/165/240` parity claim applies. At a non-60 target, emit one clear
  startup warning stating that absolute-time judgement is disabled.
- **`true` — absolute-time mode.** Install and require the entire six-site
  mechanism at every supported target FPS, including 60. All exact-clock,
  journal, executable, native-state, and zero-safe-frame preconditions apply.
  A failed precondition follows the explicit failure contract in the next
  section; it never selects stock compatibility mode automatically.

The current failed four-hook hybrid implementation is removed rather than
retained as a third fallback mode. Otherwise `false`, `true`, and a partially
active legacy path would have three different semantics and make field failures
ambiguous again.

The recommended checked-in/template default is `false`. The repository's
current default `audio_backend = 'directsound'` cannot provide the required
exact playback-epoch mapping, so defaulting the new setting to true would make
an otherwise stock configuration fail at launch. A deployment choosing the
feature, such as the current 240-FPS/WASAPI setup, sets it explicitly to true.
The setting is read once at startup and requires restarting the game; it cannot
switch judgement authority during a song or between already-created native
states.

### Activation states and failure behavior — Approved

Absolute-time mode has three explicit states: **installed**, **armed**, and
**active**. This separates facts that can be proved while the loader starts
from facts that exist only after a particular BGM playback begins.

#### 1. Installed: process-start preflight

When `enable_absolute_time_judgement = true`, validate before exposing an
operational mode:

- the exact supported executable and signatures for the scheduler seam and all
  five query sites;
- a supported, individually gameplay-validated target FPS;
- a valid monotonic QPC frequency and the configured 1000-Hz input transport;
- `audio_backend = 'wasapi_exclusive'` with the exact
  source-frame/playback-epoch clock provider active, rather than merely
  reporting that an audio hook was committed;
- the input journal and the already-audited live native configuration accessor;
  and
- successful transactional installation of all six judgement sites, rolling
  the judgement transaction back if any one site fails.

A failure here happens before gameplay: write a specific error to the log, show
one startup error message with the failed requirement and remediation, and
fail DLL/game startup. For example:

```text
Absolute-time judgement is enabled, but audio_backend='directsound'
does not provide an exact playback epoch.
Use audio_backend='wasapi_exclusive' or set
enable_absolute_time_judgement=false.
```

This is not a reason to toggle the setting internally. The user's requested
mode either installs completely or does not start.

#### 2. Armed: fresh native state, playback not active yet

The scheduler seam may see a newly created native judgement state just before
the matching BGM epoch has started. That is an ordinary startup ordering case,
not an error and not a reason to invent a timeout.

While armed, the driver:

1. verifies that native judgement has not already advanced;
2. establishes the input cutoff and held baseline approved above;
3. retains new journal records;
4. withholds native recognition and the corresponding Tune commitment; and
5. waits only while the audio provider explicitly reports **no playback epoch
   yet**.

The audio provider must publish an active generation and its exact epoch
mapping as one coherent state. Once present, the driver validates the native
identity, initial Tune relationship, immutable `GameTimeOffset`, and live
`HoldSafeFrame == 0` / `SlideHoldSafeFrame == 0`, then becomes active and
delivers any ready retained work in the approved bounded batches.

“Playback is active, but there is no exact epoch” is not the same as “playback
has not started.” It means the required clock contract failed and is fatal
before the first recognition step. Likewise, a native state that has already
processed judgement cannot be attached retrospectively.

#### 3. Active: one continuous session

The following conditions are recoverable because they do not yet contradict
the session history:

- one or more clock reads are temporarily unavailable while the provider still
  identifies the same continuous playback epoch: freeze the ready horizon,
  retain input, call no native fallback, and catch up when reads return;
- a render hitch with a valid advancing clock: perform ordinary bounded
  catch-up; and
- the approved rare late handoff record: count/log it, drop its edge, and
  resynchronize only held baseline state.

There is no arbitrary `N ms` timeout for a temporary clock-read failure. It
becomes fatal only when the provider declares the epoch lost/discontinuous or
the delay outlives retained history.

The following conditions are fatal because continuing would knowingly create
or hide a false result:

- the audio time moves backward, the playback generation changes, or exact
  mapping is lost without a matching fresh native-session boundary;
- the native judgement object, input epoch, immutable offset, or validated
  zero-safe-frame values change inside the session;
- the journal evicts a record, sequence ordering is corrupt, or retained
  history can no longer cover the frozen interval;
- schedule/time arithmetic overflows or violates monotonic ordering; or
- an active-scope native pointer/site invariant fails.

#### Fatal action

On an active-session fatal condition, latch the first failure so concurrent
reports cannot obscure it, stop issuing native recognition calls immediately,
and write one structured snapshot containing at least:

- reason and mode/target FPS;
- native session identity and audio playback generation;
- last exact audio frame/QPC mapping, last delivered `J`, and committed boundary;
- pending event/boundary counts and last input sequence;
- held mask, late-record count, and journal eviction count; and
- both live safe-frame values and immutable offsets.

Then flush the diagnostic record and terminate the game immediately through one
shared fatal path. A minidump is useful if the existing crash infrastructure can
produce it without delaying termination, but the structured snapshot is
mandatory and cannot depend on dump success. Do not show a blocking in-song
message box before termination; that would leave an invalid session and audio
running until somebody dismisses it.

Process termination is recommended because the audited seam gives us control
over judgement delivery, not a proven transaction for undoing results and
returning every gameplay subsystem safely to song select. Keeping the game open
with judgement permanently suppressed recreates the current “input does
nothing” failure, while native fallback mixes incompatible histories.

#### Concrete failure examples

1. **Harmless transient:** at `20,000 ms`, one WASAPI clock read fails. The same
   playback generation returns at `20,008 ms`, no journal record was lost, and
   delivery catches up. The song continues.
2. **Fatal seek:** results were published through `30,000 ms`, but the same
   native state reports audio at `10,000 ms`. The loader records both positions
   and terminates rather than judging `10–30 s` twice.
3. **Fatal incomplete input:** a long stall raises the journal eviction count.
   The loader cannot know which press/release was dropped, so it terminates. A
   single late handoff record with no eviction remains the explicitly accepted
   nonfatal miss.
4. **Fatal unsupported live policy:** the session sees
   `HoldSafeFrame = 2`. It refuses activation/terminates before processing a
   note, because event scopes would decrement that native counter faster than
   its authored meaning. Setting the feature toggle to false permits the stock
   path to use that configuration.

#### Rejected recovery alternatives

1. **Fall back to stock judgement:** results already emitted by absolute-time
   history cannot be reconciled with CBooster's frame ring.
2. **Rebase after a seek/generation change:** can replay or skip notes whose
   native score state cannot be undone.
3. **Keep the game running with recognition disabled:** visibly reproduces the
   no-input/no-judgement blackout and may leave a bad score/session alive.
4. **Force a return to song select:** would require a new, separately audited
   multi-subsystem gameplay teardown patch outside the chosen six-site scope.

### Runtime observability — Approved

The 2026-08-19 log could prove hook installation, input-worker activity,
WASAPI activity, and 240-FPS rendering, but not that one judgement scope was
built, one native recognition/score pair ran, or one grade was consumed. That
must be impossible with the redesign's ordinary release diagnostics.

Use two runtime detail levels, both compiled into normal builds.

#### Always-on `Info` lifecycle and summaries

Emit exactly one startup mode record, one session-start record, a compact
active-session summary at the existing roughly five-second diagnostic cadence,
and one session-end record. Do not log every heartbeat or query at `Info`.

The startup record states:

- setting state (`stock` or `absolute`), target FPS, input poll rate, and audio
  backend;
- exact-clock provider/capability and the absence of a rounded fallback; and
- the six guarded sites installed, or `sites=0` in stock mode.

The session-start record states:

- native judgement identity, audio playback generation/source rate, and input
  epoch/cutoff sequence;
- exact epoch mapping in integer source-frame/QPC form;
- initial `J`, native Tune/boundary index, immutable `GameTimeOffset`, held
  baseline, and both live safe-frame values; and
- whether activation entered `armed` first and how many outer calls were
  withheld before the exact epoch became ready.

Each periodic/session-end summary contains cumulative and interval values for:

1. **Transport:** records drained; rising/falling masks observed; pending and
   maximum journal depth; late records; evictions; and sequence errors.
2. **Clock:** exact reads, temporarily unavailable reads, playback generation,
   last source frame/QPC, last `J`, backward/discontinuity errors, and
   `rounded_fallback=0`.
3. **Schedule:** outer scheduler calls; event scopes; heartbeat scopes;
   equal-boundary substitutions; authored boundaries committed; batches;
   maximum batch size; pending scopes; maximum backlog duration; and largest
   event-delivery delay.
4. **Native execution:** recognition calls and score calls. Both counts must
   equal total delivered scopes; a mismatch is an invariant failure rather
   than a harmless statistic.
5. **Queries inside scopes:** call counts for pressed, held, released,
   direction, and held-age; true/nonzero result counts; age-`1` returns versus
   age-`>=2` returns; and any query made during an active session outside a
   driver scope.
6. **Native score results:** MISS/GOOD/COOL/GREAT counter deltas.

The last item needs no seventh production hook. E-044 proves that the score
object already passed to `0x5CF930` owns those four counters at
`+120/+124/+128/+132`. The driver snapshots them immediately before and after
the original score call and records unsigned deltas. It does not route by note
type, change a grade, or interpret descriptor state.

An illustrative healthy summary is:

```text
AbsoluteJudgement: session_stats state=active fps=240 generation=17
outer=1200 clock_exact=1200/unavailable=0 rounded_fallback=0
journal=42 late=0 evicted=0 pending=0
scopes=342 event=42 heartbeat=300 recognition=342 score=342
queries=pressed:918/true:17 held:211/true:83 released:64/true:9
direction:34/nonzero:22 age:107/age1:12/age2plus:71
boundaries=300 batches=1200 max_batch=1 backlog_ms=0
grades=miss:31 good:2 cool:5 great:9
```

This makes the old blackout localizable in one run:

- `journal>0, scopes=0` means scheduling/clock activation failed;
- `scopes>0, recognition=0` means native dispatch failed;
- `recognition>0, pressed true=0` despite recorded rises means query/history
  projection failed; and
- recognition/score activity with no grade changes over an entire chart points
  beyond scheduling into native lifecycle/result behavior.

#### Runtime `Trace` scope records

The existing compile-time-off per-step trace is not acceptable as the only
way to inspect a failed run. When the existing logging level is explicitly set
to `Trace`, emit one compact record after every delivered scope. No additional
feature toggle or special binary is required.

Each scope record includes:

- session/scope ID and `event`, `heartbeat`, or equal-boundary-substitution;
- journal sequence when applicable, exact mapped time, native integer ms/frame,
  and delivery delay;
- held-before/after, rising, and falling masks;
- actual query call/result masks and held-age classifications observed during
  the scope;
- native recognition/score completion and four score-counter deltas; and
- boundary commitment and remaining backlog after the scope.

Trace logging is a diagnostic run, not a performance-acceptance run. Normal
timing validation uses the always-on counters so per-scope file I/O cannot
perturb the result being measured.

#### Diagnostic invariants

The following are checked, not merely printed:

- `recognition_calls == score_calls == event_scopes + heartbeat_scopes`;
- committed boundary indices never decrease or skip retained work;
- no rounded clock fallback is ever selected;
- event journal sequences are strictly increasing except for the approved
  separately counted late record; and
- no score counter decreases or changes by an arithmetically impossible delta.

Violation takes the approved fatal path with the same snapshot. Ordinary zero
query/result counts are not automatically fatal—menus, preroll, or a chart
section with no matching note can legitimately produce them—but session
acceptance below requires meaningful end-to-end activity.

### Diagnostic replay decision — Rejected

Do not add a second recorded/replayed input source. The present unpatched or
failed-hybrid 240-FPS behavior is already materially broken and is not an
acceptance oracle. The required outcome is that real input through the new path
produces coherent native judgement, not that an artificial stream reproduces a
broken baseline. Replay would add configuration, file format, source-selection,
and score-isolation code without being necessary for that decision.

The hard render-independence requirement remains. It is established from the
per-event absolute-time contract and non-accumulating boundary arithmetic, then
checked in real-input native-game runs at each supported FPS. Do not claim
bit-for-bit empirical equality of manually repeated human input; claim only
what those runs and the architecture actually prove.

### Runtime and gameplay acceptance — Approved

Acceptance answers two practical questions separately:

1. **Is judgement really using the physical event's absolute song time rather
   than the render that delivered it?**
2. **Does the complete real game behave sanely with real input, including all
   note mechanics, misses, score, effects, and long-note lifecycle?**

#### Absolute-time proof in every real-input run

For every event scope, the runtime Trace/checked invariants must show:

```text
event QPC
  -> exact audio-epoch mapping J
  -> native_ms = trunc(J in milliseconds)
  -> native_frame = floor(J / Q)
  -> event-specific query answers
  -> one native recognition call
  -> one native score call with the same native_ms
```

The outer/render update number and `target_fps` may affect only when that chain
is delivered and how ready work is batched. They must not appear in any formula
that produces `J`, `native_ms`, `native_frame`, held age, lookback, or query
answers. This is checked in code review and exposed by the Trace fields; it is
stronger and more relevant than comparing against the already-broken 240-FPS
baseline.

For an event mapped to `J = 10,005.400 ms`, for example, a 240-FPS run is sane
only if native recognition and score receive `10005`, even if the render that
delivers it occurs at `10,008` or later. Supplying `10008` is a failure even if
the note happens to receive the same grade.

#### No cumulative rounding at 144 or 165 FPS

Every authored heartbeat is generated directly from its immutable integer
index, `B_n = nQ`; no next boundary is formed by repeatedly adding a rounded
`16.67 ms`. At every summary the driver checks the committed boundary index
against the index independently derived from exact current `J` (allowing only
the explicitly retained bounded backlog during catch-up). Once caught up:

- committed index equals the exact target index;
- pending authored boundaries are zero;
- exact boundary phase error is zero in the rational/integer coordinate; and
- `rounded_fallback`, skipped-boundary, and duplicate-boundary counts are zero.

A full-song run at both 144 and 165 FPS is mandatory. The fact that neither is
a multiple of 60 must change delivery grouping only; it cannot accumulate a
fractional remainder from one render to the next.

#### First gate: prove the current blackout is gone at 240 FPS

Before broad mechanic testing, run one ordinary 240-FPS chart twice:

1. **No-input run:** heartbeat scopes, recognition calls, score calls, and
   native MISS counter deltas must advance as chart notes pass. The song must
   reach its normal result/lifecycle instead of remaining judgement-dead.
2. **Real-input run:** a physical press must appear successively as a journal
   rise, an event scope, a true pressed query, a recognition/score pair, and a
   visible native result/grade delta. At least one reasonably timed input must
   produce a non-MISS result.

If either chain fails, stop. Do not continue to a broad matrix or describe the
patch as partially successful; the counters identify the first broken stage.

#### Real-input mechanic matrix

Using actual keyboard/controller input and real charts, cover these behaviors.
Coverage may span several songs; the loader remains unaware of note type.

| Case | What must be observed |
|---|---|
| Basic tap on each side | One physical rise produces one eligible pressed edge and one sensible native result; no duplicate result from later scopes |
| Rapid trigger | A press-release pulse shorter than `Q`, and where physically achievable press-release-press within one render interval, appears as separate ordered event scopes rather than one final held state |
| Simultaneous/different controls | Same-time records retain journal sequence; OR composites accept either component without losing the other control |
| Paired controls | Both-current and one-current-plus-prior-within-`4Q` cases work; one side alone or an edge older than the inclusive lookback does not falsely satisfy the pair |
| Flick/direction head | A fresh direction rise can start once; unrelated event scopes during its first `Q` do not replay the head |
| Slide-hold | Fresh direction head, held continuation, direction changes, and release all behave; continuation does not require a new head edge |
| Hold and dual hold | Press starts, held state sustains, release ends immediately under the validated zero-safe-frame policy, and duration grade/result completes |
| Scratch/beat/turn and other routed types | Native continuation windows, direction choice, aggregation, and result publication remain sane |
| Hidden chart notes and post-descriptor free input | Hidden notes remain scored chart descriptors; free input remains native effect/key-sound behavior and is not confused with a chart type |
| Pre-held session start | A key already down creates no tap/free-input/flick/slide head; releasing and pressing after start creates a normal fresh edge |
| Render hitch | With audio/epoch continuous, retained events and boundaries catch up in order, backlog returns to zero, and no event is re-stamped to resume time |

The validation record names the real chart(s) used and maps their observed
coverage back to raw/effective note types `0..15` from E-046. A type is not
marked covered merely because a shared wrapper compiled; its actual chart
behavior/result must be seen. Visuals, sounds, displayed grades, score, and song
completion are operator observations alongside the diagnostic counters.

#### FPS/runtime matrix

The mandatory acceptance rates are `60`, `144`, `165`, and `240 FPS`. At each:

- absolute mode is explicitly enabled and reaches an active exact-clock
  session;
- one real-input chart completes with sensible visible judgement and score;
- journal/event/query/native-call/grade counters are nonzero;
- recognition and score counts equal delivered scope count;
- late records and clock-unavailable reads are reported honestly; an acceptance
  run with a late miss should be repeated rather than hidden;
- evictions, sequence errors, rounded fallback, discontinuities, fatal
  invariants, and end-of-session backlog are zero; and
- the feature-off path is separately checked once to show `sites=0` and the
  explicit no-guarantee warning at non-60 FPS.

Any additional FPS advertised as supported by absolute-time mode, currently
including `120` or `360`, must pass the same basic full-chart gate before that
rate is claimed. Renderer validation alone is not judgement validation.

#### What automated/static evidence may and may not claim

The user previously rejected invented gameplay tests. Keep that boundary:

- automated checks may cover only exact integer/rational arithmetic, immutable
  transition/history policy whose expected values follow directly from this
  approved contract, byte-signature transactions, and fatal invariants;
- no fake recognition/score callback, copied native note matcher, loader-side
  note routing model, self-referential FPS-output matrix, or source-grep test is
  gameplay evidence; and
- a successful build/test run proves loader mechanics only. Actual native-game
  and cabinet/operator runs are required before saying input or judgement is
  fixed.

#### Final evidence bundle

Do not close implementation on “hooks installed.” Preserve, for each mandatory
FPS, the relevant loader log/session summary and the operator result. Preserve
one targeted Trace run for rapid input/direction/paired behavior and one hitch
run. The handoff explicitly separates:

1. static/build proof;
2. native-process structural proof from counters/Trace; and
3. actual game/cabinet acceptance.

Only the third permits the statement that the patch is sane in play. Exact
same-input replay equality is intentionally not an acceptance requirement.

### Final readiness audit — Exact clock-provider gap resolved in design

The final clean-source audit found that the current audio interfaces do not yet
map an arbitrary retained input-event QPC timestamp to an exact source position
in the matching playback generation. That was a missing provider boundary, not
a failure of the native-cadence judgement architecture. The later approved
WASAPI-only provider, playback-origin mapping, 60-second anchor history, and
single outer-plan sections close the design gap; building those interfaces is
now explicit implementation work.

Confirmed current-source facts:

- `GameplaySongClock` accepts either one `ExactSourceFrame` observation or one
  `RoundedMilliseconds` observation of the current position. Its observation
  carries source frame, source rate, and playback generation, but no QPC time
  with which to resolve an older input event.
- `GameplayAudioCursorObservation` likewise publishes an exact current source
  frame, sample rate, playback generation, and output frame, but no QPC epoch
  relation.
- WASAPI has the necessary low-level ingredients: `IAudioClock::GetPosition`
  returns endpoint position together with `qpc_100ns`, `EndpointClockMapper`
  relates endpoint position to output frames, and `AudioCursorTimeline` relates
  output frames to source frames and playback generations. However, the public
  path exposes only `CurrentOutputFrame()`/`Project(now)`, not an explicit
  `MapEventQpc(q)` result.
- `PresentedClockPublication::Project` returns the last value when a snapshot
  cannot be read or the requested time precedes the current snapshot, and then
  monotonically clamps the answer. Those convenience semantics are unsuitable
  for judgement: the approved contract requires an explicit pending,
  unavailable, discontinuous, or history-lost result rather than silently
  substituting another time.
- `AudioCursorTimeline` retains only 32 render spans. It is useful machinery,
  but cannot be reused unchanged for retained events across a sufficiently long
  game-thread hitch; old source/output mappings can disappear while the input
  journal still owns events that need them.
- ASIO currently supplies driver `sample_position` and `system_time_ns`, while
  callback-entry QPC is used for cadence diagnostics rather than carried in
  `AsioRenderRequest` as an absolute clock relation. Its presented-clock path
  converts driver time to wrapping milliseconds and projects with
  `timeGetTime()` at 48 frames per millisecond, with last-value/monotonic
  behavior. That is precisely the rounded clock path this design forbids.
- The configured audio backends are only `directsound`, `wasapi_exclusive`, and
  `asio`. DirectSound is already known not to provide the required exact epoch;
  the audit does not yet authorize claiming that ASIO does either.

Therefore the current `GameplaySongClock`, cursor-observation, and presented
clock APIs must not simply be wired into the new driver. The approved sections
below replace that shortcut with an exact historical WASAPI provider contract
and a locked WASAPI-exclusive initial backend scope. Earlier example wording
that said to choose a "supported WASAPI/ASIO backend" was provisional
capability language, not proof that both current backend implementations
satisfy this design.

### Initial exact-clock backend scope — Approved

The first absolute-time judgement implementation supports
`audio_backend = 'wasapi_exclusive'` only. This is the backend in the current
240-FPS deployment and already contains the low-level endpoint-position,
QPC-timestamp, output-frame, and source-frame ingredients needed by the new
provider.

This decision has these precise effects:

- with `enable_absolute_time_judgement = true`, either WASAPI exclusive and its
  exact provider pass preflight together, or startup fails explicitly;
- `directsound` and `asio` are rejected by that feature preflight; neither may
  silently select the rounded clock or stock judgement;
- with `enable_absolute_time_judgement = false`, all existing audio backends
  remain usable under their existing behavior; this decision does not remove
  or disable the ASIO audio backend itself; and
- exact ASIO judgement support is deferred to a separate clock-domain design
  that proves an absolute relation between ASIO driver time and input QPC. It
  is not part of the first implementation plan.

### Exact WASAPI event-time mapping — Approved

The recommended provider uses the exact WASAPI clock samples already obtained
on the audio thread, but publishes them as immutable history instead of hiding
them behind `CurrentOutputFrame()`.

Each clock anchor contains at least:

- the raw `IAudioClock` endpoint position and its reported frequency;
- the `qpc_100ns` timestamp returned for that exact position;
- the continuous WASAPI endpoint generation and output sample rate; and
- the submitted output-frame tail at publication time.

The bound gameplay BGM voice separately supplies the approved immutable
playback-origin mapping with its voice identity, playback generation, source
and output rates/origins, and monotonically published mapped tail. The provider
combines that mapping with retained WASAPI clock anchors; it does not infer song
time from the game render or from the native millisecond cursor.

For an input event at QPC `q`:

1. Select the newest retained clock anchor at or before `q` in the same WASAPI
   endpoint generation.
2. Project from that anchor to `q` using checked signed integer/rational
   arithmetic and the authoritative clock/output rates. Projection is allowed
   only while the resulting output position is inside audio already submitted
   to the endpoint. It never projects through an unsubmitted future.
3. Validate the output position against the bound BGM playback origin and
   mapped tail, then transform it into an exact rational unwrapped source
   position in that playback generation.
4. Form `J(q) = R(q) + GameTimeOffset` once. Only then derive
   `native_ms = trunc(J in milliseconds)` and
   `native_frame = floor(J / Q)`.

No intermediate operation repeatedly adds rounded milliseconds or render-frame
durations. Fractional output/source frames remain rational; converting to a
native integer happens only at the two explicitly defined native boundaries.

Concrete example at a 48-kHz endpoint:

```text
retained WASAPI anchor: qpc time 20,000.000 ms, output position 960,000.0
physical input event:  qpc time 20,005.400 ms
exact output position: 960,000 + 5.4 ms * 48 frames/ms
                     = 960,259.2 frames
```

Suppose the bound BGM playback-origin formula maps that point to source song time
`10,005.400 ms`. A 240-FPS render may not deliver the scope until
`10,008 ms`, but recognition and score still receive native millisecond
`10005`. The same physical QPC event produces that same value at 60, 144, 165,
or 240 FPS. The `.2` output-frame fraction is not rounded once per render and
therefore cannot accumulate error.

The query result is explicit:

- **NoPlayback:** the fresh native state is armed but no matching BGM playback
  generation exists yet;
- **Pending:** the event maps beyond the submitted/mapped tail or the bound
  playback origin is not ready yet; retain it and retry after audio advances;
- **Resolved:** return the exact rational source position and generation;
- **TemporarilyUnavailable:** a stable same-generation publication could not be
  read; freeze recognition for this outer update and retry;
- **HistoryLost:** the required WASAPI anchor has been overwritten or the bound
  playback publication can no longer provide its active origin; take the
  approved active-session fatal path; and
- **Discontinuous:** endpoint/voice generation, seek, reset, or other epoch
  identity no longer matches; take the approved active-session fatal path.

There is no last-value fallback, monotonic answer cache, submitted-tail clamp,
or retimestamping. For example, if the formula produces output frame
`960,700.2` but the submitted tail is `960,600`, the answer is `Pending`; it is
never changed to `960,599`.

This avoids deliberately waiting for a second WASAPI sample after every input.
A two-sample bracketing design could avoid all forward projection, but would add
up to one audio callback period of intentional judgement/effect latency and a
more complicated readiness rule. The reported WASAPI position/QPC pair plus
the endpoint frequency already defines the required relation, so bracketing is
not recommended for the first implementation.

### Gameplay-voice binding and bounded history — Approved

#### Bind only through the game's authoritative group-2 query

Reuse the existing narrow `ScopedGameplayAudioCursorQuery` around the native
sound-manager request for gameplay sound group 2. The exact secondary buffer
whose `GetCurrentPosition` call publishes inside that scope is the only buffer
eligible to become the song clock. Do not guess from buffer size, duration,
volume, creation order, or whichever voice happens to be playing.

Extend that scoped observation with:

- a monotonically allocated buffer/voice instance ID, never a reusable raw
  pointer as identity;
- a lifetime-safe handle to that voice's exact playback mapping publication;
- playback generation, source format/rate, and active/inactive state; and
- the number of qualifying buffer publications seen in the one group-2 scope.

For a fresh native judgement state:

- zero active publications means `NoPlayback`; remain armed;
- exactly one active exact publication binds the session to
  `(native state, Tune, buffer instance, playback generation, WASAPI endpoint
  generation, GameTimeOffset)`;
- more than one qualifying publication is ambiguous and must fail explicitly
  rather than choosing the first or last; and
- once active, a different buffer, playback generation, endpoint generation,
  seek, or immutable offset is the already-approved discontinuity fatal path.

An absent stable observation for one outer update remains the approved
temporary-unavailability case; it does not silently rebind. A normal new song
first ends the old native session and then binds a fresh tuple. Feature-off
operation does not create or enforce this binding.

This also makes the startup input rule concrete. Transitions that occurred
before the fresh state and exact playback tuple were both established are not
replayed into the song. The held mask is sampled as the no-edge, age-at-least-2
baseline already approved; the first later physical transition is the first
eligible event scope. If the native state is no longer fresh when an exact
voice first appears, activation is rejected as a mid-song attach.

#### Publish one exact mapping per playback generation

Do not make the current 32-entry `AudioCursorTimeline` ring the judgement
authority and do not enlarge every ordinary sound-effect voice's ring merely
for this patch. For the bound voice, publish a dedicated immutable playback
origin:

```text
buffer instance and playback generation
global output-frame origin O0
unwrapped source-frame origin S0
output rate Fo and source rate Fs
monotonically published mapped-output tail
optional exact end position/state
```

Within that unchanged generation, the mapping is direct:

```text
S(O) = S0 + (O - O0) * Fs / Fo
```

It remains rational until the native conversion boundary. This is the same
cumulative rate relation already used by the mixer; it is clearer than saving
one judgement record for every render span and cannot accumulate per-callback
rounding. Looping may wrap the ordinary DirectSound cursor, but the judgement
coordinate stays unwrapped. Play/seek/generation changes create a new identity
and are never spliced into the active session.

The audio thread publishes the origin once and monotonically extends the mapped
tail as source audio is rendered. A query before the origin or beyond the tail
is not clamped: it is `Pending`, `HistoryLost`, or `Discontinuous` according to
the explicit state above. The existing short cursor timeline may remain for
ordinary DirectSound behavior but is not consulted as judgement truth.

#### Fixed 60-second WASAPI anchor history

Clock anchors use one preallocated single-writer/single-reader sequence ring
sized at audio startup from the actual endpoint period to retain at least 60
seconds. At the deployed 10-ms period this is about 6,000 anchors; even at a
1-ms period it is about 60,000. This is a small, fixed allocation made before
the audio callback starts.

The audio thread never locks, allocates, or waits for the game thread. Each
anchor has a monotonically increasing sequence; the game thread detects an
overwritten required sequence instead of needing a producer/consumer watermark
or cutoff handshake. The separate 1,024-transition input journal retains its
existing overflow rule. Whichever required history is lost first produces the
approved structured fatal result.

Concrete hitch behavior:

- after a 4-second render-thread stall while audio continues, the retained
  input event selects the clock anchor immediately before its own QPC, maps
  through the still-live playback origin, and catches up with its original
  timestamp;
- a heartbeat needs only the current exact clock and its integer boundary
  index, so it does not consume historical anchors; and
- if the game thread is stalled for more than the guaranteed 60 seconds and an
  unconsumed event's needed anchor has been overwritten, the session reports
  `HistoryLost` and terminates. It does not call the event “now,” clamp it to
  the oldest anchor, or silently miss it.

### Final consistency finding: outer clock and Tune ownership — Approved

The clean source already has `HookGameplaySongClock` at VA `0x664DB2` (RVA
`0x264DB2`). Once per outer gameplay update it calls the native sound-group-2
cursor getter inside `ScopedGameplayAudioCursorQuery`, reads `Tune+0x10` and
`GameTimeOffset`, computes a bounded step, and writes `Tune+0x14`. The new
judgement scheduler seam is later at VA `0x640239` (RVA `0x240239`).

Leaving those as two independent clock decisions would reopen the problem this
design is meant to remove. The first hook could choose one current cursor or
even its existing rounded fallback while the scheduler separately chooses a
slightly later exact time. `Tune+0x14`, the boundary horizon, and native
recognition would then disagree inside one outer update.

The recommended resolution is one immutable outer-update advance plan:

1. When absolute-time judgement is enabled, the existing `0x664DB2` hook is the
   sole outer-update clock coordinator. It performs the approved group-2 voice
   observation/binding and asks the WASAPI provider once for exact ready `J`.
2. From the session's committed boundary index and that exact `J`, it computes
   the approved target, at-most-three-boundary delivery horizon, and boundary
   count. It publishes one immutable plan tagged with the native Tune/state
   identity, playback/endpoint generations, outer-update sequence, and
   `GameTimeOffset`.
3. It writes `Tune+0x14` from that plan's authored-boundary count only. Event
   scopes never increase this value.
4. At `0x640239`, the judgement scheduler validates and consumes that exact
   plan. It merges all retained events and authored boundaries through the
   plan's horizon, calls the native recognition/score pair for each scope, and
   runs the original outer tail once. It does not query a second “now” clock or
   independently recompute the horizon.
5. Records arriving after the plan's ready cutoff remain retained for the next
   update. Historical event-QPC mapping still uses the approved anchor history;
   sharing the outer plan does not retimestamp events to the plan time.
6. `NoPlayback`, `Pending`, or temporary same-epoch unavailability produces a
   zero-boundary/no-scope plan and preserves pending input. A discontinuity or
   invalid identity follows the approved fatal path.

The current rounded group-cursor fallback is forbidden in absolute mode. It may
remain part of the feature-off/shared-clock compatibility path, but it can
never create an absolute-mode plan. The existing pure catch-up arithmetic may
be reused after removing its rounded/current-observation ownership, or replaced
with clearer plan-specific code; the design requires the one-plan result, not
reuse of the current class.

This changes the surface accounting from “six sites exist in isolation” to:

- **six absolute-judgement interception sites:** the scheduler seam plus five
  scoped query methods; and
- **one existing shared-clock integration dependency:** `0x664DB2`, already
  installed by `FrameratePatch`, now producing the plan consumed by the
  scheduler. It is not an additional new hook, but absolute-mode preflight must
  verify that it is installed and connected to the exact provider.

The failed implementation's separate `0x63FA0C` exact-now frame-store hook is
not a judgement authority and is not part of the six-site transaction. If that
site is retained to preserve independently audited high-FPS visual clock
smoothness, it may consume the same exact outer time as a one-way visual
consumer. It must not write Tune progression, change the delivery horizon,
answer input queries, or affect recognition/score results. The old
anchor-relative `ClockFrameNow()` implementation is not reused.

### Final scope tightening: preserve high-FPS visual clock separately — Proposal awaiting approval

The old failed judgement patch owns `0x63FA0C`, but the earlier audited purpose
of that site is visual clock smoothness after Tune progression is restored to
authored 60-Hz boundaries. Simply deleting the failed module without relocating
that one-way visual behavior risks making a correctly judged 240-FPS game show
clock-derived motion in visible 16.67-ms steps.

Recommended resolution:

- retain `0x63FA0C` only when absolute-time mode is enabled above 60 FPS;
- rehome it under framerate/visual-clock ownership, not the judgement driver;
- compute its existing virtual visual frame from the latest exact WASAPI time
  with direct rational arithmetic, preserving the independently audited visual
  behavior and never using the old render-anchor correction;
- allow it to read exact time only. It cannot write `Tune+0x10/+0x14`, publish
  an outer plan, open an input scope, change a judgement horizon, or participate
  in recognition/score;
- when no exact visual time exists before the gameplay session is active, leave
  the native visual value for that update; this has no judgement fallback
  meaning; and
- validate/report it separately as `visual_sites=1` at high FPS while the
  judgement transaction remains exactly `judgement_sites=6`. Because absolute
  mode deliberately restores authored Tune cadence, failure to install this
  required high-FPS visual companion is a startup preflight failure rather than
  a silent visual regression. At 60 FPS it is unnecessary and
  `visual_sites=0`.

This preserves the purpose of higher render rates without giving a visual hook
any result authority. Dropping it would make the implementation smaller but
would knowingly reintroduce a visible behavior the prior high-FPS design had
already compensated for.

## Historical tail status — Superseded by the 2026-08-20 specification

The visual/shared-clock proposal immediately above was not approved. The scope
erratum at the top of this record and the consolidated specification resolve
the question: the new judgement feature does not modify or depend on the
existing high-FPS visual/shared-`Tune` hooks. The remaining approved decisions
are carried into the consolidated specification.

Apart from the explicit visual-scope choice immediately above, the final
consistency review found no remaining high-level semantic decision that must be
guessed during planning:

- absolute event time, authored heartbeat cadence, equal-time ordering,
  catch-up, held age, paired lookback, direction freshness, release grace, and
  native consumer ownership are explicit;
- feature activation, WASAPI-only capability, fresh-session binding,
  discontinuity/loss behavior, diagnostics, and runtime acceptance are
  explicit;
- the single outer-update plan makes exact ready time, `Tune+0x14`, and the
  scheduler horizon one decision rather than independently sampled clocks;
- the six judgement interception sites and existing `0x664DB2` integration
  dependency are named, while the old capture-ownership, duplicate-held guard,
  rounded anchor correction, and `0x63FA0C` judgement ownership are rejected;
- the completed E-042 through E-046 audit supplies the required native behavior
  contracts. This final source review found no new binary question requiring
  another IDA investigation; and
- the current clean source contains the necessary WASAPI position/QPC,
  endpoint/output mapping, mixer playback-origin, group-2 query scope, input
  journal, and hook infrastructure. It does not already contain the approved
  provider/session/query driver, so implementation is a deliberate replacement
  rather than wiring together the failed hybrid unchanged.

The design is ready for a separate Superpowers implementation plan. That plan
must preserve task boundaries between exact audio publication, gameplay-voice
binding/session state, pure retained-history query policy, native scheduling
and hook transaction, observability, automated loader-level verification, and
real-game acceptance. Static/build success must remain distinct from the final
native-game/cabinet result.

# Absolute-Time Judgement Correction Design

**Date:** 2026-08-22

**Status:** Approved design assembled from the completed audit and subsequent
compatibility decisions; implementation not started

**Initial backend:** WASAPI exclusive only

**Configuration:** `[experimental] enable_absolute_time_judgement = false`

## 1. Authority

This document is the implementation authority for correcting the current
absolute-time judgement patch. It is derived from:

- [the authoritative full audit](../audits/2026-08-21-absolute-judgement-authoritative-full-audit.md);
- [the original consolidated specification](2026-08-20-absolute-time-judgement-spec.md);
- [the original implementation plan](../plans/2026-08-20-absolute-time-judgement.md);
- the completed native evidence E-042 through E-046 under
  `H:\gc\artifacts\GCLoader\.planning\debug\high-fps-timing-domains\evidence`;
  and
- the supported binary and actual game behavior as final acceptance authority.

The old specification and plan remain historical evidence. Their product goal,
native-ownership boundary, and approved policies are preserved below. Their
construction/cleanup lifecycle, per-Play/Seek clock model, `OutsidePlayback`
input classification, current-frame-only release restriction, broad fatal
predicates, and exception boundary are superseded.

No failed design, plan, log, or native evidence is deleted by this correction.

## 2. Preserved original goal

For the same chart, settings, and successfully observed physical input event
times, native judgement results must be independent of render rate at 60, 144,
165, and 240 FPS.

Every successfully observed transition not covered by one of the explicit,
counted loss policies below is assigned one exact absolute song coordinate and
is presented to the original native recognition and score code at that
coordinate. A render update controls only when ready work reaches the game
thread. It must not change:

- the transition timestamp or order;
- held, pressed, released, direction, or held-age facts visible to that scope;
- the native millisecond or authored-frame arguments;
- native candidate ordering, note routing, windows, grade, score, sound,
  effects, long-note behavior, or free-input policy; or
- the eventual judgement result, except for the explicitly accepted loss
  policies below.

Absolute song time is the requirement. The loader does not run the game at
1000 Hz and does not redefine the game's global frame unit. It feeds exact
event scopes and exact `1/60` heartbeat scopes into the original native
judgement transaction.

### 2.1 Preserved requirements matrix

| Original requirement | Corrected design |
|---|---|
| Opt-in and off by default | Preserved unchanged |
| WASAPI exclusive first | Preserved unchanged |
| Exactly 1000-Hz gameplay input publication | Preserved unchanged |
| Render-independent results at 60/144/165/240 | Preserved unchanged |
| Exact rational arithmetic; no accumulated `16.67 ms` | Preserved unchanged |
| Native owns all chart and judgement policy | Preserved unchanged |
| One immutable event view across both booster components and original tail | Preserved unchanged |
| Exact `n/60` heartbeats, at most three heartbeat catch-ups per outer call | Preserved unchanged |
| One event-bearing scope per outer call | Preserved unchanged |
| Protect newest 32 simultaneously ready events | Preserved unchanged |
| Rare stage-entry handoff/accepted-late loss and explicit overload/cleanup loss only | Preserved, counted explicitly, and made time-causal |
| Zero `HoldSafeFrame` and `SlideHoldSafeFrame` only | Preserved unchanged |
| One fixed CBooster with left/right components | Preserved and stated explicitly |
| No replay oracle or loader gameplay simulation | Preserved unchanged |
| No timeout and no stock/native fallback once enabled | Preserved unchanged |
| Static/build evidence is not gameplay acceptance | Preserved unchanged |

The original goal's three exception families remain finite. The rare
no-watermark/late-publication family has two separately visible manifestations:
a transition observed at or after `stage_entry_qpc` and already queued when the
stage-entry cutoff lock is acquired becomes baseline-only and increments
`stage_entry_handoff_drops`; a record that becomes visible only after judgement
has committed past its exact coordinate follows the counted accepted-late
state-only rule. The other two families are the counted overload state-only
rule for the oldest excess event when more than 32 events are simultaneously
ready, and the cleanup-drop count for an undelivered event remaining when
semantic stage exit destroys the stage-owned judgement state.

None is retimestamped or passed to stock judgement. Unknown transport/history
loss, clock discontinuity, or mixed native/absolute history is not another
exception. A runtime acceptance run requires zero stage-entry handoff,
overload, and cleanup drops; repeat the run if any of those counters is nonzero.

## 3. Non-goals and hard ownership boundaries

The correction changes only input capture-to-judgement timing and its required
query materialization. It does not modify:

- the independent high-FPS framerate hooks, including `0x664DB2`;
- the shared Tune clock or the game's render/update frame unit;
- the already-working audio backend or native audio-resync policy;
- chart candidate construction, normalization, routing, gates, grade windows,
  score, effects, sounds, aggregation, or long-note policy;
- the Switch patch's alias/diagonal policy;
- the native note table or descriptor lifecycle; or
- DirectSound or ASIO support for absolute judgement.

The loader must not add note routing, replay input, sound/effect calls, a 1000-Hz
recognition loop, native CBooster ring reconstruction, render-time timestamping,
or a mixed native/absolute judgement mode.

## 4. Chosen correction strategy

### 4.1 Selected: reconstruct only the invalid domains

Retain the proven components:

- `GameplayTransitionJournal` and its 1000-Hz QPC records;
- checked rational arithmetic;
- exact WASAPI endpoint output projection;
- immutable thread-local judgement scopes;
- native recognition -> score -> original-tail execution;
- exact event/heartbeat scheduling topology; and
- the approved accepted-late, newest-32, held-age, and paired-edge policies.

Replace the invalid components:

- construction/cleanup stage lifecycle;
- per-Play/Seek judgement mapping;
- playback-gap/`OutsidePlayback` edge deletion;
- history consumption and pruning rules that separate delivery from causality;
- non-native query rejections;
- foreign-thread exclusion;
- diagnostic-to-fatal promotion;
- broad fatal reasons and invisible active-stage termination; and
- C++ exception handling in the native hook.

This is clearer and lower risk than preserving the current resolver's state
machine. It also avoids discarding the independently useful input, arithmetic,
scope, and native-call work.

### 4.2 Rejected alternatives

**Assertion-only repair** is rejected. Removing the deployed lower-bound check
prevents one crash but cannot restore the two edges already deleted before the
assertion, so it cannot restore early free-tap/ad-lib sound.

**Full module rewrite** is not selected initially. It would recreate already
proven transport, arithmetic, query-scope, and native-call behavior. Individual
files may still be rewritten when replacement is simpler than untangling their
invalid logic.

## 5. Semantic stage lifecycle

The only gameplay lifecycle is:

`NON_STAGE -> STAGE_ENTRY -> ACTIVE_STAGE -> STAGE_EXIT -> NON_STAGE`

- Object construction at `0x6629A0` creates native storage but remains
  `NON_STAGE`.
- States 6 through 15 are loading/preparation/countdown and remain
  `NON_STAGE`.
- The shared states-16/17 transition is `STAGE_ENTRY`. The loader begins its
  semantic epoch at the start of that committed transition, before native
  frame-zero input initialization and before the BGM `Play` call.
- Native state 18 is `ACTIVE_STAGE`. Only it runs input progression,
  recognition, and score.
- The state-18 branch that selects state 19 is `STAGE_EXIT`. The loader closes
  the semantic epoch on that branch; that iteration performs no judgement.
- Cleanup at `0x662080` later destroys native storage. It must find the semantic
  stage already closed and must not perform stage accounting.

Every song, retry, or other genuine re-entry through the transition creates a
fresh monotonically increasing loader stage generation. Reused Tune, booster,
audio, or native object addresses do not merge generations.

At `STAGE_ENTRY`, one synchronized journal cutoff captures:

- input transport epoch and QPC frequency;
- first eligible sequence;
- current ten-control held baseline; and
- current eviction/fault count.

The entry hook also captures one `QueryPerformanceCounter` value immediately
before that cutoff and before native frame-zero initialization. This
`stage_entry_qpc` is the audio watermark used to reject retained epochs from an
earlier song. QPC failure follows the success-only timing fatal contract.

A journal record published in the tiny interval after `stage_entry_qpc` but
before the cutoff mutex is acquired is folded into the cutoff held baseline and
exposes no edge. The cutoff counts these records as
`stage_entry_handoff_drops`. This is the previously accepted rare no-watermark
handoff loss: it is never retimestamped or hidden, and an acceptance run with a
nonzero count must be repeated.

Records before that cutoff are non-stage input and cannot create stage edges.
Every record at or after the cutoff belongs to the stage, including input before
the first BGM origin and input after natural audio drain but before stage exit.

The exact new entry and exit patch RVAs and guarded bytes must be recorded from
the already-audited supported state-machine path before source mutation. This
is an implementation-site proof, not another lifecycle design decision.

## 6. One continuous absolute stage clock

The gameplay timeline is monotonic. Native audio resync seeks audio to Tune
time; it does not rewind game time. Judgement therefore binds once per semantic
stage.

Let:

- `O0` be the endpoint output-frame origin of the first selected group-2 BGM
  `Play` belonging to this semantic stage;
- `S0` be that epoch's source-frame origin;
- `Fs` be its source sample rate;
- `Fo` be the endpoint output sample rate;
- `G` be `GameTimeOffset` captured at stage entry; and
- `O(q)` be the exact endpoint output coordinate projected from input QPC `q`.

Then:

`J(q) = S0/Fs + G/1000 + (O(q) - O0)/Fo`

The first qualifying BGM origin must be at or after the exact endpoint output
coordinate projected from `stage_entry_qpc`. An old retained buffer epoch from
an earlier song cannot bind the new stage merely because it is the first
element in a retained vector. The chosen epoch must be `Play`, match the
selected group-2 buffer/endpoint history, and precede or equal the selected
generation's current exact mapping; an immediate native resync `Seek` does not
hide that stage's preceding `Play`.

Before the anchor exists, the scheduler drains and retains journal records in
sequence but issues no native judgement. `Pending` and
`TemporarilyUnavailable` wait without a timeout. Once the origin is available,
all retained stage records resolve through the same formula. `O(q) < O0` is
valid and yields a signed pre-origin `J`; it is not an input-loss category.

After binding:

- later `Play`, `Seek`, stop, closed epoch, and natural drain are audio
  diagnostics only;
- no later source epoch can redefine `J`, shrink the ready frontier, or delete
  input;
- endpoint output/QPC continuity continues the same stage clock through the
  natural tail until semantic stage exit;
- a live `GameTimeOffset` change affects native audio behavior but does not
  mutate this stage's bound `G`; the next stage binds the new value; and
- endpoint generation/clock continuity loss remains explicitly unsupported
  because no exact bridge is selected.

All calculations use checked integer/rational arithmetic. For a scope at `J`:

- `native_ms = trunc_toward_zero(J * 1000)`;
- `native_frame = floor(J * 60)`; and
- heartbeat `n` is constructed directly as `n/60`.

No calculation repeatedly adds a rounded quantum or includes target FPS. This
prevents accumulated phase error at 144 and 165 FPS.

## 7. Ordered history and scheduler

### 7.1 One chronological record state machine

Every post-cutoff journal record retains its exact sequence, QPC, mapped `J`,
held-before/after masks, and rising/falling masks. Its disposition is one of:

- `EventEligible`;
- `AcceptedLateStateOnly`; or
- `OverloadStateOnly`.

There is no `OutsidePlayback` or playback-gap disposition.

One chronological delivery cursor owns all three dispositions. Consuming an
event scope or a state-only record advances that cursor exactly once. Pruning
may move the retained storage base, but a counting request that begins before
that base simply begins at the retained base; the old numeric lower-bound check
is not history-loss proof.

`StateAt(query_time, prefix)` applies a record only when both its sequence is in
the immutable prefix and `record.J <= query_time`, regardless of disposition.
`PruneBefore` folds a state-only record into the causal base only after its
exact time is committed. A future release can never affect an earlier
held/direction query merely because its sequence has been seen.

Accepted-late records are never retimestamped or replayed. They update future
held state without edge, freshness, paired-companion, sound, or judgement.
Overload records remain in chronological order until consumed state-only at
their own turn. Unknown journal eviction or sequence loss is not either policy.

### 7.2 Scope scheduling

The merged ready stream contains exact input events and exact `n/60`
heartbeats. Ordering is by `(J, kind, sequence)` with the already-selected
equal-boundary substitution rule.

Per rendered outer call:

- run exactly one event scope and no heartbeat, or
- run one to three heartbeat scopes and no event scope;
- defer an event encountered after heartbeat catch-up to the next outer call;
- defer all later work after an event scope; and
- run native recognition once and score once for every completed scope, then
  reach the original tail exactly once for the selected outer-call batch.

An event batch contains exactly one event, so its transient native publication
survives to that one original tail. A heartbeat-only batch may contain up to
three heartbeat scopes because it carries no physical event publication that
must survive an intervening recognition call.

More than 32 simultaneously ready undelivered events marks the oldest excess
events `OverloadStateOnly`; the newest 32 remain eligible. This is protection
against extraordinary stalls or device bounce, not a normal human-input path.

The scheduler never writes Tune frame fields and never calls stock judgement
inside an active absolute stage. Reaching the owned loop without an open
semantic stage is a named lifecycle invariant, not permission to return to the
stock loop.

## 8. Native query compatibility

There is one fixed CBooster receiver with left and right components. Within a
local immutable scope, receiver, thread, stage generation, coordinate, event
masks, and history prefix are stable.

Outside a local scope, query hooks trampoline to native. If another thread owns
a different local scope, a thread with no local scope still trampolines; global
ownership does not make the unrelated query fatal.

### 8.1 Control IDs

- Pressed, held, and released IDs outside `0..19` return false.
- Held-age IDs `>= 20` return zero.
- IDs `0..9`, composites `10..14`, and paired controls `15..19` retain native
  ordinary/OR/paired algebra.

### 8.2 Direction selector

The current argument named `booster` is a selector:

- `0`: left controls `0..3`;
- `1`: right controls `5..8`; and
- `2`: combined controls `10..13` with native priority/cancellation.

Another selector writes `x=0` and `y=0` and returns the native x86 ABI result.
It is not a missing-device error. Null output pointers remain a native call
contract violation. The one fixed booster object being absent is explicitly
unsupported.

### 8.3 Relative frame queries

Pressed remains current-frame-only inside the proven scoped caller graph.
Held and direction retain exact relative translation:

`T = scope_J + (requested_frame - native_frame)/60`.

Released supports non-current frames without recreating the CBooster ring:

- a current-frame request observes only the immutable current event, preserving
  event isolation even when several events share one authored interval;
- a different requested frame computes the same exact `T` and searches
  retained resolved events in `T - 1/60 < event.J <= T`, bounded by the scope's
  immutable history prefix;
- ordinary, composite, and paired falling-edge algebra is unchanged;
- a paired constituent in that requested window may use the other constituent
  from the selected inclusive prior `4/60` exact history; and
- an unretained or future window returns false rather than terminating.

### 8.4 Held age and paired history

Preserve the approved threshold-equivalent policy rather than claiming exact
native counter equality:

- a genuine rise reports age 1;
- later exact scopes report `max(2, 1 + floor((t-rise)*60))`;
- a pre-held or state-only baseline reports stale age 5; and
- paired edges accept the other constituent within inclusive prior `4/60`.

This policy deliberately prevents an unrelated sub-frame event from replaying
a direction head. It requires real-game acceptance because it is not bitwise
native frame-counter emulation.

## 9. Failure and diagnostic policy

Every terminating predicate belongs to exactly one class:

1. `EXPLICITLY_UNSUPPORTED` capability;
2. finite `RESOURCE_LIMIT`; or
3. `PROVEN_INTERNAL_INVARIANT`.

The exact predicate ID, expression, classification, stage generation, expected
and actual operands, relevant sequence/time/scope values, and secondary broad
category must be logged and flushed before termination.

Explicit unsupported states include:

- nonzero `HoldSafeFrame` or `SlideHoldSafeFrame`;
- missing fixed input manager/CBooster;
- unavailable/inactive 1000-Hz input transport at stage entry;
- no exact WASAPI provider/first qualifying BGM origin by semantic stage exit;
- input transport epoch/worker continuity loss during a stage; and
- endpoint generation/clock continuity loss during a stage.

Pending clock data is not failure and has no timeout. Resource limits include
journal/history eviction, fixed-capacity exhaustion, sequence/generation
exhaustion, and core exact-rational representability failure.

Diagnostics never terminate gameplay. Counter overflow saturates or invalidates
the counter. Score/transient-publication reads, playback comparisons, delivery
delay, and final accounting can become unavailable and log their own status.
Recognition-without-score or an incompletely committed native transaction
remains a true internal invariant because native state is partially mutated.

The active fatal path must show a visible prompt with no timeout after logging
and flushing. It must not silently call only `TerminateProcess`. Startup
preflight continues to use the existing visible startup-fatal path.

No C++ `try`/`catch` is used in this patch. Hook functions remain `noexcept`; an
actual standard-library throw follows normal C++ termination and the existing
crash-dump mechanism. SEH guards for native-memory ABI probes are separate.

RAII dynamic allocation is allowed when ownership is clear and leak-free. If
the one-anchor redesign makes the current playback-history vectors unnecessary,
delete them instead of preserving unused complexity. Do not replace useful
RAII storage with fixed buffers solely to avoid allocation.

New formatting uses C++23 `std::format`/`std::format_to`, not string streams.

## 10. File responsibility boundaries

- `AbsoluteJudgementPatch.{h,cpp}`: guarded native sites, semantic entry/exit,
  query trampolines, owned-loop dispatch, and exception-free hook boundary.
- `NativeJudgementAbi.h`: supported-binary addresses, signatures, x86 ABI, and
  native identity reads only.
- `JudgementStage.{h,cpp}`: one semantic state-18 epoch and its immutable
  capability/identity binding.
- `JudgementClockResolver.{h,cpp}`: one stage anchor and exact QPC -> endpoint
  output -> continuous `J` projection; later playback observations diagnostic.
- `JudgementHistory.{h,cpp}`: ordered causal records, state-only dispositions,
  time-causal state, edge/held-age/relative-release queries, and pruning.
- `JudgementScope.{h,cpp}`: local immutable scope ownership and native-compatible
  query results.
- `JudgementScheduler.{h,cpp}`: transport drain, anchor wait, ordered ready
  stream, overload, heartbeat/event batching, and commit cursor.
- `AbsoluteJudgementRuntime.{h,cpp}`: native identity capture and exactly one
  recognition -> score -> original-tail transaction per scope.
- `AbsoluteJudgementDiagnostics.{h,cpp}`: truthful nonfatal observation plus
  predicate-level visible fatal reporting.

Input, audio, config, and loader initialization files change only if an exact
interface requirement from these boundaries demands it. The independent
framerate implementation remains source-unchanged.

## 11. Proof and acceptance

No automated gameplay test, emulated expected-value model, replay oracle,
CTest invocation, or TDD task is authorized. A future test requires an
independent formally strict oracle under `AGENTS.md`.

Proof remains separated:

1. **Static proof:** guarded x86 sites and ABIs, exact lifecycle path, one-anchor
   algebra, complete fatal-predicate inventory, no target-FPS term, no fallback,
   and no exception handling.
2. **Build proof:** complete `msvc32-debug` and `msvc32-release` preset builds,
   PE32/x86 and export inspection. This proves compilation only.
3. **Runtime structural proof:** truthful logs show one semantic entry,
   activation, ordered scopes, recognition/score/tail, and one semantic exit for
   every song/retry.
4. **Game/operator acceptance:** visible judgement, score, effects, sounds,
   hidden/ad-lib behavior, free taps, long notes, lifecycle, and song completion.

Runtime acceptance order is:

1. 240 FPS, one song with no input, normal completion, no false fatal;
2. 240 FPS, real input including early free taps and hidden/ad-lib sound;
3. multiple consecutive songs/retries without restart, each with a fresh stage
   generation and no leaked history;
4. targeted taps, rapid trigger, simultaneous records, composite/paired,
   direction/flick, hold/slide/dual-hold, hidden notes, free input, and hitch
   catch-up; and
5. complete real-input charts at 60, 144, 165, and 240 FPS.

At 144 and 165 FPS, exact heartbeat phase error, skipped/duplicate boundaries,
and end backlog must remain zero. A render rate may change delivery grouping,
never timestamps or committed order.

The feature-off gate verifies zero absolute hook sites, unrestricted audio
backend selection, and the explicit stock/non-60 no-guarantee warning.

Only actual supported-game behavior permits the statement that judgement is
sane. Build success and log consistency do not.

## 12. Implementation gate

Implementation may begin only from this document, the authoritative audit, and
the new correction plan. Older absolute-judgement specs/plans are historical
negative evidence and must not be copied as implementation authority.

Before the first source edit, the executor must record the exact supported
binary RVAs, guarded bytes, register/stack contract, and ordering for the
semantic stage-entry and stage-exit hook sites. That evidence may refine hook
placement but may not redefine the lifecycle above.

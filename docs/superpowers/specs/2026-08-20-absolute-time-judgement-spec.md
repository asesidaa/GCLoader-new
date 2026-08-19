# Absolute-Time Judgement Specification

**Date:** 2026-08-20

**Status:** Approved design contract; ready for implementation planning

**Initial backend:** WASAPI exclusive only
**Configuration:** `[experimental] enable_absolute_time_judgement = false`

## 1. Authority and purpose

This is the consolidated, authoritative specification for the clean
absolute-time input/judgement implementation. It preserves the approved
post-failure discussion decisions while applying the final ownership erratum:
the existing high-FPS framerate, shared `Tune` clock, and visual hooks remain
unchanged and are not part of this feature.

The implementation starts from the post-ASIO code baseline. Removed input and
judgement code is not an implementation foundation. The failed designs and
their retained Git history are negative evidence that must be consulted to
avoid repeating their errors; they do not override this specification.

Native facts come from the completed audit at
`H:\gc\artifacts\GCLoader\.planning\debug\high-fps-timing-domains\evidence`,
especially E-042 through E-046. That audit is complete and must not be repeated
without a concrete, recorded question not answered there. Current source is
authoritative for integration shape. The supported game binary/IDB and actual
game behavior are the final ground truth.

The approved discussion record remains useful rationale and examples, but this
document supersedes it wherever the two differ:

- [post-failure discussion record](2026-08-19-absolute-time-native-cadence-judgement-design.md)
- [failed-attempt index](../failed/2026-08-high-fps-input-judgement/README.md)
- [last runtime failure diagnosis](2026-08-19-absolute-time-judgement-driver-runtime-failure-diagnosis.md)

There are no unresolved high-level design questions at publication time.

## 2. Required outcome

For the same chart, timing settings, and successfully observed physical input
event times, judgement results must be independent of render framerate at 60,
144, 165, and 240 FPS. A render hitch may delay when work is delivered to the
game thread, but it must not change:

- an event's song timestamp;
- event order;
- the input facts visible for that event;
- the native recognition and score time arguments; or
- the eventual judgement result.

Absolute song time is the requirement. A fixed update is only one possible
mechanism. The selected mechanism is an ordered sequence of exact-time input
event scopes plus native-cadence heartbeat scopes. It does not run the entire
input or gameplay pipeline at 1000 Hz and does not redefine the game's global
frame unit.

The guarantee excludes only an explicitly accepted transition that the input
transport does not successfully publish before judgement commits past its
time. Unknown history loss, corruption, clock discontinuity, or mixed
judgement histories are fatal rather than additional exceptions.

## 3. Non-goals and ownership boundaries

The loader supplies exact time and scoped input facts. Native code continues to
own gameplay policy.

The implementation must leave native ownership of all of the following:

- chart candidate construction and fixed candidate order;
- raw note normalization (`B -> A`, `C/E -> 9`, `D -> 4`);
- mode-dependent effective-type conversion and effective type `0` suppression;
- note routing for actual raw types `0..15`;
- booster-component order, descriptor lifecycle, handler success rules, and
  following-row eligibility;
- late gates, grade windows, grade selection, score changes, effects, sounds,
  aggregation, long-note state, and post-descriptor free input; and
- the Switch patch's alias selection and diagonal-match policy.

The feature must not:

- create a loader note-type routing table;
- consume an edge per descriptor or per booster component;
- rebuild the native CBooster ring or write timestamped samples into it;
- add a uniform 1000-Hz gameplay/recognition loop;
- add a replay input source or replay acceptance framework;
- change `GameplaySongClock::Create(target_fps, 1)` or the existing
  `HookGameplaySongClock` behavior;
- install or use `0x63FA0C` for this feature;
- write `Tune+0x14`, alter `Tune+0x10`, or otherwise reconfigure the existing
  target-FPS/shared-clock domain; or
- use render frame, render midpoint, a clamped current cursor, or an arbitrary
  current-frame/current-anchor correction to timestamp input.

The existing high-FPS hooks may independently observe audio and advance visual
or `Tune` state exactly as they do at the clean baseline. The judgement
scheduler obtains its own read-only exact observation. The two observations
may occur at slightly different instants; this can change which outer update
delivers a ready scope, never the scope's timestamp or result. Avoiding a shared
mutable outer plan is an intentional feature-ownership boundary.

Clarity and a small semantic/proof surface matter more than minimizing lines,
files, or hook count. Reuse native or current loader logic only when its
contract fits without unwanted side effects. Otherwise implement the smallest
audited primitive beneath it in a focused module.

## 4. Architecture

The feature consists of six responsibilities with one-way data flow:

```text
keyboard Raw Input / 1000-Hz XInput worker
              |
              v
gameplay-only transition journal (QPC, sequence, before/after/edges)
              |
              v
WASAPI endpoint anchor history + bound BGM playback mapping
              |
              v
exact event song time + retained causal input history
              |
              v
private event/heartbeat scheduler and immutable query scope
              |
              v
native recognition 0x5D68E0 -> native score 0x5CF930
```

The current worker and FastIO aggregate publication remain the physical input
transport. The clean baseline has no timestamped transition journal, so the
journal is new implementation work. It is a judgement-side history feed, not a
replacement FastIO implementation or a second device-polling system.

The judgement-facing native transaction has exactly six interception sites:

1. scheduler seam VA `0x640239` / RVA `0x240239`; and
2. five lower CBooster query methods:
   - pressed VA `0x62DFB0` / RVA `0x22DFB0`;
   - held VA `0x62DF50` / RVA `0x22DF50`;
   - released VA `0x62DD30` / RVA `0x22DD30`;
   - direction VA `0x62E480` / RVA `0x22E480`; and
   - held age VA `0x62DAA0` / RVA `0x22DAA0`.

All six sites install as one guarded, rollback-capable transaction. The
existing Switch hooks remain separate policy hooks and reach these lower
queries through their original wrappers.

## 5. Exact time coordinate

Define the authored policy quantum exactly as:

`Q = 1/60 second`.

Let `R(q)` be the exact unwrapped source position of the bound gameplay BGM at
input QPC tick `q`. Let `G` be the session's immutable native
`GameTimeOffset`. The judgement coordinate is:

`J(q) = R(q) + G`.

Native authored frame zero is `J = 0`; there is no loader-created session-time
origin. The values passed to both native calls for a scope are derived directly
from the same exact coordinate:

- `native_ms = trunc_toward_zero(J * 1000 seconds^-1)`;
- `native_frame = floor(J / Q)`; and
- heartbeat `n` occurs at `B_n = nQ` for signed integer `n`.

Native per-player/group bases, `JudgTimeOffset`, windows, and other additive
terms remain native and are not added by the loader a second time.

All time values, cross-rate conversions, products, comparisons, and floors use
checked integer/rational arithmetic. Input QPC ticks and WASAPI's `qpc_100ns`
share the QPC epoch but have different units; their conversion must be exact
and checked from the actual QPC frequency. No calculation may repeatedly add a
rounded `16.67 ms`, carry a render-frame fractional remainder, or use
`1/target_fps`. This is what prevents accumulated phase error at 144 and 165
FPS.

Exact-time/sequence scopes remain distinct even when truncation gives them the
same `native_ms` and `native_frame`.

## 6. Input transport journal

Extend the existing input publication path with a clean, gameplay-only
timestamped journal. Do not restore the failed implementation as a block.

### 6.1 Record contract

Publish one immutable record whenever the worker observes a changed aggregate
snapshot affecting the ten gameplay controls. Each record contains:

- a strictly increasing transport sequence;
- the raw QPC timestamp taken for that observed transition;
- the 10-bit gameplay held mask immediately before the change;
- the 10-bit gameplay held mask immediately after the change;
- rising mask `after & ~before`;
- falling mask `before & ~after`; and
- a transport epoch/generation that changes whenever publication is reset.

Multiple bits changing in one observed snapshot are one atomic record. System
inputs outside the ten gameplay controls do not create judgement records. The
ordinary atomic aggregate remains published for FastIO and other consumers.

Raw Input keyboard transitions remain event-driven. XInput remains sampled by
the existing worker at the configured rate, which must be exactly 1000 Hz when
absolute judgement is enabled. A press and release wholly between two polls
can be missed; no downstream design can reconstruct an unobserved pulse.

The journal must have bounded, explicitly provisioned storage, report depth and
eviction, and preserve sequence order. Its transport plus retained-history
capacity must cover at least 60 seconds at 1000 transition records per second,
matching the exact-clock retention guarantee; a still-higher Raw Input burst
that exhausts it takes the explicit fatal path. It may use a simple synchronized
queue; the user explicitly rejected extra watermark/cutoff complexity for the
rare handoff race. Memory safety and deterministic ownership are mandatory, but
the design does not require lock-free input publication.

### 6.2 Accepted late-record rule

The producer may publish the new aggregate and be delayed before the complete
journal record becomes visible. If a later drain receives a record whose mapped
coordinate is at or before already committed judgement:

1. never replay native judgement in the past;
2. never retimestamp the transition to the current time;
3. count and log the late transition;
4. apply it in sequence only to correct the retained held baseline; and
5. expose no current press/release edge, paired companion, age-1 freshness, or
   direction head for it.

This intentionally misses that edge. A late press may be held in later scopes;
a late release makes later scopes unheld. The exception is expected to be rare
and avoids a transport watermark protocol. Journal eviction, a missing sequence
range, transport epoch loss, or unknown retained-history loss is not this
exception and is fatal during an active session.

## 7. Exact WASAPI event-time provider

The initial implementation supports only the exclusive WASAPI backend. Current
`PresentedClockPublication::Project` is unsuitable because it represents the
current value, can fall back to the last value, and applies a monotonic clamp.
The ordinary 32-span `AudioCursorTimeline` is also too short and is retained
only for existing DirectSound-compatible behavior.

### 7.1 Endpoint anchor ring

The WASAPI render/audio thread publishes a preallocated single-producer,
single-consumer anchor ring. Capacity is computed from the actual endpoint
period and guarantees at least 60 seconds of retained anchors. No audio-thread
lock, allocation, file logging, or wait is permitted.

Every committed anchor contains enough immutable information to resolve an
older input QPC in one continuous endpoint generation:

- raw `IAudioClock` endpoint position and endpoint clock frequency;
- the returned `qpc_100ns` value;
- unique endpoint generation;
- output sample rate;
- submitted output tail at publication; and
- stable identity/data for mapping endpoint position to the mixer's global
  output-frame coordinate.

For an input record, select the newest anchor at or before the record's QPC in
the same endpoint generation. Project from that anchor using checked rational
arithmetic. Resolution is valid only inside the submitted output tail. Do not
bracket between two later samples, because waiting for the following callback
would add callback-period latency. Do not clamp, extrapolate across a generation
change, or substitute a last known current value.

The provider exposes explicit statuses rather than ambiguous optionals:

- `NoPlayback`;
- `Pending`;
- `Resolved`;
- `TemporarilyUnavailable`;
- `HistoryLost`; and
- `Discontinuous`.

The same endpoint source supplies current exact ready time for the scheduler.
A transient failed clock read freezes delivery; it does not manufacture a time.

### 7.2 Exact playback mapping

The mixer already has cumulative playback mapping state (`epoch_source_start`,
`epoch_output_frames`, output/source rates, and playback epoch). Add a dedicated
lifetime-safe exact mapping publication per candidate voice/playback
generation. It contains:

- unique buffer/voice instance identity that is never inferred from reusable
  pointer value;
- playback generation;
- origin global output frame `O0`;
- origin source frame `S0`;
- output rate `Fo` and source rate `Fs`;
- mapped submitted tail;
- active/end state; and
- a lifetime-safe handle/version that readers can validate.

Publish the origin once when a new playback epoch first renders, then extend
its tail monotonically. Within that epoch:

`S(O) = S0 + (O - O0) * Fs / Fo`.

This mapping is exact and unwrapped. Seek, play/reset, buffer replacement, or
generation change creates a new identity; it never silently rebases an active
judgement session. Resolution must prove that projected output lies within both
the WASAPI submitted tail and the bound voice's mapped tail.

### 7.3 Authoritative BGM binding

Bind only through the game's existing sound-group-2 query inside
`ScopedGameplayAudioCursorQuery`. Extend its read-only observation to report:

- unique buffer/voice instance;
- lifetime-safe exact mapping handle;
- playback and endpoint generations;
- source/output rates and active state; and
- the number of qualifying active publications observed during the query.

Zero qualifying voices means `NoPlayback` while armed. Exactly one binds the
session. More than one is ambiguous and fatal before recognition. Pointer
identity alone and heuristic “most recent voice” selection are forbidden.

The judgement scheduler performs exactly one group-2/current-ready observation
per outer judgement call. Historical events are still resolved from their own
QPC anchors. The independent existing high-FPS clock query is not reused as a
mutable coordinator and is not changed by this feature.

## 8. Session lifecycle and identity

The feature has three states.

### 8.1 Installed

At process-start preflight, when enabled, validate:

- game-process role, target configuration, WASAPI-exclusive route/capability,
  exact input poll rate, QPC frequency, and bounded storage allocation;
- all six supported executable signatures, x86 calling conventions, trampoline
  availability, and rollback transaction;
- availability of the audited live configuration accessor; and
- that no prohibited rounded fallback or partial hook set can activate.

An actual endpoint and BGM voice do not exist in `DllMain`; installed state
therefore validates configured capability, not fictional active playback.

### 8.2 Armed

At a fresh native gameplay lifecycle, bind all session identities before the
first recognition step:

- native `Tune`/judgement/score state identity and lifecycle generation;
- expected CBooster receiver/session identity;
- unique BGM buffer/voice instance and playback generation;
- WASAPI endpoint generation and exact mapping;
- input transport epoch and session-start sequence cutoff;
- immutable `GameTimeOffset`;
- live `HoldSafeFrame` and `SlideHoldSafeFrame`, both exactly zero; and
- private committed delivery coordinate and heartbeat index consistent with
  exact `J` and the fresh native lifecycle.

Activation is tied to the native lifecycle, never to a timeout or “first clock
that happened to arrive.” Mid-song attachment after stock CBooster judgement
has begun is forbidden. While awaiting a same-lifecycle provider, withhold
recognition and retain input only while all required histories remain intact.

At session start, discard queued pre-session records through a recorded cutoff
and sample the current 10-bit held mask as the baseline. Pre-held controls have
no current edge, no paired companion, and held age at least 2. They cannot
synthesize a tap, free-input effect, flick head, or slide head.

### 8.3 Active

One active session is immutable with respect to every identity above. A normal
song end clears pending scopes, history, bindings, cutoff, and private boundary
state together. A normal next song creates a new session. No input age, paired
lookback, mapping origin, or committed index crosses sessions.

The game has no separate gameplay “pause” state in this design. Render/game
thread lag with a continuous advancing audio epoch is catch-up. Temporary
same-generation clock unavailability freezes delivery and retains work. A seek,
backward clock, changed identity, changed offset, changed safe value, or lost
history is a discontinuity/fatal condition, not a pause or an opportunity to
rebase.

## 9. Retained causal input view

The journal is drained into session-owned retained history. Each successfully
resolved event has an exact coordinate and original sequence. History remains
long enough for every pending scope and every required lookback. It is pruned
only after no current or future scope can query the removed prefix.

For `k in 0..4`, define pair `P_k = (k, k+5)`:

- IDs `0..9` are ordinary controls;
- ID `10+k` is the composite form of `P_k`; and
- ID `15+k` is the paired form of `P_k`.

Within an event scope at `(t, sequence)`:

- ordinary `Pressed(c)` is true exactly for `c` in that record's rising mask;
- ordinary `Released(c)` is true exactly for `c` in that record's falling
  mask; and
- ordinary `Held(c)` is the post-transition state in the causal prefix through
  that sequence.

A heartbeat has no current edge. Queries are pure and non-consuming: both
booster-component passes, all fixed candidates, descriptor lifecycle, score,
and free input may observe the same fact. Later equal-time records are not
visible to an earlier sequence scope.

### 9.1 Logical algebra

The scoped lower hooks implement only the audited logical algebra:

| Query | Composite `10+k` | Paired `15+k` |
|---|---|---|
| Held | either constituent held | both constituents held |
| Pressed | either constituent rises now | both rise now, or one rises now and the other rose in the inclusive preceding `4Q` |
| Released | either constituent falls now | the exact symmetric native falling-edge algebra established by E-046 |

A paired press/release always requires at least one constituent edge in the
current scope. Two historical edges never create a new current edge. Companion
lookback is exact-time and sequence-aware: an earlier equal-time record is
eligible; a later equal-time record is not. A companion is never consumed or
exposed as a new ordinary current edge.

Direction uses the retained historical held mask and the native mask/vector and
priority helpers where they can accept this seam without obsolete-ring side
effects. If not, reproduce only their smallest audited bit-mask algebra. Do not
move direction/note policy into the loader.

### 9.2 Held state, held age, and relative queries

`Held(id,t)` uses ordinary bits for `0..9`, constituent OR for `10..14`, and
constituent AND for `15..19`.

If the logical predicate is false, held age is 0. Otherwise let `s` be its most
recent false-to-true transition and compute:

`A_time(id,t) = 1 + floor((t-s)/Q)`.

Return:

- 1 only in the exact event record that creates this logical rise; and
- `max(2, A_time)` in every later scope while held.

Freshness therefore belongs to one immutable event, not the entire first
`Q`. This prevents an unrelated event scope from replaying one flick/slide
head. The native `<=4` companion-duration meaning remains four exact quanta.
Pre-held baseline state is never fresh and starts at age at least 2.

A native query expressed as `current_frame + delta` maps to
`scope_time + delta*Q`. In particular, the audited `current_frame-2` check means
exactly `t-2Q`. The causal prefix still applies: a future-offset query may carry
current held state forward but cannot see a later drained journal record.

The direction matcher `0x5D2E50` is the judgement consumer of held age. Head
mode requires age `<=1` for a fresh contributor and maximum contributor age
`<=4`; continuation mode uses held state without freshness. Flick `0x5D3320`
and slide-hold `0x5D35C0` therefore keep native head and continuation behavior
without a new matcher hook.

### 9.3 Zero release-grace precondition

Event scopes add native recognition calls for real transitions. Nonzero
`HoldSafeFrame` or `SlideHoldSafeFrame` would make their native per-call
countdowns expire faster. The supported design accepts only live values zero:

- `HoldSafeFrame` at native configuration offset `0x64` must be 0; and
- `SlideHoldSafeFrame` at offset `0x68` must be 0.

Read both through the same audited live accessor used by their handlers. A text
configuration file is diagnostic evidence, not the activation proof. A nonzero
value rejects activation/fails the session; it is never silently approximated.
Supporting nonzero grace is a separate future design.

## 10. Immutable recognition scope

Before every delivered unit, install one game-thread/TLS immutable scope that
contains:

- session and scope identity;
- exact time and sequence cutoff;
- native millisecond and authored-frame arguments;
- event held-before/after and rising/falling masks, or no-edge heartbeat state;
- retained-history view and current logical-rise facts;
- expected CBooster receiver identity; and
- per-scope diagnostic accumulators only, not consuming gameplay state.

Keep the same scope installed across original native recognition
`0x5D68E0` and immediately following original native score `0x5CF930`, then
clear it with an explicit lifetime guard even on failure. All five lower query
hooks answer from that scope only when it is active on the game thread and the
receiver/session identity matches. Outside a scope they trampoline unchanged.
An unexpected receiver or scope invariant during an active session is fatal;
mixing native-ring and retained-history facts is forbidden.

The native recognition and score pair receive identical `native_ms`. The score
object's audited counters at offsets `+120/+124/+128/+132` are snapshotted
around the original score call for MISS/GOOD/COOL/GREAT diagnostics. No score
hook and no loader grade policy is introduced.

## 11. Private scheduler

The scheduler replaces only the native uniform judgement loop at
VA `0x640239`. It does not replace the existing once-per-update tail.

### 11.1 Scope types and order

There are exactly two scope types:

1. one event scope for every retained, resolved transition; and
2. one heartbeat scope at every exact boundary `B_n=nQ` not represented by one
   or more event scopes at exactly `B_n`.

Merge by `(exact_time, sequence)`. An atomic record with multiple changed bits
is one scope. Same-time records remain separate scopes in sequence order. If
one or more events occur exactly at a heartbeat boundary, run all equal-time
event scopes and commit that boundary after the final one; do not add a
redundant no-edge heartbeat. This lets the last equal-time scope see the full
group while preserving causal facts for earlier records.

Every scope calls original recognition and then original score exactly once.
Extra recognition happens only for real transitions. A held key without a new
transition produces only 60-Hz heartbeats. Heartbeats preserve no-input MISS
progression, long-note lifecycle, and other native time work; event scopes
preserve sub-`Q` rapid-trigger transitions.

The complete native core may advance lifecycle during an event scope. This is
an intentional, render-independent consequence of avoiding loader note-policy
reimplementation. Only zero release-grace configurations are supported to
remove the known per-call countdown conflict.

After all due scopes, resume the original outer once-per-update tail exactly
once. Event count never becomes a `Tune` step count.

### 11.2 Ready time and catch-up

The scheduler retains both its last delivered exact `(time, sequence)` frontier
and its last committed heartbeat index; the former detects late records while
the latter derives cadence without turning event scopes into boundary steps.
Let `c` be the private last committed heartbeat index and
`target=floor(ready/Q)`. The scheduler advances at most three authored
boundaries (`3Q = 50 ms`) in one outer call:

- if `target-c > 3`, delivery horizon is `B_(c+3)`;
- otherwise delivery horizon is exact `ready`.

The second rule means an event may run before the next heartbeat in ordinary
high-FPS play. The first bounds native authored catch-up after a hitch without
discarding anything.

Within the selected horizon, deliver every event and boundary, with no semantic
cap on the number of event scopes and without splitting an equal-time group.
Events beyond the horizon stay pending. Boundaries are derived from integer
indices and require no queue entries. A ten-boundary hitch is delivered in
successive at-most-three-boundary batches; it is not collapsed to three.

The scheduler obtains one exact current-ready/group-2 observation per outer
judgement call. If the same generation is temporarily unavailable, freeze the
horizon, retain records, issue no native fallback recognition, and retry. When
continuity returns before history loss, perform normal ordered catch-up.

The scheduler's committed boundary index is private state. It never writes
`Tune+0x14`, and no value is accumulated in render units. At 144 or 165 FPS,
different outer updates merely group the same exact boundaries differently.

## 12. Configuration and backend policy

Add one startup-only setting:

```toml
[experimental]
enable_absolute_time_judgement = false
```

`false` is the default and means:

- install zero absolute-judgement sites;
- preserve stock input/judgement behavior and every audio backend;
- make no FPS-independent judgement guarantee; and
- emit a clear warning when a non-60 target uses stock judgement.

`true` means:

- use the full mechanism at every supported target FPS, including 60;
- require `audio_backend = "wasapi_exclusive"` for the first implementation;
- reject DirectSound and ASIO explicitly at startup;
- require exactly 1000-Hz input polling and both live safe-frame values zero;
- install all six sites or none; and
- never silently fall back to stock judgement after any absolute scope has run.

The feature does not hot-toggle. Changing the setting requires process restart.
Support for another backend requires its own exact event-QPC-to-source provider
and acceptance, not a configuration bypass.

## 13. Failure behavior

Expected nonfatal states are limited to:

- `NoPlayback`/`Pending` while armed before a unique BGM mapping exists;
- temporary same-generation clock unavailability while retained history is
  intact;
- ordinary render hitch/catch-up; and
- the accepted, individually counted late handoff record.

Fatal conditions include:

- partial/failed six-site transaction or ABI/signature mismatch;
- ambiguous active group-2 voice;
- endpoint/playback/input/native-session generation replacement inside an
  active session;
- backward exact time, seek, unexplained stop/reset, or changed immutable
  `GameTimeOffset`;
- nonzero/changed live safe-frame values;
- journal eviction, sequence corruption, transport epoch loss, anchor/mapping
  history loss, or unresolved event falling outside a provable submitted tail;
- checked-arithmetic overflow, decreasing/duplicate committed order, or
  recognition/score/scope count mismatch; and
- active-scope thread, receiver, or lifetime invariant failure.

On the first active-session fatal condition:

1. atomically latch the first reason;
2. stop issuing native recognition immediately;
3. emit and flush one structured snapshot containing mode/FPS, every session
   identity/generation, last anchor and exact `J`, committed boundary, pending
   work, last sequence, held mask, late/eviction counts, offsets, safe values,
   and native call/query/score counters; and
4. terminate the game through one shared nonblocking fatal path.

A minidump may supplement the snapshot through existing crash infrastructure,
but the snapshot cannot depend on it. Do not display an in-song blocking dialog,
keep the game running with judgement disabled, rebase after a discontinuity, or
fall back to native CBooster mid-session.

## 14. Required observability

The latest failed run proved that hook-install messages and coarse input/audio
activity cannot localize a judgement blackout. Normal builds must expose the
whole chain.

### 14.1 Info records

At `Info`, emit one startup record, one session-start record, a compact summary
at the existing roughly five-second cadence, and one session-end record. Do not
log every scope or query at `Info`.

Startup reports setting, target FPS, input rate, backend, exact-provider
capability, `rounded_fallback=0`, and installed site count (`0` or `6`). Session
start reports all identities/generations, cutoff, exact origin/rates, initial
`J`, private boundary index, offset, baseline mask, safe values, and armed
waits.

Periodic/end summaries include interval and cumulative counters for:

- transport records, rise/fall masks, pending/max depth, late records,
  evictions, and sequence errors;
- exact/resolved/unavailable clock reads, generation, last endpoint/output/
  source/QPC and `J`, history/discontinuity errors, and zero rounded fallback;
- outer calls, event/heartbeat scopes, equal-boundary substitutions, committed
  boundaries, batches, maximum batch/backlog/delivery delay, and pending work;
- recognition and score calls;
- all five query call counts and true/nonzero counts, including age-1 versus
  age-2-plus; and
- native MISS/GOOD/COOL/GREAT counter deltas.

The checked invariant is:

`recognition_calls == score_calls == event_scopes + heartbeat_scopes`.

### 14.2 Verbose scope records

The repository's existing most detailed runtime level is `Verbose`; do not
invent a new `Trace` enum. When `Verbose` is enabled, emit one compact record
after each delivered scope containing:

- session/scope identity and event/heartbeat/equal-boundary kind;
- journal sequence, exact mapped time, native ms/frame, and delivery delay;
- held-before/after and rise/fall masks;
- actual query calls/results and held-age classes seen in the scope;
- recognition/score completion and four score-counter deltas; and
- boundary commitment and remaining backlog.

Verbose logging is diagnostic, not performance acceptance. Always-on counters
must be sufficient to identify `journal -> scope -> query -> recognition ->
score -> grade` without per-call file I/O.

## 15. Proof and acceptance policy

No automated test or test target is part of this implementation plan. The
former test suite was intentionally removed because implementation-derived
fixtures repeatedly passed while the game remained broken. Do not restore it,
run TDD ceremony, or add a loader-side simulation of recognition.

A future automated check is permitted only if every expected value has an
independent, documented, formally strict oracle under `AGENTS.md`. This
specification by itself is not an independent oracle for gameplay behavior.

Keep three proof categories separate:

1. static/build proof: guarded sites, reviewed exact arithmetic/ownership, and
   complete x86 Debug and Release builds;
2. native-process structural proof: ordinary counters and targeted Verbose
   records show the full chain; and
3. actual game/operator acceptance: visible grade, score, effects, lifecycle,
   and chart completion in the supported executable.

Only the third permits the claim that judgement is sane in play.

### 15.1 Static review gates

- Six guarded interception sites install transactionally with verified bytes
  and correct x86 ABI.
- The existing framerate/shared-Tune/visual hook implementation has no
  behavior change from the clean baseline.
- Event `J`, native ms/frame, held age, lookback, boundaries, and horizon have
  no target-FPS or render-frame term.
- There is no rounded fallback, clamp, midpoint, arbitrary origin correction,
  loader note routing, replay source, CBooster-ring materialization, or test
  suite.
- Full `msvc32-debug` and `msvc32-release` preset graphs build.

### 15.2 First runtime gate at 240 FPS

Run one ordinary chart twice before broader testing:

1. **No input:** heartbeat scopes, recognition, score, and native MISS deltas
   advance; the song reaches normal result/lifecycle.
2. **Real input:** one physical rise appears as journal record -> resolved event
   scope -> true scoped query -> one recognition/score pair -> visible native
   result/grade delta. At least one reasonably timed input produces non-MISS.

Stop on either failure. The new counters must identify the first dead stage.

### 15.3 Real-input mechanic coverage

Actual keyboard/controller input and real charts must cover:

- basic taps on both sides without duplicate reuse;
- sub-`Q` rapid press/release and, where physically achieved, multiple ordered
  transitions within one render interval;
- simultaneous/multi-bit records and same-time sequence ordering;
- composite and paired current/current plus current/prior-inclusive-`4Q` cases,
  including rejection outside the window;
- one-shot flick/direction freshness;
- slide-hold head, continuation, direction change, and release;
- hold and dual-hold start, sustain, immediate zero-grace release, aggregation,
  duration, and result;
- scratch, beat, turn, hidden notes, free input, and mode-routed behavior;
- a pre-held session start followed by a genuine release/repress; and
- a render hitch with retained, original-time ordered catch-up.

Record which real charts exercise actual raw/effective note types `0..15`.
Shared wrapper coverage is insufficient; every type claimed must be observed in
real chart behavior. Visuals, sounds, displayed grades, score, and song
completion are part of the operator record.

### 15.4 Mandatory FPS matrix

Run a complete real-input chart at 60, 144, 165, and 240 FPS with absolute mode
enabled. At every rate:

- a fresh exact session activates;
- journal/event/query/native-call/grade counters are meaningful;
- call-count invariants hold;
- the chart finishes with sensible visible judgement and score;
- eviction, sequence error, rounded fallback, discontinuity, fatal invariant,
  and end-of-session backlog are zero; and
- any late record/unavailable read is reported, not hidden (repeat an
  acceptance run containing an accepted late miss).

At 144 and 165 FPS specifically, run a full song and prove after catch-up that
private committed index equals `floor(ready/Q)`, pending authored boundaries
are zero, exact rational boundary phase error is zero, and skipped/duplicate
boundary counts are zero. Non-multiple render rates may change only which outer
update delivers a boundary.

Check feature-off once: site count is zero, the backend is unrestricted, and a
non-60 configuration prints the explicit stock/no-guarantee warning.

Do not advertise 120, 360, or another rate for this feature until it passes the
same full-chart gate. Do not deploy to `H:\gc` unless a later task explicitly
authorizes runtime deployment.

Preserve the relevant logs and operator result for every mandatory rate, one
targeted Verbose rapid/direction/paired run, and one hitch run. There is no
replay-equivalence requirement because the broken stock 240-FPS path is not an
oracle.

## 16. Explicit non-repeat register

The implementation and review must reject these known failure patterns:

- **Contradictory time views:** never combine an historical timestamp with a
  later live input snapshot.
- **Discarded transition state:** retain before/after/rise/fall masks with the
  timestamp and sequence.
- **Native-fill capture ordering:** do not capture after native ring advancement
  and then infer that absence means no transition.
- **Arbitrary phase correction:** never align exact time by current render
  frame/current cursor or tolerate it with a clamp.
- **Shared-clock coupling:** do not change target-FPS `Tune` progression and add
  another hook to compensate.
- **Hook installation as success:** installed-site logs without scopes,
  native-call counts, query results, and score deltas prove nothing about
  gameplay.
- **Implementation-derived tests:** a loader model agreeing with itself is not
  native behavior proof.
- **Partial fallback:** never mix absolute history with the native CBooster ring
  inside one active session.

These are review gates, not optional historical commentary.

## 17. Planning boundary

The implementation plan must preserve separate, reviewable tasks for:

1. configuration and fail-closed activation shell;
2. clean gameplay transition journal;
3. exact WASAPI endpoint anchor history;
4. lifetime-safe voice playback mapping and group-2 binding;
5. exact session/time resolver and private scheduler;
6. retained-history query algebra and immutable scope;
7. six-site native transaction and original recognition/score dispatch;
8. structured failure and observability;
9. x86 Debug/Release build and static ownership review; and
10. staged real-game acceptance.

No production code is authorized by this document-writing task. Implementation
begins only under the separately reviewed plan.

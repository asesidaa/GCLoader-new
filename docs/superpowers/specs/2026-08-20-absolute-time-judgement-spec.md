# Absolute-Time Judgement Specification

**Date:** 2026-08-20

**Status:** Implemented baseline; 2026-08-21 runtime-correction design approved;
correction implementation not yet authorized

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
especially E-040, E-041, and E-042 through E-046. E-040 proves that playback
generation changes can occur during one ordinary stage, while E-044 identifies
the native construction and cleanup paths that actually delimit stage state.
That audit is complete and must not be repeated without a concrete, recorded
question not answered there. Current source is authoritative for integration
shape. The supported game binary/IDB and actual game behavior are the final
ground truth.

The approved discussion record remains useful rationale and examples, but this
document supersedes it wherever the two differ:

- [post-failure discussion record](2026-08-19-absolute-time-native-cadence-judgement-design.md)
- [failed-attempt index](../failed/2026-08-high-fps-input-judgement/README.md)
- [last runtime failure diagnosis](2026-08-19-absolute-time-judgement-driver-runtime-failure-diagnosis.md)

The first 240-FPS runtime run of the implementation produced materially better
judgement but exposed two specification omissions: loader-added event scopes
could erase native transient output before the once-per-update tail consumed
it, and an exact naturally drained BGM was incorrectly classified as a native
state mismatch. The same run proved that several required Info counters had no
producer. Section 19 records the approved 2026-08-21 correction closure. The
retained diagnosis and discussion evidence is under
`.superpowers/sdd/2026-08-20-absolute-time-judgement/runtime-evidence/`.

There are no unresolved high-level correction questions at this amendment.

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

The guarantee has three explicit, counted exceptions:

1. a transition that the input transport does not successfully publish before
   judgement commits past its time follows the accepted late-record rule;
2. when more than 32 ready, undelivered event records exist, the oldest excess
   event is deliberately consumed baseline-only at its chronological turn; and
3. native cleanup counts and discards any event that still cannot be delivered
   before the stage-owned native state is destroyed.

The latter two are overload shedding, not retimestamping or fallback. Ordinary
accepted gameplay requires both drop counts to remain zero. Unknown history
loss, corruption, clock discontinuity, or mixed judgement histories remain
fatal rather than additional exceptions.

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

The feature consists of seven responsibilities with one-way data flow:

```text
native stage init success (0x6629A0) ------------+
native stage cleanup entry (0x662080) -----------| stage lifetime
                                                  v
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

The judgement-facing native transaction has exactly eight interception sites:

1. native stage-state construction VA `0x6629A0` / RVA `0x2629A0`;
2. native stage-state cleanup VA `0x662080` / RVA `0x262080`;
3. scheduler seam VA `0x640239` / RVA `0x240239`; and
4. five lower CBooster query methods:
   - pressed VA `0x62DFB0` / RVA `0x22DFB0`;
   - held VA `0x62DF50` / RVA `0x22DF50`;
   - released VA `0x62DD30` / RVA `0x22DD30`;
   - direction VA `0x62E480` / RVA `0x22E480`; and
   - held age VA `0x62DAA0` / RVA `0x22DAA0`.

All eight sites form one guarded, fully preflighted set. Verify every signature
before creating the first hook. Any creation failure takes the startup-fatal
path immediately; the process never continues with a partial set and never
falls back to stock judgement. The existing Switch hooks remain separate
policy hooks and reach these lower queries through their original wrappers.

## 5. Exact time coordinate

Define the authored policy quantum exactly as:

`Q = 1/60 second`.

Let `R(q)` be the exact unwrapped source position of the bound gameplay BGM at
input QPC tick `q`. Let `G` be the stage's immutable native
`GameTimeOffset`. The judgement coordinate is:

`J(q) = R(q) + G`.

Native authored frame zero is `J = 0`; there is no loader-created stage-time
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

When publication is armed, sample QPC immediately before the existing aggregate
exchange, then publish the complete record after the exchange. For a changed
gameplay mask, that journal publication is the first operation after the
exchange: no logging, formatting, or unrelated work may run between them. The
existing Debug snapshot log runs only after the journal push. This preserves the
aggregate as FastIO authority while keeping the explicitly accepted handoff
window as short as the journal mutex permits; the window can make a record late,
but cannot silently retimestamp it to the end of a producer delay.
`QueryPerformanceCounter` failure is checked once and hard-aborts; it has no
fallback clock.

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
`(time,sequence)` sorts behind the committed frontier (including any equal-time
boundary already committed):

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
exception and is fatal during an active stage.

## 7. Exact WASAPI event-time provider

The initial implementation supports only the exclusive WASAPI backend. Current
`PresentedClockPublication::Project` is unsuitable because it represents the
current value, can fall back to the last value, and applies a monotonic clamp.
The ordinary 32-span `AudioCursorTimeline` is also too short and is retained
only for existing DirectSound-compatible behavior.

### 7.1 Endpoint anchor ring

The WASAPI render/audio thread publishes a preallocated single-producer,
single-consumer anchor ring. Capacity is computed from the actual endpoint
period as the exact integer ratio `period_frames/output_sample_rate` and
guarantees at least 60 seconds of retained anchors. No lock, allocation, file
logging, or wait is permitted in the successful render/publication path.

Every committed anchor contains enough immutable information to resolve an
older input QPC in one continuous endpoint generation:

- raw `IAudioClock` endpoint position and endpoint clock frequency;
- the returned `qpc_100ns` value;
- unique endpoint generation;
- output sample rate;
- submitted output tail at publication; and
- stable identity/data for mapping endpoint position to the mixer's global
  output-frame coordinate.

Anchor slots use an atomic version and scalar atomic payload fields. In the
first implementation, every slot-version and payload load/store uses
`std::memory_order_seq_cst`: the writer stores an odd version, stores the complete
payload, then stores the next even version; the reader accepts fields only when
two surrounding version loads return the same even value. This gives one
unambiguous total order on supported x86 builds. Do not weaken the memory order
without a separate C++ memory-model proof.

For an input record, select the newest anchor at or before the record's QPC in
the same endpoint generation. Project from that anchor using checked rational
arithmetic. Resolution is valid only inside the submitted output tail. Do not
bracket between two later samples, because waiting for the following callback
would add callback-period latency. Do not clamp, extrapolate across a generation
change, or substitute a last known current value.

The combined endpoint/voice resolver exposes explicit statuses rather than
ambiguous optionals:

- `NoPlayback`;
- `Pending`;
- `OutsidePlayback`;
- `Resolved`;
- `TemporarilyUnavailable`;
- `HistoryLost`; and
- `Discontinuous`.

`ExactWasapiClock` itself emits only `Pending`, `Resolved`,
`TemporarilyUnavailable`, `HistoryLost`, or `Discontinuous`; it does not emit
`NoPlayback` or `OutsidePlayback`. `NoPlayback` belongs only to the native
group-2 voice probe. `OutsidePlayback` belongs only to the bound voice-history
step after endpoint output is known exactly and retained playback history
proves a gap. A missing registered endpoint provider after enabled audio
startup is a capability/invariant failure, not a BGM wait state.

The process active-provider registry stores its `std::weak_ptr` and endpoint
generation as one state protected by a small mutex. Register, acquire, and
generation-matched unregister take that mutex; acquire promotes the weak handle
while still under the mutex, and stale cleanup may not clear a newer generation.
Registration happens before the render loop and unregistration after it. The
successful render path and `ExactWasapiClock::Publish` never touch the registry
mutex. This startup/cleanup synchronization is distinct from the lock-free
anchor publication path.

The same endpoint source supplies current exact ready time for the scheduler.
A bounded coherent-publication read that cannot obtain a stable same-generation
snapshot reports `TemporarilyUnavailable` and freezes delivery; it does not
manufacture a time. An underlying `IAudioClock` HRESULT failure keeps the
existing WASAPI engine's fatal behavior. Absolute judgement may publish that
failure context, but it does not add an audio retry/recovery state machine.

### 7.2 Exact playback-epoch history

The mixer already has cumulative playback mapping state (`epoch_source_start`,
`epoch_output_frames`, output/source rates, and playback epoch). Add a dedicated
lifetime-safe, preallocated history of exact mapping epochs per candidate
voice. Each epoch contains:

- a process-unique buffer/voice instance ID that is not a pointer value;
- the immutable WASAPI endpoint generation whose global output coordinate the
  history uses;
- playback generation and origin kind (`Play` or `Seek`);
- origin global output frame `O0`;
- origin source frame `S0`;
- output rate `Fo` and source rate `Fs`;
- exact global-output coverage, mapped tail, and—when naturally ended—the
  exact terminal source-frame coordinate; and
- a coherent publication sequence and lifetime-safe history handle.

The shared ring slots do not contain concurrently accessed, ordinary
`ExactPlaybackEpoch` objects. Each slot has an atomic publication version and
scalar atomic storage for every field, including optional engagement flags,
enum values, and the numerator/denominator of an exact closed source tail. As
with anchor slots, every version and payload operation uses
`std::memory_order_seq_cst` in the first implementation: the writer stores an odd version,
updates the scalar atomics, then stores the next even version; the reader loads
an even version, loads the scalar atomics, rechecks the same even version, and
only then reconstructs an ordinary `ExactPlaybackEpoch` in caller-owned
storage. Tail extension, later-epoch closure, and natural-end closure all use
that protocol. Do not weaken this ordering without a separate C++ memory-model
proof. A seqlock version around a concurrently mutated non-atomic payload is
forbidden because it would still be a C++ data race.

After mixer-node destruction has proved writer quiescence, buffer Release may
perform the one sequential writer handoff needed for
`WriterQuiescedRelease`; it uses the same atomic publication protocol. The
audio writer and Release writer are never active concurrently.

Publish an epoch origin when that generation first renders, then extend its
tail monotonically. Within one epoch:

`S(O) = S0 + (O - O0) * Fs / Fo`.

`Play` and `SetCurrentPosition` both create playback mapping epochs. The
previous epoch remains retained for historical input resolution and the next
epoch begins at the exact global output frame where the mixer applies it.
Buffer-instance, endpoint, and playback generations are never recycled inside
exact mode; counter exhaustion takes the fatal path rather than wrapping to an
apparently valid identity.
`Stop` stops future extension but does not by itself prove where a concurrent
last render ended. To avoid a cross-thread guess, the interval after the last
mapped tail remains `Pending` until a later retained epoch origin closes the
gap, an audio-thread natural-end publication closes the epoch, a buffer Release
first destroys/quiesces the sole mixer writer and then closes its stable tail,
or native cleanup discards the stage. Natural drain publishes the exact
source-length coordinate; a later epoch or writer-quiesced Release derives the
exact source coordinate at the mapped tail. None of these events ends the
native stage.

This distinction is mandatory. E-040 proves that the game can call
`SetCurrentPosition` on both stage-BGM channels during one stage, and the
DirectSound facade creates a new playback generation for each successful seek.
Therefore playback generation, seek, buffer lifetime, and audible drain are
audio-clock facts inside a stage, never stage-lifecycle signals.

The exact epoch history has 256 preallocated entries per gameplay-native
candidate voice when the feature is enabled. E-040 proves the game's periodic
seek-request cadence is one per three seconds, and E-041 suppresses harmless
in-margin requests, so this retains far beyond 60 seconds; any exceptional
faster-generation eviction is detected as `HistoryLost`, never hidden.
Historical resolution selects the retained epoch whose global-output interval
contains projected `O`; current ready-time resolution uses the authoritative
group-2 observation's current epoch. Both require `O` inside the endpoint
submitted tail and the voice mapped tail.

`Pending` means future coherent publication may still extend coverage over
`O`: for example, the first span of a new Play/Seek generation has not been
published or `O` is at/after a last tail that has neither a later epoch nor an
audio-thread natural-end marker. `OutsidePlayback` means retained history has
already proved that `O` lies before the first playback origin, in a gap bounded
by two retained epochs, or after a natural-end tail. It must never be returned
merely because the renderer has not published enough history yet. An output
older than an evicted prefix is `HistoryLost`, not `OutsidePlayback`.

An `OutsidePlayback` result may also identify the immediately preceding exact
closed playback frontier: the prior epoch's mapped tail and its exact source
coordinate. There is no such frontier before the first origin. Historical
input at an outside coordinate remains baseline-only. For current-ready
scheduling, the closed frontier is different: it permits any native work not
yet delivered through that exact tail to catch up once, then time freezes
there. This is not a last-value clamp or extrapolation—the audio thread or a
later epoch has proved the exact endpoint of coverage.

A forward seek is ordinary exact catch-up. A backward seek is accepted only
while it remains at or after the last committed judgement frontier and does not
reverse the exact coordinates of already retained, earlier-sequence input. If
it would move behind issued native work or make a later physical transition
sort before an earlier retained transition, the clock is genuinely
discontinuous and cannot preserve both exact timestamps and event order by
replay or rebasing.

### 7.3 Authoritative BGM binding

Obtain BGM authority only through the game's existing sound-group-2 cursor
getter inside `ScopedGameplayAudioCursorQuery`. Extend the exact observation
associated with that native getter to report:

- unique buffer/voice instance;
- lifetime-safe exact epoch-history handle;
- current playback generation/origin and endpoint generation;
- source/output rates, global output frame, and current mapping state.

Reuse the native getter's channel choice; do not enumerate all active group-2
voices and invent a second selection policy. The presence of the game's two
stage-BGM channels is not ambiguity. A negative native getter result is
`NoPlayback`; an observed generation whose first mixer span is not yet
published is `Pending`. Neither condition starts, ends, or times out a native
stage. A nonnegative native result means that the getter selected and returned
a cursor; it does not prove that the selected DirectSound voice is still
mixing. The successful call's exact observation supplies that state. A
nonnegative result without that observation is an invariant failure, not
`NoPlayback`.

The supported binary makes that choice concrete at VA `0x6122B0`: it builds
the requested group's ordered channel list, calls the channel cursor method
with the same output slot, and breaks on the first successful call. Therefore
the observation published inside that successful DirectSound cursor call is
the channel that supplies the native return value. The existing scoped
single-observation overwrite behavior is sufficient because the loop does not
continue after success.

Preserve the native return's sign as the cursor-selection decision: a negative
result is `NoPlayback` and discards any incidental unsuccessful observation.
For a nonnegative result, ignore the rounded millisecond magnitude and use only
the successful call's exact history observation. `Exact` permits current ready
time, `Pending` withholds work, and `Inactive` resolves the retained history for
a proven closed frontier. The rounded value is never a judgement timestamp,
proof of active mixing, or fallback.

The stage retains every authoritative history handle it has observed until
native cleanup. This lets input predating a seek resolve through the old epoch
while current ready time resolves through the new one. A buffer or channel
change chosen by the native group getter is likewise an audio-authority change,
not a stage change; overlapping mappings must agree on the exact coordinate or
report `Discontinuous` rather than choosing by pointer or recency.

The audited two-channel group shape limits simultaneous native channel choice;
it does not prove that only two distinct secondary-buffer lifetimes can occur
over an entire stage. Therefore stage retention is keyed by the process-unique
buffer instance ID and grows on the game thread for every newly observed
authoritative handle. Do not impose a two-history lifetime cap. Allocation
failure takes the active-stage fatal path; steady-state scope delivery performs
no allocation.

The outer-call operation that can register a new history is intentionally
allocation-capable and therefore is not `noexcept`. It is invoked only inside
the `noexcept` loop-hook handler, whose immediate exception boundary catches
allocation failure and enters the active-stage fatal path. Marking that helper
`noexcept` while relying on the outer handler is forbidden: an allocation
exception would call `std::terminate` before the handler could record the fatal
snapshot. No exception crosses the native hook ABI, and no exception creates a
fallback or recovery state.

The judgement scheduler performs exactly one group-2/current-ready observation
per outer judgement call. Historical events are still resolved from their own
QPC anchors. The independent existing high-FPS clock query is not reused as a
mutable coordinator and is not changed by this feature.

Current ready time resolves only through that outer call's native-selected
history. Historical event resolution evaluates every stage-retained history at
the event's endpoint output. Any `Discontinuous` or `HistoryLost` result is
fatal because lost coverage cannot be proved irrelevant. A `Pending` history
blocks a tentative result because future publication could still prove
overlapping coverage. Otherwise, one or more
`Resolved` histories must all produce the same exact source-seconds coordinate;
that coordinate wins over other histories proven `OutsidePlayback`. If every
history is proven outside, the combined result is `OutsidePlayback`. This
precedence prevents pointer order or incomplete publication from selecting a
timestamp.

## 8. Native stage lifecycle and stage identity

The feature has three operational states: installed, native stage open while
awaiting an exact clock, and active. State changes come only from explicit
native calls and provider statuses. Elapsed time is never an input.

### 8.1 Installed

At process-start preflight, when enabled, validate:

- game-process role, target configuration, WASAPI-exclusive route/capability,
  exact input poll rate, QPC frequency, and bounded storage allocation;
- all eight supported executable signatures, x86 calling conventions,
  trampoline availability, and fail-fast all-or-none installation;
- availability of the audited live configuration accessor; and
- that no prohibited rounded fallback or partial hook set can activate.

An actual endpoint and BGM voice do not exist in `DllMain`; installed state
therefore validates configured capability, not fictional active playback.

### 8.2 Native stage begin

Hook `CTuneGameManager` gameplay-state construction at VA `0x6629A0` / RVA
`0x2629A0`. Call the original first. A false return means native loading is not
complete and creates no loader state. Each successful return is the explicit
beginning of a new stage because that call has just constructed and initialized
the per-player judgement and score objects.

This is not a heuristic. `CTuneGameManager_RunGameplayFrameStateMachine` calls
this function only in native state `5`; a false result leaves that state in
place, while a true result immediately changes it to state `6`. Therefore one
native stage has exactly one successful construction transition. The hook
observes that transition and does not independently decide whether a stage has
begun.

On success, increment a loader-owned stage generation and reset every
stage-owned journal cursor, retained history, audio handle, frontier, heartbeat
index, counter, and immutable binding before any recognition can run. Record
the `CTuneGameManager` receiver only as an invariant inside that stage; its
address is not the stage identity and may be reused in the next stage.

At native stage begin, discard queued pre-stage records through a recorded cutoff
and take the journal's current 10-bit held mask from the same synchronized
cutoff snapshot as the baseline. Do not combine that cutoff with a separately
sampled later FastIO aggregate. Pre-held controls have no current edge, no
paired companion, and stale held age 5. They cannot synthesize a tap,
free-input effect, flick head, or slide head.

The transport exposes this as one atomic cutoff operation under its existing
journal mutex. It returns the transport epoch, first sequence eligible for the
new stage, held baseline, QPC frequency, and current eviction/fault count while
discarding the older queued prefix. The stage stores the returned fault count
as its baseline; only a later change represents loss inside that stage.

### 8.3 Clock wait and activation

The first intercepted scheduler call in the open native stage binds, before
the first recognition step:

- loader stage generation and native `CTuneGameManager` receiver;
- native `Tune`/judgement/score state and expected CBooster receiver/player;
- input transport epoch and the already-recorded stage-start cutoff/baseline;
- the same positive QPC frequency in the input cutoff and exact endpoint;
- immutable `GameTimeOffset`;
- live `HoldSafeFrame` and `SlideHoldSafeFrame`, both exactly zero; and
- WASAPI endpoint generation plus the authoritative group-2 exact history.

If group 2 has no selected cursor or its first exact mapping is still pending,
withhold recognition and retain input in sequence. There is no timeout,
inactivity counter, render count, or pointer-reuse heuristic. Do not classify a
retained record as pre-audio merely because it arrived before the first epoch
was published.

Once exact history exists, first project each retained QPC to endpoint output
`O`. A record that resolves inside an epoch follows the normal event path. A
record whose result is the proven `OutsidePlayback` status updates only the
held baseline in sequence and exposes no edge, paired companion, freshness, or
scope. A `Pending` predecessor remains retained and blocks later delivery; it
is not skipped or converted by elapsed time. `HistoryLost` and
`Discontinuous` retain their explicit failure meanings.

A stable playback generation change or seek only selects another retained
audio epoch inside the same native stage. Activation occurs once the first
authoritative epoch origin and endpoint generation are exact and the immutable
native/input/safe-value binding is valid. Current output may still be before
that origin; activation itself emits no work. A generation with no first epoch
remains `Pending`. Activation is forbidden if stock CBooster judgement somehow
ran after this stage's successful begin.

Native group-2 `NoPlayback`, coherent voice-history `Pending`, exact
`Inactive`, and a current endpoint coordinate proven `OutsidePlayback` do not
change lifecycle. For `Inactive`, retain the history selected by that
nonnegative native call and run the ordinary exact resolver; do not reinterpret
the returned cursor as proof of active mixing. Before withholding scopes, the
scheduler drains any newly proven closed frontier from the last native-selected
authoritative history through the ordinary bounded catch-up rule; it never
advances beyond that frontier. This covers a bounded Play/Seek gap or the suffix
after natural drain without skipping final due work. Before the first playback
origin there is no frontier and no scope. An unproven Stop tail remains
`Pending`. The same coordinate chain is reevaluated on the next outer call
without a timeout. An absent exact WASAPI provider, an inactive input transport
at the native begin cutoff, a nonnegative native group-2 result without its
scoped exact-history handle, or an endpoint/input generation replacement is an
invariant failure. None is converted into a timed wait or a stock fallback.

While active, native receiver/state identities, `GameTimeOffset`, the zero safe
values, QPC frequency, endpoint generation, and input epoch remain immutable. Render/game
thread lag with advancing audio is bounded catch-up. A coherent publication
read that is temporarily unavailable freezes delivery and retains work; no
elapsed duration converts it into another state. Retained-history coverage,
not a deadline, decides whether exact resolution remains possible.

### 8.4 Native stage end

Hook `CTuneGameManager_Cleanup` at VA `0x662080` / RVA `0x262080`. At function
entry, end the matching loader stage, count any remaining undelivered events as
explicit cleanup drops, emit the final summary, and clear pending scopes,
retained input/audio history handles, bindings, cutoff, and private indices
before calling the original cleanup. Cleanup before any successful stage begin
is an idempotent no-op for this feature. A later successful native construction
always starts a fresh loader stage even when every native or audio address is
reused.

Stage lifecycle itself is never inferred and never becomes fatal. `Play`,
`Stop`, natural drain, buffer Release, `SetCurrentPosition`, and playback
generation changes do not begin or end it. No input age, paired lookback,
mapping origin, or committed index crosses the explicit native boundary.

## 9. Retained causal input view

The journal is drained into stage-owned retained history. Each successfully
resolved event has an exact coordinate and original sequence. History remains
long enough for every pending scope and every required lookback. It is pruned
only after no current or future scope can query the removed prefix.

Every post-cutoff transport sequence is consumed exactly once, either as a
resolved event or by the explicit baseline-only rule for proven
`OutsidePlayback`/accepted-late input. Baseline-only consumption advances causal
state and sequence continuity but creates no event entry, edge, or freshness.

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
Pre-held or baseline-only state has no accepted rise coordinate. While it
remains held, return stale age `5`, the first value strictly outside the native
`<=4` direction window. This makes “at least 2” executable without allowing a
discarded edge to re-enter as a direction companion. A genuine on-time release
followed by an on-time rise establishes a new `s` and resumes the formula above.

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
value rejects activation/fails the stage; it is never silently approximated.
Supporting nonzero grace is a separate future design.

## 10. Immutable recognition scope

Before every delivered unit, install one game-thread/TLS immutable scope that
contains:

- stage and scope identity;
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
receiver/stage identity matches. Outside a scope they trampoline unchanged.
An unexpected receiver or scope invariant during an active stage is fatal;
mixing native-ring and retained-history facts is forbidden.

The native recognition and score pair receive identical `native_ms`. The score
object's audited counters at decimal offsets `+120/+124/+128/+132`
(`+0x78/+0x7C/+0x80/+0x84`) are snapshotted around the original score call for
MISS/GOOD/COOL/GREAT diagnostics. No score hook and no loader grade policy is
introduced.

Native recognition clears and republishes transient judgement-state fields
that the original outer tail later consumes for sounds and effects. The loader
must therefore preserve a native publication interval, not merely a
recognition/score call pair. Every loader-added event scope is the only native
recognition call between two executions of the original tail. It may not share
that interval with an earlier heartbeat, a later heartbeat, or another event.
This rule is general and must not be implemented by saving or OR-merging only
the currently known sound bytes.

A pure heartbeat batch remains permitted. Those scopes replace iterations the
original uniform native loop itself would execute during ordinary authored
catch-up, so their existing last-publication behavior remains native-owned.

## 11. Private scheduler

The scheduler replaces only the native uniform judgement loop at
VA `0x640239`. It does not replace the existing once-per-update tail.

The seam owns that loop only between the explicit successful native stage begin
and native cleanup. Outside an open stage, leave the original loop guard
untouched and let the five inactive query hooks trampoline normally. Once the
stage is open, always bypass the uniform loop—even before activation or during
an explicit no-scope status—and resume the original tail once. This preserves
non-stage native behavior without permitting stock CBooster judgement to mix
into an open absolute stage. No timer or inference chooses between the paths.

### 11.1 Scope types and order

There are exactly two scope types:

1. one event scope for every retained, resolved transition that remains
   eligible after explicit overload shedding; and
2. one heartbeat scope at every exact boundary `B_n=nQ` not represented by one
   or more event scopes at exactly `B_n`.

Merge by `(exact_time, sequence)`. An atomic record with multiple changed bits
is one scope. Same-time records remain separate scopes in sequence order. If
one or more events occur exactly at a heartbeat boundary, commit that boundary
after the final same-time event and do not add a redundant no-edge heartbeat.
Event isolation may deliver those separate same-time records across successive
outer calls; this does not change their exact time, sequence order, or the rule
that only the final record commits the boundary. A multi-bit atomic record is
never split.

Every scope calls original recognition and then original score exactly once.
Extra recognition happens only for real transitions. A held key without a new
transition produces only 60-Hz heartbeats. Heartbeats preserve no-input MISS
progression, long-note lifecycle, and other native time work; event scopes
preserve sub-`Q` rapid-trigger transitions.

The complete native core may advance lifecycle during an event scope. This is
an intentional, render-independent consequence of avoiding loader note-policy
reimplementation. Only zero release-grace configurations are supported to
remove the known per-call countdown conflict.

Every open-stage outer call still resumes the original outer tail exactly once.
The scheduler ends the current batch before executing a due event if one or
more heartbeat scopes have already run. If an event is the first native scope,
it runs alone and immediately ends the batch. All later due work remains at its
original coordinate for a later outer call. Event count never becomes a `Tune`
step count.

### 11.2 Ready time and catch-up

The scheduler retains both its last delivered exact `(time, sequence)` frontier
and its last committed heartbeat index; the former detects late records while
the latter derives cadence without turning event scopes into boundary steps.
The frontier also records whether a boundary at that exact time has committed.
An ordinary event frontier permits a later sequence at the same time; a
committed heartbeat/boundary is ordered after every possible event sequence at
that time. Therefore a record that becomes visible later at an already
committed boundary is the accepted late-record case, never a second
post-boundary event scope.

Let `c` be the private last committed heartbeat index and
`target=floor(ready/Q)`. The scheduler advances at most three authored
boundaries (`3Q = 50 ms`) in one outer call:

- if `target-c > 3`, delivery horizon is `B_(c+3)`;
- otherwise delivery horizon is exact `ready`.

The second rule means an event may become ready before the next heartbeat in
ordinary high-FPS play. The first bounds native authored catch-up after a hitch.
Neither horizon rule overrides event isolation or the explicit overload-drop
policy below.

Within the selected horizon, a batch is exactly one of:

- one event scope and no other native recognition call; or
- one to three heartbeat scopes and no event scope.

If heartbeats have run and the next merged unit is an event, defer that event
before installing its scope. If an event runs, defer everything after it. A
ten-boundary hitch is still delivered in successive at-most-three-heartbeat
batches; it is not collapsed to three. Events and boundaries beyond the batch
remain pending at their exact coordinates.

Protect the newest 32 ready, undelivered event records from overload shedding.
When another ready event makes older events excess, mark the oldest excess
events for dropping. Do not apply their `held_after` state immediately: each
marked event remains in exact order until it becomes the next chronological
unit, after all earlier heartbeats/events and before all later ones. At that
point consume it baseline-only with reason `Overload`, advance its transport
sequence, expose no edge/freshness/scope/sound/effect, and continue. A dropped
unit performs no native recognition and therefore creates no publication
barrier. This protects current physical input without letting a future held
state leak into an earlier heartbeat.

At explicit native cleanup, count and discard any remaining undelivered event
records. Neither overload nor cleanup dropping is fatal. Do not infer
press/release pairs, merge controls, retimestamp a record, or let stage-owned
held/edge state cross cleanup. The large transport and exact-history capacities
remain independent proof-retention bounds; unknown eviction or history loss is
still fatal rather than relabeled as overload shedding.

The scheduler obtains one exact current-ready/group-2 observation per outer
judgement call. Normal ready time is the current resolved coordinate. When
current output is outside but the last authoritative history proves a closed
tail, that exact tail is a fixed ready frontier until all due work through it is
committed; it never moves with later endpoint time. If the same generation is
temporarily unavailable, freeze the horizon, retain records, issue no native
fallback recognition, and reevaluate on the next outer call. When continuity
returns before history loss, perform normal ordered catch-up.

The scheduler's committed boundary index is private state. It never writes
`Tune+0x14`, and no value is accumulated in render units. At 144 or 165 FPS,
different outer updates merely group the same exact boundaries differently.

The first committed-boundary seed is also exact. If `J_begin` is the first
authoritative playback origin in judgement coordinates, initialize

`c = ceil(J_begin/Q) - 1`.

Thus a playback origin exactly on boundary `n` still delivers `B_n`, while an
origin between `B_n` and `B_(n+1)` starts at `B_(n+1)` and never invents a
heartbeat before exact playback coverage.

### 11.3 Worked examples

Suppose the last committed heartbeat is `10,000.000 ms`, the next is
`10,016.667 ms`, and a press maps to `10,005.400 ms`. If an outer update sees
ready time `10,008.000 ms`, it runs the event scope immediately even though the
next heartbeat is not due. Recognition and score both receive `native_ms =
10005`. A slower render may deliver the call later, but never changes that
argument.

For a 200-ms render hitch beginning after boundary `10,000.000 ms`, suppose the
journal retains:

```text
10,023.400 ms  press A
10,031.000 ms  release A
10,044.800 ms  press A again
```

When rendering resumes at ready time `10,200.000 ms`, event barriers split the
ready work across successive outer updates and original-tail executions:

```text
outer 1: 10,016.667  heartbeat, no edge; stop before the first event; tail
outer 2: 10,023.400  first A press only; tail
outer 3: 10,031.000  A release only; tail
outer 4: 10,033.333  heartbeat, no edge; stop before the next event; tail
outer 5: 10,044.800  second A press only; tail
outer 6: 10,050.000, 10,066.667, 10,083.333  heartbeat-only catch-up; tail
```

Later outer updates deliver the remaining boundaries through `10,200.000 ms`.
The two presses remain distinct, each event's transient native publications are
consumed by the immediately following original tail, and nothing is
retimestamped to resume time. The 50-ms limit bounds heartbeat-only authored
catch-up per outer call; it is neither an input fusion window nor permission to
mix event and heartbeat scopes in one tail batch.

## 12. Configuration and backend policy

Add one startup-only setting:

```toml
[experimental]
enable_absolute_time_judgement = false
```

`false` is the default and means:

- install zero absolute-judgement sites;
- preserve stock input/judgement behavior and every audio backend;
- do not allocate/register the 60-second exact WASAPI anchor provider or
  publish gameplay transitions;
- make no FPS-independent judgement guarantee; and
- emit a clear warning when a non-60 target uses stock judgement.

`true` means:

- use the full mechanism at every supported target FPS, including 60;
- require `audio_backend = "wasapi_exclusive"` for the first implementation;
- reject DirectSound and ASIO explicitly at startup;
- require exactly 1000-Hz input polling and both live safe-frame values zero;
- install all eight sites or none; and
- never run stock CBooster judgement while an explicit native stage is open.

The feature does not hot-toggle. Changing the setting requires process restart.
Support for another backend requires its own exact event-QPC-to-source provider
and acceptance, not a configuration bypass.

## 13. Failure behavior

Expected nonfatal states are limited to:

- a native stage that is open while group 2 is `NoPlayback`, voice history is
  `Pending`/`Inactive`, or current exact output is proven `OutsidePlayback`;
- a proven `OutsidePlayback` transition applied only to the held baseline;
- overload shedding after more than 32 ready event records, with each oldest
  excess edge consumed baseline-only at its chronological turn;
- native cleanup, which counts/discards remaining undelivered events and closes
  the stage immediately without a timer;
- temporary coherent-publication unavailability while retained history is
  intact;
- an ordinary `Play`, `SetCurrentPosition`, stop, drain, or playback-generation
  transition whose exact mapping remains provable inside the same native stage;
- ordinary render hitch/catch-up; and
- the accepted, individually counted late handoff record.

### 13.1 Fail-fast implementation rule

An internal Boolean/result whose contract is “this operation is expected to
succeed” must not create a fallback mode, retry state machine, or ladder of
error propagation. Check it once. During process installation, before any
successful native stage begin, failure enters the existing startup-fatal path.
From a successful native stage begin through matching cleanup—including the
clock-wait interval before activation—failure enters the one active-stage fatal
path below. Both paths terminate the process.

An existing or layer-appropriate Boolean API may remain, but its owning caller
checks it exactly once and immediately enters the phase-appropriate fatal path;
it does not propagate the Boolean into runtime policy. `...OrFatal`/`void` is
also appropriate when it preserves cleaner ownership. A plain C/C++ `assert()`
is insufficient because Release builds may compile it out; the check and
termination must be active in both Debug and Release. This rule applies to
unlikely setup/invariant results, not to expected operational states such as
stage-open `NoPlayback`/`Pending`/`Inactive`/current `OutsidePlayback`, coherent
`TemporarilyUnavailable`, or explicit overload/cleanup dropping, which retain
their defined status semantics.

Every installed native hook handler remains `noexcept`, but broad defensive
`try`/`catch` is forbidden. The only authorized new exception boundary is the
allocation-capable native loop-hook boundary described in Section 7: map
`std::bad_alloc` to `StorageAllocationFailure`, map any other unexpected
exception to `UnexpectedInternalException`, and enter the one-way active-stage
fatal path. Other new hook paths must be nonthrowing rather than wrapping normal
control flow in catches. No exception is swallowed, retried, or converted into
fallback behavior, and no exception crosses the native ABI.

New formatting in this feature uses C++23 `std::format`/`std::format_to`. Do not
introduce `std::ostringstream`, `std::wostringstream`, or another string-stream
formatting path.

The eight sites are preflighted together and are never exposed as a
partial operational mode. If installation fails, startup terminates; there is
no fallback to stock judgement. Ordinary RAII cleanup is welcome, but no
custom recovery protocol is required merely to keep the doomed process alive.

Fatal conditions include:

- partial/failed eight-site transaction or ABI/signature mismatch;
- absent input capability at the native begin cutoff, absent endpoint
  capability at the first open-stage scheduler probe, or
  endpoint/input/native-state replacement inside an explicitly active native
  stage;
- conflicting/overlapping playback mappings, evicted required coverage, or a backward
  seek whose resolved coordinate is behind already-issued native work or
  reverses earlier-sequence retained event coordinates;
- backward endpoint time, unexplained reset, or changed immutable
  `GameTimeOffset`;
- nonzero/changed live safe-frame values;
- journal eviction, sequence corruption, transport epoch loss, or an event
  older than retained endpoint/mapping history;
- checked-arithmetic overflow, decreasing/duplicate committed order, or
  recognition/score/scope count mismatch; and
- active-scope thread, receiver, or lifetime invariant failure.

On the first active-stage fatal condition:

1. atomically latch the first reason;
2. stop issuing native recognition immediately;
3. emit and flush one structured snapshot containing mode/FPS, every stage
   identity/generation, last anchor and exact `J`, committed boundary, pending
   work, last sequence, held mask, late/drop/eviction counts, offsets, safe
   values, and native call/query/score/publication counters; and
4. terminate the game through one shared terminal fatal path after that single
   best-effort synchronous flush.

A minidump may supplement the snapshot through existing crash infrastructure,
but the snapshot cannot depend on it. Do not display an in-song blocking dialog,
keep the game running with judgement disabled, rebase after a discontinuity, or
fall back to native CBooster mid-stage.

## 14. Required observability

The latest failed run proved that hook-install messages and coarse input/audio
activity cannot localize a judgement blackout. Normal builds must expose the
whole chain.

### 14.1 Info records

At `Info`, emit one startup record, one native-stage-open record, one
absolute-stage-activation record, a compact summary at the existing roughly
five-second diagnostic cadence, and one native-stage-end record. The cadence is
observability only and never participates in lifecycle or failure decisions.
Do not log every scope or query at `Info`.

Startup reports setting, target FPS, input rate, backend, exact-provider
capability, `rounded_fallback=0`, and installed site count (`0` or `8`). Native
stage open reports loader stage generation, native manager identity, input
generation, cutoff/first eligible sequence, baseline mask, and transport fault
baseline. Absolute activation reports the complete native state identity,
endpoint generation, authoritative observed BGM history IDs and Play/Seek epoch
counts, exact origin/rates, initial `J`, private boundary index, offset, safe
values, and accumulated clock waits. These are separate records because the
explicit native stage can open before any BGM epoch exists.

Periodic/end summaries include interval and cumulative counters for:

- transport records actually drained, rising/falling control-bit counts,
  pending/max depth, late records, outside-playback baseline-only records,
  overload drops, cleanup drops, evictions, and sequence errors. One record
  that raises two controls increments records by one and rising controls by two;
- exact/resolved/unavailable clock reads, real endpoint publication
  sequence/count, real retained Play/Seek epoch counts, actual last
  endpoint/output/source/QPC and `J`, closed-frontier selections, final frozen
  coordinate, history/discontinuity errors, and zero rounded fallback;
- outer calls, event/heartbeat scopes, event-only batches, heartbeat-only
  batches, mixed event batches, event-barrier deferrals, equal-boundary
  substitutions, committed boundaries, closed-frontier catch-ups, maximum
  event backlog/batch/delivery delay, and pending work;
- recognition and score calls;
- all five query call counts and true/nonzero counts, including age-1 versus
  age-2-plus. The already-collected per-scope values must be checked-added into
  the stage totals rather than discarded after Verbose formatting;
- native MISS/GOOD/COOL/GREAT counter deltas; and
- raw aggregate counts of the audited native transient sound publications,
  including arrange, left free-tap, and right free-tap requests observed after
  the native pair and before returning to the original tail.

Remove a summary field if no truthful owner/publication source exists. A
declared atomic with no writer or a default-zero snapshot member is not
observability and must not remain in the output.

The checked invariants are:

`recognition_calls == score_calls == event_scopes + heartbeat_scopes`.

`event_scopes == event_only_batches`.

`mixed_event_batches == 0`.

At final summary, every post-cutoff transport record must be explained as an
event scope, outside-playback baseline-only record, accepted-late record,
overload drop, cleanup drop, or still-pending record. Ordinary accepted stage
completion has no pending record.

### 14.2 Verbose scope records

The repository's existing most detailed runtime level is `Verbose`; do not
invent a new `Trace` enum. When `Verbose` is enabled, emit one compact record
after each delivered scope containing:

- native-stage/scope identity and event/heartbeat/equal-boundary kind;
- journal sequence, exact mapped time, native ms/frame, and delivery delay;
- held-before/after and rise/fall masks;
- actual query calls/results and held-age classes seen in the scope;
- recognition/score completion and four score-counter deltas; and
- boundary commitment, event-isolation/batch disposition, remaining backlog,
  and the raw audited transient publication bits observed immediately before
  returning to the original native tail.

Verbose logging is diagnostic, not performance acceptance. Always-on counters
must be sufficient to identify `journal -> scope/drop -> query -> recognition
-> score -> grade/publication` without per-call file I/O. No new sound hook or
loader-owned playback call is introduced for diagnostics.

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

- Eight guarded interception sites are preflighted before mutation and all eight
  install with verified bytes and correct x86 ABI before operational mode is
  exposed.
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
   advance; exactly one native-stage-open and one absolute-stage-activation
   record appear; exact natural drain selects a closed frontier rather than a
   native-state fatal; the song reaches normal result/lifecycle.
2. **Real input:** one physical rise appears as journal record -> resolved event
   scope -> true scoped query -> one recognition/score pair -> visible native
   result/grade delta -> event-only native-tail interval. At least one reasonably
   timed input produces non-MISS, and reported hidden/ad-lib plus free-input
   cases produce their expected audible native result.

Stop on either failure. The new counters must identify the first dead stage.

### 15.3 Consecutive native-stage lifecycle gate

Complete two ordinary charts without restarting the game process. Each chart
must produce exactly one successful native-stage-open record and one matching
native cleanup/end record, plus exactly one absolute-stage-activation record.
Loader stage generation must increase, and no input age, history, audio epoch
handle, committed frontier, or counter from the first stage may enter the
second. Native/audio addresses may repeat without changing the lifecycle
decision. This is not an input replay or equality oracle.

### 15.4 Real-input mechanic coverage

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
- a pre-held stage start followed by a genuine release/repress; and
- a render hitch with retained, original-time ordered catch-up.

Record which real charts exercise actual raw/effective note types `0..15`.
Shared wrapper coverage is insufficient; every type claimed must be observed in
real chart behavior. Visuals, sounds, displayed grades, score, and song
completion are part of the operator record.

### 15.5 Mandatory FPS matrix

Run a complete real-input chart at 60, 144, 165, and 240 FPS with absolute mode
enabled. At every rate:

- a fresh explicit native stage activates exact judgement;
- journal/event/query/native-call/grade counters are meaningful;
- call-count and event-isolation invariants hold;
- the chart finishes with sensible visible judgement and score;
- eviction, sequence error, rounded fallback, discontinuity, fatal invariant,
  mixed event batch, overload drop, cleanup drop, and end-of-stage backlog are
  zero; and
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
- **Playback generation as stage identity:** ordinary `SetCurrentPosition`
  creates a new generation inside one stage. Only native state construction
  success and cleanup delimit the stage.
- **Shared-clock coupling:** do not change target-FPS `Tune` progression and add
  another hook to compensate.
- **Hook installation as success:** installed-site logs without scopes,
  native-call counts, query results, and score deltas prove nothing about
  gameplay.
- **Implementation-derived tests:** a loader model agreeing with itself is not
  native behavior proof.
- **Partial fallback:** never mix absolute history with the native CBooster ring
  inside one active stage.
- **Recovery machinery for impossible internal failures:** do not turn an
  expected-success Boolean into fallback/retry state. Perform one always-on
  check and terminate through the phase-appropriate fatal path.
- **Transient-output clobber:** never execute a loader-added event recognition
  in the same native-tail publication interval as another recognition call.
  Do not preserve only the currently reported sound bytes or invent merge
  semantics for native transient arrays.
- **Cursor sign as mixing state:** a nonnegative native group cursor identifies
  the selected cursor/history; it does not override an exact `Inactive`
  observation or turn natural drain into a native-state mismatch.
- **Silent diagnostic zeros:** do not print declared counters or snapshot fields
  that have no truthful producer assignment.
- **Early overload baseline:** never apply a dropped event's `held_after` state
  before chronological scheduling reaches that event's exact coordinate.

These are review gates, not optional historical commentary.

## 17. Planning boundary

The implementation plan must preserve separate, reviewable tasks for:

1. configuration and fail-closed activation shell;
2. clean gameplay transition journal;
3. exact WASAPI endpoint anchor history;
4. lifetime-safe voice playback mapping and group-2 binding;
5. exact stage/time resolver and private scheduler;
6. retained-history query algebra and immutable scope;
7. eight-site native transaction, explicit stage lifecycle, and original
   recognition/score dispatch;
8. structured failure and observability;
9. x86 Debug/Release build and static ownership review; and
10. staged real-game acceptance.

No production code is authorized by this document-writing task. Implementation
begins only under the separately reviewed
[implementation plan](../plans/2026-08-20-absolute-time-judgement.md).

## 18. Final consistency-audit closure

The final spec/plan audit closed these implementation traps without changing
the approved product behavior:

- QPC is sampled before aggregate publication, while the complete journal
  record is inserted after it; the stage cutoff is one journal-locked operation.
- Endpoint-clock absence is a fatal capability error, not `NoPlayback`; native
  group sign and the successful scoped observation remain the sole channel
  authority.
- Two simultaneous native BGM channels do not imply a two-buffer-lifetime cap;
  every observed authoritative history is retained through native cleanup.
- Control-thread Stop does not invent a closing tail; a later epoch,
  audio-thread natural end, or Release after proven writer quiescence closes
  coverage exactly.
- A closed playback tail is a fixed final ready frontier, preventing the last
  due heartbeat from being skipped without extrapolating beyond audio.
- The first heartbeat seed is `ceil(J_begin/Q)-1`, so no boundary is invented
  before a non-boundary playback origin.
- Outside-playback and accepted-late records still consume their transport
  sequence through the baseline-only path; resolved history cannot report a
  false sequence gap.
- Baseline-only held state uses stale age `5`, not an arbitrary value in the
  native freshness window.
- A backward seek must preserve both the committed native frontier and physical
  event sequence order.
- The uniform loop is untouched outside an explicit native stage and always
  bypassed while that stage is open; neither choice uses a timer.
- Native-stage-open and absolute-stage-activation diagnostics are separate
  because native construction can precede the first exact BGM epoch.

These corrections are binding review gates and must be reflected task-by-task
in the runtime-correction implementation plan before production code changes.

## 19. 2026-08-21 runtime-correction closure

The first implemented 240-FPS run is preserved as
`.superpowers/sdd/2026-08-20-absolute-time-judgement/runtime-evidence/20260821T025248+0800-240fps-loader-log.txt`
with SHA-256
`4371D789B0D4EC98E9923FC9A7618408CC03D2B69E51DBBE32DA056C70F922E7`.
The corresponding diagnosis closes these source-to-native chains:

- event recognition can set free-tap sound bytes `+237/+238`, and accepted
  muted hidden/ad-lib lifecycle can set `+170`; a later recognition clears them
  before the single original tail consumes them;
- the natural BGM tail was exact at `J=103.4763591 s`, but a nonnegative retained
  cursor plus exact `Inactive` observation entered `NativeStateMismatch`; and
- transport/query/endpoint/epoch counters and `last_endpoint_position` printed
  zeros even though their producer/assignment wiring was absent.

The design is therefore corrected, not replaced:

1. every event scope owns one complete recognition -> score -> original-tail
   publication interval;
2. heartbeat-only native-equivalent catch-up remains bounded to three authored
   boundaries per outer call;
3. the newest 32 ready event records remain eligible, while older excess events
   are marked and consumed baseline-only at their chronological turn;
4. natural exact `Inactive` resolves/catches up through a proven closed frontier
   and freezes there until native cleanup; and
5. diagnostics obtain values from real owning boundaries and expose event batch,
   drop, query, score, and transient-publication evidence.

The accepted cost is delivery latency, not timestamp error. An event separated
from a preceding heartbeat may be delivered one render update later: about
`4.17 ms` at 240 FPS, `6.06 ms` at 165 FPS, `6.94 ms` at 144 FPS, or
`16.67 ms` at 60 FPS. Additional already-ready event records take successive
outer calls. With three-heartbeat catch-up, the approximate steady-backlog
service condition is

`event_records_per_second <= render_fps - 20`.

This is about 40, 124, 145, and 220 event records per second at 60, 144, 165,
and 240 FPS. A full press/release tap normally creates two records. The observed
240-FPS run produced about 7.5 event records per second. The 32-record shedding
rule therefore targets synthetic bounce, a malfunctioning device, or an
extraordinary stall rather than normal human rapid trigger.

Rejected corrections remain prohibited: OR-merging selected transient fields,
calling sound/effect consumers from loader policy, replaying the inline native
tail multiple times without a complete side-effect proof, patching only the
three reported bytes, fabricating a negative DirectSound cursor, or adding an
in-song blocking fatal dialog. The existing fatal path remains one synchronous
log flush followed by termination; removing the false natural-end fatal is the
bug fix.

No automated gameplay test or emulated expected-value model is authorized for
this correction. Static review and x86 Debug/Release builds are implementation
evidence; the supported game at 60/144/165/240 FPS remains the only gameplay
acceptance oracle.

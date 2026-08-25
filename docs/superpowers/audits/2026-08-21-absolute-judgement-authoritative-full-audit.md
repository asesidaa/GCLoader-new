# Absolute-judgement authoritative full audit

Date: 2026-08-21

Status: **COMPLETE — authoritative audit input for the next specification discussion; not an implementation or runtime-acceptance claim**

Scope: current absolute-judgement input patch only

Method: main-agent audit; no delegated conclusions, no new IDA archaeology, no source/spec/plan/build/test/deployment changes

## Authority and stop rules

This audit uses:

- the current worktree implementation under `src/Patches/AbsoluteJudgement`;
- the input transition transport called by that implementation;
- the supported-binary results already closed in E-042 through E-046;
- the current deployed `H:\gc\loader-log.txt`; and
- explicit user support decisions, including zero-only safe frames, mandatory
  booster capability, accepted bounded overload loss, no fallback, and exact
  fatal logging.

It does not treat any earlier 2026-08-21 generated audit verdict as authority.
Those files remain temporarily only so this audit can replace rather than
silently lose their raw citations. They will be deleted after this file closes.

## Required classification for every predicate

Each predicate receives exactly one result:

1. `NORMAL_NATIVE_OR_GAME_STATE`: valid behavior that must be tolerated or
   reproduced.
2. `EXPLICITLY_UNSUPPORTED`: native-valid behavior the user deliberately chose
   not to support; it may terminate only with an exact log.
3. `PROVEN_INTERNAL_INVARIANT`: impossible after the loader's complete
   construction theorem; it may terminate only with the failed expression and
   all operands logged.
4. `RESOURCE_LIMIT`: finite representation exhaustion, not corruption; exact
   capacity and value must be logged.
5. `DIAGNOSTIC_ONLY`: cannot affect judgement and must never terminate gameplay.
6. `DEFECT`: a reachable false assertion, semantic mismatch, wrong lifecycle,
   dropped native effect, or unsafe continuation.
7. `UNPROVEN`: neither continuing nor terminating is justified; implementation
   must not be changed until the missing premise is closed.

Broad reasons such as `NativeStateMismatch`, `RetainedHistoryLost`,
`CheckedArithmeticFailure`, or `CommittedOrderViolation` are never audit
results. The originating Boolean predicate is the audit unit.

## Inventory checkpoint 1 — implementation and termination surface

The owned feature contains 17 files:

- patch transaction and hook shims;
- runtime/native identity extraction and native call execution;
- native-context/stage state;
- exact clock resolver;
- retained input history;
- scheduler;
- scoped CBooster query view; and
- diagnostics/fatal publication.

The audit additionally includes the gameplay transition journal entry points
and the input-polling QPC abort used by this feature.

Every call to `Fatal`, runtime `Fail`, `std::abort`, `TerminateProcess`, every
`JudgementHistoryError`, every `JudgementStageError`, and every query invariant
is in scope. Success-only startup transaction failures are in scope for logging
completeness but are not presumed recoverable.

The following sections classify the complete inventory. This file remains
in-progress only until its final consistency pass and replacement-file cleanup
are recorded at the end.

## Executive verdict

The current implementation is not safe to repair by deleting the deployed
`CountResolvedAtOrBefore` lower-bound check and continuing. That edit removes
the immediate false fatal, but it does not repair the transition that created
the stale cursor and it does not restore the input edges that were discarded.

The whole project does not need to be discarded. These parts remain useful:

- the 1000 Hz transition journal with exact QPC timestamps;
- one immutable input view for a whole native recognition call;
- exact event scopes plus exact `n/60` heartbeat scopes;
- one event-bearing native call per rendered outer call, followed by the one
  original native tail;
- at most three heartbeat-only catch-up calls per rendered outer call;
- the newest-32 ready-event overload policy;
- the approved continuous held-age and inclusive prior-four-quantum policy;
  and
- the existing native recognition, score, descriptor, result-publication, and
  tail code.

Two foundations do have to be replaced before those pieces are safe:

1. construction/destruction is being used as gameplay-stage lifecycle; and
2. every input event is being mapped through the audio buffer Play/Seek epoch
   that happens to cover it.

Both are contradicted by the native game. The first caused the deployed fatal.
The second turns normal native audio correction into apparent judgement-time
rewind, gaps, and history conflicts.

## Evidence boundary

The native lifecycle findings below come from the already-persisted Tune-run
decompile in
`H:\gc\runs\20260815T182438Z-297470b1\artifacts\audit-decompile-tune-run.json`
and its formatted high-FPS scheduling entry-point record. The frame/audio
master relationship comes from E-040 and the persisted decompile of
`GC120FPS_GameplayAudioSync_CheckAndSeek` at `0x640070`, called from
`0x664DB2`. Input algebra and result ownership come from E-042 through E-046.
No new IDA work was performed.

The runtime trace is `H:\gc\loader-log.txt`, produced by the deployed DLL.
The worktree contains a later, unbuilt one-line edit in
`JudgementHistory.cpp`; this audit keeps deployed behavior and unbuilt source
behavior distinct.

## Correct game-state vocabulary

There is no gameplay-input state named `OutsidePlayback`.

The relevant game lifecycle is:

`NON_STAGE -> STAGE_ENTRY_TRANSITION -> ACTIVE_STAGE -> STAGE_EXIT_TRANSITION -> NON_STAGE`

- `NON_STAGE` includes object construction, song loading, preparation, and
  countdown.
- `STAGE_ENTRY_TRANSITION` is the native states-16/17 shared transition block.
  It initializes Tune time, initializes gameplay input frame zero, starts the
  stage BGM pair, initializes gameplay fields, and selects state 18.
- `ACTIVE_STAGE` is native state 18. Only this state executes the audio-sync,
  input-frame, recognition, and score path.
- `STAGE_EXIT_TRANSITION` is either natural completion through the state-18
  end branch that selects state 19, or committed Test Mode entry while a song
  is active. The natural branch performs no input-frame, recognition, or score
  call on that iteration; Test Mode termination bypasses that branch.
- Native object cleanup at `0x662080` occurs later and is not the stage exit.

For lossless entry behavior, the loader cutoff belongs at the start of the
states-16/17 shared transition block, before the native frame-zero input
initialization and BGM Play call. Natural completion closes the loader stage
when the state-18 end branch is selected. Entering Test Mode during an active
song is a second valid exit which bypasses that branch; committed Test Mode
entry must therefore close the same loader stage. Every song/retry cycle must
still have exactly one entry and one exit, independent of whether the enclosing
Tune object is reused or reconstructed.

Object construction and destruction may arm and destroy loader storage. They
must not open or close semantic gameplay input history.

### Implemented semantic hook-site contract

The 2026-08-22 implementation uses the already-persisted raw disassembly in
`H:\gc\runs\20260815T182438Z-297470b1\artifacts\audit-core-callsite-disasm-2026-08-17.txt`
and a byte-for-byte `dumpbin /disasm:bytes` check of the supported
`H:\gc\game471.exe`. No IDA database was reopened.

| Meaning | VA / RVA | Guarded whole instructions | Tune receiver | Hook / continuation | Ordering proof |
|---|---|---|---|---|---|
| Semantic stage entry | `0x6641CC` / `0x2641CC` | `8B 8D 4C FD FF FF C7 41 10 00 00 00 00` (`mov ecx,[ebp-0x2B4]`; `mov dword ptr [ecx+0x10],0`) | pointer stored at `[EBP-0x2B4]` | SafetyHook MidHook before the guarded instructions; relocated instructions resume at `0x6641D9` | This is the jump-table case-17 block. A committed case 16 sets state 17 at `0x6641BF..0x6641CB` and falls into it; an existing state 17 enters it directly. The hook therefore runs once at the common transition before Tune frame zero is stored, before `GameplayInput_SetCurrentFrameAndFillHistory` at `0x6641F7`, and before the BGM-pair `Play` at `0x664235`. |
| Natural semantic stage exit | `0x664D9A` / `0x264D9A` | `8B 95 4C FD FF FF C7 42 04 13 00 00 00` (`mov edx,[ebp-0x2B4]`; `mov dword ptr [edx+4],19`) | pointer stored at `[EBP-0x2B4]` | SafetyHook MidHook before the guarded instructions; relocated instructions resume at `0x664DA7` | The state-18 end predicate is tested at `0x664D8F..0x664D98`; only its taken exit path reaches this site. The original write selects state 19, then `0x664DA7` jumps past audio sync, input-frame update, and the judgement call at `0x664E06`. |

The entry's two predecessors are controlled forms of the same native
transition, not unrelated call paths. Neither site splits an instruction, both
callbacks run before the state mutation they delimit, and both recover the
same unambiguous receiver from the active Tune-run stack frame.

### 2026-08-22 Test Mode termination correction

Runtime evidence disproved the earlier assumption that every active-song exit
reaches `0x664D9A`. Entering Test Mode stopped the active song and reached the
timing-settings UI without a `semantic-stage-end`; the following song entry
then hit the false `SemanticStageAlreadyOpen` fatal for generation 1. Changing
`JudgTimeOffset` was not causal; it merely happened inside that Test Mode visit.

The correction reuses the existing Test Mode main-form constructor inline hook
at RVA `0x173EA0`. At committed Test Mode entry it asks the absolute-judgement
runtime to end the currently open stage using its stored Tune manager. A
normally closed stage is a no-op, so the natural exit and Test Mode exit remain
exactly-once alternatives. The runtime emits one
`semantic-stage-termination source=test_mode_entry` record after the scheduler
has completed its existing end accounting.

### Implemented corrected-scheduler static trace

The 2026-08-22 controller implementation has one immutable stage anchor and
one delivery cursor. The following construction cases were traced directly
through the implemented branches; they are static evidence, not gameplay
acceptance:

- Input published after semantic entry but before BGM `Play` remains in the
  unresolved FIFO. When the first qualifying `Play` binds, its QPC maps through
  the same formula with `O(q) < O0`, producing a signed pre-origin `J` rather
  than deleting the edge.
- A later native `Seek`, `Play`, stop, or natural drain is never consulted by
  `ResolveQpc`; later input and the ready horizon continue from the immutable
  `(O0,S0,Fo,Fs,G)` anchor.
- A ten-boundary hitch caps each outer horizon at exactly three newly
  constructed `index/60` boundaries, yielding heartbeat-only batches of
  `3/3/3/1` without incrementing a rounded duration.
- With 33 simultaneously ready events, `CountResolvedAtOrBefore` marks the
  oldest one state-only and leaves the newest 32 eligible. Conversion advances
  the same delivery cursor used by event commits.
- Accepted-late input is appended with its original exact `J`, consumed
  state-only immediately in sequence, and cannot affect a held-state query
  whose query time precedes that `J`.

### Implemented failure-policy static trace

The 2026-08-22 implementation also closes the audit's false-assertion and
invisible-fatal defects. This is source/build evidence, not gameplay
acceptance:

- Every terminating emitter now constructs an
  `AbsoluteJudgementFatalPredicate`; broad reason enums are secondary metadata
  and cannot independently terminate the process.
- The predicate descriptor coverage scan matched all 75 terminating enum
  values plus the `None` sentinel. Each has a stable name, failed-expression
  text, and fixed operand labels; history retains the actual failed operands
  before returning an error.
- Native recognition without its score call, an incomplete outstanding scope,
  delivery/commit contradictions, unsupported zero-only configuration,
  endpoint/input continuity loss, and fixed-capacity/core-rational exhaustion
  remain fatal under their exact predicates.
- Score/transient diagnostic reads, score regression/deltas, query/stat
  counter overflow, delivery-delay conversion, diagnostic subtraction, and
  final transport accounting are nonfatal observations.
- The first active fatal uses fixed `std::format_to_n` storage, flushes the
  exact record and snapshot, shows an untimed `MessageBoxW` containing the
  predicate ID, then terminates. The previous repeated-fatal infinite wait is
  gone, and every remaining abort is only the logged fallback after an
  unexpected `TerminateProcess` return.
- C++ `try`/`catch` recovery was removed from both the active hook and the
  1000 Hz input runtime. Only native-memory SEH guards remain.
- The corrected `msvc32-debug` `iDmacDrv32` target linked successfully. No
  repository test or emulated oracle was created or run.

## Native lifecycle proof and current contradiction

The Tune state machine establishes the following deterministic facts:

| Native state/path | Native behavior | Loader must do |
|---|---|---|
| State 5, `0x6629A0` succeeds | Construct per-player judgement/score ownership, then select state 6 | Do not open a gameplay stage |
| States 6-15 | Load, prepare, and count down | Do not classify transitions as gameplay events |
| States 16/17 shared transition | Frame-zero input setup, BGM Play, select state 18 | Begin one semantic stage before frame-zero capture |
| State 18 normal iteration | Audio sync, input frame progression, `0x6401E0` judgement/score | Loader owns the scheduled judgement calls |
| State 18 end branch | Select state 19; no judgement on that iteration | Close that semantic stage |
| Test Mode main-form construction during state 18 | The game has committed to Test Mode and bypassed the normal state-19 branch | Close the open semantic stage; no-op if already closed |
| `0x662080` | Later native destruction | Destroy/validate storage only |

The current hooks instead call `BeginNativeStage` after successful
`0x6629A0` construction and `EndNativeStage` immediately before `0x662080`
cleanup. The source comments and current spec statements that call these the
native gameplay-stage boundaries are false.

The bad lifecycle was introduced when the ownership proof in E-044 was
misread as a playable-stage-lifetime proof. E-044 proves where judgement and
score objects are constructed and owned. It never proves that construction
means gameplay has started.

## Exact deployed fatal reconstruction

The latest log is a complete counterexample to the deployed assertion:

1. At 04:55:23.776, object construction opens loader generation 1 with cutoff
   sequence 64 and held baseline zero.
2. During the following 2.208 seconds of loading/countdown, sequence 64 is a
   rise and sequence 65 is its fall.
3. The loader drains both records. Because neither lies inside the first BGM
   buffer epoch, it converts both to its `OutsidePlayback` baseline-only form.
   History-next becomes 66 while delivery-next incorrectly remains 64.
4. At 04:55:25.984, the first BGM origin activates the scheduler at `J=0`.
5. The first heartbeat commits and prunes the two baseline-only entries.
   History-base becomes 66; delivery-next is still 64.
6. The next outer call asks
   `CountResolvedAtOrBefore(first_sequence=64, ready=...)`. The deployed code
   rejects `64 < history_base 66` as `HistoryLost` and publishes
   `retained_history_lost` at 04:55:25.997.

The log independently confirms every element: two drained records, one rise,
one fall, two `outside_playback_baseline_records`, zero event scopes, one
heartbeat scope, history last sequence 66, and no pending work.

This is a normal loader-created state. Pruned baseline-only records are, by
definition, no longer deliverable events. A delivery cursor may therefore lag
the retained-history base. The deployed lower-bound predicate is false.

The worktree's later one-line removal of that lower-bound predicate is a valid
local correction to this one lookup contract. It was not in the deployed DLL,
has not been built or run, and is not a complete fix.

`PruneBefore` is not a second copy of this same counterexample. The current
heartbeat passes `CurrentHistoryPrefixEnd()` (66 in this trace), so its own
`prefix_end < base` check is not what fired.

## Why deleting the assertion cannot fix early sound

The two sequence records were not merely pruned. Before the false assertion,
the scheduler had already changed them from gameplay events into baseline-only
state. Baseline-only records never create an event scope, so native recognition
never sees their pressed/released edge and the original tail cannot publish the
free-input/key sound for the press.

This is the deterministic song-start sound-loss path:

- stage input after semantic stage entry but before the first rendered BGM
  origin is valid gameplay input;
- its absolute stage coordinate is earlier than that origin, not outside the
  stage;
- the current resolver calls it `OutsidePlayback`; and
- the scheduler deletes its edge while retaining only the final held mask.

The same semantic loss occurs after a BGM epoch closes while state 18 is still
active. The current code freezes at the closed audio frontier and turns later
input into baseline-only state even though native gameplay has not exited.

The latest run cannot validate later hidden-note sound because it reached zero
event scopes before terminating. It therefore neither proves nor disproves the
current one-event publication-isolation change. Static native evidence does
prove that chart `HIDDEN/HIDDEN2` notes and post-descriptor free input are
different paths; this audit does not collapse them into one "ad-lib" path.

## Correct absolute-time contract

The native game has a monotonic gameplay timeline. Tune/frame time is the
master. The native/high-FPS watchdog computes expected BGM milliseconds from
Tune's frame coordinate, subtracts `GameTimeOffset`, and seeks audio to that
expected position when correction is required. An audio seek does not move the
game timeline backward.

The loader therefore needs one exact stage anchor, not a new judgement clock
for every Play/Seek epoch. With:

- first selected BGM output origin `O0`;
- its source origin `S0`;
- source rate `Fs`;
- endpoint output rate `Fo`; and
- stage-start `GameTimeOffset` `G`;

the continuous coordinate is:

`J(O) = S0/Fs + G + (O - O0)/Fo`

`O` is the exact endpoint output coordinate corresponding to the input QPC.
`O < O0` is valid and produces a signed pre-origin coordinate. Later Play and
Seek epochs may be checked and logged as audio diagnostics, but they must not
redefine `J`, move the ready frontier backward, or make stage input disappear.

This contract also continues through the short natural-audio tail until the
native state-18 exit transition. There is no playback-derived input freeze.

All arithmetic remains rational and every `n/60` heartbeat is constructed from
its integer index. No value is advanced by repeatedly adding a rounded render
step. Thus 144, 165, 240, and other render rates do not accumulate scheduling
rounding error.

The existing framerate patch at `0x664DB2` is independent. The absolute-input
patch must not hook, rewrite, or make lifecycle decisions for that high-FPS
hook. Normal audio resync behavior may expose an input-scheduler defect; that
does not make the audio backend or high-FPS hook faulty.

## Normal practical states currently mishandled

These are the concrete "valid and will happen" cases, not speculative
corruption scenarios:

| Case | Why it is normal | Current result |
|---|---|---|
| Input during loading/countdown after object construction | Construction precedes gameplay by seconds; the latest log contains a real rise/fall | Misclassified as stage input, then edge-stripped, then false-fatal |
| Input after stage entry but before the first rendered BGM origin | BGM Play and physical presentation are not instantaneous; rapid/free input can occur in this interval | Called `OutsidePlayback`; no event scope and no hit sound |
| Native BGM resync Seek | `0x640070` is a normal watchdog and seeks audio to monotonic Tune time | Source-epoch `J` can jump/recede, causing `BackwardTime`, `ClockDiscontinuous`, mapping conflict, or ready-set shrink |
| End of a BGM epoch before state-18 exit | Audio and gameplay-state exit are distinct operations | Ready time freezes and remaining state-18 input is edge-stripped |
| A later song/retry | The state machine repeats semantic entry and exit | Current lifetime is tied to construction/cleanup rather than every transition |

## Deterministic implementation defects

The following results are closed by current source plus native evidence. They
do not need another design vote.

### D-01 — wrong lifecycle boundary

Predicate/behavior: successful `0x6629A0` construction opens gameplay; entry to
`0x662080` cleanup closes gameplay.

Classification: `DEFECT`.

Correction: construction may allocate/arm storage only. Begin at every shared
states-16/17 stage-entry transition before frame-zero input capture. End at
every natural state-18-to-19 exit transition and at committed Test Mode entry
when a stage remains open. Cleanup only validates/destroys inactive storage.

### D-02 — silent stock fallback at the judgement loop

Predicate/behavior: `HookLoopGuard` returns when the loader stage is not open,
leaving the stock loop to run.

Classification: `DEFECT`.

The audited native caller reaches this hook from active state 18. Once the
feature is enabled, reaching it without the matching semantic loader stage is
a lifecycle failure, not permission to mix native frame judgement with
absolute judgement. It must produce one exact fatal predicate. Query hooks may
still trampoline when there is no thread-local loader scope because native
callers outside loader-owned recognition remain native-owned.

### D-03 — per-Play/Seek judgement remapping

Predicates/behaviors include:

- `last_J > current_J -> BackwardTime`;
- `ready < committed_frontier -> ClockDiscontinuous`;
- overlapping BGM histories must produce identical source-derived `J`;
- `marked_overload_count > newly_required_marked`; and
- retained playback-history prefix eviction kills judgement after activation.

Classification: `DEFECT` as a family.

Normal audio correction is allowed to seek. It cannot change the monotonic
stage coordinate. Bind the first exact origin once and stop consulting later
source epochs for judgement scheduling. The overload predicate becomes a real
monotonic scheduler invariant only after ready time itself is monotonic.

### D-04 — playback-gap edge deletion

Predicate/behavior: `ResolveHistoricalQpc == OutsidePlayback` calls
`ApplyBaselineOnly` and never creates an event scope.

Classification: `DEFECT`.

There is no such semantic input category inside the stage. Pre-origin input has
a signed coordinate; post-epoch input continues on the stage anchor until
stage exit.

### D-05 — pruned baseline versus delivery cursor

Deployed predicate: `first_sequence < base_next_sequence_` in
`CountResolvedAtOrBefore` means history loss.

Classification: `DEFECT`.

Baseline-only entries are not deliverable. Pruning them may advance history
base past delivery-next. The unbuilt deletion corrects this lookup but must not
be mistaken for the lifecycle/sound correction.

### D-06 — baseline-only records are not time-causal

Predicates/behaviors:

- `StateAt` applies every `resolved == false` record solely by sequence even
  when its transition occurs after `query_time`; and
- `PruneBefore` folds those records into the causal base without respecting
  their actual event coordinate.

Classification: `DEFECT` that can change judgement.

Example: a release at `J=2.000` is dropped as overload or accepted-late state.
At a later event scope `J=2.010`, a direction query for the prior authored
frame asks about approximately `J=1.993`. The current code applies the dropped
release anyway and reports not-held 7 ms before the release happened.

Every post-entry transition that is kept as state-only still needs its exact
coordinate. Overload conversion already retains that coordinate in storage;
the query code currently ignores it. Accepted-late conversion must retain it
as well.

### D-07 — cross-buffer playback equality as a judgement invariant

Predicate: every pair of observed stage-BGM histories must agree on source
mapping wherever their output ranges overlap.

Classification: `DEFECT` for judgement. Native selects a group/channel; it
does not require all old and new BGM buffer histories to define one eternal
source timeline. After the one stage anchor is bound, cross-history comparison
may be diagnostic only.

### D-08 — live `GameTimeOffset` change is falsely fatal

Predicate: `native.game_time_offset_ms != bound.game_time_offset_ms` causes
`GameTimeOffsetChanged`.

Classification: `DEFECT`.

Native Tune time does not rewind when this setting changes; the audio watchdog
adjusts audio against Tune. The loader should retain the stage's anchor offset
for that stage and allow the live native audio path to observe any later
setting change. The next stage binds the then-current value. This does not
alter the explicit zero-only safe-frame decision.

### D-09 — native-neutral control IDs are fatal

Predicates:

- pressed/held/released rejects `control < 0 || control >= 20`;
- held-age rejects `control >= 20`; and
- the history converts `InvalidControl` into a fatal query invariant.

Classification: `DEFECT`.

Native pressed, held, and released return false for an unsupported control ID;
native held-age returns zero. The loader has no explicit decision to reject
these IDs, so it must return the same neutral values.

### D-10 — direction selector is confused with device identity

Predicate: `selector < 0 || selector > 2` is fatal before native-compatible
output initialization.

Classification: `DEFECT`.

There is one fixed CBooster device. The argument currently named `booster` is
a direction selector:

- `0`: left component controls 0-3, vertical priority up/down and horizontal
  priority left/right;
- `1`: right component controls 5-8 with the same priority;
- `2`: combined controls 10-13, where opposite directions add and cancel.

For valid selectors, the current vector and return-bit implementation matches
native: selector 2's return is specifically the control-13/right held result.
For another selector value, native first writes `x=0` and `y=0`, performs no
direction selection, and returns the x-pointer value left in EAX by the helper.
The loader must mimic that x86 ABI result. Null x/y pointers are different:
native dereferences them immediately, so they remain a real call-contract
failure rather than a neutral selector case.

### D-11 — pressed and released were incorrectly treated as one frame-domain claim

The predicates are identical in source, but their native caller evidence is
not.

- Every exhaustively recorded pressed call inside the scoped recognition graph
  pushes judgement-state `+0xA0`, the current recognition frame. For pressed,
  `requested_frame == active.native_frame` is a
  `PROVEN_INTERNAL_INVARIANT` of this local scope and may remain, provided both
  values and the exact call predicate are logged.
- The same exhaustive scoped proof does not exist for released. Native
  `0x62DD30` accepts an arbitrary retained frame. Treating a non-current release
  frame as corruption is `UNPROVEN`, not fatal-eligible.

Outside a thread-local loader scope, both hooks already trampoline to native,
so the generic helper ABI remains native-owned. The release-frame
materialization decision is closed below.

### D-12 — unrelated-thread native queries are made fatal

Predicate: thread-local scope is null but global
`g_active_scope_thread != 0`.

Classification: `DEFECT`.

A query on another thread has no local loader scope to observe. It should use
the native trampoline just like any other out-of-scope query. Inside the local
game-thread scope, thread identity, generation, and the one fixed receiver are
real scope invariants.

### D-13 — observations terminate gameplay

Classification: `DIAGNOSTIC_ONLY`, currently implemented as `DEFECT`.

The following cannot stop recognition or score:

- query/result counter overflow;
- batch/statistics counter overflow;
- score-counter regression or score-counter read failure;
- transient arrange/free-tap publication read failure;
- transient publication count overflow;
- delivery-delay diagnostic conversion failure;
- playback Play/Seek diagnostic count overflow;
- final diagnostic accounting mismatch; and
- diagnostic monotonic subtraction failure.

They must saturate, become unavailable/invalid, and log the exact diagnostic
problem. Score state and transient publications are observations here; native
recognition/score has already established the actual game result.

### D-14 — C++ exception handling in the active hook

Behavior: `HookLoopGuard` catches `std::bad_alloc` and `...`, then converts
those exceptions into loader fatal reasons.

Classification: `DEFECT` against the patch's implementation contract.

Dynamic allocation is not itself prohibited. The current vectors are
RAII-owned scheduler members: stage reset clears their elements and releases
owned `shared_ptr` references, retained capacity is reusable rather than
leaked, and the process-static runtime destroys the vectors at process exit.
They may remain unless a separate measured allocation or growth problem is
found.

The explicit C++ catches must be removed. The patch has no supported recovery
or fallback behavior for exceptions. If a standard-library allocation or some
other operation actually throws through the `noexcept` hook, normal C++
termination and the existing crash-dump path are the failure surface; the
patch must not invent `StorageAllocationFailure` or
`UnexpectedInternalException` control flow. SEH-guarded native memory reads
remain a separate ABI-safety mechanism.

### D-15 — fatal reasons hide the failed predicate

Behavior: many unrelated Boolean expressions collapse into reasons such as
`NativeStateMismatch`, `RetainedHistoryLost`, or
`CheckedArithmeticFailure`.

Classification: `DEFECT`.

Before any supported-mode hard stop, the log must contain a stable predicate
name and all operands needed to reproduce it. The broad reason may remain as a
secondary category only.

The active-stage path currently logs and flushes, then calls
`TerminateProcess(0xA7)`. It never invokes the startup-fatal UI, which is the
exact reason the game exited without a prompt. A visible no-timeout fatal
surface is required; the implementation mechanism belongs in the later plan.

### D-16 — cleanup accounting is attached to object destruction

Behavior: transport identity, `post_cutoff_records`, and cleanup drops are
finalized at `0x662080`.

Classification: `DEFECT`.

Those figures currently include loading/countdown and post-gameplay object
lifetime. Stage accounting must close at the semantic exit transition. Object
destruction may only confirm that no semantic stage remains open.

### D-17 — the first retained buffer epoch is mistaken for this stage's origin

Behavior: `FindFirstPlaybackOrigin` examines each observed buffer history and
selects `scratch[0]`, the earliest retained epoch in that buffer object. There
is no semantic-stage-entry playback watermark.

Classification: `DEFECT`.

A buffer/history may survive a retry or be reused across a later semantic stage.
The earliest retained Play is then not the current stage's Play. Entry must
capture a QPC/exact-output watermark together with the input cutoff, before the
native BGM Play in the transition block. Activation binds the first selected
BGM Play origin after that watermark. No timeout is used while waiting for that
origin.

## Native query compatibility table

| Query | Native domain/result | Current loader | Audit result |
|---|---|---|---|
| Pressed, controls 0-9 | Ordinary edge at requested frame | Event edge at current synthetic frame | All scoped recognition callsites use current frame; retain exact invariant |
| Pressed, 10-14 | OR of paired ordinary sides | Matches for current event | Retain |
| Pressed, 15-19 | Both sides current, or one current plus the other's inclusive prior four frames | Matches approved exact-time prior-four-quantum policy | Retain |
| Pressed, other ID | False | Fatal | Fix to false |
| Held, 0-9/10-14/15-19 | Ordinary / OR / AND at requested ring frame | Exact translation supports the known current and current-minus-two callsites | Retain known scoped domain; an older retained-floor failure is not native corruption |
| Held, other ID | False | Fatal | Fix to false |
| Released, valid IDs | Symmetric native edge algebra | Matches for current event | Retain current-event isolation and support non-current retained-frame queries through exact translated history |
| Released, other ID | False | Fatal | Fix to false |
| Held age, valid IDs | Native consecutive-frame count; only judgement consumer is direction/hold progression | Approved continuous-age threshold model | Retain unless a concrete consumer counterexample is found |
| Held age, other ID | Zero | Fatal | Fix to zero |
| Direction selector 0/1/2 | Left/right/combined at requested ring frame as detailed above | Valid values and the known current-frame callsite match | Retain known scoped domain; an older retained-floor failure is not native corruption |
| Direction other selector | Zero x/y, helper's x-pointer ABI return | Fatal | Mimic native |
| Direction null x/y | Native invalid dereference contract | Fatal | Exact logged invariant is allowed |

The fixed game has exactly one gameplay CBooster with two physical components.
There is no multi-booster problem to design. Within a local loader scope,
receiver equality is therefore a `PROVEN_INTERNAL_INVARIANT`. Missing that one
device is `EXPLICITLY_UNSUPPORTED` by user decision even though native helper
functions often return neutral values when the object is not ready.

The endpoint-provider pointer check is also not an open equality question.
`AcquireExactWasapiClock` returns the provider registered for one generation,
and registration explicitly refuses a different provider with the same
generation. Same generation plus different provider pointer is a proven
registry-invariant failure. A new endpoint generation is a different
capability/lifecycle question.

## Held-age and paired-edge decision

The current continuous held-age model is intentionally not a byte-for-byte
copy of the native vector counter:

- the transition that creates a logical rise reports age 1;
- later scopes report `max(2, 1 + floor((t - rise) * 60))`;
- a held baseline reports stale age 5; and
- paired press/release accepts the other constituent within an inclusive
  prior `4/60` seconds.

This was selected to preserve the native judgement consumer thresholds while
allowing exact event scopes between authored boundaries. In particular, an
unrelated event scope must not replay a direction head as age 1. The only
identified native judgement use of held-age is in direction/hold progression;
ordinary taps, hidden notes, and free input do not use it.

Exact native counter equality is disproven and must not be claimed:

- a control already held at the stage cutoff may have native age 1, 2, 3, 4,
  6, and so on, while the loader deliberately reports stale age 5; and
- a second exact event scope inside the same authored `1/60` interval reports
  loader age 2 even though native's per-frame vector would still be age 1.

Those differences can affect the direction/hold consumer. They were selected
deliberately: pre-stage edges are ignored, and an unrelated exact event must
not replay the same direction head. This is therefore an explicit judgement
policy requiring in-game acceptance, not binary-equivalent emulation and not
the cause of the latest fatal (baseline zero, held-age calls zero).

The paired-edge policy has the same explicit loss boundary. A transition
converted to state-only because of approved overload or accepted-late handling
does not remain a prior-four-quantum paired-edge companion. That is intentional
input dropping. The lifecycle/clock defects are different: they currently
convert normal stage transitions to state-only without authorization. Once
those false conversions are removed, `HasPriorEdge` skipping deliberately
dropped records is consistent with the approved policy.

## Scheduler behavior that remains valid

- Event time and heartbeat time are exact rationals.
- Heartbeats are `index/60`, constructed directly from the integer index.
- One event scope uses one immutable held/edge view across both native booster
  components, descriptor processing, result publication, and post-descriptor
  free input.
- Native pressed queries remain non-consuming; the loader does not claim an
  edge per descriptor.
- One event-bearing scope per outer call preserves the one original tail's
  chance to observe transient hit/free-input publications.
- Heartbeat-only catch-up is capped at three calls per rendered outer call.
- An event due after a heartbeat in the same outer call is deferred to the next
  outer call rather than mixed with that heartbeat batch.
- When more than 32 resolved events are simultaneously ready, the approved
  policy keeps the newest 32 and turns older ready transitions into time-causal
  state-only records.

These statements are static construction results, not in-game acceptance. The
latest run terminated before any event scope, so runtime acceptance remains
pending after implementation.

## Complete hard-stop classification

This section classifies the remaining fatal families that are not defects
above. "May hard-stop" always means exact predicate plus operands are logged
and flushed first. It never means the current broad reason string is enough.

### Explicitly unsupported supported-mode states

| Predicate/family | Classification | Required handling |
|---|---|---|
| `HoldSafeFrame != 0` or `SlideHoldSafeFrame != 0` | `EXPLICITLY_UNSUPPORTED` | Exact values, config address, and stage generation; hard-stop before judgement |
| The fixed input manager or its one CBooster is missing at semantic stage use | `EXPLICITLY_UNSUPPORTED` | Log manager and booster operands; hard-stop |
| 1000 Hz gameplay-transition transport is disabled/inactive at stage entry | `EXPLICITLY_UNSUPPORTED` | Log enabled/active/epoch/frequency; hard-stop |
| Exact WASAPI provider or first exact BGM origin is unavailable | `EXPLICITLY_UNSUPPORTED` while this WASAPI-only mode is selected | Pending/unavailable is allowed without timeout; if semantic stage exits without ever binding, log the final capability state and hard-stop rather than claim successful judgement |
| Input transport epoch changes or worker becomes inactive during a stage | `EXPLICITLY_UNSUPPORTED` because exact input continuity is lost and fallback is forbidden | Log old/new epoch, enabled/active, sequence, and frequency; hard-stop |
| Exact endpoint generation changes during a stage | Current implementation has no continuity bridge; treat as unsupported unless the later design explicitly proves re-anchoring | Log old/new generation and provider state; no mixed fallback |

Zero-only safe frames and mandatory device capability are explicit user
support decisions. They are not claims that the native game would reject all
other values.

`Pending` and `TemporarilyUnavailable` are not failures and must not acquire a
timer. The scheduler retains work until exact data is available or the
deterministic game-state exit occurs.

### Resource limits

| Predicate/family | Classification | Required handling |
|---|---|---|
| Gameplay transition journal eviction changes from the stage baseline | `RESOURCE_LIMIT` / exact input history lost | Log capacity 65,536, baseline/current eviction count, first/next sequence, depth; hard-stop |
| Unresolved queue or retained judgement history reaches 65,536 entries | `RESOURCE_LIMIT` | Log capacity, size, next drain/delivery/history sequence, ready frontier; hard-stop |
| Transition sequence or stage generation reaches `uint64_t` exhaustion | `RESOURCE_LIMIT` | Log counter and subsystem; hard-stop |
| Core exact rational cannot represent event time, boundary, native milliseconds, or native frame | `RESOURCE_LIMIT` for exact scheduling | Log operation and rational operands; hard-stop |

The approved newest-32 policy applies after an event has been timestamped and
is ready. It does not authorize silently relabeling unknown journal eviction as
ordinary overload. Journal eviction loses which transition occurred and cannot
be reconstructed.

### Proven internal invariants

| Predicate family | Proof basis | Classification |
|---|---|---|
| Semantic stage entry while one is already open; stage exit with a mismatched generation/object; object cleanup while a semantic stage remains open | Single game-thread state transition, one active state-18 epoch | `PROVEN_INTERNAL_INVARIANT` after lifecycle is corrected |
| Tune, judgement-state, score-state, player index, and the one booster identity change inside one state-18 epoch | Native Tune/player ownership closed by E-042-E-046 | `PROVEN_INTERNAL_INVARIANT` |
| Input QPC frequency differs from endpoint QPC frequency | Both bind the process/system QPC frequency | `PROVEN_INTERNAL_INVARIANT` |
| Same exact endpoint generation yields a different provider pointer | Exact-WASAPI registry refuses a different provider for an existing generation | `PROVEN_INTERNAL_INVARIANT` |
| Endpoint publication count regresses on the same provider | Atomic provider publication counter is monotonic | `PROVEN_INTERNAL_INVARIANT` |
| A buffer instance ID changes its exact-history object or endpoint generation | Buffer instance/configuration identity contract | `PROVEN_INTERNAL_INVARIANT` before the first anchor; later observations are diagnostic |
| Journal record epoch/sequence/masks do not match the mutex-protected producer algebra | One producer publishes `rising=after & ~before` and `falling=before & ~after` in sequence | `PROVEN_INTERNAL_INVARIANT` |
| Drain returns more than requested, `next_sequence < depth`, depth remains but drain returns zero, or first remaining sequence differs from next drain | One locked ring-buffer drain transaction | `PROVEN_INTERNAL_INVARIANT` |
| History is uninitialized, valid scheduler prefix is greater than history-next, or a retained entry cannot be found at its promised sequence | Scheduler/history construction after D-05 is corrected | `PROVEN_INTERNAL_INVARIANT` |
| A resolved event coordinate regresses after the one continuous stage anchor | Single QPC producer plus monotonic exact output mapping | `PROVEN_INTERNAL_INVARIANT`; log both coordinates and sequences |
| Outstanding scope, commit identity, event count, boundary index, or overload mark violates scheduler construction | One game-thread scheduler transaction | `PROVEN_INTERNAL_INVARIANT` |
| Recognition-call count, score-call count, and committed scope count differ | Each completed loader scope performs exactly one recognition then one score call | `PROVEN_INTERNAL_INVARIANT`; partial native mutation means continuing is unsafe |
| Local scope thread, stage generation, fixed receiver, immutable event data, or destructor ownership differs | One thread-local scope around one native transaction | `PROVEN_INTERNAL_INVARIANT` |
| Direction x/y pointer is null inside a native-compatible call | Native writes both pointers before selector dispatch | `PROVEN_INTERNAL_INVARIANT` for supported native callers |
| Game executable base/tail address arithmetic contradicts preflighted fixed RVAs | Supported-binary signature preflight | `PROVEN_INTERNAL_INVARIANT` |
| `QueryPerformanceCounter` fails after successful frequency setup | Required success-only OS timing primitive | Hard-stop assertion with exact API/result log |

The following superficially similar checks are not in this table:

- `GameTimeOffsetChanged` is D-08, not native identity corruption.
- invalid control/selector values are D-09/D-10, not corrupt callers.
- no thread-local scope on another thread is D-12, not a scope-thread
  violation.
- score/publication/counter observations are diagnostic-only.

## Exact status handling for the clock

The current `ExactClockStatus` enum mixes endpoint capability, buffer playback,
and stage meaning. The audit result for each status is:

| Status/context | Correct meaning for judgement |
|---|---|
| Endpoint `Pending` / `TemporarilyUnavailable` before anchor | Retain all transitions and wait without timeout |
| No selected exact BGM origin yet | Retain all transitions and wait without timeout |
| First exact BGM origin becomes available | Bind the single continuous stage anchor and resolve the retained prefix, including negative/pre-origin coordinates |
| Later buffer `Play` or `Seek` | Audio diagnostic only; do not reset or remap `J` |
| Later buffer `NoPlayback` / closed epoch while state 18 remains active | Continue `J` from the stage anchor; do not freeze or delete input |
| Exact playback-history prefix eviction after anchor | Diagnostic history loss only; the bound judgement anchor remains sufficient |
| Endpoint generation/clock continuity lost | Unsupported exact-clock capability; exact hard-stop, no fallback |
| Core endpoint QPC-to-output mapping is internally discontinuous | Exact hard-stop with provider generation, anchors, QPC, and mapping operands |

There is no judgement status corresponding to `OutsidePlayback` between
semantic stage entry and semantic stage exit.

## Every diagnostic fatal that must become nonfatal

The exact current call sites are grouped here so none can survive under a new
broad name:

| Current condition | Current effect | Required effect |
|---|---|---|
| `RecordBatch`: `batches == UINT64_MAX` | Active fatal | Saturate batch diagnostic and mark overflow |
| Sum of event and heartbeat scopes overflows | `NativeCallCountMismatch` fatal | Counter invalid; the independently tracked semantic calls remain authoritative |
| Query counter addition overflows | `CheckedArithmeticFailure` fatal | Saturate/mark query diagnostics invalid |
| Transient publication count reaches max | `CheckedArithmeticFailure` fatal | Saturate/mark publication diagnostic invalid |
| Play/Seek epoch count or sum overflows | `CheckedArithmeticFailure` fatal | Saturate/mark playback diagnostic invalid |
| Diagnostic history vector and observation vector differ | `NativeStateMismatch` fatal | Disable that playback diagnostic and log mismatch |
| Score field read fails | `NativeStateMismatch` fatal | Mark score diagnostic unavailable; do not stop native result flow |
| Score counter regresses | `ScoreCounterRegression` fatal | Log old/new values and restart/disable the diagnostic baseline |
| Score delta accumulation overflows | `CheckedArithmeticFailure` fatal | Saturate/mark score diagnostic invalid |
| Arrange/free-tap publication field read fails | `NativeStateMismatch` fatal before original tail | Mark transient diagnostic unavailable and still reach the original tail |
| Delivery-delay rational conversion fails | `CheckedArithmeticFailure` fatal | Mark delay diagnostic unavailable |
| Final classified-record arithmetic or equality differs | Transport/checked fatal during cleanup | Log accounting invalid; it cannot retroactively change completed judgement |
| `SubtractMonotonic(value, baseline)` sees `value < baseline` | Raw abort | Mark summary diagnostic invalid and log both values |

Semantic call topology is not merely diagnostic: if recognition ran without
its paired score call, or a scope was committed without both, native state is
partially mutated. Those exact transaction predicates remain hard invariants.

## Raw abort and termination inventory

Every raw termination site in the owned flow is accounted for below.

| Site | Current predicate/behavior | Classification and correction |
|---|---|---|
| `InputPollingRuntime.cpp` | `QueryPerformanceCounter` returns false, then `std::abort()` | Success-only timing assertion; log API, thread, poll state, and result before abort |
| `JudgementHistory::Reset` | baseline contains a bit outside the ten ordinary controls | Proven producer invariant; log baseline and valid mask before hard-stop |
| `JudgementScheduler::UnresolvedFront` | called with unresolved size zero | Proven scheduler invariant; log size/read slot/drain sequence before hard-stop |
| Scope-install rollback CAS | global owner is not the installing thread after failed local validation | Proven scope invariant; emergency-log expected/actual owner before hard-stop |
| Scope destructor local check | TLS scope pointer or current thread differs | Proven scope invariant; log both pointers and thread IDs before hard-stop |
| Scope destructor global CAS | global owner cannot be cleared from installing thread to zero | Proven scope invariant; log expected/actual owner before hard-stop |
| Diagnostic `SubtractMonotonic` | diagnostic value is below diagnostic baseline | Diagnostic-only; remove abort and invalidate/log diagnostic |
| Absolute patch `PublishStartupFatal` returns | the shared startup publisher violated its noreturn contract | Emergency-log `startup_fatal_returned` before abort |
| Install-failure switch falls through/defaults | invalid install-stage enum or a noreturn publisher returned | Log stage/site numeric operands before abort |
| Second active fatal | waits on current process, then fail-fast/abort fallback | Preserve the first exact fatal; emergency-log the second predicate/reason rather than silently waiting with no evidence |
| `TerminateProcess` unexpectedly returns | fail-fast then abort | Emergency-log Win32 result/last-error before fallback |

Startup preflight, hook-create, and hook-enable failures already identify the
native site in their primary startup messages and present a startup dialog.
The raw fallback sites still need their own exact emergency predicate.

No timeout is introduced anywhere in this table. `INFINITE` waiting is not a
substitute for identifying the first failed predicate.

## Fatal reporting contract

Every gameplay hard stop must emit one record with at least:

- stable predicate ID and expression;
- classification (`EXPLICITLY_UNSUPPORTED`, `RESOURCE_LIMIT`, or
  `PROVEN_INTERNAL_INVARIANT`);
- stage generation and native state-transition epoch;
- expected and actual operands;
- input epoch, drain/delivery/history sequences, held mask, and capacity when
  applicable;
- endpoint generation/anchor/QPC operands when applicable;
- scope kind/time/sequence/native ms/native frame when applicable; and
- the broad fatal category only as secondary metadata.

The record must be flushed before a visible, no-timeout fatal prompt and
process termination. No predicate may inherit the word "impossible" merely
because it maps to an enum member that previously used that wording.

## Closed compatibility decision before specification

### Q-01 — resolved: support non-current released queries

Native released accepts any requested ring frame. The native CBooster stores a
finite frame ring and applies its ordinary/composite/paired release algebra to
that slot. The current event scope materializes one exact point transition and
hard-fails unless released requests the synthetic current frame.

Concrete example: suppose native frame 101 contains the release of control 0,
and recognition is now evaluating frame 102. Native
`Released(control=0, frame=101)` reads the retained frame-101 edge and returns
true, while `Released(control=0, frame=102)` returns false. In a loader event
scope whose synthetic current frame is 102, the current implementation instead
classifies the first call as `InvalidFrame` solely because `101 != 102`.

Supporting that native-legal call translates frame 101 to the corresponding
exact `1/60` history interval and asks whether retained absolute history
contains the native logical falling edge in that interval. No currently proven
gameplay callsite makes the frame-101 query; this example describes required
compatibility, not the latest runtime failure.

This is not a current pressed problem: all scoped pressed callsites pass the
current frame. Known held callsites pass current or current-minus-two and are
already handled by exact `1/60` translation; the known direction callsite
passes current. A request older than retained causal history in those translated
queries still must not be mislabeled as corruption, but no supported judgement
callsite requiring such an old query has been found.

The 2026-08-22 decision is:

- support a non-current released frame argument when its finite history window
  is retained;
- compute `T = scope_time + (requested_frame - native_frame) / 60` exactly;
- preserve current-frame event isolation: a query for the active frame observes
  only the immutable current event, never another event from the same authored
  interval;
- for another frame, search retained resolved events in the exact window
  `T - 1/60 < event_time <= T`, bounded by the immutable scope prefix, and
  apply the same ordinary/composite/paired falling-edge algebra;
- keep the selected continuous paired-companion rule: a qualifying constituent
  in the requested window may use the other constituent from its inclusive
  prior `4/60` history;
- when the finite requested window is not retained, return the native-neutral
  false result and record a diagnostic if useful; never classify the query as
  corruption; and
- do not recreate or expose the native CBooster frame ring merely to support
  this query.

The other deterministic requirements remain:

- controls outside 0-19 return the native neutral value; and
- no native fallback may be mixed inside an active loader scope.

This deliberately chooses the small exact-history extension over native
ring-wrap emulation. It reuses the existing relative-frame translation and
retained-edge algebra without adding another input representation.

Endpoint replacement is not a native-input semantic question. The current
scope has no proven continuity bridge, so it remains explicitly unsupported
during a stage unless a future request deliberately expands support. The
recommended simple implementation is exact hard-stop on generation change and
normal rebinding at the next stage entry.

No release-frame, booster-count, side-count, playback-time-rewind, or arbitrary
song-count question remains. The fixed cabinet has one booster/two components;
game time is monotonic; and every semantic stage transition creates a fresh
epoch.

## Required correction order after this audit

No source/spec/plan edit is authorized by this ordering yet. It records the
dependency graph that the later spec must respect:

1. Replace construction/cleanup lifecycle with semantic transition entry/exit.
2. Replace per-Play/Seek mapping with the one continuous exact stage anchor.
3. Remove the `OutsidePlayback` input category and retain signed pre-origin and
   natural-tail input.
4. Make every state-only transition time-causal and then close scheduler cursor
   invariants, including the deployed stale delivery lookup.
5. Restore native-neutral control/selector behavior and implement the resolved
   exact-history support for non-current released queries.
6. Remove the foreign-thread restriction while retaining local fixed-receiver
   scope proof.
7. Separate core invariants/resource limits from diagnostic observations.
8. Remove C++ `try`/`catch` control flow from the active hook. RAII-owned
   dynamic storage may remain; do not add exception recovery or fallback.
9. Implement predicate-level logs and the visible fatal surface.
10. Only then update the spec and implementation plan; only after those are
    reviewed should source implementation resume.

## Exhaustive enum-sink audit

This appendix prevents a future implementation from taking a broad enum name
as proof. Every current sink value is mapped back to the predicate classes
above.

### `JudgementStageError`

| Value | Audit result |
|---|---|
| `AlreadyOpen` | Internal invariant only after semantic transition lifecycle replaces construction lifecycle |
| `GenerationExhausted` | Resource limit |
| `InputCapabilityUnavailable` | Explicit unsupported capability; operands required |
| `NativeIdentityInvalid` | Must be split: Tune/judgement/score structure is native/internal; missing booster is explicitly unsupported; endpoint/QPC fields are loader capability |
| `NativeIdentityChanged` | Stable object/player/booster identity is internal within one corrected state-18 epoch; must log the exact field |
| `EndpointGenerationChanged` | Unsupported exact-clock continuity unless a later design adds a proven bridge |
| `InputGenerationChanged` | No current emitter; if added, exact-input continuity loss is unsupported capability |
| `QpcFrequencyChanged` | Proven system-timebase invariant; exact values required |
| `GameTimeOffsetChanged` | D-08 defect; tolerate without moving this stage's anchor |
| `SafeFrameChanged` | Explicit zero-only unsupported mode; exact two values required |
| `CleanupIdentityChanged` | Current construction-lifecycle use is invalid; corrected inactive object cleanup mismatch is an internal ownership invariant |

### `JudgementHistoryError`

| Value | Audit result |
|---|---|
| `NotInitialized` | Internal construction invariant |
| `TransportEpochMismatch` | Record mismatch is internal; a live transport epoch change is unsupported capability caught at the transport boundary |
| `SequenceDiscontinuity` | FIFO mismatch is internal; numeric exhaustion is a resource limit |
| `TransportStateMismatch` | Mutex-producer mask algebra invariant |
| `BackwardTime` | D-03 under the current source-epoch clock; internal only after one monotonic stage anchor |
| `HistoryLost` | Must be split: D-05 false lower bound, 65,536 capacity resource limit, or a proven scheduler prefix invariant |
| `InvalidControl` | D-09 native-neutral value, never a history fatal |
| `CheckedArithmeticFailure` | Core exact-coordinate representability may hard-stop as a resource limit; diagnostic arithmetic may not |

### `JudgementQueryInvariant`

| Value | Audit result |
|---|---|
| `None` | No failure |
| `ThreadMismatch` | Local TLS owner mismatch is internal; no local scope while another thread owns one is D-12 and must trampoline |
| `ReceiverMismatch` | Internal fixed-one-CBooster scope invariant |
| `StageMismatch` | Internal corrected semantic-stage generation invariant |
| `ScopeLifetimeViolation` | Internal transaction invariant; exact owner/scope operands required |
| `InvalidScope` | Internal immutable-scope construction invariant |
| `InvalidControl` | D-09 native-neutral result |
| `InvalidFrame` | Pressed/current is source-callgraph proven; released/non-current is supported through translated retained history and is not fatal |
| `InvalidDirectionArguments` | Must split null outputs (internal native call contract) from other selector (D-10 native-compatible result) |
| `HistoryLost` | Inherit the exact history predicate; enum alone proves nothing |
| `CheckedArithmeticFailure` | Core query-time arithmetic only; diagnostic arithmetic excluded |
| `HistoryInvariantFailure` | Inherit the exact history predicate; enum alone proves nothing |
| `DiagnosticOverflow` | Diagnostic-only; never fatal |

### `AbsoluteJudgementFatalReason`

| Value/family | Audit result |
|---|---|
| `InputCapabilityUnavailable` | Explicit unsupported input continuity |
| `EndpointCapabilityUnavailable` | Explicit WASAPI exact-clock prerequisite |
| `NativeIdentityChanged` | Split and log exact stable field; GameTimeOffset excluded |
| `EndpointGenerationChanged` | Unsupported continuity, not native corruption |
| `InputGenerationChanged` | Unsupported input continuity, not native corruption |
| `NativeStateMismatch` | Invalid umbrella; all emitters must become named predicates |
| `ClockHistoryLost` | Before anchor: exact capability; after anchor: playback diagnostic only |
| `ClockDiscontinuous` | Current normal-Seek false model (D-03), or exact endpoint continuity predicate after redesign |
| `PlaybackMappingConflict` | D-03/D-07; later playback mappings cannot kill judgement after anchor |
| `BackwardTime` | D-03 now; continuous-anchor/QPC monotonic invariant later |
| `GameTimeOffsetChanged` | D-08 |
| `SafeFrameChanged` | Explicit zero-only unsupported mode |
| `TransportEviction` | Resource limit |
| `TransportSequenceError` | Split FIFO internal invariant from diagnostic final accounting |
| `TransportEpochLost` | Unsupported input continuity or exact startup/exit capability predicate |
| `RetainedHistoryLost` | Split D-05, capacity limit, and internal prefix/queue predicates |
| `CheckedArithmeticFailure` | Split core exact representability from all diagnostic counters/conversions |
| `CommittedOrderViolation` | D-03 if caused by shrinking source-derived ready time; otherwise internal scheduler transaction |
| `HeartbeatFrontierViolation` | Internal scheduler transaction after monotonic clock construction |
| `ScoreCounterRegression` | Diagnostic-only |
| `NativeCallCountMismatch` | Semantic recognition/score transaction invariant; pure counter overflow excluded |
| `ScopeThreadMismatch` | Split local invariant from D-12 foreign-thread native query |
| `ScopeReceiverMismatch` | Internal fixed receiver invariant |
| `ScopeLifetimeViolation` | Internal scheduler/TLS transaction invariant |
| `StorageAllocationFailure` | D-14: remove this loader-defined exception reason and let an actual uncaught allocation failure reach normal C++ termination/crash dumping |
| `UnexpectedInternalException` | D-14: remove this catch-all reason and its exception control flow |

`None` is not a valid termination reason. Any default/fallthrough mapping to a
broad reason is itself an exact internal predicate and must be logged as such.

## Corrections to the replaced audits

The superseded audit set failed for specific reasons that must not be repeated:

- it treated loader ownership of exact-time scheduling as the question, even
  though that ownership is the required goal;
- it audited object ownership without first proving the Tune state transition,
  so it called construction/cleanup a stage lifecycle and missed the actual
  root defect;
- it conflated native-valid values with deliberately unsupported mode values
  for safe frames and missing device capability;
- it invented a multi-booster equality problem in a fixed one-booster game;
- it described normal audio resync as a game-timeline rewind instead of
  recognizing that audio seeks to monotonic Tune time;
- it grouped invalid selector with null output pointers even though native
  gives them different behavior;
- it grouped current-frame pressed proof with the unproven released-frame
  restriction;
- it sometimes called held-age binary-equivalent and elsewhere called every
  difference a new defect, without respecting the already-selected threshold
  policy; and
- it reported broad fatal enums instead of the Boolean expression and operands
  that actually failed.

Deleting those generated audit files does not delete failed designs, specs,
plans, native evidence, or runtime logs. Those remain historical evidence so a
redesign cannot repeat their mistakes.

## Final self-review and closure

This audit is complete under the evidence boundary stated at the start of this
document:

- every failure value in `JudgementStageError`, `JudgementHistoryError`,
  `JudgementQueryInvariant`, and `AbsoluteJudgementFatalReason` is classified
  above;
- every raw `abort`, `TerminateProcess`, and fail-fast site in the current input
  patch is classified above;
- the NON_STAGE -> STAGE -> NON_STAGE lifecycle is tied to the native Tune
  transition into state 18 and to either natural state-18 exit to state 19 or
  committed Test Mode termination, rather than to object construction and
  later destruction;
- the latest runtime log is reconciled with one complete sequence trace: the
  two pre-stage records were misclassified and deleted, then the stale lower
  cursor produced the reported false `retained_history_lost` fatal;
- the native query compatibility decisions are separated from explicit loader
  support limits, and the fixed one-CBooster/two-side game model is preserved;
- deterministic implementation defects are separated from diagnostic-only
  conditions, explicit unsupported modes, finite resource limits, and actual
  loader invariants;
- static/native proof is kept separate from future build proof and in-game
  acceptance; neither is claimed here; and
- failed generated audit reports were removed, while failed designs, specs,
  plans, native evidence, and runtime logs remain as historical constraints.

No audit question remains before the specification may be corrected. Q-01 is
closed by supporting non-current released queries through the finite retained
exact-history window without recreating the native frame ring. The lifecycle,
clock, pruning, baseline-time, selector/control, foreign-thread, diagnostic,
exception-handling, fallback, and fatal-reporting corrections are deterministic.

No source, specification, implementation plan, build output, test, deployment,
or runtime installation was changed as part of this audit closure.

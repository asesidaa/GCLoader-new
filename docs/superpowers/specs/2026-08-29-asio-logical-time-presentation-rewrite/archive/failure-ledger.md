# ASIO Logical-Time Rewrite Failure Ledger

**Status:** Historical archive; non-normative and excluded from design review

**Purpose:** Preserve every known architecture and specification mistake so a
later rewrite cannot silently reintroduce it under different names.

The raw review findings are intentionally retained even when two reviewers
identified the same root defect. A duplicate finding is independent evidence
that the defect was visible from more than one review angle. Nothing in this
archive is a current requirement. In particular, headings or clauses below
that say "normative", "must", or "never" describe a rejected revision and do
not override the current specification.

Each individual finding records the correction attempted or required at that
revision. A later numbered finding may historically reject an earlier attempt,
but no entry can supersede the current design. Only
`../../2026-08-29-asio-logical-time-presentation-rewrite-design.md` is
normative. Nothing in this archive—including every old gate, prevention
clause, “must”, “never”, “normative”, or supersession statement—has current
design authority.

## Historical architecture mistakes

### H-001: ASIO was allowed to own logical time

- **Mistake:** Callback cadence, sample position, physical-session generation,
  or driver timestamps were allowed to create or revise the game-facing
  playback timeline.
- **Failure:** Judgement changed with the backend and with the song number even
  though authored notes and captured input should meet in one logical domain.
- **Never again:** ASIO is a reader of logical PCM only. No ASIO value is an
  input to logical time, DirectSound cursor state, or judgement.

### H-002: Callback service advanced the mixer

- **Mistake:** ASIO callbacks acquired a logical render lease and called the
  mixer.
- **Failure:** Missing, late, or stopped callbacks also stopped logical source
  progression. Preview playback then repeated old PCM and recovery changed
  game-facing history.
- **Never again:** One permanent logical producer is the only mixer caller.
  Physical callbacks can only read already-published PCM.

### H-003: Physical observations were interpolated into judgement

- **Mistake:** Synthetic anchors, callback interpolation, and physical-to-
  logical attachment were used to estimate judgement time.
- **Failure:** The estimate was stable-looking but belonged to the wrong clock
  and produced backend-dependent offsets and fast/slow bias.
- **Never again:** Judgement resolves captured input through the accepted
  retained logical playback epoch containing `L(q)`. Resampling is permitted
  for audio; judgement interpolation is not.

### H-004: Physical interruption recovery mutated logical stage state

- **Mistake:** Focus/modal interruption, physical teardown, and recovery were
  coupled to logical stage admission, stage exit, and producer ownership.
- **Failure:** Recovery work could freeze logical rendering, alter stage
  sequence, or strand a stage between semantic and physical states.
- **Historical attempted cure:** Physical interruption was limited to physical
  desired state while a loader-owned native modal transaction paused native and
  logical time together.
- **Superseded:** S-169 and S-170 rejected the loader-owned modal clock and its
  window-thread publication. Current gate G-03 has no focus/modal audio or
  logical-time path; this historical cure is not current authority.

### H-005: A physical failure selected another backend or continued silently

- **Mistake:** Failure paths considered WASAPI/DirectSound fallback or allowed
  an invalid committed ASIO session to continue.
- **Failure:** The runtime no longer had one timing/audio contract and could
  present silence or stale data as success.
- **Never again:** There is no fallback. Explicit focus/modal interruption may
  recover with a fresh session; committed-session structural instability is
  fatal unless the sole physical-signal publisher proves that a newer explicit
  interruption generation already invalidated the observed session.
- **Superseded:** Focus/modal generations and classification were deleted.
  Current recovery begins only from an SDK restart notification; structural
  instability is fatal without reconstructing which window event came first.

### H-006: Recovery reused a contaminated physical session

- **Mistake:** A focus-invalid IASIO instance, buffer set, callback route, or
  clock fit could be resumed.
- **Failure:** Old callbacks and stale phase state crossed the recovery
  boundary.
- **Never again:** Every acknowledged focus loss or modal entry destroys the
  old physical session. Recovery creates a fresh driver object, buffers,
  callback attachment, bridge state, and resampler.
- **Superseded:** The fresh-session rule remains, but focus/modal acknowledgement
  does not. Only a real SDK restart request replaces the physical session.

### H-007: Tests copied the implementation contract

- **Mistake:** Fake callbacks, copied policy, or source-shape assertions were
  treated as independent proof.
- **Failure:** Tests passed while the real game, driver, and native lifecycle
  contracts were wrong.
- **Never again:** No new test is authorized without a user-approved production
  boundary and an independent oracle. Static/build proof and runtime acceptance
  remain separate.

### H-008: Static evidence was reported as gameplay evidence

- **Mistake:** Builds, unit tests, and source inspection were allowed to imply
  correct cabinet timing or focus recovery.
- **Failure:** Regressions appeared only in multi-song and focus-transition
  runs.
- **Never again:** Hash-controlled build evidence, instrumented runtime
  evidence, and user-observed gameplay acceptance are reported separately.

### H-009: The spec was patched instead of simplified

- **Mistake:** Each race found in a shared multi-owner protocol was repaired by
  adding another word, slot, helper, watchdog rule, or matrix row.
- **Failure:** The candidate grew to 3,410 lines and still contained mutually
  incompatible linearization, lifetime, deadline, and verification claims.
- **Never again:** A finding that exposes shared ownership requires removing
  that sharing, not another repair state. Document splitting is not a substitute
  for architectural simplification.

### H-010: Focus recovery became a distributed ordering protocol

- **Mistake:** Physical ASIO recovery was made dependent on a dedicated
  `SetWinEventHook` thread, focus and modal generations, mirrored state,
  two-stage barriers, acknowledgement tokens, and event-order reconstruction.
- **Failure:** ASIO lifecycle correctness depended on platform queue ordering
  that was not guaranteed. Every race repair added another synchronization
  mechanism while focus history acquired authority over physical commit/fatal
  decisions.
- **Never again:** The game/window thread publishes only current physical
  eligibility plus one sticky idempotent reconcile request. The ASIO controller
  consumes current state and creates or destroys a disposable session. It never
  replays, orders, or classifies focus-event history.

### H-011: File decomposition preserved the rejected complexity

- **Mistake:** The monolithic design was divided into nineteen mutually
  dependent files while retaining the same mirrors, generations, fences,
  numerical controllers, and lifecycle cross-products.
- **Failure:** The suite looked modular but still required reading the whole
  distributed protocol to understand any transition. Splitting increased the
  contradiction surface without reducing behavior.
- **Never again:** Split only by cohesive ownership. A moderately sized file is
  preferable to several files that recreate one state machine. The current
  suite has six one-way normative subjects and no event-order subsystem.

## Review round 1: blob `74fb029ad6145499b61db5bf14adee572a764707`

The candidate contained 3,154 lines. Aquinas reported 10 findings and Goodall
reported 11 findings.

### R1-A01: Entry both blocked and did not block

- **Mistake:** One section made native entry wait indefinitely for producer
  readiness while other sections prohibited any logical wait edge.
- **Invalid assumption:** A wait can be described as classification without
  changing native-thread behavior.
- **Prevention:** Stage entry never waits for ASIO or the logical producer. The
  producer is permanent and already healthy or the process is failed.

### R1-A02: Expiring readiness authorized a later semantic CAS

- **Mistake:** A 3.25 ms readiness snapshot was carried across a physical RMW
  and a separately scheduled semantic CAS.
- **Invalid assumption:** Rechecking before a CAS prevents descheduling between
  the check and publication.
- **Prevention:** No expiring observation authorizes semantic state. Publication
  itself is the linearization point.

### R1-A03: `entry_pending` silenced a healthy reader and preserved stale phase

- **Mistake:** Healthy callbacks emitted silence without advancing their reader,
  then restored `RunningAudible` after an unbounded classifier delay.
- **Invalid assumption:** A committed physical reader can skip periods and
  resume with unchanged phase.
- **Prevention:** Logical entry never freezes the physical reader. A physical
  discontinuity always creates a new session and attachment.

### R1-A04: Driver `systemTime` was bracketed by later callback-entry QPC reads

- **Mistake:** A timestamp created before callback delivery was asserted to lie
  between host reads taken after delivery; the same callback entry started the
  hardware deadline.
- **Invalid assumption:** Callback delivery has zero or bounded latency without
  evidence.
- **Prevention:** Driver timestamps are mapped only into file 04's conservative
  physical target interval by file 03b's fixed three-sample procedure. Callback
  entry never substitutes for that mapping, no fabricated buffer deadline
  exists, and judgement never consumes either value.

### R1-A05: Resolved cleanup monopolized the only service slot

- **Mistake:** Cleanup evidence had to remain linked while same-stage recovery
  also required the only live service reference.
- **Invalid assumption:** Provenance retention and active operation ownership
  require the same mutable slot.
- **Prevention:** The single physical controller retains ordinary immutable
  diagnostics and starts the next operation only after cleanup; there is no
  shared service-slot protocol.

### R1-A06: Exit could discard a stage-origin event before service linkage

- **Mistake:** Focus/recovery events survived CAS exhaustion but became
  unclassifiable when exit forbade new stage services.
- **Invalid assumption:** An unlinked event will be reconstructed from the
  latest intent.
- **Prevention:** Focus is a sticky level plus loss generation. The controller
  must acknowledge every loss generation; stage exit does not erase it.

### R1-A07: Initial focus loss had no capability-observation path

- **Mistake:** Startup required continued capability measurement after focus
  loss, while token mismatch retired the only callback request that could
  provide it.
- **Invalid assumption:** One callback path can simultaneously be canceled and
  remain an observation source.
- **Prevention:** Startup/recovery attempts are controller-owned. A focus change
  cancels the attempt after its current driver call returns; no capability
  result is required from a canceled session.

### R1-A08: Active-stage shutdown had no legal transition

- **Mistake:** Shutdown promised normal cleanup but was absent from the service
  origin set and transition matrix.
- **Invalid assumption:** A special out-of-band event will fit a lifecycle
  protocol without an explicit owner.
- **Prevention:** The controller alone handles sticky shutdown and never starts
  recovery afterward. Logical stage state is not rewritten by shutdown.

### R1-A09: Four service slots did not cover linked plus candidate concurrency

- **Mistake:** The pool counted the installed service but not every concurrent
  private candidate.
- **Invalid assumption:** Lock-free candidate construction is free capacity.
- **Prevention:** Remove shared service pools. Controller operations are serial;
  callback work has one fixed slot per physical buffer.

### R1-A10: The SPSC timing ring had multiple callback producers

- **Mistake:** Overlap was rejected only after callbacks could publish timing
  observations.
- **Invalid assumption:** A later global flight claim retroactively serializes
  earlier writes.
- **Prevention:** A callback publishes per-buffer data only after acquiring that
  buffer's ownership token. Cross-buffer telemetry uses atomic counters or a
  genuinely bounded MPSC channel.

### R1-G01: `entry_pending` could silence a song indefinitely

- **Mistake:** Progress depended on one watchdog with no classification
  deadline.
- **Prevention:** No logical/physical intermediate state requires a helper to
  finish publication. The game thread publishes stage state once.

### R1-G02: The entry matrix could revive a focus-invalid session

- **Mistake:** Background, unacknowledged loss, and stale physical models were
  missing or lower precedence.
- **Prevention:** Every loss generation invalidates the complete old session.
  There is no preserve/resume matrix.

### R1-G03: A losing exit CAS left physical inactive and semantic active

- **Mistake:** Exit had no retry/assist protocol after concurrent intent
  mutation.
- **Prevention:** Logical exit is written only by the single game thread and
  never mutates a physical word.

### R1-G04: `exit_after_qpc` charged post-exit descheduling as an active miss

- **Mistake:** A timestamp sampled after semantic close was treated as evidence
  about the stage interval.
- **Prevention:** The logical close publication is authoritative. Physical work
  observed afterward is non-stage work; no later sample reopens the interval.

### R1-G05: Callback entry plus one period was not the device deadline

- **Mistake:** Delivery lateness had already consumed part of the device period.
- **Prevention:** No callback-entry-plus-period device deadline exists.
  Supported callbacks complete inline before return; committed cadence is a
  separate actual accepted-entry-gap invariant.

### R1-G06: Callback `systemTime` had an unsupported QPC interval

- **Mistake:** Host reads after delivery were used as an intercept oracle.
- **Prevention:** File 03b maps the driver timestamp with three fixed
  `QPC-before, timeGetTime, QPC-after` samples and outward rounding. File 04 may
  use only the resulting conservative interval for physical presentation;
  logical time and judgement never consume it.

### R1-G07: The callback prologue had an unobservable reserved state

- **Mistake:** Count/reserve happened before the monitor-visible deadline.
- **Prevention:** Buffer ownership acquisition is the first operation that can
  access session memory. A process-lifetime route protects the earlier entry
  edge, and teardown nulls the route before reclamation.

### R1-G08: Uncommitted timing misses had contradictory outcomes

- **Mistake:** The same miss was described as retryable, clean failure, and
  terminal.
- **Prevention:** Acquisition failure may retry only before commit. A committed
  structural observation is fatal unless a newer explicit interruption
  generation invalidated that session. Unsafe cleanup is fatal in both cases.

### R1-G09: Logical admission waited on a nonexistent steady-state signal

- **Mistake:** Only bootstrap had a completion signal, yet entry expected every
  dispatch to wake it.
- **Prevention:** Delete logical admission waiting. The permanent producer has
  no stage bootstrap.

### R1-G10: Watchdog activation contradicted itself outside stages

- **Mistake:** Some sections armed physical monitoring whenever callbacks were
  possible; others made the whole watchdog dormant outside stages.
- **Prevention:** Committed physical cadence monitoring is always active.
  Stage state changes neither recovery deadlines nor physical stability rules;
  recovery has no elapsed failure deadline in any game state.

### R1-G11: Runtime acceptance asked the user to prove causality

- **Mistake:** The user was asked to prove that a frame stall was loader-induced.
- **Prevention:** User acceptance records observable behavior only. Attribution
  belongs to hash-controlled profiling and source evidence.

## Review round 2: blob `f498a7fdb4ca83b8008eb74439edc519038b9e9b`

The candidate contained 3,410 lines. Aquinas reported 10 findings and Goodall
reported 7 findings.

### R2-A01: Semantic entry had two incompatible linearization points

- **Mistake:** `Hentry` was the charged time coordinate while a later intent
  CAS was the sole semantic publication; intervening focus events were folded
  into entry classification.
- **Prevention:** The game-thread stage publication is the only entry order.
  Later focus events remain physical events.

### R2-A02: Failed entry publication stranded `entry_pending`

- **Mistake:** Shutdown/fault could prevent publication after the physical word
  had entered a state that only the unpublished stage record could clear.
- **Prevention:** No preparatory physical mutation occurs at logical entry.

### R2-A03: Native exit returned while the logical stage remained open

- **Mistake:** The hook returned after an exit request while a helper still had
  to publish semantic close.
- **Prevention:** The game thread completes logical close before the hook
  returns. Physical observers may catch up later.

### R2-A04: An assistable exit RMW could lose its exact pre-word evidence

- **Mistake:** One helper performed the RMW, another could mutate the word, and
  no helper could recover the first helper's unpublished result.
- **Prevention:** Remove assistable multi-actor lifecycle RMWs. Only the
  controller mutates physical lifecycle.

### R2-A05: Timely evidence could still be overwritten by `Late`

- **Mistake:** `post_qpc` was published before a second result CAS, leaving a
  window where the watchdog could publish failure.
- **Prevention:** A result and its classification are one controller-owned
  transition. Callback telemetry never commits lifecycle success.

### R2-A06: Clock uncertainty could not meet the promised recovery budget

- **Mistake:** Independent 1.25 ms endpoint intervals need about ten seconds to
  prove 500 ppm, while recovery promised completion within five seconds.
- **Prevention:** Do not gate recovery on that estimator. Use the driver rate as
  nominal and a bounded occupancy-controlled ASRC; prove all numerical budgets
  before accepting them.

### R2-A07: Repeated map handoffs were not included in the phase recurrence

- **Mistake:** The proof added one eight-frame impulse but allowed a new impulse
  every second indefinitely.
- **Prevention:** The new bridge has no map handoff. Any repeated disturbance in
  a control law must be part of an inductive invariant, not a one-time term.

### R2-A08: Service capacity omitted hazard-retired slots

- **Mistake:** The pool counted live candidates but not removed slots still
  protected by readers.
- **Prevention:** No hazard-managed service pool exists. Immutable session
  descriptors are controller-owned and reclaimed only after callback rundown.

### R2-A09: Overlapping callbacks could write the same driver buffer

- **Mistake:** A global flight loser cleared a buffer that an older deferred
  worker might still be filling.
- **Prevention:** Ownership is per physical buffer. Failure to acquire means
  latch fault and return without writing that buffer.

### R2-A10: Native intent publication had no bounded losing-CAS outcome

- **Mistake:** Every edge had to win one shared word without dropping, waiting,
  or exposing the losing candidate.
- **Prevention:** File 03's dedicated physical-signal thread is the sole focus
  writer and publishes a level plus loss generation. Shutdown is a separate
  sticky bit. No shared intent CAS is required.

### R2-G01: `Hentry` retroactively started producer limits

- **Mistake:** Dispatches between timestamp capture and stage publication were
  not retained but were later treated as active-stage work.
- **Prevention:** The logical producer is permanently active and has no
  stage-entry bootstrap or retroactive deadline.

### R2-G02: `Hexit` allowed post-exit claims to become stage results

- **Mistake:** Timely/late claims could win after the charged exit coordinate
  but before a later physical RMW.
- **Prevention:** Stage exit does not resolve physical claims. Logical close is
  immediate; physical policy reads the closed state later.

### R2-G03: Buffers could be disposed while an entered callback still used them

- **Mistake:** Teardown waited for a global flight but not every callback that
  could later acquire the route.
- **Prevention:** The callback route is process-lifetime. Teardown publishes a
  null route, waits for every callback that already loaded the old route and
  every callback/copy/slot owner, then performs only the phase-legal driver
  disposal or ASIO-exit-equivalent release.

### R2-G04: A committed driver that stopped callbacks was unmonitored

- **Mistake:** The monitor watched only active callback records and flights.
- **Prevention:** Every accepted committed callback publishes its actual entry
  coordinate. A proven committed cadence gap is classified in every game state
  through the same interruption-generation fence as every structural fault.

### R2-G05: A global flight loser could alias the active buffer

- **Mistake:** Same root as R2-A09, independently found by the verification
  reviewer.
- **Prevention:** Per-buffer ownership; no write on ownership failure.

### R2-G06: Hot/native paths contained unbounded retry loops

- **Mistake:** Lock-free system progress was mistaken for bounded progress of
  an individual callback or hook.
- **Prevention:** Native stage publication is single-writer. Callbacks perform
  one per-buffer acquisition attempt. No hot path retries indefinitely.

### R2-G07: Entry classification required physical reads it prohibited

- **Mistake:** The matrix depended on descriptor freshness even though entry
  claimed no physical read dependency.
- **Prevention:** Logical stage entry reads no physical state and performs no
  physical classification.

## Current suite self-audit corrections

These mistakes were made while replacing the rejected monolith. They were
removed before the suite was frozen for independent review, but they remain
recorded here so a later edit cannot silently restore them.

### S-001: A new global command journal replaced an existing ownership boundary

- **Mistake:** An early draft invented a process-wide logical command journal
  without first resolving the existing `SecondarySoundBuffer` mutex,
  `MixerVoice`, and `AudioCursorTimeline` contracts.
- **Prevention:** File 02 keeps the existing DirectSound control boundary. The
  permanent producer observes that boundary and does not add a second command
  owner.

### S-002: A fabricated copy deadline was treated as a driver contract

- **Mistake:** An early callback draft used callback entry plus one nominal
  period as a copy deadline, even though ASIO does not provide that deadline
  and callback delivery may already be late.
- **Prevention:** Files 05/05a use actual per-buffer ownership and synchronous
  inline completion before callback return. No predecessor or elapsed device
  deadline exists.

### S-003: Callback lifetime and driver exit were combined

- **Mistake:** One draft treated callback rundown, worker drain,
  `disposeBuffers`, and release of the IASIO object as one informal cleanup
  action and assumed a raw pointer reset was equivalent to SDK exit.
- **Prevention:** File 06 specifies the complete order and requires the proven
  ASIO-exit equivalent, including the final driver release.

### S-004: The physical controller was allowed to read stage state

- **Mistake:** A controller draft selected lifecycle transitions using whether
  a stage was active, coupling physical ownership back to gameplay state.
- **Prevention:** File 03a has no stage input. File 07 only observes the
  controller's sticky terminal publication and adds no stage-sensitive
  lifecycle or recovery deadline.

### S-005: Running commitment followed the first audible callback

- **Mistake:** A draft let the first callback become audible before the session
  had one authoritative committed-Running record.
- **Prevention:** File 03a performs silent A/B priming, publishes the sole
  Running commit, and only then permits a callback to open the audible gate.

### S-006: Stage exit raced terminal deadline publication

- **Mistake:** A monitor could publish a terminal stage-recovery failure while
  the native game thread was concurrently closing that stage, allowing a stale
  observation to terminate a later state.
- **Prevention:** No stage recovery deadline or deadline publication exists.
  The native game thread only acquire-checks the controller's already-sticky
  terminal result before recognition and score work.

### S-007: Several domains could write one terminal fault record

- **Mistake:** Callback, controller, teardown, and stage monitor paths were
  drafted as competing writers of one terminal record.
- **Prevention:** Session sources write only assigned local records/bits. File
  03a's controller alone classifies them and publishes the one immutable
  terminal record and sticky ready flag; no stage candidate or coordinator
  exists.

### S-008: Callback-entry QPC was used as the driver timestamp

- **Mistake:** A draft ignored callback `systemTime` and substituted callback
  entry QPC as the physical presentation coordinate.
- **Prevention:** File 04 maps the driver-provided timestamp through a
  conservative QPC/timeGetTime bracket. That coordinate remains physical and
  never enters logical time or judgement.

### S-009: Startup focus loss ignored focus regained during a driver call

- **Mistake:** A draft always converged a focus-invalidated startup to
  Suspended, even when focus had already returned before the blocking driver
  call completed.
- **Prevention:** File 03a completes teardown and then converges from the current
  coherent focus level: background becomes Suspended; foreground begins a fresh
  attempt-zero acquisition.

### S-010: Callback focus validation happened only once

- **Mistake:** A draft checked focus before rendering but could copy audible PCM
  after a concurrent loss-generation change.
- **Prevention:** File 05 performs the bounded initial validation and a final
  focus/mode validation immediately before the owned full-buffer copy; failure
  substitutes silence.

### S-011: Numerical closure was deferred to an implementation plan

- **Mistake:** A draft asserted that later planning would choose lead, capacity,
  uncertainty, and ASRC-control constants, leaving the architecture impossible
  to prove.
- **Prevention:** Files 02a and 04a fix the constants and close the store,
  admission, and phase-control inequalities before implementation planning.

### S-012: Modal-pause history had no bounded ownership or capacity

- **Mistake:** A draft required exact projection across native move/resize pause
  intervals but did not specify who wrote the history, how readers observed it,
  or what prevented overwrite.
- **Prevention:** File 00 defines one window-thread writer, immutable-prefix
  publication, bounded lookup, and a fixed non-wrapping lifetime capacity.

### S-013: PCM-reader registration and reclamation were underspecified

- **Mistake:** A draft allowed physical readers to appear while producer pages
  were reclaimed without an acknowledgement or rundown contract.
- **Prevention:** File 02 has no reader registration or reclamation frontier.
  Its own one-attempt pin gate, exclusive page states, and thirteenth spare page
  make producer progress independent of one physical reader; file 06 separately
  drains callback lifetime before destroying session staging.

### S-014: Physical signals and IASIO lifecycle remained one broad spec

- **Mistake:** Even after splitting the monolith, one file still combined focus
  evidence, fault publications, controller states, driver calls, and recovery.
- **Prevention:** File 03 now owns only bounded physical signals; file 03a owns
  only the IASIO lifecycle and consumes those signals through a one-way edge.

### S-015: Judgement preceded and consumed its playback-history provider

- **Mistake:** Judgement lived in file 01 while its BGM binding was supplied by
  dependent file 02, creating an undeclared reverse edge and conceptual cycle.
- **Prevention:** File 01 owns only logical time/stage, file 02 owns playback
  history, and new file 02b consumes both in one forward-only projection.

### S-016: Priming used a non-stage elapsed-time failure

- **Mistake:** A 250 ms A/B priming timeout could consume retry budget or fail
  startup/menu acquisition even though non-stage acquisition has no deadline.
- **Prevention:** File 03a permits asynchronous Priming without an elapsed
  failure deadline in every game state. File 07 adds no priming timeout.

### S-017: One callback spec owned three different contracts

- **Mistake:** Route/buffer ownership, `directProcess` deferral, and committed
  cadence were combined, hiding state ownership and making review harder.
- **Prevention:** Files 05, 05a, and 05b now own those subjects separately with
  declared one-way dependencies.

### S-018: A deferred worker could start copying after the real boundary

- **Mistake:** Merely detecting predecessor incompletion at the next callback
  did not prevent a queued worker from beginning a late driver-buffer write.
- **Prevention:** Deferred driver-buffer writes are forbidden. Sequential
  `ASIOTrue` time-info and legacy callbacks complete inline; `ASIOFalse` is an
  explicit no-write failure.

### S-019: Teardown could race copy/output-ready after driver stop

- **Mistake:** The controller stopped ASIO before proving deferred copy and
  output-ready activity had ceased, permitting a host driver call after stop.
- **Prevention:** File 06 first closes a one-way session copy gate, drains every
  already-claimed copy/output-ready action, and only then calls stop.

### S-020: Focus-inert faults could consume process-lifetime slots

- **Mistake:** Every local observation was drafted directly into a permanent
  domain candidate slot, so a focus-invalidated old session could prevent a
  later real session fault from being published.
- **Prevention:** File 03a's controller classifies session-local fixed records
  directly. A newer explicit interruption may make only the old observation
  inert; otherwise the same controller publishes the single process-lifetime
  terminal record.

### S-021: A deadline helper recreated the stage-exit race

- **Mistake:** A monitor could publish a deadline fact asynchronously with the
  sole game-thread stage close, requiring another stale-candidate protocol.
- **Prevention:** The helper and stage deadline are both removed. The native
  game thread only observes `terminal_ready` at its already-owned outer call.

### S-022: Modal-journal comparison count was off by one

- **Mistake:** The draft promised at most 16 comparisons for a binary search of
  up to 65,536 entries; the worst-case bound is 17.
- **Prevention:** File 00 states the correct fixed 17-comparison bound.

### S-023: Fault attribution still had competing first writers

- **Mistake:** A session-wide first-fault latch allowed overlapping callbacks
  to compete to become the attributed writer.
- **Prevention:** File 05 assigns one local record/bit to each source; file
  03a's single controller is the sole classifier and physical-candidate writer.

### S-024: Static and gameplay acceptance remained one large spec

- **Mistake:** Build/test policy and deployed runtime/user sessions were mixed,
  obscuring the evidence boundary and keeping another large review unit.
- **Prevention:** File 08 owns static/build/test evidence; file 08a owns only
  deployed runtime instrumentation and user acceptance.

### S-025: Focus word/QPC validation admitted a torn event

- **Mistake:** Reading `word -> QPC -> word` could accept a newly written QPC
  between two reads of the still-old word.
- **Prevention:** File 03 uses a sole-writer odd/even publication sequence for
  coherent word/QPC readers; callbacks still use only two bounded word loads.

### S-026: The pause provider and logical clock depended on each other

- **Mistake:** File 01 declared and consumed `P(H)` while the former file 01a
  depended back on file 01's coordinate declaration.
- **Prevention:** File 00 now owns the independent host-QPC pause journal;
  file 01 has a single forward dependency on that projection.

### S-027: Teardown completion was described as a helper port

- **Mistake:** `Releasing` awaited `teardown_complete` even though the sole
  controller itself executes teardown, recreating helper-dependent state.
- **Prevention:** File 03a treats Releasing as the controller's own transaction;
  file 06 elaborates its steps and result without another transition writer.

### S-028: Fault publication had more domains than semantic owners

- **Mistake:** Separate callback-health, controller, and teardown terminal
  slots preserved classification across several actors and stale-session
  disposition complexity.
- **Prevention:** Local sources only record assigned bits. File 03a's controller
  owns all physical classification and the sole terminal record/publication;
  no stage-owned candidate exists.

### S-029: Driver timestamp conversion hid its uncertainty budget

- **Mistake:** The bridge asserted a two-millisecond target interval without a
  separate mapping algorithm, fixed host-sampling count, timer-period lifetime,
  wrap rule, or room for the QPC bracket beyond multimedia quantization.
- **Prevention:** File 03b now owns a fixed three-sample conservative mapping;
  `U` is 120 frames and file 04a closes the revised 210.56-frame phase bound.

### S-030: A cohesive lifecycle state machine was split for document size

- **Mistake:** Acquisition/commit and recovery/stability/shutdown were placed in
  separate files even though one controller owns all of those transitions.
  That reduced line count while making the actual state machine harder to
  review and easier to contradict.
- **Prevention:** File 03a keeps the complete physical-session lifecycle in one
  moderately sized contract. A separate file is justified only by a genuinely
  independent owner, interface, numerical proof, or acceptance boundary—not by
  an arbitrary size target.

### S-031: The bridge's logical-clock dependency was omitted

- **Mistake:** File 04 directly evaluated `L(H)` but its dependency declaration
  and suite graph named only logical PCM. The same file then mislabeled file 01
  as the owner of file 00's modal-pause journal.
- **Prevention:** File 04 explicitly consumes file 01's read-only logical
  projection and file 00's modal state. The graph contains both edges, while
  authority remains one-way from logical state into physical presentation.

### S-032: The numerical proof hid a direct timestamp-mapping dependency

- **Mistake:** File 04a bounded the interval produced by file 03b but declared
  only file 04 as a dependency, leaving a direct proof input absent from the
  graph.
- **Prevention:** File 04a and the suite graph explicitly include file 03b;
  every numerical premise must name its defining contract directly.

### S-033: Teardown retained a removed stage monitor

- **Mistake:** File 06 still described an independent active-stage monitor
  after file 07 moved the deadline check to the native game thread.
- **Prevention:** Teardown has no stage input at all. No stage deadline,
  asynchronous stage monitor, or helper is permitted.

### S-034: Pre-commit driver events were assigned to the stage policy

- **Mistake:** File 05b said file 07 classified pre-commit driver events even
  though file 03a's controller is the sole physical classifier.
- **Prevention:** Per-source records always flow to file 03a's controller.
  File 07 observes classified outcomes and never classifies driver events.

### S-035: Initial focus and queued event identity were unspecified

- **Mistake:** File 03 named a writer for focus changes but did not define the
  first authoritative level before acquisition. Re-querying the current window
  from a delayed foreground callback could also collapse a queued away/back
  pair and lose the loss edge.
- **Prevention:** The dedicated event thread installs its hook before one
  ready-gated initial query, then processes each event-supplied window identity
  in order. Controller acquisition is impossible before that ready record, and
  publisher failure is terminal rather than a frozen focus value.

### S-036: Retry eligibility was described but not enumerable

- **Mistake:** “Documented transient busy/hardware error” left different
  implementations free to retry incompatible ASIO, COM, or host failures.
- **Prevention:** File 03a contains a closed result table. Only `init == false`,
  four named SDK device-state errors, or named pre-commit driver abort events
  are retryable; every other result has an explicit disposition.

### S-037: Timestamp work preceded session-route acquisition

- **Mistake:** File 05 ordered the legacy driver query and host samples before
  acquiring a non-null session descriptor, so a detached callback could touch
  driver/session state outside rundown.
- **Prevention:** Route entry and session-pointer acquisition come first;
  timestamp work occurs only after indexed buffer ownership and writes directly
  into that owned request.

### S-038: A health monitor was accidentally made a fault classifier

- **Mistake:** File 05 assigned attribution and terminal eligibility to file
  05b's monitor, contradicting the sole classifier in file 03a.
- **Prevention:** Callback/event sources publish only one assigned immutable
  record and bit. The controller alone classifies every physical fault.

### S-039: Legacy `bufferSwitch` had no execution rule

- **Mistake:** The inline/deferred split covered only callbacks carrying
  `directProcess`; legacy `bufferSwitch` has no such argument.
- **Prevention:** File 05a explicitly assigns legacy `bufferSwitch` to the same
  synchronous inline path as supported time-info callbacks. It completes before
  return and has no predecessor boundary.

### S-040: Copy permission did not linearize with teardown closure

- **Mistake:** A worker could read an open gate, lose the race to controller
  closure, and then claim its request permit, while other text claimed that no
  post-close copy could begin.
- **Prevention:** One copy-gate word combines a permanent closed bit and active
  token count. Atomic modification order decides whether a token precedes
  closure, and teardown drains exactly those successful tokens.

### S-041: Cadence ownership introduced an unnecessary helper

- **Mistake:** A separate health monitor owned the committed callback deadline
  even though the Running controller already owns state and classification.
- **Prevention:** File 03a's controller records, polls, and classifies the
  cadence obligation directly; callbacks publish only monotonic entry count.

### S-042: Retry classification was duplicated in the stage policy

- **Mistake:** Files 03a and 07 both listed retryable acquisition outcomes,
  allowing their result sets to drift.
- **Prevention:** File 03a exclusively owns the closed retry table. File 07
  consumes only the controller's already-classified disposition.

### S-043: The stage policy claimed every outcome was a controller action

- **Mistake:** File 07's completion text assigned every policy row to the
  controller even though its deadline candidate is intentionally published by
  the native game thread.
- **Prevention:** Every physical transition and terminal publication belongs to
  the controller. The game thread only observes sticky `terminal_ready` before
  recognition/score work and publishes no physical outcome.

### S-044: Terminal selection had no bounded snapshot linearization

- **Mistake:** Reading candidate slots separately could miss a candidate
  published between the first load and terminal publication while claiming all
  previously visible candidates were ordered by QPC.
- **Prevention:** Candidate slots, their shared mask, and the fatal coordinator
  are removed. The controller directly fills one immutable terminal record and
  release-publishes its sticky ready flag exactly once.

### S-045: A stage deadline could still issue native recognition

- **Mistake:** The game thread published a terminal deadline candidate before
  recognition but the spec did not forbid it from continuing recognition and
  score work in the same or later outer calls.
- **Prevention:** An acquire-observed `terminal_ready` result makes the current
  and every later outer call issue no recognition or score work.

### S-046: Judgement-regression triage excluded logical defects

- **Mistake:** Runtime acceptance listed only presentation or run variance for
  a WASAPI/ASIO judgement difference, even though a logical implementation bug
  could violate the approved equation.
- **Prevention:** Triage includes logical implementation, physical
  presentation, and input/run variance while still forbidding ASIO-derived
  compensation in judgement.

### S-047: Commit QPC was used as publication-order evidence

- **Mistake:** File 07 accepted `commit_qpc <= deadline` as proof that Running
  had been published by the deadline, although the QPC capture could precede
  the release publication and backdate intervening work.
- **Prevention:** There is no stage threshold or deadline snapshot.
  `commit_qpc` is diagnostic only; Running is the exact controller-owned
  `commit_fault_word` CAS and cannot be backdated by that timestamp.

### S-048: The permanent producer wording implied a WASAPI rewrite

- **Mistake:** File 02 said the producer was independent of “every physical
  backend,” although source inspection shows WASAPI and ASIO are selected
  alternative engines and the suite explicitly excludes a WASAPI redesign.
- **Prevention:** The permanent producer is process-lifetime within an
  ASIO-selected runtime and independent of every physical ASIO session.
  WASAPI keeps its existing render caller; fallback/switching is still absent.

### S-049: Component startup order was implicit

- **Mistake:** The logical engine, focus publisher, and controller each had
  local startup text, but no contract prohibited IASIO acquisition before the
  logical service and initial focus level were ready.
- **Prevention:** The suite index fixes one bootstrap order: ready logical
  service; resource-empty controller infrastructure; coherent physical-signal
  publisher/modal sink; game-facing facade; then one activation of asynchronous
  physical acquisition. Recovery introduces no alternate startup path.

### S-050: Driver-fault classification could outrun focus publication

- **Mistake:** The controller could classify a committed driver fault after one
  focus-word load while the causative foreground event was still queued on its
  sole publisher thread.
- **Prevention:** Audible staging closes first; file 00's modal fence orders
  earlier modal entry into file 03; then file 03's sole-publisher barrier
  reconciles focus and returns the immutable interruption generation. No grace
  timeout is treated as interruption evidence.

### S-051: The suite index retained displaced owners

- **Mistake:** After cadence moved to the controller and candidate publication
  gained per-writer ready bits, the README still assigned cadence to file 05b
  and all candidate publication to file 07a.
- **Prevention:** File 05b defines the cadence observation, callbacks publish
  its entry record, and file 03a's controller alone polls/classifies it and
  writes the final terminal record. No file 07a or coordinator exists.

### S-052: Invalid buffer index had no safe fault source

- **Mistake:** Fault bits were assigned to per-buffer owners, but an invalid
  callback index cannot select such an owner without performing the forbidden
  out-of-bounds access.
- **Prevention:** File 05 assigns one unindexed callback-protocol record/bit.
  Bounds failure publishes only that record and touches no buffer slot.

### S-053: Route rundown overclaimed cross-session callback identity

- **Mistake:** The route proof said a delayed old callback would see null, but
  the callback ABI has no session token and that invocation could instead see a
  newly published session after the detached interval.
- **Prevention:** Route null/count protects loader memory only during
  detachment. The old driver's completed SDK exit/release boundary is also
  mandatory before a new session pointer can be published.

### S-054: A pre-publication focus failure targeted a nonexistent controller

- **Mistake:** File 03 said the controller would classify focus-publisher setup
  failure, while the suite bootstrap order starts the controller only after
  that publisher is ready.
- **Prevention:** Pre-sink infrastructure failures abort private ASIO backend
  construction. After the process-lifetime modal sink is published, any
  infrastructure invariant uses startup fail-stop; runtime terminal publication
  is owned only by the activated controller.

### S-055: Controller COM apartment lifetime was omitted

- **Mistake:** Sole IASIO-thread ownership did not state where COM was
  initialized or guarantee same-thread release and balanced uninitialization
  across focus recovery.
- **Prevention:** The controller establishes one STA apartment and timer-period
  contract before service publication, retains both across all sessions, then
  releases all IASIO state, timer ownership, and COM exactly once in order.

### S-056: Asynchronous acquisition could race service publication

- **Mistake:** The controller could begin and terminally fail IASIO acquisition
  immediately after infrastructure-ready but before outer bootstrap published
  the game-facing service, mixing startup and runtime failure domains.
- **Prevention:** The resource-empty controller waits for one sticky activation
  released only after service publication. Recovery never reuses that signal.

### S-057: Terminal infrastructure could start after fault producers

- **Mistake:** Bootstrap activated the controller without first requiring the
  fixed candidate slots and sole fatal coordinator to exist.
- **Prevention:** The controller's sole terminal record/ready flag and all
  controller infrastructure exist while it is resource-empty; service
  publication and acquisition activation occur only afterward.

### S-058: Producer bounds omitted frame-stall mechanisms

- **Mistake:** A four-block dispatch bound alone did not forbid allocation,
  synchronous logging, driver work, priority changes, or holding control locks
  across PCM publication.
- **Prevention:** File 02 fixes preallocation and hot-path exclusions. Runtime
  profiling remains separate evidence for any visible loader-attributed stall.

### S-059: Bridge “logging” could imply callback file I/O

- **Mistake:** File 04 required detailed diagnostics without stating their
  hot-path storage, conflicting with file 05's no-logging callback rule.
- **Prevention:** Callbacks update only preallocated counters/extrema and a
  lossy diagnostic ring. Formatting/file I/O occurs on a non-callback drainer
  and telemetry loss never changes behavior.

### S-060: Initial ASIO buffer clearing had no declared owner

- **Mistake:** File 03a required pre-start buffer clears while file 05 allowed
  writes only through callback-owned slots.
- **Prevention:** The uncommitted attempt route exists before `createBuffers`,
  but `buffers_ready` remains false. After successful creation the controller
  privately writes format-correct silence to both halves, transfers initialized
  slots to `Free`, and only then publishes `buffers_ready` before `start`.

### S-061: The stable entry pointed to the wrong README location

- **Mistake:** The entry referred to bare `README.md`, which resolves beside
  the entry rather than inside the suite directory.
- **Prevention:** The authority pointer names the complete project-relative
  suite README path and link verification is a freeze gate.

### S-062: Integer logical-frame rounding was implicit

- **Mistake:** `L(H)` was exact rational data while
  `CurrentOutputFrame()` returned an integer, but the conversion rule was not
  stated.
- **Prevention:** Integer cursor APIs use `floor(L(H))`; judgement and physical
  target projection retain the exact rational until their own declared
  outward/native conversions.

### S-063: Buffer-dependent latency was queried before buffer creation

- **Mistake:** File 03a placed `getLatencies` in the pre-`createBuffers`
  capability query even though the selected buffer configuration determines
  that value.
- **Prevention:** The controller publishes an uncommitted no-buffer route before
  `createBuffers`, then after successful creation queries/finalizes latency,
  applies admission, and constructs the bridge before `buffers_ready` and
  `start`.

### S-064: Candidate-bit aggregation lacked cumulative visibility

- **Mistake:** Two writers release-ORed one shared readiness word without
  stating how an acquire mask containing both bits made both preceding slot
  records visible.
- **Prevention:** Terminal candidate aggregation is removed. The controller
  directly fills the sole immutable terminal record and release-publishes one
  sticky `terminal_ready` flag.

### S-065: Physical-fault aggregation had the same visibility gap

- **Mistake:** Several callback/event sources could publish distinct fault bits
  into one word without an explicit cumulative rule for the controller's
  multi-record snapshot.
- **Prevention:** Each source uses one `acq_rel fetch_or` of its fixed bit, and
  the controller's acquire mask names the immutable records it may classify.

### S-066: Route and copy-gate memory orders were implicit

- **Mistake:** Counts and gates were atomic but the spec did not state how
  teardown's zero observations acquired preceding callback/worker memory
  accesses.
- **Prevention:** File 05 fixes pointer/RMW/decrement orders; file 06 advances
  only after acquire-observing authoritative callback/copy counts and
  owner/slot free state.

### S-067: Controller seqlock fields could be plain data races

- **Mistake:** File 03a declared a publication sequence without requiring the
  concurrently read controller fields themselves to be atomic under C++.
- **Prevention:** The controller sequence and every covered field are lock-free
  atomics on x86; readers accept one equal-even bounded snapshot only.

### S-068: The final terminal record lacked a publication primitive

- **Mistake:** File 07a named a sole writer and immutable record but did not
  define how other threads observed the fully initialized contents.
- **Prevention:** File 03a's controller alone fills the physical terminal
  record, then release-stores one sticky `terminal_ready` flag; readers acquire
  that flag before reading the record.

### S-069: Physical terminal-pending did not stop recognition

- **Mistake:** File 07 stopped recognition after its own deadline candidate but
  did not explicitly apply the same rule to file 03a's physical candidate.
- **Prevention:** The already-owned pre-recognition outer boundary acquire-
  checks the controller's sticky `terminal_ready` flag and never waits for
  another publication.

### S-070: Focus fencing could leave a faulted session advertised as Running

- **Mistake:** A controller waiting for the focus barrier had no published
  non-Running state or active-stage origin, so a silent faulted session could
  evade the five-second stage bound indefinitely.
- **Prevention:** A structural source bit immediately makes the exact audible
  predicate false, and the controller closes copy permission before either
  classification fence. Reflected `Running` state alone is never health
  evidence; the sole-publisher acknowledgement classifies before teardown. No
  stage origin or elapsed bound exists.

### S-071: Terminal publication created a reverse controller dependency

- **Mistake:** The terminal coordinator was said to publish the final record
  before the controller stopped, so a stage-deadline failure could leave an
  acquisition/retry running and made file 03a depend back on file 07a.
- **Prevention:** File 03a's controller is the sole physical terminal classifier,
  record writer, and ready-flag writer. Removing stage terminal publication and
  the coordinator removes the reverse dependency.

### S-072: Silent priming did not prove the first audible read head

- **Mistake:** A priming callback could align to its own target without
  advancing one period, leaving the first post-commit callback one physical
  period behind.
- **Prevention:** After initial alignment, every valid silent priming callback
  dry-runs exactly one full resampler period and preserves the resulting
  contiguous next-callback read head.

### S-073: Facade publication did not require logical-producer readiness

- **Mistake:** Bootstrap started the permanent producer but could publish the
  facade before its fixed forward lead and contiguous initial pages existed.
- **Prevention:** Private startup waits without a gameplay deadline for one
  immutable producer lead-ready record before any game call can observe the
  service.

### S-074: Focus/fault classification had no exact race linearization

- **Mistake:** “Reload focus and classify” did not say whether a focus event
  racing the terminal candidate won or lost.
- **Prevention:** Before teardown, file 00's modal fence orders into file 03's
  one physical-signal thread, whose later barrier acknowledgement is the single
  classification point. An interruption processed afterward is later and
  cannot revoke the result.

### S-075: Focus could appear to excuse unsafe reclamation

- **Mistake:** A newer loss generation was allowed to make the original fault
  inert without explicitly giving teardown/exit failure higher priority.
- **Prevention:** Unsafe stop, drain, dispose, exit, or release is terminal
  regardless of focus/modal state; only the original safely reclaimed
  observation may be made interruption-inert.

### S-076: Sticky shutdown could still produce a stage deadline candidate

- **Mistake:** The stage formula applied to every foreground non-Running state
  without excluding cleanup-only shutdown.
- **Prevention:** No stage recovery deadline or stage terminal source exists.
  Sticky shutdown remains cleanup-only, while the controller's independent
  `terminal_ready` publication stops recognition after a physical terminal
  result.

### S-077: A retained classification timestamp could reopen a later episode

- **Mistake:** The controller snapshot did not require the structural origin
  to clear after leaving its pending state.
- **Prevention:** The classification-origin timestamp is removed entirely.
  Structural classification is event-generation based and has no elapsed
  stage episode to reopen.

### S-078: Bootstrap health could fail between ready and facade publication

- **Mistake:** Individually successful ready records were not revalidated at
  the final publication boundary.
- **Prevention:** Outer bootstrap performs one final private health/emptiness
  revalidation; failure still unwinds without exposing the facade.

### S-079: A full logical PCM store had no nonblocking outcome

- **Mistake:** Reader-frontier protection forbade overwrite but did not say
  whether the high-priority producer could wait for capacity.
- **Prevention:** Capacity exhaustion is a logical fatal invariant; the
  producer never waits, overwrites reachable PCM, or alters logical time.

### S-080: Output channel format was frozen before buffer creation

- **Mistake:** Acquisition grouped channel-format validation with inquiries
  made before `ASIOCreateBuffers`, contrary to the local SDK host sequence
  where active buffer channel details are obtained afterward.
- **Prevention:** Pre-buffer work selects indices from the channel count;
  channel format and latency are queried and frozen only after successful
  buffer creation.

### S-081: Capability queries could see a null session route

- **Mistake:** Every `asioMessage` selector required a published session even
  though the driver negotiates immutable host capabilities during
  `ASIOCreateBuffers`, before session-route publication.
- **Prevention:** Fixed host-capability selectors are process-lifetime replies
  independent of a session; driver-event selectors still require the routed
  session and controller classification.

### S-082: Priming dry-run conflicted with logical modal pause

- **Mistake:** “Every valid priming callback advances one period” could consume
  PCM while file 00 intentionally froze both logical time and the bridge read
  head.
- **Prevention:** Modal entry invalidates the complete physical session.
  Callbacks still entering during teardown stage silence; modal exit creates a
  fresh bridge whose own priming establishes new A/B evidence.

### S-083: The legacy mixer discontinuity input was left implicit

- **Mistake:** “Render the next contiguous block” did not explicitly forbid an
  implementation from passing ASIO callback/recovery gaps through the existing
  `MixerRenderTimeline::discontinuity_frames` field.
- **Prevention:** The permanent producer always renders its next logical tail
  with zero discontinuity frames; only logical voice-control generations alter
  source mapping.

### S-084: The immutable BGM anchor had no exact writer/selection path

- **Mistake:** “The first producer span binds” could let the producer choose a
  stage BGM or leave two threads competing to publish the anchor.
- **Prevention:** The producer publishes logical playback history only; the
  native game thread performs the accepted scoped group-2 getter and alone
  selects authoritative retained histories. Each input resolves through its
  containing exact `Play`/`Seek` epoch.

### S-085: The suite did not explicitly remove the old multimedia clock

- **Mistake:** A QPC formula in prose still allowed implementation to retain
  `LogicalPresentationClock`, `LogicalMultimediaMilliseconds`, or physical
  provider-info fields as the ASIO judgement source.
- **Prevention:** File 01 rejects those symbols/contracts and requires a tagged
  logical-QPC provider with no ASIO period, latency, timestamp, or sample
  operands.

### S-086: Backend parity wording implied a WASAPI-engine rewrite

- **Mistake:** Saying both backends “reach the same logical provider” conflicted
  with the explicit scope that the working WASAPI render engine remains
  unchanged.
- **Prevention:** WASAPI retains its audited endpoint provider; ASIO uses `L`
  in the same retained-epoch algebra, and parity is checked at absolute
  song/native arguments rather than by sharing the ASIO producer.

### S-087: Modal recovery preserved a physical session unnecessarily

- **Mistake:** An attempted correction froze and later resumed the same ASIO
  bridge/resampler across native move/resize, requiring rollback state and a
  larger PCM-store proof.
- **Prevention:** Modal entry is an explicit interruption generation that
  destroys the old physical session. Modal exit creates a fresh session and
  bridge; no same-session rewind, resampler resume, or store expansion exists.

### S-088: Active-stage recovery time was treated as failure evidence

- **Mistake:** A five-second stage deadline could turn slow but safe physical
  teardown/acquisition into terminal failure, despite the SDK providing no
  safe cancellation and the logical timeline already being independent.
- **Prevention:** Physical convergence is asynchronous with no elapsed failure
  deadline in any game state. Only explicit driver results, exhausted retry,
  committed structural invariants, or unsafe teardown select terminal failure.

### S-089: Deadline machinery created needless terminal ownership

- **Mistake:** A stage deadline required two candidate slots, a shared ready
  mask, and a fatal coordinator, creating reverse dependencies and race rules
  unrelated to ASIO ownership.
- **Prevention:** The physical controller alone owns one immutable terminal
  record and sticky `terminal_ready` publication. Stage code only reads that
  publication before recognition/score work.

### S-090: Structural classification recognized focus but not modal interruption

- **Mistake:** The controller could classify a committed observation as fatal
  from focus generation alone while a racing native move/resize had already
  invalidated the physical session.
- **Prevention:** File 03's one physical-signal thread serializes focus loss and
  file 00 modal-entry notifications. Before teardown, the controller closes
  audio, completes file 00's modal fence and then file 03's classification
  barrier, and classifies from that sole publisher's immutable acknowledgement.

### S-091: Terminal publication waited behind potentially unbounded cleanup

- **Mistake:** The controller's generic terminal procedure completed teardown
  before publishing `terminal_ready`, so a blocked or unsafe driver call could
  leave native recognition running after terminal failure was already known.
- **Prevention:** Once a terminal result is known, the controller closes
  permission, fills the immutable record, and release-publishes
  `terminal_ready` before cleanup-only continuation. Structural observations
  that still require interruption classification are not terminal results yet.

### S-092: A physical reader could still stop the logical producer

- **Mistake:** The PCM store let an ASIO reader frontier pin pages. A stalled
  callback or unbounded teardown could then fill the store and select a logical
  fatal invariant, violating physical/logical independence.
- **Prevention:** Fixed ring slots use atomic page-state/sequence/sample
  snapshots. The producer always reuses slots in logical order; a racing or
  stale physical read rejects its whole staging copy and faults only the
  physical session. No reader registration or producer back-pressure remains.

### S-093: The first overwrite snapshot order admitted an ABA-shaped race

- **Mistake:** Reloading page sequence before page state could accept mixed PCM
  if the writer began during the copy, the reader saw the old sequence, and the
  writer returned state to `Published` before the final state load.
- **Prevention:** Writer entry uses an acquire/release `Writing` exchange and
  release-publishes new sequence before `Published`. The reader validates in
  fixed `state_1, sequence_1, samples, state_2, sequence_2` order and accepts
  only two `Published` states with the same expected nonwrapping sequence.

### S-094: Terminal-ready was checked too late in the native outer path

- **Mistake:** The stage policy required `terminal_ready` before recognition
  and score but still allowed playback-binding or retained-input work earlier
  in the same outer call after terminal failure was known.
- **Prevention:** The acquire-load is the first loader-owned action at every
  existing outer boundary, before playback binding, input dequeue,
  recognition, or score work.

### S-095: Reader registration survived after page pinning was removed

- **Mistake:** Once atomic overwrite-safe page snapshots removed reclamation
  ownership, a producer-acknowledged reader generation remained as needless
  bidirectional physical/logical state.
- **Prevention:** The bridge reads the fixed ring directly by expected logical
  page sequence. Controller single-session ownership and route rundown protect
  bridge lifetime; the logical producer stores no physical reader state.

## 2026-08-30 frozen-suite independent review failures

### S-096: Modal publication could retroactively revise logical time

- **Mistake:** The writer captured `Henter` before publishing the Enter event.
  Another thread could project a later `H` from the old journal, after which
  publication of the earlier Enter timestamp changed `P(H)` retroactively.
- **Prevention:** File 00 uses one atomic RMW projection-admission gate. A reader
  captures `H` before its one admission CAS; a writer orders its transition RMW
  before the event QPC. Whichever RMW wins proves the order, while append-only
  prefixes let an already-admitted reader finish without blocking the writer.

### S-097: One unconditional teardown assumed a fully acquired session

- **Mistake:** The teardown text always closed a session gate, stopped a
  driver, drained a worker, and disposed buffers even when failure could occur
  after activation, init, or an entered-but-failed `createBuffers` call.
- **Prevention:** File 03a records a monotonic resource ledger and file 06
  performs conditional reverse unwind. Stop and individual dispose are called
  only after their success facts; partial create uses the SDK exit boundary.

### S-098: Running commit raced a callback fault publication

- **Mistake:** The controller read health and later published Running, allowing
  a fault to publish between those actions and be misclassified.
- **Prevention:** One `commit_fault_word` modification order contains the exact
  zero-to-`Committed` CAS and every fixed fault-source RMW. Both bits can exist
  only when commit won first; any earlier source bit makes the CAS fail.

### S-099: Attachment readiness confused pre- and post-render heads

- **Mistake:** Priming aligned a read head, consumed a full period, and then
  claimed the resulting head still lay inside the same callback's target
  interval.
- **Prevention:** Files 04/04a name `R_i_pre` and `R_i_post` separately. Ready
  records the validated pre-head and the deliberately one-period-advanced
  next-callback post-head; it makes no false post-head claim.

### S-100: Delayed commit left Priming outside the rate/phase recurrence

- **Mistake:** The closed-loop ASRC recurrence was specified only for Running,
  so arbitrarily delayed commit could let silent dry-runs drift open-loop.
- **Prevention:** Initial alignment and every subsequent valid Priming callback
  run the exact Running recurrence. `attachment_ready` forbids later reset, so
  commit latency adds no unbounded phase term.

### S-101: Forward-lead admission omitted the legal phase envelope

- **Mistake:** The admission inequality counted latency, one period, resampler
  reach, and observation width but omitted `E`; a legal positive phase could
  read beyond immutable future PCM.
- **Prevention:** Admission now includes `+ E`. `Plo/Phi` become 1,024/1,216;
  the period bound remains 582 frames and the complete retained-span proof is
  recomputed.

### S-102: Driver events could be orphaned during callback-table exposure

- **Mistake:** The session route was published after `createBuffers`, although
  that call may already invoke capability and driver-event callbacks.
- **Prevention:** An uncommitted attempt route is published before the callback
  table is exposed. It can record all events but exposes no buffer pointers
  until successful buffer creation publishes `buffers_ready`.

### S-103: Failed ownership consumed a nonexistent request sequence

- **Mistake:** A callback allocated sequence `k` before acquiring its indexed
  buffer, then could fail ownership without completing `k`, leaving the next
  request waiting on a predecessor that never existed.
- **Prevention:** The request/predecessor sequence is removed. Cadence entry is
  published only after successful indexed ownership and remains a separate
  observation with no predecessor or buffer-ownership meaning.

### S-104: Poll time hid the real callback-service gap

- **Mistake:** Controller progress polling reset the cadence baseline to poll
  time, so a long callback gap could be shortened or erased by delayed
  observation.
- **Prevention:** The route entry and immediate QPC capture publish actual
  accepted-entry coordinates under one callback execution owner. The
  controller's bounded zero-active/free-owner snapshot compares `Hobserve`
  directly with the last real entry and never replaces that baseline.

### S-105: Structural terminal classification waited behind teardown

- **Mistake:** Focus/modal classification occurred only after complete teardown;
  a blocked stop or dispose could therefore prevent a known unchanged-tuple
  terminal result from ever reaching `terminal_ready`.
- **Prevention:** The controller closes audio, completes file 00's modal fence
  and file 03's sole-publisher barrier, and classifies before teardown. An
  unchanged interruption generation publishes terminal immediately; a newer
  one permits teardown and convergence, while any teardown failure is a new
  terminal result.

### S-106: Runtime acceptance conflated observation, causality, and absence

- **Mistake:** User-visible behavior, internal generations, loader attribution,
  and absence of fallback were mixed in one checklist; missing fallback logs
  were treated as proof that no source path existed.
- **Prevention:** Files 08/08a separate static/build, instrumented runtime,
  external profiling, and user evidence. No-fallback requires an exhaustive
  static factory/error/call-graph/artifact proof; runtime ID only corroborates.

### S-107: The stable hard condition overstated committed fatality

- **Mistake:** The entry-point condition said every committed instability was
  fatal while the suite correctly allowed a newer explicit focus/modal
  generation to invalidate the observed old session.
- **Prevention:** The hard condition now states that exact exception and no
  other. Elapsed time, stage state, or silence can never excuse instability.

### S-108: Atomic sample snapshots were not valid C++ synchronization

- **Mistake:** Equal state/sequence values around relaxed atomic sample loads
  could still accept mixed page contents; the proposed protocol had no C++
  happens-before proof for one coherent PCM version.
- **Prevention:** Page state now grants exclusive `Writing` or `ReaderOwned`
  access to ordinary PCM. The store's own nonblocking gate permits one reader,
  which pins/copies/releases one page at a time. Thirteen
  slots preserve a 12-page retained window indefinitely even if one old page
  remains pinned. This supersedes S-092/S-093's rejected snapshot prevention.

### S-109: Deferred `ASIOFalse` work could write after its real boundary

- **Mistake:** A worker could claim permission, be preempted, and later resume a
  driver-buffer write after the next callback had exposed that buffer to the
  driver. Expiring only unclaimed work did not revoke the in-progress copy.
- **Prevention:** This version has no deferred worker. It supports sequential
  inline `ASIOTrue` time-info and legacy callbacks; `ASIOFalse` is an explicit
  no-write unsupported failure before commit and a structural fault afterward.
  Exact target-driver runtime evidence must confirm the supported form.

### S-110: Equal acquire loads did not prove modal publication freshness

- **Mistake:** The corrected modal design still claimed that two equal acquire
  loads of an old control word proved a later writer had not already published
  its transition gate. C++ acquire loads do not promise the newest concurrent
  value, so the proof still lacked an ordering operation shared by reader and
  writer.
- **Prevention:** The projection reader and modal writer now participate in one
  atomic gate RMW order. The reader captures `H` before its one admission CAS;
  the writer sets the transition bit before capturing the event QPC. No
  freshness assumption or writer wait remains.

### S-111: The PCM history proof omitted declared interval uncertainty

- **Mistake:** The retained-history equation counted `Phi`, phase `E`, resampler
  reach `Q`, and two pages but omitted the declared `U`-frame physical target
  interval. The bridge also expressed its phase invariant as maximum distance
  to both endpoints even though its controller and numerical proof use distance
  outside the interval.
- **Prevention:** The invariant is uniformly
  `distance_to_interval(R_i, X_i) <= E`, and the retained span is recomputed as
  `Phi + E + U + Q + 2*B = 2,040` frames. It remains below the 2,304-frame
  retained window with a separate thirteenth pin-spare page.

### S-112: Focus and modal classification had no common publication order

- **Mistake:** A focus-thread barrier followed by ordinary focus/modal loads did
  not serialize the modal writer and could not prove that a modal entry which
  caused the old-session observation was included before terminal
  classification.
- **Prevention:** File 03's existing signal thread is the sole writer of a
  combined physical interruption generation. File 00 enqueues modal entry
  before publishing direct `Entering`. Classification closes audio, fences the
  window publisher, then obtains the signal thread's immutable barrier
  acknowledgement before any teardown or terminal decision.

### S-113: Null-route callbacks had no complete behavior contract

- **Mistake:** The route proof said a callback seeing null could not access a
  session, but did not define every callback kind's return, capability reply,
  or diagnostic behavior during the detached-before-exit interval.
- **Prevention:** File 05 now gives every null-route callback a bounded
  process-lifetime-only path. Capability replies remain immutable; other kinds
  increment only fixed diagnostics, touch no session/driver memory, make no
  driver call, and return the SDK-required value.

### S-114: Modal pause could replay accumulated input at one coordinate

- **Mistake:** Pausing `L` during native move/resize did not classify input
  captured inside the pause. A high-rate input history could later deliver all
  such edges at the single clamped logical coordinate after the game resumed.
- **Prevention:** File 00 projects exact historical modal membership. File 02b
  consumes records inside `[Henter, Hexit)` only through the accepted
  baseline-held update path, with no recognition/score/fresh edge. Focus loss
  never selects this modal-only rule.

### S-115: The ASRC proof treated phase and correction as independently legal

- **Mistake:** The prose claimed its slew term covered an arbitrary legal prior
  correction, even though a phase near one envelope edge paired with the
  opposite saturated correction is not a reachable healthy state and can exceed
  the stated one-transition bound.
- **Prevention:** A fresh bridge starts inside its target interval with exactly
  zero correction. The proof applies only to states reachable through the fixed
  recurrence and requires an independent interval-arithmetic certificate for
  all allowed disturbance reversals; arbitrary uncorrelated state pairs are not
  admitted.

### S-116: A singleton stage anchor discarded logical playback epochs

- **Mistake:** File 02b froze one `(O0,S0,Fs)` anchor for the entire stage and
  said later `Play`/`Seek` could not replace it. That contradicted the accepted
  absolute-judgement authority, where logical `Play`/`Seek` source epochs are
  retained and an input resolves through the epoch containing its exact output
  coordinate.
- **Prevention:** File 02b now retains the audited playback-epoch and native
  group-2 binding model and substitutes only `O(q)=L(q)` with output rate `Fl`
  for ASIO-selected runtime. Logical DirectSound control owns epochs; physical
  ASIO sessions can never create, select, close, or revise one.

### S-117: The logical producer had no explicit exact-epoch publication port

- **Mistake:** After restoring retained `Play`/`Seek` resolution in file 02b,
  file 02 still described only ordinary cursor spans and PCM pages, leaving the
  accepted exact 256-entry per-voice history without an ASIO-selected writer or
  lifetime boundary.
- **Prevention:** File 02 now makes the permanent logical producer the sole
  ordinary writer of the accepted scalar-atomic exact epoch history and retains
  only the audited writer-quiesced release handoff. Physical state cannot own,
  truncate, or republish it.

### S-118: Modal sink publication preceded fallible startup work

- **Mistake:** File 03 exposed its process-lifetime modal notification sink and
  then still allowed setup to fail, while the bootstrap text promised to unwind
  every private component. Retracting a window-thread-visible sink had no
  rundown contract.
- **Prevention:** The resource-empty controller initializes before file 03.
  File 03 completes every fallible queue/hook/query step before publishing the
  sink; after publication only infallible reconciliation and ready publication
  remain. A later invariant fail-stops and never attempts route retraction.

### S-119: The phase controller named an unsigned distance as signed error

- **Mistake:** File 04 said phase error was signed but defined it only as
  `distance_to_interval`, which is nonnegative. That left the correction
  direction ambiguous and permitted an implementation to increase consumption
  when the read head was already ahead.
- **Prevention:** File 04 defines the directed piecewise interval error: positive
  behind, zero inside, and negative ahead. Its absolute value is the declared
  `distance_to_interval <= E` invariant used by files 02a and 04a.

### S-120: Failed stop did not constrain later disposal

- **Mistake:** File 06 said a failed `IASIO::stop` could continue with only safe
  cleanup, but its next unconditional resource-fact rule still authorized an
  individual `disposeBuffers` call merely because buffers had once been
  created. A failed stop does not prove that disposal is safe.
- **Prevention:** Failed stop publishes terminal, closes and drains only host
  access, skips individual disposal, and proceeds solely to the local SDK's
  ASIO-exit-equivalent COM release. That documented boundary implies
  stop/dispose and no later callbacks; its return is required before host
  attempt memory can be reclaimed, and recovery remains forbidden.

### S-121: Historical prevention text retained rejected deadline ownership

- **Mistake:** Several self-audit entries preserved prevention clauses from the
  removed five-second stage-deadline, candidate-slot, and fatal-coordinator
  design even after later findings rejected all three. Although the ledger
  allowed later findings to supersede earlier attempts, those clauses still
  contradicted the current suite and made the durable rule unclear.
- **Prevention:** Historical mistakes remain intact, but their prevention
  clauses now name the current one-controller terminal publication, no stage
  recovery deadline, and no stage candidate/coordinator. Future ledger edits
  must be checked against the final non-recurrence gates as well as later
  findings.

### S-122: Final commit could outrun queued focus loss

- **Mistake:** Final Running admission used an ordinary coherent focus snapshot.
  A real foreground loss could already have occurred while its out-of-context
  WinEvent notification was still queued, allowing the old attempt to commit
  against stale eligibility.
- **Prevention:** Final commit first completes file 00's modal fence and file
  03's sole-publisher synchronization barrier, including current-foreground
  reconciliation. Only the same eligible interruption tuple may proceed to the
  later exact-zero `commit_fault_word` CAS; a newer/ineligible acknowledgement
  tears down the attempt and converges from current state.

### S-123: Uncommitted terminal publication could outrun interruption evidence

- **Mistake:** Deterministic pre-commit failures and retry exhaustion could
  publish the sticky terminal result without the committed-fault classification
  fence. A focus loss or modal entry during the driver call could therefore be
  queued but not yet visible, turning a recoverable invalidated attempt into a
  process failure.
- **Prevention:** Every terminal-eligible uncommitted result uses the same modal
  fence and sole-publisher acknowledgement as final commit. A newer
  interruption discards only that old result and restarts convergence after
  safe teardown; unchanged eligible state permits terminal. Unsafe teardown or
  synchronization-infrastructure failure remains immediately terminal.

### S-124: The WinEvent barrier assumed a nonexistent cross-queue order

- **Mistake:** The proposed controller posted a barrier message to the
  foreground WinEvent thread and treated its acknowledgement as proof that all
  earlier foreground changes had already been delivered. Windows documents
  sequential out-of-context WinEvent delivery on the installing thread, but it
  does not establish the required order between pending WinEvents and an
  independently posted thread message. Posted messages can therefore outrun the
  evidence the barrier claimed to include.
- **Prevention:** Delete the WinEvent thread and every barrier built around it.
  The game/window thread publishes only the current two-bit presentation state
  and sets one sticky reconcile request. The controller coalesces requests,
  discards the old physical session, and converges from current state. No
  irreversible decision is excused by platform-event timing. This finding
  explicitly supersedes the prevention mechanisms recorded in S-112, S-118,
  S-122, and S-123.

### S-125: A nominal rate ratio cannot hold two independent clocks together

- **Mistake:** The simplified rewrite aligned once and then consumed at the
  declared `Fl/Fd` ratio forever. A normal hardware-clock error therefore
  accumulates directly into audible phase; even 50 ppm becomes about 9 ms in a
  three-minute song.
- **Prevention:** Logical time remains QPC-owned, but physical presentation uses
  one callback-local asynchronous sample-rate boundary driven by measured ASIO
  switch time/sample position. The adapter may change only physical PCM
  consumption. It has no logical, cursor, epoch, stage, or judgement writer.

### S-126: Callback entry is not the ASIO buffer-switch instant

- **Mistake:** The initial mapper combined a QPC tick count with seconds and
  treated callback entry as the buffer switch. Delivery delay then became a
  permanent audible offset.
- **Prevention:** Physical mapping performs explicit unit conversion and uses
  valid ASIO `systemTime`/`samplePosition` for the switch represented by the
  callback. Callback-entry host time is used only to correlate the driver's
  Windows multimedia-clock value with QPC, never as the switch itself.

### S-127: Modal state had two authorities and no exact transition boundary

- **Mistake:** One mutex-protected logical modal record and a separate atomic
  physical modal bit could disagree. The prose also failed to define where QPC
  capture sat relative to the state update and native modal loop.
- **Prevention:** One game/window-thread record under one mutex owns foreground,
  native-modal state, and modal pause intervals. Each transition takes the
  mutex, captures QPC, publishes the complete record, closes any old physical
  session when necessary, unlocks, and then wakes the controller.

### S-128: Interruption did not directly close the callback data plane

- **Mistake:** The controller could react later while an old callback continued
  consuming PCM; nulling the route then falsely claimed that a route-less
  callback could write format-correct silence without buffer/session metadata.
- **Prevention:** One process-lifetime current-session status word has a sticky
  interruption bit and audible bit. Window/callback faults atomically clear
  audible before waking the controller. The old route remains alive and writes
  silence without consuming PCM until the driver is stopped or exited; a null
  route never claims buffer access.

### S-129: Logical bootstrap and the physical lead envelope were undefined

- **Mistake:** The logical engine could be published before a healthy producer
  and contiguous ahead window existed. Lead, store size, converter reach, and
  scheduling margin were prose rather than an admission inequality.
- **Prevention:** The logical engine has fixed backend-independent block, ahead,
  and SPSC-feed capacities. Startup waits for a healthy contiguous logical
  ahead window before publishing DirectSound. Physical admission must satisfy
  the complete latency/buffer/filter-reach inequality declared in the logical
  engine contract.

### S-130: ASIO properties were queried before they were defined

- **Mistake:** The attempt treated selected-channel activity/format and latency
  as pre-`createBuffers` facts. The ASIO contract defines active channel facts
  and latency only after buffers exist.
- **Prevention:** Pre-create queries are limited to driver identity, channel
  counts, current rate, and buffer constraints. Selected channel info and
  latencies are queried and validated after successful `createBuffers`, then
  both halves are silenced before `buffers_ready` is published or `start` is
  called.

### S-131: Running commit could overwrite a callback fault

- **Mistake:** Controller publication and callback fault publication did not
  share one atomic modification order, so `Running` could be committed after a
  concurrent fault or interruption.
- **Prevention:** One session-status word contains primed, audible,
  interrupted, restart, and fault bits. Callback/window operations set sticky
  bits and clear audible in that word. Final commit is one exact-state CAS while
  holding the authoritative window-state mutex; it cannot erase a concurrent
  bit.

### S-132: Retry classification was open-ended

- **Mistake:** Phrases such as ordinary unavailable or clock-not-ready did not
  form an implementable closed mapping from operation and result to accept,
  retry, restart, or terminal.
- **Prevention:** The physical lifecycle spec contains a closed operation/result
  table. Unknown results, host protocol/memory/lifetime failures, and unsafe
  teardown are terminal. Current-state changes may discard only an otherwise
  successful or explicitly retryable uncommitted attempt.

### S-133: Silent callback loss was promised detectable without evidence

- **Mistake:** The rewrite prohibited cadence timeouts yet implied that a driver
  which stopped callbacks without returning an error would be recovered.
- **Prevention:** Elapsed silence remains non-evidence. A driver which neither
  calls back nor signals reset/loss is unobservable and is outside the admitted
  target contract. Runtime acceptance must show the supported driver either
  remains healthy or emits an explicit restart/discontinuity observation.

### S-134: The callback ABI and COM apartment were incomplete

- **Mistake:** `asioMessage`, `sampleRateDidChange`, `ASIOTrue` versus legacy
  callback form, null/prebuffer behavior, and controller COM initialization
  were left as general prose.
- **Prevention:** The physical spec gives a closed selector/return table,
  rejects unsupported callback forms before audible commit, and assigns one
  controller-thread COM apartment for creation through release.

### S-135: Teardown could remove the only safe silence writer too early

- **Mistake:** The route was detached before a potentially blocking stop/exit,
  and outer shutdown could destroy PCM while callbacks still referenced it.
  The proof also assumed COM `Release` was an ASIO-exit boundary without naming
  the local wrapper/SDK evidence.
- **Prevention:** Close audible first, keep the routed session and buffers alive
  to write silence, call stop or the local ASIO-exit-equivalent release, then
  drain and detach only after the documented no-later-callback boundary. Outer
  shutdown destroys the physical owner before the logical producer/feed. If
  the driver never establishes that boundary, memory remains unreclaimed until
  process termination.

### S-136: One immutable terminal record contained future cleanup facts

- **Mistake:** A supposedly immutable fatal record was published before later
  teardown results existed, making either the immutability or the contents
  false; publication ordering was also unspecified.
- **Prevention:** Publish one immutable immediate fatal record with release/
  acquire ordering, and keep later cleanup results in a separate monotonic
  cleanup record. Cleanup can add facts but cannot revise classification.

### S-137: Two-song acceptance incorrectly required a physical reset

- **Mistake:** The physical controller was specified as stage-blind while the
  acceptance test required its session/read state to reset between songs.
- **Prevention:** Only logical stage and playback-epoch identity reset at the
  audited stage boundary. A healthy ASIO session may remain continuous across
  songs; it contains no song identity and has no judgement authority.

### S-138: File dependencies contradicted the claimed one-way graph

- **Mistake:** Judgement consumed epochs from a file which depended on
  judgement, callback gateway and presentation depended on one another, and a
  static rule forbade the very read-only logical port required for physical
  rendering.
- **Prevention:** The suite is split by owner with an acyclic order: logical
  clock/stage, logical producer/feed, judgement, physical ASIO, failure/
  shutdown, verification. Physical code may use only the named read-only
  logical-position and PCM-feed ports; all physical writes into logical state
  remain forbidden.

### S-139: Rejected tests were not explicitly deletion-only

- **Mistake:** The review candidate prohibited new tests but did not prevent
  three implementation-coupled tests from being adapted to bless another
  internal design.
- **Prevention:** The implementation must delete, not rewrite or replace,
  `LogicalPresentationClockTests.cpp`, `LogicalJudgementTimelineTests.cpp`, and
  `AsioPhysicalSessionControllerTests.cpp`, together with their CMake entries.
  No successor test is authorized by this spec.

### S-140: Runtime evidence lacked artifact and configuration identity

- **Mistake:** Log observations could be attributed to the wrong DLL or config,
  and static, instrumented, profiler, and user claims were mixed.
- **Prevention:** Every runtime run records the intended and deployed DLL hashes
  and effective config identity first. Evidence remains split into static/build,
  production log, external profile, and user gameplay categories.

### S-141: A shared overwrite store required the synchronization we meant to remove

- **Mistake:** The producer and callback shared a circular immutable-block store
  whose slot pinning, overwrite, starvation, and C++ publication proof were not
  actually simple or complete.
- **Prevention:** Replace it with one fixed process-lifetime SPSC queue. The
  logical producer is the sole writer; the controller may drain only while the
  driver is quiescent, and the ASIO callback is the sole running consumer.
  A full queue stops only physical copying while logical rendering continues;
  an overtaken feed cursor is marked discontinuous. Gaps are silent during
  priming and terminal after audible commit.

### S-142: An oversized logical lead would delay later control changes

- **Mistake:** The first local rewrite draft chose an 8,192-frame future lead
  merely because memory was cheap. That would pre-render about 186 ms at
  44,100 Hz, so a later `Play`, `Seek`, or `Stop` could not affect PCM already
  cached for that interval.
- **Prevention:** Derive the smallest fixed backend-independent envelope which
  admits the measured target session. The normative values are a 64-frame
  render block and 832-frame future lead; every ASIO session must satisfy the
  explicit latency/period/filter/uncertainty inequality instead of enlarging
  logical lookahead without reviewing DirectSound control latency.

### S-143: A latest-coordinate request made the one-way feed bidirectional

- **Mistake:** The first local SPSC draft still let the ASIO callback publish a
  desired logical coordinate back to the producer. Although it was one scalar,
  it made the claimed one-way PCM boundary false and invited another request/
  acknowledgement protocol later.
- **Prevention:** The producer owns a continuous private feed cursor and only
  publishes tagged PCM forward. A fresh silent callback discards stale tagged
  blocks until current coverage arrives. There is no physical-to-logical
  request, completion, or feedback path.

### S-144: The reduced fixed lead still changed DirectSound control timing

- **Mistake:** The frozen rewrite reduced the producer lead to 832 logical
  frames but still made `Play`, `Seek`, and `Stop` wait behind about 18.9 ms of
  immutable PCM. That was an extra software delay, not the hardware commit
  boundary, and could explain the observed backend timing shift.
- **Prevention:** Delete the producer/feed design. The ASIO callback directly
  renders the next unwritten physical interval and retains at most the two
  logical samples required for physical interpolation.

### S-145: The supposedly removed reverse request remained in priming text

- **Mistake:** File 04 still said priming published a newer latest request even
  though the same suite and S-143 prohibited all physical-to-logical requests.
- **Prevention:** Delete priming, the feed, and the producer. There is no
  coordinate request in either direction because the callback renders directly.

### S-146: `outputReady` was probed after latency had been frozen

- **Mistake:** The acquisition order called `getLatencies` before the
  `outputReady` capability probe, contradicting the local SDK rule that latency
  must reflect whether `outputReady` has been called.
- **Prevention:** After buffer creation, silence both halves, probe
  `outputReady`, and only then make the final latency query used by mapping.

### S-147: Clock correlation claimed a bound without timer/bracket ownership

- **Mistake:** The spec asserted a 3 ms QPC-to-`timeGetTime` correlation without
  a bounded sampling bracket or a balanced `timeBeginPeriod` lifetime.
- **Prevention:** Do not correlate QPC to ASIO at all. Fit driver sample position
  directly in ASIO's multimedia-time domain during silent startup; own one
  balanced 1 ms timer-period request and freeze the accepted map.

### S-148: ASIO time-info change flags were omitted

- **Mistake:** The callback table handled the separate rate callback but did not
  classify `kSampleRateChanged` or `kClockSourceChanged` in `ASIOTimeInfo`.
- **Prevention:** Either flag writes silence, publishes `RestartRequested`, and
  wakes the sole controller.

### S-149: Clearing `Audible` was falsely described as revoking submitted PCM

- **Mistake:** A callback could pass its final `Audible` check before another
  thread cleared the bit and then submit audible PCM. Hardware-submitted PCM was
  also irrevocable, so the promised atomic permanent silence was impossible.
- **Prevention:** Delete focus-driven audible closure. Explicit restart/fatal
  publication prevents later callback work but honestly permits an already
  submitted physical period to drain. Modal recovery never replays it.

### S-150: The synchronization inventory falsely claimed project-wide completeness

- **Mistake:** The suite called five mechanisms exhaustive while retaining the
  DirectSound control mutex, epoch publication, route pointer, callback-active
  state, fatal publication, and diagnostics.
- **Prevention:** Enumerate only new ASIO coordination and separately name
  retained logical/local mechanisms. Never claim all project synchronization
  fits a physical-session budget.

### S-151: Logical producer fatality had no fail-stop owner

- **Mistake:** The producer could fail after facade publication, but only the
  physical controller had a defined fatal publication path.
- **Prevention:** Delete the producer. Mixer/render failure occurs in the ASIO
  callback and uses the single packed physical fatal publication.

### S-152: Producer scheduling and catch-up work were unbounded

- **Mistake:** “Bounded catch-up” had no bound; a scheduling gap could trigger
  hundreds of render blocks and cause the very in-song hitch being fixed.
- **Prevention:** Delete scheduled production and catch-up. Each callback makes
  one bounded variable-span render call for its physical interval; a recovery
  gap is represented by `discontinuity_frames`, not rendered block by block.

### S-153: Foreground initialization and window-hook lifetime were undefined

- **Mistake:** Acquisition trusted a foreground record that might never receive
  an initial event, and the installed window procedure had no safe lifetime.
- **Prevention:** ASIO has no foreground record or focus hook. The separate
  same-thread logical modal hook and its clock state are process-lifetime;
  dynamic unload while the game window exists is explicitly unsupported.

### S-154: The obsolete-test deletion inventory was incomplete

- **Mistake:** `AsioForegroundStateTests.cpp` still asserted the forbidden loss-
  generation model, and four already-absent policy tests were not durably named
  as forbidden recreations.
- **Prevention:** File 04 lists four deletion-only tests plus four absent tests
  that must remain absent. No replacement is authorized.

### S-155: Target-driver and two-rate claims lacked immutable provenance

- **Mistake:** Overwritten-log values were called captured facts without a DLL,
  config, or log identity, and “support” was conflated with runtime exercise at
  two rates.
- **Prevention:** Treat old values as sizing history only. Static arithmetic
  proves at least 44.1/48 kHz; the user runs the driver's actual rate. Every
  runtime claim starts with intended/deployed hashes and effective config.

### S-156: Focus recovery depended on callback/message arrival order

- **Mistake:** A discontinuous callback was fatal if it arrived just before the
  window thread published interruption and recoverable if it arrived just
  after. This reconstructed order between unrelated platform events.
- **Prevention:** Delete focus from ASIO lifecycle. Focus cannot excuse or cause
  a physical result, so the race does not exist.

### S-157: Callback route teardown used the `stop` boundary instead of exit

- **Mistake:** The route was detached after successful `stop`, although the SDK
  only ends buffer-switch callbacks there; selector/rate callbacks may remain
  until ASIO exit. Some non-started paths did not detach coherently at all.
- **Prevention:** Keep one route and complete runtime alive through the proven
  ASIO-exit-equivalent boundary on every resource-ledger path. Clear/destroy it
  only after that boundary returns.

### S-158: `Primed` had contradictory writers, wake, and invalidation

- **Mistake:** The callback was forbidden to write non-sticky state but also had
  to set `Primed`; no wake was guaranteed and a later benign miss could leave
  stale readiness committed.
- **Prevention:** Delete priming. Silent calibration has one callback publication
  (`CalibrationReady`); the controller either commits `Running` from its frozen
  data or rejects the session.

### S-159: Lead admission omitted producer scheduling delay

- **Mistake:** Capacity equations claimed complete future coverage but left less
  than one normal scheduling interval and defined no maximum producer service
  gap.
- **Prevention:** Delete future lead and producer scheduling. Callback-local
  scratch is sized directly from the admitted physical interval.

### S-160: A pivoted running fit was not validated after transformation

- **Mistake:** The raw new fit could pass slope/residual checks, then pivoting it
  through the old map could make the actual map disagree with all new samples.
- **Prevention:** There is no running fit or pivot. Calibrate silently once,
  freeze the map, validate without modifying it, and treat violation as fatal.

### S-161: Capacity used reported rate while rendering used measured rate

- **Mistake:** A session at the edge of an inequality could pass with reported
  `Fd` and exceed the same capacity at an allowed measured-rate deviation.
- **Prevention:** Allocate callback-local scratch from the accepted measured
  map and checked worst-case interval before `Running`; no fixed producer-lead
  inequality remains.

### S-162: Route/fault/rundown memory ordering was incomplete

- **Mistake:** The normative files omitted release/acquire edges for route
  publication and teardown and gave overlapping callbacks no coherent first-
  fault writer.
- **Prevention:** Publish/load the active runtime release/acquire, retain it to
  ASIO exit, and pack the first fatal code with the sticky fatal bit in one CAS.
  The exit contract replaces an invented rundown protocol.

### S-163: Shutdown precedence masked unsafe teardown failure

- **Mistake:** Sticky shutdown was said to permit cleanup only while stop/exit
  failures were simultaneously terminal, allowing opposite outcomes.
- **Prevention:** Fatality outranks shutdown. Shutdown forbids new acquisition
  but never relabels callback or teardown failure as successful cleanup.

### S-164: IDE source navigation was accidentally batched

- **Mistake:** During this rewrite, several documentation files were opened in
  one MCP call despite the user's file-by-file IDE rule.
- **Prevention:** Every later CLion source/diagnostic operation handles one file,
  waits for analysis, and then moves to the next. Files and CLion are never
  closed or restarted by the agent.

### S-165: The producer cure for callback-owned rendering became worse than the defect

- **Mistake:** H-002 correctly identified that callback loss must not stop
  logical cursor/judgement progression, but its historical “never again” cure
  made a permanent producer authoritative for mixer advancement. The producer
  then required lead, feed, requests, priming, and synchronization and changed
  DirectSound control timing.
- **Prevention:** Separate logical authority from physical rendering instead of
  duplicating rendering. The logical cursor and bound judgement epoch progress
  from host time without callbacks. The ASIO callback may remain the sole mixer
  render caller for physical PCM; after loss it makes one forward
  `discontinuity_frames` advance and never replays/catches up PCM.

### S-166: The frozen rewrite still used an overengineered calibration protocol

- **Mistake:** The supposedly simplified design collected 1,024 callbacks,
  published `CalibrationReady`, woke the controller, fitted an affine clock,
  allocated again, and published `Running` back to the callback. This was a
  callback-controller-callback barrier while the text claimed no barrier.
- **Prevention:** Delete calibration, the fit, its records, and all calibration
  modes. One valid direct callback creates one immutable fixed-rate physical
  anchor from SDK `systemTime`, sample position, and output latency.

### S-167: Physical callback pacing still created logical playback history

- **Mistake:** `Play` during startup/recovery had no epoch until a later callback
  rendered PCM, so callback loss delayed logical cursors, preview state, and the
  possible judgement anchor.
- **Prevention:** `Play`, `Seek`, and `Stop` publish logical control history at
  control time before returning. ASIO consumes this history and never creates or
  revises it.

### S-168: Stage binding omitted the audited watermark and immutable offset

- **Mistake:** The design could select a prior-stage playback generation and
  reread live `GameTimeOffset` on every hit.
- **Prevention:** Semantic stage entry publishes a generation watermark and
  snapshots `G_stage`; bind the first selected BGM `Play` after the watermark.
  Later playback and settings changes cannot move the anchor.

### S-169: The modal-pause formula corrupted historical timestamps

- **Mistake:** Subtracting the current accumulated pause from every historical
  timestamp changed pre-pause input and misclassified input inside a pause.
- **Prevention:** Delete the loader modal-pause clock entirely. Focus and
  move/resize do not change logical time; there is no historical modal
  reconstruction problem.

### S-170: Modal publication introduced another cross-thread snapshot

- **Mistake:** A window thread wrote pause fields while the ASIO callback read
  them without a coherent representation, requiring another synchronization
  protocol.
- **Prevention:** Delete all WndProc/WinEvent/modal state from this design.

### S-171: Rational physical placement was forced into whole-frame epochs

- **Mistake:** The design called `O0` rational while retaining `uint64_t` epoch
  and judgement storage, leaving truncation/rounding capable of moving timing.
- **Prevention:** Logical control epochs intentionally use the integer 44.1-kHz
  sample lattice with a specified `floor` rule. Rational ASIO placement exists
  only inside physical interpolation and is never stored in judgement history.

### S-172: Fractional spans lacked a complete mixer recurrence

- **Mistake:** Variable 176.4-frame spans did not define outward bounds, cached
  indices, rational carry, integer discontinuity, or behind-tail behavior. A
  naive 176/177 choice could drift or repeat preview PCM.
- **Prevention:** Specify `L`, `U`, `next_logical_frame`, two indexed retained
  samples, one exact fixed-ratio remainder, one mixer call, integer skip, and
  whole-buffer behind silence.

### S-173: `ASIOFalse` was accepted while doing heavy inline work

- **Mistake:** The callback always mixed and converted inline even though the
  local SDK uses `ASIOFalse` to request deferred processing.
- **Prevention:** Admit only `ASIOTrue`. Initial or later `ASIOFalse` is
  structural fatality; never add a deferred worker to claim support.

### S-174: One packed runtime word created unsafe mixed-writer transitions

- **Mistake:** Mode, callback-active, restart, fatal, and shutdown shared one
  RMW word. Benign concurrent changes could look like overlap or overwrite
  another transition.
- **Prevention:** Use a controller-only public state and a separate write-only
  signal bitset. Delete the overlap flag and every mixed-writer mode transition.

### S-175: Restart versus fatality depended on notification arrival order

- **Mistake:** The same discontinuity was recoverable if a reset message arrived
  first and fatal if the buffer callback arrived first.
- **Prevention:** Structurally validate every indexed callback before reading
  pending signals. A structural buffer violation is fatal regardless of later
  notification order; a structurally valid explicit notification is recovery.

### S-176: Stage readiness checked a mode but ignored sticky failure

- **Mistake:** `Running` could coexist with restart/fatal/shutdown bits, so a new
  stage could be admitted into a knowingly silent runtime.
- **Prevention:** The controller owns one public backend state. Initial exposure
  and stage entry accept only controller-published `Running`; the controller
  leaves that state before recovery or fatal cleanup.

### S-177: Initial facade exposure during long silent setup was undefined

- **Mistake:** The game-facing facade could be visible during more than four
  seconds of calibration, making healthy startup race stage entry.
- **Prevention:** Calibration is deleted. Initial construction is not
  stage-capable until the first valid direct buffer; the wait has no diagnostic
  timeout.

### S-178: Runtime fatality had no game-facing fail-stop consumer

- **Mistake:** The controller could close ASIO while the current stage and score
  continued indefinitely in silence.
- **Prevention:** The controller invokes the existing production runtime-failure
  observer exactly once before potentially blocking teardown. That existing
  path logs, shows the audio error, and performs process fail-stop.

### S-179: Retry policy omitted COM and boolean failure domains

- **Mistake:** Only `ASIOError` values were classified; COM activation HRESULT
  and `init` boolean failure had no defined outcome.
- **Prevention:** Initial failure never retries. During explicit recovery every
  acquisition failure, regardless of result domain, consumes one of the fixed
  attempts. Structural runtime failure never retries.

### S-180: The host advertised an SDK-unsupported buffer-size selector

- **Mistake:** `kAsioBufferSizeChange` returned supported even though the local
  SDK says hosts return `0` and drivers request reset instead.
- **Prevention:** Do not advertise it and always return `0`.

### S-181: The affine fit was numerically underdefined

- **Mistake:** Estimator, centering, rounding, wrap handling, and uniqueness were
  absent, so several maps could satisfy the same loose residual bounds while
  selecting different PCM.
- **Prevention:** Delete the fit. Use one exact SDK timestamp/sample-position
  anchor plus the reported fixed rate, with checked integer rational math.

### S-182: Required per-hit diagnostics violated hot-path policy

- **Mistake:** The verification draft required logging every judgement's
  operands despite `AGENTS.md` forbidding per-call normal-path diagnostics.
- **Prevention:** Prove operand ownership statically and emit only bounded
  stage-level snapshots and aggregate failures outside the hot path.

### S-183: The recovery acceptance matrix never exercised recovery

- **Mistake:** Ordinary focus/two-song runs could pass while no explicit ASIO
  recovery notification ever occurred.
- **Prevention:** Claim runtime recovery only from a hash-controlled target run
  with a real SDK restart notification. Otherwise label it unexercised; do not
  invent a fake driver or policy-copying test.

### S-184: Callback/log overlap was treated as frame-drop causality

- **Mistake:** A periodic callback overlapping a hitch was enough to blame the
  loader even though native, GPU, or scheduler work could be causal.
- **Prevention:** Logs show correlation only. Causality requires an external
  profile with loader stack CPU/blocking contribution.

### S-185: Non-buffer notifications were told to silence a buffer

- **Mistake:** `sampleRateDidChange` and `asioMessage` have no buffer index but
  were assigned buffer writes.
- **Prevention:** They publish signal bits only. A later indexed buffer callback
  may observe the bit and silence its own half.

### S-186: WASAPI scope was contradictory

- **Mistake:** The suite alternately claimed one new backend-independent clock
  and an ASIO-only provider while saying WASAPI remained unchanged.
- **Prevention:** The WASAPI engine, endpoint clock, and render schedule are
  explicitly out of scope and remain the baseline. Shared algebra does not route
  any ASIO value into WASAPI.

### S-187: The hard complexity rule was stated but not applied

- **Mistake:** Enumerating calibration states, barriers, packed flags, and event
  precedence was used as a substitute for simplifying ownership.
- **Prevention:** Treat substantial synchronization or event-order dependence as
  an immediate design rejection. A documented complicated protocol is still a
  failed architecture.

## 2026-08-31 frozen-tree review failures

The following findings rejected frozen tree
`6735b0a0a141fdf4c2e57e92f143811b5423f7bd`. Duplicate entries are retained
because the two independent reviewers reached them separately.

### S-188: Direct callback ownership assumed SDK serialization

- **Mistake:** `ASIOTrue` was treated as proof that buffer callbacks cannot
  overlap even though the local SDK permits recursive access.
- **Prevention:** Admit the stateful render path with one atomic one-flight
  guard. A loser touches no mutable render state or driver buffer, latches fatal,
  and returns. Do not add a queue, wait, or worker.

### S-189: The fixed nominal rate recreated accumulated drift

- **Mistake:** One anchor advanced forever at reported `Fl/Fd`, knowingly
  recreating the 50-ppm/about-9-ms three-minute failure in S-125.
- **Prevention:** Measure actual samplePosition/systemTime slope inside the
  callback during silent admission and freeze that measured physical ratio for
  the session. It remains read-only relative to logical state.

### S-190: An exact QPC mapping was claimed but not defined

- **Mistake:** The text called QPC-to-multimedia correlation exact while current
  resolution ignored QPC and used one-millisecond timestamps.
- **Prevention:** QPC itself is the sole logical authority:
  `L(q)=(q-Q0)*Fl/Fq`. Multimedia time appears only in the physical bridge.

### S-191: Play/Seek first observation replayed from its source origin

- **Mistake:** The mixer applies a new generation at render time and explicitly
  skips discontinuity, so a control during callback absence started late at
  `S0` instead of materializing `max(0,R-O0)`.
- **Prevention:** Publish control QPC/state before return. At render, silence
  before the origin and derive source position, loop, and end directly from the
  control origin. A global gap never precedes a later Play.

### S-192: A fresh adapter depended on state it had discarded

- **Mistake:** Each attempt destroyed retained PCM and `next_logical_frame` but
  the recovery recurrence still compared against them.
- **Prevention:** Keep the logical mixer and exclusive render tail for the ASIO
  backend lifetime. A fresh physical adapter owns no old samples and stays
  silent while its request is behind that persistent tail.

### S-193: End, loop, status, and preview sequencing remained callback-owned

- **Mistake:** The logical epoch lacked playing/looping/length state, while
  natural end and `GetStatus` still depended on mixer rendering.
- **Prevention:** One logical media snapshot contains the complete projection
  inputs. Cursor, loop, natural end, and playing status derive from QPC without
  a physical callback.

### S-194: Historical modal requirements contradicted the final gate

- **Mistake:** H-004 still described modal-pause behavior after S-169/S-170 and
  G-03 had deleted all loader modal state, allowing reviewers to select two
  different authorities.
- **Prevention:** Mark the historical cure superseded. The current controlling
  rule has no focus/move/resize input to logical time or ASIO lifecycle.

### S-195: Startup callback failure and runtime failure shared the wrong sink

- **Mistake:** A first-callback failure was both startup-fatal and routed through
  a wrongly named runtime observer.
- **Prevention:** Before facade exposure use
  `ProductionAudioBackendControllerReporter::FatalStartupFailure`; afterward
  use `ProductionAsioObserver::RuntimeFailed`/`ReportAsioRuntimeFailure`.

### S-196: A signal word was both cleared and required to be sticky

- **Mistake:** The controller cleared restart/fatal/shutdown bits before old
  teardown, permitting later callbacks to resume rendering.
- **Prevention:** Attempt-local terminal bits are never cleared. Teardown and
  final classification use the same word; retry allocates a new zeroed runtime.

### S-197: The claimed coordination budget omitted real publications

- **Mistake:** Backend state and the fatal record had no C++ publication rule,
  and logical control publication was hidden while being required by callback
  rendering.
- **Prevention:** Remove cross-thread public backend state, atomically encode the
  first fatal code, and document logical media publication separately as one
  single-writer scalar-atomic snapshot boundary.

### S-198: Runtime acceptance used unsupported timing and causality wording

- **Mistake:** Startup was required to be “prompt” without a deadline, and any
  focus-correlated recovery was rejected even if the driver emitted a real SDK
  notification.
- **Prevention:** Startup exposure is eventual. Window actions cause no
  loader-initiated transition; an actual SDK notification is valid regardless
  of temporal correlation with an alt-tab.

### S-199: WASAPI proof inspected only WASAPI-named files

- **Mistake:** Shared `DirectSoundFacade`, `AudioRenderCore`, and
  `MiniaudioMixer` behavior could change while the WASAPI-specific files stayed
  untouched.
- **Prevention:** Trace every changed shared branch reachable from WASAPI and
  preserve its old mixer mode, cursor/end path, clock, schedule, and provider.

### S-200: Failed stop left disposal ownership ambiguous

- **Mistake:** Teardown said to dispose buffers “if permitted” without closing
  the local SDK rule.
- **Prevention:** Successful stop permits individual disposal. Failed stop skips
  it and proceeds only through the ASIO-exit-equivalent driver release.

### S-201: Independent review again found nominal-rate drift

- **Mistake:** Deleting a fit was treated as proof that reported rate became
  physically exact.
- **Prevention:** The physical side must observe the actual device clock. The
  accepted rewrite uses one callback-local integer measurement and no logical
  writeback.

### S-202: Independent review again found incomplete control state

- **Mistake:** `{O0,S0,Fl,Fs,kind}` could not reconstruct stopped Seek,
  looping, natural end, or a Play origin inside the render span.
- **Prevention:** Publish a complete immutable current media state and materialize
  each voice from it at the requested logical frame.

### S-203: Independent review again found mixed adapter ownership

- **Mistake:** The recurrence alternated between fresh adapter state and an
  undeclared retained logical tail.
- **Prevention:** Name the persistent mixer tail as logical-render state. Driver
  buffers, resampler phase, and interpolation samples remain attempt-local.

### S-204: Independent review again rejected the QPC correlation claim

- **Mistake:** Adjacent QPC/timeGetTime samples were described as an exact map,
  although QPC resolution was discarded.
- **Prevention:** Use QPC directly for logical time. Define a separate bounded
  multimedia/QPC bridge only for physical ASIO presentation.

### S-205: Independent review selected the stale modal cure

- **Mistake:** The ledger's old “accepted modal transaction” sentence survived
  after later findings explicitly deleted it.
- **Prevention:** This was the rejected revision's attempted correction. In the
  current tree neither this entry nor its old final gates are normative.

### S-206: Lifecycle and logical-media synchronization were conflated

- **Mistake:** A tiny lifecycle budget falsely implied that control-to-render
  media publication did not exist.
- **Prevention:** Keep two direct boundaries: lifecycle pointer/signals/flight
  guard, and one immutable logical media snapshot. Neither is an acknowledgement
  protocol and neither writes into the other.

### S-207: Per-buffer generations could not order stage entry globally

- **Mistake:** A stage watermark compared generation values owned by different
  buffers and G-18 appeared to reject its own mechanism.
- **Prevention:** Use one monotonic logical control order solely for DirectSound
  control-versus-stage linearization. Scope the architecture stop to distributed
  recovery/event reconstruction, not one scalar publication boundary.

### S-208: Independent review again rejected callback serialization

- **Mistake:** Empirical target-driver behavior was proposed as an SDK contract.
- **Prevention:** The one-flight guard is mandatory and converts overlap into a
  memory-safe structural fatality without pretending to support concurrent
  stateful rendering.

### S-209: Readiness and terminal publication admitted teardown races

- **Mistake:** `FirstDirectBuffer` could be observed before complete output and
  controller state could still say Running after a terminal signal.
- **Prevention:** Publish `FirstOutput` only after render, write, and enabled
  `outputReady` all succeed. It proves the path, not non-silent PCM. Stage code
  never consumes physical readiness.

### S-210: Restart before first valid buffer had no closed transition

- **Mistake:** A pre-Running SDK restart could loop without consuming an
  attempt.
- **Prevention:** On initial construction it is startup failure. During an
  explicit recovery episode it ends and consumes that fixed attempt.

### S-211: Physical state had a reverse edge into stage admission

- **Mistake:** Stage entry read ASIO Running/Recovering and could reject or
  strand native sequencing.
- **Prevention:** Remove the read entirely. Recovery is transparent to semantic
  stage entry; unrecoverable physical failure process-fail-stops.

### S-212: The runtime fatal observer name was incorrect

- **Mistake:** The ASIO path named `ProductionAudioObserver` even though frozen
  source uses `ProductionAsioObserver` and `ReportAsioRuntimeFailure`.
- **Prevention:** Name and preserve the actual ASIO sink.

### S-213: Rate admission rejected valid current-rate drivers

- **Mistake:** Wording required every driver to advertise both 44.1 and 48 kHz
  while also prohibiting a rate change.
- **Prevention:** The loader supports both rates; a session admits the driver's
  one current valid rate and never calls `setSampleRate`.

### S-214: Deleting all physical measurement overcorrected S-166

- **Mistake:** S-166 correctly rejected a callback-controller-callback
  calibration barrier, but its cure deleted the only observation capable of
  separating the hardware oscillator from nominal `Fd`.
- **Prevention:** Restore only callback-local, count-bounded integer measurement.
  The callback completes it autonomously and proceeds to rendering; the
  controller receives no fit and sends no acknowledgement.

### S-215: The complexity gate was worded too broadly

- **Mistake:** Saying any generation or synchronization meant failure also
  appeared to ban one atomic publication or one-flight guard at a real ownership
  boundary.
- **Prevention:** Reject substantial distributed synchronization, mirrored
  lifecycle state, acknowledgements, and event-order reconstruction. Minimal
  single-owner publication and fail-safe admission are allowed only where the
  underlying C++/SDK boundary requires them.

### S-216: The millisecond estimator rejected a stable device

- **Mistake:** Integer-millisecond timestamp aliasing made two finite halves
  disagree by more than the 20 ppm admission limit even for an exactly linear
  oscillator inside the nominal-rate bound.
- **Prevention:** Delete physical-rate measurement. The callback renders an
  ordinary sequential stream at the driver's admitted current rate.

### S-217: A frozen finite fit plus permanent residual must false-fatal

- **Mistake:** Any nonzero finite-fit error eventually exceeds a fixed 2 ms
  residual threshold, so a healthy unbounded session was guaranteed to fail.
- **Prevention:** Delete the fit and residual watchdog. No running timestamp
  predicate diagnoses a driver clock.

### S-218: Observation counts were mistaken for elapsed intervals

- **Mistake:** `H` samples were claimed to span `H` callback periods although
  they span only `H - 1`.
- **Prevention:** The entire estimator and its duration claims are removed.
  Future count-versus-interval reasoning must state both explicitly.

### S-219: Independent timestamp floors understated bridge uncertainty

- **Mistake:** The difference between independently floored multimedia times
  was bounded by half a millisecond; its error can approach a full timestamp
  quantum, plus QPC bracket and rounding error.
- **Prevention:** Delete the multimedia/QPC bridge rather than revise a bound
  for a clock relationship the architecture does not need.

### S-220: Background startup invalidated the timer-resolution premise

- **Mistake:** `timeBeginPeriod(1)` was treated as proof of a 1 ms effective
  lattice even though Windows does not guarantee that for an occluded,
  minimized, invisible, or inaudible window-owning process.
- **Prevention:** ASIO startup and rendering do not use multimedia timer
  resolution at all.

### S-221: One phase sample could not map two clock rates

- **Mistake:** A single QPC/multimedia sample was used as if it established both
  phase and rate between unrelated clocks. Physical output could drift from
  logical QPC while the device-versus-multimedia residual remained perfect.
- **Prevention:** There is no cross-clock map. QPC owns logical state; the ASIO
  device paces only sequential PCM.

### S-222: The residual prediction map was not defined

- **Mistake:** Prose required comparison with a frozen map but defined only a
  slope, not its intercept, anchor, unwrapping, or exact predicate.
- **Prevention:** Delete the residual policy. Any future mathematical predicate
  must define every operand before acceptance.

### S-223: The global control watermark had no safe allocation equation

- **Mistake:** `fetch_add` return semantics, initial value, watermark meaning,
  and exhaustion made the first post-entry Play ambiguous and allowed wrap.
- **Prevention:** Delete the global order. Stage binding reads the current
  selected buffer's direct Play QPC/run anchor and compares it with stage entry.

### S-224: The mixer render tail had two owners and no initial value

- **Mistake:** The persistent mixer and fresh adapter both appeared to own
  `Mtail`, so recovery either lost or duplicated authoritative state.
- **Prevention:** Delete `Mtail` and the adapter. The persistent driver-rate
  mixer renders one sequential period; a new attempt rebases voices once.

### S-225: Voice conversion was reset on every callback

- **Mistake:** Per-frame materialization wording allowed source converters to
  discard their fractional phase every buffer, causing periodic jitter when
  `Fs != Fd`.
- **Prevention:** Reposition only on a new control snapshot or first callback of
  a fresh attempt. Contiguous callbacks preserve converter phase.

### S-226: Facade exposure raced a terminal callback

- **Mistake:** The controller could read `FirstOutput`, then receive a terminal
  bit, then expose the backend from the stale read.
- **Prevention:** Delete `FirstOutput`. After `start` returns, one CAS commits
  Running only when no terminal bit is present; atomic modification order is
  the sole startup classification boundary.

### S-227: Shutdown disappeared between physical attempts

- **Mistake:** Shutdown was stored only in attempt-local state, but retry delays
  intentionally have no live attempt.
- **Prevention:** Outer shutdown is controller-lifetime state and wakes the same
  controller event during a session, teardown, or retry delay.

### S-228: ASIO buffer geometry leaked into DirectSound write position

- **Mistake:** The supposedly logical cursor path retained endpoint frames and
  output rate when projecting the write cursor.
- **Prevention:** ASIO uses a fixed logical DirectSound write lead; WASAPI alone
  retains its endpoint-packet policy.

### S-229: Natural end returned an invalid cursor and omitted replay rules

- **Mistake:** A one-past-end sentinel was returned as a DirectSound byte offset
  and later Play behavior was unspecified.
- **Prevention:** Keep end as internal state, return `B - 1`, and define Play
  after natural end to begin a new run at zero.

### S-230: Every recovery attempt intentionally muted about eight seconds

- **Mistake:** Clock admission forced a long silent callback interval for every
  fresh physical session, making bounded retries unusable during a song.
- **Prevention:** There is no admission measurement or first-output wait. A
  successful attempt renders on its first admitted callback.

### S-231: Exact estimator products had no feasible x86 representation

- **Mistake:** The specification required cross-products far beyond 64-bit but
  named neither a multiword type nor an overflow-free comparison.
- **Prevention:** Delete estimator arithmetic. Remaining checked rational
  operations must fit the repository's explicitly bounded representation.

### S-232: `FirstOutput` and retry success had two meanings

- **Mistake:** One file said later `FirstOutput` had no effect while another
  used it to commit each recovery attempt; the retry budget scope was also
  unstated.
- **Prevention:** Delete `FirstOutput`. Running CAS commits an attempt, and the
  three-attempt budget is explicitly per recovery episode.

### S-233: Startup required both audible and legitimately silent output

- **Mistake:** Initial success demanded an “audible” buffer even though no voice
  may exist before facade exposure.
- **Prevention:** Startup commits after successful `start`, not after inspecting
  PCM energy or waiting for a callback.

### S-234: Modal interruption had no stated native catch-up contract

- **Mistake:** QPC advanced while native Tune could pause, but the design merely
  declared modal state absent and did not say how the two reunite.
- **Prevention:** Cursor and judgement continue on one QPC projection. On native
  resume, the existing gameplay-song-clock path catches Tune up under its
  bounded policy; acceptance requires no permanent offset.

### S-235: Startup reporting sat behind potentially hanging teardown

- **Mistake:** The startup fail-stop was invoked only after cleanup even though
  driver release was explicitly allowed not to return.
- **Prevention:** Publish the one-shot startup failure and invoke the existing
  fail-stop before waiting on potentially blocking cleanup.

### S-236: Fatal reporting promised impossible retroactive ordering

- **Mistake:** A fatal reason first observed during blocking teardown was still
  promised to have been reported before teardown began.
- **Prevention:** Report known fatality before teardown; report late fatality as
  soon as observed and before the next blocking operation.

### S-237: Failed `stop` could still continue recovery

- **Mistake:** Cleanup wording preserved an already-selected Restart after
  `stop` failed, allowing a replacement session after unsafe teardown.
- **Prevention:** Failed running `stop` selects Fatal, skips disposal, and
  prohibits every replacement attempt.

### S-238: The closed coordination list omitted startup completion

- **Mistake:** The caller had no race-free publication by which construction
  could receive success or failure from the controller thread.
- **Prevention:** Name exactly one controller-to-caller one-shot startup result.
  It is not reused for recovery.

### S-239: Persistent mixer capacity was not closed across replacement

- **Mistake:** Each attempt could choose a larger buffer/rate while callback
  scratch belonged to the persistent mixer and could not be resized safely.
- **Prevention:** Initial `Fd` and configured `N` are backend-lifetime
  invariants. Recovery must match them and the original capacity.

### S-240: Required logs contradicted callback-local measurement

- **Mistake:** Runtime acceptance demanded estimator and bridge values while
  the callback was forbidden to publish them and the controller was forbidden
  to receive them.
- **Prevention:** Delete those values. Logs contain only bounded startup,
  recovery, final, fatal, and stage-anchor records.

### S-241: Runtime provenance could not be rechecked

- **Mistake:** A deployed hash without intended-artifact equality, effective
  config identity, and the exact overwritten-log hash could not attribute a
  run.
- **Prevention:** Capture intended/deployed SHA-256 equality and config identity
  before a run, then hash or retain the exact latest log before the next run.

### S-242: One runtime log was asked to prove no fallback path exists

- **Mistake:** Absence of a transition record was presented as proof that no
  fallback source branch existed.
- **Prevention:** No fallback is proved by exhaustive static factory/error-path
  inspection. Runtime records only what happened in that run.

### S-243: Physical-clock repair violated the architecture stop rule

- **Mistake:** S-214 restored measurement, which expanded into regression,
  half-window admission, timer-resolution assumptions, a QPC bridge, residual
  monitoring, interpolation, and long silent startup. The cure recreated the
  overengineered subsystem the stop rule was meant to reject.
- **Prevention:** Supersede S-214's cure. Use conventional sequential ASIO
  output and keep logical time wholly outside the device.

### S-244: Callback validation exceeded the data rendering actually needs

- **Mistake:** Exact sample-position increments, index alternation,
  `ASIOTrue`, valid timestamps, and one callback style were made fatal although
  indexed sequential output needs none of them.
- **Prevention:** Accept both callback styles and validate only Ready state,
  index, buffer views, render, conversion, and enabled `outputReady`.

### S-245: Driver overload telemetry was treated as broken ownership

- **Mistake:** `kAsioOverload` was made structurally fatal even though it reports
  a performance miss and does not itself invalidate session resources.
- **Prevention:** Count it without a lifecycle transition. Concrete render or
  lifecycle failure remains fatal.

### S-246: First-output admission added synchronization without authority

- **Mistake:** Facade exposure waited for a callback even though successful
  synchronous ASIO setup and `start` already define acquisition success.
- **Prevention:** Commit Running immediately after successful `start` through
  one atomic linearization; never wait for a callback to expose the backend.

### S-247: A global logical output timeline was an unnecessary middle layer

- **Mistake:** DirectSound controls were translated into global output frames,
  histories, and providers before being translated back into source/song time.
- **Prevention:** Store per-buffer control QPC and Play QPC directly. Cursor and
  judgement project from those anchors with no global stream.

### S-248: Stage selection was turned into an event-history protocol

- **Mistake:** Playback histories and a global watermark reconstructed an order
  already present in the selected buffer's current Play run and native stage
  call order.
- **Prevention:** The scoped group-2 observation carries the current direct Play
  anchor. If native Play ordering is false, stop and revise rather than add
  history.

### S-249: The physical output adapter solved a problem the new ownership removed

- **Mistake:** Canonical-rate PCM, retained samples, variable spans, and output
  interpolation remained after logical time no longer depended on device
  position.
- **Prevention:** Create the mixer at `Fd` and render exactly `N` sequential
  frames. Miniaudio alone performs ordinary source-rate conversion.

### S-250: Independent clock perfection was implied without choosing an owner

- **Mistake:** Prose promised zero physical drift while also forbidding both
  ASIO clock authority and a running clock controller.
- **Prevention:** QPC remains the only logical/judgement owner. Target hardware
  must pass multi-song physical acceptance under conventional driver pacing; if
  not, stop for an explicit requirement decision instead of hiding feedback.

## Historical non-recurrence gates from rejected tree `7dfb0633`

This rejected tree claimed that these gates superseded earlier prevention
clauses. They are retained only to show what that revision asserted. They do
not supersede anything in the current tree and have no normative authority.

1. **G-01 — Per-buffer QPC authority:** Each ASIO-selected DirectSound buffer's
   control QPC/state owns cursor and status. There is no global logical output
   clock and no ASIO-derived logical value.
2. **G-02 — Closed DirectSound semantics:** Play-while-playing, stopped Play,
   natural-end replay, Seek, Stop, loop, public cursor range, and status follow
   file 01 without callback publication.
3. **G-03 — Direct stage anchor:** The scoped selected-BGM observation carries
   the current buffer's Play QPC/run directly. There is no watermark, output
   epoch, or playback-history reconstruction.
4. **G-04 — Absolute judgement equation:** Input QPC, Play QPC, source origin/
   rate, stage-start `GameTimeOffset`, and the unchanged native
   `JudgTimeOffset` boundary are the only judgement inputs.
5. **G-05 — Focus/window absence:** Focus, foreground, move, resize, WinEvent,
   and WndProc have no loader ASIO-lifecycle or logical-clock path. Native
   resume catches Tune up from the same continuing cursor.
6. **G-06 — Driver-rate sequential output:** The mixer runs at admitted `Fd`
   and renders exactly `N` frames once per callback. No output adapter, logical
   PCM stream, variable span, feed, queue, or catch-up loop exists.
7. **G-07 — Boundary-only positioning:** A physical voice is repositioned only
   for a new control snapshot or the first callback of a fresh attempt.
   Contiguous callbacks preserve source-converter phase.
8. **G-08 — No physical clock subsystem:** `samplePosition`, `systemTime`,
   multimedia timers, clock measurement, regression, bridges, residuals, and
   phase/rate controllers are absent.
9. **G-09 — Current immutable format:** Initial construction uses the driver's
   current integral supported `Fd`, never calls `setSampleRate`, supports at
   least 44.1/48 kHz, and makes `Fd` and configured `N` recovery invariants.
10. **G-10 — Minimal callback contract:** Both callback styles render directly;
    only Ready/index/views/render/conversion/outputReady are validated. One
    one-flight guard converts overlap to fatal without a wait or worker.
11. **G-11 — Direct publications only:** One per-buffer scalar snapshot, one
    one-shot startup result, and one attempt-local signal block are the complete
    cross-thread publications. They do not acknowledge or reconstruct events.
12. **G-12 — Atomic Running commit:** Successful `start` is followed by one CAS
    that commits Running only without a terminal bit. No first-output wait or
    read-to-expose window exists.
13. **G-13 — Single IASIO owner:** Only the controller creates, starts, stops,
    retries, disposes, releases, or replaces a driver session. Its lifecycle is
    procedural local flow, not a public state machine.
14. **G-14 — Closed recovery:** Only explicit SDK requests recover. Each episode
    has one immediate acquisition plus at most two retries; a successful commit
    ends the episode; structural failure never retries.
15. **G-15 — Correct failure owners:** Initial failure is published/reported
    before blocking cleanup. Known runtime fatality is reported before teardown;
    late fatality is reported when observed before the next blocking call.
16. **G-16 — Exit-owned lifetime:** Callback runtime remains valid through the
    local exit-equivalent release. Failed running `stop` skips disposal, selects
    Fatal, and prohibits replacement.
17. **G-17 — Controller-lifetime shutdown:** Shutdown remains observable between
    attempts and interrupts retry delays using the controller wake event.
18. **G-18 — No time as loss evidence:** Time is used only for QPC projection,
    physical output-latency positioning, and explicit retry delays—never to
    infer focus, ownership loss, callback death, or recovery success.
19. **G-19 — Bounded diagnostics:** No callback/per-cursor/per-hit or periodic
    stage logging exists. Runtime records do not claim static no-fallback proof.
20. **G-20 — No policy-copy tests:** No fake driver/native test or production
    formula restatement is evidence without an independent oracle and approval.
21. **G-21 — Evidence separation:** Static/build, exact deployment/log identity,
    external profile, and user gameplay are separate claims.
22. **G-22 — WASAPI baseline:** Every shared branch preserves WASAPI's endpoint
    clock, mixer mode, cursor/end/write-cursor path, render schedule, gameplay
    song clock, and judgement provider.
23. **G-23 — Static no fallback:** Every ASIO factory/error/recovery branch is
    inspected. A runtime log cannot prove source-path absence.
24. **G-24 — Clock-domain honesty:** QPC owns logical/judgement time and the
    driver owns sequential PCM pacing. Target multi-song drift must pass runtime
    acceptance; failure stops for a requirement decision, not hidden feedback.
25. **G-25 — Architecture stop:** Substantial synchronization, mirrored state,
    event-order reconstruction, calibration, barriers, acknowledgements,
    producer/consumer PCM, or multiple coordinators means the architecture is
    wrong. Stop and rewrite.
26. **G-26 — Frozen-tree review:** Any review finding is recorded here, changes
    the tree, and resets both independent reviews.

## Rejection of frozen tree `7dfb0633a7d87b107fc0c97e228205da28d858d7`

### S-251: Nominal driver rate was treated as a shared clock

- **Mistake:** Sequential `N`-frame rendering at reported `Fd` was claimed to
  remain aligned with the independent QPC song clock. Hardware oscillator
  error can accumulate across a song or credit.
- **Prevention:** Physical output must use the ASIO block timestamp to select
  absolute logical content. This correction remains physical-only and never
  becomes a judgement operand.

### S-252: Callback-entry QPC was substituted for buffer-switch time

- **Mistake:** The proposed first-buffer rebase added output latency to a QPC
  captured after callback dispatch, so scheduler delay became a constant audio
  advance.
- **Prevention:** Use the SDK's current-block `systemTime/samplePosition` pair
  and reported output latency for physical DAC time. Callback-entry time is not
  a presentation timestamp.

### S-253: The publication inventory omitted existing owners

- **Mistake:** The suite claimed only three publications while the production
  mixer already had immutable PCM snapshot/hazard publication, retirement,
  writer locking, and independent gain publication.
- **Prevention:** Inventory only new ASIO-specific coordination and explicitly
  preserve established PCM, gain, and voice-lifetime boundaries.

### S-254: Snapshot traversal order was impossible

- **Mistake:** The callback was required to read every voice snapshot before a
  callback QPC even though miniaudio discovers voices during graph traversal.
- **Prevention:** Physical rendering compares each snapshot's host timestamp
  with the already-known DAC block time and retains the prior snapshot when a
  control is not yet eligible.

### S-255: Publication revision and positioning revision were conflated

- **Mistake:** `Play` while already playing changed the publication sequence,
  and every new sequence would reset the converter even when only the loop flag
  changed.
- **Prevention:** A general publication revision is distinct from the anchor's
  positioning revision. Only a real Play run or Seek changes positioning.

### S-256: Playing Seek split audio and judgement

- **Mistake:** Seek changed physical/cursor projection while the bound stage
  kept its old Play anchor, allowing song and judgement coordinates to diverge.
- **Prevention:** A positioning-revision change on the bound gameplay BGM is a
  fatal stage-contract violation. Non-stage and non-BGM Seek remain ordinary.

### S-257: Attempt-owned callback routing had a retry lifetime hole

- **Mistake:** The active callback pointer and wake event were described as
  attempt-owned even though callbacks and controller waits span teardown and
  replacement attempts.
- **Prevention:** One controller-lifetime callback runtime and wake event serve
  every attempt. Attempt data are installed only while callback admission is
  disabled and quiescent.

### S-258: Fatal and Shutdown precedence was contradictory

- **Mistake:** Different sections allowed intentional shutdown to hide an
  already-published structural fatality.
- **Prevention:** Fatal always wins. Shutdown only prevents a replacement
  attempt after teardown.

### S-259: Recovery logging contradicted coalescing

- **Mistake:** Per-request lifecycle records were required even though sticky
  Restart coalesces multiple notifications into one episode.
- **Prevention:** Emit one recovery-episode record with aggregate selector and
  attempt counts; never reconstruct a request order.

### S-260: An unobservable release error was specified

- **Mistake:** The contract branched on a driver-release error even though the
  local wrapper exposes only returning versus not returning.
- **Prevention:** Specify only observable stop/dispose results and whether the
  final exit-equivalent release returns.

### S-261: Logical-control fatality had no named production owner

- **Mistake:** QPC/publication arithmetic failures were called fatal without
  naming the existing synchronous DirectSound invariant-fatal path.
- **Prevention:** Logical control failures use the existing synchronous
  `ExactInvariantFatal` owner; callback/runtime failures use the ASIO fatal
  reporter.

### S-262: Runtime evidence was asked to prove static properties

- **Mistake:** Aggregate logs and a profile were treated as proof of
  uninterrupted callbacks, absence of hidden operands, and finite recovery
  elapsed time.
- **Prevention:** Source tracing proves operands and call paths; a profile proves
  only sampled CPU/blocking; one retained log proves only that run; the user
  decides audible and gameplay acceptance.

### S-263: The deletion set omitted `AsioClock`

- **Mistake:** `AsioClock.{h,cpp}` and `AsioClockTracker` remained outside the
  explicit deletion inventory although they belong to the rejected output-frame
  mapping.
- **Prevention:** Delete them and their CMake entries with the other rejected
  bridge components.

### S-264: A raw historical ledger was made design authority

- **Mistake:** Thousands of revision-local prevention clauses, including
  mutually exclusive decisions, were placed in the normative reading order.
- **Prevention:** Keep the ledger only in this archive. The short current suite
  is the sole design authority.

### S-265: Avoiding overengineering became a ban on required physical timing

- **Mistake:** The rejected tree removed every physical time projection even
  though physical audio must remain aligned with an independent logical clock.
- **Prevention:** Allow the smallest mechanism that the requirement actually
  needs: one callback-local block-time projection and absolute PCM sampling.
  Do not expand it into a queue, regression, feedback controller, protocol, or
  cross-thread clock publication.

### S-266: Reference implementations were treated as if they solved our clock boundary

- **Mistake:** Device-master osu/BASS and KeyASIO playback paths were considered
  evidence that ordinary driver-paced output also aligns an independent QPC
  judgement clock.
- **Prevention:** Use them only for conventional ASIO session/callback shape.
  GCLoader's external logical-clock boundary must be solved from the SDK timing
  contract and its own ownership requirements.

### S-267: Admission-disabled callbacks were incorrectly fatal

- **Mistake:** The first replacement draft made `admission == false` fatal even
  though the controller deliberately disables admission during setup and
  teardown, when a late driver callback must not touch attempt views.
- **Prevention:** Admission false is a bounded no-view return. Invalid views are
  fatal only while admission is true.

### S-268: The multimedia-timer quantum was hard-coded

- **Mistake:** The physical mapper assumed a one-millisecond `timeGetTime`
  quantum while simultaneously removing timer-resolution ownership.
- **Prevention:** Query `timeGetDevCaps`, make one balanced minimum-period
  request for the backend lifetime, log the requested supported period, and use
  it as the ordinary mapper uncertainty rather than assuming it silently.

### S-269: Logical snapshots carried a redundant multimedia timestamp

- **Mistake:** The first replacement draft added `Mc/Mplay` to logical control
  state even though the callback can translate the ASIO timestamp directly into
  QPC using a local current `{QPC, timeGetTime}` observation.
- **Prevention:** Logical snapshots and judgement remain QPC-only. The Windows
  clock-domain conversion exists solely inside the physical callback.

### S-270: Absolute voices could disappear from traversal at natural end

- **Mistake:** The direct sampler was specified without saying how an ended
  miniaudio node remains discoverable for a later replay.
- **Prevention:** ASIO facade voices remain attached and traversal-enabled for
  object lifetime; the logical snapshot alone controls silence/PCM.

### S-271: Stop and Seek left natural-end restart state ambiguous

- **Mistake:** The first replacement draft did not say that Seek clears natural
  end or that Stop preserves whether its QPC projection had naturally ended.
- **Prevention:** Those transitions now state both results explicitly so replay
  from natural end and ordinary stopped replay cannot be conflated.

### S-272: A successful timer-resolution request was treated as a guarantee

- **Mistake:** The draft called the requested WinMM period "admitted" and used
  it as proof of actual precision, ignoring Windows 11's documented right to
  disregard the request for a fully occluded window-owning process.
- **Prevention:** Treat `R` as a requested supported period only. Physical
  sample-position prediction carries continuity; runtime acceptance must prove
  stable foreground precision and recovery after background use. No focus
  event is introduced.

### S-273: Timer capability discovery added policy without improving the contract

- **Mistake:** The correction to S-268 introduced `timeGetDevCaps`, a variable
  period `R`, and new logging even though the existing process already owns a
  checked one-millisecond request and the physical mapper still must tolerate
  Windows delivering coarser timestamps.
- **Prevention:** Keep one checked, controller-lifetime
  `timeBeginPeriod(1)` / `timeEndPeriod(1)` pair, describe one millisecond only
  as the ordinary foreground uncertainty, and let sample-position prediction
  preserve continuity when the timestamp is coarser. S-268 and S-272 record
  rejected intermediate reasoning; this later entry supersedes their proposed
  variable-period remedy.

### S-274: Self-audit tried to relax the immutable driver-rate contract

- **Mistake:** The self-audit proposed admitting a new sample rate during
  recovery even though the agreed contract fixes the driver's initially
  admitted current rate for the backend lifetime and treats later instability
  as fatal.
- **Prevention:** Use the driver's current rate during initial acquisition,
  then make `sampleRateDidChange`, time-info `kSampleRateChanged`, and a
  different reacquired rate structural fatality. Do not resize or reconfigure a
  running backend.

### S-275: Callback admission was checked after the one-flight guard

- **Mistake:** A callback arriving during intentional cleanup could observe the
  controller-held guard and be reported as reentrancy before observing that
  admission was disabled.
- **Prevention:** Check admission before the callback guard attempt, recheck it
  if the guard is busy, and check once more after successful acquisition. Only
  a busy guard while admission remains true is concurrent-render fatality.

### S-276: Shutdown cleanup failure was allowed to bypass Fatal precedence

- **Mistake:** One paragraph assigned cleanup failure during intentional
  shutdown to a vague shutdown reporter even though the suite says Fatal always
  outranks Shutdown.
- **Prevention:** Cleanup failure publishes and reports Fatal on shutdown too;
  Shutdown only prevents construction of a replacement session.

### S-277: Physical cleanup was given an unnecessary completion shape

- **Mistake:** The cleanup sequence acquired and retained the render guard
  before `stop`, which made rare session disposal look like audio work that had
  to be completed rather than discarded.
- **Prevention:** Close admission first, discard uncommitted callback scratch,
  call `stop`, and wait for an already-executing callback only before its views
  are destroyed. That wait is object-lifetime safety, not audio draining,
  ordering, or acknowledgement.

### S-278: Lifetime quiescence was misclassified as audio completion

- **Mistake:** S-277 briefly moved `stop` ahead of the guard even though a
  legacy callback can call `ASIOGetSamplePosition` and callback code can still
  use the same IASIO attempt. That would permit a lifecycle call to overlap
  callback use merely to avoid the appearance of draining audio.
- **Prevention:** Close admission, let executing callback code discard and
  leave, then hold the one-flight guard across `stop`, disposal, view clearing,
  and release. This is necessary IASIO/object-lifetime exclusion, not an audio
  completion contract. S-278 supersedes S-277's proposed ordering.

### S-279: Legacy timestamp acquisition preceded callback lifetime gates

- **Mistake:** The legacy entry was told to call `ASIOGetSamplePosition` before
  hazard, admission, and one-flight checks, so a late dropped callback could
  still enter IASIO during cleanup.
- **Prevention:** Both callback styles enter the common lifetime gates first.
  Only an admitted, guarded legacy callback calls `ASIOGetSamplePosition`.

### S-280: Time-info actions preceded index and view validation

- **Mistake:** The prose said a rate/clock flag immediately cleared the indexed
  half before the common path had validated that index and its bound view.
- **Prevention:** Validate the index, view, and immutable format first; then
  copy/validate the current-block pair and handle its flags using only the
  already-validated half.

### S-281: The spec forbade an irrelevant private engine-counter advance

- **Mistake:** It said ASIO mode would not advance miniaudio engine time even
  though reusing the established node graph for traversal may advance that
  private counter as an implementation detail.
- **Prevention:** Forbid ASIO voices from reading engine time as a source
  coordinate; do not require removal of an unused counter when doing so would
  force a new voice registry. The graph remains traversal and summation only.

### S-282: Absolute sampling left expensive arithmetic inside the frame loop

- **Mistake:** The formula was called checked rational arithmetic without
  requiring its invariant divisions to be hoisted, leaving room for per-voice,
  per-frame division in the real-time callback.
- **Prevention:** Compute checked `source(0)` and constant `Fs / Fd` once per
  voice per block. The frame loop only advances fixed point, decodes,
  interpolates, gains, and accumulates.

### S-283: The scalar snapshot carried two general publication counters

- **Mistake:** The snapshot listed a general publication revision in addition
  to the odd/even publication sequence even though no consumer needed both.
- **Prevention:** The sequence identifies scalar publication; the separate
  positioning revision exists only to detect a changed `{Qc, Sc}` anchor.
  Do not add another general counter.

### S-284: Negative pre-Play judgement time risked unsigned underflow

- **Mistake:** The design allowed input before the selected Play anchor but did
  not explicitly require `q - Qplay` to be evaluated as a checked signed tick
  difference.
- **Prevention:** Preserve negative pre-Play song coordinates with signed
  checked arithmetic; never wrap them through an unsigned subtraction.

### S-285: Cursor query time could precede the snapshot it projected

- **Mistake:** The cursor equation required `q >= Qc` without specifying that
  an asynchronous reader accepts its complete snapshot before capturing `q`.
  Capturing time first could race a later control publication and pair a future
  anchor with an earlier query.
- **Prevention:** Read one stable snapshot, then capture query QPC. Treat any
  resulting `q < Qc` as exact invariant failure.

### S-286: Stop was conflated with an in-run relocation

- **Mistake:** The revision was said to change whenever `{Qc, Sc}` changed.
  Stop must freeze a new cursor anchor, so that wording would make ordinary
  Stop look like a Seek and could falsely fail a bound stage.
- **Prevention:** A relocation revision changes only on
  `SetCurrentPosition`. New Play is identified by run id; Stop and natural end
  preserve the selected run anchor. Stage failure checks buffer/run identity
  and relocation revision separately.

### S-287: Repeated Restart wakes could collapse retry intervals

- **Mistake:** The delay used the shared wake event without saying that a
  Restart-only wake resumes the remaining delay, so coalesced notifications
  could turn delayed retries into immediate attempts.
- **Prevention:** Give each retry delay one monotonic policy due time. Fatal or
  Shutdown interrupts it; Restart only coalesces. The due time never diagnoses
  driver loss or recovery.

### S-288: Every stopped voice was left on the callback traversal path

- **Mistake:** The replacement text kept every ASIO facade voice
  traversal-enabled for object lifetime to avoid a natural-end replay race.
  That would make all ordinary stopped buffers consume callback work.
- **Prevention:** Keep nodes attached, let Play enable and explicit Stop disable
  traversal, and never let the callback disable a node at projected natural
  end. Naturally ended voices emit silence until Play/Stop/destruction without
  a callback-versus-control node-state race or a new voice registry.

### S-289: Valid ASIO rate and speed fields were ignored

- **Mistake:** The callback checked only the rate/clock change flags even though
  time-info can explicitly mark its current sample-rate and speed fields valid.
  A changed rate or varispeed would invalidate the fixed `Fs / Fd` sampler.
- **Prevention:** When valid, require the time-info rate to equal immutable
  `Fd` and speed to be finite nominal `1.0`; otherwise publish structural
  Fatal rather than rendering on a false physical-time contract.

### S-290: Stage binding exposed unused cursor and loop fields

- **Mistake:** The scoped BGM observation listed current projected cursor and
  looping even though binding consumes only buffer/run identity, immutable Play
  anchor, relocation revision, and projected playing state.
- **Prevention:** Keep the observation to fields with a named binding or
  validity consumer. Ordinary cursor/status queries remain file 01's separate
  DirectSound projection.

### S-291: Output latency was both immutable and re-queried on recovery

- **Mistake:** `Od` appeared in the immutable backend-format list even though
  latency-change recovery must query and use the replacement attempt's reported
  output latency.
- **Prevention:** Freeze `Fd`, `N`, channel selection/sample types, and
  capacities. Treat non-negative `Od` as per-attempt data and apply it exactly
  once in that attempt's physical DAC coordinate.

### S-292: Callback requirements lacked an explicit terminal branch

- **Mistake:** The numbered render path said timestamp/render/conversion values
  were required but did not explicitly stop the path, latch Fatal, and wake the
  controller when a requirement failed.
- **Prevention:** Every failed callback requirement immediately latches the
  first Fatal code, safely clears only an already-validated half, wakes the
  controller, and executes no later render/output step.

### S-293: Callback absence wording denied logical QPC advancement

- **Mistake:** The physical-output section said no audio state advances while
  callbacks are absent, contradicting the central rule that logical
  DirectSound time continues from QPC.
- **Prevention:** Only physical render-cursor/feed state is absent. Logical
  cursor, song time, input, and judgement continue independently.

### S-294: “No output resampler” could prohibit required source conversion

- **Mistake:** A deletion list used unqualified “output resampler” even though
  direct per-voice `Fs / Fd` linear interpolation is necessary when source and
  driver rates differ.
- **Prevention:** Prohibit a global feedback/correction stream, not the bounded
  per-voice format conversion in the absolute sampler.

### S-295: The outcome list omitted orderly Shutdown

- **Mistake:** The failure section claimed exactly three production outcomes
  while its own precedence and controller loop also specified normal final
  Shutdown.
- **Prevention:** Name orderly Shutdown as the fourth outcome; it prevents
  replacement but does not erase an already-published Fatal.

### S-296: One fatal owner conflicted with later cleanup reporting

- **Mistake:** The text promised exactly one runtime fail-stop invocation and
  also said every later cleanup failure was “reported,” which could imply
  invoking that fail-stop more than once.
- **Prevention:** Invoke the fail-stop once for the first Fatal. If it returns,
  record later cleanup failures immediately as bounded secondary diagnostics;
  if cleanup creates the first Fatal, invoke the same owner at that point.

### S-297: Startup logging claimed a callback style before any callback

- **Mistake:** The startup record was asked to say whether time-info or legacy
  timestamp acquisition was active even though startup deliberately does not
  wait for a callback and therefore cannot observe which entry the driver uses.
- **Prevention:** Startup records only that the host advertised time-info with
  legacy fallback. Final/fatal aggregate counts report the callback styles
  actually observed.

### S-298: “Every control” contradicted independent gain and PCM publication

- **Mistake:** Static proof said every accepted control publishes the scalar
  snapshot even though gain and PCM deliberately retain their established
  independent publication owners.
- **Prevention:** Require one scalar publication for state-changing
  Play/Seek/Stop/loop controls and separately preserve PCM and gain boundaries.

### S-299: The summary still called sample-rate change recoverable

- **Mistake:** After the detailed contract made sample-rate change fatal, the
  top-level design still grouped “rate” with SDK notifications that trigger
  recovery.
- **Prevention:** Recovery is limited to reset, resync, clock-source, and
  latency notifications. The initially admitted driver rate is immutable and
  every later rate change is Fatal.

### S-300: `outputReady` was probed after freezing output latency

- **Mistake:** Acquisition queried `ASIOGetLatencies` before the first
  `outputReady` probe even though the SDK permits that first call to reduce
  output latency by one block.
- **Prevention:** Clear the created buffers, probe `outputReady`, and only then
  query the authoritative per-attempt output latency used by physical mapping.

### S-301: A single-reader runtime hazard dropped terminal notifications

- **Mistake:** Reusing `AudioSnapshot`'s single-reader hazard for every ASIO
  callback meant a notification concurrent with rendering could not acquire the
  runtime and could silently lose Restart or sample-rate Fatal.
- **Prevention:** Give the callback gateway/runtime DLL-lifetime static storage.
  Notification callbacks always publish directly; only rendering uses
  admission and the one-flight guard.

### S-302: The physical measurement clamp had no valid timer bound

- **Mistake:** The draft named undefined `measurement_lower/upper` bounds and
  assumed one-millisecond precision even though Windows 11 may ignore that
  timer request while the process is occluded.
- **Prevention:** Before ASIO activation, use the documented process policy that
  keeps timer-resolution requests honored, require `timeBeginPeriod(1)`, and
  derive the exact `[L,U]` interval with quantization from both readings. An
  incompatible prediction is Fatal rather than an audio jump.

### S-303: Gain was assigned to two render owners

- **Mistake:** The absolute sampler manually applied the independently
  published gain while the preserved miniaudio node graph also retained its
  node-bus gain behavior.
- **Prevention:** The node bus remains the sole gain owner and applies it once.
  Absolute source sampling performs no gain multiplication.

### S-304: The logical seqlock lacked a C++ memory contract

- **Mistake:** Equal acquire reads of a sequence counter were claimed to protect
  ordinary scalar payload fields, leaving a C++ data race and no proof against
  mixed revisions.
- **Prevention:** Make mutable sequence and payload fields atomic and use
  sequential consistency for the writer's odd/payload/even publication and the
  reader's sequence/payload/sequence observation.

### S-305: The archive still claimed normative authority

- **Mistake:** The archive header and old “final gates” claimed authority despite
  the suite excluding the entire directory.
- **Prevention:** Only the five current normative files are authoritative.
  Every statement in this archive is historical, including old supersession and
  non-recurrence language.

### S-306: Running and terminal publication lacked one atomic commit point

- **Mistake:** Running was conceptual state outside the Restart/Fatal word, so a
  callback could publish Fatal after the controller's check but before a
  separate startup-success publication.
- **Prevention:** The attempt word starts at `Pending`; controller startup uses
  one `Pending -> Running` compare/exchange while callbacks publish Restart or
  Fatal into that same atomic modification order.

### S-307: Ordinary native BGM correction Seek was made judgement-fatal

- **Mistake:** The draft invalidated a bound stage when the selected buffer's
  relocation revision changed, although the traced Tune watchdog legitimately
  seeks BGM without moving the gameplay timeline.
- **Prevention:** Copy one immutable stage anchor. Later Seek, Play, Stop,
  identity, and revision changes may be audio diagnostics but cannot rebind,
  invalidate, or delay judgement.

### S-308: A loop-flag change reinterpreted an old unwrapped cursor

- **Mistake:** Play-while-playing changed a looping flag without reanchoring. A
  multi-wrap looping raw cursor could therefore become an immediately ended
  non-looping cursor.
- **Prevention:** When the requested flag differs, project the old public cursor
  and reanchor `{Qc,Sc}` there while preserving run id, immutable stage anchor,
  and relocation revision.

### S-309: Acceptance was asked to prove internal implementation absence

- **Mistake:** User acceptance was said to prove no periodic internal work and
  the residual error was limited by a requested timer precision that had not
  been established.
- **Prevention:** Static source review proves scheduled-work absence, profiling
  attributes sampled hitches, retained logs report measurements, and user
  acceptance covers only observable audio/timing/frame behavior.

### S-310: Exceptional teardown retained unnecessary audio semantics

- **Mistake:** Teardown language risked treating scratch, tails, or already
  prepared work as continuity state that cleanup should preserve.
- **Prevention:** Teardown is drop-only. Close admission, discard uncommitted
  scratch, wait solely for code/memory lifetime safety, then stop, dispose,
  clear, and release. It never drains or replays audio.

### S-311: Adjacent-block continuity was applied across a proved callback gap

- **Mistake:** The corrected projector required one nominal-rate interval
  intersection even when sample position advanced by multiple buffers. A long
  valid gap could then accumulate harmless device/QPC rate difference and be
  misclassified as structural instability.
- **Prevention:** Require interval consistency only for `dP == N`. For a larger
  block-aligned advance, drop the old physical projection and select current
  absolute content directly from the new measurement interval; do not replay or
  trigger lifecycle recovery.

### S-312: The persistent timer policy was described as having no later effect

- **Mistake:** The draft said the process power-throttling policy had no effect
  after ASIO balanced its own timer request, although the policy governs every
  timer-resolution request made by that process.
- **Prevention:** State the process-wide lifetime honestly. The policy remains
  until exit, controls only whether timer requests are honored, and never
  enables execution-speed throttling.

### S-313: Static callback storage was still described as published

- **Mistake:** After removing the active runtime pointer, the acquisition list
  still said initial construction “publishes” the runtime.
- **Prevention:** The static runtime is always addressable. Initial construction
  only claims/resets it and installs its wake handle while no driver exists.

### S-314: Fatal was described as a consumable callback bit

- **Mistake:** Notification text grouped Restart and Fatal as sticky only until
  controller consumption even though the backend must never clear Fatal.
- **Prevention:** Restart may be consumed into a clean replacement attempt;
  Fatal remains sticky for the entire backend lifetime and dominates every
  other outcome.

### S-315: A first overlapping logical read had no retained snapshot

- **Mistake:** The reader was told to retain its last complete snapshot on a
  concurrent write without specifying how that cache exists before its first
  callback read.
- **Prevention:** Publish the initial stopped snapshot and seed the physical
  voice's cache from it before the node can be enabled or exposed to rendering.

### S-316: Loop changes bypassed their reanchored snapshot admission

- **Mistake:** Physical rendering grouped “loop-only” changes with independent
  gain/PCM publication even after a loop-flag change was corrected to reanchor
  `{Qc,Sc}` in the logical snapshot.
- **Prevention:** Loop state always travels with and obeys the complete logical
  snapshot's `Qc` admission. Only established gain and PCM publications remain
  independent.

## 2026-08-31 frozen-tree review failures: tree `1087cef7`

Both independent reviewers rejected staged tree
`1087cef710304f77ad9a7c8a44a3bd16de28b182`. Aquinas reviewed specification
consistency and source/SDK contracts. Goodall independently reviewed
verification policy, arithmetic, source/SDK contracts, and the same complete
staged tree. Both verified the tree hash before and after review and made no
edits.

### S-317: The ASIO Tune path was not bound to the immutable stage anchor

- **Mistake:** The suite claimed that one QPC `Play` anchor owned song and
  judgement time but defined the equation only at the judgement input boundary.
  The existing ASIO `GameplaySongClock` could still consume mutable cursor,
  revision, and playback-generation state.
- **Prevention:** The ASIO Tune/song-clock coordinate and judgement coordinate
  must be named outputs of the same immutable stage equation. Later physical
  work and later audio controls cannot become Tune operands.

### S-318: An unbound semantic stage had no terminal outcome

- **Mistake:** Stage binding waited for the first qualifying selected-BGM Play
  but did not define what happens if a stage-owned Tune or judgement operation
  occurs before that observation, or if the stage exits unbound.
- **Prevention:** The first stage-owned operation requiring the anchor is the
  fail-stop boundary. If no such operation occurs, semantic stage exit verifies
  that the anchor was bound. No timeout, fallback clock, or ASIO state is used.

### S-319: The static callback route had no mixer ownership contract

- **Mistake:** The callback runtime was made permanently addressable but had no
  specified stable route to the render target. Adding an implementation-only
  raw pointer would recreate a hidden teardown race.
- **Prevention:** The ASIO backend-lifetime callback gateway and render target
  are constructed once, retained across physical recovery, and destroyed only
  after final driver stop, buffer disposal, and driver release return.

### S-320: Terminal publication could race a driver-buffer commit

- **Mistake:** Restart or Fatal could be published after the callback's only
  terminal check while the callback still wrote the driver half and called
  `outputReady`.
- **Prevention:** The later zero-coupling correction removes terminal gating
  from ordinary render commits. A valid callback already in progress completes;
  the sole controller subsequently stops the driver. Invalid indexed callbacks
  do not write. No claim of atomic revocation is made.

### S-321: A pre-commit Restart consumed an attempt without a successor

- **Mistake:** A recovery acquisition invalidated by a synchronous Restart was
  said to consume an attempt but had no defined next delay or exhaustion result.
- **Prevention:** Such an invalidation is one failed attempt in the existing
  immediate/one-second/two-second recovery episode. It proceeds to the next
  scheduled attempt or becomes fatal after the third; it creates no new state.

### S-322: Reacquired sample-rate mismatch was both retryable and fatal

- **Mistake:** One clause classified format-query failure as retryable while
  another made any post-admission rate mismatch immediately fatal.
- **Prevention:** A returned rate different from the initially admitted driver
  rate is structural Fatal and is never retried. Ordinary acquisition-call
  failures may consume the bounded recovery attempts.

### S-323: Buffer-half parity was not validated

- **Mistake:** The callback accepted any in-range index and block-aligned sample
  position without requiring the SDK's initial zero position/index or matching
  subsequent double-buffer parity.
- **Prevention:** If sample position is retained as a structural diagnostic,
  validate initial zero and buffer parity. It never selects PCM, logical time,
  recovery alignment, or judgement.

### S-324: Naturally ended voices accumulated permanent callback work

- **Mistake:** To avoid an end-versus-replay race, the suite left every
  naturally ended voice traversable for object lifetime. Callback work could
  therefore grow without bound across menus, songs, and credits.
- **Prevention:** Natural-end traversal ownership belongs to the logical mixer,
  not the ASIO lifecycle specification. Preserve or restore the mixer's bounded
  node-lifecycle rule; do not create an ASIO acknowledgement protocol.

### S-325: A Windows 11 timer override silently removed Windows 10 support

- **Mistake:** The suite required a Windows 11-specific power-throttling control
  to succeed on every supported host, making Windows 10 startup fail despite the
  established Windows 10+ support floor.
- **Prevention:** The final zero-coupling correction deletes multimedia-timer
  use and the process policy entirely. There is no version branch to maintain.

### S-326: The selected physical coordinate could move backwards

- **Mistake:** Clamping a midpoint to the overlap of predicted and measured
  intervals did not require `Qblock > Qprev`. Valid one-millisecond intervals
  could therefore select a later ASIO block at an earlier QPC coordinate and
  repeat source PCM.
- **Prevention:** Reject the point-projection architecture. ASIO does not select
  a logical coordinate from physical timestamps.

### S-327: The exact timer interval depended on a nonexistent oracle

- **Mistake:** The suite asserted a one-sided one-millisecond truth interval for
  `timeGetTime` and interpolated driver `systemTime`, although neither Microsoft
  nor the ASIO SDK provides that polarity or absolute error contract.
- **Prevention:** Delete the timer interval and every exactness claim built on
  it. Do not manufacture a calibration or policy-copying test as an oracle.

### S-328: Immediate node disabling violated admitted snapshot semantics

- **Mistake:** A racing Stop could disable a node after a callback captured an
  earlier logical block, causing that earlier block to become silent despite
  the claimed snapshot rule.
- **Prevention:** Do not make ASIO own logical control admission or node
  lifetime. The logical mixer owns the complete control/render contract before
  PCM reaches the ASIO boundary.

### S-329: Callback clock-query requirements contradicted each other

- **Mistake:** One file prohibited callback clock queries while another
  required QPC, `timeGetTime`, and QPC on every block.
- **Prevention:** The ASIO callback performs no host-clock or logical-clock
  query. Any logical renderer clock use is internal to that renderer and is not
  an ASIO operand.

### S-330: Recovery alignment was an invented cross-layer problem

- **Mistake:** A private render tail advanced only on physical callback commit.
  After interruption it lagged logical time, so the design introduced phase
  matching, resampling, timestamp projection, and alignment to reconcile two
  authorities.
- **Prevention:** ASIO transports the PCM supplied by the logical renderer. It
  never owns, reconciles, rebases, or requests a logical coordinate. Recovery
  replaces only the physical driver session and buffers.

### S-331: ASIO `systemTime` resolution was confused with audio resolution

- **Mistake:** The coarse or implementation-dependent Windows-correlated
  `systemTime` field was treated as if it limited ASIO's sample accuracy, then a
  timer policy was added to repair that false premise.
- **Prevention:** Driver sample transport remains sample-based. `systemTime` is
  optional timing metadata and is ignored by the PCM transport contract.

### S-332: A partial baseline was called a working implementation

- **Mistake:** The pre-recovery ASIO path was described as the last working
  implementation even though it worked only without interruption and failed a
  required lifecycle contract.
- **Prevention:** It is evidence only for uninterrupted playback behavior. No
  repository revision is accepted as a complete recovery architecture.

### S-333: ASIO was still allowed to request a logical presentation point

- **Mistake:** Even after claiming zero coupling, the proposed callback asked
  the logical renderer for PCM at `QPC now + output latency`. This retained an
  ASIO-to-logical time input and made recovery appear to need re-alignment.
- **Prevention:** The ASIO callback passes only the configured frame count to
  the established PCM-render boundary. It supplies no time, latency, sample
  position, generation, recovery, or presentation coordinate.

### S-334: Callback lateness was incorrectly made a lifecycle failure

- **Mistake:** Deadline measurements, phase envelopes, underrun inference, or
  insufficient CPU budget could trigger recovery or Fatal even though no such
  ASIO host contract exists.
- **Prevention:** Assume configured callback work completes. If the selected
  buffer is too small for the machine, audio may underrun or become abnormal
  while the session continues. The player manually selects a larger buffer.
  Time and workload measurements are diagnostic only and never change state.

### S-335: Output latency was given authority over PCM selection

- **Mistake:** Reported ASIO output latency was added to a logical coordinate to
  choose source content, coupling physical buffering to logical audio and
  judgement behavior.
- **Prevention:** Output latency may be reported diagnostically but is never an
  operand in logical time, PCM selection, Tune, cursor projection, or
  judgement. Existing configuration offsets retain their separately traced
  meanings.

### S-336: The simplified replacement was split into a suite again

- **Mistake:** After removing the clock bridge and most lifecycle machinery, I
  still recreated five normative files plus a README. The decomposition no
  longer represented independent complex owners and increased the review and
  contradiction surface.
- **Prevention:** This design has one normative specification file. The failure
  ledger remains a separate non-normative historical record only.

### S-337: `directProcess == ASIOFalse` was misclassified as driver failure

- **Mistake:** The first consolidated draft treated the SDK's advisory request
  to defer processing as a structural Fatal merely because this host does not
  want another worker.
- **Prevention:** Both `directProcess` values use the same simple inline render
  path. The host adds no worker. If inline work is unsuitable for the selected
  buffer/driver, audio may be abnormal and the player changes configuration;
  the advisory value does not change lifecycle state.

### S-338: Callback duration survived as an unnecessary diagnostic

- **Mistake:** The consolidated draft removed workload-driven recovery but
  still allowed a retained maximum callback-duration counter, preserving a
  timer mechanism with no authorized behavioral consumer.
- **Prevention:** The callback performs no duration measurement. The driver's
  explicit overload notification may increment one aggregate counter, but time
  is neither diagnostic evidence nor lifecycle evidence in ASIO transport.

### S-339: The frozen driver rate was both required and forbidden

- **Mistake:** The consolidated hard-invariant list called ASIO sample rate a
  forbidden PCM-selection operand while the format section correctly required
  the driver's initial rate to configure the renderer output format.
- **Prevention:** Frozen rate, frame count, channel layout, and sample type are
  format parameters only. ASIO timing/progress values and later format changes
  are forbidden logical-coordinate inputs. The initial rate never owns time.

### S-340: Startup signal classification lacked a linearization point

- **Mistake:** The one-file draft said “if no signal is pending, publish
  Running” without defining whether a concurrent notification belongs to the
  startup attempt or runtime, inviting another false atomic-commit protocol.
- **Prevention:** The controller's Running publication is the classification
  point. Earlier observed sticky signals reject the attempt; later signals are
  runtime work. Callbacks never gate PCM on Running, and the sticky word/event
  prevents loss in either classification.

### S-341: Zero coupling was stated as an impossible PCM contract

- **Mistake:** The frozen specification required callback cadence and absence
  never to affect per-block PCM selection, required the callback to be the only
  caller of a pull renderer, forbade an independent producer, and also forbade
  replay after callback absence. No implementation can satisfy all four rules.
- **Prevention:** Separate logical time from physical PCM sequencing. QPC owns
  cursor, Tune, input, and judgement. A running ASIO callback advances only a
  private sequential physical render cursor. A successful replacement reseeds
  that physical cursor once from current logical state before `ASIOStart`; it
  never changes or feeds back into logical state.

### S-342: `ASIOStop` was mistaken for a callback join

- **Mistake:** The frozen specification disposed driver buffers immediately
  after `ASIOStop` returned. The SDK prevents later callback entry but does not
  state that a callback already inside host code has returned.
- **Prevention:** After a successful Stop, retain the driver buffers, views,
  route, and callback target until the single in-flight callback has left. This
  teardown-only lifetime wait has no timeout and no audio/timeline meaning. A
  Stop failure hard-crashes immediately and performs no further cleanup.

### S-343: Fatal was incorrectly modeled as a cleanup state

- **Mistake:** The frozen specification allowed the fail-stop observer to
  return and then performed Stop, disposal, release, route clearing, and later
  state transitions. It even downgraded cleanup failures to diagnostics.
- **Prevention:** Every Fatal source enters the same non-returning hard-crash
  sink. Fatal performs no ASIO cleanup and has no successor state. Cleanup is
  only for orderly shutdown or a replacement that remains recoverable; failure
  of any cleanup operation hard-crashes at that operation without continuing.

### S-344: Startup exposure and concurrent signals had no total rule

- **Mistake:** A separate signal check and Running store allowed Recover or
  Shutdown to arrive between them, so an attempt could be exposed after the
  condition that supposedly rejected it.
- **Prevention:** Running is diagnostic, not an exposure or PCM-admission gate.
  The persistent facade/renderer exists independently. Immediately after Start,
  the controller atomically takes the pending Recover bit for classification;
  a bit taken there consumes that attempt, while a later bit is runtime work.
  Shutdown is checked before any replacement and before returning startup.

### S-345: The no-timer rule contradicted fixed retry delays

- **Mistake:** The ASIO controller was said to own no timer policy while it also
  owned one-second and two-second recovery backoff waits.
- **Prevention:** Prohibit elapsed time from diagnosing loss or selecting any
  logical/PCM coordinate. Fixed interruptible retry backoff is allowed solely
  as lifecycle scheduling and has no timing evidentiary meaning.

### S-346: Driver-owned sample rate stopped at the ConfigGUI boundary

- **Mistake:** The specification admitted a driver-owned 44.1 kHz rate while
  the current ConfigGUI/probe still rejects anything except 48 kHz and formats
  durations with a hard-coded 48 kHz divisor.
- **Prevention:** Include the GUI inspection, validation, and display paths in
  implementation scope. They accept the inspected integral supported rate,
  never require or set 48 kHz, and derive time labels from that inspected rate.

### S-347: A declared ASIO sample-rate change could escape Fatal

- **Mistake:** The specification required a valid numeric rate different from
  the frozen rate before treating `kSampleRateChanged` as Fatal. The flag itself
  declares a change and is not merely a value-validity bit.
- **Prevention:** Any observed `kSampleRateChanged` flag is immediately Fatal.
  Any numeric rate collected safely is diagnostic detail only.

### S-348: Seek after natural end could restart without Play

- **Mistake:** `SetCurrentPosition` was said to preserve playing state without
  first projecting natural end. It could therefore reanchor an internally
  stale playing flag and restart output, or preserve a restart-at-zero marker
  that discarded the requested position on the next Play.
- **Prevention:** Project effective state first. An active seek remains playing.
  A seek after natural end stores the requested frame, remains stopped, and
  clears restart-at-zero state so the next genuine Play begins there.

### S-349: Retry proof omitted pre-Running invalidations

- **Mistake:** Lifecycle text made a pre-Running Recover consume an attempt,
  while static verification claimed only ordinary acquisition failures did.
- **Prevention:** Both ordinary recover-acquisition failures and Recover bits
  atomically taken before Running consume the three-attempt episode. Structural
  Fatal does not consume an attempt because it never returns.

### S-350: `directProcess == ASIOFalse` was ignored despite SDK advice

- **Mistake:** The host always performed substantial mixing inline even when
  the driver explicitly advised deferring work from its low-level callback.
  That can itself create timing instability.
- **Prevention:** The supported host contract is explicit: direct processing is
  required. Receiving `ASIOFalse` is an unsupported structural condition and
  hard-crashes. Do not add a deferred-worker protocol merely to broaden driver
  compatibility.

### S-351: Required stage anchoring had no always-installed owner

- **Mistake:** ASIO could be configured while absolute judgement was disabled,
  but the only semantic stage hooks that created the mandatory anchor were then
  not installed.
- **Prevention:** ASIO configuration requires absolute-time judgement. Reject
  that invalid combination at startup/GUI validation instead of adding another
  stage-hook owner.

### S-352: Gain both did and did not belong to a whole control revision

- **Mistake:** The specification required every scalar control to participate
  in one whole-revision snapshot while retaining gain on its independent node
  bus, as the current source does.
- **Prevention:** Explicitly exclude gain from the scalar timeline revision.
  Gain retains its independent atomic/node-bus linearization and never affects
  cursor, run anchors, Tune, or judgement.

### S-353: Latency change caused recovery without a latency consumer

- **Mistake:** The host advertised and recovered from `kAsioLatenciesChanged`
  while intentionally never querying or consuming ASIO latency. A driver could
  therefore cause pointless replacement after each Start.
- **Prevention:** Do not advertise or act on latency-change support. Return `0`
  for that selector. Latency remains absent from logical and PCM selection.

### S-354: Fatal time-info flags could be skipped by data-path early returns

- **Mistake:** Flag classification was not ordered before index validation or
  the callback one-flight guard, so an overlapping callback could return before
  publishing a mandatory sample-rate-change Fatal.
- **Prevention:** Classify valid notification flags at callback entry before
  every data-path early return. Fatal calls the non-returning crash sink there;
  Recover publication remains a single atomic operation.

### S-355: An explicit driver reset was treated as recoverable focus loss

- **Mistake:** I called a retained atomic a “sticky reset” and designed a
  recovery controller around it without first stating what the ASIO selector
  means. The SDK says `kAsioResetRequest` asks the host to close/reopen after a
  driver configuration change, and `kAsioResyncRequest` says the driver went
  out of sync. Neither is the observed focus path; the retained run reported
  zero reset and resync requests.
- **Prevention:** Focus has no ASIO edge. Under the fixed-format, fixed-session
  contract, Reset, Resync, clock-source change, and sample-rate change are
  immediate non-returning Fatal. There is no reset flag, rebuild, retry, or
  recovery subsystem.

### S-356: A diagnostic Running state created a fictitious startup race

- **Mistake:** I made Running an admission/commit boundary, then needed a
  compare/exchange protocol to decide whether a concurrent notification could
  invalidate startup. PCM and logical state never needed that gate.
- **Prevention:** Startup is one blocking procedural sequence. A successful
  Start returns a live backend; any unsupported notification is already Fatal.
  No callback reads a lifecycle state, and there is no commit race to solve.

### S-357: A second physical timeline recreated the rejected architecture

- **Mistake:** To answer a reviewer contradiction, I invented a private
  callback-driven PCM cursor, a recovery reseed, a physical voice registry, and
  a bookkeeping mutex. This contradicted the controlling rule that the logical
  renderer owns current PCM and ASIO only requests a frame count.
- **Prevention:** `RenderPcm(N)` emits the next sequential mixer frames and
  advances only the mixer's private audio-conversion cursors. It accepts no QPC,
  ASIO timestamp, presentation coordinate, or logical-time operand. There is no
  recovery reseed, registry, or ASIO-owned logical state.

### S-358: Host-side callback teardown proof became another protocol

- **Mistake:** I added an explicit callback-active wait after Stop to compensate
  for speculative driver behavior, even though the supported ASIO lifecycle is
  the teardown boundary and any inability to complete it is Fatal.
- **Prevention:** Do not add a host acknowledgement, wait generation, or
  callback-join protocol. Orderly shutdown uses the supported SDK lifecycle in
  order; the first failing call enters non-returning Fatal and no later cleanup
  runs. Fatal paths themselves perform no cleanup.

### S-359: Explicit overload was allowed to continue

- **Mistake:** I preserved a continue-with-abnormal-audio policy even when the
  driver explicitly published `kAsioOverload`. That ignored authoritative
  evidence that the configured callback budget was not being met.
- **Prevention:** An explicit ASIO overload notification is immediate
  non-returning Fatal. Do not infer overload from duration, silence, callback
  cadence, or CPU measurements; absent the explicit selector, continue the
  ordinary callback contract without a timing diagnosis.

### S-360: Hundreds of secondary defects were treated as repair work

- **Mistake:** The ledger grew beyond 350 entries because each contradiction in
  added recovery, clock, lifecycle, or synchronization machinery prompted more
  machinery instead of deletion. The repeated error pattern was architectural,
  not a collection of isolated edge cases.
- **Prevention:** Removal is the default solution for every defect. Replacement
  logic is prohibited until removal is shown unable to satisfy a concrete
  required behavior. Any unavoidable replacement must be the smallest direct
  behavior and may not introduce a new state machine, synchronization protocol,
  worker, clock, retry loop, lifecycle owner, or compatibility layer without
  explicit proof that it is necessary.

### S-361: A task-local correction rule was written into repository guidance

- **Mistake:** I added the ASIO rewrite's removal-first decision to `AGENTS.md`,
  incorrectly making one task's controlling design rule apply to unrelated
  repository work.
- **Prevention:** Keep this rule in this task's normative specification and
  non-normative failure ledger only. Repository-wide guidance changes require a
  separate explicit user request.

### S-362: Implementation archaeology replaced specification work

- **Mistake:** Before producing one approved, internally consistent
  behavior-level specification, I inspected current and historical source to
  decide which implementation mechanisms to retain. That inverted the required
  order and pulled implementation accidents back into the design.
- **Prevention:** Freeze and review the behavior-level specification first.
  Until the user approves it, do not inspect more source, design around current
  classes, or prescribe internal publication and synchronization structures.
  Source archaeology begins only while writing the later implementation plan.

### S-363: Removal-first was treated as permission to omit focus behavior

- **Mistake:** The rejected tree `7e9353123dc309accaef575e0436cad631181fff`
  said focus did nothing without stating the required exclusive-session
  behavior. That made deletion sound like a shortcut around the focus,
  move/resize, and external-audio cases.
- **Prevention:** Removal-first never waives a requirement. The supported ASIO
  session retains exclusive ownership and continues unchanged across focus,
  move, and resize. Windows notification audio does not acquire that ownership.
  A reported ownership loss is immediate Fatal; there is no recovery.

### S-364: Optional judgement contradicted the required timeline diagram

- **Mistake:** Both fresh reviewers found that the diagram required judgement
  to use the QPC/BGM anchor while later text allowed the configuration option to
  disable that path and introduced an ASIO-conditioned “service.”
- **Prevention:** State the two supported modes directly. Tune always uses the
  QPC/BGM timeline. With absolute judgement enabled, judgement uses it too;
  otherwise native judgement remains unchanged and still receives no ASIO time.
  Do not introduce an ASIO-owned logical-time service.

### S-365: The current stage's accepted BGM Play was ambiguous

- **Mistake:** Both fresh reviewers found no deterministic behavioral rule for
  choosing among stage entry, sound-group-2 selection, and repeated Play calls.
  A present but wrong anchor could silently corrupt the whole stage.
- **Prevention:** Stage entry clears the binding. The first sound-group-2
  observation whose selected buffer is playing from a Play issued after that
  stage entry binds that current Play exactly once. Later observations and
  Plays cannot replace it; stage exit clears it.

### S-366: Ordinary shutdown did not classify every step

- **Mistake:** A fresh reviewer found that the spec made Stop and buffer
  disposal failures Fatal but did not state the contract for driver exit/release
  and callback-route clearing.
- **Prevention:** Every shutdown call that returns a failure is Fatal and stops
  the sequence. A release operation with no failure result is invoked once; only
  its return permits the next direct cleanup step. Infallible local clearing is
  identified as such rather than given an invented error protocol.

### S-367: Offset behavior was not self-contained

- **Mistake:** Both fresh reviewers found that “existing meaning” and
  “already-traced comparison” did not specify where GameTimeOffset and
  JudgTimeOffset enter the judgement operands or prove they are not duplicated.
- **Prevention:** Preserve the previously traced contract explicitly:
  `J(q) = song_time(q) + GameTimeOffset`; the loader supplies `J` to Tune and,
  when enabled, absolute judgement. Native grading alone adds its existing base
  containing JudgTimeOffset, exactly once. The loader never applies, caches, or
  combines JudgTimeOffset.

### S-368: Frame-hitch acceptance used an undefined attribution oracle

- **Mistake:** A fresh reviewer found that “no loader-attributable frame hitch”
  could not be decided by the stated evidence, especially after normal timing
  diagnostics were removed.
- **Prevention:** Runtime acceptance is simply no visible frame hitch. Any
  observed hitch fails that run and is investigated separately with an external
  profiler before a cause is claimed; the spec does not invent an attribution
  signal.

### S-369: The diagram still transformed input in disabled mode

- **Mistake:** A fresh reviewer found that the top-level diagram sent all input
  through the QPC/BGM timeline while the normative mode split converted input
  only when absolute judgement was enabled.
- **Prevention:** Show Tune as the unconditional QPC/BGM consumer, preserve the
  captured input QPC as data, and draw explicit enabled-conversion and
  disabled-native judgement branches.

### S-370: Explicit ASIO faults were scoped only to ordinary running

- **Mistake:** A fresh reviewer found that the listed Fatal notifications were
  called runtime faults without saying what happens if a callback reports one
  during startup or shutdown.
- **Prevention:** From callback-route installation through route clearing, any
  listed explicit notification is immediate Fatal whenever it can arrive. This
  is one direct rule and requires no lifecycle state.

### S-371: Notification-audio continuity was assigned to static proof

- **Mistake:** Both fresh reviewers found that source inspection can prove only
  the absence of a loader Stop/restart edge, not actual target-driver continuity
  while Windows notification audio is attempted. The runtime matrix also
  omitted that case.
- **Prevention:** Static proof checks only loader control flow. User acceptance
  explicitly triggers notification audio while backgrounded and observes either
  uninterrupted ASIO or the allowed Fatal on an explicit loss report.

### S-372: Judgement operands and conversion remained opaque

- **Mistake:** A fresh reviewer found that `native_base_including_JudgTimeOffset`
  did not state the integer conversion, additive sign, or the other native
  operand, so exact offset placement was still not normative.
- **Prevention:** Define `passed_ms = trunc_toward_zero(1000 * J(q))`,
  `judged_ms = passed_ms + native_player_base_ms`, and
  `grade_error_ms = note_target_ms - judged_ms`. The native player lookup
  remains unchanged and contains the live additive JudgTimeOffset; score keeps
  its unchanged audio-group base. The loader adds neither native base.

### S-373: WASAPI parity and second-song stability had no oracle

- **Mistake:** A fresh reviewer found that “same meaning” and “no second-song
  bias” had no concrete observation or tolerance.
- **Prevention:** Use the existing ConfigGUI Judgement Offset Advisor on one
  retained two-song run. Require its stable-result contract, overlap with the
  accepted WASAPI `-10..-8 ms` estimator range, and a per-stage median
  difference no larger than the greater displayed stage MAD.

### S-374: The acceptance oracle required diagnostics the spec prohibited

- **Mistake:** The ConfigGUI advisor consumes the existing bounded judgement
  timing records, while the spec broadly prohibited normal per-judgement
  logging.
- **Prevention:** Retain exactly the already-existing bounded timing records
  required by the approved advisor. Add no new per-judgement or callback log;
  distinguish retained analysis input from newly invented diagnostics.

### S-375: Startup froze channel format before the SDK exposes it

- **Mistake:** A fresh reviewer found that the spec froze channel sample types
  before `createBuffers`, while the recorded SDK contract exposes active channel
  details only afterward.
- **Prevention:** Validate rate, frame count, channel count, and chosen indices
  before buffer creation. Query, validate, and freeze each selected channel type
  immediately after `createBuffers`, before touching either half or starting.

### S-376: Stateless asioMessage capability queries were omitted

- **Mistake:** A fresh reviewer found no outcome for ordinary
  `kAsioSelectorSupported`, engine-version, time-info, time-code, and unknown
  selector queries, despite the rule that every callback case has one result.
- **Prevention:** Return fixed ASIO 2 capability answers with no session state:
  advertise the handled notification selectors and time info, reject time code
  and unknown selectors, and report engine version 2. Notification delivery
  still enters Fatal under the separate explicit-fault rule.

### S-377: Created output halves were left uninitialized

- **Mistake:** A fresh reviewer found that startup reached Start without
  initializing both created output halves, leaving callback order to determine
  whether uninitialized device memory could be presented.
- **Prevention:** After post-create type validation, clear both halves with
  format-correct silence. Do not call the mixer or advance audio state while
  priming them.

### S-378: outputReady support had no discovery rule

- **Mistake:** A fresh reviewer found that callbacks conditionally called
  `outputReady` without any operation establishing the frozen condition.
- **Prevention:** Probe once before Start. Success enables callback calls,
  `ASE_NotPresent` disables them normally, and every other result is startup
  Fatal. Once enabled, a later callback call failure is structural Fatal.

### S-379: Buffer-size change was falsely advertised as supported

- **Mistake:** Both fresh reviewers found `kAsioBufferSizeChange` in the
  `kAsioSelectorSupported` success list even though the bundled SDK explicitly
  requires that selector to return unsupported.
- **Prevention:** Return unsupported for the capability query. If a driver
  nevertheless delivers an actual buffer-size-change notification, the frozen
  session format makes that explicit observation immediate Fatal.

### S-380: Judgement acceptance omitted the active configuration mode

- **Mistake:** A fresh reviewer found that a native-judgement run could satisfy
  the two-song advisor oracle because retained provenance did not include
  `enable_absolute_time_judgement` and acceptance did not require it enabled.
- **Prevention:** Record the effective mode with the retained run and require it
  to be enabled before using that run to accept the absolute-judgement design.

### S-381: Ordinary shutdown named two possible driver-release operations

- **Mistake:** A fresh reviewer found `release/exit driver` ambiguous, allowing
  an implementation to call two release operations, neither operation, or put
  callback-route clearing before actual driver release.
- **Prevention:** Name the bundled SDK operation exactly: call `ASIOExit` once
  after successful buffer disposal, require `ASE_OK`, and clear the callback
  route only after that call returns successfully. There is no separate driver
  release step.

### S-382: The time-info callback return was unspecified

- **Mistake:** Both fresh reviewers found no required `ASIOTime*` result for
  `bufferSwitchTimeInfo`, leaving room to return driver-owned input and imply
  outbound time-info or time-code semantics that the host does not support.
- **Prevention:** Return `nullptr` after successful PCM submission, matching the
  bundled SDK host example. The callback never mutates or returns time data.

### S-383: Valid ASIO rate and speed fields were not checked

- **Mistake:** A fresh reviewer found that the callback could render after
  receiving `kSampleRateValid` with a rate unequal to the frozen rate or
  `kSpeedValid` with non-finite or non-nominal speed. Checking change flags
  alone did not enforce the immutable physical transport contract.
- **Prevention:** Before rendering, require every valid sample-rate field to be
  finite and exactly equal to the frozen driver rate, and every valid speed
  field to be finite and exactly `1.0`. A mismatch is immediate Fatal. These
  checks validate physical format only and never supply logical time.

### S-384: A scheduling hint was misclassified as driver failure

- **Mistake:** The candidate made `directProcess == ASIOFalse` Fatal even
  though the bundled SDK defines it as a suggestion that the host may defer
  processing, not an error or loss report. This would make a valid driver
  callback abort the process.
- **Prevention:** The simplified host has no deferral worker, so both hint
  values execute the same synchronous PCM callback. Ignore the hint; do not
  infer health, timing, or ownership from it.

### S-385: The legacy buffer callback had no explicit result

- **Mistake:** The candidate advertised time-info support but specified only
  `bufferSwitchTimeInfo`, while the bundled SDK still requires the legacy
  `bufferSwitch` entry to be supported for compatibility.
- **Prevention:** Install both callback entries. Both execute the same direct
  PCM path; only the time-info form performs the immutable physical-field
  checks and returns `nullptr`.

### S-386: A null time-info input had no structural-failure rule

- **Mistake:** The time-info callback was required to inspect flags without
  first stating the result of a null `ASIOTime*`, leaving an unchecked
  dereference in the callback contract.
- **Prevention:** A null time-info pointer is immediate structural Fatal before
  any field access or PCM rendering.

### S-387: The spec assumed callback serialization that the SDK does not grant

- **Mistake:** A fresh reviewer found that the bundled SDK permits
  `bufferSwitch` access possibly recursively, while the candidate assumed
  serialization and gave overlap no explicit outcome. Reentry could corrupt
  sequential mixer state and the output half.
- **Prevention:** Add exactly one non-blocking atomic callback-active bit. The
  first entry owns it until normal return; any recursive or simultaneous entry
  is immediate structural-overload Fatal. The bit never waits, queues, orders,
  or coordinates lifecycle work.

### S-388: Focus acceptance could run entirely during silence

- **Mistake:** A fresh reviewer found that background, focus, move, and resize
  checks could pass without audible PCM, so callback cessation followed by a
  later restart could be missed despite violating continuous-session behavior.
- **Prevention:** Exercise these cases while an in-game preview or song is
  audibly producing PCM and directly require no gap, silence, or repeated
  section. Keep watchdogs and callback diagnostics prohibited.

### S-389: Ordinary shutdown had no target-driver acceptance step

- **Mistake:** A fresh reviewer found that all listed gameplay criteria could
  pass even if the required Stop/dispose/exit chain later hung, crashed, or
  failed to release the driver.
- **Prevention:** After the retained session, the user performs the game's
  ordinary close, observes a normal prompt exit without Fatal or hang, then
  starts it normally again and requires the same ASIO driver to open. This is
  runtime acceptance of shutdown and release, not agent authorization to close
  or control a process.

### S-390: Focus text retained the rejected serialization assumption

- **Mistake:** After adding the recursive-entry Fatal guard, the focus section
  still claimed that the driver continued “serialized callbacks,” recreating
  the exact unsupported SDK assumption the guard corrected.
- **Prevention:** Require the same session and continuing callbacks without
  claiming driver serialization. Section 5.3 alone defines recursive or
  simultaneous entry as structural Fatal.

### S-391: A blanket overlap-detector ban contradicted the one-bit guard

- **Mistake:** The callback section required one atomic active bit and then
  still said there was no overlap detector at all.
- **Prevention:** Prohibit overlap recovery, waiting, and serialization while
  naming the sole non-blocking Fatal detector explicitly. Never use a blanket
  ban that contradicts the required direct guard.

### S-392: Checked logical-time arithmetic had no failure outcome

- **Mistake:** A fresh reviewer found that the candidate called anchor
  arithmetic and native millisecond conversion “checked” without saying what
  overflow, invalid division, non-finite data, or an unrepresentable result
  does. That admitted silent clamp, drop, fallback, or continuation.
- **Prevention:** Every logical-time arithmetic or conversion operation has
  exactly two outcomes: its exact representable value or immediate
  non-returning Fatal. Clamping, saturation, dropping, and fallback are
  prohibited.

### S-393: DirectSound query ordering and integral conversion were omitted

- **Mistake:** A fresh reviewer found that the candidate neither ordered query
  QPC after its coherent control snapshot nor said how an ordinary fractional
  projected source frame becomes the integral DirectSound cursor. A query could
  pair an earlier QPC with later state or treat every fractional frame as
  arithmetic failure.
- **Prevention:** A control captures its QPC before publishing one complete
  state. A query accepts one complete state first and then captures its QPC;
  `q < Qc` is Fatal. Integral cursor APIs use mathematical floor on the exact
  projected source frame before the existing frame-to-byte and wrap/end
  conventions.

### S-394: Tune's exact-rational native boundary was unspecified

- **Mistake:** A fresh reviewer found that `J(q)` was named as Tune input but
  only judgement had a declared native conversion. Implementers would have to
  invent Tune's representation, rounding, or failure behavior.
- **Prevention:** At the existing Tune song-clock boundary, capture QPC and
  compute the checked signed desired tick as mathematical
  `floor(J(q) * configured_target_rate)`. The existing Tune catch-up consumes
  that absolute tick. Judgement never consumes Tune ticks and remains on exact
  event `J(q)` through its separate millisecond boundary.

### S-395: The entire live IASIO lifecycle was assigned to the wrong COM apartment

- **Mistake:** The approved candidate prohibited a loader-owned COM thread and
  required direct IASIO creation, use, and release on the native game loop's
  existing MTA. This premise was not validated against the selected driver's COM
  registration before approval or implementation. The deployed driver was
  registered `ThreadingModel=Apartment`; runtime terminated in
  `CoCreateInstance` with `E_NOINTERFACE` before `IASIO::init`. Because startup,
  steady ownership, message servicing, and shutdown all depended on that MTA
  premise, the lifecycle half of both the specification and plan was invalid,
  not merely one factory call.
- **Prevention:** Every selected ASIO driver uses one identical generic host
  path. One dedicated STA initializes COM, creates and owns IASIO, pumps its
  message queue, executes every host lifecycle call, releases IASIO, and
  uninitializes COM. The game MTA receives no IASIO ownership. Startup completion
  and ordinary shutdown use only two one-shot handoffs; no recovery state
  machine is restored. Driver registration plus runtime evidence must validate
  the thread premise before future approval.

### S-396: A driver-specific threading dispatcher was proposed after the MTA failure

- **Mistake:** The first reaction to the apartment failure began expanding the
  design toward registry-driven `Apartment`/`Free`/`Both`/`Neutral` dispatch.
  That would replace one false assumption with vendor-registration policy,
  branches, and unsupported compatibility behavior that the task never needed.
- **Prevention:** Use the same Steinberg-style STA host path for every selected
  32-bit ASIO driver. Do not branch on vendor, name, CLSID, hardware, or
  `ThreadingModel`; a direct COM or IASIO failure is Fatal, never a trigger for a
  second apartment, manual DLL loading, retry, or fallback.

### S-397: The first STA rewrite still let the owner outlive shell storage it could access

- **Mistake:** The first replacement draft separated the driver lifecycle from
  the game thread but still described the thread, live session, and published
  services as one backend object. Beginning that object's destructor on the game
  thread while its owner thread still used members would recreate a C++ lifetime
  race even if every IASIO call used the correct apartment.
- **Prevention:** Fully construct a game-facing shell before starting the owner.
  Move the owner's inputs into its entry and give it only copied kernel-handle
  values. The owner may fill the shell's service slot only before signalling
  startup; afterward it never dereferences the shell. Its private live session
  is created and destroyed inside the STA, and the shell keeps handles alive
  until its join completes.

### S-398: The first STA startup order published callbacks before their target was complete

- **Mistake:** The first replacement draft installed the callback route before
  buffer creation, channel-type discovery, mixer construction, and conversion
  storage construction. A driver callback at that point could observe a
  published but incomplete target.
- **Prevention:** Supply the static callbacks to CreateBuffers while the audio
  route remains null. Finish channel discovery, mixer/conversion construction,
  initial silence, and the outputReady probe first. Publish the one complete
  callback target immediately before Start; a callback entering with a null
  target is Fatal.

### S-399: Callback pseudocode and prose disagreed on the first operation

- **Mistake:** After adding the null-route Fatal rule, the callback pseudocode
  loaded the route before testing the callback-active bit while the prose still
  called the bit the first callback operation. Both could not be literal.
- **Prevention:** Load and validate the one callback target first. The
  callback-active test-and-set is the first operation on that target. Keep the
  same order in prose, pseudocode, static proof, and implementation.

### S-400: The rejected implementation plan still advertised old approval

- **Mistake:** After rejecting the MTA implementation, its plan still named the
  old frozen tree as approved and remained directly executable. It also required
  removing `user32`, contradicting the corrected STA owner's required Windows
  message wait and pump.
- **Prevention:** Mark the entire old plan rejected and historical immediately.
  Do not edit it into the replacement plan. Write a new plan only after the
  replacement specification passes its new review gate and the user explicitly
  approves that specification.

### S-401: Static proof overclaimed COM balancing on Fatal paths

- **Mistake:** A static-proof bullet initially required every successful
  `CoInitializeEx` to reach `CoUninitialize`, contradicting the controlling rule
  that any later Fatal terminates immediately without cleanup.
- **Prevention:** Require exact COM balancing only for a session that reaches
  ordinary shutdown. Keep every Fatal path non-returning and cleanup-free even
  when STA initialization had already succeeded.

### S-402: Startup proof contradicted synchronous callbacks from Start

- **Mistake:** The first frozen STA rewrite correctly published the callback
  target before Start and required every admitted callback to render, but its
  static proof also said startup never advances mixer state. A conforming driver
  may invoke a buffer callback synchronously inside Start, before Start returns,
  so all three statements could not hold together.
- **Prevention:** Limit the no-advance guarantee to the explicit pre-Start
  digital-silence fill. A callback admitted by Start always uses the same normal
  synchronous `RenderPcm` path and may advance the mixer before startup-complete
  is signalled. Do not add a priming mode, readiness branch, or audio-start
  barrier.

### S-403: The review gate replaced reviewers after every small correction

- **Mistake:** The first frozen rewrite required newly created reviewers after
  every finding and correction. That contradicted the user's explicit rule to
  reuse reviewers unless the full specification or plan had been rewritten,
  wasted reviewer work, and made each small correction unnecessarily slow. The
  orchestrator then followed that stale written rule instead of the user's
  controlling instruction after S-402.
- **Prevention:** Keep the same two independent reviewers throughout iterative
  corrections to one rewrite and send each new exact tree as follow-up work.
  Replace them only after a full design/specification/plan rewrite or explicit
  user direction. A local document never overrides the user's later workflow
  instruction.

### S-404: Blanket QPC cursor reporting broke native streaming refills

- **Mistake:** The logical-time rewrite made every ASIO secondary buffer report
  a QPC-projected play cursor. The native preview path uses
  `DSBCAPS_CTRLPOSITIONNOTIFY`, treats the reported play cursor as the amount of
  its ring buffer that is safe to refill, and ignores the write cursor. QPC
  projection could therefore move past source PCM that the mixer had not copied
  yet, letting the refill overwrite unread samples. Preview audio repeated and
  crackled while immutable menu and stage buffers remained clean.
- **Prevention:** Keep QPC logical position for ordinary static/gameplay
  buffers. A notification-backed streaming buffer reports only the mixer's
  already-consumed source frame as its DirectSound refill coordinate. Never
  publish that coordinate as gameplay time or feed it into Tune, judgement,
  either offset, or the stage anchor; no ASIO timing value is involved.

### S-405: Removing the ASIO provider activation record broke ConfigGUI analysis

- **Mistake:** The ASIO logical-clock rewrite correctly stopped emitting
  `absolute-stage-activation`, whose payload describes the removed provider
  timeline, but left ConfigGUI requiring that intermediate record before it
  accepted a completed song. The retained log contained two trustworthy
  `semantic-stage-end activated=1` records and hundreds of eligible timing
  observations, yet the advisor discarded both songs and produced no estimate.
- **Prevention:** Treat the matching `semantic-stage-end activated=1` record as
  the authoritative proof that absolute judgement activated and the song
  completed. Continue validating `absolute-stage-activation` when a
  provider-backed run emits it, but never require that obsolete provider record
  from an ASIO logical-clock stage.

# ASIO Transport and Absolute Judgement Simplification

**Status:** Replacement review candidate after runtime rejection of the prior
thread model. No implementation plan or source change is permitted until this
exact specification receives two independent zero-finding reviews and the user
approves it.

This is the task's only normative specification. The adjacent
`archive/failure-ledger.md` is non-normative history.

The prior candidate and its implementation are rejected. It created IASIO on
the native game loop's existing MTA. The selected x86 driver is registered
`ThreadingModel=Apartment`; the deployed implementation consequently terminated
during `CoCreateInstance` with `E_NOINTERFACE` before `IASIO::init`. Steinberg's
Windows host uses the driver CLSID as both class ID and IASIO IID, so the IID was
not the error. The rejected path required COM to expose that apartment-owned
custom interface to the game MTA, and that operation failed. No statement from
the rejected thread model remains authoritative.

## 1. Controlling decision

This task defaults every correction to deletion.

- Delete the failed ASIO recovery and clock-reconciliation work.
- Do not repair, preserve, or rename that machinery.
- Add replacement behavior only where deletion alone cannot satisfy a required
  behavior stated in this document.
- Any unavoidable replacement is the smallest direct connection between
  existing owners. The one dedicated STA owner and its two one-shot handoffs in
  section 5 are required by the Windows ASIO COM contract. They must not grow
  into a recovery state machine, command queue, retry loop, clock bridge,
  history, generation, or compatibility fallback.
- A new secondary problem means the added mechanism is removed and the
  smallest way to satisfy the original required behavior is reconsidered; it
  is not a reason to omit that behavior or add another coordinating mechanism.

This rule applies only to this ASIO simplification task.

Removal-first is not a shortcut around required behavior. Every required case
in this document must still have one explicit result: ordinary continuation,
ordinary shutdown, or Fatal.

Deletion may remove unnecessary machinery; it may not remove a required
outcome or substitute for handling that outcome.

## 2. Required result

The result has two independent paths:

```text
QPC + selected-BGM Play anchor --------------------> Tune
input event ----------------------> captured input QPC
captured input QPC + same anchor --> absolute judgement when enabled
captured input -------------------> unchanged native judgement when disabled

DirectSound controls --> sequential mixer PCM --> ASIO callback --> device
```

ASIO is only the physical PCM transport. It does not own, supply, correct,
pause, resume, interpolate, or align gameplay time.

The following ASIO values are never operands of DirectSound cursor reporting,
Tune, input time, note time, judgement, GameTimeOffset, or JudgTimeOffset:

- ASIO system time or sample position;
- callback count, cadence, duration, or absence;
- buffer index, buffer lead, or buffer duration;
- driver latency;
- mixer progress; and
- ASIO startup, shutdown, focus, or fault state.

WASAPI behavior is unchanged.

## 3. Logical song and judgement time

### 3.1 DirectSound logical position

ASIO-mode DirectSound secondary buffers answer play position from their current
control state plus QPC. Play, Stop, SetCurrentPosition, looping, and natural
end retain their DirectSound-visible meanings.

A DirectSound control captures its control QPC `Qc` before publishing one
complete logical state containing the exact source-frame anchor `Sc` and the
current play/stop/loop/end facts, then forwards the same control to the mixer.
A cursor query first accepts one complete control state and only then captures
its query QPC `q`. The exact source-frame projection applies the existing
Play/Stop/SetCurrentPosition/loop/natural-end rules to that state. `q < Qc` is
immediate Fatal.

When a DirectSound API requires an integral cursor, it uses mathematical
`floor` of that exact projected source frame, then applies the existing checked
frame-to-byte and buffer wrap/end convention. Ordinary fractional progress is
therefore not an arithmetic failure. The mixer never feeds position back into
the logical state. No ASIO callback or ASIO value participates.

The implementation stores only the information needed to answer the next
logical query and to identify the current Play anchor. This specification does
not authorize a publication history, clock service, bridge, reconciliation
object, or additional lifecycle model.

### 3.2 Stage anchor

The existing sound-group-2 selection identifies the gameplay BGM. Stage entry
clears any prior binding and begins one new, initially unbound stage. The first
sound-group-2 observation that satisfies all of these conditions binds it:

- the selected buffer is currently playing;
- that current Play was issued after this stage entry in program order; and
- this stage has not already bound an anchor.

The qualifying current Play supplies one immutable anchor:

```text
stage-entry GameTimeOffset Gstage
Play QPC Qplay
source frame Splay
source sample rate Fs
QPC frequency Fq
```

If an observation is not yet eligible, the stage remains unbound; it does not
search history or use a timeout. Once bound, later observations, Play, seek,
stop, natural end, focus change, mixer advance, and ASIO events cannot replace
or mutate the copied anchor. Stage exit discards it, so the next stage cannot
reuse it.

For a captured QPC value `q`, the absolute song position is:

```text
song_seconds(q) = Splay / Fs + (q - Qplay) / Fq
J(q)            = song_seconds(q) + Gstage / 1000
```

Every arithmetic operation and native-boundary conversion in section 3 has
exactly two outcomes: the exact representable result or immediate non-returning
Fatal. Division by zero, overflow, non-finite data, and an unrepresentable
`passed_ms` are Fatal. No value is clamped, saturated, dropped, or replaced by a
fallback. Input may have been captured before `Qplay`, so its signed difference
is valid. An operation that requires a current-stage anchor before one has been
accepted is Fatal; it never substitutes ASIO time, a prior stage, elapsed wall
time, or a fallback clock.

### 3.3 Consumers and offsets

At each existing Tune song-clock update, the loader captures QPC `q`, computes
`J(q)` through the current-stage anchor, and performs the one declared native
conversion:

```text
desired_tune_tick(q) = mathematical floor(J(q) * configured_target_rate)
```

`configured_target_rate` is the existing positive exact Tune/gameplay tick rate
in ticks per second; this specification does not alter it. The result is a
checked signed integral tick. The existing Tune update and its bounded catch-up
consume that absolute desired tick. This adds no interpolation and no new clock
state. After a native modal pause, Tune may catch up to a later absolute tick,
but neither `J` nor judgement is rewritten. Judgement never consumes a Tune
tick.

Captured input retains its original QPC timestamp.

When `enable_absolute_time_judgement` is enabled, the loader converts each
accepted input timestamp through that same anchor. `J(q)` remains an exact
rational value in seconds until the native call boundary, where conversion and
grading retain the previously traced contract:

```text
passed_ms      = checked truncation toward zero of (1000 * J(q))
judged_ms      = passed_ms + native_player_base_ms
grade_error_ms = note_target_ms - judged_ms
```

The native per-player lookup supplies `native_player_base_ms`; it is unchanged
and contains the live JudgTimeOffset with the additive sign shown above. The
native score path receives the same `passed_ms` and retains its unchanged
audio-group base. The loader applies the stage-entry GameTimeOffset once in
`J`; it never reads, applies, caches, combines, or compensates JudgTimeOffset or
the audio-group base. Note target and judged time therefore remain in the same
native millisecond coordinate. No ASIO-specific offset or latency compensation
exists.

When `enable_absolute_time_judgement` is disabled, Tune still uses `J`, while
the existing native judgement path remains unchanged. That legacy path also
receives no ASIO time or state. This is an explicit supported exception to
absolute input-time judgement, not a second loader clock.

## 4. PCM production

The mixer supplies the next sequential PCM frames requested by the audio
backend. Its only ASIO-facing input is the requested frame count.

```text
RenderPcm(frame_count) -> exactly frame_count interleaved stereo float frames
```

Source-rate conversion, looping, Play, Stop, Seek, gain, and voice lifetime are
ordinary mixer behavior. Any private sample-conversion cursor remains audio
state only and is never exposed as gameplay time.

`RenderPcm` receives no QPC, timestamp, sample position, latency, presentation
coordinate, recovery state, or desired alignment. It performs no ASIO clock or
timer query. It is allocation-free and non-throwing after successful startup.

## 5. ASIO transport

### 5.1 One STA-owned session

Every configured 32-bit ASIO driver uses the same host path. There is no driver
name, vendor, CLSID, executable, or hardware-specific branch. The live backend
has exactly two ownership units:

- a game-facing shell owns only the dedicated thread, the two one-shot handles,
  and the successfully published immutable service view; and
- one private live session is created, retained, shut down, and destroyed
  entirely inside that thread. It owns the IASIO wrapper, ASIO buffers, frozen
  format, mixer, conversion storage, and callback target.

The shell never owns IASIO and the live session never owns the shell or native
game lifetime. The shell is fully constructed before the owner thread starts.
The owner entry receives all moved startup inputs and copies of the two kernel
handle values. It may write the shell's empty service-view slot only before it
signals startup complete; after that signal it never dereferences the shell.
The shell keeps both handles open until the owner has returned. This split
prevents game-thread shell destruction from beginning the lifetime end of an
object or C++ storage still used by the owner. It adds no command interface or
state machine.

The shell starts one dedicated thread. The owner entry initializes its own
thread exactly once with `CoInitializeEx(nullptr,
COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE)`, and creates IASIO there
through Steinberg's standard
`CoCreateInstance(driver_clsid, nullptr, CLSCTX_INPROC_SERVER, driver_clsid,
...)` contract. IASIO ownership is never published to the game loop or another
host control thread.

The host does not inspect or branch on `InprocServer32\ThreadingModel`, load a
vendor DLL manually, or attempt a second apartment. Windows COM applies the
selected driver's registration to this one STA `CoCreateInstance` call. A
driver that requires COM marshaling must provide it through its own valid COM
registration. Any returned COM failure is startup Fatal, not permission for a
driver-specific workaround.

The owner creates its Windows message queue before opening the driver. All
host-initiated IASIO lifecycle calls execute on that same owner thread. The only
exception is `outputReady` from inside a driver-owned ASIO callback, which is
part of the SDK callback contract defined in section 5.3. The game loop's MTA
never creates, receives, calls, stops, disposes, releases, or unmarshals IASIO.

`S_OK` and `S_FALSE` are successful STA initialization results and each is
balanced by exactly one `CoUninitialize` on ordinary shutdown. A failed
`CoInitializeEx` result is immediate Fatal and is not balanced as though COM had
initialized successfully.

The caller observes one synchronous, unbounded startup operation. Its internal
order is exact:

1. fully construct the game-facing shell, its empty service-view slot, and its
   two one-shot synchronization handles;
2. move all startup inputs into the sole STA owner entry, copy the two handle
   values into that entry, start it, and wait for its startup-complete handle;
3. on the owner, initialize COM as STA and create the owner's message queue;
4. allocate the private live session, resolve and create the configured driver,
   then call `init` with the game window as the one-shot SDK system reference;
5. query its current sample rate, buffer capabilities, and output-channel
   count;
6. validate and freeze sample rate, exact configured frame count, and the two
   selected output-channel indices;
7. supply the fixed callbacks and create both output buffer halves while the
   audio callback route is still null;
8. query the two active channel descriptions, then validate and freeze their
   sample types;
9. construct the mixer and fixed conversion storage for that frozen format;
10. clear both halves with format-correct digital silence without calling the
    mixer or advancing audio state;
11. probe `outputReady` exactly once: `ASE_OK` freezes support as enabled,
    `ASE_NotPresent` freezes it as disabled, and any other result is startup
    Fatal;
12. publish the one callback target, call Start, and require `ASE_OK`;
13. write the immutable game-facing service view, emit the bounded startup
    record, signal startup complete exactly once, and enter the owner message
    loop; and
14. return the shell for the already-live session after that signal.

The two handles have no general protocol. `startup-complete` is signalled once
by the owner and waited once by the starting caller. `shutdown-requested` is
signalled once by ordinary backend destruction and consumed by the owner
message loop. There is no command queue, request identifier, acknowledgement,
generation, intermediate readiness state, or second start.

After the startup wait succeeds, shell service calls can only create mixer
voices or read the frozen frame count/sample rate. They cannot invoke IASIO,
signal the owner, or mutate lifecycle. Publication is one-way and the service
view remains stable until the proven ordinary-close seam, after the native game
has destroyed its sound owner and can make no later service call.

The successful startup-complete signal/wait is the only publication boundary
for the service view. Its fields are immutable afterward and therefore need no
per-field atomics, lock, version, or readiness flag.

The published view contains exactly one `AudioRenderCore*`, the frozen frame
count, and the frozen sample rate. `CreateVoice` forwards to that render core;
`endpoint_buffer_frames` and `output_sample_rate` return the copied frozen
values; `CurrentOutputFrame` remains unavailable; and the two legacy cursor
diagnostic counters remain no-ops. No shell method dereferences the live session
or its IASIO wrapper.

After startup, the owner performs no periodic work and makes no runtime IASIO
call. It waits indefinitely with `MsgWaitForMultipleObjectsEx` for either the
one shutdown handle or its STA message queue. Message availability only drains
and dispatches that queue; it cannot alter audio, focus, timing, or lifecycle
state. Shutdown availability enters the one ordinary sequence in section 5.5.
The wait has no timeout or polling interval. `WAIT_FAILED` or any unexpected
result is immediate ownership Fatal; it never begins shutdown or recovery.

Startup has no focus check, deadline, retry, fallback, replacement session, or
post-start calibration. The actual stage does not begin inside ASIO startup, so
startup may take as long as the driver requires. Event creation, thread
creation, STA initialization, driver creation, or any later startup operation
failure is immediate non-returning Fatal. Fatal performs no partial startup
cleanup and no backend fallback.

### 5.2 Immutable driver format

The driver's current positive integral sample rate is used. It may be 44,100
Hz, 48,000 Hz, or another rate accepted by the existing format constraints. The
loader never forces 48 kHz and never changes the driver rate.

The configured buffer frame count must be supported exactly. Sample rate,
buffer frame count, output channels, channel indices, channel sample types, and
conversion capacity are immutable for the session.

ConfigGUI inspection and validation use the inspected driver rate. They do not
assume 48 kHz; any duration display is derived from the inspected rate or is
shown only in frames.

### 5.3 Callback

The audio callback route is one non-owning atomic pointer. It is null while
buffers are created, stored exactly once immediately before Start, loaded once
at audio-callback entry, and cleared exactly once after successful Exit. A null
route at audio-callback entry is immediate Fatal. The route never transfers
ownership and carries no generation, status, or recovery meaning; the Stop/Exit
contract and owner-thread lifetime keep its target alive for every admitted
callback.

The bundled SDK permits callback access recursively. Both `bufferSwitch` and
`bufferSwitchTimeInfo` are installed, and exactly one non-blocking atomic
callback-active bit rejects recursion or simultaneous entry:

```text
load callback target; null means Fatal
if callback-active was already set: Fatal
RenderPcm(frozen_frame_count)
convert to the two frozen channel formats
copy to the selected driver half
call outputReady only when supported
clear callback-active
return
```

After the one route load succeeds, the bit is the first target operation. It is
cleared only immediately before normal return and has no consumer outside the
two callback entries. It never waits, queues, serializes after contention,
orders lifecycle work, or protects any non-callback path. Reentry is an explicit
structural overload and therefore immediate Fatal.

Ordinary `asioMessage` capability queries use fixed, session-independent
answers:

- `kAsioSelectorSupported` returns supported exactly for
  `kAsioEngineVersion`, `kAsioResetRequest`, `kAsioResyncRequest`,
  `kAsioLatenciesChanged`, `kAsioOverload`, `kAsioSupportsTimeInfo`, and
  `kAsioSupportsTimeCode`; it returns unsupported for every other selector,
  including `kAsioBufferSizeChange` as required by the bundled SDK;
- `kAsioEngineVersion` returns ASIO version 2;
- `kAsioSupportsTimeInfo` returns supported;
- `kAsioSupportsTimeCode` returns unsupported; and
- every other capability query returns unsupported.

The handled notification selectors include reset, resync, latency change, and
overload so the driver can report them; delivery still enters the Fatal rule in
section 5.4. Although buffer-size change is not advertised as supported, an
actual `kAsioBufferSizeChange` delivery also enters that Fatal rule. These fixed
replies require no lifecycle state.

`directProcess` is a scheduling hint, not a failure report. Its value is
ignored and both callback forms process synchronously; there is no deferral
worker.

The time-info callback never uses ASIO time as an audio position or logical-time
operand. A null `ASIOTime*` is immediate structural Fatal. Otherwise, before
rendering it applies only these physical-contract checks:

- `kSampleRateChanged` or `kClockSourceChanged` is immediate Fatal;
- when `kSampleRateValid` is present, the field must be finite and exactly
  equal to the frozen driver sample rate, otherwise Fatal; and
- when `kSpeedValid` is present, the field must be finite and exactly `1.0`,
  otherwise Fatal.

All other ASIO timestamps, sample positions, and time-code fields are ignored.
After successful PCM submission, `bufferSwitchTimeInfo` returns `nullptr`; it
does not mutate or return the driver's `ASIOTime` input. The legacy
`bufferSwitch` callback performs the same PCM submission without time-info
field checks and returns normally.

An invalid buffer index, a render/conversion failure, a non-finite sample, or
an enabled `outputReady` call returning anything other than `ASE_OK` is
immediate Fatal.

There is no callback worker, queue, render lock, overlap recovery or
serialization, timing measurement, deadline, silence inference, callback
history, or normal per-callback logging. The STA owner never renders or receives
PCM and is not a callback worker. The one callback-active bit is only a
non-blocking Fatal detector.

### 5.4 Focus and runtime faults

The admitted configuration requires the ASIO driver to retain exclusive
ownership from successful Start until ordinary Stop. Focus loss, focus return,
window movement, resize, and foreground state are handled by retaining that
same session and continuing callbacks. They never release, stop, restart, or
replace the driver, and the independent QPC timeline continues. Recursive or
simultaneous callback entry has only the structural Fatal result in section
5.3; focus does not change that rule.

The ASIO session is assumed to own its device exclusively. Windows
notification or shared-mode audio may be blocked, routed elsewhere, or mixed
outside this ASIO session through driver-specific behavior, but it cannot share
or acquire this session's ownership and cannot interrupt or reconfigure it. If
an explicit ASIO result or notification reports that ownership or session
stability was lost, that report is immediate Fatal rather than a recovery
trigger.

The loader never infers a driver fault from elapsed time, callback silence,
audio content, CPU load, or focus. It reacts only to an explicit SDK result or
notification. Silent callback cessation is outside the admitted driver
contract; no time watchdog is invented for it.

From the moment callbacks are supplied to CreateBuffers until callback-route
clearing, each of these explicit observations is immediate non-returning Fatal
whenever it can arrive, including during startup, ordinary running, or ordinary
shutdown:

- `kAsioResetRequest`;
- `kAsioResyncRequest`;
- `kAsioBufferSizeChange`;
- `kAsioLatenciesChanged`;
- `kAsioOverload`;
- `sampleRateDidChange`, regardless of the numeric argument;
- `kSampleRateChanged`; and
- `kClockSourceChanged`.

There is no pending flag, recovery owner, Stop/reopen sequence, retry budget,
replacement, continuation, or fallback after such an observation.

### 5.5 Shutdown and Fatal

Ordinary shutdown is the only cleanup path. It discards unsubmitted audio work
and uses the second one-shot handoff. Destruction of the game-facing shell on the
native game loop signals `shutdown-requested` exactly once and joins the STA
owner. The private live session remains wholly alive while the shell destructor
waits. The owner wakes and directly performs, on the same STA that created
IASIO:

```text
ASIOStop -> ASIODisposeBuffers -> ASIOExit -> clear callback route
-> destroy live-session storage -> CoUninitialize -> owner thread returns
```

The native game loop waits only for that owner thread to return; it never calls
IASIO and receives no intermediate result. There is no callback join, callback
acknowledgement, lifecycle command queue, generation, timeout, or teardown
recovery. The owner message loop has exactly two normal outcomes: dispatch an
STA message and continue waiting, or consume `shutdown-requested` and execute
the sequence above.

`ASIOStop`, `ASIODisposeBuffers`, and `ASIOExit` are each called once in that
order and must return `ASE_OK`; the first other result enters Fatal immediately
and no later cleanup step runs. Successful Stop is the driver boundary after
which no callback may enter. `ASIOExit` is the one driver exit-and-release
operation; there is no separate release call. Callback-route clearing occurs
only after `ASIOExit` returns `ASE_OK` and is an infallible local assignment.
The owner then destroys the now driver-free live session, calls
`CoUninitialize` once, and returns. The shell destructor's join completes only
after that return. It then closes the two synchronization handles and allows the
native game loop to continue through its own MTA `CoUninitialize` unchanged.

Every Fatal source calls one production non-returning hard-crash boundary.
Fatal may record only directly available bounded context. It performs no Stop,
disposal, release, route clear, COM uninitialization, recovery, retry, fallback,
or later state transition.

## 6. Required deletion

Delete, rather than disable, all code and build entries whose purpose is any of
the following:

- foreground/focus monitoring for ASIO;
- recovery, retry, replacement, or session-state coordination;
- ASIO-to-QPC or ASIO-to-gameplay clock conversion;
- physical/logical presentation bridges, alignment, priming, phase correction,
  or rate matching;
- callback workers, deferred rendering, callback timing, deadline policy, or
  timer-resolution policy;
- presentation histories, handoffs, generations, leases, reseeds, or
  acknowledgements; and
- ASIO-derived judgement or offset behavior.

Retain only the direct code required for the logical QPC behavior in section 3,
the sequential mixer behavior in section 4, the single ASIO session in section
5, its sole STA owner/message pump and two one-shot handoffs, ConfigGUI's
driver-format validation, ordinary shutdown, and unchanged WASAPI behavior.

Do not keep deleted mechanisms dormant behind flags or compatibility wrappers.

## 7. Verification and acceptance

### 7.1 Test policy

No new automated test is authorized. Delete these rejected tests and their
CMake registrations:

- `tests/Audio/Logical/LogicalPresentationClockTests.cpp`;
- `tests/Patches/AbsoluteJudgement/LogicalJudgementTimelineTests.cpp`;
- `tests/Audio/Asio/AsioPhysicalSessionControllerTests.cpp`;
- `tests/Audio/Asio/AsioForegroundStateTests.cpp`.

Do not replace them with fake IASIO, fake-stage, source-grep, formula, selector,
retry, or lifecycle tests. A future test requires explicit user approval and an
independently derived oracle. Existing unrelated tests prove only what they
execute.

### 7.2 Static proof

After implementation, trace the final source and demonstrate:

- ASIO and mixer progress never reach DirectSound logical position, Tune,
  input, judgement, GameTimeOffset, or JudgTimeOffset;
- DirectSound controls capture QPC before publishing one complete state,
  queries accept that state before capturing QPC, and integral cursors use the
  declared mathematical floor conversion;
- Tune alone converts `J` to
  `floor(J * configured_target_rate)`; enabled absolute judgement uses the same
  current-stage BGM anchor and exact event `J`, disabled judgement remains
  wholly native, and neither judgement mode consumes Tune ticks;
- each stage discards its anchor and cannot reuse the prior stage;
- GameTimeOffset enters `J` exactly once and loader code never applies
  JudgTimeOffset;
- native judgement applies JudgTimeOffset exactly once in its existing base;
- the PCM pull accepts only a frame count and advances sequentially;
- channel sample types are frozen only after buffer creation, both halves are
  silenced before Start, and startup never advances mixer state;
- `asioMessage` capability replies are fixed and `outputReady` support comes
  only from the one pre-Start probe;
- `kAsioBufferSizeChange` is not advertised as supported, while any actual
  delivery is Fatal;
- valid time-info sample rate and speed fields can only confirm the frozen
  physical format and never reach logical time;
- both callback forms use the same synchronous PCM path regardless of the
  `directProcess` hint;
- callback recursion or simultaneous entry is rejected by one non-blocking
  callback-active bit with no wait, queue, or lifecycle role;
- no focus/window code reaches ASIO;
- no loader focus, move/resize, or notification-audio path stops, releases,
  restarts, or replaces the ASIO session;
- no recovery, retry, replacement, or ASIO clock-reconciliation code remains;
- every selected driver uses the same live host path with no vendor, name,
  CLSID, executable, or hardware-specific branch;
- the game-facing shell owns only the STA thread, two handles, and immutable
  published services, while the private live session and all of its storage are
  created and destroyed inside the owner thread;
- the owner entry receives startup inputs and handle values without retaining a
  shell reference; it writes the service slot only before startup-complete and
  never dereferences shell storage afterward;
- the live driver factory is reached only after the sole owner thread has
  successfully initialized one STA and created its message queue;
- the callback route remains null through buffer creation, format discovery,
  mixer/conversion construction, initial silence, and outputReady probing; it is
  published once immediately before Start, and a null audio-callback route is
  Fatal;
- IASIO ownership is never published to the game loop, every host-initiated
  IASIO lifecycle call and final release executes on the owner STA, and the game
  loop's MTA never touches it; the only non-owner IASIO call is callback-local
  `outputReady` under the SDK callback contract;
- exactly one startup-complete handoff and one shutdown-request handoff exist;
  neither has an identifier, timeout, retry, queue, state transition, or timing
  role;
- after startup the STA owner only dispatches its message queue or consumes the
  one shutdown request;
- every listed explicit runtime fault enters non-returning Fatal;
- Fatal has no cleanup successor;
- ordinary shutdown is exactly one successful
  `ASIOStop -> ASIODisposeBuffers -> ASIOExit -> route clear -> CoUninitialize`
  chain on the owner STA with live-session destruction before COM
  uninitialization, followed by owner return and one game-thread join; every
  successful STA initialization that reaches ordinary shutdown has exactly that
  one balance, while Fatal terminates without cleanup;
- ConfigGUI has no 48 kHz assumption; and
- WASAPI behavior is unchanged.

CLion diagnostics are performed one source file at a time: open that file, let
it analyze, then request its diagnostics. Do not batch diagnostics. Do not
close, restart, stop, or terminate CLion or any other process.

### 7.3 Build proof

Use the x86 environment from:

```text
C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat
```

Set `GC_ASIO_SDK_DIR=H:\gc\artifacts\ASIOSDK` and use the existing
`msvc32-*` presets. Build and existing-test success are static evidence, not
gameplay acceptance.

### 7.4 Runtime evidence

Before a retained run, record source/build identity, intended/deployed DLL
SHA-256 equality, backend/driver identity, frozen driver rate and buffer size,
effective GameTimeOffset/JudgTimeOffset, and the effective
`enable_absolute_time_judgement` value.

`H:\gc\loader-log.txt` is overwritten each launch and represents only the
latest run. Preserve it before another run only when that evidence is needed.

Logging is limited to startup format, direct Fatal context, existing ordinary
audio diagnostics, and the already-existing bounded judgement timing records
consumed by the approved ConfigGUI Judgement Offset Advisor. No new
per-callback, cadence, deadline, focus-recovery, or per-judgement diagnostic is
added.

### 7.5 User acceptance

After static/build proof, the user verifies on the target driver:

1. normal foreground startup;
2. background shortly after startup, then foreground return;
3. while in-game PCM is audibly playing, covering at least one menu preview and
   one song, perform later background/foreground and move/resize: the audible
   stream has no gap, silence, or repeated section and there is no ASIO
   lifecycle transition;
4. while audible in-game PCM continues in the background, trigger Windows
   notification/shared-mode audio for the same physical output: the expected
   exclusive-session result is uninterrupted ASIO with no gap, silence, or
   repeated section; an explicit ownership-loss report instead takes the
   required Fatal path rather than continuing;
5. menu previews without repeat or silence;
6. at least two songs in one credit;
7. no visible frame hitch;
8. with `enable_absolute_time_judgement` recorded as enabled, the configured
   judgement offset has the same meaning as WASAPI and no second-song bias; and
9. song end proceeds to normal ranking/demo; and
10. after the retained session, use the game's ordinary close path, observe a
    prompt normal exit without Fatal or hang, then start it normally again and
    require the same ASIO driver to open successfully.

An explicit reset, resync, overload, format/rate/latency/clock change, or
structural callback failure must instead produce the recorded hard crash.
Successful recovery is not an acceptance criterion because recovery does not
exist.

Any visible hitch fails that run. Its cause is investigated separately with an
external profiler before attribution; ordinary logging is not treated as a
frame-time oracle.

For item 8, first require the retained provenance to record
`enable_absolute_time_judgement` as enabled, then run the existing ConfigGUI
`Analyze latest run` operation on the retained two-song log. Acceptance
requires all of the following existing observables:

- the advisor reports a stable suggestion, which already requires at least two
  complete songs and an estimator spread no greater than 3 ms;
- its ASIO estimator range overlaps the accepted WASAPI reference range
  `-10..-8 ms` (center `-9 ms`); and
- the absolute difference between the two displayed per-song `Median error
  before offset` values is no greater than the larger of their displayed MADs.

Failure of any condition rejects the run. The specification does not alter the
advisor, add a test, or derive an offset from ASIO latency.

Item 10 is a user-operated runtime acceptance step. It does not authorize the
agent to close, stop, restart, or otherwise control any process.

## 8. Review gate

Freeze this specification and the non-normative failure ledger as one exact
tree. Create two fresh independent reviewers after the tree is frozen; do not
reuse reviewers from any rejected tree. Give both the exact tree hash and the
same complete latest task rules and decisions. Both inspect that same tree and
must report zero findings. Every finding is recorded before correction. Any
correction creates a new tree and restarts both reviews with newly created
reviewers.

Only after two zero-finding reviews and explicit user approval may an
implementation plan or source implementation begin.

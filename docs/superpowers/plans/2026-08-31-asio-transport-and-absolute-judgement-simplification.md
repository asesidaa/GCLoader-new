# ASIO Transport and Absolute Judgement Simplification Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Execute inline on the current branch; do not create a worktree or dispatch subagents unless the user explicitly changes that instruction.

**Goal:** Replace the failed ASIO recovery and clock-reconciliation implementation with one synchronous exclusive ASIO PCM session and one ASIO-independent QPC song/judgement timeline, while preserving the existing WASAPI path.

**Architecture:** ASIO-mode DirectSound controls own a current logical cursor and current Play anchor. The existing semantic-stage hooks copy the first eligible sound-group-2 Play anchor, and both ASIO Tune timing and enabled absolute judgement evaluate the same exact `J(q)` equation. The ASIO backend only pulls the next fixed number of PCM frames, converts them, and submits them to the driver; it has no gameplay clock, recovery state, focus state, worker, or presentation bridge.

**Tech Stack:** C++23, Win32 QPC, DirectSound emulation, miniaudio, Steinberg ASIO SDK, SafetyHook, CMake/MSVC x86, CLion MCP.

**Spec:** `docs/superpowers/specs/2026-08-29-asio-logical-time-presentation-rewrite-design.md`, approved with the frozen staged tree `437ed8493bf5c2f7f225f2b4871c0315222bd11a`.

## Global Constraints

- The approved specification is the only behavior authority. The adjacent failure ledger is historical evidence, not a source of replacement requirements.
- Every previous ASIO implementation and every previous ASIO implementation plan is historical. Inspect current source only to identify deletion targets, surviving native seams, and unchanged WASAPI code. Do not repair, refactor, rename, or restore a historical ASIO mechanism.
- Default every correction in this task to deletion. Add only the smallest direct connection required by the approved specification.
- Execute inline on `fix/asio-lifecycle-recovery`. Do not create a worktree and do not use subagents.
- Do not stop, terminate, restart, close, or otherwise control CLion, the game, a driver host, or any other process. If a required capability is unavailable, stop and ask the user instead of inventing a workaround.
- Use CLion MCP for indexed source navigation, source edits, source formatting, and source diagnostics. Use the normal shell for Git, CMake, builds, tests, hashes, and read-only text checks. Do not use CLion for Git or command execution.
- CLion diagnostics are strictly sequential. For each source file: open that one file, wait for analysis, request its diagnostics, record the result, then proceed to the next file. Do not batch diagnostics and do not close files or CLion.
- Format each changed C/C++ file with CLion's format MCP. Do not hand-format around the configured formatter.
- Do not add an automated test. Delete the four rejected tests and their registrations. Existing unrelated tests remain evidence only for the code they execute.
- Preserve the x86 ABI, C++23 configuration, static MSVC runtime, callback `noexcept` boundaries, and allocation-free normal callback path.
- Preserve the WASAPI engine, endpoint clock, render schedule, exact history, DirectSound presented-cursor path, and judgement provider. Shared changes must have an explicit WASAPI branch whose behavior is unchanged.
- Do not edit or deploy anything under `H:\gc` during source implementation. Runtime deployment to `H:\gc\iDmacDrv32.dll` requires a later explicit user instruction. Never edit `H:\gc\config.toml` as part of this plan.
- Commit each coherent task only after its stated static/build checks pass. Never hide an incomplete historical path behind a compatibility flag.

## Final Ownership Model

```text
ASIO-mode DirectSound Play/Stop/Seek + QPC
                  |
                  +--> current logical cursor (per secondary buffer)
                  |
                  +--> current Play anchor -- sound-group-2 selection --+
                                                                       |
semantic stage entry: {Gstage, Play-order cutoff} ---------------------+--> immutable stage anchor
                                                                       |       |
input event QPC --------------------------------------------------------+       +--> exact J(q) --> judgement
Tune update QPC ----------------------------------------------------------------+--> floor(J * target rate) --> bounded Tune step

DirectSound voice controls --> sequential miniaudio PCM --> ASIO callback --> two frozen driver channels
```

The only ordering witness added for stage eligibility is one monotonically increasing Play-order number. Stage entry snapshots its current value; the selected buffer qualifies only when its current Play order is greater. This scalar has no history, retry, handoff, wait, lifecycle transition, or ASIO consumer. It exists solely to implement the specification's “Play issued after stage entry in program order” requirement without treating time as evidence.

Ordinary ASIO close has one separate native seam: after the game destroys its DirectSound sound owner and immediately before that same loop thread calls `CoUninitialize`, a guarded one-shot hook destroys the loader's sole ASIO owner. This seam has no gameplay-time, focus, recovery, or callback-ordering role.

## Historical Deletion Manifest

Delete these files, not merely their call sites:

- `src/Audio/Asio/AsioClock.h`
- `src/Audio/Asio/AsioClock.cpp`
- `src/Audio/Asio/AsioCallbackRuntime.h`
- `src/Audio/Asio/AsioCallbackRuntime.cpp`
- `src/Audio/Asio/AsioForegroundState.h`
- `src/Audio/Asio/AsioForegroundMonitor.h`
- `src/Audio/Asio/AsioForegroundMonitor.cpp`
- `src/Audio/Asio/AsioPhysicalSessionController.h`
- `src/Audio/Asio/AsioPhysicalSessionController.cpp`
- `src/Audio/Asio/AsioPresentationBridge.h`
- `src/Audio/Asio/AsioPresentationBridge.cpp`
- `src/Audio/Asio/AsioPresentationRateMatcher.h`
- `src/Audio/Asio/AsioPresentationRateMatcher.cpp`
- `src/Audio/Asio/AsioSession.h`
- `src/Audio/Asio/AsioSession.cpp`
- `src/Audio/Asio/AsioOutputBackendInternal.h`
- `src/Audio/Logical/LogicalPresentationClock.h`
- `src/Audio/Logical/LogicalPresentationClock.cpp`
- `src/Audio/Logical/LogicalPresentedOutputClock.h`
- `src/Audio/Logical/LogicalPresentedOutputClock.cpp`
- `src/Audio/Mixer/LogicalRenderStream.h`
- `src/Audio/Mixer/LogicalRenderStream.cpp`
- `tests/Audio/Logical/LogicalPresentationClockTests.cpp`
- `tests/Patches/AbsoluteJudgement/LogicalJudgementTimelineTests.cpp`
- `tests/Audio/Asio/AsioPhysicalSessionControllerTests.cpp`
- `tests/Audio/Asio/AsioForegroundStateTests.cpp`

Retain and modify only where the plan says so:

- the production driver/registry wrappers and isolated ConfigGUI probe;
- ASIO buffer-frame rules and sample conversion;
- the generic mixer and WASAPI presentation/history code;
- the existing semantic-stage, input-history, native judgement, and bounded Tune catch-up owners;
- one rewritten `AsioOutputBackend` as the sole live ASIO session owner.

---

## Task 1: Commit the approved authority before source work

**Files:**

- Commit as already staged: `docs/superpowers/specs/2026-08-29-asio-logical-time-presentation-rewrite-design.md`
- Commit as already staged: `docs/superpowers/specs/2026-08-29-asio-logical-time-presentation-rewrite/archive/failure-ledger.md`
- Commit separately: `docs/superpowers/plans/2026-08-31-asio-transport-and-absolute-judgement-simplification.md`

- [ ] Verify that no source file is staged and the reviewed specification tree is still exact:

```powershell
git diff --cached --check
git write-tree
git diff --cached --name-only
```

Expected before the first commit:

- `git write-tree` prints `437ed8493bf5c2f7f225f2b4871c0315222bd11a`.
- The staged name list contains exactly the approved specification and failure ledger.

- [ ] Commit the reviewed authority without the new plan:

```powershell
git commit -m "docs: approve ASIO transport simplification"
```

- [ ] Stage and commit this plan separately:

```powershell
git add -- docs/superpowers/plans/2026-08-31-asio-transport-and-absolute-judgement-simplification.md
git commit -m "docs: add ASIO simplification implementation plan"
```

- [ ] Verify the two commits and clean source baseline:

```powershell
git log -2 --oneline
git status --short
```

Expected: the two documentation commits are separate and there are no source changes.

---

## Task 2: Delete the rejected tests before implementing behavior

**Files:**

- Delete: `tests/Audio/Logical/LogicalPresentationClockTests.cpp`
- Delete: `tests/Patches/AbsoluteJudgement/LogicalJudgementTimelineTests.cpp`
- Delete: `tests/Audio/Asio/AsioPhysicalSessionControllerTests.cpp`
- Delete: `tests/Audio/Asio/AsioForegroundStateTests.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] Delete the four files and their complete `add_executable`, `target_link_libraries`, and `add_test` blocks.
- [ ] Do not add a replacement fake IASIO, fake-stage, formula, selector, source-grep, retry, or lifecycle test.
- [ ] Confirm that the unrelated WASAPI compatibility, exact-history isolation, IME, and config tests remain registered:

```powershell
rg -n "LogicalPresentationClock|LogicalJudgementTimeline|AsioPhysicalSessionController|AsioForegroundState" tests
rg -n "ExactWasapiClockCompatibility|ExactHistoryIsolation|ImeSuppression|ConfigContract|ConfigStartup" tests/CMakeLists.txt
git diff --check
```

Expected: the first search returns no matches; the second still finds all unrelated tests.

- [ ] Commit:

```powershell
git add -- tests/CMakeLists.txt tests/Audio/Logical/LogicalPresentationClockTests.cpp tests/Patches/AbsoluteJudgement/LogicalJudgementTimelineTests.cpp tests/Audio/Asio/AsioPhysicalSessionControllerTests.cpp tests/Audio/Asio/AsioForegroundStateTests.cpp
git commit -m "test: remove rejected ASIO recovery tests"
```

---

## Task 3: Replace the ASIO gameplay-time path end to end

**Files:**

- Create: `src/Audio/AudioContractFatal.h`
- Create: `src/Audio/AudioContractFatal.cpp`
- Modify: `src/Audio/CMakeLists.txt`
- Modify: `src/Audio/DirectSound/DirectSoundFacade.h`
- Modify: `src/Audio/DirectSound/DirectSoundFacade.cpp`
- Modify: `src/Audio/DirectSound/GameplayAudioCursorObservation.h`
- Modify: `src/Audio/DirectSound/GameplayAudioCursorObservation.cpp`
- Modify: `src/Audio/AudioBackendController.h`
- Modify: `src/Audio/AudioBackendController.cpp`
- Modify: `src/Patches/AbsoluteJudgement/JudgementSettings.h`
- Modify: `src/Patches/AbsoluteJudgement/JudgementClockResolver.h`
- Modify: `src/Patches/AbsoluteJudgement/JudgementClockResolver.cpp`
- Modify: `src/Patches/AbsoluteJudgement/JudgementStage.h`
- Modify: `src/Patches/AbsoluteJudgement/JudgementStage.cpp`
- Modify: `src/Patches/AbsoluteJudgement/JudgementScheduler.h`
- Modify: `src/Patches/AbsoluteJudgement/JudgementScheduler.cpp`
- Modify: `src/Patches/AbsoluteJudgement/AbsoluteJudgementRuntime.h`
- Modify: `src/Patches/AbsoluteJudgement/AbsoluteJudgementRuntime.cpp`
- Modify: `src/Patches/AbsoluteJudgement/AbsoluteJudgementPatch.cpp`
- Modify: `src/Patches/AbsoluteJudgement/AbsoluteJudgementDiagnostics.h`
- Modify: `src/Patches/AbsoluteJudgement/AbsoluteJudgementDiagnostics.cpp`
- Modify: `src/Patches/Framerate/GameplaySongClock.h`
- Modify: `src/Patches/Framerate/GameplaySongClock.cpp`
- Modify: `src/Patches/Framerate/FrameratePatch.h`
- Modify: `src/Patches/Framerate/FrameratePatch.cpp`
- Modify: `src/Patches/Framerate/FrameratePatchPlan.h`
- Modify: `src/Patches/Framerate/FrameratePatchPlan.cpp`
- Modify: `src/Config/ConfigCompiler.cpp`
- Modify: `src/Loader/DllMain.cpp`

### 3.1 Add the single hard-crash boundary

- [ ] Implement one non-returning `FailAudioContract` production boundary. Its inputs are a compact reason enum and a bounded fixed number of integral operands.
- [ ] Encode the direct reason/operands in the fail-fast exception record, call `RaiseFailFastException`, and retain only a non-returning compiler/CRT fallback after it.
- [ ] Do not allocate, lock, wait, flush, Stop ASIO, dispose buffers, clear routes, retry, or invoke a process-management callback in this boundary.
- [ ] Route every new section-3 arithmetic/clock Fatal and every new ASIO Fatal to this function. Existing bounded absolute-judgement diagnostics may record their already-available context before calling it, but they must not own another terminal mechanism.

### 3.2 Give DirectSound an explicit cursor model

- [ ] Add one `AudioCursorModel` query to `IAudioEngineController` with exactly two values:

```cpp
enum class AudioCursorModel : std::uint8_t {
    PresentedOutput, // unchanged WASAPI path
    LogicalQpc,      // ASIO path
};
```

- [ ] `AudioBackendController` returns the model directly from its frozen configured backend: WASAPI is `PresentedOutput` and ASIO is `LogicalQpc`. Do not add this query to either physical backend; ASIO transport must not acquire a gameplay-time responsibility.
- [ ] Pass and cache the model when a secondary buffer is created. Never infer the model from endpoint frames, a missing clock, an ASIO failure, or a fallback.
- [ ] This task changes the cursor model and gameplay-time consumers only. Keep the current physical voice/timeline construction temporarily so the intermediate Task 3 commit remains buildable against the historical backend; the `LogicalQpc` cursor, stage anchor, Tune, and judgement branches must not read it. Task 4.4 removes that obsolete ASIO timeline plumbing atomically with the rewritten backend and mixer entry.

### 3.3 Implement the ASIO-mode current logical cursor in `SecondarySoundBuffer`

- [ ] Keep the current WASAPI `CurrentOutputFrame`/`AudioCursorTimeline` branch byte-for-byte equivalent except for the explicit model dispatch.
- [ ] For `LogicalQpc`, store under the existing `control_mutex_` only:

```text
control QPC Qc
exact source-frame anchor Sc
source rate Fs and source length
playing / looping / natural-end facts
current Play {Play order, Qplay, exact Splay, Fs, Fq}
```

- [ ] Add one checked process-wide Play-order counter. ASIO-mode `Play` captures its control QPC, claims the next order with one non-blocking atomic operation, and publishes both in the same complete logical state; semantic stage entry only snapshots that same counter. Overflow is Fatal. This is the minimum scalar witness required by the specification's explicit program-order condition; it is not a session/logical generation and has no history, lifecycle transition, ASIO/callback consumer, wait, or acknowledgement.
- [ ] Cache a positive QPC frequency for the logical buffer. A control captures QPC before publishing one complete state, publishes it while holding the existing control mutex, and then forwards the same control to the mixer.
- [ ] If the mixer rejects a control after logical publication, call Fatal immediately; do not roll back, retry, or leave logical/mixer state divergent.
- [ ] Implement exact projection from the accepted state:

```text
stopped: Sc
playing non-looping: Sc + (q - Qc) * Fs / Fq, capped exactly at natural end
playing looping:      exact modulo of the same projection by source length
```

- [ ] A query copies/accepts one complete state first and only then captures `q`. `q < Qc`, invalid rates, overflow, invalid modulo, or an unrepresentable result is Fatal.
- [ ] `GetCurrentPosition` applies mathematical floor once at the DirectSound integral boundary, then the existing checked byte and wrap/end convention. For ASIO, both returned cursors use that same logical projected frame: no synthetic write lead is invented from ASIO geometry. WASAPI retains its existing endpoint-derived write cursor.
- [ ] `GetStatus` in ASIO mode derives playing/looping/natural-end from the same logical state and QPC, never from rendered frames or callback progress.
- [ ] `SetCurrentPosition` changes the current logical control anchor but not the current Play anchor. `Stop` freezes the exact projected source position. A later `Play` replaces the current Play anchor; replay from natural end starts at zero. Once the stage has copied an anchor, none of these later controls can mutate it.

### 3.4 Replace the cross-module observation with explicit backend payloads

- [ ] Keep the existing scoped sound-group query seam, but make its payload explicit:

```text
WASAPI payload: unchanged presented/exact-history fields
ASIO payload:   current logical playing fact plus copied current Play anchor
```

- [ ] The ASIO payload contains no output frame, ASIO position, latency, callback count, presentation provider, render history, playback history, bridge revision, or physical generation.
- [ ] The sound-group-2 native lookup remains the sole selector. A cursor observation from another group or another backend mode cannot bind the ASIO stage.

### 3.5 Make the existing semantic stage the only anchor owner

- [ ] Keep semantic-stage open/closed ownership solely in `JudgementStage`; do not mirror it in `JudgementClockResolver`. The WASAPI resolver branch retains the existing audited provider behavior. The ASIO stage-owned payload contains only:

```text
stage Play-order cutoff
stage-entry GameTimeOffset Gstage
optional immutable {Qplay, Splay, Fs, Fq}
```

- [ ] Reduce the ASIO side of `JudgementClockResolver` to binding/evaluating an explicitly supplied stage payload. It stores no stage-open flag, lifecycle enum, previous stage, or second copy of the anchor.
- [ ] At semantic stage entry, snapshot `Gstage` and the current Play order and clear the prior anchor. The first selected ASIO observation binds only if it is currently playing and its current Play order is greater than the stage cutoff.
- [ ] Ineligible observations leave the stage unbound. There is no history search, timeout, candidate list, retry, or previous-stage fallback.
- [ ] Once bound, ignore every later observation/control for binding purposes. Stage exit and gameplay-initialization cancellation both discard the anchor.
- [ ] Keep one stage lifecycle owner by running the existing `JudgementStage`/`JudgementScheduler` stage begin/end even in ASIO timing-only mode. When absolute judgement is disabled, skip input-transport/history/native-judgement setup, but retain the three existing gameplay-initialization/semantic-entry/semantic-exit hooks needed by Tune. Do not install loop, pressed, held, released, direction, held-age, or timing-grade hooks in disabled mode.
- [ ] Do not add a second stage hook, manager, service thread, event, or synchronization protocol.

### 3.6 Implement exact `J(q)` once and expose it to the two ASIO consumers

- [ ] The ASIO resolver evaluates only:

```text
song_seconds(q) = Splay / Fs + (q - Qplay) / Fq
J(q)            = song_seconds(q) + Gstage / 1000
```

- [ ] Preserve the signed difference for input captured before `Qplay`. Use `CheckedRational` throughout. A missing current-stage anchor, zero/invalid rate, overflow, non-finite value, or failed native conversion is Fatal; never clamp, drop, saturate, or substitute another clock.
- [ ] Remove ASIO acquisition of `ExactJudgementTimeline`, ASIO provider-domain validation, output-frame resolution, history scratch buffers, provider positions, and mutable playback epochs from the ASIO resolver/scheduler branch. Retain them only in the unchanged WASAPI branch where currently required.
- [ ] Stop `ConfigCompiler` from assigning `ExactJudgementTimelineDomain::LogicalMultimediaMilliseconds` for ASIO; it supplies an exact-timeline domain only for WASAPI (`WasapiQpc`). Keep the dormant enumerator/name temporarily so the still-compiled historical `LogicalPresentationClock.cpp` remains buildable in this intermediate commit. Task 4 deletes that clock and the now-unreferenced domain/name atomically.

### 3.7 Feed ASIO Tune from absolute `J`, without changing WASAPI

- [ ] Pass the configured audio backend into `FrameratePatchInit` instead of the ambiguous `authoritative_audio_clock_available` boolean. Give the hook plan separate explicit WASAPI-presented and ASIO-QPC branches.
- [ ] In the ASIO branch of the existing Tune song-clock hook:

```text
perform the existing sound-group-2 selected-buffer query
offer the current Play observation to the current semantic stage
capture q
resolve exact J(q)
desired_tick = floor(J(q) * configured_target_rate)
apply the existing bounded catch-up from current_tick to desired_tick
```

The observation is accepted before `q` is captured so a newly bound current
Play cannot postdate the Tune query being evaluated. This ordering does not use
time as evidence for stage eligibility; only the Play-order condition does.

- [ ] Keep the target rate as the existing positive exact numerator/denominator. Add a `GameplaySongClock` operation that accepts an already-computed absolute desired tick and only performs the existing checked delta/maximum-step calculation. It stores no ASIO epoch or observation history.
- [ ] Preserve `GameplaySongClock::Observe` and all current source-observation behavior for the WASAPI branch. Do not route the ASIO anchor into WASAPI.
- [ ] Remove ASIO epoch-change/rejection/output-frame logging. Do not add per-update timing logging.

### 3.8 Feed enabled ASIO absolute judgement from event `J`

- [ ] In the ASIO scheduler branch, resolve every accepted input record from its original captured QPC through the same immutable current-stage anchor.
- [ ] Preserve the native boundary exactly:

```text
passed_ms = checked truncation toward zero of 1000 * J(event_qpc)
native recognition receives passed_ms and the existing native frame argument
native score receives the same passed_ms
```

- [ ] Keep the native per-player base lookup and its additive live JudgTimeOffset untouched. Do not read, cache, apply, compensate, or log-transform JudgTimeOffset in loader clock code. Keep the score path's existing audio-group base untouched.
- [ ] When absolute judgement is disabled, install no judgement override hooks and leave native judgement wholly unchanged; only ASIO Tune uses the stage anchor.
- [ ] Retain existing bounded advisor-compatible judgement observations (`mapped_j`, native milliseconds, note target, native grade). Remove only ASIO provider/output-frame/recovery diagnostics.
- [ ] Route the final absolute-judgement Fatal tail to `FailAudioContract` after its existing bounded direct record; remove its separate `TerminateProcess`/fail-fast chain.

### 3.9 Format, diagnose, build, and commit

- [ ] Use CLion's format MCP on each changed C/C++ file, one file per call.
- [ ] For each changed source/header listed in this task, use the required CLion sequence: open one file, allow analysis, request that file's diagnostics, record zero errors, then continue. Do not batch and do not close CLion.
- [ ] Run a Debug compile before committing:

```powershell
cmd.exe /d /s /c 'call "C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && set "GC_ASIO_SDK_DIR=H:\gc\artifacts\ASIOSDK" && cmake --preset msvc32-debug && cmake --build --preset msvc32-debug'
```

Expected: configure and build exit 0. This is compile/static evidence only.

- [ ] Inspect the diff for an explicit untouched WASAPI branch and no new test files:

```powershell
git diff --check
git diff --name-status
git diff -- src/Audio/Wasapi src/Audio/DirectSound src/Patches/AbsoluteJudgement src/Patches/Framerate
```

- [ ] Commit:

```powershell
git add -- src/Audio src/Patches/AbsoluteJudgement src/Patches/Framerate src/Config/ConfigCompiler.cpp src/Loader/DllMain.cpp
git commit -m "refactor: make ASIO gameplay time QPC absolute"
```

---

## Task 4: Replace the physical ASIO backend wholesale

**Files:**

- Rewrite: `src/Audio/Asio/AsioOutputBackend.h`
- Rewrite: `src/Audio/Asio/AsioOutputBackend.cpp`
- Modify: `src/Audio/Asio/AsioDriver.h`
- Modify: `src/Audio/Asio/AsioDriver.cpp`
- Modify: `src/Audio/Asio/AsioTypes.h`
- Modify: `src/Audio/Asio/AsioCapabilityProbe.h`
- Modify: `src/Audio/Asio/AsioCapabilityProbe.cpp`
- Modify: `src/Audio/Asio/AsioControlPanel.h`
- Modify: `src/Audio/Asio/AsioControlPanel.cpp`
- Modify: `src/Audio/Asio/AsioProbeProtocol.h`
- Modify: `src/Audio/Asio/AsioProbeProtocol.cpp`
- Modify: `src/Audio/Asio/AsioSampleConverter.h`
- Modify: `src/Audio/Asio/AsioSampleConverter.cpp`
- Modify: `src/Audio/Asio/CMakeLists.txt`
- Modify: `src/Audio/AudioPatch.cpp`
- Modify: `src/Audio/AudioPatchInternal.h`
- Modify: `src/Audio/AudioSettings.h`
- Modify: `src/Audio/ExactJudgementTimeline.h`
- Modify: `src/Audio/ExactJudgementTimeline.cpp`
- Modify: `src/Audio/AudioBackendController.h`
- Modify: `src/Audio/AudioBackendController.cpp`
- Modify: `src/Audio/DirectSound/DirectSoundFacade.h`
- Modify: `src/Audio/DirectSound/DirectSoundFacade.cpp`
- Modify: `src/Audio/Mixer/AudioRenderCore.h`
- Modify: `src/Audio/Mixer/AudioRenderCore.cpp`
- Modify: `src/Audio/Mixer/MiniaudioMixer.h`
- Modify: `src/Audio/Mixer/MiniaudioMixer.cpp`
- Modify: `src/Audio/CMakeLists.txt`
- Modify: `src/Config/ConfigCompiler.cpp`
- Modify: `tools/ConfigGUI/AudioBackendEditorModel.cpp`
- Modify: `tools/ConfigGUI/Main.cpp`
- Delete every source/header in the Historical Deletion Manifest under `src/Audio`

Do not edit the historical 3,900-line backend incrementally. Replace `AsioOutputBackend.h/.cpp` as new files whose public and private members are derived only from the approved specification.

### 4.1 Separate isolated offline inspection from the live session

- [ ] Preserve the existing ConfigGUI helper-process isolation, pipe client, timeout/crash reporting, driver catalog, and control-panel isolation. `ProbeAsioCapability` runs inside that helper and opens its own registry-selected driver; ConfigGUI never loads the ASIO driver in its UI process.
- [ ] Rewrite the helper probe around its two existing isolated modes. `inspect` performs resolve/create/init, then queries identity, current sample rate, buffer limits, channel counts, and output-channel descriptions. `validate` additionally checks the configured frame count/pair, creates exactly that two-channel buffer with fixed inert callbacks, queries the two active channel descriptions after creation, then disposes the buffers. Neither mode calls Start, probes `outputReady`, queries latency/sample position, recovers, changes sample rate, or shares the live runtime session.
- [ ] Every helper path after successful driver creation calls Exit exactly once before returning, including an `init` or later query failure. After successful validation-buffer creation it first calls DisposeBuffers exactly once, including on a later validation failure. These are bounded isolated-helper cleanup rules, not a live-session lifecycle or recovery mechanism.
- [ ] Apply the same rule to the isolated control-panel path: every path after successful driver creation explicitly Exits once, including `init` or `controlPanel` failure. Probe/control-panel Exit failures remain bounded helper errors; they do not use the live runtime Fatal policy.
- [ ] Delete `AsioSession` rather than retaining inspect/validate/sample-rate-policy variants.
- [ ] Trim `AsioFailureStage` and `AsioCapabilityReport` fields that exist only for live runtime, recovery, foreground monitoring, multimedia timers, startup/runtime clocks, latency, outputReady probing, sample-rate restoration, or lifecycle summaries. Retain buffer-create/dispose plus the helper/process/control-panel failures required by the isolated validation contract.
- [ ] Keep only the inspection report data the GUI actually consumes: driver identity, current sample rate, buffer limits/effective configured frames, channel counts/descriptions, and selected output pair.
- [ ] Bump the isolated-probe protocol version and update serialization/deserialization atomically with the reduced report. A protocol mismatch remains a clear helper failure; do not add backward-compatibility decoding.

### 4.2 Make `AsioOutputBackend` the one session owner

- [ ] Its permanent state is limited to:

```text
driver wrapper
two ASIOBufferInfo records
frozen {sample rate, frame count, channel indices, channel sample types}
one AudioRenderCore
fixed conversion storage for both frozen output-channel types
one outputReady-supported boolean
one static atomic callback target pointer
one callback-active atomic flag
```

- [ ] Define one immutable process-lifetime `ASIOCallbacks` table containing the four static callback entry points and pass that stable address to `createBuffers`. It is constant program data, not backend/session state; never allocate, replace, clear, or synchronize it.
- [ ] It stores no HWND after `init`, focus state, controller thread, event handle, timer period, retry count, lifecycle enum, physical/logical generation, recovery flag, presentation clock, rate matcher, bridge, lease, history, deadline, cadence sample, or runtime summary counter.
- [ ] Runtime never calls `ProbeAsioCapability` or opens a preflight driver. The one live `AsioOutputBackend::Start` sequence performs its own direct validation and is the only runtime acquisition.
- [ ] Allocate the backend object and its driver-wrapper storage before opening the driver. `Start` initializes a raw backend object and transfers it into `unique_ptr` only after ASIO Start succeeds. Adoption of a successfully returned raw `IASIO*` into the preallocated wrapper is infallible. Every post-open failure calls Fatal in place and therefore performs no stack-unwind cleanup or wrapper-destructor release.
- [ ] Delete ASIO participation in the historical `AudioBackendController` startup state machine. Its ASIO branch has no `starting`/`active_asio`/`failed` state, condition-variable wait, result publication, failure continuation, or lock. It owns only the nullable live backend: null before its one synchronous start, non-null after success, and destroyed at the proven ordinary-close seam. Preserve the existing mutex/state/wait behavior only inside the unchanged WASAPI branch.
- [ ] Remove lazy allocation synchronization from the ASIO factory path. Construct its controller eagerly inside `ProductionDetourState` before the DirectSound hook is committed; `GetOrCreate` returns that fixed address directly for ASIO. Keep the current lazy mutex/attempt behavior only for WASAPI.
- [ ] Rely on the supported binary's proven single-thread program order instead of adding a reentry detector: `WinMain` runs `GWMain_RunUpdatePhase` (`0x458B70`) on its own loop thread, which reaches `LoopTask_RunStateMachine` (`0x63C860`) -> `sub_613DD0` -> `sub_613740` -> `sub_615410` -> the sole `DirectSoundCreate8` call. `sub_615410` guards that call with its initialized byte and sets the byte on success. Do not add a thread ID, mutex, wait, retry, or second-start state for ASIO.
- [ ] Add no loader-owned COM lifetime. The same native loop thread calls `CoInitializeEx(nullptr, COINIT_MULTITHREADED)` at `0x63C36F` before the DirectSound chain. Call `CoCreateInstance` directly on that already initialized caller. At loop teardown, the native game destroys its global sound owner through `sub_6103B0`, then reaches the Task 4.7 close hook immediately before its matching `CoUninitialize`; the hook destroys ASIO on that same thread. Do not call `CoInitializeEx` or `CoUninitialize` from loader code and do not create a COM/controller thread.

### 4.3 Implement exactly the approved startup order

- [ ] In one synchronous call:

1. resolve, create, and `init` the configured driver, passing the game window only as the SDK system-reference argument and never retaining it after `init` returns;
2. query current rate, buffer limits, and output count;
3. require a finite positive integral supported rate, the exact configured frame count, and two in-range adjacent selected output indices;
4. publish the callback target and call `createBuffers` for exactly those two channels using the immutable process-lifetime callback table;
5. call `getChannelInfo` for exactly the two active channels after buffer creation, and freeze their supported types;
6. construct the mixer and fixed conversion capacity for the frozen driver rate/frame count;
7. clear both channel buffers in both halves with `ClearAsioChannel`, without calling the mixer;
8. call `outputReady` once: `ASE_OK` enables callback calls, `ASE_NotPresent` disables them, every other value is Fatal;
9. call `start` once and require `ASE_OK`;
10. emit one bounded startup-format record and return the backend.

- [ ] Never call `canSampleRate` or `setSampleRate`. Use the driver's current rate, including 44,100 or 48,000 Hz when supported.
- [ ] Delete the unused wrapper operations for `canSampleRate`, `setSampleRate`, `getSamplePosition`, `getLatencies`, and `future`; no retained runtime, probe, or control-panel path needs them.
- [ ] Never query sample position, latency, ASIO time, or a startup clock. Never wait for a callback or readiness event. Never inspect focus. Never retry or fall back to WASAPI.

### 4.4 Make PCM production sequential and private

- [ ] Allow `AudioRenderCore` to operate without an `IPresentedOutputClock`; the WASAPI constructor continues to pass its existing clock and retains existing behavior.
- [ ] Add a no-timeline sequential render entry to `AudioRenderCore`/`MiniaudioMixer`. WASAPI retains the existing `MixerRenderTimeline` entry unchanged. The ASIO backend's renderer has the effective signature:

```cpp
std::span<const float> RenderPcm(std::uint32_t frame_count) noexcept;
```

- [ ] The only input is the frozen frame count. ASIO does not construct a `MixerRenderTimeline`, maintain an output-frame counter, or supply any coordinate to the mixer. Miniaudio's own decoder/resampler state advances naturally when the next fixed PCM block is read.
- [ ] In ASIO `CreateVoice`, do not configure exact playback history and do not pass a timeline publisher into the mixer. Permit `MiniaudioMixer` voices without a render timeline. The no-timeline render entry skips `PublishMappedSpans`, audible-until publication, and every other presented-coordinate publication entirely, while preserving the existing WASAPI publication branch.
- [ ] Remove the temporary Task 3 ASIO timeline plumbing in the same change: `SecondarySoundBuffer` creates no `AudioCursorTimeline` for `LogicalQpc`, resets its voice first during destruction, and closes exact history only in the `PresentedOutput` branch. The `LogicalQpc` creation and teardown paths never dereference a timeline. Preserve the WASAPI construction and voice-reset-before-history-close order unchanged.
- [ ] Require exactly the requested interleaved stereo frame count. A mixer error, active-voice short read, invalid render contract, or non-finite sample is Fatal. Ordinary no-active-voice silence remains a successful full block.
- [ ] Keep the normal render/convert/copy path allocation-free, non-throwing, and free of logging.

### 4.5 Put both callbacks on one direct synchronous path

- [ ] Delete `AsioCallbackRuntime` and define the four SDK callbacks directly on the rewritten backend.
- [ ] Publish/load/clear the single static callback target with direct release/acquire atomic operations; it carries only the backend pointer and has no wait or state-machine role. A null target while a callback is delivered is structural Fatal.
- [ ] Both `bufferSwitch` and `bufferSwitchTimeInfo` immediately test-and-set the same process-lifetime callback-active atomic flag before validation or route access. If it was already set, call Fatal. Clear it only immediately before a normal return.
- [ ] The flag has no non-callback reader, wait, queue, worker, join, shutdown, or ordering role.
- [ ] Ignore `directProcess` in both forms and synchronously:

```text
validate index 0 or 1
RenderPcm(frozen frame count)
convert to the two frozen formats
copy directly into that selected half
call outputReady iff the pre-Start probe enabled it
clear callback-active
return
```

- [ ] In the time-info form, a null pointer is Fatal. `kSampleRateChanged` or `kClockSourceChanged` is Fatal. A valid sample-rate field must be finite and exactly equal to the frozen rate; a valid speed field must be finite and exactly `1.0`. Ignore every timestamp, sample position, and time-code field and return `nullptr`.
- [ ] An invalid buffer index, conversion failure, non-finite sample, or enabled `outputReady` result other than `ASE_OK` is Fatal.

### 4.6 Implement fixed SDK messages and direct runtime Fatal

- [ ] `kAsioSelectorSupported` returns supported exactly for engine version, reset, resync, latencies changed, overload, supports time info, and supports time code. It returns unsupported for buffer-size change and every other selector.
- [ ] Return engine version `2`, supports-time-info `1`, supports-time-code `0`, and `0` for other capability queries.
- [ ] Actual reset, resync, buffer-size-change, latencies-changed, or overload delivery calls Fatal immediately. `sampleRateDidChange` always calls Fatal. Time-info rate/clock change flags call Fatal.
- [ ] Do not store a pending fault, recovery request, sticky reset, callback timestamp, deadline, or diagnostic counter.

### 4.7 Implement only ordinary shutdown

- [ ] Add one explicit driver-wrapper `Exit` operation with ASIOExit semantics: release the IASIO interface once, null the wrapper's raw pointer, and return `ASE_OK`. The wrapper destructor performs no implicit Release; every non-Fatal owner must call Exit explicitly. There is no separate runtime Release step.
- [ ] Use the proven normal-close/COM seam in the supported `game471.exe`, not a guessed window or loader callback. IDA establishes this exact same-thread sequence at preferred image base `0x00400000`:

```text
0x63C36F  native CoInitializeEx(nullptr, COINIT_MULTITHREADED)
...       WinMain update chain creates/uses the one DirectSound sound owner
0x63C5B0  native loop teardown destroys game subsystems
0x63C76A  sub_6103B0 destroys and nulls the global DirectSound sound owner
0x63C853  native call to CoUninitialize, after subsystem teardown
```

- [ ] During ASIO-only `AudioPatchInit`, preflight RVA `0x0023C853` against the exact 16-byte sequence `FF 15 3C D6 6A 00 8B E5 5D C3 CC CC CC 55 8B EC`, then create one SafetyHook mid-hook candidate at the `CoUninitialize` call. Hold that candidate locally until the existing DirectSound detour and `ProductionDetourState` commit succeed; publish its process-lifetime owner only with that successful commit, while any failed initialization destroys the uncommitted candidate before returning false. A non-preferred image base, read failure, byte mismatch, or hook-creation failure fails audio-patch initialization closed. Do not install this hook for DirectSound or WASAPI, so their shutdown behavior remains unchanged.
- [ ] Keep the committed close hook itself process-lifetime; never remove a hook from inside its own callback. Immediately before the native `CoUninitialize` executes, its one-shot callback atomically takes the ASIO `ProductionDetourState` owner and destroys it synchronously on that same loop thread. This owner pointer is not the ASIO callback route; the backend keeps that route installed through successful `Exit` as specified below. A missing owner at this ASIO-only seam is structural Fatal. Do not add a shutdown thread, event, mutex, acknowledgement, timeout, `WM_DESTROY` handler, `atexit` callback, or `DLL_PROCESS_DETACH` cleanup.
- [ ] Destruction of `ProductionDetourState` destroys the controller and its successfully constructed ASIO backend. If the backend was never created, owner destruction performs no ASIO operation. After the callback returns, native execution continues with its original `CoUninitialize`. The process-lifetime DirectSound detour and close hook remain installed while `WinMain` completes. Do not move the seam earlier than completed native sound-owner destruction or later than native COM uninitialization.
- [ ] The successfully constructed backend destructor directly performs:

```text
Stop -> DisposeBuffers -> Exit -> clear callback target
```

- [ ] Call each exactly once and require `ASE_OK`. On the first other result, call Fatal and do not execute a later cleanup step.
- [ ] Do not join callbacks, wait for acknowledgement, check focus, perform partial cleanup after startup failure, or clear the route before successful Exit.

### 4.8 Remove ASIO lifecycle reporting and fallback contracts

- [ ] Remove `AsioRuntimeCountersSnapshot`, logical/physical records, session lifecycle enums, recovery observer methods, runtime summaries, and their formatting from `AsioOutputBackend` and `AudioPatch.cpp`.
- [ ] Retain the game window only as the one-shot SDK system-reference argument required by `IASIO::init`; remove every focus/foreground use and do not store it. Remove startup timeout and `enable_absolute_time_judgement` from the physical backend API. The backend must be unable to see judgement configuration.
- [ ] Make the ASIO factory return either a live backend or not return because Fatal occurred. Remove the `AsioFailure*` continuation and ASIO failure/fallback branch from `AudioBackendController`; preserve the WASAPI startup failure path unchanged.
- [ ] Delete the backend-agnostic `AudioSettings::exact_clock_required` accessor, constructor argument, and member. Preserve existing WASAPI exact-history behavior by moving that same compiled value into a WASAPI-only `WasapiExclusiveSettings::exact_history_required` field and reading it only when constructing the WASAPI factory. `AsioSettings`, the ASIO controller/factory, and `AsioOutputBackend` carry no judgement-enable flag. `ConfigCompiler` continues to pass the value separately to `JudgementSettings` and input configuration exactly as before.
- [ ] With `LogicalPresentationClock` and its build entry deleted in this same task, delete the now-unreferenced `ExactJudgementTimelineDomain::LogicalMultimediaMilliseconds` enumerator and `"logical_multimedia_ms"` name case. Preserve `WasapiQpc`, the provider registry, and `ExactWasapiClock` unchanged.
- [ ] Delete `AsioResultDomain::winmm`, the `multimedia_timer` failure stage, and every corresponding probe decoder/formatter case in `AsioProbeProtocol.cpp`, `AudioPatch.cpp`, and `AudioBackendEditorModel.cpp`. There is no timer policy or WinMM result owner in the rewritten ASIO design.
- [ ] Remove all historical files from both `src/Audio/Asio/CMakeLists.txt` and `src/Audio/CMakeLists.txt`.
- [ ] Remove `gc_timing`, `avrt`, `winmm`, and `user32` from `gc_asio` link dependencies. They exist only for the rejected clock, worker-priority, multimedia-timer, and focus-monitor mechanisms. Retain only `gc_asio_sdk`, `ole32`, and `advapi32`, which the rewritten driver creation and registry catalog still use.

### 4.9 Validate ConfigGUI against the inspected driver format

- [ ] Update `AudioBackendEditorModel.cpp` and the display in `Main.cpp` for the reduced inspection report and failure-stage set.
- [ ] Replace both exact-48-kHz checks with validation that the inspected current rate is finite, positive, integral, and accepted by the existing mixer/format constraints.
- [ ] Continue requiring the configured frame count exactly and the selected adjacent output pair exactly. Use the isolated helper's post-create validation result and channel descriptions; never Start a validation stream and never load the driver in the GUI process.
- [ ] If the GUI displays ASIO duration, compute it from `buffer_frames / inspected_sample_rate`; otherwise display frames only. Remove latency/outputReady/overload fields that the inspection-only helper no longer reports.
- [ ] Do not change config persistence semantics or runtime values outside the existing startup correction rules.
- [ ] Verify no ASIO-specific 48-kHz assumption remains:

```powershell
rg -n "48.?000|48 kHz|48000" tools/ConfigGUI src/Audio/Asio
```

Expected: no ASIO-specific fixed-rate validation remains. Any unrelated
literal must be read in context rather than deleted mechanically.

### 4.10 Format, diagnose, build, and commit

- [ ] Format every changed C/C++ file with CLion, one file per call.
- [ ] Run CLion diagnostics sequentially, one changed file at a time using open -> analyze -> diagnostics. Do not batch or close CLion.
- [ ] Build Debug with the exact x86 command from Task 3.
- [ ] Confirm the historical files and symbols are gone:

```powershell
rg --files src/Audio | rg "AsioClock|AsioCallbackRuntime|AsioForeground|AsioPhysicalSessionController|AsioPresentation|AsioSession|AsioOutputBackendInternal|LogicalPresentation|LogicalPresentedOutput|LogicalRenderStream"
rg -n "foreground|focus|recovery|retry|replacement|rate.?match|presentation.?bridge|startup_clock|runtime_clock|GetSamplePosition|GetLatencies|SetSampleRate|CanSampleRate|\bwinmm\b|multimedia_timer|timeBeginPeriod|timeEndPeriod|timeGetTime" src/Audio/Asio src/Audio/Logical
git diff --check
```

Expected: both searches return no matches in the final ASIO/logical transport source. A generic retained WASAPI symbol outside those paths is not removed.

- [ ] Commit:

```powershell
git add -- src/Audio src/Config/ConfigCompiler.cpp tools/ConfigGUI/AudioBackendEditorModel.cpp tools/ConfigGUI/Main.cpp
git commit -m "refactor: replace ASIO recovery with one direct session"
```

---

## Task 5: Perform the final source proof and CLion audit

**Files:** Inspect every changed surviving source/header; modify only to correct a discovered violation. Do not add tests or diagnostics infrastructure.

- [ ] Confirm ASIO has no gameplay-time operand:

```powershell
rg -n "ASIO(Time|Samples|SamplePosition)|sample_position|system_time|latency|callback.*(count|time|cadence|deadline)" src/Audio/DirectSound src/Patches/AbsoluteJudgement src/Patches/Framerate
rg -n "CurrentOutputFrame|endpoint_buffer_frames|output_sample_rate" src/Audio/DirectSound/DirectSoundFacade.cpp
```

Expected: the ASIO logical branch uses only its QPC control state; presented-output/endpoint operands occur only in the explicit WASAPI branch.

- [ ] Confirm the exact consumer boundary:

```powershell
rg -n "desired_tick|configured_target_rate|\.Floor\(\)|\.Truncate\(\)|native_ms|score\(" src/Patches/Framerate src/Patches/AbsoluteJudgement
rg -n "JudgTimeOffset|judg.*offset" src/Patches/AbsoluteJudgement src/Audio
```

Expected:

- only ASIO Tune floors `J * configured_target_rate` into a Tune tick;
- enabled absolute judgement truncates `1000 * J` once into `passed_ms` and gives the same value to recognition and score;
- loader clock code does not read or apply JudgTimeOffset;
- disabled judgement has no installed override path.

- [ ] Confirm stage immutability and no reuse:

```powershell
rg -n "Play.*order|stage.*cutoff|Qplay|Splay|Gstage|Reset|EndSemanticStage|GameplayInitialization" src/Audio/DirectSound src/Patches/AbsoluteJudgement
```

Read the matched functions in CLion and trace one complete stage: entry clears/snapshots, first eligible group-2 Play copies once, later controls cannot write the anchor, exit/cancellation clears it.

- [ ] Confirm the physical callback accepts only frame count and advances sequentially:

```powershell
rg -n "RenderPcm|RenderSequential|MixerRenderTimeline|bufferSwitch|bufferSwitchTimeInfo|outputReady" src/Audio/Asio/AsioOutputBackend.cpp src/Audio/Mixer
```

Expected: the ASIO callback reaches only `RenderPcm(frame_count)` and the
no-timeline sequential mixer entry; `MixerRenderTimeline` remains reachable
only from WASAPI.

- [ ] Confirm exact ASIO startup/shutdown and fixed selector behavior by reading the final `AsioOutputBackend.cpp` in CLion from top to bottom. Record source locations for:

- post-create channel type freeze;
- both-half silence before Start;
- the single pre-Start outputReady probe;
- fixed selector replies and non-advertised buffer-size change;
- the one immutable process-lifetime callback table;
- one callback-active flag;
- every explicit runtime notification/format fault reaching Fatal;
- exact Stop -> DisposeBuffers -> Exit -> route-clear shutdown;
- no Fatal successor cleanup.

- [ ] Read the final ASIO branches of `AudioBackendController.cpp` and `AudioPatch.cpp` in CLion and record source locations proving:

- the controller and factory are eager/direct for ASIO and never enter the WASAPI mutex, condition-variable, startup-state, result-publication, or lazy-attempt paths;
- loader code calls neither `CoInitializeEx` nor `CoUninitialize` for ASIO;
- the exact preferred-base/RVA/16-byte guard for the pre-`CoUninitialize` close hook;
- the hook atomically takes the sole ASIO owner, destroys it synchronously, and leaves the ASIO callback route to the backend's exact shutdown chain;
- DirectSound and WASAPI install no close hook and keep their previous lifetime behavior.

- [ ] Search for the qualified historical ASIO mechanisms across only the relevant audio/build registrations:

```powershell
rg -n "AsioClock|AsioCallbackRuntime|AsioForegroundMonitor|AsioPhysicalSessionController|AsioPresentationBridge|AsioPresentationRateMatcher|LogicalPresentationClock|LogicalPresentedOutputClock|LogicalRenderStream|LogicalMultimediaMilliseconds|logical_multimedia_ms|focus_loss_generation|physical_session_generation|recovery_attempt|recovery_budget|recovery_pending|Asio.*[Hh]andoff|Asio.*[Ll]ease|Asio.*[Aa]cknowledg" src/Audio tests/CMakeLists.txt
```

Expected: no source/build/test reference remains. Legitimate RFID/input acknowledgements and the preserved WASAPI buffer handoff are outside this qualified search and must not be removed. Historical documentation may still contain these terms and is deliberately excluded.

- [ ] Confirm every ASIO-only WinMM result/timer remnant is gone from the retained protocol and formatters:

```powershell
rg -n "\bwinmm\b|multimedia_timer|timeBeginPeriod|timeEndPeriod|timeGetTime" src/Audio/Asio src/Audio/AudioPatch.cpp tools/ConfigGUI/AudioBackendEditorModel.cpp
```

Expected: no matches. This search deliberately excludes preserved WASAPI timing dependencies and the native game binary's own timer calls.

- [ ] Confirm the dormant judgement-to-audio flag and obsolete ASIO libraries are deleted:

```powershell
rg -n "exact_clock_required" src
rg -n "exact_history_required" src
rg -n "exact_history_required" src/Audio/Asio src/Audio/AudioBackendController.h src/Audio/AudioBackendController.cpp
rg -n "gc_timing|avrt|winmm|user32" src/Audio/Asio/CMakeLists.txt
```

Expected: the first, third, and fourth searches return no matches. Read every second-search match in CLion: matches are allowed only in `AudioSettings.h`'s `WasapiExclusiveSettings`, `ConfigCompiler.cpp`'s explicit WASAPI construction, and `AudioPatch.cpp`'s explicit `ProductionWasapiOutputBackendFactory` construction. Absolute-judgement enablement remains in its judgement/input owners plus that preserved WASAPI-only exact-history field; it is absent from generic `AudioSettings` and every ASIO type/path. The retained ASIO library list is exactly the one stated in Task 4.8.

- [ ] Run CLion formatting once per changed source file, then perform the final diagnostics pass strictly one source file at a time. Record all diagnostics. Informational inspections are not errors; every real error must be corrected before continuing.
- [ ] Run:

```powershell
git diff --check HEAD~3..HEAD
git status --short
```

Expected: no whitespace errors and no uncommitted source changes. If this audit required corrections, format/diagnose/build them and commit a narrowly named correction commit; do not fold in unrelated cleanup.

---

## Task 6: Build and run the surviving suite in both x86 configurations

**Files:** No planned source changes.

- [ ] Configure, build, and run the surviving Debug suite:

```powershell
cmd.exe /d /s /c 'call "C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && set "GC_ASIO_SDK_DIR=H:\gc\artifacts\ASIOSDK" && cmake --preset msvc32-debug && cmake --build --preset msvc32-debug && ctest --preset msvc32-debug --output-on-failure'
```

- [ ] Configure, build, and run the surviving RelWithDebInfo suite:

```powershell
cmd.exe /d /s /c 'call "C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && set "GC_ASIO_SDK_DIR=H:\gc\artifacts\ASIOSDK" && cmake --preset msvc32-release && cmake --build --preset msvc32-release && ctest --preset msvc32-release --output-on-failure'
```

Expected: both commands exit 0. The suite contains no replacement for the four rejected tests. Passing builds/tests are static evidence only and are not evidence that ASIO audio, focus behavior, judgement, or game sequence works.

- [ ] Record build identity without deploying:

```powershell
git rev-parse HEAD
Get-FileHash -Algorithm SHA256 -LiteralPath 'build-msvc32-release\dist\iDmacDrv32.dll'
Get-FileHash -Algorithm SHA256 -LiteralPath 'build-msvc32-release\dist\ConfigGUI.exe'
git status --short
```

Expected: exact commit and artifact hashes are recorded; worktree is clean.

---

## Task 7: Prepare the user-operated runtime acceptance handoff

**Files:** Runtime paths are out of repository scope and are not touched without a new explicit user instruction.

- [ ] Report the Release DLL/ConfigGUI paths, commit, and SHA-256 values. Ask the user whether they want to deploy the candidate. Do not copy, replace, stop, start, close, or restart anything without that instruction.
- [ ] If the user explicitly authorizes deployment, copy only the named candidate artifacts to the exact named destinations and verify equality:

```powershell
Get-FileHash -Algorithm SHA256 -LiteralPath 'build-msvc32-release\dist\iDmacDrv32.dll','H:\gc\iDmacDrv32.dll'
Get-FileHash -Algorithm SHA256 -LiteralPath 'build-msvc32-release\dist\ConfigGUI.exe','H:\gc\ConfigGUI.exe'
Get-FileHash -Algorithm SHA256 -LiteralPath 'H:\gc\config.toml'
```

Expected: intended/deployed artifact pairs match exactly and the config hash is recorded, not changed.

- [ ] Before interpreting a retained run, record from source/config/log:

- source commit and build/deployed DLL SHA-256;
- selected ASIO backend and exact driver identity;
- frozen current driver rate and exact buffer frames;
- effective GameTimeOffset and JudgTimeOffset;
- effective `enable_absolute_time_judgement` value.

- [ ] The user performs, without agent process control:

1. foreground startup;
2. background shortly after startup and foreground return;
3. audible menu preview plus song while backgrounding/foregrounding and moving/resizing;
4. while audible in-game PCM is already continuing with the game in the background, trigger a Windows notification/shared-mode audio attempt on the same physical output;
5. multiple menu previews;
6. two songs in one credit;
7. observation for any visible hitch;
8. judgement play with absolute judgement confirmed enabled;
9. song end through normal ranking/demo;
10. ordinary close, prompt exit, then a fresh normal start that reacquires the same ASIO driver.

- [ ] Treat any gap, silence, repeated preview section, lifecycle transition, visible hitch, hang, unexpected continuation after explicit ASIO fault, ranking/demo break, or restart acquisition failure as rejection. Do not explain a hitch from elapsed time or ordinary logging; profile it separately before attribution.
- [ ] Preserve `H:\gc\loader-log.txt` before another launch only when the current run must be retained; every launch overwrites it.
- [ ] For judgement acceptance, run the existing ConfigGUI `Analyze latest run` operation on the retained two-song log. Require:

- a stable suggestion with at least two complete songs and estimator spread no greater than 3 ms;
- ASIO estimator overlap with the accepted WASAPI `-10..-8 ms` range;
- difference between the two displayed per-song median errors no greater than the larger displayed MAD.

- [ ] An explicit reset, resync, overload, buffer/rate/latency/clock change, invalid callback structure, conversion failure, or ownership-loss report is expected to hard-crash. It must never recover, retry, fall back to WASAPI, or continue.

## Spec Coverage Checklist

- [ ] Section 2 independence: Task 3 removes ASIO operands from logical time; Task 4 removes logical time from ASIO transport.
- [ ] Section 3.1 DirectSound position: Task 3.2-3.4.
- [ ] Section 3.2 immutable stage anchor: Task 3.4-3.6.
- [ ] Section 3.3 Tune, enabled judgement, disabled judgement, and offsets: Task 3.7-3.8.
- [ ] Section 4 sequential PCM: Task 4.4.
- [ ] Section 5.1-5.2 synchronous session and immutable driver format: Task 4.1-4.3.
- [ ] Section 5.3 callback and selector contracts: Task 4.5-4.6.
- [ ] Section 5.4 focus and explicit runtime faults: Task 4.2, 4.6, and deletion proof in Task 5.
- [ ] Section 5.5 ordinary shutdown and Fatal: Task 3.1 and Task 4.7.
- [ ] Section 6 required deletion: Historical Deletion Manifest, Task 4.8, and Task 5 searches.
- [ ] Section 7.1 no-new-test policy: Task 2 and final suite inspection.
- [ ] Section 7.2-7.3 static/CLion/build proof: Tasks 5-6.
- [ ] Section 7.4-7.5 provenance and user acceptance: Task 7.

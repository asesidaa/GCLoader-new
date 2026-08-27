# ASIO Session Recovery Implementation Plan

> **Execution:** Implement inline on the current branch. Do not create a worktree,
> delegate to agents, stop any process, or deploy runtime files.

**Goal:** Make ASIO loss and reacquisition a repeatable physical-session
lifecycle while preserving one logical mixer/timeline/judgement engine, and
contain exact-playback publication failures to the timeline that owns them.

**Architecture:** A production `AsioLogicalRenderSequencer` is the sole writer
of logical render coordinates. Both detached advancement and IASIO callbacks
must acquire its non-blocking render claim, obtain a checked render plan, call
the real `AudioRenderCore`, then commit the consumed block. Physical ASIO
sessions receive independent diagnostic generations and may be discarded and
reopened without replacing the mixer, presented clock, exact endpoint, or
judgement binding. Exact-history failure remains sticky on its
`AudioCursorTimeline`, but no longer poisons the entire mixer.

**Tech stack:** Windows x86, C++23, ASIO SDK, miniaudio, CMake/MSVC, CLion
clangd/clang-tidy.

**Specification:**
`docs/superpowers/specs/2026-08-25-asio-focus-recovery-design.md`

## Constraints

- Configured ASIO must never instantiate or fall back to WASAPI.
- Foreground state comes only from the foreground monitor. Time quantifies
  logical playback advancement; it is never evidence of focus or ownership.
- Once startup commits, a physical driver/session failure enters ASIO recovery
  even while the game is foreground.
- The mixer, voices, logical output cursor, presented clock, `ExactAsioClock`
  object, and endpoint generation survive physical-session replacement.
- The callback path remains non-blocking and allocation-free.
- A completed mixer render is the logical commit point because miniaudio has
  consumed voice state even if the produced block is silence. Plan failures
  before rendering must not advance any logical state.
- Preserve unrelated dirty files. Use CLion diagnostics one file at a time by
  opening the file first; do not close editor files. Use CLion's formatter for
  modified C++ files.
- Automated tests prove only deterministic production contracts. Real IASIO
  release/reacquisition remains an in-game acceptance checkpoint.

---

### Task 1: Add a single production logical-render sequencer

**Files:**

- Create: `src/Audio/Asio/AsioLogicalRenderSequencer.h`
- Create: `src/Audio/Asio/AsioLogicalRenderSequencer.cpp`
- Create: `tests/Audio/Asio/AsioLogicalRenderSequencerTests.cpp`
- Modify: `src/Audio/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Production contract:**

- The sequencer owns the next logical output frame, the last committed
  frame/time anchor, the active physical-session generation, and the current
  raw-driver-to-logical mapping.
- `TryPlanDetached(now_ms)` and
  `TryPlanPhysical(session_generation, AsioClockDecision)` compete for one
  non-blocking claim and return a `MixerRenderTimeline` plus the timestamp that
  will become the next anchor.
- The first physical callback of a new session maps its raw coordinate to the
  frame-accurate logical position implied by the persistent time anchor. Later
  callbacks use the committed raw-to-logical mapping. Every omitted frame,
  including a sub-period remainder, becomes `discontinuity_frames`; no period
  is replayed and logical time never pauses.
- `Commit(plan)` advances logical state and installs any new physical mapping.
  `Abandon(plan)` releases ownership without changing coordinates.
- Beginning a replacement physical session increments only its diagnostic
  generation and clears only its raw mapping.

- [x] Write the sequencer test first and confirm the new target fails before
      the production type exists.
- [x] Cover hand-derived 48 kHz / 192-frame cases: a 20 ms absence maps the
      recovered block to frame 960 with a 768-frame discontinuity; a 7 ms
      absence adds 336 frames with a 144-frame discontinuity rather than
      dropping three milliseconds; the next raw period maps contiguously; a
      replacement session whose raw position starts at zero keeps the
      persistent logical cursor; an abandoned plan changes nothing; and a
      second caller cannot acquire a live claim.
- [x] Implement checked frame conversion, wrapping-millisecond comparison,
      claim ownership, plan commit/abandon, and physical-session generations.
- [x] Run only `AsioLogicalRenderSequencer` until its independent expectations
      pass.

### Task 2: Drive both ASIO render paths through the sequencer

**Files:**

- Modify: `src/Audio/Asio/AsioOutputBackend.cpp`
- Modify: `src/Audio/Asio/AsioOutputBackend.h`
- Modify: `src/Audio/AudioPatch.cpp`

**Production contract:**

- Remove the backend's separate logical-cursor, render-anchor, and physical
  mapping fields. `RenderAsioBlock` and detached silent advancement both plan
  and commit through `AsioLogicalRenderSequencer` while holding its claim
  across `AudioRenderCore::Render`.
- A competing callback clears its ASIO block and leaves the logical cursor
  untouched. Invalid physical coordinates fault only that physical session.
- Detached advancement renders/discards one current block with the complete
  frame-accurate discontinuity rather than replaying every missed period.
- Presented-clock continuity uses the committed plan timestamp and logical
  coordinates. Exact ASIO anchors resume only on stable physical callbacks and
  retain the same endpoint generation.
- Runtime summaries expose physical-session generation/loss count and logical
  discarded frames without per-callback log spam.

- [x] Replace `MapLogicalDecision`, `RenderLogicalBlock`, and the implicit
      detached cursor math with sequencer plans.
- [x] Reset the sequencer's physical mapping only after the old callback worker
      is joined and before starting the new IASIO session.
- [x] Retain the existing mixer and exact provider across every close/open.
- [x] Build the focused test target before changing lifecycle policy.

### Task 3: Recover physical faults without hiding logical faults

**Files:**

- Modify: `src/Audio/Asio/AsioCallbackRuntime.cpp`
- Modify: `src/Audio/Asio/AsioOutputBackend.cpp`
- Modify: `src/Audio/Asio/AsioOutputBackend.h`
- Modify: `src/Audio/AudioPatch.cpp`

**Production contract:**

- Runtime fault publication records whether the fault invalidates a physical
  session or the persistent logical engine.
- Reset, resync, rate/buffer/latency change, driver clock discontinuity,
  callback contract, conversion, and `OutputReady` failure are physical.
- Overload is counted but is advisory by itself and does not latch a fault.
- Exact-provider corruption, logical render arithmetic/ownership failure,
  foreground-monitor failure, and persistent endpoint-contract failure are
  logical fatal errors.
- In committed runtime, a physical fault releases IASIO, records the loss and
  generation, advances detached logical time, and retries ASIO only. A logical
  fault terminates as before. During initial foreground startup, inability to
  establish a stable physical session remains a startup failure; initial focus
  loss commits detached ASIO and later reacquires.

- [x] Add typed fault scope to the existing lock-free latch.
- [x] Route committed physical faults into the same close/retry state used by
      focus loss, regardless of current foreground state.
- [x] Keep shutdown/close failures attached to recovery diagnostics without
      replacing the original loss reason.
- [x] Verify controller startup still has no ASIO-to-WASAPI path.

### Task 4: Contain exact-history failure to its owning timeline

**Files:**

- Create: `tests/Audio/Mixer/ExactHistoryIsolationTests.cpp`
- Modify: `src/Audio/Mixer/MiniaudioMixer.cpp`
- Modify: `src/Audio/Mixer/MiniaudioMixer.h`
- Modify: `tests/CMakeLists.txt`

**Independent oracle:** A real mixer with one ordinary audible voice and one
exact candidate deliberately violates the exact timeline's same-generation
origin contract. The candidate timeline must report the precise sticky
failure, but the next mixer render must still succeed and retain the ordinary
voice's non-zero audio. This asserts endpoint isolation rather than a private
implementation detail.

- [x] Write the production-facing mixer regression test first and confirm the
      current mixer-wide `exact_publication_failed` latch makes it fail.
- [x] Remove only the mixer-global poison/return condition. Preserve the first
      exact-publication diagnostic and the timeline's sticky discontinuity.
- [x] Confirm the exact candidate can fail without converting subsequent
      endpoint blocks into mixer errors or global silence.

### Task 5: Format, diagnose, build, and hand off runtime acceptance

**Files:** All files modified by Tasks 1-4.

- [x] Run CLion's formatter on modified C++/header files.
- [x] For each modified source file, open it in CLion, request diagnostics, and
      finish that file before moving to the next. Do not close files or batch
      diagnostics.
- [x] Run `git diff --check` and inspect the focused diff without disturbing
      unrelated dirty changes.
- [x] Configure/build/test complete `msvc32-debug` and `msvc32-release` presets
      with `GC_ASIO_SDK_DIR=H:\gc\artifacts\ASIOSDK`.
- [ ] Do not deploy. Report static evidence separately and ask the user to run
      the exact reproduction: lose focus during boot, regain it, enter the
      menu, scroll long enough to cover the prior failure, then play multiple
      songs. Analyze the resulting `H:\gc\loader-log.txt` for ASIO-only
      recovery, stable logical endpoint generation, resumed current-position
      audio, and no judgement assertion.

> **ARCHIVED FAILED ATTEMPT — DO NOT EXECUTE.** The implementation produced a
> run with no registered input and no judgement, and was rolled back in full.

# Absolute-Time Judgement Driver Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers-extended-cc:subagent-driven-development (recommended) or superpowers-extended-cc:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the native uniform frame loop of `CTuneGameManager_ProcessJudgementFrame` with an absolute-time step driver so gameplay judgement uses exact song-millisecond times (press at 9999 ms against a 10000 ms note judges as 1 ms error) at any framerate, with all native query/judgement/score code untouched.

**Architecture:** Keep the native frame domain authored (1 frame = 16.666666 ms of song time, operand never rewritten). Four MidHooks: run absolute step list at `0x640239`, guard consecutive-held at `0x62DC60`, capture-ownership at `0x659920`, clock-exact-now at `0x63FA0C`. Steps run at authored frame boundaries and at journalled input-edge times; each step captures the containing frame through native capture with the input seam answering state-as-of(t), then calls the proven native pair `0x5D68E0(jstate, ms, frame)` + `0x5CF930(sstate, ms)`. Delete the HighFps bridge entirely.

**Tech Stack:** C++23 (MSVC x86), CMake presets `msvc32-debug`/`msvc32-release`, safetyhook, hand-rolled `Expect` tests, IDA-CLI daemon (`H:\gc\game471.exe.i64`) for byte freezing.

**Spec:** `docs/superpowers/specs/2026-08-19-absolute-time-judgement-driver-design.md` — read it first; §4 (verified native contract), §5 (mechanics), §10 (test validity rules) bind every task. Game-process facts in this plan cite that contract.

## Global Constraints

- **Hard goal (spec §1):** judgement values must be absolute-song-time and framerate-invariant. The 10000/9999 → 1 ms identity is the architecture's acceptance test.
- **R1:** `target_fps == 60` ⇒ no patch installs. **R4:** bool failures assert + hard-abort (`FatalRuntimeConversion` pattern); no fallbacks, latches, retries, degraded modes. **R5:** no fake-native gameplay tests, no copied native models, no FPS matrices validated against our own policy (spec §10 lists the invalid set).
- **Image base 0x400000.** All addresses in code are **RVAs** (VA − 0x400000). Site RVAs: loop-guard `0x240239` (native `jle loc_6402D0`, bytes `0F 8E 91 00 00 00`), tail `0x2402D0`, held-guard `0x22DC60`, capture `0x259920`, clock `0x23FA0C` (`mov [ebp-0xE0], ecx`), recognition core `0x1D68E0`, score `0x1CF930`, capture-fn `0x22CFB0`, fill `0x259860`, input-manager getter `0x001040`, global getter `0x0011D0`.
- **Frame constant:** `kFrameMilliseconds = 16.6666660308837890625` — the exact double value of float bits `0x41855555` (`0x6FC0A0`). Use this exact double in all scheduler math.
- **ABI (proven, spec §4):** core = `void(__thiscall*)(void* jstate, int ms, int frame)`, callee `retn 8`; score = `void(__thiscall*)(void* sstate, int ms)`, `retn 4`; capture = `int(__thiscall*)(void* booster, int frame)`; fill = `void(__thiscall*)(void* input_mgr, int target_frame)`; input-mgr/global getters = `void*(__cdecl*)()`. Never invent prototypes beyond these.
- **Object layout (proven):** `Tune+0x10` frame counter, `Tune+0x14` step; jstate list at `Tune+0x254`, sstate list at `Tune+0x26C` (MSVC vector: element `= *(void**)(begin + 4*player)`); player index `= *(uint32*)(global() + 0xCB4)`; input-mgr frame `= *(uint32*)input_mgr`; booster `= *(void**)(input_mgr + 4)`; booster last-captured `= *(uint32*)(booster + 100)`.
- **Native singletons** via the getter RVAs above; every native read/write guarded (`ReadU32Safe`/`WriteU32Safe` pattern) with assert-abort on failure.
- **Build discipline (AGENTS.md):** x86 MSVC presets only; full affected preset graph + full ctest before completion claims; `git diff --check` + `git status` before every commit. `H:\gc` runtime tree is read-only — no deployment, no gameplay-acceptance claims from static evidence. Runtime/cabinet validation is the user's separate phase.
- **Workspace:** all work happens in the existing worktree `H:\gc\artifacts\GCLoader\.worktrees\asio-audio-backend` (branch `asio-audio-backend`, base `a6f7ed1`) after Task 0 cleans its dirty state. The user declared the dirty state failed-attempt garbage (2026-08-19); no new worktree or branch is created. The main checkout's uncommitted spec/plan/tasks.json copies are the startup master copies; Task 0 copies them into the worktree and commits them.
- **Commits:** one per task, imperative subject lines matching repo history style (e.g. "Drive gameplay judgement from absolute song time").

**User decisions (already made):** 60 FPS untouched (R1); authored real-time frame semantics preserved; assert/hard-abort error policy, no fallbacks; no invented tests; every note family must work via the one mechanism (no per-family loader branches); consecutive-held guard locked in; capture-ownership detour on `0x659920`; clock exact-now locked in.

---

### Task 0: Worktree cleanup, HighFps removal, journal/anchor re-homing

**Goal:** Clean the dirty failed-attempt state from `.worktrees/asio-audio-backend` (restoring it to `a6f7ed1` plus committed history records), delete the HighFps bridge and its hook transaction, re-home the transition journal and song-time anchor math into their new homes, and leave both presets building with the full suite green.

**Files:**
- Workspace: `H:\gc\artifacts\GCLoader\.worktrees\asio-audio-backend` (branch `asio-audio-backend`); no new worktree or branch
- Restore to `a6f7ed1`: all tracked modifications/deletions (`git restore --source=a6f7ed1 --worktree --staged -- .`)
- Delete (untracked failure artifacts): `src/Input/AbsoluteTime/`, `tests/Input/AbsoluteTime/`, `docs/superpowers/specs/2026-08-18-absolute-time-judgement-redesign.md`, `docs/superpowers/plans/2026-08-18-absolute-time-judgement-redesign.md`, `tools/analysis/ida_game471.py`
- Commit as history (untracked records): `docs/reverse-engineering/high-fps-absolute-time-redesign-failure-index.md`, `docs/reverse-engineering/high-fps-input-judgement-pipeline.md`
- Delete after cleanup: `src/Input/HighFps/` (13 files), `src/Input/Switch/GameplayInputHookTransaction.{h,cpp}`, `tests/Input/HighFps/` (10 files), `tests/Input/Switch/GameplayInputHookTransactionTests.cpp`
- Create: `src/Input/Polling/InputTransitionJournal.{h,cpp}` (re-homed from `src/Input/HighFps/InputTransitionJournal.{h,cpp}`, namespace `gc::input`, record fields `{sequence, qpc, held_before, held_after, rising, falling}` — drop `cohort`/`epoch`)
- Create: `src/Patches/JudgementTiming/JudgementSongTime.{h,cpp}` (re-homed `SongTime`, `SongTimeAnchor`, `SongTimeAnchorState`, `MapQpcToSongTime` from `src/Input/HighFps/SongTime.h/.cpp`, namespace `gc::judgement_timing`)
- Modify: `src/Input/Polling/InputPollingRuntime.cpp` (journal publish call sites — drop cohort/epoch args), `src/Input/Switch/SwitchInputPatch.{h,cpp}` (remove `HighFpsJudgement` hook site, `high_fps_bridge_requested` plan flag, `GameplayQueryCallbacks` bridge fields, `ComposeGameplayQuery` bridge plumbing; keep alias/diagonal logic), `src/Patches/Framerate/FrameratePatch.{h,cpp}` (remove `BuildHighFpsInputBridgeConfig`, `HighFpsInputBridgeConfig` include/uses; keep `PublishGameplaySongTimeAnchor` call site as a compile error stub for Task 6 to finalize — temporarily `#include "Patches/JudgementTiming/JudgementSongTime.h"` and call `gc::judgement_timing::PublishSongTimeAnchor(anchor)` with a new trivial store added to `JudgementSongTime.cpp`), `src/Input/CMakeLists.txt`, `src/Patches/CMakeLists.txt`, `tests/Input/CMakeLists.txt`, `tests/Patches/CMakeLists.txt`
- Move: `tests/Input/HighFps/InputTransitionJournalTests.cpp` → `tests/Input/Polling/InputTransitionJournalTests.cpp` (namespace/type updates, cohort/epoch cases dropped); delete the other HighFps tests
- Copy into the worktree and commit: the spec `docs/superpowers/specs/2026-08-19-absolute-time-judgement-driver-design.md` and this plan + `.tasks.json` from the main checkout

**Acceptance Criteria:**
- [ ] `git -C .worktrees/asio-audio-backend status --porcelain` is empty at each commit point; `git log --oneline a6f7ed1..HEAD` shows the records commit, the spec/plan commit, then the removal/re-home commit(s)
- [ ] `git grep -n "high_fps_input\|HighFps\|GameplayInputHookTransaction\|AbsoluteTime" -- src tests` in the worktree returns nothing
- [ ] `cmake --build --preset msvc32-debug` and `msvc32-release` succeed; `ctest --preset msvc32-debug -j 4` and release pass with only the deleted tests absent
- [ ] The failure-index and pipeline-summary records exist as committed files; no untracked files remain

**Verify:** `cmake --preset msvc32-debug && cmake --build --preset msvc32-debug && ctest --preset msvc32-debug -j 4` → all pass; same for release.

**Steps:**

- [ ] **Step 1: Clean the dirty state.** In `H:\gc\artifacts\GCLoader\.worktrees\asio-audio-backend`:
  ```bash
  git add docs/reverse-engineering/high-fps-absolute-time-redesign-failure-index.md \
          docs/reverse-engineering/high-fps-input-judgement-pipeline.md
  git commit -m "Record absolute-time redesign failure index and pipeline summary"
  git restore --source=a6f7ed1 --worktree --staged -- .
  git clean -fd src/Input/AbsoluteTime tests/Input/AbsoluteTime tools/analysis \
      docs/superpowers/specs/2026-08-18-absolute-time-judgement-redesign.md \
      docs/superpowers/plans/2026-08-18-absolute-time-judgement-redesign.md
  git status --porcelain   # must be empty
  ```
  (`git clean` with explicit paths only — never a bare `-fd` at repo root. If `git status` shows anything unexpected, stop and report instead of forcing.)
- [ ] **Step 2: Bring in spec and plan.** Copy from the main checkout `H:\gc\artifacts\GCLoader\docs\superpowers\` the spec, this plan, and the `.tasks.json` into the same paths in the worktree, then:
  ```bash
  git add docs/superpowers
  git commit -m "Record absolute-time judgement driver design and plan"
  ```
- [ ] **Step 3: Delete HighFps and the hook transaction** (`git rm -r` the four delete targets). Do not touch anything else in this step.
- [ ] **Step 4: Re-home the journal.** Create `src/Input/Polling/InputTransitionJournal.{h,cpp}` from the deleted sources with namespace `gc::input`; the record becomes:
  ```cpp
  struct InputTransitionRecord {
      std::uint64_t sequence{};
      std::int64_t qpc{};
      GameplayInputMask held_before{};
      GameplayInputMask held_after{};
      GameplayInputMask rising{};
      GameplayInputMask falling{};
  };
  ```
  Keep `kTransitionCapacity = 1024`, `PushNewest`, `DrainInto`, `DiscardAll`, `depth`, `eviction_count`, `GameplayMaskFromFastIo`, and `BuildTransitionRecord` with signature `(previous_fastio, next_fastio, sequence, qpc)`. Update `InputPollingRuntime.cpp` call sites to the new signature (they pass sequence/qpc from the polling path; cohort/epoch were bridge concepts). Move and adapt `InputTransitionJournalTests.cpp`: keep ordering/lossless/capacity/eviction/mask cases with independently derived expectations; drop cohort/epoch cases.
- [ ] **Step 5: Re-home song-time math.** Create `src/Patches/JudgementTiming/JudgementSongTime.{h,cpp}` from `SongTime.h/.cpp` under `namespace gc::judgement_timing`, unchanged math (`SongTime`, `MapQpcToSongTime`, anchor struct). Add to `JudgementSongTime.{h,cpp}` a minimal thread-safe anchor store that Task 6 consumes:
  ```cpp
  // JudgementSongTime.h
  void PublishSongTimeAnchor(const SongTimeAnchor& anchor) noexcept;
  std::optional<SongTimeAnchor> LoadSongTimeAnchor() noexcept;
  ```
- [ ] **Step 6: Repair references.** In `FrameratePatch.cpp` replace `gc::high_fps_input::PublishGameplaySongTimeAnchor(anchor)` (at the former line ~1643) with `gc::judgement_timing::PublishSongTimeAnchor(anchor)` (types are structurally identical). Remove `BuildHighFpsInputBridgeConfig`, the `HighFpsInputBridge` include, and `HighFpsInputBridgeConfig` from `FrameratePatch.h`. In `SwitchInputPatch.{h,cpp}` remove the `HighFpsJudgement` site, `high_fps_bridge_requested`, the bridge fields of `GameplayQueryCallbacks`, and the `EffectiveInputQuery`/`JudgementQueryInvocation` plumbing (from the deleted `JudgementQueryContract`); keep alias composition, diagonal match, diagnostics. Update all four CMakeLists for moves/deletes/additions (`gc_input` gains `Polling/InputTransitionJournal.cpp`; `gc_runtime_patches` gains `JudgementTiming/JudgementSongTime.cpp`).
- [ ] **Step 7: Build and test both presets**, fix fallout until green, then:
  ```bash
  git add -A && git diff --check && git status
  git commit -m "Remove high-FPS bridge and re-home input transport utilities"
  ```

### Task 1: Native ABI header and frozen byte signatures

**Goal:** Freeze the four patch sites' expected original bytes from the IDB and define the proven `__thiscall` native-call types, with a validator and tests.

**Files:**
- Create: `src/Patches/JudgementTiming/NativeJudgementAbi.h`, `src/Patches/JudgementTiming/JudgementTimingSignatures.{h,cpp}`
- Test: `tests/Patches/JudgementTiming/JudgementTimingSignaturesTests.cpp`
- Modify: `src/Patches/CMakeLists.txt`, `tests/Patches/CMakeLists.txt`

**Acceptance Criteria:**
- [ ] `NativeJudgementAbi.h` declares the RVA constants from Global Constraints and exactly these types:
  ```cpp
  namespace gc::judgement_timing {
  using RecognitionStepFn = void(__thiscall*)(void* judgement_state, int recognition_ms, int frame);
  using ScoreFrameFn = void(__thiscall*)(void* score_state, int recognition_ms);
  using CaptureFrameFn = int(__thiscall*)(void* booster, int frame);
  using AdvanceCaptureFn = unsigned int(__thiscall*)(void* input_manager);
  using GetInputManagerFn = void*(__cdecl*)();
  using GetGlobalFn = void*(__cdecl*)();
  }
  ```
- [ ] Each of the four sites has a named expected-bytes array whose bytes were read from the IDB this task (provenance comment: `// IDB H:\gc\game471.exe.i64 SHA-256 3F911E...3C47163, VA <va>, read 2026-08-19`); the loop-guard array is `{0x0F, 0x8E, 0x91, 0x00, 0x00, 0x00}`
- [ ] `ValidateJudgementTimingSignatures(const SignatureSpans&)` returns the first mismatching site, and the tests exercise match + each mismatch using synthetic spans (not a copy of production arrays re-read from source)

**Verify:** `ctest --preset msvc32-debug -R JudgementTimingSignatures` → pass.

**Steps:**

- [ ] **Step 1: Freeze bytes from the IDB.** With the ida-cli daemon (`AgentSession.start(r"H:\gc\game471.exe.i64", daemon=True, ...)`), read 16 bytes at each VA — `0x640239`, `0x62DC60`, `0x659920`, `0x63FA0C` — via `ai.bytes_hex(ea, 16)`. Record them in `JudgementTimingSignatures.h` as `kLoopGuardSiteBytes` (first 6 = the jle above), `kHeldGuardEntryBytes` (first 8), `kCaptureEntryBytes` (first 8), `kClockFrameStoreBytes` (first 6, `mov [ebp-0xE0], ecx` = `89 8D 20 FF FF FF`). Verify each frozen prefix against the disassembly facts in spec §4 (jle target `0x6402D0`; `push ebp; mov ebp, esp` prologues for the two entries) before committing.
- [ ] **Step 2: Write the validator tests first** (Expect-style): synthetic matching spans → ok; each site individually corrupted → that site reported.
- [ ] **Step 3: Implement `JudgementTimingSignatures`** mirroring the existing `ValidateSwitchInputSignatures` shape (spans of live bytes read at `ExecutableBase() + rva`).
- [ ] **Step 4: Build, run, commit:** `git commit -m "Freeze judgement timing patch-site ABI signatures"`.

### Task 2: Absolute step scheduler (pure arithmetic)

**Goal:** A pure, independently-testable function that builds the ascending step list — authored frame boundaries crossed plus journalled edge times — with exact integer ms, containing frame, and recapture flag.

**Files:**
- Create: `src/Patches/JudgementTiming/JudgementStepScheduler.{h,cpp}`
- Test: `tests/Patches/JudgementTiming/JudgementStepSchedulerTests.cpp`
- Modify: `src/Patches/CMakeLists.txt`, `tests/Patches/CMakeLists.txt`

**Acceptance Criteria:**
- [ ] API:
  ```cpp
  namespace gc::judgement_timing {
  inline constexpr double kFrameMilliseconds = 16.6666660308837890625;
  struct ScheduledStep {
      std::int64_t time_1000x;   // exact time in 1/1000-ms units
      int recognition_ms;        // value passed to the native pair = floor(time_1000x / 1000)
      int frame;                 // containing authored frame
      bool recapture;            // an earlier step in this list already used this frame number
  };
  std::vector<ScheduledStep> BuildStepList(
      std::int64_t last_1000x,               // last processed time, exclusive bound
      std::int64_t now_1000x,                // inclusive bound
      std::uint32_t base_frame,              // Tune+0x10 at update start
      std::uint32_t frames_crossed,          // Tune+0x14
      std::span<const std::int64_t> edge_times_1000x); // timeline-mapped, in (last, now]
  }
  ```
  (Boundary times are exact integers in 1/1000-ms units because `kFrameMilliseconds = 50/3 ms` exactly: `boundary_units(g) = floor(g * 50000 / 3)` by integer division; `recognition_ms = floor(boundary_units / 1000)` reproduces the native `trunc(frame × float)` value for every g < 2^21. Fixtures derive these by hand with the same integer expressions as literals.)
- [ ] Hand-derived fixtures (compute expected values by hand as literals, not by calling the code): (a) identity case — `now` 10000000, edge at 9999000 → step `{9999 ms, frame 599, recapture=false}` when frame 599 was crossed this update, and `{recapture=true}` when it was crossed in an earlier update (`frames_crossed == 0`); (b) empty window (no boundaries, no edges) → empty list; (c) 3-frame stutter catch-up → 3 boundary steps at frames g+1,g+2,g+3 with `recognition_ms = trunc(g×kFrameMilliseconds)` hand-computed; (d) two edges inside one frame → second step `recapture=true`; (e) an edge exactly on a boundary → deduplicated to one step with `recapture=false`; (f) an edge time ≤ last → assert-abort (test expects abort via the repo's assert-test pattern; if the repo lacks one, test the returned `std::expected` error path instead — do not silently drop).
- [ ] Boundary steps only for frames `Tune+0x10+1 … Tune+0x10+frames_crossed` are enumerated as times, never by counting updates.

**Verify:** `ctest --preset msvc32-debug -R JudgementStepScheduler` → pass.

**Steps:**

- [ ] **Step 1: Write the failing tests** with the six fixtures above as literal expected lists.
- [ ] **Step 2: Implement:** merge boundary times `{ (Tune target frames) }` and edge times, sort ascending, dedup exact-equal times, classify each step's frame by `floor`, mark `recapture` when an earlier step in the same list already used that frame number.
- [ ] **Step 3: Green, then commit:** `git commit -m "Schedule absolute judgement steps"`.

### Task 3: Timeline mapping (anchor reuse + frame-origin correction)

**Goal:** Map a QPC timestamp to the frame-derived judgement timeline (0 at frame 0) using the published song-time anchor plus a once-captured origin correction, with checked arithmetic and abort-on-backwards.

**Files:**
- Create: `src/Patches/JudgementTiming/JudgementTimeline.{h,cpp}`
- Test: `tests/Patches/JudgementTiming/JudgementTimelineTests.cpp`
- Modify: `src/Patches/CMakeLists.txt`, `tests/Patches/CMakeLists.txt`

**Acceptance Criteria:**
- [ ] API:
  ```cpp
  namespace gc::judgement_timing {
  struct FrameTimelineOrigin {
      std::int64_t correction_1000x{};   // timeline_ms(anchor_qpc) − trunc(frame×F)
      std::uint32_t origin_frame{};
      bool valid{};
  };
  FrameTimelineOrigin CaptureFrameTimelineOrigin(
      const SongTimeAnchor& anchor, std::uint32_t target_frame); // correction = map(anchor.qpc) − units(target_frame×F)
  std::int64_t MapQpcToFrameTimeline(                      // 1/1000-ms units
      const SongTimeAnchor& anchor,
      const FrameTimelineOrigin& origin,
      std::int64_t qpc,
      std::int64_t qpc_frequency);
  }
  ```
  using the salvaged `MapQpcToSongTime` for `map(anchor.qpc …)`. Overflow-checked; a non-monotonic anchor (`qpc` older than the anchor's own with a different generation) returns an error the caller asserts on — no clamping.
- [ ] Tests with synthetic anchors (independently computed expectations): identity (correction 0), nonzero offset, fractional position handling, overflow inputs, backwards observation → error.

**Verify:** `ctest --preset msvc32-debug -R JudgementTimeline` → pass.

**Steps:** TDD as in Task 2; commit `git commit -m "Map QPC edges onto the authored frame timeline"`.

### Task 4: As-of capture seam (state-at evaluation + Switch integration)

> **SUPERSEDED (2026-08-19, post-runtime-review) — DO NOT RE-EXECUTE AS
> WRITTEN.** This task's seam was implemented, produced a total input /
> grading blackout in runtime validation, and has been deleted. The proven
> facts that invalidate it: the judgement core and every note handler read
> the `CBooster` ring through `0x659640`/`0x659570`, while the capture
> (`0x62CFB0`) samples devices via `0x633620`/`0x6335B0` — an as-of branch
> on the query hooks therefore only ever intercepted the recognition reads,
> and because `last_capture_t` was advanced to the step time before each
> recognition, the pressed window `(last_capture_t, t]` was always empty:
> every tap answered 0. The replacement (already in the tree): the query
> hooks always answer the native ring; the driver owns the ring via
> per-boundary captures and same-frame OR-recaptures; the journal drives
> step scheduling only. `CaptureAsOfContext.{h,cpp}` and
> `JudgementInputState.{h,cpp}` no longer exist. See the correction note
> in spec §5.2.

**Goal:** An as-of context that the driver sets around native captures, plus a pure state-at evaluator over the drained journal, wired into the existing Switch query hooks so capture-path queries answer state-as-of(t) and everything else answers live.

**Files:**
- Create: `src/Patches/JudgementTiming/CaptureAsOfContext.{h,cpp}` (TLS slot: `SetCaptureAsOf(int64 t_1000x)`, `ClearCaptureAsOf()`, `LoadCaptureAsOf() -> optional`)
- Create: `src/Patches/JudgementTiming/JudgementInputState.{h,cpp}` (driver-owned pending transition list + `HeldMaskAt(t_1000x)`, `RisingSince(prev_t, t)`; initial mask captured at session start)
- Modify: `src/Input/Switch/SwitchInputPatch.{h,cpp}` (query hooks consult the seam: as-of set → `JudgementInputState` answers; unset → current behavior), `src/Input/Polling/InputPollingRuntime.cpp` (no change if journal drain API already sufficient; otherwise expose `DrainPendingForDriver`)
- Test: `tests/Patches/JudgementTiming/JudgementInputStateTests.cpp`
- Modify: `src/Patches/CMakeLists.txt`, `tests/Patches/CMakeLists.txt`

**Acceptance Criteria:**
- [ ] `HeldMaskAt` folds transitions: mask before oldest retained transition = session initial mask (assert if list empty and t before session start); each transition ≤ t applies `held_after`. `RisingSince(prev, t)` returns controls whose press transition lies in `(prev, t]` and is still held at t.
- [ ] Pure evaluator tests with literal sequences: press/release ordering, two controls same frame, press before window, release before window, empty list.
- [ ] Switch hook integration: when `LoadCaptureAsOf()` has a value the held/pressed-edge hooks answer from `JudgementInputState` (pressed-edge = bit set in `RisingSince(last_capture_t, t)`); otherwise exactly today's path. The hooks are installed when the driver is enabled (extend `BuildSwitchHookPlan` with `judgement_driver_requested` replacing the deleted `high_fps_bridge_requested`).
- [ ] No loader code answers a query with anything but journal state — no direction logic, no composite/paired emulation (spec §5.5's F-003 line).

**Verify:** `ctest --preset msvc32-debug -R "JudgementInputState|SwitchInputPatch"` → pass.

**Steps:** TDD for the evaluator; hook wiring mirrors the existing `hook_pressed_edge`/`hook_held_state` structure with the branch at the top; commit `git commit -m "Answer capture-path input queries as-of song time"`.

### Task 5: Capture-ownership session state machine and alignment reset

**Goal:** The deterministic session logic deciding when the `0x659920` detour owns captures, and the alignment-reset write sequence, both testable without the game.

**Files:**
- Create: `src/Patches/JudgementTiming/CaptureOwnershipSession.{h,cpp}`
- Test: `tests/Patches/JudgementTiming/CaptureOwnershipSessionTests.cpp`
- Modify: `src/Patches/CMakeLists.txt`, `tests/Patches/CMakeLists.txt`

**Acceptance Criteria:**
- [ ] API (pure logic + an interface for the native writes):
  ```cpp
  enum class CaptureOwner { Driver, Native };
  struct AlignmentWriter {                       // implemented in Task 6 against guarded native writes
      virtual bool SetInputFrame(std::uint32_t frame) = 0;      // *(uint32*)input_mgr
      virtual bool SetBoosterLastCaptured(std::uint32_t frame) = 0; // *(uint32*)(booster+100)
      virtual bool CaptureFresh(std::uint32_t frame) = 0;       // capturefn(booster, frame) with as-of set
  };
  class CaptureOwnershipSession {
  public:
      explicit CaptureOwnershipSession(std::uint32_t looplast_ticks_per_judgement);
      void OnJudgementUpdate();                  // from the 0x640239 stub
      // Called from the 0x659920 detour on the LoopLast path (never from
      // driver-internal calls). target_frame is the driver's current
      // scheduled frame (floor(now/F)). If a session is starting, performs
      // the alignment reset through the writer before returning Driver.
      CaptureOwner OnAdvanceCaptureTick(AlignmentWriter& writer, std::uint32_t target_frame);
  };
  ```
  with `looplast_ticks_per_judgement = ceil(target_fps/60.0) + 2` computed at install.
- [ ] Behavior: first `OnJudgementUpdate` after ≥ N ticks without one starts a session (and, on the first driver-owned capture, runs the alignment reset sequence: `SetInputFrame(target−1)`, `SetBoosterLastCaptured(target−1)`, `CaptureFresh(target)` — ordering asserted via a recording fake writer); sustained updates keep `Driver`; N consecutive ticks without an update end the session (`Native` resumes, menus native); a mid-session gap ≤ N ticks keeps the session; any writer bool failure asserts (R4).
- [ ] Tests: literal tick sequences for start/sustain/end/restart, reset ordering, and the one-tick start tolerance (spec §5.2).

**Verify:** `ctest --preset msvc32-debug -R CaptureOwnershipSession` → pass.

**Steps:** TDD; the fake writer records the call sequence and expected sequence is a literal list — this tests *our write ordering*, not native behavior (spec §10 allows it); commit `git commit -m "Gate capture ownership on judgement sessions"`.

### Task 6: Driver runtime, four MidHooks, install transaction, wiring

**Goal:** The complete runtime: the `0x640239` step-list stub calling the native pair at proven ABIs, the `0x659920` schedule-advance detour, the `0x62DC60` guard, the `0x63FA0C` clock hook, the transactional installer with the 60-FPS gate, and the FrameratePatch/SongClock wiring.

**Files:**
- Create: `src/Patches/JudgementTiming/JudgementTimingDriver.{h,cpp}` (runtime state: pending journal, origin, session; `RunJudgementFrame(void* tune)`; capture helpers)
- Create: `src/Patches/JudgementTiming/JudgementTimingPatch.{h,cpp}` (signatures → hook creation order → rollback; `JudgementTimingPatchInit(bool enabled, std::uint32_t target_fps, std::int64_t qpc_frequency)`; the four MidHook handlers)
- Modify: `src/Patches/Framerate/FrameratePatch.cpp` (`GameplaySongClock::Create(60, 1)` when the driver is enabled, replacing `Create(target, 1)` at the former line ~2210 — the authored judgement clock; render-side profile unchanged), `src/Loader/DllMain.cpp` (call `JudgementTimingPatchInit` between `FrameratePatchInit` and `SwitchInputPatchInit`, passing `target_fps != 60` as the enable and aborting the loader on install failure), `src/Patches/CMakeLists.txt`
- Test: `tests/Patches/JudgementTiming/JudgementTimingPatchTransactionTests.cpp`

**Acceptance Criteria:**
- [ ] The `0x640239` MidHook handler: drain journal → map edges to timeline (Task 3) → `BuildStepList` (Task 2) → for each step: set as-of context (Task 4), advance captures to `step.frame` **through the detour's own function**: call `AdvanceCaptureFn(input_mgr)` once per not-yet-captured frame — reentrancy is explicit: the stub sets a TLS "driver-internal" flag for its duration, and the `0x659920` handler, seeing that flag, performs a raw advance (`++` input frame, `CaptureFrameFn(booster, frame)` with the current as-of, no session tick) — then recapture an already-captured frame via `CaptureFrameFn(booster, frame)` directly, then `RecognitionStepFn(jstate, ms, frame)` + `ScoreFrameFn(sstate, ms)` with jstate/sstate from the §4 layout and player from `global()+0xCB4`; clear as-of; finally `context.eip = base + 0x2402D0` (tail) — the native loop body never runs. On session start, capture the timeline origin before the first step (Task 3).
- [ ] The `0x659920` MidHook handler: if the driver-internal TLS flag is set → raw advance per above; otherwise `OnAdvanceCaptureTick(session, target_frame = floor(now_timeline/F))` (Task 5) — `Native` → do nothing (original body runs); `Driver` → advance at most to `target_frame` one frame at a time, capture with as-of set to the frame boundary (state constant since the last step — Task 4 evaluator), then set `context.eip = *(uint32*)context.esp; context.esp += 4; context.eax = 1;` (emulated `ret` of the thiscall).
- [ ] The `0x62DC60` MidHook handler: if `(booster, frame)` equals the cached pair → `context.eip = *(uint32*)context.esp; context.esp += 4;` (skip, eax left); else cache and let the original run.
- [ ] The `0x63FA0C` MidHook handler: `*(int32*)(context.ebp - 0xE0) = lround(now_ms / kFrameMilliseconds)` (now from the anchor), then let the original instruction and body run.
- [ ] Install: validate all four signatures (Task 1) → create hooks in order 1→4 → any failure resets prior hooks and returns false (loader aborts, R4). `target_fps == 60` ⇒ init returns true immediately without touching the image (R1).
- [ ] `GameplaySongClock` is constructed at authored rate when enabled; step semantics otherwise untouched; `HookGameplaySongClock` continues to publish the anchor to `gc::judgement_timing::PublishSongTimeAnchor`.
- [ ] Transaction tests (existing pattern from `FrameratePatchTransactionTests`): correct bytes install; corrupted guard at each of the four sites → abort before any activation; 60-FPS gate leaves the image untouched (assert no writes via the test hook-recording seam the transaction tests already use).
- [ ] Every native read/write guarded; every bool checked with assert-abort (`FatalRuntimeConversion` pattern); no diagnostics on the per-step path (Task 7 adds the gated trace).

**Verify:** `ctest --preset msvc32-debug -R JudgementTiming` → pass; full `ctest --preset msvc32-debug -j 4` and release pass.

**Steps:**

- [ ] **Step 1: Write the transaction tests first** (site-guard rejection for each of the four sites, rollback order, 60-FPS no-op).
- [ ] **Step 2: Implement `JudgementTimingPatch`** — signature validation, ordered `safetyhook::create_mid` for the four sites (same pattern as the diagonal-match MidHook), rollback on failure, init gate.
- [ ] **Step 3: Implement `JudgementTimingDriver`** with the four handlers per the AC; all native calls through the Task 1 `__thiscall` pointer types resolved at `ExecutableBase() + rva`; session and alignment from Task 5 (the production `AlignmentWriter` doing the guarded writes).
- [ ] **Step 4: Wire FrameratePatch + DllMain** per the AC; build and run the full suites both presets.
- [ ] **Step 5: Commit:** `git commit -m "Drive gameplay judgement from absolute song time"`.

### Task 7: Compile-gated step trace (user's runtime-instrumentation hook)

**Goal:** A default-off compile gate that logs each step/capture/pair-call so the user's native-process validation phase has evidence output, without touching normal-path behavior when disabled.

**Files:**
- Create: `src/Patches/JudgementTiming/JudgementTimingTrace.{h,cpp}` (`GC_JUDGEMENT_TIMING_TRACE` macro, no-op unless `GC_ENABLE_JUDGEMENT_TIMING_TRACE != 0`; one plog line per step: `ms`, `frame`, `recapture`, captured frame span, pair-call)
- Modify: `src/Patches/JudgementTiming/JudgementTimingDriver.cpp` (trace points), CMake option `GC_ENABLE_JUDGEMENT_TIMING_TRACE` default OFF, `src/Patches/CMakeLists.txt`

**Acceptance Criteria:**
- [ ] With the option OFF, the driver compiles to identical behavior and emits nothing (AGENTS.md per-call logging rule).
- [ ] With the option ON, each step logs the fields above through the existing plog setup.
- [ ] No unit test asserts on log content (runtime evidence only — spec §10).

**Verify:** build both presets with the option OFF (default) green; one manual debug build with `-DGC_ENABLE_JUDGEMENT_TIMING_TRACE=ON` compiles.

**Steps:** implement, build both ways, commit `git commit -m "Add gated judgement step trace"`.

### Task 8: Full verification pass

**Goal:** Final cross-task verification and commit hygiene before handing off to the user's runtime validation phase.

**Files:** none new; verification only.

**Acceptance Criteria:**
- [ ] `cmake --preset msvc32-debug && cmake --build --preset msvc32-debug && ctest --preset msvc32-debug -j 4` — full pass
- [ ] Same for `msvc32-release`
- [ ] `git grep -n "high_fps_input\|HighFps\|AbsoluteTime" -- src tests` → empty; `git diff --check` clean; `git status` clean; every task has its own commit
- [ ] Final summary states explicitly: static/build evidence only; no gameplay claim; runtime instrumentation and cabinet validation (60/144/165/240, the 10000/9999 identity, per-family scripted chart) remain for the user's phase per spec §10

**Verify:** the four commands above with their outputs captured.

**Steps:** run the verification commands, fix any fallout (bounded to integration mistakes — design changes require stopping and consulting the plan author), final commit if needed.

---

## Out of plan scope (user-driven, after this plan)

Native-process instrumentation runs and cabinet/operator acceptance at 60/144/165/240 FPS, the 10000/9999 identity check under instrumentation, watchdog-tolerance observation, and consecutive-held/feel validation — spec §10's runtime obligations. No static result in this plan may be reported as gameplay acceptance.

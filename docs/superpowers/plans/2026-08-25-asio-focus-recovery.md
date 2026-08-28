# ASIO Focus Recovery Implementation Plan

> **Superseded:** Do not execute this plan. The replacement design is
> `docs/superpowers/specs/2026-08-29-asio-logical-time-presentation-rewrite-design.md`.

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Release IASIO when the game loses foreground ownership and recover a fresh session without losing mixer, voice, cursor, or judgement-clock continuity.

**Architecture:** Keep one logical DirectSound/miniaudio engine and exact-clock provider for the backend lifetime, while making IASIO sessions replaceable. An audio-owned WinEvent monitor drives a control-thread lifecycle state machine; background renders advance voices silently and new driver coordinates are rebased onto the persistent output-frame domain.

**Tech Stack:** Windows x86, C++23, ASIO SDK, WinEvent hooks, miniaudio, CMake/MSVC, CLion inspections.

**Spec:** `docs/superpowers/specs/2026-08-25-asio-focus-recovery-design.md`

## Global Constraints

- Foreground transitions and direct foreground-state queries are the only lifecycle authority.
- Time may schedule silent playback advancement and retry attempts but may not infer focus or driver ownership.
- The logical exact-clock provider pointer and endpoint generation survive every focus transition.
- Focus loss fully stops, disposes, closes, and releases the physical IASIO session.
- Active voices advance silently while IASIO is absent.
- Reacquisition retries only while explicitly foreground and never falls back to WASAPI mid-run.
- Preserve unrelated worktree changes and do not deploy to `H:\gc`.
- The repository has no test tree; do not recreate a test target without an independent oracle.

---

### Task 1: Audio-owned foreground monitor

**Files:**
- Create: `src/Audio/Asio/AsioForegroundMonitor.h`
- Create: `src/Audio/Asio/AsioForegroundMonitor.cpp`
- Modify: `src/Audio/Asio/CMakeLists.txt`

**Interfaces:**
- Produces: `AsioForegroundMonitor::Start(HWND, AsioFailure*) noexcept`
- Produces: `HANDLE change_event() const noexcept`
- Produces: `bool is_foreground() const noexcept`
- Produces: deterministic shutdown that unhooks WinEvent on the installing thread.

- [ ] **Step 1: Confirm the production target currently has no foreground-monitor source**

Use CLion project search for `EVENT_SYSTEM_FOREGROUND` under
`src/Audio/**`. Expected: no match.

- [ ] **Step 2: Add the monitor interface and implementation**

Implement a dedicated message-loop thread that installs
`SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, ...,
WINEVENT_OUTOFCONTEXT)`. Use thread-local callback routing because Windows
delivers out-of-context events on the installing thread. The callback performs
only process-ID comparison, atomic publication, and event signaling.

- [ ] **Step 3: Add the new source to the ASIO CMake target**

List `AsioForegroundMonitor.cpp` and `AsioForegroundMonitor.h` beside the
other ASIO runtime sources.

- [ ] **Step 4: Run CLion diagnostics**

Inspect both new files with warnings enabled. Expected: no errors or warnings.

### Task 2: Persistent logical render and clock domain

**Files:**
- Modify: `src/Audio/Asio/AsioClock.h`
- Modify: `src/Audio/Asio/AsioClock.cpp`
- Modify: `src/Audio/Asio/AsioOutputBackend.cpp`

**Interfaces:**
- Consumes: `AsioForegroundMonitor` from Task 1.
- Produces: a single backend-lifetime `AudioRenderCore`,
  `AsioPresentedClockPublication`, `ExactAsioClock`, and logical output
  cursor.
- Produces: explicit presented-clock publication for discarded logical renders
  without publishing synthetic exact ASIO anchors.

- [ ] **Step 1: Establish the current compile baseline**

Run the affected x86 Debug build before editing production behavior. Record any
pre-existing failure separately.

- [ ] **Step 2: Add explicit logical-silence clock publication**

Add a narrowly named method to `AsioPresentedClockPublication` that publishes
a valid presented/submitted logical frame pair using an explicit multimedia
timestamp. It must preserve monotonic `CurrentOutputFrame()` behavior and
must not touch `ExactAsioClock`.

- [ ] **Step 3: Create logical engine state once**

Move render-core creation, presented-clock ownership, exact-clock creation,
registration, output format publication, and logical cursor initialization out
of physical-session initialization. Retain the registry and driver factory for
repeated use instead of consuming them during first startup.

- [ ] **Step 4: Add silent mixer advancement**

Advance the persistent render core in complete `request_.buffer_frames`
periods based on a monotonic playback anchor. Discard samples, advance the
logical output cursor, publish only the normal presented cursor, and keep exact
hardware-anchor publication suspended.

- [ ] **Step 5: Run CLion diagnostics**

Inspect `AsioClock.*` and `AsioOutputBackend.cpp`. Expected: no new errors or
warnings.

### Task 3: Replaceable IASIO session lifecycle

**Files:**
- Modify: `src/Audio/Asio/AsioOutputBackend.h`
- Modify: `src/Audio/Asio/AsioOutputBackendInternal.h`
- Modify: `src/Audio/Asio/AsioOutputBackend.cpp`
- Modify: `src/Audio/AudioPatch.cpp`

**Interfaces:**
- Consumes: persistent logical engine and foreground monitor from Tasks 1-2.
- Produces: active, releasing, background-silent, reacquiring, and fatal
  lifecycle states.
- Produces: session-local reset/close operations and raw-driver-to-logical-frame
  rebasing.
- Produces: transition diagnostics through `IAsioOutputObserver`.

- [ ] **Step 1: Split physical session open and close operations**

Create repeatable operations that resolve the configured driver, create a new
IASIO object, prepare/install callbacks, create buffers, configure channel
views, reset the session clock tracker, start, and await stable callbacks.
Closing must disable render entry, stop, join/uninstall callbacks, dispose
buffers, release IASIO, clear views, and leave the logical engine untouched.

- [ ] **Step 2: Rebase replacement-session callback coordinates**

On the first stable callback of each session, pair its raw render coordinate
with the persistent next logical frame. Map subsequent raw presented/render
coordinates with checked arithmetic, reject within-session discontinuity, and
publish exact anchors using the unchanged logical endpoint generation.

- [ ] **Step 3: Implement the committed lifecycle loop**

Wait for shutdown, foreground changes, session faults, runtime-summary
deadlines, and retry deadlines. Foreground loss releases IASIO then enters
silent rendering. Foreground gain attempts a fresh session and retries after a
fixed scheduling delay only while still foreground.

- [ ] **Step 4: Make fault classification foreground-authoritative**

Before reporting a committed session fault as fatal, query the actual
foreground process. If the game is background, convert it into normal session
release and recovery state. If foreground, retain the existing typed fatal
path.

- [ ] **Step 5: Add bounded transition diagnostics**

Extend the observer/reporting path for foreground loss, release, first
reacquisition failure, and successful restoration with attempt count. Do not
log each callback or every retry.

- [ ] **Step 6: Run CLion diagnostics**

Inspect every modified source file with warnings enabled. Expected: no errors
or warnings.

### Task 4: Static verification and runtime handoff

**Files:**
- Verify: all files changed by Tasks 1-3.

**Interfaces:**
- Produces: static build evidence and a precise runtime acceptance checklist.

- [ ] **Step 1: Inspect the focused diff**

Run `git diff --check`, inspect `git status --short`, and review only the
ASIO focus-recovery paths while preserving all unrelated dirty files.

- [ ] **Step 2: Build Debug**

Set `GC_ASIO_SDK_DIR=H:\gc\artifacts\ASIOSDK`, configure with
`msvc32-debug` if required, and build the complete preset graph. Expected:
successful exit.

- [ ] **Step 3: Build Release**

Set the same SDK path, configure with `msvc32-release` if required, and build
the complete preset graph. Expected: successful exit.

- [ ] **Step 4: Re-run CLion diagnostics and diff checks**

Confirm no new IDE errors/warnings and no whitespace errors after compiler-led
fixes.

- [ ] **Step 5: Report the runtime acceptance boundary**

Do not deploy automatically. Ask the user to deploy/test focus loss and regain
mid-song, then inspect `H:\gc\loader-log.txt` for explicit release,
reacquisition, stable logical generation, and absence of judgement assertions.

# ASIO STA Owner Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans. Execute inline on the current branch; do not create a worktree or use subagents.

**Goal:** Replace the rejected caller-thread ASIO lifetime with the reviewed one-STA-owner design while leaving the independent QPC song/judgement timeline and WASAPI behavior unchanged.

**Normative specification:** `docs/superpowers/specs/2026-08-29-asio-logical-time-presentation-rewrite-design.md` at reviewed commit `e30913b97c9e58905bb3e8006ae46e55741e10a2` (tree `2d2c5a19e071d6eef802869f8ca46eb1069b0b0b`).

**Implementation rule:** Delete the rejected ASIO ownership/lifecycle behavior. Add only one dedicated STA owner, two one-shot handles, one immutable service publication, one callback route, and one callback-active Fatal detector. Do not add recovery, retries, fallbacks, polling, timeouts, clocks, histories, generations, command queues, acknowledgements, focus handling, or driver-specific paths.

**Test exception:** The normative specification explicitly forbids new ASIO tests. Do not add or restore tests. Existing tests are run only as regression evidence; runtime evidence from the user's target session is the only acceptance gate.

---

## Task 1: Make the ASIO backend a game-facing shell

**Files:**
- Modify: `src/Audio/Asio/AsioOutputBackend.h`

Replace the shell's ownership of IASIO, buffers, format, mixer storage, and callback statics with:

```cpp
struct PublishedServices final {
    AudioRenderCore* render_core{};
    std::uint32_t endpoint_buffer_frames{};
    std::uint32_t output_sample_rate{};
};

std::thread owner_thread_;
HANDLE startup_complete_{};
HANDLE shutdown_requested_{};
PublishedServices services_{};
```

Change `Start` so it takes ownership of the two startup providers:

```cpp
static std::unique_ptr<AsioOutputBackend> Start(
    HWND game_window,
    AsioStreamRequest request,
    std::unique_ptr<IAsioRegistrySource> registry,
    std::unique_ptr<IAsioDriverFactory> driver_factory,
    std::shared_ptr<const ma_allocation_callbacks> allocation_callbacks = {}) noexcept;
```

Declare one private static owner entry that receives the request, moved providers, allocation callbacks, copied handle values, and a pointer to the empty service slot. It must not receive or capture an `AsioOutputBackend*`.

Keep the existing public audio service surface. `CreateVoice` forwards only through the published `AudioRenderCore*`; frame count and sample rate return copied immutable values; output-frame and legacy diagnostics remain unavailable/no-op.

The destructor performs only: signal shutdown, join the owner, close the two handles. Any failure is immediate Fatal.

## Task 2: Put the complete live ASIO session on the STA

**Files:**
- Replace implementation: `src/Audio/Asio/AsioOutputBackend.cpp`

Create a private `LiveAsioSession` owned solely by the owner-thread stack. It owns:

- `IAsioDriver`;
- ASIO buffer descriptors;
- frozen sample rate, frame count, channel indices, and channel sample types;
- `AudioRenderCore`;
- fixed conversion storage; and
- frozen `outputReady` support.

Keep exactly these process-wide callback routing objects:

```cpp
std::atomic<LiveAsioSession*> callback_target{};
std::atomic_flag callback_active = ATOMIC_FLAG_INIT;
```

Implement startup in this exact order:

1. The caller fully allocates the shell and both manual-reset events.
2. The caller starts the owner with moved startup inputs and copied handle values, then waits indefinitely for startup-complete.
3. The owner calls `CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE)`; only `S_OK` and `S_FALSE` continue.
4. The owner creates its message queue with `PeekMessageW(..., PM_NOREMOVE)`.
5. The owner resolves, creates, and initializes the selected driver on that STA.
6. It queries and freezes current driver sample rate, exact configured buffer size, output count, and selected channels.
7. It supplies fixed callbacks and creates buffers while `callback_target` is null.
8. It queries/finalizes the two channel sample types.
9. It constructs `AudioRenderCore` and fixed conversion storage.
10. It fills both ASIO halves with format-correct digital silence without calling `RenderPcm`.
11. It probes `outputReady` once; accept only `ASE_OK` or `ASE_NotPresent`.
12. It publishes `callback_target` once immediately before `Start`, then requires `Start == ASE_OK`.
13. It writes the three immutable service fields, logs bounded startup identity/format, signals startup-complete once, and discards its service-slot pointer.
14. It enters an unbounded `MsgWaitForMultipleObjectsEx` loop that only dispatches queued STA messages or consumes shutdown.

There is no startup timeout, retry, fallback, focus branch, second session, or partial-failure cleanup. Every startup failure calls the existing non-returning ASIO Fatal boundary.

Implement ordinary shutdown only on the owner STA:

```text
Stop -> DisposeBuffers -> Exit -> callback_target = null
-> destroy LiveAsioSession -> CoUninitialize -> return
```

Require `ASE_OK` at each driver step. A failure stops immediately at Fatal; it does not run a later cleanup step.

## Task 3: Make callbacks independent of shell and lifecycle state

**Files:**
- Modify: `src/Audio/Asio/AsioOutputBackend.cpp`

Use fixed, target-independent `asioMessage` capability replies so valid capability queries work while the callback route is null. Do not advertise `kAsioBufferSizeChange`.

For `bufferSwitch` and `bufferSwitchTimeInfo`:

1. load and validate `callback_target`;
2. set the one non-blocking `callback_active` bit and Fatal if it was already set;
3. render exactly the frozen frame count;
4. convert/copy into the selected half;
5. call `outputReady` only when the startup probe froze it as supported;
6. clear the active bit immediately before normal return.

The time-info callback validates only physical invariants: non-null input, no sample-rate/clock-source change flags, exact finite frozen sample rate when valid, and finite speed exactly `1.0` when valid. It ignores all ASIO timestamps/sample positions and returns `nullptr`.

Direct notifications for reset, resync, buffer-size change, latency change, overload, and sample-rate change call Fatal immediately in startup, running, or shutdown. No notification creates state or schedules work.

## Task 4: Transfer startup-provider ownership and link the STA message API

**Files:**
- Modify: `src/Audio/AudioPatch.cpp`
- Modify: `src/Audio/CMakeLists.txt`
- Inspect, expected unchanged: `src/Audio/AudioBackendController.cpp`
- Inspect, expected unchanged: `src/Audio/AudioBackendController.h`

Make `ProductionAsioOutputBackendFactory` own the production registry source and driver factory through `std::unique_ptr`, then move both into the single `AsioOutputBackend::Start` call. The factory is one-shot because the audio controller already starts one backend once.

Add `user32` only to the `gc_audio` link dependencies for `PeekMessageW`, `MsgWaitForMultipleObjectsEx`, `TranslateMessage`, and `DispatchMessageW`.

Do not change the WASAPI branch, controller state machine, DirectSound public contract, focus hooks, or game timing.

## Task 5: Prove the source matches the reviewed split

**Files to inspect without expected edits:**
- `src/Audio/DirectSound/DirectSoundFacade.h`
- `src/Audio/DirectSound/DirectSoundFacade.cpp`
- `src/Audio/DirectSound/GameplayAudioCursorObservation.h`
- `src/Audio/DirectSound/GameplayAudioCursorObservation.cpp`
- `src/Audio/ExactJudgementTimeline.h`
- `src/Audio/ExactJudgementTimeline.cpp`
- `src/Patches/AbsoluteJudgement/JudgementClockResolver.h`
- `src/Patches/AbsoluteJudgement/JudgementClockResolver.cpp`
- `src/Patches/AbsoluteJudgement/AbsoluteJudgementRuntime.cpp`
- `src/Patches/Framerate/FrameratePatch.cpp`

Trace and record static evidence that:

- ASIO only receives sequential frame-count PCM pulls;
- no ASIO value reaches DirectSound logical cursors, Tune, input timestamps, judgement, GameTimeOffset, or JudgTimeOffset;
- the QPC Play anchor is the sole current-stage song/judgement coordinate;
- enabled absolute judgement converts the captured input QPC through that anchor, while disabled judgement remains native;
- stage exit discards the anchor;
- no focus/recovery/retry/reconciliation implementation remains; and
- WASAPI code is unchanged.

If the trace exposes a direct contradiction, correct only that concrete contradiction in the owning file; do not introduce another abstraction or lifecycle model.

## Task 6: Format, diagnose, build, and commit

Use CLion formatting on every changed C/C++ file. For diagnostics, handle one source file at a time: open it, allow analysis, request its diagnostics, then move to the next file. Do not close files or CLion.

From the repository root, use:

```powershell
cmd /d /s /c "\"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat\" && set GC_ASIO_SDK_DIR=H:\gc\artifacts\ASIOSDK && cmake --preset msvc32-debug && cmake --build --preset msvc32-debug && ctest --preset msvc32-debug -j 4"
cmd /d /s /c "\"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat\" && set GC_ASIO_SDK_DIR=H:\gc\artifacts\ASIOSDK && cmake --preset msvc32-release && cmake --build --preset msvc32-release && ctest --preset msvc32-release -j 4"
git diff --check
git status --short
```

Inspect the built x86 DLL and record its SHA-256 as static build identity. Do not deploy it to `H:\gc`.

Commit the complete implementation after static checks. Report build/test/diagnostic results only as static evidence. The user performs the normative runtime matrix and decides acceptance.

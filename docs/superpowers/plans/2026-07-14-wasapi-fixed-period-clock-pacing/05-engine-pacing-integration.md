# Engine Pacing Integration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Anchor the stream clock before start, integrate fixed-slot pacing and mixer discontinuities, expose software stream latency, and report recoverable versus chronic pacing accurately.

**Architecture:** The audio thread reads the stopped stream clock after prefill, maps it to output frame zero, starts the endpoint, and then services whole-buffer events. `OutputPacingTracker` plans each block; the engine commits its tail only after successful release. The existing monitor owns all formatting and fatal user guidance.

**Tech Stack:** C++23, WASAPI, `IAudioClock`, miniaudio, Win32 events/MMCSS, CTest.

## Global Constraints

- Apply every constraint in `README.md`.
- Integrate the existing uncommitted configuration/duration handoff diagnostics
  rather than discarding them.
- The first post-start clock sample seeds QPC lateness and is never compared
  against the pre-start sample.
- `FrameratePatch.cpp` remains unchanged.

---

### Task 1: Endpoint stream-latency metadata

**Files:**
- Modify: `WasapiEndpoint.h`
- Modify: `WasapiEndpoint.cpp`
- Modify: `tests/WasapiEndpointTests.cpp`
- Modify: `tests/ExclusiveAudioEngineTests.cpp`

**Interfaces:**

Add to `IWasapiApi`:

```cpp
virtual HRESULT GetStreamLatency(REFERENCE_TIME*) noexcept = 0;
```

Add to `EndpointInitialization`:

```cpp
REFERENCE_TIME stream_latency{};
HRESULT stream_latency_result{E_NOTIMPL};
bool stream_latency_available{};
```

- [ ] **Step 1: Write failing latency metadata tests**

Assert a successful fake query stores the exact value. Assert a failed query
stores its HRESULT, leaves availability false, and does not fail endpoint
creation.

- [ ] **Step 2: Implement production and endpoint query**

`ProductionWasapiApi::GetStreamLatency` delegates to
`IAudioClient::GetStreamLatency`. Query after successful initialization and
actual buffer validation, preserving failure only as metadata.

### Task 2: Pre-start origin and paced render loop

**Files:**
- Modify: `ExclusiveAudioEngine.h`
- Modify: `ExclusiveAudioEngine.cpp`
- Modify: `ExclusiveAudioEngineInternal.h`
- Modify: `tests/ExclusiveAudioEngineTests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**

Add failure stages:

```cpp
InvalidClockPosition,
ChronicOutputGap,
```

Retain the split cursor counters from Plan 4 and add:

```cpp
std::uint64_t confirmed_gap_events{};
std::uint64_t skipped_output_frames{};
std::uint64_t maximum_skipped_output_frames{};
std::uint64_t chronic_pacing_failures{};
std::int64_t current_submitted_lead_frames{};
std::int64_t minimum_submitted_lead_frames{};
```

- [ ] **Step 3: Write failing engine call-order and pacing tests**

Extend the fake call log and assert:

1. `GetClockPosition` occurs before `Start`;
2. the pre-start clock maps to output frame zero;
3. the first event renders at one packet after prefill;
4. a normal event produces `MixerRenderTimeline{tail, 0}`;
5. presentation beyond the tail produces one gap and the aligned block begin;
6. the engine commits the tracker only after `ReleaseRenderBuffer` succeeds;
7. a release failure leaves the committed tail unchanged;
8. two gap events recover and update counters;
9. the third gap within one second records `ChronicOutputGap`, attempts silence
   only when permitted, and reaches `RuntimeFailed` on the monitor thread;
10. a QPC late wake with presentation still inside the submitted tail increments
    only `late_event_wakes`;
11. repeated/equal clock positions are valid and regression is
    `InvalidClockPosition`;
12. pending and unmapped cursor counters are independently visible.

- [ ] **Step 4: Run engine tests and verify red**

Reconfigure for the new tracker source, then run:

```powershell
& $env:ComSpec /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build build-msvc32-latest --target WasapiEndpointTests ExclusiveAudioEngineTests && ctest --test-dir build-msvc32-latest -R "^(WasapiEndpointTests|ExclusiveAudioEngineTests)$" --output-on-failure'
```

Expected: old start/clock order and sequential `submitted_frames_` logic fail.

- [ ] **Step 5: Establish clock origin before start**

After mixer/vector creation and before `Start`:

```cpp
EndpointClockPosition origin{};
if (FAILED(endpoint_->ReadClock(&origin, &failure))) {
    // existing startup failure cleanup
}
clock_mapper_.Reset(origin.position, initialization_.clock_frequency, 0);
pacing_tracker_.emplace(initialization_.actual_buffer_frames);
if (FAILED(endpoint_->Start(&failure))) {
    // existing startup failure cleanup
}
last_qpc_100ns_ = 0;
```

Do not seed late-wake timing from `origin.qpc_100ns`.

- [ ] **Step 6: Integrate pacing decisions**

The render loop maps clock position and calls `Plan`. Handle decisions:

```cpp
case OutputPacingDecisionKind::Sequential:
case OutputPacingDecisionKind::RecoverableGap:
    rendered = mixer_->Render(float_mix_, {
        decision.block_begin,
        decision.discontinuity_frames,
    });
    break;
case OutputPacingDecisionKind::ChronicGap:
    RecordRuntimeFailure({AudioFailureStage::ChronicOutputGap, E_FAIL});
    break;
case OutputPacingDecisionKind::InvalidClock:
    RecordRuntimeFailure({AudioFailureStage::InvalidClockPosition, E_FAIL});
    break;
}
```

After successful `SubmitPcm16`, require `pacing_tracker_->Commit(decision)` and
publish its tail. Update gap/lead counters with relaxed atomics. The first
post-start event only seeds `last_qpc_100ns_`; later events use the existing
1.5-period lateness diagnostic.

### Task 3: Observer text and duration handoff integration

**Files:**
- Modify: `WasapiAudioPatch.cpp`
- Modify: `WasapiAudioPatchInternal.h`
- Modify: `tests/WasapiAudioPatchTests.cpp`
- Modify: `docs/superpowers/plans/2026-07-12-wasapi-exclusive-low-latency-audio/12-configurable-fixed-buffer-duration.md`

- [ ] **Step 7: Extend failing text assertions**

Startup text must include stream latency or `stream_latency=unavailable`.
Runtime summary/fatal text must include `confirmed_gap_events`,
`skipped_output_frames`, `maximum_skipped_output_frames`,
`pending_cursor_queries`, `unmapped_cursor_failures`, signed lead, and chronic
failure count. The chronic fatal message must contain
`increase wasapi_exclusive_buffer_ms and restart`.

Retain the already-written assertions that the parsed 10 ms value reaches the
config log, detour state, and production engine start.

- [ ] **Step 8: Implement non-real-time formatting**

Only observer/initialization code formats the new fields. The render loop
updates counters but calls no logger. Map new startup and runtime failure stages
to stable stage names in the existing failure-text switch.

- [ ] **Step 9: Verify all focused audio targets**

```powershell
& $env:ComSpec /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build build-msvc32-latest --target WasapiEndpointTests ExclusiveAudioEngineTests WasapiAudioPatchTests SecondarySoundBufferTests MiniaudioMixerTests OutputPacingTrackerTests iDmacDrv32 && ctest --test-dir build-msvc32-latest -R "^(WasapiEndpointTests|ExclusiveAudioEngineTests|WasapiAudioPatchTests|SecondarySoundBufferTests|MiniaudioMixerTests|OutputPacingTrackerTests)$" --output-on-failure'
```

Expected: all focused targets pass and `iDmacDrv32.dll` links.

- [ ] **Step 10: Commit**

```powershell
git add -- CMakeLists.txt WasapiEndpoint.h WasapiEndpoint.cpp ExclusiveAudioEngine.h ExclusiveAudioEngine.cpp ExclusiveAudioEngineInternal.h WasapiAudioPatch.cpp WasapiAudioPatchInternal.h tests/WasapiEndpointTests.cpp tests/ExclusiveAudioEngineTests.cpp tests/WasapiAudioPatchTests.cpp docs/superpowers/plans/2026-07-12-wasapi-exclusive-low-latency-audio/12-configurable-fixed-buffer-duration.md
git commit -m "fix: harden WASAPI clock pacing"
```

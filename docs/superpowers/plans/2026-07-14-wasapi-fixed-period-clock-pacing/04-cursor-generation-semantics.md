# Cursor Generation Semantics Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Return an immediate stable cursor after game play/seek controls without treating the interval before presentation as a timeline failure.

**Architecture:** Timeline lookup returns a status that distinguishes a resolved segment, a generation that is accepted but not yet presented, and a genuine missing active segment. Each new play run and seek has a unique generation; only the audio thread writes timeline slots.

**Tech Stack:** C++23 atomics/seqlock ring, DirectSound 8 facade, CTest.

## Global Constraints

- Apply every constraint in `README.md`.
- Game threads never publish render spans.
- Pending-generation queries return the requested anchor and increment only the
  expected-pending counter.
- Endpoint clock API failure is not double-counted as a timeline failure.

---

### Task 1: Status-bearing timeline lookup

**Files:**
- Modify: `AudioCursorTimeline.h`
- Modify: `AudioCursorTimeline.cpp`
- Modify: `DirectSoundFacade.h`
- Modify: `DirectSoundFacade.cpp`
- Modify: `MiniaudioMixer.cpp`
- Modify: `ExclusiveAudioEngine.h`
- Modify: `ExclusiveAudioEngine.cpp`
- Modify: `tests/AudioCursorTimelineTests.cpp`
- Modify: `tests/MiniaudioMixerTests.cpp`
- Modify: `tests/SecondarySoundBufferTests.cpp`
- Modify: `tests/DirectSoundDeviceTests.cpp`
- Modify: `tests/ExclusiveAudioEngineTests.cpp`
- Modify: `tests/WasapiAudioPatchTests.cpp`

**Interfaces:**
- Produces:

```cpp
enum class AudioCursorResolutionKind : std::uint8_t {
    Resolved,
    PendingGeneration,
    Unmapped,
};

struct AudioCursorResolution {
    AudioCursorResolutionKind kind{};
    std::uint64_t source_frame{};
};

AudioCursorResolution ResolveSourceFrame(
    std::uint64_t output_frame,
    std::uint64_t generation,
    std::uint64_t source_length_frames) const noexcept;
```

Extend `IAudioEngineServices` with:

```cpp
virtual void CountPendingCursorQuery() noexcept = 0;
virtual void CountUnmappedCursorFailure() noexcept = 0;
```

Remove `CountCursorTimelineFailure` after all fakes and production consumers are
updated.

- [x] **Step 1: Write failing timeline status tests**

Assert:

```cpp
expect(timeline.ResolveSourceFrame(100, 7, 1000).kind ==
           AudioCursorResolutionKind::PendingGeneration,
       "generation with no published span is pending");

timeline.Publish({200, 300, 50, 150, 7, false, false});
expect(timeline.ResolveSourceFrame(150, 7, 1000).kind ==
           AudioCursorResolutionKind::PendingGeneration,
       "queued generation before first presentation is pending");
expect(timeline.ResolveSourceFrame(250, 7, 1000).kind ==
           AudioCursorResolutionKind::Resolved,
       "presented generation resolves");
expect(timeline.ResolveSourceFrame(350, 7, 1000).kind ==
           AudioCursorResolutionKind::Unmapped,
       "active generation after its latest segment is unmapped");
```

Retain loop, conversion-ratio, stable-read, and bounded-ring tests with the new
result type.

- [x] **Step 2: Write failing facade generation tests**

In `SecondarySoundBufferTests`, assert that:

- `SetCurrentPosition` immediately returns the requested byte cursor before a
  new span is presented;
- the query increments `pending_cursor_queries`, not unmapped failures;
- first presentation resolves normally;
- a later uncovered active frame increments only unmapped failures;
- every new `Play` uses a generation newer than all prior spans;
- `SetCurrentPosition` followed by `Play` leaves only the latest generation
  authoritative;
- a null `CurrentOutputFrame` preserves the last cursor without calling either
  facade counter, because the engine owns clock-failure accounting.

- [x] **Step 3: Run focused tests and verify red**

```powershell
& $env:ComSpec /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build build-msvc32-latest --target AudioCursorTimelineTests MiniaudioMixerTests SecondarySoundBufferTests DirectSoundDeviceTests ExclusiveAudioEngineTests WasapiAudioPatchTests && ctest --test-dir build-msvc32-latest -R "^(AudioCursorTimelineTests|MiniaudioMixerTests|SecondarySoundBufferTests|DirectSoundDeviceTests|ExclusiveAudioEngineTests|WasapiAudioPatchTests)$" --output-on-failure'
```

Expected: old optional results and counter interface fail the new assertions.

- [x] **Step 4: Implement lookup classification**

While scanning stable slots, track whether the requested generation exists and
its earliest output begin. Return:

```cpp
if (covering_span) return {AudioCursorResolutionKind::Resolved, frame};
if (!generation_seen || output_frame < earliest_begin)
    return {AudioCursorResolutionKind::PendingGeneration, 0};
return {AudioCursorResolutionKind::Unmapped, 0};
```

Invalid zero source length is `Unmapped`.

- [x] **Step 5: Implement unique control generations and counters**

Rename the facade field to `playback_generation_`. Increment it for every
accepted `Play` and `SetCurrentPosition`. Store the requested/fallback source
frame in `last_reported_source_frame_` before publishing the mailbox request.
Keep `SeekMailbox::PublishForPlay` preserving an unapplied seek's frame while
republishing it under the newer play generation; this makes
`SetCurrentPosition` followed by `Play` converge on the play generation without
losing the requested source anchor.

In `ResolveCurrentSourceFrameLocked`, return the stored frame for
`PendingGeneration` and call `CountPendingCursorQuery`. Call
`CountUnmappedCursorFailure` only for `Unmapped`. A resolved result updates the
stored frame. Update `ExclusiveAudioEngine`, its counter snapshot, and every
`IAudioEngineServices` fake in this same task so the commit builds on its own;
Plan 5 only adds the pacing counters.

- [x] **Step 6: Verify green**

Run the six focused targets. Expected: all pass with the expected counter split,
new play generation propagation, and no game-thread timeline writes.

- [x] **Step 7: Commit**

```powershell
git add -- AudioCursorTimeline.h AudioCursorTimeline.cpp DirectSoundFacade.h DirectSoundFacade.cpp MiniaudioMixer.cpp ExclusiveAudioEngine.h ExclusiveAudioEngine.cpp tests/AudioCursorTimelineTests.cpp tests/MiniaudioMixerTests.cpp tests/SecondarySoundBufferTests.cpp tests/DirectSoundDeviceTests.cpp tests/ExclusiveAudioEngineTests.cpp tests/WasapiAudioPatchTests.cpp
git commit -m "fix: distinguish pending audio cursor generations"
```

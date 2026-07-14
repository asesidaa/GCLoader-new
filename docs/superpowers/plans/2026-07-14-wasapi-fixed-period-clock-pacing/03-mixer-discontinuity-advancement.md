# Mixer Discontinuity Advancement Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Advance voices across a confirmed endpoint gap without rendering discarded samples, while preserving looping, source-rate conversion, end/drain, and explicit game-generation precedence.

**Architecture:** The existing mixer render context gains a discontinuity length immediately preceding the output block. Each voice consumes a newly published play/seek generation first; only a continuously playing older generation advances across the gap. Existing cumulative source/output mapping publishes cursor segments for both skipped and rendered intervals.

**Tech Stack:** C++23, miniaudio no-device engine, immutable snapshots, CTest.

## Global Constraints

- Apply every constraint in `README.md`.
- Zero-discontinuity output must remain sample-equivalent to the current path.
- Skipped source frames are never rendered into a discard buffer.
- A new play/seek mailbox value wins over the gap for that voice.

---

### Task 1: Gap-aware mixer render context

**Files:**
- Modify: `MiniaudioMixer.h`
- Modify: `MiniaudioMixer.cpp`
- Modify: `ExclusiveAudioEngine.cpp`
- Modify: `tests/MiniaudioMixerTests.cpp`
- Modify: `tests/SecondarySoundBufferTests.cpp`

**Interfaces:**
- Consumes:

```cpp
struct MixerRenderTimeline {
    std::uint64_t output_frame_begin{};
    std::uint64_t discontinuity_frames{};
};
```

- Produces:

```cpp
MixerRenderResult Render(
    std::span<float> stereo,
    const MixerRenderTimeline&) noexcept;
```

The existing `Render(stereo, output_frame_begin)` call sites are replaced rather
than retained as a second behavior path. Until Plan 5 supplies pacing decisions,
`ExclusiveAudioEngine` and test helpers pass
`MixerRenderTimeline{output_frame_begin, 0}` so this commit remains fully
buildable.

- [ ] **Step 1: Write failing native-rate gap tests**

Create a looping 44.1 kHz source whose frame value identifies its source
position. Render `[441,882)`, then render with:

```cpp
MixerRenderTimeline timeline{
    .output_frame_begin = 1323,
    .discontinuity_frames = 441,
};
```

Assert that the second audible block begins 441 source frames later than a
sequential render, the cursor timeline resolves the skipped interval, and the
voice did not invoke an extra mixer render for the missing packet.

Add non-looping coverage where the source ends inside the gap and publishes the
correct `audible_until_output_frame`.

- [ ] **Step 2: Write failing converted and precedence tests**

For 22.05 kHz and 48 kHz sources, assert cumulative mapped advancement matches
`floor(discontinuity_frames * source_rate / 44100)` without per-gap rounding
drift. Then publish `Seek(frame, new_epoch)` or `Play(loop, new_epoch)` before a
gap render and assert the first sample comes from the explicit requested frame,
not requested frame plus the older gap.

- [ ] **Step 3: Run focused tests and verify red**

```powershell
& $env:ComSpec /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build build-msvc32-latest --target MiniaudioMixerTests && ctest --test-dir build-msvc32-latest -R "^MiniaudioMixerTests$" --output-on-failure'
```

Expected: missing `MixerRenderTimeline` or wrong sequential source samples.

- [ ] **Step 4: Extend render context and apply mailbox first**

Validate `output_frame_begin >= discontinuity_frames`. Store both values in
`MixerRenderContext`. In `VoiceNodeProcess`, preserve the existing stable
mailbox read and set a local `applied_new_generation` when its sequence differs
from `applied_seek_sequence`.

Only call `AdvanceVoiceAcrossDiscontinuity` when:

```cpp
!applied_new_generation &&
render->discontinuity_frames != 0 &&
playback_run != 0
```

- [ ] **Step 5: Implement direct source advancement**

Use the existing cumulative epoch mapping:

```cpp
const auto gap_begin = render->output_frame_begin -
    render->discontinuity_frames;
const auto available_output = OutputFramesUntilSourceEnd(voice);
const auto represented = voice.looping
    ? render->discontinuity_frames
    : std::min(render->discontinuity_frames, available_output);

PublishMappedSpans(
    voice, gap_begin, represented, loop_wrapped, source_ended);
voice.cursor.store(mapped_source_position, std::memory_order_seq_cst);
ma_data_converter_reset(&voice.converter);
```

Load `voice.looping` atomically before choosing the path. Derive
`mapped_source_position` from the updated cumulative epoch mapping, wrapping it
by source length only for a looping source. For a non-looping source that ends,
call `EndPlayback` with
`gap_begin + represented`. For a looping source, wrap the stored cursor. Return
silence for the current voice if it ended in the gap.

- [ ] **Step 6: Verify focused mixer behavior**

Run `MiniaudioMixerTests`. Expected: all native, converted, loop, end, seek,
play, allocation, and prior zero-gap cases pass.

- [ ] **Step 7: Commit**

```powershell
git add -- MiniaudioMixer.h MiniaudioMixer.cpp ExclusiveAudioEngine.cpp tests/MiniaudioMixerTests.cpp tests/SecondarySoundBufferTests.cpp
git commit -m "feat: advance mixer voices across output gaps"
```

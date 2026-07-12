# Endpoint-Clock Cursor Timeline Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Translate the hardware endpoint play clock into each DirectSound buffer's original source-frame and source-byte domain.

**Architecture:** Each voice publishes bounded render spans into a fixed seqlock ring. Readers map `IAudioClock` units onto the engine output-frame timeline, select a span from the current playback/seek epoch, interpolate unwrapped source frames, and wrap only at the final source-domain boundary.

**Tech Stack:** C++23 atomics, fixed arrays, `std::optional`, CTest.

## Global Constraints

- The render writer performs no allocation and takes no mutex.
- Store output begin/end, unwrapped source begin/end, epoch, loop-wrap flag, and ended flag.
- Old spans remain physically present but are ignored after an epoch change.
- The play cursor uses the hardware clock; the write cursor is one actual endpoint buffer ahead in source time.
- Convert to bytes with the original source block alignment.
- Do not use floating point for cursor interpolation.

---

## Prerequisites

- Plan 02 is committed. Plan 03 may execute in parallel only in a separate worktree; normal sequence keeps it committed first.

## File Structure

- Create `AudioCursorTimeline.h` / `AudioCursorTimeline.cpp`.
- Create `tests/AudioCursorTimelineTests.cpp`.
- Modify `CMakeLists.txt`.

### Task 1: Fixed Render Spans and Clock Mapping

**Interfaces:**

```cpp
inline constexpr std::size_t kRenderSpanCapacity = 32;

struct AudioRenderSpan {
    std::uint64_t output_frame_begin{};
    std::uint64_t output_frame_end{};
    std::uint64_t source_frame_begin_unwrapped{};
    std::uint64_t source_frame_end_unwrapped{};
    std::uint64_t epoch{};
    bool loop_wrapped{};
    bool source_ended{};
};

class AudioCursorTimeline {
public:
    void Publish(const AudioRenderSpan&) noexcept;
    std::optional<std::uint64_t> ResolveSourceFrame(
        std::uint64_t output_frame,
        std::uint64_t epoch,
        std::uint64_t source_length_frames) const noexcept;
};

class EndpointClockMapper {
public:
    void Reset(std::uint64_t position, std::uint64_t frequency,
               std::uint64_t output_frame) noexcept;
    std::optional<std::uint64_t> ToOutputFrame(
        std::uint64_t position) const noexcept;
};
```

- [ ] **Step 1: Write the failing span scenarios**

Create `tests/AudioCursorTimelineTests.cpp` and assert:

- empty timeline returns no cursor;
- span output `[100,200)` / source `[0,100)` resolves 100→0, 150→50, 199→99, and rejects 99/200;
- loop span output `[200,300)` / unwrapped source `[90,110)` with length 100 resolves 250→0;
- ended span resolves only through its active output end;
- epoch 2 ignores every epoch-1 span;
- later epoch 3 resynchronization supersedes queued epoch-2 audio;
- source frame 25 at block alignment 4 becomes byte 100;
- 133 output frames project a 44.1 kHz play frame 90 to write frame 23 in length 100;
- the same period projects a 22.05 kHz source to frame 57;
- a device clock with origin `(10000, frequency 10000000, output 500)` maps position `10010000` to output frame `44600` and rejects a regressing position.

- [ ] **Step 2: Register the test and verify red**

```cmake
add_executable(AudioCursorTimelineTests
        AudioCursorTimeline.cpp
        tests/AudioCursorTimelineTests.cpp
)
target_include_directories(AudioCursorTimelineTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME AudioCursorTimelineTests COMMAND AudioCursorTimelineTests)
```

Append `AudioCursorTimeline.cpp` to `SOURCES`, reconfigure, and build. Expected: missing header/compiler symbols.

- [ ] **Step 3: Implement the fixed seqlock ring**

Each slot contains `std::atomic<std::uint64_t> sequence` plus one `AudioRenderSpan`. The single writer uses an odd sequence while writing and the matching even sequence after:

```cpp
const auto generation = writer_generation_++;
auto& slot = slots_[generation % kRenderSpanCapacity];
const auto writing = generation * 2 + 1;
slot.sequence.store(writing, std::memory_order_release);
slot.span = span;
slot.sequence.store(writing + 1, std::memory_order_release);
published_generation_.store(generation + 1, std::memory_order_release);
```

Readers scan newest to oldest, retry a slot at most three times, and accept only the expected stable even sequence.

- [ ] **Step 4: Implement integer interpolation and wrapping**

Avoid multiplication overflow by quotient/remainder decomposition:

```cpp
std::uint64_t scale_floor(
    std::uint64_t value,
    std::uint64_t numerator,
    std::uint64_t denominator) noexcept {
    const auto quotient = numerator / denominator;
    const auto remainder = numerator % denominator;
    return value * quotient + (value * remainder) / denominator;
}
```

For a matching span:

```cpp
const auto unwrapped = span.source_frame_begin_unwrapped + scale_floor(
    output_frame - span.output_frame_begin,
    span.source_frame_end_unwrapped - span.source_frame_begin_unwrapped,
    span.output_frame_end - span.output_frame_begin);
return unwrapped % source_length_frames;
```

- [ ] **Step 5: Implement clock and write-cursor helpers**

Map device units using the frequency returned by `IAudioClock::GetFrequency`. Reject zero frequency and positions before the origin.

Project one endpoint period with ceiling division:

```cpp
const auto source_frames_ahead =
    (static_cast<std::uint64_t>(endpoint_buffer_frames) * source_rate +
     kOutputSampleRate - 1) /
    kOutputSampleRate;
return (play_frame + source_frames_ahead) % source_length_frames;
```

- [ ] **Step 6: Verify focused behavior**

```powershell
& $env:ComSpec /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build build-msvc32-latest --target AudioCursorTimelineTests iDmacDrv32 && ctest --test-dir build-msvc32-latest -R "^AudioCursorTimelineTests$" --output-on-failure'
```

Expected: all queued-start, interpolation, wrap, end, epoch, resync, clock, and write-cursor cases pass.

- [ ] **Step 7: Commit**

```powershell
git add -- CMakeLists.txt AudioCursorTimeline.h AudioCursorTimeline.cpp tests/AudioCursorTimelineTests.cpp
git commit -m "feat: map endpoint clock to source cursors"
```

## Completion Gate

The raw miniaudio data-source cursor is not a valid substitute. The focused test must demonstrate that queued output and seek epochs change what the game sees.

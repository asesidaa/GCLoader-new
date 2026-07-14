# Output Pacing Tracker Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Convert mapped hardware presentation frames and the committed submitted tail into deterministic sequential, recoverable-gap, chronic-gap, or invalid-clock decisions.

**Architecture:** A standalone allocation-free integer state machine owns the packet grid, committed tail, last presentation position, and the last three confirmed gap positions. Planning and submission commit remain separate so a failed endpoint release cannot advance queued state.

**Tech Stack:** C++23 fixed-width integers/arrays, CTest.

## Global Constraints

- Apply every constraint in `README.md`.
- Packet boundaries are multiples of the actual endpoint frame count.
- QPC values are not inputs to this component.
- A third confirmed gap in a rolling 44,100-frame window is fatal.

---

### Task 1: Pure fixed-slot pacing state machine

**Files:**
- Create: `OutputPacingTracker.h`
- Create: `OutputPacingTracker.cpp`
- Create: `tests/OutputPacingTrackerTests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: mapped `presented_frame` values and successful endpoint submission
  commits.
- Produces:

```cpp
enum class OutputPacingDecisionKind : std::uint8_t {
    Sequential,
    RecoverableGap,
    ChronicGap,
    InvalidClock,
};

struct OutputPacingDecision {
    OutputPacingDecisionKind kind{};
    std::uint64_t block_begin{};
    std::uint64_t block_end{};
    std::uint64_t discontinuity_begin{};
    std::uint64_t discontinuity_frames{};
    std::int64_t submitted_lead_frames{};
};

class OutputPacingTracker {
public:
    explicit OutputPacingTracker(std::uint32_t packet_frames) noexcept;
    OutputPacingDecision Plan(std::uint64_t presented_frame) noexcept;
    bool Commit(const OutputPacingDecision&) noexcept;
    std::uint64_t submitted_tail() const noexcept;
};
```

- [ ] **Step 1: Register a failing focused target**

Add:

```cmake
add_executable(OutputPacingTrackerTests
        OutputPacingTracker.cpp
        tests/OutputPacingTrackerTests.cpp)
target_include_directories(OutputPacingTrackerTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME OutputPacingTrackerTests COMMAND OutputPacingTrackerTests)
```

Append `OutputPacingTracker.cpp` to `SOURCES` and to
`ExclusiveAudioEngineTests` after Plan 5 integrates it.

- [ ] **Step 2: Write the failing state-machine cases**

Use a 441-frame tracker and assert:

```cpp
OutputPacingTracker tracker(441);
auto first = tracker.Plan(0);
expect(first.kind == OutputPacingDecisionKind::Sequential &&
       first.block_begin == 441 && first.block_end == 882 &&
       first.discontinuity_frames == 0,
       "prefill leaves one sequential packet queued");
expect(tracker.Commit(first), "successful release commits first packet");

auto gap = tracker.Plan(900);
expect(gap.kind == OutputPacingDecisionKind::RecoverableGap &&
       gap.discontinuity_begin == 882 &&
       gap.block_begin == 1323 &&
       gap.discontinuity_frames == 441,
       "presentation beyond submitted tail skips to next packet boundary");
```

Also cover presentation equal to the tail, repeated equal clock samples,
regression, zero packet frames, align-up overflow, block-end overflow, commit of
a stale/mismatched decision, two recoverable gap events, a third gap within
44,100 frames producing `ChronicGap`, and an old gap expiring before the third.

- [ ] **Step 3: Run and verify red**

Reconfigure inside `vcvars32.bat`, then run:

```powershell
& $env:ComSpec /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build build-msvc32-latest --target OutputPacingTrackerTests && ctest --test-dir build-msvc32-latest -R "^OutputPacingTrackerTests$" --output-on-failure'
```

Expected: compilation fails because the tracker files or symbols are absent.

- [ ] **Step 4: Implement minimal deterministic planning**

Initialize `submitted_tail_` to `packet_frames`. `Plan` must:

```cpp
if (packet_frames_ == 0 ||
    (has_last_presentation_ && presented_frame < last_presented_frame_)) {
    return {.kind = OutputPacingDecisionKind::InvalidClock};
}

const auto lead = SaturatingSignedDifference(
    submitted_tail_, presented_frame);
auto begin = submitted_tail_;
if (presented_frame > submitted_tail_) {
    begin = AlignUp(presented_frame, packet_frames_);
}
```

Reject overflow before producing `block_end`. For a gap, expire stored gap
positions whose distance from the current presentation is at least 44,100
output frames, insert the current presentation position, and return
`ChronicGap` on the third live entry. `Commit` accepts only
a renderable decision whose `block_begin` still matches the current tail or its
declared discontinuity, then stores `block_end`.

- [ ] **Step 5: Verify green and allocation independence**

Run the focused target twice. Expected: PASS both times and no test uses heap
allocation to drive `Plan` or `Commit`.

- [ ] **Step 6: Commit**

```powershell
git add -- CMakeLists.txt OutputPacingTracker.h OutputPacingTracker.cpp tests/OutputPacingTrackerTests.cpp
git commit -m "feat: track WASAPI output pacing"
```

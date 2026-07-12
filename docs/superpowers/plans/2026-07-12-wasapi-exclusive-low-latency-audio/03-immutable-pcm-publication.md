# Immutable PCM Publication Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give every future secondary buffer one copy-on-write PCM store whose `Lock`/`Unlock` behavior is game-compatible and whose render reads never block, allocate, or free.

**Architecture:** Game/streaming threads clone the latest snapshot and publish only after a matching `Unlock`. The single render thread protects one raw snapshot pointer with a hazard slot; replaced snapshots retire and are reclaimed only by non-real-time callers.

**Tech Stack:** C++23 atomics, `std::unique_ptr`, `std::vector<std::byte>`, DirectSound lock flags/HRESULTs, CTest.

## Global Constraints

- One outstanding writable lock per buffer.
- Support flags `0` and `DSBLOCK_ENTIREBUFFER`; reject `DSBLOCK_FROMWRITECURSOR` and every unknown bit.
- Offsets and lengths must be nonzero/in-range/block-aligned; wrapped locks return exactly two regions.
- A mismatched `Unlock` returns `DSERR_INVALIDPARAM`, publishes nothing, and leaves the correct unlock retry possible.
- Render acquire/release performs no allocation, deletion, copy, mutex wait, or logging.
- Render never observes bytes from an incomplete write.
- Snapshot destruction/reclamation occurs on a non-real-time thread.

---

## Prerequisites

- Plans 01-02 are committed.

## File Structure

- Create `AudioSnapshot.h` / `AudioSnapshot.cpp`.
- Create `tests/AudioSnapshotTests.cpp`.
- Modify `CMakeLists.txt` to compile the source and register the focused test.

### Task 1: Copy-on-Write Lock Storage

**Interfaces:**

```cpp
struct AudioLockRegions {
    void* first{};
    DWORD first_bytes{};
    void* second{};
    DWORD second_bytes{};
};

class AudioSnapshot final {
public:
    class RenderView;
    AudioSnapshot(std::uint32_t byte_length, std::uint16_t block_align);
    HRESULT Lock(DWORD, DWORD, DWORD, AudioLockRegions*) noexcept;
    HRESULT Unlock(void*, DWORD, void*, DWORD) noexcept;
    RenderView AcquireForRender() const noexcept;
    void ReclaimRetired() noexcept;
    std::uint32_t byte_length() const noexcept;
    std::uint64_t generation() const noexcept;
};
```

- [ ] **Step 1: Write the failing deterministic tests**

Create `tests/AudioSnapshotTests.cpp` and cover this exact matrix:

```cpp
AudioSnapshot snapshot(16, 4);

// Initial snapshot is 16 zero bytes at generation 0.
// Lock(4, 8, 0) returns first=8 bytes, second=0.
// Lock(12, 8, 0) returns first=4 bytes, second=4 bytes.
// Lock(2, 4), Lock(0, 6), Lock(16, 4), and Lock(0, 20) fail.
// DSBLOCK_FROMWRITECURSOR fails.
// A second Lock while one is outstanding returns DSERR_ALLOCATED.
// A wrong pointer/length Unlock fails without changing generation.
// Retrying that Unlock with exact regions succeeds.
```

Hold a `RenderView`, publish different bytes, and assert the held view remains unchanged. Assert one retired object remains while the hazard is set; move-assign `{}` into the view, call `ReclaimRetired`, and assert the retired count becomes zero.

Override global test `operator new/delete` or use an equivalent allocation probe. Enable it only around `AcquireForRender`, `bytes()[0]`, and view destruction; assert zero calls.

- [ ] **Step 2: Register the target and verify red**

```cmake
add_executable(AudioSnapshotTests
        AudioSnapshot.cpp
        tests/AudioSnapshotTests.cpp
)
target_include_directories(AudioSnapshotTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME AudioSnapshotTests COMMAND AudioSnapshotTests)
```

Append `AudioSnapshot.cpp` to `SOURCES`, reconfigure, and build `AudioSnapshotTests`.

Expected: compilation fails because `AudioSnapshot.h` is absent.

- [ ] **Step 3: Define the movable render view and owner state**

The header must use this ownership shape:

```cpp
struct Snapshot {
    std::vector<std::byte> bytes;
    std::uint64_t generation{};
};

struct WritableLock {
    std::unique_ptr<Snapshot> snapshot;
    AudioLockRegions regions;
};

mutable std::atomic<const Snapshot*> render_hazard_{};
std::atomic<const Snapshot*> published_{};
mutable std::mutex writer_mutex_;
std::unique_ptr<Snapshot> published_owner_;
std::unique_ptr<WritableLock> outstanding_;
std::vector<std::unique_ptr<Snapshot>> retired_;
```

`RenderView` is noncopyable, movable, and exposes only `std::span<const std::byte> bytes()`, `size()`, and `generation()`. Its destructor clears `render_hazard_` with sequential consistency and does not reclaim.

- [ ] **Step 4: Implement lock validation and publication**

Use this exact publication sequence inside the game-thread mutex:

```cpp
ReclaimRetiredLocked();
auto old = std::move(published_owner_);
published_owner_ = std::move(outstanding_->snapshot);
outstanding_.reset();
published_.store(published_owner_.get(), std::memory_order_seq_cst);
retired_.push_back(std::move(old));
ReclaimRetiredLocked();
```

`Lock` clones `published_owner_->bytes` before returning pointers. For wraparound:

```cpp
const DWORD first_bytes = std::min<DWORD>(
    byte_count, byte_length_ - offset);
const DWORD second_bytes = byte_count - first_bytes;
```

Do not mutate `published_owner_` until `Unlock` arguments exactly equal the recorded regions.

- [ ] **Step 5: Implement hazard acquisition and non-RT reclaim**

Use the standard single-reader loop:

```cpp
const Snapshot* candidate = nullptr;
do {
    candidate = published_.load(std::memory_order_seq_cst);
    render_hazard_.store(candidate, std::memory_order_seq_cst);
} while (candidate != published_.load(std::memory_order_seq_cst));
return RenderView(this, candidate);
```

Reclamation erases every retired owner except the pointer currently in `render_hazard_`. It is called only by `Lock`, successful `Unlock`, `Restore`, or other non-render control paths.

- [ ] **Step 6: Verify focused behavior**

```powershell
& $env:ComSpec /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build build-msvc32-latest --target AudioSnapshotTests iDmacDrv32 && ctest --test-dir build-msvc32-latest -R "^AudioSnapshotTests$" --output-on-failure'
```

Expected: all lock, immutable-view, reclamation, and zero-render-allocation cases pass.

- [ ] **Step 7: Commit**

```powershell
git add -- CMakeLists.txt AudioSnapshot.h AudioSnapshot.cpp tests/AudioSnapshotTests.cpp
git commit -m "feat: publish immutable DirectSound PCM snapshots"
```

## Completion Gate

No later plan may replace the hazard publisher with `std::shared_ptr` on the render path: releasing the last shared owner could free memory on that thread.

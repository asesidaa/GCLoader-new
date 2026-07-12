#include "AudioSnapshot.h"

#include <Windows.h>
#include <dsound.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <malloc.h>
#include <new>
#include <string_view>
#include <thread>
#include <type_traits>

namespace allocation_probe {

std::atomic<bool> enabled{};
std::atomic<std::size_t> allocations{};
std::atomic<std::size_t> deallocations{};

void CountAllocation() noexcept {
    if (enabled.load(std::memory_order_relaxed)) {
        allocations.fetch_add(1, std::memory_order_relaxed);
    }
}

void CountDeallocation(void* pointer) noexcept {
    if (pointer != nullptr && enabled.load(std::memory_order_relaxed)) {
        deallocations.fetch_add(1, std::memory_order_relaxed);
    }
}

void Begin() noexcept {
    allocations.store(0, std::memory_order_relaxed);
    deallocations.store(0, std::memory_order_relaxed);
    enabled.store(true, std::memory_order_seq_cst);
}

struct Counts {
    std::size_t allocations;
    std::size_t deallocations;
};

Counts End() noexcept {
    enabled.store(false, std::memory_order_seq_cst);
    return {
        allocations.load(std::memory_order_relaxed),
        deallocations.load(std::memory_order_relaxed),
    };
}

} // namespace allocation_probe

void* operator new(std::size_t size) {
    allocation_probe::CountAllocation();
    if (void* pointer = std::malloc(size == 0 ? 1 : size)) {
        return pointer;
    }
    throw std::bad_alloc{};
}

void* operator new[](std::size_t size) {
    return ::operator new(size);
}

void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    try {
        return ::operator new(size);
    } catch (...) {
        return nullptr;
    }
}

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
    return ::operator new(size, std::nothrow);
}

void operator delete(void* pointer) noexcept {
    allocation_probe::CountDeallocation(pointer);
    std::free(pointer);
}

void operator delete[](void* pointer) noexcept {
    ::operator delete(pointer);
}

void operator delete(void* pointer, std::size_t) noexcept {
    ::operator delete(pointer);
}

void operator delete[](void* pointer, std::size_t) noexcept {
    ::operator delete(pointer);
}

void* operator new(std::size_t size, std::align_val_t alignment) {
    allocation_probe::CountAllocation();
    if (void* pointer = _aligned_malloc(
            size == 0 ? 1 : size,
            static_cast<std::size_t>(alignment))) {
        return pointer;
    }
    throw std::bad_alloc{};
}

void* operator new[](
    std::size_t size,
    std::align_val_t alignment) {
    return ::operator new(size, alignment);
}

void* operator new(
    std::size_t size,
    std::align_val_t alignment,
    const std::nothrow_t&) noexcept {
    try {
        return ::operator new(size, alignment);
    } catch (...) {
        return nullptr;
    }
}

void* operator new[](
    std::size_t size,
    std::align_val_t alignment,
    const std::nothrow_t&) noexcept {
    return ::operator new(size, alignment, std::nothrow);
}

void operator delete(
    void* pointer,
    std::align_val_t) noexcept {
    allocation_probe::CountDeallocation(pointer);
    _aligned_free(pointer);
}

void operator delete[](
    void* pointer,
    std::align_val_t alignment) noexcept {
    ::operator delete(pointer, alignment);
}

void operator delete(
    void* pointer,
    std::size_t,
    std::align_val_t alignment) noexcept {
    ::operator delete(pointer, alignment);
}

void operator delete[](
    void* pointer,
    std::size_t,
    std::align_val_t alignment) noexcept {
    ::operator delete(pointer, alignment);
}

namespace {

using gc::audio::AudioLockRegions;
using gc::audio::AudioSnapshot;

static_assert(
    !std::is_move_constructible_v<AudioSnapshot::RenderView>,
    "RenderView must remain scoped and non-move-constructible");
static_assert(
    !std::is_move_assignable_v<AudioSnapshot::RenderView>,
    "RenderView must not expose the unsafe active-view move assignment");

int Expect(bool condition, std::string_view name) {
    if (condition) {
        return 0;
    }

    std::cerr << "Expected " << name << '\n';
    return 1;
}

bool EveryByte(
    const AudioSnapshot::RenderView& view,
    std::byte expected) noexcept {
    for (const auto value : view.bytes()) {
        if (value != expected) {
            return false;
        }
    }
    return true;
}

void FillRegions(
    const AudioLockRegions& regions,
    unsigned char first,
    unsigned char second) noexcept {
    std::memset(regions.first, first, regions.first_bytes);
    if (regions.second_bytes != 0) {
        std::memset(regions.second, second, regions.second_bytes);
    }
}

int TestLockContract() {
    int failures = 0;
    AudioSnapshot snapshot(16, 4);

    failures += Expect(snapshot.byte_length() == 16, "16-byte length");
    failures += Expect(snapshot.generation() == 0, "initial generation zero");
    {
        auto view = snapshot.AcquireForRender();
        failures += Expect(view.size() == 16, "initial render size");
        failures += Expect(view.generation() == 0, "initial render generation");
        failures += Expect(
            EveryByte(view, std::byte{0}),
            "initial render bytes are zero");
    }

    AudioLockRegions contiguous{};
    failures += Expect(
        snapshot.Lock(4, 8, 0, &contiguous) == DS_OK,
        "contiguous lock succeeds");
    failures += Expect(
        contiguous.first != nullptr && contiguous.first_bytes == 8 &&
            contiguous.second == nullptr && contiguous.second_bytes == 0,
        "contiguous lock returns one exact region");
    FillRegions(contiguous, 0x11, 0);
    failures += Expect(
        snapshot.Unlock(
            contiguous.first,
            contiguous.first_bytes,
            contiguous.second,
            contiguous.second_bytes) == DS_OK,
        "contiguous unlock publishes");
    failures += Expect(snapshot.generation() == 1, "first publication generation");

    AudioLockRegions wrapped{};
    failures += Expect(
        snapshot.Lock(12, 8, 0, &wrapped) == DS_OK,
        "wraparound lock succeeds");
    failures += Expect(
        wrapped.first != nullptr && wrapped.first_bytes == 4 &&
            wrapped.second != nullptr && wrapped.second_bytes == 4,
        "wraparound lock returns two exact regions");
    FillRegions(wrapped, 0x22, 0x33);
    failures += Expect(
        snapshot.Unlock(
            wrapped.first,
            wrapped.first_bytes,
            wrapped.second,
            wrapped.second_bytes) == DS_OK,
        "wraparound unlock publishes");
    {
        auto view = snapshot.AcquireForRender();
        const auto bytes = view.bytes();
        failures += Expect(
            view.generation() == 2,
            "wraparound publication generation");
        failures += Expect(
            bytes[0] == std::byte{0x33} &&
                bytes[3] == std::byte{0x33} &&
                bytes[4] == std::byte{0x11} &&
                bytes[11] == std::byte{0x11} &&
                bytes[12] == std::byte{0x22} &&
                bytes[15] == std::byte{0x22},
            "wraparound publication preserves unwritten bytes");
    }

    AudioLockRegions invalid{};
    failures += Expect(
        snapshot.Lock(2, 4, 0, &invalid) == DSERR_INVALIDPARAM,
        "unaligned offset rejected");
    failures += Expect(
        snapshot.Lock(0, 6, 0, &invalid) == DSERR_INVALIDPARAM,
        "unaligned length rejected");
    failures += Expect(
        snapshot.Lock(16, 4, 0, &invalid) == DSERR_INVALIDPARAM,
        "end offset rejected");
    failures += Expect(
        snapshot.Lock(0, 20, 0, &invalid) == DSERR_INVALIDPARAM,
        "oversized length rejected");
    failures += Expect(
        snapshot.Lock(0, 0, 0, &invalid) == DSERR_INVALIDPARAM,
        "zero length rejected");
    failures += Expect(
        snapshot.Lock(0, 4, DSBLOCK_FROMWRITECURSOR, &invalid) ==
            DSERR_INVALIDPARAM,
        "write-cursor lock rejected");
    failures += Expect(
        snapshot.Lock(0, 4, 0x80000000UL, &invalid) ==
            DSERR_INVALIDPARAM,
        "unknown lock flag rejected");
    failures += Expect(
        snapshot.Lock(0, 4, 0, nullptr) == DSERR_INVALIDPARAM,
        "null lock output rejected");

    AudioLockRegions entire{};
    failures += Expect(
        snapshot.Lock(12, 4, DSBLOCK_ENTIREBUFFER, &entire) == DS_OK,
        "entire-buffer flag succeeds");
    failures += Expect(
        entire.first_bytes == 16 && entire.second == nullptr &&
            entire.second_bytes == 0,
        "entire-buffer flag returns full contiguous buffer");
    FillRegions(entire, 0x44, 0);
    failures += Expect(
        snapshot.Unlock(
            entire.first,
            entire.first_bytes,
            entire.second,
            entire.second_bytes) == DS_OK,
        "entire-buffer unlock publishes");

    AudioLockRegions outstanding{};
    AudioLockRegions second{};
    failures += Expect(
        snapshot.Lock(4, 8, 0, &outstanding) == DS_OK,
        "outstanding lock setup succeeds");
    failures += Expect(
        snapshot.Lock(0, 4, 0, &second) == DSERR_ALLOCATED,
        "second outstanding lock rejected");
    FillRegions(outstanding, 0x55, 0);
    const auto generation_before_bad_unlock = snapshot.generation();
    failures += Expect(
        snapshot.Unlock(
            outstanding.first,
            outstanding.first_bytes - 4,
            outstanding.second,
            outstanding.second_bytes) == DSERR_INVALIDPARAM,
        "mismatched unlock rejected");
    failures += Expect(
        snapshot.generation() == generation_before_bad_unlock,
        "mismatched unlock does not publish");
    failures += Expect(
        snapshot.Unlock(
            outstanding.first,
            outstanding.first_bytes,
            outstanding.second,
            outstanding.second_bytes) == DS_OK,
        "exact unlock retry succeeds");
    failures += Expect(
        snapshot.generation() == generation_before_bad_unlock + 1,
        "exact unlock retry publishes once");
    failures += Expect(
        snapshot.Unlock(
            outstanding.first,
            outstanding.first_bytes,
            outstanding.second,
            outstanding.second_bytes) == DSERR_INVALIDPARAM,
        "unlock without outstanding lock rejected");

    return failures;
}

int TestImmutablePublicationAndReclamation() {
    int failures = 0;
    AudioSnapshot snapshot(16, 4);

    {
        auto held = snapshot.AcquireForRender();

        AudioLockRegions regions{};
        failures += Expect(
            snapshot.Lock(0, 16, 0, &regions) == DS_OK,
            "immutable publication lock succeeds");
        FillRegions(regions, 0x7f, 0);
        failures += Expect(
            snapshot.Unlock(
                regions.first,
                regions.first_bytes,
                regions.second,
                regions.second_bytes) == DS_OK,
            "immutable publication unlock succeeds");
        failures += Expect(
            held.generation() == 0 && EveryByte(held, std::byte{0}),
            "held render view remains immutable after publication");

        allocation_probe::Begin();
        snapshot.ReclaimRetired();
        const auto while_held = allocation_probe::End();
        failures += Expect(
            while_held.deallocations == 0,
            "hazard retains the one retired snapshot");
    }

    allocation_probe::Begin();
    snapshot.ReclaimRetired();
    const auto after_release = allocation_probe::End();
    failures += Expect(
        after_release.deallocations == 2,
        "released retired snapshot and its byte storage are reclaimed");

    {
        auto current = snapshot.AcquireForRender();
        failures += Expect(
            current.generation() == 1 &&
                EveryByte(current, std::byte{0x7f}),
            "current render view observes published bytes");
    }

    return failures;
}

int TestRenderViewDoesNotAllocateOrFree() {
    int failures = 0;
    AudioSnapshot snapshot(16, 4);
    std::byte observed{};
    std::size_t observed_size{};
    std::uint64_t observed_generation{};

    allocation_probe::Begin();
    {
        auto view = snapshot.AcquireForRender();
        observed = view.bytes()[0];
        observed_size = view.size();
        observed_generation = view.generation();
    }
    const auto counts = allocation_probe::End();

    failures += Expect(
        observed == std::byte{0} && observed_size == 16 &&
            observed_generation == 0,
        "probed render view is usable");
    failures += Expect(
        counts.allocations == 0,
        "render acquire, access, and destruction allocate zero times");
    failures += Expect(
        counts.deallocations == 0,
        "render acquire, access, and destruction free zero times");
    return failures;
}

int TestSecondAcquireFailsWithoutDisturbingActiveView() {
    int failures = 0;
    AudioSnapshot snapshot(16, 4);

    {
        auto active = snapshot.AcquireForRender();
        auto rejected = snapshot.AcquireForRender();
        failures += Expect(
            active.size() == 16 && active.generation() == 0,
            "first render acquire remains active");
        failures += Expect(
            rejected.bytes().empty() && rejected.size() == 0 &&
                rejected.generation() == 0,
            "second render acquire returns an empty view");

        AudioLockRegions regions{};
        failures += Expect(
            snapshot.Lock(0, 16, 0, &regions) == DS_OK,
            "publication with one active view locks");
        FillRegions(regions, 0x6a, 0);
        failures += Expect(
            snapshot.Unlock(
                regions.first,
                regions.first_bytes,
                regions.second,
                regions.second_bytes) == DS_OK,
            "publication with one active view unlocks");
        failures += Expect(
            active.generation() == 0 &&
                EveryByte(active, std::byte{0}),
            "rejected acquire does not disturb active view protection");

        allocation_probe::Begin();
        snapshot.ReclaimRetired();
        const auto while_active = allocation_probe::End();
        failures += Expect(
            while_active.deallocations == 0,
            "active view still retains the retired snapshot");
    }

    allocation_probe::Begin();
    snapshot.ReclaimRetired();
    const auto after_scope = allocation_probe::End();
    failures += Expect(
        after_scope.deallocations == 2,
        "scoped view destruction permits retired snapshot reclamation");
    return failures;
}

class SequencedPublishedPointer {
public:
    using value_type = const int*;

    SequencedPublishedPointer(
        value_type first,
        value_type replacement) noexcept
        : first_(first), replacement_(replacement) {}

    value_type load(std::memory_order) noexcept {
        const auto load = load_count_++;
        return load == 0 ? first_ : replacement_;
    }

    std::size_t load_count() const noexcept {
        return load_count_;
    }

private:
    value_type first_;
    value_type replacement_;
    std::size_t load_count_{};
};

class RecordingHazardPointer {
public:
    using value_type = const int*;

    bool compare_exchange_strong(
        value_type& expected,
        value_type desired,
        std::memory_order,
        std::memory_order) noexcept {
        ++protect_count_;
        if (current_ != expected) {
            expected = current_;
            return false;
        }
        current_ = desired;
        return true;
    }

    void store(value_type pointer, std::memory_order) noexcept {
        current_ = pointer;
        if (pointer == nullptr) {
            ++clear_count_;
        }
    }

    value_type current() const noexcept {
        return current_;
    }

    std::size_t protect_count() const noexcept {
        return protect_count_;
    }

    std::size_t clear_count() const noexcept {
        return clear_count_;
    }

private:
    value_type current_{};
    std::size_t protect_count_{};
    std::size_t clear_count_{};
};

int TestCaptureRetriesWhenPublicationChangesInProtectionWindow() {
    // Drive the exact helper used by AcquireForRender through the race order.
    const int old_snapshot = 1;
    const int new_snapshot = 2;
    SequencedPublishedPointer published(&old_snapshot, &new_snapshot);
    RecordingHazardPointer hazard;

    const auto* captured =
        gc::audio::detail::CaptureSingleReaderSnapshot(published, hazard);

    int failures = 0;
    failures += Expect(
        captured == &new_snapshot,
        "capture retries to the replacement publication");
    failures += Expect(
        hazard.current() == &new_snapshot,
        "replacement publication remains hazard-protected");
    failures += Expect(
        published.load_count() == 4 && hazard.protect_count() == 2 &&
            hazard.clear_count() == 1,
        "capture performs load, protect, recheck, clear, and retry");
    return failures;
}

int TestRepeatedAcquireDuringPublication() {
    constexpr std::uint32_t publication_count = 4096;
    constexpr std::uint32_t minimum_reads = publication_count * 2;
    AudioSnapshot snapshot(64, 4);
    std::atomic<bool> reader_ready{};
    std::atomic<bool> writer_ready{};
    std::atomic<bool> start{};
    std::atomic<bool> writer_done{};
    std::atomic<bool> failed{};
    std::atomic<bool> reader_observed_publication{};
    std::atomic<std::uint32_t> read_count{};

    std::thread reader([&] {
        reader_ready.store(true, std::memory_order_release);
        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }

        while (!writer_done.load(std::memory_order_acquire) ||
               read_count.load(std::memory_order_relaxed) < minimum_reads) {
            {
                auto view = snapshot.AcquireForRender();
                const auto generation = view.generation();
                const auto expected = static_cast<std::byte>(
                    static_cast<unsigned char>(generation));
                if (view.size() != 64 || !EveryByte(view, expected)) {
                    failed.store(true, std::memory_order_relaxed);
                }
                if (generation != 0) {
                    reader_observed_publication.store(
                        true, std::memory_order_release);
                }
            }
            read_count.fetch_add(1, std::memory_order_relaxed);
        }
    });

    std::thread writer([&] {
        writer_ready.store(true, std::memory_order_release);
        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }

        for (std::uint32_t iteration = 1;
             iteration <= publication_count;
             ++iteration) {
            AudioLockRegions regions{};
            if (snapshot.Lock(0, 64, 0, &regions) != DS_OK) {
                failed.store(true, std::memory_order_relaxed);
                break;
            }
            FillRegions(
                regions,
                static_cast<unsigned char>(iteration),
                0);
            if (snapshot.Unlock(
                    regions.first,
                    regions.first_bytes,
                    regions.second,
                    regions.second_bytes) != DS_OK) {
                failed.store(true, std::memory_order_relaxed);
                break;
            }
            if (iteration == 1) {
                while (!reader_observed_publication.load(
                    std::memory_order_acquire)) {
                    std::this_thread::yield();
                }
            }
        }
        writer_done.store(true, std::memory_order_release);
    });

    while (!reader_ready.load(std::memory_order_acquire) ||
           !writer_ready.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
    start.store(true, std::memory_order_release);
    reader.join();
    writer.join();

    int failures = 0;
    failures += Expect(
        !failed.load(std::memory_order_relaxed),
        "repeated acquisition observes only coherent publications");
    failures += Expect(
        snapshot.generation() == publication_count &&
            read_count.load(std::memory_order_relaxed) >= minimum_reads &&
            reader_observed_publication.load(std::memory_order_relaxed),
        "repeated acquisition overlaps every publication run");
    return failures;
}

int TestConcurrentPublicationKeepsHeldViewStable() {
    constexpr std::uint32_t publication_count = 128;
    AudioSnapshot snapshot(64, 4);
    std::atomic<std::uint32_t> view_held{};
    std::atomic<std::uint32_t> published{};
    std::atomic<std::uint32_t> view_released{};
    std::atomic<bool> failed{};

    std::thread reader([&] {
        for (std::uint32_t iteration = 1;
             iteration <= publication_count;
             ++iteration) {
            {
                auto view = snapshot.AcquireForRender();
                const auto expected_generation = iteration - 1;
                const auto expected_byte = static_cast<std::byte>(
                    static_cast<unsigned char>(expected_generation));
                if (view.generation() != expected_generation ||
                    !EveryByte(view, expected_byte)) {
                    failed.store(true, std::memory_order_relaxed);
                }

                view_held.store(iteration, std::memory_order_release);
                while (published.load(std::memory_order_acquire) < iteration) {
                    std::this_thread::yield();
                }

                if (view.generation() != expected_generation ||
                    !EveryByte(view, expected_byte)) {
                    failed.store(true, std::memory_order_relaxed);
                }
            }
            view_released.store(iteration, std::memory_order_release);
        }
    });

    std::thread writer([&] {
        for (std::uint32_t iteration = 1;
             iteration <= publication_count;
             ++iteration) {
            while (view_held.load(std::memory_order_acquire) < iteration) {
                std::this_thread::yield();
            }

            AudioLockRegions regions{};
            if (snapshot.Lock(0, 64, 0, &regions) != DS_OK) {
                failed.store(true, std::memory_order_relaxed);
            } else {
                FillRegions(
                    regions,
                    static_cast<unsigned char>(iteration),
                    0);
                if (snapshot.Unlock(
                        regions.first,
                        regions.first_bytes,
                        regions.second,
                        regions.second_bytes) != DS_OK) {
                    failed.store(true, std::memory_order_relaxed);
                }
            }

            published.store(iteration, std::memory_order_release);
            while (view_released.load(std::memory_order_acquire) < iteration) {
                std::this_thread::yield();
            }
            snapshot.ReclaimRetired();
        }
    });

    reader.join();
    writer.join();

    int failures = 0;
    failures += Expect(
        !failed.load(std::memory_order_relaxed),
        "concurrent publication never changes a held render view");
    failures += Expect(
        snapshot.generation() == publication_count,
        "every concurrent publication advances generation once");
    return failures;
}

} // namespace

int main() {
    int failures = 0;
    failures += TestLockContract();
    failures += TestImmutablePublicationAndReclamation();
    failures += TestRenderViewDoesNotAllocateOrFree();
    failures += TestSecondAcquireFailsWithoutDisturbingActiveView();
    failures += TestCaptureRetriesWhenPublicationChangesInProtectionWindow();
    failures += TestRepeatedAcquireDuringPublication();
    failures += TestConcurrentPublicationKeepsHeldViewStable();
    return failures == 0 ? 0 : 1;
}

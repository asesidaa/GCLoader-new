#include "Audio/Mixer/AudioCursorTimeline.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <string_view>
#include <thread>

static_assert(
    gc::audio::detail::kRenderSpanAtomicOrder ==
        std::memory_order_seq_cst,
    "render-span sequence and payload operations require one seq_cst order");

namespace allocation_probe {

std::atomic<bool> enabled{};
std::atomic<std::size_t> allocations{};
std::atomic<std::size_t> deallocations{};

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
    if (allocation_probe::enabled.load(std::memory_order_relaxed)) {
        allocation_probe::allocations.fetch_add(1, std::memory_order_relaxed);
    }
    if (void* pointer = std::malloc(size == 0 ? 1 : size)) {
        return pointer;
    }
    throw std::bad_alloc{};
}

void* operator new[](std::size_t size) {
    return ::operator new(size);
}

void operator delete(void* pointer) noexcept {
    if (pointer != nullptr &&
        allocation_probe::enabled.load(std::memory_order_relaxed)) {
        allocation_probe::deallocations.fetch_add(
            1,
            std::memory_order_relaxed);
    }
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

namespace {

using gc::audio::AudioCursorTimeline;
using gc::audio::AudioCursorResolution;
using gc::audio::AudioCursorResolutionKind;
using gc::audio::AudioRenderSpan;
using gc::audio::EndpointClockMapper;

int Expect(bool condition, std::string_view name) {
    if (condition) {
        return 0;
    }

    std::cerr << "Expected " << name << '\n';
    return 1;
}

bool ResolvesTo(
    const AudioCursorResolution& resolution,
    std::uint64_t source_frame) noexcept {
    return resolution.kind == AudioCursorResolutionKind::Resolved &&
        resolution.source_frame == source_frame;
}

int TestEmptyTimeline() {
    const AudioCursorTimeline timeline;
    return Expect(
        timeline.ResolveSourceFrame(0, 1, 100).kind ==
            AudioCursorResolutionKind::PendingGeneration,
        "an empty timeline generation to remain pending");
}

int TestHalfOpenSpanInterpolation() {
    AudioCursorTimeline timeline;
    timeline.Publish({100, 200, 0, 100, 1, false, false});

    int failures = 0;
    failures += Expect(
        ResolvesTo(timeline.ResolveSourceFrame(100, 1, 100), 0),
        "span begin to map to source begin");
    failures += Expect(
        ResolvesTo(timeline.ResolveSourceFrame(150, 1, 100), 50),
        "span midpoint to interpolate in source frames");
    failures += Expect(
        ResolvesTo(timeline.ResolveSourceFrame(199, 1, 100), 99),
        "last active output frame to map inside the source span");
    failures += Expect(
        timeline.ResolveSourceFrame(99, 1, 100).kind ==
            AudioCursorResolutionKind::PendingGeneration,
        "output before first presentation to remain pending");
    failures += Expect(
        timeline.ResolveSourceFrame(200, 1, 100).kind ==
            AudioCursorResolutionKind::Unmapped,
        "exclusive output end to be unmapped");
    failures += Expect(
        timeline.ResolveSourceFrame(150, 1, 0).kind ==
            AudioCursorResolutionKind::Unmapped,
        "zero source length to be rejected");
    return failures;
}

int TestLargeIntegerInterpolation() {
    constexpr std::uint64_t denominator = std::uint64_t{1} << 32;
    AudioCursorTimeline timeline;
    timeline.Publish({
        0,
        denominator,
        0,
        denominator * 2,
        4,
        false,
        false,
    });

    int failures = 0;
    failures += Expect(
        ResolvesTo(
            timeline.ResolveSourceFrame(
                denominator - 1,
                4,
                denominator * 4),
            (denominator - 1) * 2),
        "quotient/remainder interpolation to avoid a naive product overflow");

    AudioCursorTimeline fractional;
    fractional.Publish({
        0,
        denominator + 1,
        0,
        denominator,
        5,
        false,
        false,
    });
    failures += Expect(
        ResolvesTo(
            fractional.ResolveSourceFrame(
                denominator,
                5,
                denominator * 2),
            denominator - 1),
        "fractional remainder interpolation to avoid product overflow");
    return failures;
}

int TestLoopAndEndedSpans() {
    AudioCursorTimeline timeline;
    timeline.Publish({200, 300, 90, 110, 1, true, false});
    timeline.Publish({300, 325, 75, 100, 1, false, true});

    int failures = 0;
    failures += Expect(
        ResolvesTo(timeline.ResolveSourceFrame(250, 1, 100), 0),
        "unwrapped loop span to wrap only in the source domain");
    failures += Expect(
        ResolvesTo(timeline.ResolveSourceFrame(324, 1, 100), 99),
        "ended span to resolve through its last active output frame");
    failures += Expect(
        timeline.ResolveSourceFrame(325, 1, 100).kind ==
            AudioCursorResolutionKind::Unmapped,
        "ended span active output end to remain unmapped");
    return failures;
}

int TestEpochFilteringAndResynchronization() {
    AudioCursorTimeline timeline;
    timeline.Publish({0, 100, 0, 100, 1, false, false});
    timeline.Publish({100, 200, 100, 200, 1, false, false});

    int failures = 0;
    failures += Expect(
        timeline.ResolveSourceFrame(50, 2, 1000).kind ==
                AudioCursorResolutionKind::PendingGeneration &&
            timeline.ResolveSourceFrame(150, 2, 1000).kind ==
                AudioCursorResolutionKind::PendingGeneration,
        "generation 2 to remain pending behind generation-1 spans");

    timeline.Publish({1000, 1100, 0, 100, 2, false, false});
    timeline.Publish({1050, 1150, 500, 600, 3, false, false});
    failures += Expect(
        ResolvesTo(timeline.ResolveSourceFrame(1075, 3, 1000), 525),
        "newest epoch-3 resynchronization to supersede overlapping epoch 2");
    failures += Expect(
        timeline.ResolveSourceFrame(1025, 3, 1000).kind ==
            AudioCursorResolutionKind::PendingGeneration,
        "current generation before first presentation to remain pending");
    return failures;
}

int TestNewestSpanAndBoundedCapacity() {
    AudioCursorTimeline timeline;
    timeline.Publish({100, 200, 0, 100, 1, false, false});
    timeline.Publish({100, 200, 400, 500, 1, false, false});

    int failures = 0;
    failures += Expect(
        ResolvesTo(timeline.ResolveSourceFrame(150, 1, 1000), 450),
        "newest matching span to win");

    AudioCursorTimeline bounded;
    for (std::uint64_t generation = 0;
         generation < gc::audio::kRenderSpanCapacity + 8;
         ++generation) {
        const auto begin = generation * 10;
        bounded.Publish({
            begin,
            begin + 10,
            begin,
            begin + 10,
            1,
            false,
            false,
        });
    }
    failures += Expect(
        bounded.ResolveSourceFrame(5, 1, 1000).kind ==
            AudioCursorResolutionKind::PendingGeneration,
        "an output before the retained generation window to be pending");
    failures += Expect(
        ResolvesTo(bounded.ResolveSourceFrame(395, 1, 1000), 395),
        "newest fixed-ring span to remain available");
    return failures;
}

int TestCursorHelpers() {
    constexpr auto max_dword =
        std::numeric_limits<std::uint32_t>::max();
    constexpr auto max_uint64 =
        std::numeric_limits<std::uint64_t>::max();
    int failures = 0;
    failures += Expect(
        gc::audio::SourceFrameToByte(25, 4) == 100,
        "source frame 25 at block alignment 4 to become byte 100");
    failures += Expect(
        gc::audio::SourceFrameToByte(max_dword / 4, 4) == 4294967292ULL,
        "last four-byte-aligned frame in the DWORD byte domain to convert");
    failures += Expect(
        gc::audio::SourceFrameToByte(max_dword / 4 + 1, 4) == 0 &&
            gc::audio::SourceFrameToByte(max_uint64, 2) == 0 &&
            gc::audio::SourceFrameToByte(25, 0) == 0,
        "zero alignment and source-byte overflow to return zero safely");
    failures += Expect(
        gc::audio::ProjectWriteCursorFrame(
            90, 133, 44100, 44100, 100) == 23,
        "one 44.1 kHz endpoint period to project to write frame 23");
    failures += Expect(
        gc::audio::ProjectWriteCursorFrame(
            90, 133, 44100, 22050, 100) == 57,
        "one endpoint period at 22.05 kHz to project to write frame 57");
    failures += Expect(
        gc::audio::ProjectWriteCursorFrame(
            0,
            max_dword,
            44100,
            max_dword,
            max_uint64) == 418293516215865ULL,
        "maximum declared endpoint and source rates to ceil without overflow");
    failures += Expect(
        gc::audio::ProjectWriteCursorFrame(
            max_uint64 - 1,
            2,
            44100,
            44100,
            max_uint64) == 1,
        "write-frame modular addition to survive uint64 wrap");
    failures += Expect(
        gc::audio::ProjectWriteCursorFrame(
            max_uint64 - 17,
            max_dword,
            44100,
            max_dword,
            max_uint64 - 3) == 418293516215851ULL,
        "maximum-rate write projection to add modulo without overflow");
    failures += Expect(
        gc::audio::ProjectWriteCursorFrame(
            90, 133, 44100, 44100, 0) == 0,
        "zero source length projection to return zero safely");
    failures += Expect(
        gc::audio::ProjectWriteCursorFrame(
            90, 441, 44100, 44100, 1000) ==
            gc::audio::ProjectWriteCursorFrame(
                90, 480, 48000, 44100, 1000),
        "equal ten-millisecond periods project equal source-time lead");
    failures += Expect(
        gc::audio::ProjectWriteCursorFrame(
            90, 480, 0, 44100, 1000) == 0,
        "zero output rate projection to return zero safely");
    return failures;
}

int TestEndpointClockMapping() {
    EndpointClockMapper mapper;
    int failures = 0;
    failures += Expect(
        !mapper.ToOutputFrame(10000).has_value(),
        "clock mapping before reset to fail");

    mapper.Reset(10000, 10000000, 500, 44100);
    failures += Expect(
        mapper.ToOutputFrame(10010000) == 44600,
        "one device-clock second to map to 44,100 output frames");
    failures += Expect(
        !mapper.ToOutputFrame(9999).has_value(),
        "a device position regressing before the origin to fail");

    mapper.Reset(10000, 10000000, 500, 48000);
    failures += Expect(
        mapper.ToOutputFrame(10010000) == 48500,
        "one device-clock second to map to 48,000 output frames");

    mapper.Reset(10000, 0, 500, 44100);
    failures += Expect(
        !mapper.ToOutputFrame(10010000).has_value(),
        "zero clock frequency to invalidate mapping");
    mapper.Reset(10000, 10000000, 500, 0);
    failures += Expect(
        !mapper.ToOutputFrame(10010000).has_value(),
        "zero output rate to invalidate mapping");
    return failures;
}

AudioRenderSpan ConcurrentSpan(std::uint64_t generation) noexcept {
    const auto output_begin = generation * 4;
    const auto source_begin = generation * 8;
    return {
        output_begin,
        output_begin + 4,
        source_begin,
        source_begin + 4,
        generation,
        false,
        false,
    };
}

int TestSingleWriterMultipleReaders() {
    constexpr std::uint64_t publication_count = 20000;
    constexpr std::uint64_t source_length = 1000000;
    constexpr std::uint64_t minimum_reads = 4096;
    AudioCursorTimeline timeline;
    std::atomic<std::uint64_t> latest{};
    std::atomic<std::uint64_t> ready_readers{};
    std::array<std::atomic<bool>, 2> observed_first{};
    std::array<std::atomic<std::uint64_t>, 2> read_counts{};
    std::atomic<bool> writer_done{};
    std::atomic<bool> failed{};

    const auto reader = [&](std::size_t index) {
        ready_readers.fetch_add(1, std::memory_order_release);
        while (!writer_done.load(std::memory_order_acquire) ||
               read_counts[index].load(std::memory_order_relaxed) <
                   minimum_reads) {
            const auto generation = latest.load(std::memory_order_acquire);
            if (generation == 0) {
                std::this_thread::yield();
                continue;
            }

            const auto span = ConcurrentSpan(generation);
            const auto resolved = timeline.ResolveSourceFrame(
                span.output_frame_begin + 2,
                generation,
                source_length);
            if (resolved.kind == AudioCursorResolutionKind::Resolved) {
                if (resolved.source_frame !=
                    span.source_frame_begin_unwrapped + 2) {
                    failed.store(true, std::memory_order_relaxed);
                }
                observed_first[index].store(true, std::memory_order_release);
            }
            read_counts[index].fetch_add(1, std::memory_order_relaxed);
        }
    };

    std::thread first_reader(reader, 0);
    std::thread second_reader(reader, 1);
    std::thread writer([&] {
        while (ready_readers.load(std::memory_order_acquire) != 2) {
            std::this_thread::yield();
        }

        timeline.Publish(ConcurrentSpan(1));
        latest.store(1, std::memory_order_release);
        while (!observed_first[0].load(std::memory_order_acquire) ||
               !observed_first[1].load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }

        for (std::uint64_t generation = 2;
             generation <= publication_count;
             ++generation) {
            timeline.Publish(ConcurrentSpan(generation));
            latest.store(generation, std::memory_order_release);
        }
        writer_done.store(true, std::memory_order_release);
    });

    writer.join();
    first_reader.join();
    second_reader.join();

    const auto final_span = ConcurrentSpan(publication_count);
    int failures = 0;
    failures += Expect(
        !failed.load(std::memory_order_relaxed),
        "concurrent readers to accept only coherent render spans");
    failures += Expect(
        observed_first[0].load(std::memory_order_relaxed) &&
            observed_first[1].load(std::memory_order_relaxed) &&
            read_counts[0].load(std::memory_order_relaxed) >= minimum_reads &&
            read_counts[1].load(std::memory_order_relaxed) >= minimum_reads,
        "both readers to overlap the single writer");
    failures += Expect(
        ResolvesTo(
            timeline.ResolveSourceFrame(
                final_span.output_frame_begin + 2,
                publication_count,
                source_length),
            final_span.source_frame_begin_unwrapped + 2),
        "final publication to remain coherent after the concurrent run");
    return failures;
}

int TestPublishAndResolveDoNotAllocateOrFree() {
    AudioCursorTimeline timeline;
    const AudioRenderSpan span{100, 200, 0, 100, 1, false, false};

    allocation_probe::Begin();
    timeline.Publish(span);
    const auto resolved = timeline.ResolveSourceFrame(150, 1, 100);
    const auto counts = allocation_probe::End();

    int failures = 0;
    failures += Expect(
        ResolvesTo(resolved, 50),
        "probed publish and resolve result to remain usable");
    failures += Expect(
        counts.allocations == 0 && counts.deallocations == 0,
        "publish and resolve to allocate and free zero times");
    return failures;
}

} // namespace

int main() {
    int failures = 0;
    failures += TestEmptyTimeline();
    failures += TestHalfOpenSpanInterpolation();
    failures += TestLargeIntegerInterpolation();
    failures += TestLoopAndEndedSpans();
    failures += TestEpochFilteringAndResynchronization();
    failures += TestNewestSpanAndBoundedCapacity();
    failures += TestCursorHelpers();
    failures += TestEndpointClockMapping();
    failures += TestSingleWriterMultipleReaders();
    failures += TestPublishAndResolveDoNotAllocateOrFree();
    return failures == 0 ? 0 : 1;
}

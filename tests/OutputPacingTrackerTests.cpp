#include "OutputPacingTracker.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <string_view>

namespace allocation_probe {

std::atomic_bool enabled{};
std::atomic_size_t allocations{};

void Begin() noexcept {
    allocations.store(0, std::memory_order_relaxed);
    enabled.store(true, std::memory_order_seq_cst);
}

std::size_t End() noexcept {
    enabled.store(false, std::memory_order_seq_cst);
    return allocations.load(std::memory_order_relaxed);
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

using gc::audio::OutputPacingDecisionKind;
using gc::audio::OutputPacingTracker;

int Expect(bool condition, std::string_view name) {
    if (condition) {
        return 0;
    }
    std::cerr << "Expected " << name << '\n';
    return 1;
}

int TestPrefillAndSequentialProgression() {
    OutputPacingTracker tracker(441, 44'100);
    const auto first = tracker.Plan(0);
    int failures = Expect(
        first.kind == OutputPacingDecisionKind::Sequential &&
            first.block_begin == 441 && first.block_end == 882 &&
            first.discontinuity_begin == 441 &&
            first.discontinuity_frames == 0 &&
            first.submitted_lead_frames == 441,
        "prefill to leave one sequential packet queued");
    failures += Expect(
        tracker.Commit(first) && tracker.submitted_tail() == 882,
        "successful release to commit the first packet");

    const auto jitter = tracker.Plan(500);
    failures += Expect(
        jitter.kind == OutputPacingDecisionKind::Sequential &&
            jitter.block_begin == 882 && jitter.block_end == 1323 &&
            jitter.discontinuity_frames == 0 &&
            jitter.submitted_lead_frames == 382,
        "presentation jitter inside submitted audio not to skip time");
    failures += Expect(tracker.Commit(jitter), "sequential jitter commit");

    const auto repeated = tracker.Plan(500);
    failures += Expect(
        repeated.kind == OutputPacingDecisionKind::Sequential &&
            repeated.block_begin == 1323,
        "an equal presentation sample to remain valid");
    return failures;
}

int TestGapAlignment() {
    OutputPacingTracker tracker(441, 44'100);
    const auto first = tracker.Plan(0);
    tracker.Commit(first);

    const auto boundary = tracker.Plan(882);
    int failures = Expect(
        boundary.kind == OutputPacingDecisionKind::Sequential &&
            boundary.block_begin == 882 &&
            boundary.discontinuity_frames == 0 &&
            boundary.submitted_lead_frames == 0,
        "presentation exactly at the tail to use the next sequential slot");

    OutputPacingTracker one_gap(441, 44'100);
    const auto initial = one_gap.Plan(0);
    one_gap.Commit(initial);
    const auto gap = one_gap.Plan(900);
    failures += Expect(
        gap.kind == OutputPacingDecisionKind::RecoverableGap &&
            gap.discontinuity_begin == 882 &&
            gap.block_begin == 1323 && gap.block_end == 1764 &&
            gap.discontinuity_frames == 441 &&
            gap.submitted_lead_frames == -18,
        "one missed packet slot to align to the next packet boundary");

    OutputPacingTracker multiple(441, 44'100);
    const auto multiple_initial = multiple.Plan(0);
    multiple.Commit(multiple_initial);
    const auto multiple_gap = multiple.Plan(2000);
    failures += Expect(
        multiple_gap.kind == OutputPacingDecisionKind::RecoverableGap &&
            multiple_gap.discontinuity_begin == 882 &&
            multiple_gap.block_begin == 2205 &&
            multiple_gap.discontinuity_frames == 1323,
        "multiple missed packet slots to become one confirmed gap");
    return failures;
}

int TestInvalidClockAndCommitRejections() {
    OutputPacingTracker zero(0, 44'100);
    int failures = Expect(
        zero.Plan(0).kind == OutputPacingDecisionKind::InvalidClock,
        "zero packet frames to be invalid");
    OutputPacingTracker zero_rate(441, 0);
    failures += Expect(
        zero_rate.Plan(0).kind == OutputPacingDecisionKind::InvalidClock,
        "zero output rate to be invalid");

    OutputPacingTracker regression(441, 44'100);
    const auto forward = regression.Plan(100);
    failures += Expect(
        forward.kind == OutputPacingDecisionKind::Sequential &&
            regression.Plan(99).kind ==
                OutputPacingDecisionKind::InvalidClock,
        "presentation regression to be invalid");

    OutputPacingTracker overflow(10, 44'100);
    failures += Expect(
        overflow.Plan(std::numeric_limits<std::uint64_t>::max()).kind ==
            OutputPacingDecisionKind::InvalidClock,
        "align-up overflow to be invalid");

    const auto maximum = std::numeric_limits<std::uint64_t>::max();
    const auto packet = std::numeric_limits<std::uint32_t>::max();
    const auto last_boundary = maximum - maximum % packet;
    OutputPacingTracker block_overflow(packet, 44'100);
    failures += Expect(
        block_overflow.Plan(last_boundary).kind ==
            OutputPacingDecisionKind::InvalidClock,
        "block-end overflow to be invalid");

    OutputPacingTracker commit(441, 44'100);
    auto decision = commit.Plan(0);
    auto mismatched = decision;
    ++mismatched.block_end;
    failures += Expect(
        !commit.Commit(mismatched) && commit.submitted_tail() == 441,
        "a mismatched decision not to advance the tail");
    failures += Expect(
        commit.Commit(decision) && !commit.Commit(decision) &&
            commit.submitted_tail() == 882,
        "a committed decision not to be reusable");
    return failures;
}

int TestRollingGapPolicy() {
    OutputPacingTracker tracker(10, 44'100);
    const auto first = tracker.Plan(0);
    tracker.Commit(first);
    const auto gap1 = tracker.Plan(21);
    tracker.Commit(gap1);
    const auto gap2 = tracker.Plan(41);
    tracker.Commit(gap2);
    const auto gap3 = tracker.Plan(61);
    int failures = Expect(
        gap1.kind == OutputPacingDecisionKind::RecoverableGap &&
            gap2.kind == OutputPacingDecisionKind::RecoverableGap &&
            gap3.kind == OutputPacingDecisionKind::ChronicGap &&
            !tracker.Commit(gap3),
        "the third gap inside one hardware second to be fatal");

    OutputPacingTracker expiry(10, 44'100);
    const auto old1 = expiry.Plan(11);
    expiry.Commit(old1);
    const auto old2 = expiry.Plan(31);
    expiry.Commit(old2);
    const auto new1 = expiry.Plan(44'111);
    expiry.Commit(new1);
    const auto new2 = expiry.Plan(44'131);
    expiry.Commit(new2);
    const auto new3 = expiry.Plan(44'151);
    failures += Expect(
        old1.kind == OutputPacingDecisionKind::RecoverableGap &&
            old2.kind == OutputPacingDecisionKind::RecoverableGap &&
            new1.kind == OutputPacingDecisionKind::RecoverableGap &&
            new2.kind == OutputPacingDecisionKind::RecoverableGap &&
            new3.kind == OutputPacingDecisionKind::ChronicGap,
        "gap events at least one hardware second old to expire");

    OutputPacingTracker expiry_48000(10, 48'000);
    const auto rate48_old1 = expiry_48000.Plan(11);
    expiry_48000.Commit(rate48_old1);
    const auto rate48_old2 = expiry_48000.Plan(31);
    expiry_48000.Commit(rate48_old2);
    const auto rate48_new = expiry_48000.Plan(48'011);
    failures += Expect(
        rate48_new.kind == OutputPacingDecisionKind::RecoverableGap,
        "48 kHz gap expires at exactly 48,000 output frames");
    return failures;
}

int TestPlanningDoesNotAllocate() {
    OutputPacingTracker tracker(441, 44'100);
    allocation_probe::Begin();
    const auto first = tracker.Plan(0);
    const auto committed = tracker.Commit(first);
    const auto gap = tracker.Plan(900);
    const auto allocations = allocation_probe::End();
    return Expect(
        committed && gap.kind == OutputPacingDecisionKind::RecoverableGap &&
            allocations == 0,
        "planning and commit not to allocate");
}

} // namespace

int main() {
    int failures = 0;
    failures += TestPrefillAndSequentialProgression();
    failures += TestGapAlignment();
    failures += TestInvalidClockAndCommitRejections();
    failures += TestRollingGapPolicy();
    failures += TestPlanningDoesNotAllocate();
    return failures == 0 ? 0 : 1;
}

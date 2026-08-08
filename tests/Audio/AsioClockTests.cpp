// SPDX-License-Identifier: CC0-1.0

#include "Audio/Asio/AsioClock.h"

#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <string_view>

namespace {

using gc::audio::AsioClockDecision;
using gc::audio::AsioClockDecisionKind;
using gc::audio::AsioClockNowActions;
using gc::audio::AsioClockTracker;
using gc::audio::AsioPresentedClockPublication;

int Expect(bool condition, std::string_view name) {
    if (condition) {
        return 0;
    }
    std::cerr << "Expectation failed: " << name << '\n';
    return 1;
}

constexpr std::uint64_t Milliseconds(std::uint64_t value) noexcept {
    return value * 1'000'000;
}

AsioClockDecision StableDecision(
    std::uint64_t presented,
    std::uint64_t system_time_ns) noexcept {
    return {
        AsioClockDecisionKind::stable,
        presented,
        presented + 384,
        system_time_ns,
    };
}

int TestPrimingAndPhysicalPlacement() {
    AsioClockTracker tracker;
    tracker.Reset(192, 384);

    const auto first = tracker.Observe(0, Milliseconds(1'000));
    const auto second = tracker.Observe(192, Milliseconds(1'000));
    const auto third = tracker.Observe(384, Milliseconds(1'004));
    const auto fourth = tracker.Observe(576, Milliseconds(1'008));

    int failures{};
    failures += Expect(
        first.kind == AsioClockDecisionKind::priming,
        "first observation primes");
    failures += Expect(
        second.kind == AsioClockDecisionKind::priming,
        "repeated initial timestamp still primes");
    failures += Expect(
        third.kind == AsioClockDecisionKind::stable,
        "third consecutive block becomes stable");
    failures += Expect(
        third.presented_output_frame == 384,
        "sample position is the physically presented frame");
    failures += Expect(
        third.render_output_frame_begin == 768,
        "latency places the next rendered block in the future");
    failures += Expect(
        third.system_time_ns == Milliseconds(1'004),
        "stable decision preserves driver system time");
    failures += Expect(
        fourth.kind == AsioClockDecisionKind::stable &&
            fourth.presented_output_frame == 576 &&
            fourth.render_output_frame_begin == 960,
        "stable tracking advances one block at a time");
    return failures;
}

int TestPlacementRejectionsLatch() {
    int failures{};

    AsioClockTracker unaligned;
    unaligned.Reset(192, 384);
    failures += Expect(
        unaligned.Observe(1, Milliseconds(10)).kind ==
            AsioClockDecisionKind::invalid,
        "non-buffer-aligned position is rejected");
    failures += Expect(
        unaligned.Observe(192, Milliseconds(14)).kind ==
            AsioClockDecisionKind::invalid,
        "placement fault stays latched");

    AsioClockTracker regression;
    regression.Reset(192, 384);
    (void)regression.Observe(384, Milliseconds(20));
    failures += Expect(
        regression.Observe(192, Milliseconds(24)).kind ==
            AsioClockDecisionKind::invalid,
        "sample-position regression is rejected");

    AsioClockTracker skipped;
    skipped.Reset(192, 384);
    (void)skipped.Observe(0, Milliseconds(30));
    failures += Expect(
        skipped.Observe(384, Milliseconds(34)).kind ==
            AsioClockDecisionKind::invalid,
        "skipped block is rejected");

    AsioClockTracker overflow;
    overflow.Reset(1, std::numeric_limits<std::uint32_t>::max());
    (void)overflow.Observe(
        std::numeric_limits<std::uint64_t>::max() - 2,
        Milliseconds(40));
    (void)overflow.Observe(
        std::numeric_limits<std::uint64_t>::max() - 1,
        Milliseconds(41));
    failures += Expect(
        overflow.Observe(
            std::numeric_limits<std::uint64_t>::max(),
            Milliseconds(42)).kind == AsioClockDecisionKind::invalid,
        "future render-position overflow is rejected");

    AsioClockTracker invalid_configuration;
    invalid_configuration.Reset(0, 384);
    failures += Expect(
        invalid_configuration.Observe(0, Milliseconds(50)).kind ==
            AsioClockDecisionKind::invalid,
        "zero-sized buffer configuration is rejected");
    return failures;
}

int TestTimestampValidationAndWrap() {
    int failures{};

    AsioClockTracker missing;
    missing.Reset(192, 384);
    failures += Expect(
        missing.Observe(0, 0).kind == AsioClockDecisionKind::invalid,
        "zero system timestamp is rejected");

    AsioClockTracker repeated;
    repeated.Reset(192, 384);
    (void)repeated.Observe(0, Milliseconds(100));
    (void)repeated.Observe(192, Milliseconds(100));
    failures += Expect(
        repeated.Observe(384, Milliseconds(100)).kind ==
            AsioClockDecisionKind::invalid,
        "zero timestamp delta at stability boundary is rejected");

    AsioClockTracker after_stable;
    after_stable.Reset(192, 384);
    (void)after_stable.Observe(0, Milliseconds(200));
    (void)after_stable.Observe(192, Milliseconds(200));
    (void)after_stable.Observe(384, Milliseconds(204));
    failures += Expect(
        after_stable.Observe(576, Milliseconds(204)).kind ==
            AsioClockDecisionKind::invalid,
        "repeated timestamp after priming is rejected");
    failures += Expect(
        after_stable.Observe(768, Milliseconds(208)).kind ==
            AsioClockDecisionKind::invalid,
        "runtime clock fault does not restart priming");

    constexpr std::uint64_t near_wrap =
        std::numeric_limits<std::uint32_t>::max() - 3ULL;
    AsioClockTracker wrapping;
    wrapping.Reset(192, 384);
    (void)wrapping.Observe(0, Milliseconds(near_wrap));
    (void)wrapping.Observe(192, Milliseconds(near_wrap));
    failures += Expect(
        wrapping.Observe(384, Milliseconds(2)).kind ==
            AsioClockDecisionKind::stable,
        "normal low-32-bit millisecond wrap is accepted");

    AsioClockTracker backwards;
    backwards.Reset(192, 384);
    (void)backwards.Observe(0, Milliseconds(1'000));
    (void)backwards.Observe(192, Milliseconds(1'004));
    failures += Expect(
        backwards.Observe(384, Milliseconds(1'003)).kind ==
            AsioClockDecisionKind::invalid,
        "non-wrapping backwards timestamp is rejected");
    return failures;
}

int TestLegacyValuesUseTheSameTracker() {
    // These values model the exact samplePosition/systemTime pair returned by
    // IASIO::getSamplePosition when a legacy callback has no ASIOTime payload.
    AsioClockTracker tracker;
    tracker.Reset(192, 384);
    (void)tracker.Observe(9'600, Milliseconds(500));
    (void)tracker.Observe(9'792, Milliseconds(500));
    const auto decision = tracker.Observe(9'984, Milliseconds(504));

    return Expect(
        decision.kind == AsioClockDecisionKind::stable &&
            decision.presented_output_frame == 9'984 &&
            decision.render_output_frame_begin == 10'368,
        "legacy position pair follows identical clock rules");
}

struct FakeNow {
    std::uint32_t now_ms{};
    std::uint32_t calls{};
};

std::uint32_t ReadFakeNow(void* context) noexcept {
    auto& fake = *static_cast<FakeNow*>(context);
    ++fake.calls;
    return fake.now_ms;
}

int TestPublicationProjectionAndInvalidation() {
    FakeNow now{1'000, 0};
    AsioPresentedClockPublication publication({&now, &ReadFakeNow});

    int failures{};
    failures += Expect(
        !publication.CurrentOutputFrame().has_value() && now.calls == 0,
        "publication is null before a stable anchor");

    publication.Publish(
        {AsioClockDecisionKind::priming, 0, 0, Milliseconds(1'000)},
        2'000);
    failures += Expect(
        !publication.CurrentOutputFrame().has_value() && now.calls == 0,
        "priming decision is not published");

    publication.Publish(StableDecision(384, Milliseconds(1'000)), 700);
    failures += Expect(
        publication.CurrentOutputFrame() == std::optional<std::uint64_t>{384},
        "stable anchor publishes presented rather than rendered frame");

    now.now_ms = 1'002;
    failures += Expect(
        publication.CurrentOutputFrame() == std::optional<std::uint64_t>{480},
        "projection advances at exactly 48 frames per millisecond");

    now.now_ms = 1'001;
    failures += Expect(
        publication.CurrentOutputFrame() == std::optional<std::uint64_t>{480},
        "publication never moves below its last returned frame");

    now.now_ms = 1'100;
    failures += Expect(
        publication.CurrentOutputFrame() == std::optional<std::uint64_t>{700},
        "projection is capped at submitted output tail");

    publication.Invalidate();
    const auto calls_before_invalid_read = now.calls;
    now.now_ms = 1'200;
    failures += Expect(
        publication.CurrentOutputFrame() == std::optional<std::uint64_t>{700},
        "invalidation retains last returned frame");
    failures += Expect(
        now.calls == calls_before_invalid_read,
        "invalid publication no longer advances from wall time");
    return failures;
}

int TestPublicationTimeWrap() {
    constexpr std::uint32_t anchor_ms =
        std::numeric_limits<std::uint32_t>::max() - 1U;
    FakeNow now{2, 0};
    AsioPresentedClockPublication publication({&now, &ReadFakeNow});
    publication.Publish(
        StableDecision(100, Milliseconds(anchor_ms)),
        1'000);

    return Expect(
        publication.CurrentOutputFrame() == std::optional<std::uint64_t>{292},
        "publication projects across timeGetTime wrap");
}

int TestInvalidPublicationInputs() {
    FakeNow now{100, 0};
    AsioPresentedClockPublication publication({&now, &ReadFakeNow});
    publication.Publish(StableDecision(500, Milliseconds(100)), 499);

    int failures{};
    failures += Expect(
        !publication.CurrentOutputFrame().has_value(),
        "submitted tail before presented frame is rejected");

    publication.Publish(
        {AsioClockDecisionKind::invalid, 500, 884, Milliseconds(100)},
        1'000);
    failures += Expect(
        !publication.CurrentOutputFrame().has_value(),
        "invalid decision cannot create an anchor");
    return failures;
}

} // namespace

int main() {
    int failures{};
    failures += TestPrimingAndPhysicalPlacement();
    failures += TestPlacementRejectionsLatch();
    failures += TestTimestampValidationAndWrap();
    failures += TestLegacyValuesUseTheSameTracker();
    failures += TestPublicationProjectionAndInvalidation();
    failures += TestPublicationTimeWrap();
    failures += TestInvalidPublicationInputs();
    return failures == 0 ? 0 : 1;
}

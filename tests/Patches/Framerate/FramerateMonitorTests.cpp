#include "Patches/Framerate/FramerateMonitor.h"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>

namespace {

constexpr std::int64_t kFrequency = 1'438'704'000;

int Expect(bool condition, const char* name) {
    if (condition) {
        return 0;
    }
    std::cerr << "Expectation failed: " << name << "\n";
    return 1;
}

std::optional<gc::framerate::FramerateObservation> FeedSeconds(
    gc::framerate::FramerateMonitor& monitor,
    std::int64_t& now,
    double fps,
    double seconds,
    std::optional<std::size_t> one_stall_after = std::nullopt) {
    const auto step = static_cast<std::int64_t>(
        std::llround(static_cast<double>(kFrequency) / fps));
    const auto frames = static_cast<std::size_t>(
        std::ceil(fps * seconds)) + 2;
    std::optional<gc::framerate::FramerateObservation> last;
    for (std::size_t frame = 0; frame < frames; ++frame) {
        now += step;
        if (one_stall_after && frame == *one_stall_after) {
            now += kFrequency / 2;
        }
        if (auto observation = monitor.Observe(now)) {
            last = observation;
        }
    }
    return last;
}

gc::framerate::FramerateObservation ReachDecision(
    std::uint32_t target,
    double measured,
    bool add_stall = false) {
    auto monitor = gc::framerate::FramerateMonitor::Create(
        target, kFrequency).value();
    std::int64_t now = 0;
    (void)monitor.Observe(now);
    FeedSeconds(monitor, now, measured, 5.1);
    std::optional<gc::framerate::FramerateObservation> result;
    for (int window = 0; window < 3; ++window) {
        result = FeedSeconds(
            monitor,
            now,
            measured,
            2.1,
            add_stall && window == 1
                ? std::optional<std::size_t>{10}
                : std::nullopt);
    }
    return *result;
}

} // namespace

int main() {
    using gc::framerate::FramerateDecision;
    using gc::framerate::FramerateMonitor;
    int failures = 0;

    failures += Expect(!FramerateMonitor::Create(59, kFrequency),
        "reject invalid target");
    failures += Expect(!FramerateMonitor::Create(120, 0),
        "reject invalid frequency");
    failures += Expect(
        !FramerateMonitor::Create(
            120, std::numeric_limits<std::int64_t>::max()),
        "reject QPC duration multiplication overflow");

    auto boundaries = FramerateMonitor::Create(120, 120'000).value();
    std::int64_t boundary_now = 0;
    failures += Expect(!boundaries.Observe(boundary_now),
        "first timestamp only establishes epoch");
    for (int frame = 0; frame < 599; ++frame) {
        boundary_now += 1'000;
        failures += Expect(!boundaries.Observe(boundary_now),
            "no decision before five-second warm-up");
    }
    boundary_now = 600'000;
    failures += Expect(!boundaries.Observe(boundary_now),
        "five-second boundary starts collection");
    for (int frame = 0; frame < 239; ++frame) {
        boundary_now += 1'000;
        failures += Expect(!boundaries.Observe(boundary_now),
            "no result before two-second window boundary");
    }
    boundary_now = 840'000;
    const auto first_window = boundaries.Observe(boundary_now);
    failures += Expect(
        first_window &&
            first_window->decision == FramerateDecision::WindowMatch,
        "two-second boundary publishes first matching window");

    for (const std::uint32_t rate : {60U, 120U, 144U, 165U, 240U, 360U, 500U}) {
        const auto result = ReachDecision(rate, static_cast<double>(rate));
        failures += Expect(result.decision == FramerateDecision::Validated,
            "three matching windows validate");
        failures += Expect(result.matching_streak == 3,
            "matching streak reaches three");
        failures += Expect(std::fabs(result.relative_error) < 0.0001,
            "exact cadence error near zero");
    }

    const auto mismatch = ReachDecision(144, 120.0);
    failures += Expect(mismatch.decision == FramerateDecision::FatalMismatch,
        "144 versus 120 aborts");
    failures += Expect(mismatch.mismatching_streak == 3,
        "mismatch streak reaches three");

    const auto low_edge = ReachDecision(144, 144.0 * 0.97);
    const auto high_edge = ReachDecision(144, 144.0 * 1.03);
    failures += Expect(low_edge.decision == FramerateDecision::Validated,
        "minus-three-percent edge matches");
    failures += Expect(high_edge.decision == FramerateDecision::Validated,
        "plus-three-percent edge matches");
    failures += Expect(
        ReachDecision(144, 144.0 * 0.969).decision ==
            FramerateDecision::FatalMismatch,
        "outside low tolerance aborts");
    failures += Expect(
        ReachDecision(144, 144.0 * 1.031).decision ==
            FramerateDecision::FatalMismatch,
        "outside high tolerance aborts");

    const auto stalled = ReachDecision(240, 240.0, true);
    failures += Expect(stalled.decision == FramerateDecision::Validated,
        "single long interval does not dominate median");

    const auto overflow = ReachDecision(500, 2'000.0);
    failures += Expect(
        overflow.decision == FramerateDecision::FatalMismatch &&
            overflow.storage_overflowed,
        "uncapped sample overflow is mismatch evidence");

    auto streaks = FramerateMonitor::Create(144, kFrequency).value();
    std::int64_t now = 0;
    (void)streaks.Observe(now);
    FeedSeconds(streaks, now, 144.0, 5.1);
    FeedSeconds(streaks, now, 144.0, 2.1);
    auto reset_to_mismatch = FeedSeconds(streaks, now, 120.0, 2.1).value();
    failures += Expect(
        reset_to_mismatch.matching_streak == 0 &&
            reset_to_mismatch.mismatching_streak == 1,
        "mismatch resets matching streak");
    auto reset_to_match = FeedSeconds(streaks, now, 144.0, 2.1).value();
    failures += Expect(
        reset_to_match.matching_streak == 1 &&
            reset_to_match.mismatching_streak == 0,
        "match resets mismatch streak");

    auto disabled = FramerateMonitor::Create(120, kFrequency).value();
    now = 0;
    (void)disabled.Observe(now);
    FeedSeconds(disabled, now, 120.0, 5.1);
    FeedSeconds(disabled, now, 120.0, 6.3);
    failures += Expect(!disabled.active(), "validated monitor disables itself");
    failures += Expect(!disabled.Observe(now + kFrequency),
        "disabled monitor publishes nothing later");

    auto invalid_clock = FramerateMonitor::Create(120, kFrequency).value();
    (void)invalid_clock.Observe(100);
    const auto clock_result = invalid_clock.Observe(99);
    failures += Expect(
        clock_result &&
            clock_result->decision == FramerateDecision::FatalClock,
        "nonmonotonic clock fails closed");

    return failures == 0 ? 0 : 1;
}

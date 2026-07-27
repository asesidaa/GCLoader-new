#include "Patches/Framerate/GameplaySongClock.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string_view>

namespace {

using gc::framerate::CountCrossedAuthored60Ticks;
using gc::framerate::CrossesAuthored60Cadence;
using gc::framerate::FramerateProfile;
using gc::framerate::FramerateProfileError;
using gc::framerate::GameplaySongClock;
using gc::framerate::GameplaySongClockDecision;
using gc::framerate::GameplaySongClockError;
using gc::framerate::SongClockObservation;
using gc::framerate::SongClockObservationKind;

int Expect(bool condition, std::string_view name) {
    if (condition) {
        return 0;
    }
    std::cerr << "Expected " << name << '\n';
    return 1;
}

SongClockObservation Exact(
    std::uint64_t source_frame,
    std::uint32_t source_rate = 44'100,
    std::uint64_t generation = 1) noexcept {
    return {
        .kind = SongClockObservationKind::ExactSourceFrame,
        .position = source_frame,
        .source_sample_rate = source_rate,
        .playback_generation = generation,
    };
}

SongClockObservation Rounded(std::uint64_t milliseconds) noexcept {
    return {
        .kind = SongClockObservationKind::RoundedMilliseconds,
        .position = milliseconds,
    };
}

int TestConstructionAndAbsoluteConversion() {
    int failures = 0;
    failures += Expect(
        GameplaySongClock::Create(0, 1).error() ==
            GameplaySongClockError::InvalidRate,
        "zero rate numerator to be rejected");
    failures += Expect(
        GameplaySongClock::Create(60, 0).error() ==
            GameplaySongClockError::InvalidRate,
        "zero rate denominator to be rejected");
    failures += Expect(
        GameplaySongClock::Create(
            std::numeric_limits<std::uint32_t>::max(),
            1,
            std::numeric_limits<std::uint32_t>::max()).error() ==
            GameplaySongClockError::DestinationOverflow,
        "an unrepresentable catch-up step to be rejected");

    auto clock44 = GameplaySongClock::Create(60, 1).value();
    const auto one_second44 = clock44.Observe(
        59, 0, Exact(44'100, 44'100, 1));
    failures += Expect(
        one_second44.has_value() &&
            one_second44->desired_tick == 60 &&
            one_second44->delta_ticks == 1 &&
            one_second44->step == 1 &&
            one_second44->remaining_backlog == 0 &&
            one_second44->new_generation,
        "one 44.1 kHz source second to become target tick 60");

    auto clock48 = GameplaySongClock::Create(60, 1).value();
    const auto one_second48 = clock48.Observe(
        59, 0, Exact(48'000, 48'000, 2));
    failures += Expect(
        one_second48.has_value() &&
            one_second48->desired_tick == 60 &&
            one_second48->step == 1,
        "one 48 kHz source second to become the same target tick");

    auto offset_clock = GameplaySongClock::Create(60, 1).value();
    const auto positive_offset = offset_clock.Observe(
        59, 500, Exact(22'050, 44'100, 3));
    failures += Expect(
        positive_offset.has_value() &&
            positive_offset->desired_tick == 60,
        "positive GameTimeOffset to add to exact source time");

    auto negative_clock = GameplaySongClock::Create(60, 1).value();
    const auto negative_offset = negative_clock.Observe(
        0, -1, Exact(0, 44'100, 4));
    failures += Expect(
        negative_offset.has_value() &&
            negative_offset->desired_tick == -1 &&
            negative_offset->delta_ticks == -1 &&
            negative_offset->step == 0,
        "negative adjusted source time to use mathematical floor");

    auto rounded_clock = GameplaySongClock::Create(60, 1).value();
    const auto rounded_second =
        rounded_clock.Observe(59, 0, Rounded(1'000));
    const auto rounded_negative =
        rounded_clock.Observe(0, -1, Rounded(0));
    failures += Expect(
        rounded_second.has_value() &&
            rounded_second->desired_tick == 60 &&
            rounded_second->step == 1,
        "whole-millisecond fallback to map absolute time");
    failures += Expect(
        rounded_negative.has_value() &&
            rounded_negative->desired_tick == -1 &&
            rounded_negative->step == 0,
        "rounded fallback to use the same signed floor rule");

    auto rational_clock = GameplaySongClock::Create(120, 2).value();
    const auto rational_second =
        rational_clock.Observe(59, 0, Exact(44'100));
    failures += Expect(
        rational_second.has_value() &&
            rational_second->desired_tick == 60,
        "rate numerator and denominator to remain rational");
    return failures;
}

int TestStepBoundAndBacklog() {
    int failures = 0;
    auto clock240 = GameplaySongClock::Create(240, 1).value();
    const auto first =
        clock240.Observe(0, 0, Exact(44'100, 44'100, 7));
    failures += Expect(
        first.has_value() &&
            first->desired_tick == 240 &&
            first->delta_ticks == 240 &&
            first->step == 12 &&
            first->remaining_backlog == 228,
        "240 FPS catch-up to be bounded to 50 milliseconds");

    const auto second =
        clock240.Observe(12, 0, Exact(44'100, 44'100, 7));
    failures += Expect(
        second.has_value() &&
            second->step == 12 &&
            second->remaining_backlog == 216,
        "a large backlog to drain over later updates");

    auto clock60 = GameplaySongClock::Create(60, 1).value();
    const auto sixty =
        clock60.Observe(0, 0, Exact(44'100, 44'100, 8));
    failures += Expect(
        sixty.has_value() &&
            sixty->step == 3 &&
            sixty->remaining_backlog == 57,
        "60 FPS catch-up to use a three-tick 50 ms bound");

    auto minimum = GameplaySongClock::Create(60, 1, 1).value();
    const auto minimum_step =
        minimum.Observe(0, 0, Exact(44'100, 44'100, 9));
    failures += Expect(
        minimum_step.has_value() &&
            minimum_step->step == 1 &&
            minimum_step->remaining_backlog == 59,
        "sub-tick catch-up duration to retain a one-tick minimum");

    auto zero_delta_clock = GameplaySongClock::Create(60, 1).value();
    const auto zero =
        zero_delta_clock.Observe(60, 0, Exact(44'100, 44'100, 10));
    const auto behind =
        zero_delta_clock.Observe(61, 0, Exact(44'100, 44'100, 10));
    failures += Expect(
        zero.has_value() && zero->delta_ticks == 0 && zero->step == 0,
        "zero delta to select a zero step");
    failures += Expect(
        behind.has_value() &&
            behind->delta_ticks == -1 &&
            behind->step == 0,
        "audio behind current gameplay not to move gameplay backwards");

    auto overflow_clock = GameplaySongClock::Create(60, 1).value();
    failures += Expect(
        overflow_clock.Observe(
            0, 0, Rounded(100'000'000'000ULL)).error() ==
            GameplaySongClockError::DestinationOverflow,
        "an unrepresentable diagnostic backlog to be rejected");

    auto tick_overflow_clock =
        GameplaySongClock::Create(60, 1).value();
    constexpr auto first_unrepresentable_tick =
        static_cast<std::uint64_t>(
            std::numeric_limits<std::uint32_t>::max()) + 1;
    constexpr auto first_unrepresentable_milliseconds =
        (first_unrepresentable_tick * 1'000 + 59) / 60;
    const auto tick_overflow = tick_overflow_clock.Observe(
        std::numeric_limits<std::uint32_t>::max(),
        0,
        Rounded(first_unrepresentable_milliseconds));
    failures += Expect(
        !tick_overflow &&
            tick_overflow.error() ==
                GameplaySongClockError::DestinationOverflow,
        "a selected step that would overflow Tune tick to be rejected");
    return failures;
}

int TestExactGenerationsAndRejectedObservations() {
    auto clock = GameplaySongClock::Create(120, 1).value();
    int failures = 0;

    const auto first = clock.Observe(0, 0, Exact(1'000, 44'100, 11));
    const auto later = clock.Observe(0, 0, Exact(2'000, 44'100, 11));
    failures += Expect(
        first.has_value() && first->new_generation &&
            later.has_value() && !later->new_generation,
        "the first exact epoch observation alone to mark a new generation");

    failures += Expect(
        clock.Observe(0, 0, Exact(1'999, 44'100, 11)).error() ==
            GameplaySongClockError::BackwardsObservation,
        "same-generation backwards source motion to be rejected");

    const auto rounded = clock.Observe(0, 0, Rounded(500));
    const auto exact_after_rounded =
        clock.Observe(0, 0, Exact(2'001, 44'100, 11));
    failures += Expect(
        rounded.has_value() &&
            exact_after_rounded.has_value() &&
            !exact_after_rounded->new_generation,
        "rounded fallback not to mutate exact generation state");

    const auto new_generation =
        clock.Observe(0, 0, Exact(0, 44'100, 12));
    failures += Expect(
        new_generation.has_value() && new_generation->new_generation,
        "a new playback generation to accept a lower absolute source frame");

    failures += Expect(
        clock.Observe(0, 0, Exact(10, 0, 12)).error() ==
            GameplaySongClockError::InvalidObservation,
        "an exact observation with zero source rate to be rejected");

    const auto maximum = std::numeric_limits<std::uint64_t>::max();
    failures += Expect(
        clock.Observe(0, 0, Exact(maximum, 44'100, 12)).error() ==
            GameplaySongClockError::ArithmeticOverflow,
        "an overflowing source-frame product to be rejected");
    const auto after_overflow =
        clock.Observe(0, 0, Exact(1, 44'100, 12));
    failures += Expect(
        after_overflow.has_value() && !after_overflow->new_generation,
        "a rejected observation not to mutate the exact monotonic baseline");

    auto rounded_overflow = GameplaySongClock::Create(60, 1).value();
    failures += Expect(
        rounded_overflow.Observe(0, 0, Rounded(maximum)).error() ==
            GameplaySongClockError::ArithmeticOverflow,
        "an overflowing rounded-millisecond observation to be rejected");
    return failures;
}

int TestAuthored60Ranges() {
    const auto profile60 = FramerateProfile::Create(60).value();
    const auto profile120 = FramerateProfile::Create(120).value();
    const auto profile144 = FramerateProfile::Create(144).value();
    const auto profile240 = FramerateProfile::Create(240).value();
    int failures = 0;

    failures += Expect(
        CountCrossedAuthored60Ticks(profile60, 10, 0).value() == 0 &&
            CountCrossedAuthored60Ticks(profile60, 10, 1).value() == 1 &&
            CountCrossedAuthored60Ticks(profile60, 10, 2).value() == 2,
        "native-60 boundary count to preserve zero, one, and two steps");
    failures += Expect(
        CountCrossedAuthored60Ticks(profile240, 6, 1).value() == 0 &&
            CountCrossedAuthored60Ticks(profile240, 7, 1).value() == 1,
        "240 FPS effect boundary to occur on each fourth target tick");
    failures += Expect(
        CountCrossedAuthored60Ticks(profile144, 2, 3).value() == 2,
        "a non-integral target range to count every authored crossing");
    failures += Expect(
        CountCrossedAuthored60Ticks(
            profile60,
            std::numeric_limits<std::uint32_t>::max(),
            1).error() == FramerateProfileError::DestinationOverflow,
        "authored range end overflow to be rejected");

    failures += Expect(
        !CrossesAuthored60Cadence(profile60, 5, 0, 0, 6).value() &&
            !CrossesAuthored60Cadence(profile60, 5, 1, 0, 6).value() &&
            CrossesAuthored60Cadence(profile60, 5, 2, 0, 6).value(),
        "pre-commit half-open cadence range to see an intermediate tick");
    failures += Expect(
        CrossesAuthored60Cadence(profile60, 6, 1, 0, 6).value() &&
            !CrossesAuthored60Cadence(profile60, 7, 1, 0, 6).value(),
        "step one cadence range to preserve point-test behavior");
    failures += Expect(
        CrossesAuthored60Cadence(profile120, 11, 2, 0, 6).value(),
        "120 FPS cadence range to see authored frame six");
    failures += Expect(
        CrossesAuthored60Cadence(profile60, 1, 1, -1, 4).value(),
        "signed negative cadence phase to retain normalized modulo");
    failures += Expect(
        CrossesAuthored60Cadence(profile60, 0, 1, 0, 0).error() ==
            FramerateProfileError::InvalidPeriod,
        "zero cadence period to be rejected");
    return failures;
}

struct SimulationResult {
    int failures{};
    std::uint64_t updates{};
    std::uint64_t zero_steps{};
    std::uint64_t multi_steps{};
    std::uint32_t final_tick{};
    std::uint64_t final_desired{};
};

SimulationResult RunSimulation(
    std::uint32_t target_fps,
    std::uint32_t source_rate,
    std::uint64_t outer_rate_numerator,
    std::uint64_t outer_rate_denominator,
    std::uint32_t seconds) {
    SimulationResult result{};
    auto clock = GameplaySongClock::Create(target_fps, 1).value();
    const auto updates =
        static_cast<std::uint64_t>(seconds) * outer_rate_numerator /
        outer_rate_denominator;
    std::uint32_t current_tick{};
    const auto maximum_step = std::max(1U, target_fps * 50U / 1'000U);

    for (std::uint64_t update = 1; update <= updates; ++update) {
        const auto source_frame =
            update * source_rate * outer_rate_denominator /
            outer_rate_numerator;
        const auto expected_desired =
            source_frame * target_fps / source_rate;
        const auto decision = clock.Observe(
            current_tick,
            0,
            Exact(source_frame, source_rate, 1));
        if (!decision.has_value()) {
            ++result.failures;
            break;
        }
        if (decision->desired_tick !=
                static_cast<std::int64_t>(expected_desired) ||
            decision->step > maximum_step) {
            ++result.failures;
            break;
        }

        result.zero_steps += decision->step == 0;
        result.multi_steps += decision->step > 1;
        current_tick += decision->step;
        result.final_desired = expected_desired;
    }

    result.updates = updates;
    result.final_tick = current_tick;
    return result;
}

int TestTenMinuteAbsoluteClockSimulations() {
    int failures = 0;
    for (const auto target : {60U, 120U, 144U, 165U, 240U, 360U}) {
        const auto simulation = RunSimulation(
            target,
            44'100,
            target,
            1,
            600);
        failures += Expect(
            simulation.failures == 0 &&
                simulation.final_tick == simulation.final_desired,
            "ten-minute exact-target simulation to end at the absolute oracle");
    }

    const auto non_integral_144 =
        RunSimulation(144, 48'000, 144, 1, 600);
    const auto non_integral_165 =
        RunSimulation(165, 44'100, 165, 1, 600);
    failures += Expect(
        non_integral_144.failures == 0 &&
            non_integral_144.final_tick ==
                non_integral_144.final_desired &&
            non_integral_165.failures == 0 &&
            non_integral_165.final_tick ==
                non_integral_165.final_desired,
        "144 and 165 FPS to need no integer sample period");

    const auto near_5994 =
        RunSimulation(60, 44'100, 60'000, 1'001, 600);
    failures += Expect(
        near_5994.failures == 0 &&
            near_5994.final_tick == near_5994.final_desired &&
            near_5994.multi_steps >= 34 &&
            near_5994.multi_steps <= 37,
        "59.94 outer cadence to use about 36 step-two corrections");

    const auto near_239703 =
        RunSimulation(240, 44'100, 239'703, 1'000, 600);
    const auto net_extra_steps =
        near_239703.multi_steps - near_239703.zero_steps;
    failures += Expect(
        near_239703.failures == 0 &&
            near_239703.final_tick == near_239703.final_desired &&
            net_extra_steps >= 175 &&
            net_extra_steps <= 180,
        "239.703 outer cadence to add one net tick about every 3.4 seconds");

    const auto slightly_fast =
        RunSimulation(60, 48'000, 60'060, 1'000, 600);
    failures += Expect(
        slightly_fast.failures == 0 &&
            slightly_fast.final_tick == slightly_fast.final_desired &&
            slightly_fast.zero_steps > 0,
        "a slightly fast outer cadence to use zero-step corrections");
    return failures;
}

} // namespace

int main() {
    int failures = 0;
    failures += TestConstructionAndAbsoluteConversion();
    failures += TestStepBoundAndBacklog();
    failures += TestExactGenerationsAndRejectedObservations();
    failures += TestAuthored60Ranges();
    failures += TestTenMinuteAbsoluteClockSimulations();
    return failures == 0 ? 0 : 1;
}

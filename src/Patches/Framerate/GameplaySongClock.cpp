#include "Patches/Framerate/GameplaySongClock.h"

#include "Patches/Framerate/FramerateAuthoredClock.h"

#include <algorithm>
#include <limits>

namespace gc::framerate {
namespace {

bool CheckedAdd(
    std::int64_t left,
    std::int64_t right,
    std::int64_t& result) noexcept {
    constexpr auto minimum = std::numeric_limits<std::int64_t>::min();
    constexpr auto maximum = std::numeric_limits<std::int64_t>::max();
    if ((right > 0 && left > maximum - right) ||
        (right < 0 && left < minimum - right)) {
        return false;
    }
    result = left + right;
    return true;
}

bool CheckedMultiplyByNonNegative(
    std::int64_t value,
    std::uint64_t factor,
    std::int64_t& result) noexcept {
    constexpr auto minimum = std::numeric_limits<std::int64_t>::min();
    constexpr auto maximum = std::numeric_limits<std::int64_t>::max();
    if (factor > static_cast<std::uint64_t>(maximum)) {
        return false;
    }
    const auto signed_factor = static_cast<std::int64_t>(factor);
    if (signed_factor != 0 &&
        ((value > 0 && value > maximum / signed_factor) ||
         (value < 0 && value < minimum / signed_factor))) {
        return false;
    }
    result = value * signed_factor;
    return true;
}

bool CheckedMultiply(
    std::uint64_t left,
    std::uint64_t right,
    std::uint64_t& result) noexcept {
    if (left != 0 &&
        right > std::numeric_limits<std::uint64_t>::max() / left) {
        return false;
    }
    result = left * right;
    return true;
}

std::int64_t FloorDivide(
    std::int64_t numerator,
    std::int64_t positive_denominator) noexcept {
    auto quotient = numerator / positive_denominator;
    if (numerator % positive_denominator < 0) {
        --quotient;
    }
    return quotient;
}

std::expected<std::int64_t, GameplaySongClockError>
ConvertExactObservation(
    const SongClockObservation& observation,
    std::int32_t game_time_offset_ms,
    std::uint32_t rate_numerator,
    std::uint32_t rate_denominator) noexcept {
    constexpr auto maximum = std::numeric_limits<std::int64_t>::max();
    if (observation.source_sample_rate == 0 ||
        observation.position >
            static_cast<std::uint64_t>(maximum) / 1'000) {
        return std::unexpected(
            observation.source_sample_rate == 0
                ? GameplaySongClockError::InvalidObservation
                : GameplaySongClockError::ArithmeticOverflow);
    }

    std::int64_t source_time_numerator{};
    if (!CheckedMultiplyByNonNegative(
            static_cast<std::int64_t>(observation.position),
            1'000,
            source_time_numerator)) {
        return std::unexpected(GameplaySongClockError::ArithmeticOverflow);
    }

    std::int64_t offset_numerator{};
    if (!CheckedMultiplyByNonNegative(
            game_time_offset_ms,
            observation.source_sample_rate,
            offset_numerator)) {
        return std::unexpected(GameplaySongClockError::ArithmeticOverflow);
    }

    std::int64_t adjusted_time_numerator{};
    if (!CheckedAdd(
            source_time_numerator,
            offset_numerator,
            adjusted_time_numerator)) {
        return std::unexpected(GameplaySongClockError::ArithmeticOverflow);
    }

    std::int64_t desired_numerator{};
    if (!CheckedMultiplyByNonNegative(
            adjusted_time_numerator,
            rate_numerator,
            desired_numerator)) {
        return std::unexpected(GameplaySongClockError::ArithmeticOverflow);
    }

    std::uint64_t denominator{};
    std::uint64_t source_milliseconds{};
    if (!CheckedMultiply(
            observation.source_sample_rate,
            1'000,
            source_milliseconds) ||
        !CheckedMultiply(
            source_milliseconds,
            rate_denominator,
            denominator) ||
        denominator == 0 ||
        denominator > static_cast<std::uint64_t>(maximum)) {
        return std::unexpected(GameplaySongClockError::ArithmeticOverflow);
    }

    return FloorDivide(
        desired_numerator,
        static_cast<std::int64_t>(denominator));
}

std::expected<std::int64_t, GameplaySongClockError>
ConvertRoundedObservation(
    const SongClockObservation& observation,
    std::int32_t game_time_offset_ms,
    std::uint32_t rate_numerator,
    std::uint32_t rate_denominator) noexcept {
    constexpr auto maximum = std::numeric_limits<std::int64_t>::max();
    if (observation.position > static_cast<std::uint64_t>(maximum)) {
        return std::unexpected(GameplaySongClockError::ArithmeticOverflow);
    }

    std::int64_t adjusted_milliseconds{};
    if (!CheckedAdd(
            static_cast<std::int64_t>(observation.position),
            game_time_offset_ms,
            adjusted_milliseconds)) {
        return std::unexpected(GameplaySongClockError::ArithmeticOverflow);
    }

    std::int64_t desired_numerator{};
    if (!CheckedMultiplyByNonNegative(
            adjusted_milliseconds,
            rate_numerator,
            desired_numerator)) {
        return std::unexpected(GameplaySongClockError::ArithmeticOverflow);
    }

    const auto denominator =
        static_cast<std::uint64_t>(rate_denominator) * 1'000;
    if (denominator == 0 ||
        denominator > static_cast<std::uint64_t>(maximum)) {
        return std::unexpected(GameplaySongClockError::ArithmeticOverflow);
    }
    return FloorDivide(
        desired_numerator,
        static_cast<std::int64_t>(denominator));
}

} // namespace

std::expected<GameplaySongClock, GameplaySongClockError>
GameplaySongClock::Create(
    std::uint32_t rate_numerator,
    std::uint32_t rate_denominator,
    std::uint32_t catchup_milliseconds) noexcept {
    if (rate_numerator == 0 || rate_denominator == 0) {
        return std::unexpected(GameplaySongClockError::InvalidRate);
    }

    const auto catchup_numerator =
        static_cast<std::uint64_t>(catchup_milliseconds) *
        rate_numerator;
    const auto catchup_denominator =
        static_cast<std::uint64_t>(rate_denominator) * 1'000;
    const auto maximum_step = std::max<std::uint64_t>(
        1,
        catchup_numerator / catchup_denominator);
    if (maximum_step > std::numeric_limits<std::uint32_t>::max()) {
        return std::unexpected(
            GameplaySongClockError::DestinationOverflow);
    }

    GameplaySongClock clock;
    clock.rate_numerator_ = rate_numerator;
    clock.rate_denominator_ = rate_denominator;
    clock.maximum_step_ = static_cast<std::uint32_t>(maximum_step);
    return clock;
}

std::expected<GameplaySongClockDecision, GameplaySongClockError>
GameplaySongClock::Observe(
    std::uint32_t current_tick,
    std::int32_t game_time_offset_ms,
    const SongClockObservation& observation) noexcept {
    bool new_generation{};
    if (observation.kind == SongClockObservationKind::ExactSourceFrame) {
        new_generation =
            !has_exact_generation_ ||
            observation.playback_generation != exact_generation_;
        if (!new_generation &&
            observation.position < last_exact_source_frame_) {
            return std::unexpected(
                GameplaySongClockError::BackwardsObservation);
        }
    } else if (
        observation.kind !=
        SongClockObservationKind::RoundedMilliseconds) {
        return std::unexpected(
            GameplaySongClockError::InvalidObservation);
    }

    const auto desired =
        observation.kind == SongClockObservationKind::ExactSourceFrame
        ? ConvertExactObservation(
              observation,
              game_time_offset_ms,
              rate_numerator_,
              rate_denominator_)
        : ConvertRoundedObservation(
              observation,
              game_time_offset_ms,
              rate_numerator_,
              rate_denominator_);
    if (!desired) {
        return std::unexpected(desired.error());
    }

    std::int64_t delta{};
    if (!CheckedAdd(
            desired.value(),
            -static_cast<std::int64_t>(current_tick),
            delta)) {
        return std::unexpected(GameplaySongClockError::ArithmeticOverflow);
    }

    std::uint32_t step{};
    std::uint32_t remaining_backlog{};
    if (delta > 0) {
        const auto selected = std::min<std::uint64_t>(
            static_cast<std::uint64_t>(delta),
            maximum_step_);
        const auto remaining =
            static_cast<std::uint64_t>(delta) - selected;
        if (remaining > std::numeric_limits<std::uint32_t>::max()) {
            return std::unexpected(
                GameplaySongClockError::DestinationOverflow);
        }
        step = static_cast<std::uint32_t>(selected);
        remaining_backlog = static_cast<std::uint32_t>(remaining);
    }

    if (observation.kind == SongClockObservationKind::ExactSourceFrame) {
        has_exact_generation_ = true;
        exact_generation_ = observation.playback_generation;
        last_exact_source_frame_ = observation.position;
    }

    return GameplaySongClockDecision{
        .desired_tick = desired.value(),
        .delta_ticks = delta,
        .step = step,
        .remaining_backlog = remaining_backlog,
        .new_generation = new_generation,
    };
}

std::expected<std::uint32_t, FramerateProfileError>
CountCrossedAuthored60Ticks(
    const FramerateProfile& profile,
    std::uint32_t current_tick,
    std::uint32_t step) noexcept {
    if (step >
        std::numeric_limits<std::uint32_t>::max() - current_tick) {
        return std::unexpected(
            FramerateProfileError::DestinationOverflow);
    }

    const auto end_tick = current_tick + step;
    const auto current_authored = profile.MapToAuthored60(current_tick);
    if (!current_authored) {
        return std::unexpected(current_authored.error());
    }
    const auto end_authored = profile.MapToAuthored60(end_tick);
    if (!end_authored) {
        return std::unexpected(end_authored.error());
    }
    if (end_authored.value() < current_authored.value()) {
        return std::unexpected(
            FramerateProfileError::ArithmeticOverflow);
    }
    return end_authored.value() - current_authored.value();
}

std::expected<bool, FramerateProfileError>
CrossesAuthored60Cadence(
    const FramerateProfile& profile,
    std::uint32_t current_tick,
    std::uint32_t step,
    std::int32_t phase,
    std::uint32_t authored_period) noexcept {
    if (authored_period == 0) {
        return std::unexpected(FramerateProfileError::InvalidPeriod);
    }
    if (step >
        std::numeric_limits<std::uint32_t>::max() - current_tick) {
        return std::unexpected(
            FramerateProfileError::DestinationOverflow);
    }

    const auto end_tick = current_tick + step;
    for (auto tick = current_tick; tick < end_tick; ++tick) {
        const auto run = ShouldRunAuthored60Cadence(
            profile,
            tick,
            phase,
            authored_period);
        if (!run) {
            return std::unexpected(run.error());
        }
        if (run.value()) {
            return true;
        }
    }
    return false;
}

} // namespace gc::framerate

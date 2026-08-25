#pragma once

#include "Patches/Framerate/FramerateProfile.h"

#include <cstdint>
#include <expected>

namespace gc::framerate {

enum class SongClockObservationKind : std::uint8_t {
    ExactSourceFrame,
    RoundedMilliseconds,
};

struct SongClockObservation {
    SongClockObservationKind kind{};
    std::uint64_t position{};
    std::uint32_t source_sample_rate{};
    std::uint64_t buffer_instance_id{};
    std::uint64_t playback_generation{};
};

enum class GameplaySongClockError : std::uint8_t {
    InvalidRate,
    InvalidObservation,
    ArithmeticOverflow,
    DestinationOverflow,
    BackwardsObservation,
};

struct GameplaySongClockDecision {
    std::int64_t desired_tick{};
    std::int64_t delta_ticks{};
    std::uint32_t step{};
    std::uint32_t remaining_backlog{};
    bool new_playback_epoch{};
};

class GameplaySongClock final {
public:
    [[nodiscard]] static std::expected<
        GameplaySongClock,
        GameplaySongClockError>
    Create(
        std::uint32_t rate_numerator,
        std::uint32_t rate_denominator,
        std::uint32_t catchup_milliseconds = 50) noexcept;

    [[nodiscard]] std::expected<
        GameplaySongClockDecision,
        GameplaySongClockError>
    Observe(
        std::uint32_t current_tick,
        std::int32_t game_time_offset_ms,
        const SongClockObservation& observation) noexcept;

private:
    std::uint32_t rate_numerator_{};
    std::uint32_t rate_denominator_{};
    std::uint32_t maximum_step_{};
    bool has_exact_epoch_{};
    std::uint64_t exact_buffer_instance_id_{};
    std::uint64_t exact_playback_generation_{};
    std::uint64_t last_exact_source_frame_{};
};

[[nodiscard]] std::expected<std::uint32_t, FramerateProfileError>
CountCrossedAuthored60Ticks(
    const FramerateProfile& profile,
    std::uint32_t current_tick,
    std::uint32_t step) noexcept;

[[nodiscard]] std::expected<bool, FramerateProfileError>
CrossesAuthored60Cadence(
    const FramerateProfile& profile,
    std::uint32_t current_tick,
    std::uint32_t step,
    std::int32_t phase,
    std::uint32_t authored_period) noexcept;

} // namespace gc::framerate

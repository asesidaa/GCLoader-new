#include "Patches/Framerate/FramerateAuthoredClock.h"

#include <cstdint>
#include <limits>

namespace gc::framerate {

Authored60PhaseClock::Authored60PhaseClock(
    const FramerateTimingProfile& profile) noexcept
    : target_fps_{profile.target_fps()},
      phase_{target_fps_ - 60U} {
}

bool Authored60PhaseClock::Advance() noexcept {
    phase_ += 60U;
    if (phase_ < target_fps_) {
        return false;
    }
    phase_ -= target_fps_;
    return true;
}

std::expected<bool, FramerateTimingProfileError>
IsAuthored60FrameBoundary(
    const FramerateTimingProfile& profile,
    std::uint32_t target_frame) noexcept {
    if (target_frame == 0) {
        return true;
    }

    const auto current = profile.MapToAuthored60(target_frame);
    if (!current) {
        return std::unexpected(current.error());
    }
    const auto previous = profile.MapToAuthored60(target_frame - 1);
    if (!previous) {
        return std::unexpected(previous.error());
    }
    return current.value() != previous.value();
}

std::expected<bool, FramerateTimingProfileError>
ShouldRunAuthored60Cadence(
    const FramerateTimingProfile& profile,
    std::uint32_t target_frame,
    std::int32_t phase,
    std::uint32_t authored_period) noexcept {
    if (authored_period == 0) {
        return std::unexpected(FramerateTimingProfileError::InvalidPeriod);
    }

    const auto boundary =
        IsAuthored60FrameBoundary(profile, target_frame);
    if (!boundary) {
        return std::unexpected(boundary.error());
    }
    if (!boundary.value()) {
        return false;
    }

    const auto authored = profile.MapToAuthored60(target_frame);
    if (!authored) {
        return std::unexpected(authored.error());
    }

    const auto period = static_cast<std::int64_t>(authored_period);
    auto remainder =
        (static_cast<std::int64_t>(authored.value()) + phase) % period;
    if (remainder < 0) {
        remainder += period;
    }
    return remainder == 0;
}

std::expected<std::uint32_t, FramerateTimingProfileError>
ReconstructUnsignedModuloDividend(
    std::uint32_t quotient,
    std::uint32_t remainder,
    std::uint32_t divisor) noexcept {
    if (divisor == 0 || remainder >= divisor) {
        return std::unexpected(FramerateTimingProfileError::InvalidPeriod);
    }

    const auto value = static_cast<std::uint64_t>(quotient) * divisor +
        remainder;
    if (value > std::numeric_limits<std::uint32_t>::max()) {
        return std::unexpected(FramerateTimingProfileError::ArithmeticOverflow);
    }
    return static_cast<std::uint32_t>(value);
}

std::expected<std::uint32_t, FramerateTimingProfileError>
MapPositiveTargetFrameToAuthored60(
    const FramerateTimingProfile& profile,
    std::uint32_t raw_value) noexcept {
    if (static_cast<std::int32_t>(raw_value) <= 0) {
        return raw_value;
    }
    return profile.MapToAuthored60(raw_value);
}

std::expected<std::uint32_t, FramerateTimingProfileError>
ScalePositiveDuration(
    const FramerateTimingProfile& profile,
    std::uint32_t raw_value) noexcept {
    const auto signed_value = static_cast<std::int32_t>(raw_value);
    if (signed_value <= 0) {
        return raw_value;
    }
    const auto scaled = profile.ScaleDurationFrames(signed_value);
    if (!scaled) {
        return std::unexpected(scaled.error());
    }
    return static_cast<std::uint32_t>(scaled.value());
}

std::expected<std::uint32_t, FramerateTimingProfileError>
ScaleIfblIntegerWait(
    const FramerateTimingProfile& profile,
    std::uint32_t raw_value) noexcept {
    if (raw_value <= 1U) {
        return raw_value;
    }
    return ScalePositiveDuration(profile, raw_value);
}

std::expected<std::uint32_t, FramerateTimingProfileError>
MapPlayerPositionElapsedToAuthored60(
    const FramerateTimingProfile& profile,
    std::uint32_t raw_total,
    std::uint32_t scaled_remaining) noexcept {
    const auto scaled_total = ScalePositiveDuration(profile, raw_total);
    if (!scaled_total) {
        return std::unexpected(scaled_total.error());
    }
    const std::uint32_t elapsed_target =
        scaled_total.value() - scaled_remaining;
    return MapPositiveTargetFrameToAuthored60(profile, elapsed_target);
}

} // namespace gc::framerate

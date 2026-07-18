#include "Patches/Framerate/FramerateAuthoredClock.h"

#include <cstdint>
#include <limits>

namespace gc::framerate {

std::expected<bool, FramerateProfileError>
IsAuthored60FrameBoundary(
    const FramerateProfile& profile,
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

std::expected<bool, FramerateProfileError>
ShouldRunAuthored60Cadence(
    const FramerateProfile& profile,
    std::uint32_t target_frame,
    std::int32_t phase,
    std::uint32_t authored_period) noexcept {
    if (authored_period == 0) {
        return std::unexpected(FramerateProfileError::InvalidPeriod);
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

std::expected<std::uint32_t, FramerateProfileError>
ReconstructUnsignedModuloDividend(
    std::uint32_t quotient,
    std::uint32_t remainder,
    std::uint32_t divisor) noexcept {
    if (divisor == 0 || remainder >= divisor) {
        return std::unexpected(FramerateProfileError::InvalidPeriod);
    }

    const auto value = static_cast<std::uint64_t>(quotient) * divisor +
        remainder;
    if (value > std::numeric_limits<std::uint32_t>::max()) {
        return std::unexpected(FramerateProfileError::ArithmeticOverflow);
    }
    return static_cast<std::uint32_t>(value);
}

std::expected<std::uint32_t, FramerateProfileError>
MapPositiveTargetFrameToAuthored60(
    const FramerateProfile& profile,
    std::uint32_t raw_value) noexcept {
    if (static_cast<std::int32_t>(raw_value) <= 0) {
        return raw_value;
    }
    return profile.MapToAuthored60(raw_value);
}

} // namespace gc::framerate

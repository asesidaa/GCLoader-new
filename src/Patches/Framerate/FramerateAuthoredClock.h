#pragma once

#include "Patches/Framerate/FramerateTimingProfile.h"

#include <cstdint>
#include <expected>

namespace gc::framerate {

class Authored60PhaseClock {
public:
    explicit Authored60PhaseClock(
        const FramerateTimingProfile& profile) noexcept;

    [[nodiscard]] bool Advance() noexcept;

private:
    std::uint32_t target_fps_{};
    std::uint32_t phase_{};
};

[[nodiscard]] std::expected<bool, FramerateTimingProfileError>
IsAuthored60FrameBoundary(
    const FramerateTimingProfile& profile,
    std::uint32_t target_frame) noexcept;

[[nodiscard]] std::expected<bool, FramerateTimingProfileError>
ShouldRunAuthored60Cadence(
    const FramerateTimingProfile& profile,
    std::uint32_t target_frame,
    std::int32_t phase,
    std::uint32_t authored_period) noexcept;

[[nodiscard]] std::expected<std::uint32_t, FramerateTimingProfileError>
ReconstructUnsignedModuloDividend(
    std::uint32_t quotient,
    std::uint32_t remainder,
    std::uint32_t divisor) noexcept;

[[nodiscard]] std::expected<std::uint32_t, FramerateTimingProfileError>
MapPositiveTargetFrameToAuthored60(
    const FramerateTimingProfile& profile,
    std::uint32_t raw_value) noexcept;

[[nodiscard]] std::expected<std::uint32_t, FramerateTimingProfileError>
ScalePositiveDuration(
    const FramerateTimingProfile& profile,
    std::uint32_t raw_value) noexcept;

[[nodiscard]] std::expected<std::uint32_t, FramerateTimingProfileError>
ScaleIfblIntegerWait(
    const FramerateTimingProfile& profile,
    std::uint32_t raw_value) noexcept;

[[nodiscard]] std::expected<std::uint32_t, FramerateTimingProfileError>
MapPlayerPositionElapsedToAuthored60(
    const FramerateTimingProfile& profile,
    std::uint32_t raw_total,
    std::uint32_t scaled_remaining) noexcept;

} // namespace gc::framerate

#pragma once

#include "Patches/Framerate/FramerateProfile.h"

#include <cstdint>
#include <expected>

namespace gc::framerate {

class Authored60PhaseClock {
public:
    explicit Authored60PhaseClock(
        const FramerateProfile& profile) noexcept;

    [[nodiscard]] bool Advance() noexcept;

private:
    std::uint32_t target_fps_{};
    std::uint32_t phase_{};
};

[[nodiscard]] std::expected<bool, FramerateProfileError>
IsAuthored60FrameBoundary(
    const FramerateProfile& profile,
    std::uint32_t target_frame) noexcept;

[[nodiscard]] std::expected<bool, FramerateProfileError>
ShouldRunAuthored60Cadence(
    const FramerateProfile& profile,
    std::uint32_t target_frame,
    std::int32_t phase,
    std::uint32_t authored_period) noexcept;

[[nodiscard]] std::expected<std::uint32_t, FramerateProfileError>
ReconstructUnsignedModuloDividend(
    std::uint32_t quotient,
    std::uint32_t remainder,
    std::uint32_t divisor) noexcept;

[[nodiscard]] std::expected<std::uint32_t, FramerateProfileError>
MapPositiveTargetFrameToAuthored60(
    const FramerateProfile& profile,
    std::uint32_t raw_value) noexcept;

[[nodiscard]] std::expected<std::uint32_t, FramerateProfileError>
ScalePositiveDuration(
    const FramerateProfile& profile,
    std::uint32_t raw_value) noexcept;

[[nodiscard]] std::expected<std::uint32_t, FramerateProfileError>
ScaleIfblIntegerWait(
    const FramerateProfile& profile,
    std::uint32_t raw_value) noexcept;

[[nodiscard]] std::expected<std::uint32_t, FramerateProfileError>
MapPlayerPositionElapsedToAuthored60(
    const FramerateProfile& profile,
    std::uint32_t raw_total,
    std::uint32_t scaled_remaining) noexcept;

} // namespace gc::framerate

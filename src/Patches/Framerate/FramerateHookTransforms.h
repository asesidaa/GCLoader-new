#pragma once

#include "Patches/Framerate/FramerateTimingProfile.h"
#include "Patches/Framerate/FramerateGameProfile.h"

#include <safetyhook.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>

namespace gc::framerate {

enum class FramerateHookTransformError {
    MemoryRead,
    ProfileConversion,
};


[[nodiscard]] std::uint32_t& FrameRegister(
    safetyhook::Context& context, FramerateRegister selected) noexcept;

void RedirectEaxToAuthoredOperand(
    safetyhook::Context& context,
    const AuthoredFrameOperand& operand) noexcept;
void RedirectEcxToAuthoredOperand(
    safetyhook::Context& context,
    const AuthoredFrameOperand& operand) noexcept;
void RedirectEdxToAuthoredOperand(
    safetyhook::Context& context,
    const AuthoredFrameOperand& operand) noexcept;

[[nodiscard]] std::expected<void, FramerateHookTransformError>
MapFrameRegisterToAuthored60(
    safetyhook::Context& context,
    const FramerateTimingProfile& profile,
    FramerateRegister source) noexcept;

[[nodiscard]] std::expected<void, FramerateHookTransformError>
ScalePlayerPositionDurationEax(
    safetyhook::Context& context,
    const FramerateTimingProfile& profile) noexcept;

[[nodiscard]] std::expected<void, FramerateHookTransformError>
MapPlayerPositionAssetFrame(
    safetyhook::Context& context,
    const FramerateTimingProfile& profile,
    const FramerateNativeLayout& layout,
    std::uint32_t remaining) noexcept;

[[nodiscard]] std::expected<void, FramerateHookTransformError>
PreparePlayerPositionDenominator(
    safetyhook::Context& context,
    const FramerateTimingProfile& profile,
    PlayerPositionDurationOperand& operand,
    std::uint32_t raw_duration) noexcept;

} // namespace gc::framerate

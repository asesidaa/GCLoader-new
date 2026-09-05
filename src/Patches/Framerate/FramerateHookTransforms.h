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


using RuntimeReadU32 = bool (*)(
    std::uintptr_t address,
    std::uint32_t& value) noexcept;

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
MapCountdownAssetFrame(
    safetyhook::Context& context,
    const FramerateTimingProfile& profile) noexcept;

[[nodiscard]] std::expected<void, FramerateHookTransformError>
ScalePlayerPositionDurationEax(
    safetyhook::Context& context,
    const FramerateTimingProfile& profile) noexcept;

[[nodiscard]] std::expected<void, FramerateHookTransformError>
MapPlayerPositionAssetFrame(
    safetyhook::Context& context,
    const FramerateTimingProfile& profile,
    const FramerateNativeLayout& layout,
    RuntimeReadU32 read_u32) noexcept;

[[nodiscard]] std::expected<void, FramerateHookTransformError>
PreparePlayerPositionDenominator(
    safetyhook::Context& context,
    const FramerateTimingProfile& profile,
    PlayerPositionDurationOperand& operand,
    const FramerateNativeLayout& layout,
    RuntimeReadU32 read_u32) noexcept;

} // namespace gc::framerate

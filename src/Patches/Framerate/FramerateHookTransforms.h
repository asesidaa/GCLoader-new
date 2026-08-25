#pragma once

#include "Patches/Framerate/FramerateProfile.h"

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

struct AuthoredFrameOperand {
    std::array<std::byte, 0x18> padding{};
    float frame_milliseconds{1000.0F / 60.0F};
};

struct PlayerPositionDurationOperand {
    std::array<std::byte, 0xC4> padding{};
    std::int32_t duration_frames{};
};

static_assert(offsetof(AuthoredFrameOperand, frame_milliseconds) == 0x18);
static_assert(offsetof(PlayerPositionDurationOperand, duration_frames) == 0xC4);

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
    const FramerateProfile& profile) noexcept;

[[nodiscard]] std::expected<void, FramerateHookTransformError>
ScalePlayerPositionDurationEax(
    safetyhook::Context& context,
    const FramerateProfile& profile) noexcept;

[[nodiscard]] std::expected<void, FramerateHookTransformError>
MapPlayerPositionAssetFrame(
    safetyhook::Context& context,
    const FramerateProfile& profile,
    RuntimeReadU32 read_u32) noexcept;

[[nodiscard]] std::expected<void, FramerateHookTransformError>
PreparePlayerPositionDenominator(
    safetyhook::Context& context,
    const FramerateProfile& profile,
    PlayerPositionDurationOperand& operand,
    RuntimeReadU32 read_u32) noexcept;

} // namespace gc::framerate

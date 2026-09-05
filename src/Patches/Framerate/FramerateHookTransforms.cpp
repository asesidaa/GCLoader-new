#include "Patches/Framerate/FramerateHookTransforms.h"
#include "Patches/Framerate/FramerateAuthoredClock.h"

#include <bit>
#include <cstdint>

namespace gc::framerate {

namespace {

std::uint32_t OperandAddress(const void* operand) noexcept {
    static_assert(sizeof(void*) == sizeof(std::uint32_t));
    return static_cast<std::uint32_t>(
        reinterpret_cast<std::uintptr_t>(operand));
}

} // namespace

void RedirectEaxToAuthoredOperand(
    safetyhook::Context& context,
    const AuthoredFrameOperand& operand) noexcept {
    context.eax = OperandAddress(&operand);
}

void RedirectEcxToAuthoredOperand(
    safetyhook::Context& context,
    const AuthoredFrameOperand& operand) noexcept {
    context.ecx = OperandAddress(&operand);
}

void RedirectEdxToAuthoredOperand(
    safetyhook::Context& context,
    const AuthoredFrameOperand& operand) noexcept {
    context.edx = OperandAddress(&operand);
}

std::expected<void, FramerateHookTransformError>
MapCountdownAssetFrame(
    safetyhook::Context& context,
    const FramerateTimingProfile& profile) noexcept {
    const auto mapped = MapPositiveTargetFrameToAuthored60(
        profile, context.ecx);
    if (!mapped) {
        return std::unexpected(
            FramerateHookTransformError::ProfileConversion);
    }
    context.ecx = mapped.value();
    return {};
}

std::expected<void, FramerateHookTransformError>
ScalePlayerPositionDurationEax(
    safetyhook::Context& context,
    const FramerateTimingProfile& profile) noexcept {
    const auto scaled = ScalePositiveDuration(profile, context.eax);
    if (!scaled) {
        return std::unexpected(
            FramerateHookTransformError::ProfileConversion);
    }
    context.eax = scaled.value();
    return {};
}

std::expected<void, FramerateHookTransformError>
MapPlayerPositionAssetFrame(
    safetyhook::Context& context,
    const FramerateTimingProfile& profile,
    const FramerateNativeLayout& layout,
    RuntimeReadU32 read_u32) noexcept {
    std::uint32_t remaining{};
    const std::uint32_t address =
        context.edx + context.ecx * 4U + layout.player_position_remaining;
    if (read_u32 == nullptr || !read_u32(address, remaining)) {
        return std::unexpected(FramerateHookTransformError::MemoryRead);
    }
    const auto mapped = MapPlayerPositionElapsedToAuthored60(
        profile, context.eax, remaining);
    if (!mapped) {
        return std::unexpected(
            FramerateHookTransformError::ProfileConversion);
    }
    context.eax = mapped.value();
    context.eip += layout.player_position_skip;
    return {};
}

std::expected<void, FramerateHookTransformError>
PreparePlayerPositionDenominator(
    safetyhook::Context& context,
    const FramerateTimingProfile& profile,
    PlayerPositionDurationOperand& operand,
    const FramerateNativeLayout& layout,
    RuntimeReadU32 read_u32) noexcept {
    std::uint32_t raw_duration{};
    const std::uint32_t address = context.eax + layout.player_position_duration;
    if (read_u32 == nullptr || !read_u32(address, raw_duration)) {
        return std::unexpected(FramerateHookTransformError::MemoryRead);
    }
    const auto scaled = ScalePositiveDuration(profile, raw_duration);
    if (!scaled) {
        return std::unexpected(
            FramerateHookTransformError::ProfileConversion);
    }
    const auto duration = std::bit_cast<std::int32_t>(scaled.value());
    operand.duration_frames = duration;
    context.eax = OperandAddress(&operand);
    return {};
}

} // namespace gc::framerate

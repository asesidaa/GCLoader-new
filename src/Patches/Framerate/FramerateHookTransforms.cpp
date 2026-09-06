#include "Patches/Framerate/FramerateHookTransforms.h"
#include "Patches/Framerate/FramerateAuthoredClock.h"

#include <bit>
#include <cstdint>
#include <utility>

namespace gc::framerate {

namespace {

std::uint32_t OperandAddress(const void* operand) noexcept {
    static_assert(sizeof(void*) == sizeof(std::uint32_t));
    return static_cast<std::uint32_t>(
        reinterpret_cast<std::uintptr_t>(operand));
}

} // namespace

std::uint32_t& FrameRegister(
    safetyhook::Context& context, FramerateRegister selected) noexcept {
    switch (selected) {
    case FramerateRegister::eax: return context.eax;
    case FramerateRegister::ecx: return context.ecx;
    case FramerateRegister::edx: return context.edx;
    }
    std::unreachable();
}

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
MapFrameRegisterToAuthored60(
    safetyhook::Context& context,
    const FramerateTimingProfile& profile,
    FramerateRegister source) noexcept {
    auto& value = FrameRegister(context, source);
    const auto mapped = MapPositiveTargetFrameToAuthored60(
        profile, value);
    if (!mapped) {
        return std::unexpected(
            FramerateHookTransformError::ProfileConversion);
    }
    value = mapped.value();
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
    std::uint32_t remaining) noexcept {
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
    std::uint32_t raw_duration) noexcept {
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

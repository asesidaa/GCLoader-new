#include "Patches/Framerate/FramerateEffectTiming.h"
#include "Patches/Framerate/FramerateAuthoredClock.h"
namespace gc::framerate {
std::expected<void, EffectTimingTransformError>
MapEffectFrameEaxToAuthored60(
    safetyhook::Context& context,
    const FramerateTimingProfile& profile) noexcept {
    const auto mapped =
        MapPositiveTargetFrameToAuthored60(profile, context.eax);
    if (!mapped) {
        return std::unexpected(
            EffectTimingTransformError::ProfileConversion);
    }
    context.eax = mapped.value();
    return {};
}

std::expected<void, EffectTimingTransformError>
MapEffectFrameEdxToAuthored60(
    safetyhook::Context& context,
    const FramerateTimingProfile& profile) noexcept {
    const auto mapped =
        MapPositiveTargetFrameToAuthored60(profile, context.edx);
    if (!mapped) {
        return std::unexpected(
            EffectTimingTransformError::ProfileConversion);
    }
    context.edx = mapped.value();
    return {};
}

std::expected<void, EffectTimingTransformError>
ScaleEffectDurationEaxToTarget(
    safetyhook::Context& context,
    const FramerateTimingProfile& profile) noexcept {
    const auto scaled = ScalePositiveDuration(profile, context.eax);
    if (!scaled) {
        return std::unexpected(
            EffectTimingTransformError::ProfileConversion);
    }
    context.eax = scaled.value();
    return {};
}

} // namespace gc::framerate

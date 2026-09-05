#include "Patches/Framerate/FrameratePatchPlan.h"
#include <bit>
#include <limits>
namespace gc::framerate {
namespace {
bool IsLegacyAudioHook(FramerateHookId id) noexcept {
    return id == FramerateHookId::AudioSkipMargin ||
        id == FramerateHookId::AudioSkipInterval ||
        id == FramerateHookId::AudioResyncPolicy;
}

bool IsSharedGameplayConsumer(FramerateHookId id) noexcept {
    switch (id) {
    case FramerateHookId::GameplayEffectAdvance:
    case FramerateHookId::EffectCadence6:
    case FramerateHookId::EffectCadence5:
    case FramerateHookId::EffectCadence4:
    case FramerateHookId::EffectCadence16A:
    case FramerateHookId::EffectCadence16B:
    case FramerateHookId::EffectCadence8:
        return true;
    default:
        return false;
    }
}


}
std::expected<FramerateDirectPatchPlan, FrameratePatchPlanError>
BuildFramerateDirectPatchPlan(const FramerateGameProfile& game_profile,
    const FramerateTimingProfile& profile, std::uint64_t target_fps_operand) noexcept {
    if (target_fps_operand == 0 || target_fps_operand > (std::numeric_limits<std::uint32_t>::max)())
        return std::unexpected(FrameratePatchPlanError::OperandAddressOutOfRange);
    FramerateDirectPatchPlan plan{};
    if (profile.native_timing()) return plan;
    const auto initial = profile.ScaleDurationFrames(16);
    const auto next = profile.ScaleDurationFrames(8);
    const auto interval = profile.ScaleDurationFrames(3);
    if (!initial || !next || !interval)
        return std::unexpected(FrameratePatchPlanError::ProfileConversion);
    plan.menu_repeat_initial = *initial;
    plan.menu_repeat_interval = *interval;
    for (const auto& write : game_profile.writes) {
        std::uint32_t value{};
        switch (write.value) {
        case FramerateWriteValue::frame_ms: value = std::bit_cast<std::uint32_t>(profile.frame_milliseconds()); break;
        case FramerateWriteValue::frame_seconds: value = std::bit_cast<std::uint32_t>(profile.frame_seconds()); break;
        case FramerateWriteValue::smoothing: value = std::bit_cast<std::uint32_t>(profile.render_smoothing_step()); break;
        case FramerateWriteValue::decay: value = std::bit_cast<std::uint32_t>(profile.render_offset_decay_step()); break;
        case FramerateWriteValue::repeat_initial:
        case FramerateWriteValue::menu_initial: value = static_cast<std::uint32_t>(*initial); break;
        case FramerateWriteValue::repeat_next: value = static_cast<std::uint32_t>(*next); break;
        case FramerateWriteValue::two_seconds: value = profile.two_second_frames(); break;
        case FramerateWriteValue::target_operand: value = static_cast<std::uint32_t>(target_fps_operand); break;
        case FramerateWriteValue::menu_interval: value = static_cast<std::uint32_t>(*interval); break;
        }
        auto replacement = write.site.original;
        if (replacement.size < sizeof(value) || replacement.size > replacement.bytes.size())
            return std::unexpected(FrameratePatchPlanError::Capacity);
        for (std::size_t index = 0; index < sizeof(value); ++index)
            replacement.bytes[replacement.size - sizeof(value) + index] =
                static_cast<std::byte>((value >> (index * 8)) & 0xFF);
        auto contract = write.site;
        contract.installed = replacement;
        plan.writes[plan.count++] = {contract, replacement, write.memory_kind};
    }
    return plan;
}
FramerateHookPlan BuildFramerateHookPlan(
    const FramerateGameProfile& game_profile,
    bool transformed_timing,
    GameplayAudioClockPlan audio_clock_plan) noexcept {
    FramerateHookPlan plan{};
    for (const auto& contract : game_profile.hooks) {
        bool selected{};
        switch (audio_clock_plan) {
        case GameplayAudioClockPlan::OriginalWatchdog:
            selected =
                contract.id == FramerateHookId::OuterFrame ||
                (transformed_timing &&
                    contract.id != FramerateHookId::AudioResyncPolicy &&
                    contract.id != FramerateHookId::GameplaySongClock);
            break;
        case GameplayAudioClockPlan::WasapiLegacyResync:
            selected =
                contract.id == FramerateHookId::OuterFrame ||
                contract.id == FramerateHookId::AudioResyncPolicy ||
                (transformed_timing &&
                    contract.id != FramerateHookId::GameplaySongClock);
            break;
        case GameplayAudioClockPlan::WasapiSharedSongClock:
        case GameplayAudioClockPlan::AsioQpcSongClock:
            selected =
                contract.id == FramerateHookId::OuterFrame ||
                contract.id == FramerateHookId::GameplaySongClock ||
                IsSharedGameplayConsumer(contract.id) ||
                (transformed_timing && !IsLegacyAudioHook(contract.id));
            break;
        }
        if (selected) {
            plan.contracts[plan.count++] = contract;
        }
    }
    return plan;
}


std::uint32_t ApplyCmp32Flags(
    std::uint32_t existing,
    std::uint32_t left,
    std::uint32_t right) noexcept {
    constexpr std::uint32_t kCarry = 0x001;
    constexpr std::uint32_t kParity = 0x004;
    constexpr std::uint32_t kAuxiliary = 0x010;
    constexpr std::uint32_t kZero = 0x040;
    constexpr std::uint32_t kSign = 0x080;
    constexpr std::uint32_t kOverflow = 0x800;
    constexpr std::uint32_t kMask =
        kCarry | kParity | kAuxiliary | kZero | kSign | kOverflow;

    const std::uint32_t result = left - right;
    const bool even_parity =
        (std::popcount(result & 0xFFU) & 1U) == 0;
    std::uint32_t flags = existing & ~kMask;
    flags |= left < right ? kCarry : 0;
    flags |= even_parity ? kParity : 0;
    flags |= ((left ^ right ^ result) & 0x10U) != 0 ? kAuxiliary : 0;
    flags |= result == 0 ? kZero : 0;
    flags |= (result & 0x80000000U) != 0 ? kSign : 0;
    flags |= ((left ^ right) & (left ^ result) & 0x80000000U) != 0
        ? kOverflow
        : 0;
    return flags;
}

} // namespace gc::framerate

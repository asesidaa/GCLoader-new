#pragma once

#include "Patches/Framerate/FrameratePatchTransaction.h"
#include "Patches/Framerate/FramerateProfile.h"

#include <array>
#include <cstdint>
#include <expected>
#include <span>

namespace gc::framerate {

enum class FrameratePatchPlanError {
    ProfileConversion,
    OperandAddressOutOfRange,
    UnexpectedImageBase,
    Capacity,
};

struct FramerateDirectPatchPlan {
    std::array<CheckedWrite, 17> writes{};
    std::size_t count{};
    std::int32_t menu_repeat_initial{16};
    std::int32_t menu_repeat_interval{3};

    [[nodiscard]] std::span<const CheckedWrite> view() const noexcept {
        return {writes.data(), count};
    }
};

enum class FramerateHookId {
    MovieClipGoto,
    MovieClipAdvance,
    PaletteCompare,
    StageClipFrame,
    IfblWait,
    StageBgmPreload,
    TuneCountdownCompare,
    AudioSkipMargin,
    AudioSkipInterval,
    AudioResyncDiagnostic,
    GameplayEffectAdvance,
    EffectCadence6,
    EffectCadence5,
    EffectCadence4,
    EffectCadence16A,
    EffectCadence16B,
    EffectCadence8,
    RemoteCadenceA,
    RemoteCadenceB,
    GameplayBlink,
    GreatGoodLifetimeOperand,
    GreatGoodFrameOperand,
    EffectLifetimeAOperand,
    EffectFrameAOperand,
    EffectLifetimeBOperand,
    EffectFrameBOperand,
    DirectEffectFrameOperand,
    ChartEffectFrameAOperand,
    ChartEffectFrameBOperand,
    ChartEffectFrameCOperand,
    ChartEffectFrameDOperand,
    FixedVisualFrameOperand,
    GameplayCountdownAssetFrame,
    OuterFrame,
};

struct FramerateHookContract {
    FramerateHookId id{};
    std::uintptr_t rva{};
    BytePattern expected{};
    const char* name{};
};

[[nodiscard]] std::expected<
    FramerateDirectPatchPlan,
    FrameratePatchPlanError>
BuildFramerateDirectPatchPlan(
    std::uintptr_t executable_base,
    const FramerateProfile& profile,
    std::uint64_t target_fps_operand) noexcept;

[[nodiscard]] std::span<const FramerateHookContract>
FramerateHookContracts(bool transformed_timing) noexcept;

[[nodiscard]] std::uint32_t ApplyCmp32Flags(
    std::uint32_t existing_eflags,
    std::uint32_t left,
    std::uint32_t right) noexcept;

} // namespace gc::framerate

#pragma once
#include "Patches/Framerate/FramerateGameProfile.h"
#include "Patches/Framerate/FramerateTimingProfile.h"

namespace gc::framerate {
enum class FrameratePatchPlanError { ProfileConversion, OperandAddressOutOfRange, Capacity };
struct FramerateDirectPatchPlan final {
    std::array<game_version::BytePatchOperation, 17> writes{};
    std::size_t count{};
    std::int32_t menu_repeat_initial{16};
    std::int32_t menu_repeat_interval{3};
    [[nodiscard]] std::span<const game_version::BytePatchOperation> view() const noexcept {
        return {writes.data(), count};
    }
};
struct FramerateHookPlan final {
    std::array<FramerateHookContract, 53> contracts{};
    std::size_t count{};
    [[nodiscard]] std::span<const FramerateHookContract> view() const noexcept {
        return {contracts.data(), count};
    }
};
// Owns the dynamic replacement bytes until VersionedPlanSet copies them.
struct PreparedFrameratePlan final {
    std::array<game_version::VersionedOperation, 80> operations{};
    std::size_t count{};
    [[nodiscard]] game_version::FeaturePlan feature_plan() const noexcept {
        return {game_version::FeatureId::framerate, {operations.data(), count}, {}};
    }
};
[[nodiscard]] std::expected<FramerateDirectPatchPlan, FrameratePatchPlanError>
BuildFramerateDirectPatchPlan(const FramerateGameProfile&, const FramerateTimingProfile&,
    std::uint64_t target_fps_operand) noexcept;
[[nodiscard]] FramerateHookPlan BuildFramerateHookPlan(
    const FramerateGameProfile&, bool transformed_timing, GameplayAudioClockPlan) noexcept;
[[nodiscard]] std::uint32_t ApplyCmp32Flags(
    std::uint32_t existing_eflags, std::uint32_t left, std::uint32_t right) noexcept;
}

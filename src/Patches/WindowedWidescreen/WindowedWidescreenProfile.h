#pragma once
#include "Patches/WindowedWidescreen/WindowedWidescreenAbi.h"
#include <span>
namespace gc::windowed_widescreen {
// Native carriers and exact CTune effect ownership for selected feedback.
using NativeBatchCounts = std::array<std::uint32_t, 4>;
using NativeNetworkMatrix = std::array<float, 16>;
inline constexpr std::array<std::size_t, 3> kNativeMatrixHorizontalComponents{0, 4, 8};
inline constexpr std::size_t kNativeMatrixTranslation = 12;
// 648D40: primary score display, current/previous successful grade (1..3).
// Slot = 9 + 3 * grade + 12 * history; these are separate from track effects.
inline constexpr std::array<std::uint32_t, 6> kPlayerOneJudgementTextSlots{
    12, 15, 18, 24, 27, 30};
inline constexpr std::array<std::uint32_t, 9> kNoteTutorialSlots{
    0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB9, 0xBA, 0xBB, 0xC0};
    struct NativeViewport
    {
        float x{};
        float y{};
        float width{};
        float height{};
    };
    struct HudOrthographicArguments
    {
        float left{};
        float right{};
        float bottom{};
        float top{};
        float near_plane{};
        float far_plane{};
    };
struct WidescreenByteContract final {
    WidescreenContractSite site;
    game_version::SiteContract contract;
};
struct WidescreenPointerContract final {
    WidescreenContractSite site;
    game_version::SiteContract contract;
    runtime_image::Rva target_rva{};
};
struct WindowedWidescreenProfile final {
    game_version::GameBuild build;
    game_version::GameImageVariant variant;
    std::span<const WidescreenByteContract> byte_contracts;
    std::array<WidescreenPointerContract, 9> pointer_contracts;
    std::span<const WidescreenFunctionAbi> function_abis;
    std::span<const WidescreenContractSite> hook_order;
    WidescreenNativeLayout layout;
    // Versioned policy; 2.06 retains its existing mixed-pass behavior until ported.
    bool selected_hud_draws_only{};
};
struct PreparedWidescreenPlan final {
    std::array<game_version::VersionedOperation, 100> operations;
    std::size_t count{};
    [[nodiscard]] game_version::FeaturePlan feature_plan() const noexcept {
        return {game_version::FeatureId::windowed_widescreen,
            std::span{operations}.first(count), {}};
    }
};
[[nodiscard]] const WindowedWidescreenProfile* ProfileFor(
    game_version::GameBuild, game_version::GameImageVariant) noexcept;
[[nodiscard]] std::expected<PreparedWidescreenPlan, game_version::PlanError>
BuildWidescreenPlan(game_version::GameBuild, game_version::GameImageVariant,
    bool enabled, const runtime_image::RuntimeImage&) noexcept;
namespace detail {
// Callbacks stay with rendering policy; the profile owns the native contracts.
[[nodiscard]] game_version::VersionedOperation BindWidescreenHook(
    WidescreenContractSite, const game_version::SiteContract&, void* expected = nullptr) noexcept;
}
} // namespace gc::windowed_widescreen

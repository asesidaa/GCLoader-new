#pragma once
#include "Patches/WindowedWidescreen/WindowedWidescreenAbi.h"
namespace gc::windowed_widescreen {
// Native carriers: four batch queues, a 4x4 matrix, and five player-one effect slots.
using NativeBatchCounts = std::array<std::uint32_t, 4>;
using NativeNetworkMatrix = std::array<float, 16>;
inline constexpr std::array<std::size_t, 3> kNativeMatrixHorizontalComponents{0, 4, 8};
inline constexpr std::size_t kNativeMatrixTranslation = 12;
inline constexpr std::array<std::uint32_t, 5> kPlayerOneJudgementSlots{93, 94, 95, 96, 97};
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
    std::array<WidescreenByteContract, 40> byte_contracts;
    std::array<WidescreenPointerContract, 9> pointer_contracts;
    std::array<WidescreenFunctionAbi, 36> function_abis;
    std::array<WidescreenContractSite, 36> hook_order;
    WidescreenNativeLayout layout;
};
struct PreparedWidescreenPlan final {
    std::array<game_version::VersionedOperation, 49> operations;
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

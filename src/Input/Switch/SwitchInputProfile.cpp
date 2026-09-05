#include "Input/Switch/SwitchInputProfile.h"
#include "Input/Switch/SwitchInputPatch.h"
namespace gc::switch_input {
namespace {
using namespace game_version;
using runtime_image::PatternOf;
// Parent frame is the EBP frame of RVA0x1D2E50. Native match is one byte;
// target and mapped current direction are signed32-bit logical direction IDs.
constexpr DiagonalStackLayout kDiagonal471{-0x75, -0x7C, -0x68};
void DiagonalMatch471(safetyhook::Context& context) noexcept {
    ApplySwitchDiagonalMatch(context, kDiagonal471);
}
SwitchInputProfile MakeProfile(GameImageVariant variant) noexcept {
    constexpr auto entry = PatternOf<0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x18, 0x89, 0x4D,
        0xEC, 0xC6, 0x45, 0xFF, 0x00, 0x8B, 0x4D, 0xEC>();
    // SafetyHook E9 consumption: PUSH EBP/MOV EBP,ESP/SUB ESP,18h =6;
    // diagonal MOVZX EDX,[EBP-75h]/CMP EDX,1 =7. Longer prefixes are read contracts.
    return {GameBuild::groove_coaster_471, variant, {
        InlineHookOperation{{FeatureId::switch_input, "pressed_edge",
            VersionedOperationKind::inline_hook, 0x00259640, 6, entry, {}, 0},
            reinterpret_cast<void*>(SwitchPressedEdgeDetour),
            hooking::OriginalPublisher::To(&detail::g_query_originals.pressed)},
        InlineHookOperation{{FeatureId::switch_input, "held_state",
            VersionedOperationKind::inline_hook, 0x00259570, 6, entry, {}, 1},
            reinterpret_cast<void*>(SwitchHeldStateDetour),
            hooking::OriginalPublisher::To(&detail::g_query_originals.held)},
        MidHookOperation{{FeatureId::switch_input, "diagonal_match",
            VersionedOperationKind::mid_hook, 0x001D32A0, 7,
            PatternOf<0x0F, 0xB6, 0x55, 0x8B, 0x83, 0xFA, 0x01, 0x75, 0x2B>(), {}, 2},
            DiagonalMatch471},
    }, kDiagonal471};
}
}
const SwitchInputProfile* ProfileFor(game_version::GameBuild build, game_version::GameImageVariant variant) noexcept {
    // Stable storage: FeaturePlan borrows until VersionedPlanSet copies operations.
    static const std::array profiles{
        MakeProfile(GameImageVariant::clean), MakeProfile(GameImageVariant::legacy_patched),
        MakeProfile(GameImageVariant::locally_verified)};
    for (const auto& profile : profiles)
        if (profile.build == build && profile.variant == variant) return &profile;
    return nullptr;
}
std::expected<game_version::FeaturePlan, game_version::PlanError> BuildSwitchInputPlan(
    game_version::GameBuild build, game_version::GameImageVariant variant,
    const SwitchInputSettings& settings) noexcept {
    if (settings.style() == input::GameplayInputStyle::Arcade)
        return FeaturePlan{FeatureId::switch_input, {}, {}};
    if (settings.style() == input::GameplayInputStyle::Switch)
        if (const auto* profile = ProfileFor(build, variant))
            return FeaturePlan{FeatureId::switch_input, profile->operations, {}};
    return std::unexpected(PlanError{.stage = PlanStage::unsupported_feature, .feature = FeatureId::switch_input});
}
}

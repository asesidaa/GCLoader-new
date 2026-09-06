#include "Nesys/Network/NesysPingProfile.h"
namespace gc::nesys_service {
namespace {
NesysPingProfile Make(NesysImageVariant variant) {
    using namespace game_version;
    return {NesysBuild::service_297, variant, {{
        MidHookOperation{
            {FeatureId::nesys_ping, "service_ping", VersionedOperationKind::mid_hook,
             0x8E40, 5, runtime_image::PatternOf<
                0x51, 0x53, 0x55, 0x56, 0x57, 0x50, 0x8B, 0xD9,
                0x8D, 0x6B, 0x04, 0x6A, 0x10, 0x55, 0xC7, 0x44,
                0x24, 0x1C, 0x00, 0x00, 0x00, 0x00, 0xE8, 0x02,
                0x73, 0x02, 0x00, 0x83, 0xC4, 0x0C, 0x8D, 0x73>(), {}, 0},
            &OnServicePingAddress}
    }}};
}
NesysPingProfile Make2861(NesysImageVariant variant) {
    using namespace game_version;
    return {NesysBuild::service_2861, variant, {{
        MidHookOperation{
            {FeatureId::nesys_ping, "service_ping", VersionedOperationKind::mid_hook,
             0x8E20, 5, runtime_image::PatternOf<
                0x51, 0x53, 0x55, 0x56, 0x57, 0x50, 0x8B, 0xD9,
                0x8D, 0x6B, 0x04, 0x6A, 0x10, 0x55, 0xC7, 0x44,
                0x24, 0x1C, 0x00, 0x00, 0x00, 0x00, 0xE8, 0x02,
                0x72, 0x02, 0x00, 0x83, 0xC4, 0x0C, 0x8D, 0x73>(), {}, 0},
            &OnServicePingAddress}
    }}};
}
}
const NesysPingProfile* PingProfileFor(NesysBuild build, NesysImageVariant variant) noexcept {
    if (build == NesysBuild::service_2861) {
        static const auto original2861 = Make2861(NesysImageVariant::original);
        static const auto verified2861 = Make2861(NesysImageVariant::locally_verified);
        switch (variant) {
        case NesysImageVariant::original: return &original2861;
        case NesysImageVariant::locally_verified: return &verified2861;
        }
    }
    if (build != NesysBuild::service_297) return nullptr;
    static const auto original = Make(NesysImageVariant::original);
    static const auto verified = Make(NesysImageVariant::locally_verified);
    switch (variant) {
    case NesysImageVariant::original: return &original;
    case NesysImageVariant::locally_verified: return &verified;
    }
    return nullptr;
}
std::expected<game_version::FeaturePlan, game_version::PlanError> BuildNesysPingPlan(
    NesysBuild build, NesysImageVariant variant) noexcept {
    using namespace game_version;
    const auto* profile = PingProfileFor(build, variant);
    if (!profile) return std::unexpected(PlanError{.stage = PlanStage::unsupported_feature,
        .context = {build, variant}, .feature = FeatureId::nesys_ping, .site = "profile"});
    return FeaturePlan{FeatureId::nesys_ping, profile->operations, {}};
}
}

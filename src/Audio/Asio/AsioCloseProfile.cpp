#include "Audio/Asio/AsioCloseProfile.h"
namespace gc::audio::asio {
namespace {
AsioCloseProfile Make(game_version::GameImageVariant variant) {
    using namespace game_version;
    return {GameBuild::groove_coaster_471, variant, {{
        MidHookOperation{
            {FeatureId::asio_close, "ordinary_close", VersionedOperationKind::mid_hook,
             0x23C853, 6, runtime_image::PatternOf<
                0xFF, 0x15, 0x3C, 0xD6, 0x6A, 0x00, 0x8B, 0xE5,
                0x5D, 0xC3, 0xCC, 0xCC, 0xCC, 0x55, 0x8B, 0xEC>(), {}, 0},
            &OnOrdinaryAsioClose}
    }}};
}
// GC 2.06 native contracts; see the September 6 port audit.
AsioCloseProfile Make206(game_version::GameImageVariant variant) {
    using namespace game_version;
    return {GameBuild::groove_coaster_206, variant, {{
        MidHookOperation{
            {FeatureId::asio_close, "ordinary_close", VersionedOperationKind::mid_hook,
             0x20965E, 6, runtime_image::PatternOf<0xFF, 0x15, 0x3C, 0xE6, 0x66, 0x00, 0x8B, 0xE5, 0x5D, 0xC3, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC>(), {}, 0},
            &OnOrdinaryAsioClose}
    }}};
}
}
const AsioCloseProfile* ProfileFor(
    game_version::GameBuild build, game_version::GameImageVariant variant) noexcept {
    using namespace game_version;
    if (build == GameBuild::groove_coaster_206) {
        static const auto clean206 = Make206(GameImageVariant::clean);
        static const auto verified206 = Make206(GameImageVariant::locally_verified);
        switch (variant) {
        case GameImageVariant::clean: return &clean206;
        case GameImageVariant::locally_verified: return &verified206;
        case GameImageVariant::legacy_patched: return nullptr;
        }
    }
    if (build != GameBuild::groove_coaster_471) return nullptr;
    static const auto clean = Make(GameImageVariant::clean);
    static const auto patched = Make(GameImageVariant::legacy_patched);
    static const auto verified = Make(GameImageVariant::locally_verified);
    switch (variant) {
    case GameImageVariant::clean: return &clean;
    case GameImageVariant::legacy_patched: return &patched;
    case GameImageVariant::locally_verified: return &verified;
    }
    return nullptr;
}
std::expected<game_version::FeaturePlan, game_version::PlanError> BuildAsioClosePlan(
    game_version::GameBuild build, game_version::GameImageVariant variant) noexcept {
    using namespace game_version;
    const auto* profile = ProfileFor(build, variant);
    if (!profile) return std::unexpected(PlanError{.stage = PlanStage::unsupported_feature,
        .context = {build, variant}, .feature = FeatureId::asio_close, .site = "profile"});
    return FeaturePlan{FeatureId::asio_close, profile->operations, {}};
}
}

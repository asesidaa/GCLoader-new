#include "Patches/AutoPlay/AutoPlayProfile.h"
#include "Patches/AutoPlay/AutoPlayPatch.h"
#include <array>
namespace gc::auto_play {
namespace {
using namespace game_version;
using runtime_image::PatternOf;
// Both known files have original AutoPlay bytes. The marker seam precedes
// render-subscriber dispatch and replays the native LEA/PUSH/CALL sequence.
// The independent +0xA7 state getter/setter is deliberately absent.
constexpr std::array<VersionedOperation, 5> kOperations{
    ReadOnlyContractOperation{{FeatureId::auto_play, "native_debug_text",
        VersionedOperationKind::read_only_contract, 0x00069650, 5,
        PatternOf<0x55, 0x8B, 0xEC, 0x6A, 0xFF>(), {}, 0, SiteDisposition::verify_only}},
    MidHookOperation{{FeatureId::auto_play, "marker_seam",
        VersionedOperationKind::mid_hook, 0x00058BE9, 5,
        PatternOf<0x8D, 0x44, 0x24, 0x08, 0x50, 0xE8, 0x8D, 0x03, 0x00, 0x00>(),
        {}, 1}, AutoPlayMarkerMidHook},
    BytePatchOperation{{FeatureId::auto_play, "do_not_save_card_data",
        VersionedOperationKind::byte_patch, 0x00269951, 3,
        PatternOf<0x0F, 0x95, 0xC1>(), PatternOf<0xB1, 0x01, 0x90>(), 2},
        PatternOf<0xB1, 0x01, 0x90>()},
    BytePatchOperation{{FeatureId::auto_play, "complete_is_mute",
        VersionedOperationKind::byte_patch, 0x0003CAFA, 6,
        PatternOf<0x8A, 0x80, 0xA6, 0x00, 0x00, 0x00>(),
        PatternOf<0xB0, 0x01, 0x90, 0x90, 0x90, 0x90>(), 3},
        PatternOf<0xB0, 0x01, 0x90, 0x90, 0x90, 0x90>()},
    BytePatchOperation{{FeatureId::auto_play, "native_auto_play",
        VersionedOperationKind::byte_patch, 0x0003CADA, 6,
        PatternOf<0x8A, 0x80, 0xA5, 0x00, 0x00, 0x00>(),
        PatternOf<0xB0, 0x01, 0x90, 0x90, 0x90, 0x90>(), 4},
        PatternOf<0xB0, 0x01, 0x90, 0x90, 0x90, 0x90>()},
};
// 2.06 has no expconfig no-save flag. State zero instead enters native
// post-save state 13 before card request allocation; completion still runs.
constexpr std::array<VersionedOperation, 5> kOperations206{
    ReadOnlyContractOperation{{FeatureId::auto_play, "native_debug_text",
        VersionedOperationKind::read_only_contract, 0x0005ABF0, 5,
        PatternOf<0x55, 0x8B, 0xEC, 0x6A, 0xFF>(), {}, 0, SiteDisposition::verify_only}},
    MidHookOperation{{FeatureId::auto_play, "marker_seam",
        VersionedOperationKind::mid_hook, 0x00049FB9, 5,
        PatternOf<0x8D, 0x44, 0x24, 0x08, 0x50, 0xE8, 0x7D, 0x03, 0x00, 0x00>(),
        {}, 1}, AutoPlayMarkerMidHook},
    BytePatchOperation{{FeatureId::auto_play, "suppress_card_save",
        VersionedOperationKind::byte_patch, 0x001EF52A, 2,
        PatternOf<0x74, 0x0F>(), PatternOf<0x90, 0x90>(), 2},
        PatternOf<0x90, 0x90>()},
    BytePatchOperation{{FeatureId::auto_play, "complete_is_mute",
        VersionedOperationKind::byte_patch, 0x00030B1A, 6,
        PatternOf<0x8A, 0x80, 0xA6, 0x00, 0x00, 0x00>(),
        PatternOf<0xB0, 0x01, 0x90, 0x90, 0x90, 0x90>(), 3},
        PatternOf<0xB0, 0x01, 0x90, 0x90, 0x90, 0x90>()},
    BytePatchOperation{{FeatureId::auto_play, "native_auto_play",
        VersionedOperationKind::byte_patch, 0x00030AFA, 6,
        PatternOf<0x8A, 0x80, 0xA5, 0x00, 0x00, 0x00>(),
        PatternOf<0xB0, 0x01, 0x90, 0x90, 0x90, 0x90>(), 4},
        PatternOf<0xB0, 0x01, 0x90, 0x90, 0x90, 0x90>()},
};
}
std::expected<game_version::FeaturePlan, game_version::PlanError> BuildAutoPlayPlan(
    game_version::GameBuild build, game_version::GameImageVariant variant, bool enabled) noexcept {
    if (!enabled) return FeaturePlan{FeatureId::auto_play, {}, {}};
    if (build == GameBuild::groove_coaster_471 &&
        (variant == GameImageVariant::clean || variant == GameImageVariant::legacy_patched ||
         variant == GameImageVariant::locally_verified))
        return FeaturePlan{FeatureId::auto_play, kOperations, {}};
    if (build == GameBuild::groove_coaster_206 &&
        (variant == GameImageVariant::clean || variant == GameImageVariant::locally_verified))
        return FeaturePlan{FeatureId::auto_play, kOperations206, {}};
    return std::unexpected(PlanError{.stage = PlanStage::unsupported_feature, .feature = FeatureId::auto_play});
}
}

#include "Patches/SongUnlock/SongUnlockProfile.h"
#include <array>
namespace gc::song_unlock {
namespace {
using namespace game_version;
using runtime_image::PatternOf;
// The nonzero flag at [accessor(0x1260)+0x8C] selects RVA0x257A77.
// Both JNZ and replacement JMP target that same branch, which sets the cap
// to15 and skips the three-difficulty clear-state loop. Later checks remain.
constexpr std::array<VersionedOperation, 1> kOperations{
    BytePatchOperation{{FeatureId::song_unlock, "availability_branch",
        VersionedOperationKind::byte_patch, 0x00257854, 6,
        PatternOf<0x0F, 0x85, 0x1D, 0x02, 0x00, 0x00>(),
        PatternOf<0xE9, 0x1E, 0x02, 0x00, 0x00, 0x90>(), 0},
        PatternOf<0xE9, 0x1E, 0x02, 0x00, 0x00, 0x90>()},
};
}
std::expected<game_version::FeaturePlan, game_version::PlanError> BuildSongUnlockPlan(
    game_version::GameBuild build, game_version::GameImageVariant variant, bool enabled) noexcept {
    if (!enabled) return FeaturePlan{FeatureId::song_unlock, {}, {}};
    if (build == GameBuild::groove_coaster_471 &&
        (variant == GameImageVariant::clean || variant == GameImageVariant::legacy_patched ||
         variant == GameImageVariant::locally_verified))
        return FeaturePlan{FeatureId::song_unlock, kOperations, {}};
    return std::unexpected(PlanError{.stage = PlanStage::unsupported_feature, .feature = FeatureId::song_unlock});
}
}

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
// 2.06 separates its song list from EXTRA availability consumers. Keep the
// native song-type/chart-reference checks, and bypass eligibility/progression
// only at availability decisions. Shared clear-record getters and reward
// calculations must retain their original behavior.
// Native control-flow evidence: docs/reverse-engineering/gc206-complete-song-unlock-2026-09-06.md.
constexpr std::array<VersionedOperation, 13> kOperations206{
    BytePatchOperation{{FeatureId::song_unlock, "availability_branch",
        VersionedOperationKind::byte_patch, 0x00223F10, 6,
        PatternOf<0x0F, 0x85, 0xCE, 0x01, 0x00, 0x00>(),
        PatternOf<0xE9, 0xCF, 0x01, 0x00, 0x00, 0x90>(), 0},
        PatternOf<0xE9, 0xCF, 0x01, 0x00, 0x00, 0x90>()},
    // Song focus panels and visible-list badges: preserve native drawing and
    // record display, but select EXTRA state 2 whenever its chart is defined.
    BytePatchOperation{{FeatureId::song_unlock, "other_focus_eligibility",
        VersionedOperationKind::byte_patch, 0x0018C9D8, 2,
        PatternOf<0x74, 0x71>(), PatternOf<0x90, 0x90>(), 1},
        PatternOf<0x90, 0x90>()},
    BytePatchOperation{{FeatureId::song_unlock, "other_focus_available",
        VersionedOperationKind::byte_patch, 0x0018CA03, 2,
        PatternOf<0x75, 0x24>(), PatternOf<0xEB, 0x34>(), 2},
        PatternOf<0xEB, 0x34>()},
    BytePatchOperation{{FeatureId::song_unlock, "list_badge_available",
        VersionedOperationKind::byte_patch, 0x0018D7D3, 2,
        PatternOf<0x74, 0x48>(), PatternOf<0xEB, 0x36>(), 3},
        PatternOf<0xEB, 0x36>()},
    BytePatchOperation{{FeatureId::song_unlock, "selected_focus_eligibility",
        VersionedOperationKind::byte_patch, 0x0018D9B8, 2,
        PatternOf<0x74, 0x5C>(), PatternOf<0x90, 0x90>(), 4},
        PatternOf<0x90, 0x90>()},
    BytePatchOperation{{FeatureId::song_unlock, "selected_focus_available",
        VersionedOperationKind::byte_patch, 0x0018D9DE, 2,
        PatternOf<0x75, 0x2D>(), PatternOf<0xEB, 0x24>(), 5},
        PatternOf<0xEB, 0x24>()},
    // Carry the same availability into local/shared song selection state.
    BytePatchOperation{{FeatureId::song_unlock, "selection_state_available",
        VersionedOperationKind::byte_patch, 0x0018F169, 2,
        PatternOf<0x74, 0x3D>(), PatternOf<0xEB, 0x36>(), 6},
        PatternOf<0xEB, 0x36>()},
    // Random candidates retain category, difficulty and rating-range filters.
    // Both difficulty and score sorts include the newly available EXTRA chart.
    BytePatchOperation{{FeatureId::song_unlock, "random_candidate_available",
        VersionedOperationKind::byte_patch, 0x00190B12, 2,
        PatternOf<0x75, 0x04>(), PatternOf<0xEB, 0x50>(), 7},
        PatternOf<0xEB, 0x50>()},
    BytePatchOperation{{FeatureId::song_unlock, "difficulty_sort_available",
        VersionedOperationKind::byte_patch, 0x00191043, 2,
        PatternOf<0x75, 0x04>(), PatternOf<0xEB, 0x3C>(), 8},
        PatternOf<0xEB, 0x3C>()},
    BytePatchOperation{{FeatureId::song_unlock, "score_sort_available",
        VersionedOperationKind::byte_patch, 0x00191132, 2,
        PatternOf<0x75, 0x06>(), PatternOf<0xEB, 0x40>(), 9},
        PatternOf<0xEB, 0x40>()},
    // Native course randomization, difficulty input and unlock track previews.
    // The difficulty screen keeps its separate nonempty chart-reference check.
    BytePatchOperation{{FeatureId::song_unlock, "course_random_available",
        VersionedOperationKind::byte_patch, 0x001987E6, 2,
        PatternOf<0x74, 0x04>(), PatternOf<0x90, 0x90>(), 10},
        PatternOf<0x90, 0x90>()},
    BytePatchOperation{{FeatureId::song_unlock, "difficulty_screen_available",
        VersionedOperationKind::byte_patch, 0x0019AC29, 2,
        PatternOf<0x74, 0x33>(), PatternOf<0x90, 0x90>(), 11},
        PatternOf<0x90, 0x90>()},
    BytePatchOperation{{FeatureId::song_unlock, "unlock_preview_available",
        VersionedOperationKind::byte_patch, 0x001D423E, 2,
        PatternOf<0x74, 0x1D>(), PatternOf<0x90, 0x90>(), 12},
        PatternOf<0x90, 0x90>()},
};
}
std::expected<game_version::FeaturePlan, game_version::PlanError> BuildSongUnlockPlan(
    game_version::GameBuild build, game_version::GameImageVariant variant, bool enabled) noexcept {
    if (!enabled) return FeaturePlan{FeatureId::song_unlock, {}, {}};
    if (build == GameBuild::groove_coaster_471 &&
        (variant == GameImageVariant::clean || variant == GameImageVariant::legacy_patched ||
         variant == GameImageVariant::locally_verified))
        return FeaturePlan{FeatureId::song_unlock, kOperations, {}};
    if (build == GameBuild::groove_coaster_206 &&
        (variant == GameImageVariant::clean || variant == GameImageVariant::locally_verified))
        return FeaturePlan{FeatureId::song_unlock, kOperations206, {}};
    return std::unexpected(PlanError{.stage = PlanStage::unsupported_feature, .feature = FeatureId::song_unlock});
}
}

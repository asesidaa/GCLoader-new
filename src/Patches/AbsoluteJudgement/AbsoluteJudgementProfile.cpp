#include "Patches/AbsoluteJudgement/AbsoluteJudgementProfile.h"
#include "Patches/AbsoluteJudgement/AbsoluteJudgementPatch.h"

namespace gc::absolute_judgement {
namespace {
using namespace game_version;
using runtime_image::PatternOf;
AbsoluteJudgementProfile MakeProfile(GameImageVariant variant) noexcept {
    const std::array<VersionedOperation, 18> enabled{
        InlineHookOperation{{FeatureId::absolute_judgement, "pressed",
            VersionedOperationKind::inline_hook, 0x22dfb0, 6,
            PatternOf<0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x28, 0x89, 0x4D, 0xD8, 0xC6, 0x45, 0xFF, 0x00, 0x8B, 0x4D, 0xD8>(), {}, 0},
            reinterpret_cast<void*>(HookPressed),
            hooking::OriginalPublisher::To(&detail::g_originals.pressed)},
        InlineHookOperation{{FeatureId::absolute_judgement, "held",
            VersionedOperationKind::inline_hook, 0x22df50, 6,
            PatternOf<0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x0C, 0x89, 0x4D, 0xF4, 0xC6, 0x45, 0xFF, 0x00, 0x8B, 0x4D, 0xF4>(), {}, 1},
            reinterpret_cast<void*>(HookHeld),
            hooking::OriginalPublisher::To(&detail::g_originals.held)},
        InlineHookOperation{{FeatureId::absolute_judgement, "released",
            VersionedOperationKind::inline_hook, 0x22dd30, 6,
            PatternOf<0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x28, 0x89, 0x4D, 0xD8, 0xC6, 0x45, 0xFF, 0x00, 0x8B, 0x4D, 0xD8>(), {}, 2},
            reinterpret_cast<void*>(HookReleased),
            hooking::OriginalPublisher::To(&detail::g_originals.released)},
        InlineHookOperation{{FeatureId::absolute_judgement, "direction",
            VersionedOperationKind::inline_hook, 0x22e480, 6,
            PatternOf<0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x08, 0x89, 0x4D, 0xF8, 0x8B, 0x45, 0x0C, 0xD9, 0xEE, 0xD9, 0x18>(), {}, 3},
            reinterpret_cast<void*>(HookDirection),
            hooking::OriginalPublisher::To(&detail::g_originals.direction)},
        InlineHookOperation{{FeatureId::absolute_judgement, "held_age",
            VersionedOperationKind::inline_hook, 0x22daa0, 6,
            PatternOf<0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x08, 0x89, 0x4D, 0xF8, 0xC7, 0x45, 0xFC, 0x00, 0x00, 0x00, 0x00>(), {}, 4},
            reinterpret_cast<void*>(HookHeldAge),
            hooking::OriginalPublisher::To(&detail::g_originals.held_age)},
        MidHookOperation{{FeatureId::absolute_judgement, "loop_guard",
            VersionedOperationKind::mid_hook, 0x240239, 6,
            PatternOf<0x0F, 0x8E, 0x91, 0x00, 0x00, 0x00>(), {}, 5}, HookLoopGuard},
        MidHookOperation{{FeatureId::absolute_judgement, "semantic_stage_exit",
            VersionedOperationKind::mid_hook, 0x264d9a, 6,
            PatternOf<0x8B, 0x95, 0x4C, 0xFD, 0xFF, 0xFF, 0xC7, 0x42, 0x04, 0x13, 0x00, 0x00, 0x00>(), {}, 6}, HookSemanticStageExit},
        MidHookOperation{{FeatureId::absolute_judgement, "gameplay_initialization",
            VersionedOperationKind::mid_hook, 0x26251c, 8,
            PatternOf<0x89, 0x4D, 0x80, 0xE8, 0x2C, 0x60, 0xF0, 0xFF>(), {}, 7}, HookGameplayInitialization},
        MidHookOperation{{FeatureId::absolute_judgement, "semantic_stage_entry",
            VersionedOperationKind::mid_hook, 0x2641cc, 6,
            PatternOf<0x8B, 0x8D, 0x4C, 0xFD, 0xFF, 0xFF, 0xC7, 0x41, 0x10, 0x00, 0x00, 0x00, 0x00>(), {}, 8}, HookSemanticStageEntry},
        InlineHookOperation{{FeatureId::absolute_judgement, "timing_grade",
            VersionedOperationKind::inline_hook, 0x1d0e00, 6,
            PatternOf<0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x4C, 0x89, 0x4D, 0xCC, 0x8B, 0x45, 0x08, 0xD9, 0x80, 0xB0, 0x00, 0x00, 0x00>(), {}, 9},
            reinterpret_cast<void*>(HookTimingGrade),
            hooking::OriginalPublisher::To(&detail::g_originals.timing_grade)},
        ReadOnlyContractOperation{{FeatureId::absolute_judgement, "loop_tail",
            VersionedOperationKind::read_only_contract, 0x2402d0, 15,
            PatternOf<0x8B, 0x4D, 0xF8, 0x51, 0x8B, 0x8D, 0xD4, 0xFC, 0xFF, 0xFF, 0xE8, 0x01, 0x7E, 0xDF, 0xFF>(), {}, 0,
            SiteDisposition::verify_only}},
        ReadOnlyContractOperation{{FeatureId::absolute_judgement, "recognition",
            VersionedOperationKind::read_only_contract, 0x1d68e0, 16,
            PatternOf<0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68, 0x31, 0xA6, 0x67, 0x00, 0x64, 0xA1, 0x00, 0x00, 0x00, 0x00>(), {}, 0,
            SiteDisposition::verify_only}},
        ReadOnlyContractOperation{{FeatureId::absolute_judgement, "score",
            VersionedOperationKind::read_only_contract, 0x1cf930, 12,
            PatternOf<0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x0C, 0x89, 0x4D, 0xF4, 0x8B, 0x45, 0xF4>(), {}, 0,
            SiteDisposition::verify_only}},
        ReadOnlyContractOperation{{FeatureId::absolute_judgement, "get_input_manager",
            VersionedOperationKind::read_only_contract, 0x1040, 16,
            PatternOf<0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68, 0x8E, 0xD6, 0x67, 0x00, 0x64, 0xA1, 0x00, 0x00, 0x00, 0x00>(), {}, 0,
            SiteDisposition::verify_only}},
        ReadOnlyContractOperation{{FeatureId::absolute_judgement, "get_global",
            VersionedOperationKind::read_only_contract, 0x11d0, 12,
            PatternOf<0x55, 0x8B, 0xEC, 0xE8, 0x18, 0xFF, 0xFF, 0xFF, 0x5D, 0xC3, 0xCC, 0xCC>(), {}, 0,
            SiteDisposition::verify_only}},
        ReadOnlyContractOperation{{FeatureId::absolute_judgement, "get_config",
            VersionedOperationKind::read_only_contract, 0x11e0, 15,
            PatternOf<0x55, 0x8B, 0xEC, 0xE8, 0xE8, 0xFF, 0xFF, 0xFF, 0x8B, 0xC8, 0xE8, 0x81, 0xFF, 0xFF, 0xFF>(), {}, 0,
            SiteDisposition::verify_only}},
        ReadOnlyContractOperation{{FeatureId::absolute_judgement, "get_sound_manager",
            VersionedOperationKind::read_only_contract, 0x210400, 12,
            PatternOf<0x55, 0x8B, 0xEC, 0xA1, 0x9C, 0x24, 0x7F, 0x00, 0x5D, 0xC3, 0xCC, 0xCC>(), {}, 0,
            SiteDisposition::verify_only}},
        ReadOnlyContractOperation{{FeatureId::absolute_judgement, "get_group_cursor",
            VersionedOperationKind::read_only_contract, 0x2122b0, 16,
            PatternOf<0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68, 0x9B, 0x8D, 0x67, 0x00, 0x64, 0xA1, 0x00, 0x00, 0x00, 0x00>(), {}, 0,
            SiteDisposition::verify_only}}
    };
    return {GameBuild::groove_coaster_471, variant, enabled,
        {enabled[6], enabled[7], enabled[8], enabled[15]},
        {
            .tune_stack_offset = -0x32C,
            .semantic_stage_tune_stack_offset = -0x2B4,
            .tune_judgement_states_offset = 0x254,
            .tune_score_states_offset = 0x26C,
            .pointer_collection_begin_offset = 0x0C,
            .pointer_collection_end_offset = 0x10,
            .global_player_index_offset = 0xCB4,
            .input_manager_booster_offset = 4,
            .game_time_offset_offset = 0x2C,
            .hold_safe_frame_offset = 0x64,
            .slide_hold_safe_frame_offset = 0x68,
            .score_miss_offset = 120,
            .score_good_offset = 124,
            .score_cool_offset = 128,
            .score_great_offset = 132,
            .judgement_arrange_publication_offset = 0xAA,
            .judgement_left_free_tap_publication_offset = 0xED,
            .judgement_right_free_tap_publication_offset = 0xEE,
            .timing_grade_note_target_float_index = 44,
            .gameplay_sound_group = 2,
        }};
}
}
const AbsoluteJudgementProfile* ProfileFor(GameBuild build, GameImageVariant variant) noexcept {
    static const std::array profiles{
        MakeProfile(GameImageVariant::clean), MakeProfile(GameImageVariant::legacy_patched),
        MakeProfile(GameImageVariant::locally_verified)};
    for (const auto& profile : profiles)
        if (profile.build == build && profile.variant == variant) return &profile;
    return nullptr;
}
std::expected<FeaturePlan, PlanError> BuildAbsoluteJudgementPlan(
    GameBuild build, GameImageVariant variant, bool enabled) noexcept {
    if (const auto* profile = ProfileFor(build, variant))
        return FeaturePlan{FeatureId::absolute_judgement,
            enabled ? std::span<const VersionedOperation>(profile->enabled_operations)
                    : std::span<const VersionedOperation>(profile->disabled_operations), {}};
    return std::unexpected(PlanError{.stage = PlanStage::unsupported_feature,
        .feature = FeatureId::absolute_judgement});
}
}

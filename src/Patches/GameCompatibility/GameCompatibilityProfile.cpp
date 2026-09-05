#include "Patches/GameCompatibility/GameCompatibilityProfile.h"
#include <array>
namespace gc::game_compatibility {
namespace {
using namespace game_version;
using runtime_image::PatternOf;
constexpr auto Operations(SiteDisposition disposition) {
    return std::array<VersionedOperation, 4>{
        BytePatchOperation{{FeatureId::game_compatibility, "native_mouse_events",
            VersionedOperationKind::byte_patch, 0x000B0896, 2,
            PatternOf<0x75, 0x02>(), PatternOf<0x90, 0x90>(), 0, disposition}, PatternOf<0x90, 0x90>()},
        BytePatchOperation{{FeatureId::game_compatibility, "dongle_failure",
            VersionedOperationKind::byte_patch, 0x00102C7B, 2,
            PatternOf<0x75, 0x3B>(), PatternOf<0xEB, 0x3B>(), 1, disposition}, PatternOf<0xEB, 0x3B>()},
        BytePatchOperation{{FeatureId::game_compatibility, "dongle_security_transmit",
            VersionedOperationKind::byte_patch, 0x00103EE6, 5,
            PatternOf<0xE8, 0x45, 0xF6, 0xFF, 0xFF>(), PatternOf<0x90, 0x90, 0x90, 0x90, 0x90>(),
            2, disposition}, PatternOf<0x90, 0x90, 0x90, 0x90, 0x90>()},
        BytePatchOperation{{FeatureId::game_compatibility, "rfid_com_port",
            VersionedOperationKind::byte_patch, 0x002F7AC3, 1,
            PatternOf<0x31>(), PatternOf<0x32>(), 3, disposition}, PatternOf<0x32>(), runtime_image::MemoryKind::data},
    };
}
// Whole-image identity selects the coherent disposition; unknown images use
// only original contracts. COM1/COM2 is data, not an instruction write.
constexpr auto kOriginal = Operations(SiteDisposition::install);
constexpr auto kLegacyPatched = Operations(SiteDisposition::already_installed);
}
std::expected<game_version::FeaturePlan, game_version::PlanError> BuildGameCompatibilityPlan(
    game_version::GameBuild build, game_version::GameImageVariant variant) noexcept {
    if (build == GameBuild::groove_coaster_471) {
        switch (variant) {
        case GameImageVariant::clean:
        case GameImageVariant::locally_verified: return FeaturePlan{FeatureId::game_compatibility, kOriginal, {}};
        case GameImageVariant::legacy_patched: return FeaturePlan{FeatureId::game_compatibility, kLegacyPatched, {}};
        }
    }
    return std::unexpected(PlanError{.stage = PlanStage::unsupported_feature,
        .feature = FeatureId::game_compatibility});
}
}

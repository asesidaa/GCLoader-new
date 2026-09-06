#include "Patches/GameCompatibility/GameCompatibilityProfile.h"
#include <array>
namespace gc::game_compatibility {
namespace {
using namespace game_version;
using runtime_image::PatternOf;
using runtime_image::Rva;
constexpr auto Operations(SiteDisposition disposition, const std::array<Rva, 4>& rvas) {
    return std::array<VersionedOperation, 4>{
        BytePatchOperation{{FeatureId::game_compatibility, "native_mouse_events",
            VersionedOperationKind::byte_patch, rvas[0], 2,
            PatternOf<0x75, 0x02>(), PatternOf<0x90, 0x90>(), 0, disposition}, PatternOf<0x90, 0x90>()},
        BytePatchOperation{{FeatureId::game_compatibility, "dongle_failure",
            VersionedOperationKind::byte_patch, rvas[1], 2,
            PatternOf<0x75, 0x3B>(), PatternOf<0xEB, 0x3B>(), 1, disposition}, PatternOf<0xEB, 0x3B>()},
        BytePatchOperation{{FeatureId::game_compatibility, "dongle_security_transmit",
            VersionedOperationKind::byte_patch, rvas[2], 5,
            PatternOf<0xE8, 0x45, 0xF6, 0xFF, 0xFF>(), PatternOf<0x90, 0x90, 0x90, 0x90, 0x90>(),
            2, disposition}, PatternOf<0x90, 0x90, 0x90, 0x90, 0x90>()},
        BytePatchOperation{{FeatureId::game_compatibility, "rfid_com_port",
            VersionedOperationKind::byte_patch, rvas[3], 1,
            PatternOf<0x31>(), PatternOf<0x32>(), 3, disposition}, PatternOf<0x32>(), runtime_image::MemoryKind::data},
    };
}
// Known image variants supply dispositions directly; unknown images classify
// each site's original/installed bytes in preflight. COM1/COM2 is data.
constexpr std::array<Rva, 4> kRvas471{0x000B0896, 0x00102C7B, 0x00103EE6, 0x002F7AC3};
constexpr std::array<Rva, 4> kRvas206{0x000A3FF6, 0x000F7E9B, 0x000F90F6, 0x002B68C7};
constexpr auto kOriginal = Operations(SiteDisposition::install, kRvas471);
constexpr auto kLegacyPatched = Operations(SiteDisposition::already_installed, kRvas471);
constexpr auto kOriginal206 = Operations(SiteDisposition::install, kRvas206);
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
    if (build == GameBuild::groove_coaster_206 &&
        (variant == GameImageVariant::clean || variant == GameImageVariant::locally_verified))
        return FeaturePlan{FeatureId::game_compatibility, kOriginal206, {}};
    return std::unexpected(PlanError{.stage = PlanStage::unsupported_feature,
        .feature = FeatureId::game_compatibility});
}
}

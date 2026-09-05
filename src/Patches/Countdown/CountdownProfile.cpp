#include "Patches/Countdown/CountdownProfile.h"
#include <limits>
namespace gc::timer_freeze {
namespace {
using namespace game_version;
constexpr runtime_image::Rva kGlobalFrameDeltaSeconds = 0x002350C0;
struct DeltaCall final { runtime_image::Rva call, resume; const char* name; };
constexpr std::array<DeltaCall, 32> kCalls{{
    {0x30322, 0x30327, "delta_00030322"},
    {0x30340, 0x30345, "delta_00030340"},
    {0x1B45D2, 0x1B45D7, "delta_001b45d2"},
    {0x1B45F1, 0x1B45F6, "delta_001b45f1"},
    {0x1B4871, 0x1B4876, "delta_001b4871"},
    {0x1B4890, 0x1B4895, "delta_001b4890"},
    {0x1A6FB4, 0x1A6FB9, "delta_001a6fb4"},
    {0x1A6FD3, 0x1A6FD8, "delta_001a6fd3"},
    {0x1A83B4, 0x1A83B9, "delta_001a83b4"},
    {0x1A83D3, 0x1A83D8, "delta_001a83d3"},
    {0x1AEF84, 0x1AEF89, "delta_001aef84"},
    {0x1AEFA3, 0x1AEFA8, "delta_001aefa3"},
    {0x1BB104, 0x1BB109, "delta_001bb104"},
    {0x1BB123, 0x1BB128, "delta_001bb123"},
    {0x1C1805, 0x1C180A, "delta_001c1805"},
    {0x1C1824, 0x1C1829, "delta_001c1824"},
    {0x1C55F6, 0x1C55FB, "delta_001c55f6"},
    {0x1C5615, 0x1C561A, "delta_001c5615"},
    {0x1C6746, 0x1C674B, "delta_001c6746"},
    {0x1C6765, 0x1C676A, "delta_001c6765"},
    {0x201C22, 0x201C27, "delta_00201c22"},
    {0x201C41, 0x201C46, "delta_00201c41"},
    {0x201E70, 0x201E75, "delta_00201e70"},
    {0x201E8F, 0x201E94, "delta_00201e8f"},
    {0x201FD4, 0x201FD9, "delta_00201fd4"},
    {0x201FF3, 0x201FF8, "delta_00201ff3"},
    {0x204FB6, 0x204FBB, "delta_00204fb6"},
    {0x204FD5, 0x204FDA, "delta_00204fd5"},
    {0x2078A3, 0x2078A8, "delta_002078a3"},
    {0x2078C2, 0x2078C7, "delta_002078c2"},
    {0x20B124, 0x20B129, "delta_0020b124"},
    {0x20B143, 0x20B148, "delta_0020b143"},
}};
consteval bool ValidCalls() {
    for (const auto& call : kCalls) {
        const auto displacement = std::int64_t{kGlobalFrameDeltaSeconds} - call.resume;
        if (call.resume != call.call + 5 || displacement < (std::numeric_limits<std::int32_t>::min)() ||
            displacement > (std::numeric_limits<std::int32_t>::max)()) return false;
    }
    return true;
}
static_assert(ValidCalls());
CountdownProfile MakeProfile(GameImageVariant variant) noexcept {
    CountdownProfile profile{GameBuild::groove_coaster_471, variant, {}};
    for (std::size_t index = 0; index < kCalls.size(); ++index) {
        const auto& call = kCalls[index];
        auto original = runtime_image::PatternOf<0xE8, 0, 0, 0, 0>();
        const auto displacement = static_cast<std::uint32_t>(
            static_cast<std::int32_t>(std::int64_t{kGlobalFrameDeltaSeconds} - call.resume));
        for (std::size_t byte = 0; byte < sizeof(displacement); ++byte)
            original.bytes[byte + 1] = static_cast<std::byte>((displacement >> (byte * 8)) & 0xFF);
        constexpr auto frozen = runtime_image::PatternOf<0xD9, 0xEE, 0x90, 0x90, 0x90>();
        profile.operations[index] = BytePatchOperation{{FeatureId::countdown, call.name,
            VersionedOperationKind::byte_patch, call.call, 5, original, frozen,
            static_cast<std::uint32_t>(index)}, frozen};
    }
    return profile;
}
}
const CountdownProfile* ProfileFor(GameBuild build, GameImageVariant variant) noexcept {
    static const std::array profiles{
        MakeProfile(GameImageVariant::clean), MakeProfile(GameImageVariant::legacy_patched),
        MakeProfile(GameImageVariant::locally_verified)};
    for (const auto& profile : profiles)
        if (profile.build == build && profile.variant == variant) return &profile;
    return nullptr;
}
std::expected<FeaturePlan, PlanError> BuildCountdownPlan(
    GameBuild build, GameImageVariant variant, bool enabled) noexcept {
    if (!enabled) return FeaturePlan{FeatureId::countdown, {}, {}};
    if (const auto* profile = ProfileFor(build, variant))
        return FeaturePlan{FeatureId::countdown, profile->operations, {}};
    return std::unexpected(PlanError{.stage = PlanStage::unsupported_feature,
        .feature = FeatureId::countdown});
}
}

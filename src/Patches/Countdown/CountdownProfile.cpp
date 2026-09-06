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
// Fifteen native pairs. Only add countdowns with an independent player-confirm
// path; timeouts that are the sole way to advance must keep running.
// Native ownership and exit paths: docs/reverse-engineering/gc206-countdown-fix-2026-09-06.md.
constexpr runtime_image::Rva kGlobalFrameDeltaSeconds206 = 0x00203F80;
constexpr std::array<DeltaCall, 30> kCalls206{{
    {0x193580, 0x193585, "delta_00193580"},
    {0x19359F, 0x1935A4, "delta_0019359f"},
    {0x193880, 0x193885, "delta_00193880"},
    {0x19389F, 0x1938A4, "delta_0019389f"},
    {0x18B7C4, 0x18B7C9, "delta_0018b7c4"},
    {0x18B7E3, 0x18B7E8, "delta_0018b7e3"},
    // SelectMusic: flt_7AA650 is the visible timer. The earlier pair at
    // 0x18F692/0x18F6B1 decrements the input-repeat delay flt_7AA63C.
    {0x18F6F6, 0x18F6FB, "select_music_countdown_compare"},
    {0x18F715, 0x18F71A, "select_music_countdown_store"},
    // EventCourse: input 14 confirms independently of flt_7AA560 reaching zero.
    {0x198874, 0x198879, "event_course_countdown_compare"},
    {0x198893, 0x198898, "event_course_countdown_store"},
    {0x196764, 0x196769, "delta_00196764"},
    {0x196783, 0x196788, "delta_00196783"},
    {0x19BC88, 0x19BC8D, "delta_0019bc88"},
    {0x19BCA7, 0x19BCAC, "delta_0019bca7"},
    {0x19F3F4, 0x19F3F9, "delta_0019f3f4"},
    {0x19F413, 0x19F418, "delta_0019f413"},
    {0x19F1FF, 0x19F204, "delta_0019f1ff"},
    {0x19F21E, 0x19F223, "delta_0019f21e"},
    {0x1DAF54, 0x1DAF59, "delta_001daf54"},
    {0x1DAF73, 0x1DAF78, "delta_001daf73"},
    {0x1DB0D4, 0x1DB0D9, "delta_001db0d4"},
    {0x1DB0F3, 0x1DB0F8, "delta_001db0f3"},
    {0x1DCF54, 0x1DCF59, "delta_001dcf54"},
    {0x1DCF73, 0x1DCF78, "delta_001dcf73"},
    {0x1DECA1, 0x1DECA6, "delta_001deca1"},
    {0x1DECC0, 0x1DECC5, "delta_001decc0"},
    {0x1E0844, 0x1E0849, "delta_001e0844"},
    {0x1E0863, 0x1E0868, "delta_001e0863"},
    // ResultEventScore: input 14 confirms independently of flt_79408C.
    {0x1E2074, 0x1E2079, "event_score_result_countdown_compare"},
    {0x1E2093, 0x1E2098, "event_score_result_countdown_store"},
}};
template<std::size_t N>
consteval bool ValidCalls(const std::array<DeltaCall, N>& calls, runtime_image::Rva delta) {
    for (const auto& call : calls) {
        const auto displacement = std::int64_t{delta} - call.resume;
        if (call.resume != call.call + 5 || displacement < (std::numeric_limits<std::int32_t>::min)() ||
            displacement > (std::numeric_limits<std::int32_t>::max)()) return false;
    }
    return true;
}
static_assert(ValidCalls(kCalls, kGlobalFrameDeltaSeconds));
static_assert(ValidCalls(kCalls206, kGlobalFrameDeltaSeconds206));
template<std::size_t N>
constexpr auto Operations(const std::array<DeltaCall, N>& calls, runtime_image::Rva delta) noexcept {
    std::array<VersionedOperation, N> operations{};
    for (std::size_t index = 0; index < calls.size(); ++index) {
        const auto& call = calls[index];
        auto original = runtime_image::PatternOf<0xE8, 0, 0, 0, 0>();
        const auto displacement = static_cast<std::uint32_t>(
            static_cast<std::int32_t>(std::int64_t{delta} - call.resume));
        for (std::size_t byte = 0; byte < sizeof(displacement); ++byte)
            original.bytes[byte + 1] = static_cast<std::byte>((displacement >> (byte * 8)) & 0xFF);
        constexpr auto frozen = runtime_image::PatternOf<0xD9, 0xEE, 0x90, 0x90, 0x90>();
        operations[index] = BytePatchOperation{{FeatureId::countdown, call.name,
            VersionedOperationKind::byte_patch, call.call, 5, original, frozen,
            static_cast<std::uint32_t>(index)}, frozen};
    }
    return operations;
}
constexpr auto kOperations471 = Operations(kCalls, kGlobalFrameDeltaSeconds);
constexpr auto kOperations206 = Operations(kCalls206, kGlobalFrameDeltaSeconds206);
}
const CountdownProfile* ProfileFor(GameBuild build, GameImageVariant variant) noexcept {
    static const std::array profiles{
        CountdownProfile{GameBuild::groove_coaster_471, GameImageVariant::clean, kOperations471},
        CountdownProfile{GameBuild::groove_coaster_471, GameImageVariant::legacy_patched, kOperations471},
        CountdownProfile{GameBuild::groove_coaster_471, GameImageVariant::locally_verified, kOperations471},
        CountdownProfile{GameBuild::groove_coaster_206, GameImageVariant::clean, kOperations206},
        CountdownProfile{GameBuild::groove_coaster_206, GameImageVariant::locally_verified, kOperations206}};
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

#include "Patches/RendererDeviceLoss/RendererDeviceLossProfile.h"
namespace gc::renderer_device_loss {
namespace {
using namespace game_version;
using namespace runtime_image;
constexpr std::size_t kHooks = 6;
constexpr std::size_t kContinuations = 4;
static_assert(kHooks == 6 && kContinuations == 4 && kHooks + kContinuations == 10);
RendererDeviceLossProfile Make(GameImageVariant variant) {
    return {GameBuild::groove_coaster_471, variant, {{
        MidHookOperation{
            {FeatureId::renderer_device_loss, "device_lost_tail", VersionedOperationKind::mid_hook,
             0xE67D8, 6, PatternOf<0x89, 0xBE, 0x18, 0x1, 0x0, 0x0, 0x89, 0xBE, 0x1C, 0x1, 0x0, 0x0>(), {}, 0},
            &OnDeviceLostTail},
        MidHookOperation{
            {FeatureId::renderer_device_loss, "vertex_buffer_result", VersionedOperationKind::mid_hook,
             0xE79F7, 7, PatternOf<0x85, 0xC0, 0x7C, 0x59, 0x8B, 0x4F, 0xC>(), {}, 1},
            &OnVertexBufferCreateResult},
        MidHookOperation{
            {FeatureId::renderer_device_loss, "index_buffer_result", VersionedOperationKind::mid_hook,
             0xE7A84, 9, PatternOf<0x85, 0xC0, 0x7D, 0x13, 0x68, 0xE4, 0xA5, 0x71, 0x0>(), {}, 2},
            &OnIndexBufferCreateResult},
        MidHookOperation{
            {FeatureId::renderer_device_loss, "vertex_buffer_lock_guard", VersionedOperationKind::mid_hook,
             0xE5578, 9, PatternOf<0x3B, 0xF9, 0x72, 0x5, 0xE8, 0x66, 0x0, 0x2, 0x0>(), {}, 3},
            &OnVertexBufferLockGuard},
        MidHookOperation{
            {FeatureId::renderer_device_loss, "direct_lock_result", VersionedOperationKind::mid_hook,
             0xE691E, 5, PatternOf<0x8B, 0x4C, 0x24, 0x14, 0x51, 0x8B, 0x8E, 0xE4, 0x1, 0x0, 0x0>(), {}, 4},
            &OnDirectLockResult},
        MidHookOperation{
            {FeatureId::renderer_device_loss, "buffered_unlock_result", VersionedOperationKind::mid_hook,
             0xE5662, 9, PatternOf<0x85, 0xC0, 0x7D, 0x13, 0x68, 0xE4, 0xA5, 0x71, 0x0>(), {}, 5},
            &OnBufferedUnlockResult},
        ReadOnlyContractOperation{
            {FeatureId::renderer_device_loss, "initializer_epilogue", VersionedOperationKind::read_only_contract,
             0xE7EE9, 7, PatternOf<0x5F, 0x5E, 0x5B, 0x8B, 0xE5, 0x5D, 0xC3>(), {}, 0,
             SiteDisposition::verify_only}},
        ReadOnlyContractOperation{
            {FeatureId::renderer_device_loss, "vertex_buffer_lock_failure", VersionedOperationKind::read_only_contract,
             0xE55E2, 12, PatternOf<0x5F, 0x5E, 0x89, 0x18, 0x89, 0x58, 0x4, 0x5B, 0x59, 0xC2, 0x8, 0x0>(), {}, 0,
             SiteDisposition::verify_only}},
        ReadOnlyContractOperation{
            {FeatureId::renderer_device_loss, "direct_batch_cleanup", VersionedOperationKind::read_only_contract,
             0xE6AD6, 12, PatternOf<0x8B, 0xB6, 0xE4, 0x1, 0x0, 0x0, 0x8B, 0x5E, 0x10, 0x39, 0x5E, 0xC>(), {}, 0,
             SiteDisposition::verify_only}},
        ReadOnlyContractOperation{
            {FeatureId::renderer_device_loss, "buffered_unlock_continuation", VersionedOperationKind::read_only_contract,
             0xE5679, 12, PatternOf<0x8B, 0x86, 0x80, 0x4, 0x0, 0x0, 0x8B, 0x8E, 0x44, 0x7, 0x0, 0x0>(), {}, 0,
             SiteDisposition::verify_only}},
    }}, {.initialized_offset = 0x484, .index_buffer_holder_offset = 0x778,
         .vertex_buffer_lock_output_stack_offset = 0x14}};
}
}
const RendererDeviceLossProfile* ProfileFor(
    game_version::GameBuild build, game_version::GameImageVariant variant) noexcept {
    using namespace game_version;
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
std::expected<game_version::FeaturePlan, game_version::PlanError> BuildRendererDeviceLossPlan(
    game_version::GameBuild build, game_version::GameImageVariant variant) noexcept {
    using namespace game_version;
    const auto* profile = ProfileFor(build, variant);
    if (!profile) return std::unexpected(PlanError{.stage = PlanStage::unsupported_feature,
        .context = {build, variant}, .feature = FeatureId::renderer_device_loss, .site = "profile"});
    return FeaturePlan{FeatureId::renderer_device_loss, profile->operations, {}};
}
}

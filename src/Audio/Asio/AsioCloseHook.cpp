#include "Audio/Asio/AsioCloseProfile.h"
#include "Audio/AudioRuntimeState.h"
#include <algorithm>
namespace gc::audio::asio {
namespace { runtime_image::Rva g_close_site{}; }
std::expected<void, game_version::PlanError> PrepareAsioCloseRuntime(
    const game_version::ApprovedVersionedPlan& plan) noexcept {
    using namespace game_version;
    const auto invalid = [&](std::string_view site) {
        return std::unexpected(PlanError{.stage = PlanStage::invalid_plan,
            .context = plan.context(), .feature = FeatureId::asio_close, .site = site});
    };
    if (g_close_site || !IsAsioRuntimePublished()) return invalid("audio_owner_binding");
    const auto* build = std::get_if<GameBuild>(&plan.context().build);
    const auto* variant = std::get_if<GameImageVariant>(&plan.context().variant);
    const auto* profile = build && variant ? ProfileFor(*build, *variant) : nullptr;
    if (!profile) return invalid("profile");
    const auto& expected = ContractOf(profile->operations.front());
    const auto site = std::ranges::find_if(plan.sites(), [&](const ApprovedSite& actual) {
        const auto& contract = actual.contract();
        return contract.feature == expected.feature && contract.site == expected.site &&
            contract.rva == expected.rva && contract.kind == expected.kind;
    });
    if (site == plan.sites().end()) return invalid(expected.site);
    g_close_site = expected.rva;
    return {};
}
// Context registers are untouched; the relocated CoUninitialize call continues.
void OnOrdinaryAsioClose(safetyhook::Context&) noexcept {
    ReleaseAudioRuntimeAtOrdinaryAsioClose(g_close_site);
}
}

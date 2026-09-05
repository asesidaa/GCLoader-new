#include "Patches/AutoPlay/AutoPlayPatch.h"
#include "Patches/AutoPlay/AutoPlayMarker.h"
#include "Patches/AutoPlay/AutoPlayPatchDiagnostics.h"
#include "Diagnostics/FatalProcess.h"
#include "plog/Log.h"
#include <atomic>

namespace gc::auto_play {
namespace {
std::atomic_bool g_marker_active{};
NativeDebugTextFunction g_native_text{};
}
std::expected<void, game_version::PlanError> PrepareAutoPlayRuntime(
    const game_version::ApprovedVersionedPlan& plan) noexcept {
    using namespace game_version;
    bool enabled{};
    for (const auto& site : plan.sites()) {
        if (site.contract().feature != FeatureId::auto_play) continue;
        enabled = true;
        if (site.contract().site == "native_debug_text" &&
            site.contract().kind == VersionedOperationKind::read_only_contract) {
            // Publish the validated call target before the registry enables the
            // marker hook. Rendering stays dormant until every write succeeds.
            g_native_text = reinterpret_cast<NativeDebugTextFunction>(site.address);
            return {};
        }
    }
    if (enabled) return std::unexpected(PlanError{.stage = PlanStage::invalid_plan,
        .context = plan.context(), .feature = FeatureId::auto_play, .site = "native_debug_text"});
    return {};
}
void ActivateAutoPlayMarker(const game_version::ApprovedVersionedPlan& plan) noexcept {
    using namespace game_version;
    std::size_t patched{}, existing{};
    bool enabled{};
    for (const auto& site : plan.sites()) {
        if (site.contract().feature != FeatureId::auto_play) continue;
        enabled = true;
        if (site.contract().kind == VersionedOperationKind::byte_patch) {
            if (site.disposition == SiteDisposition::install) ++patched;
            else if (site.disposition == SiteDisposition::already_installed) ++existing;
        }
    }
    if (!enabled) return;
    if (!g_native_text) PublishAutoPlayMarkerRuntimeFatal();
    g_marker_active.store(true, std::memory_order_release);
    try {
        PLOG_WARNING << "AutoPlayPatch: state=enabled direct_patched=" << patched
            << " direct_existing=" << existing << " marker=active score_save=disabled";
    } catch (...) { diagnostics::AbortProcess({}); }
}
void AutoPlayMarkerMidHook(safetyhook::Context&) noexcept {
    try {
        if (!g_marker_active.load(std::memory_order_acquire)) return;
        if (!DrawAutoPlayMarker(g_native_text)) PublishAutoPlayMarkerRuntimeFatal();
    } catch (...) { PublishAutoPlayMarkerRuntimeFatal(); }
}
}

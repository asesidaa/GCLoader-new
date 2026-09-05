#include "Audio/AudioFeature.h"
#include "Audio/Asio/AsioCloseProfile.h"
#include "Audio/DirectSound/DirectSoundHook.h"
#include <algorithm>
#include <atomic>

namespace gc::audio {
namespace {
std::atomic_bool g_route_prepared{};
std::atomic<AudioBackend> g_prepared_backend{AudioBackend::directsound};
}
std::expected<PreparedAudioFeature, game_version::PlanError> PrepareAudioFeature(
    AudioSettings settings, game_version::GameBuild build, game_version::GameImageVariant variant) noexcept {
    PreparedAudioFeature prepared{std::move(settings), std::nullopt};
    if (prepared.settings.backend() == AudioBackend::asio) {
        auto profile = asio::BuildAsioClosePlan(build, variant);
        if (!profile) return std::unexpected(profile.error());
        prepared.versioned = *profile;
    }
    return prepared;
}
std::expected<void, hooking::HookError> PreparedAudioFeature::AddExportHooks(hooking::HookPlan& plan) const noexcept {
    if (settings.backend() == AudioBackend::directsound) return {};
    return AddDirectSoundHook(plan);
}
std::expected<void, AudioRuntimeError> PublishAudioFeature(
    PreparedAudioFeature prepared, const hooking::ValidatedHookPlan& exports) noexcept {
    const auto backend = prepared.settings.backend();
    const bool has_route = std::ranges::any_of(exports.requests(), [](const auto& request) {
        return request.target.identity.feature == "DirectSound" &&
               request.target.identity.site == "DirectSoundCreate8";
    });
    if (backend != AudioBackend::directsound && !has_route)
        return std::unexpected(AudioRuntimeError{AudioRuntimeStage::missing_hook_route, ERROR_INVALID_STATE});
    auto published = PrepareAndPublishAudioRuntime(std::move(prepared.settings));
    if (!published) return published;
    g_prepared_backend.store(backend, std::memory_order_relaxed);
    g_route_prepared.store(true, std::memory_order_release);
    return {};
}
bool IsAudioRoutePrepared(AudioBackend backend) noexcept {
    return g_route_prepared.load(std::memory_order_acquire) &&
        g_prepared_backend.load(std::memory_order_relaxed) == backend;
}
}

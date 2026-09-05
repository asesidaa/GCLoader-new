#pragma once
#include "Audio/AudioSettings.h"
#include "Audio/AudioRuntimeState.h"
#include "Patches/GameVersion/VersionedPlan.h"

namespace gc::audio {
struct PreparedAudioFeature final {
    AudioSettings settings;
    std::optional<game_version::FeaturePlan> versioned;
    [[nodiscard]] std::expected<void, hooking::HookError> AddExportHooks(hooking::HookPlan&) const noexcept;
};
[[nodiscard]] std::expected<PreparedAudioFeature, game_version::PlanError>
PrepareAudioFeature(AudioSettings, game_version::GameBuild, game_version::GameImageVariant) noexcept;
[[nodiscard]] std::expected<void, AudioRuntimeError>
PublishAudioFeature(PreparedAudioFeature, const hooking::ValidatedHookPlan&) noexcept;
[[nodiscard]] bool IsAudioRoutePrepared(AudioBackend) noexcept;
[[nodiscard]] bool IsAudioHookCommitted() noexcept;
}

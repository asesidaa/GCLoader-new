#include "Audio/AudioPatch.h"
#include "Audio/AudioRuntimeState.h"
namespace gc::audio {
std::expected<void, hooking::HookError> AddAudioHooks(
    hooking::HookPlan& hooks, AudioSettings settings) noexcept {
    const auto prepared = PrepareAndPublishAudioRuntime(std::move(settings));
    if (!prepared) return std::unexpected(hooking::HookError{
        .stage = hooking::HookStage::invalid_plan,
        .identity = {"Audio", prepared.error().stage == AudioRuntimeStage::already_prepared
            ? "runtime_already_prepared" : "runtime_construction"},
        .win32_error = prepared.error().win32_error});
    return AddPublishedAudioRuntimeHook(hooks);
}
}

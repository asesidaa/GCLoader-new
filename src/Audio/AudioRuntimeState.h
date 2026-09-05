#pragma once
#include "Audio/AudioSettings.h"
#include "Platform/Win32/Hooking/HookPlan.h"
namespace gc::audio {
class IAudioEngineController;
[[nodiscard]] IAudioEngineController* GetOrCreatePublishedAudioController() noexcept;
enum class AudioRuntimeStage : std::uint8_t { already_prepared, construction, missing_hook_route };
struct AudioRuntimeError final {
    AudioRuntimeStage stage{};
    DWORD win32_error{};
};
[[nodiscard]] std::expected<void, AudioRuntimeError>
PrepareAndPublishAudioRuntime(AudioSettings settings) noexcept;
[[nodiscard]] bool IsAsioRuntimePublished() noexcept;
void ReleaseAudioRuntimeAtOrdinaryAsioClose(std::uintptr_t site_rva) noexcept;
}

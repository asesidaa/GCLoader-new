#pragma once
#include "Audio/AudioSettings.h"
#include "Platform/Win32/Hooking/HookPlan.h"
namespace gc::audio {
enum class AudioRuntimeStage : std::uint8_t { already_prepared, construction };
struct AudioRuntimeError final {
    AudioRuntimeStage stage{};
    DWORD win32_error{};
};
[[nodiscard]] std::expected<void, AudioRuntimeError>
PrepareAndPublishAudioRuntime(AudioSettings settings) noexcept;
[[nodiscard]] bool IsAsioRuntimePublished() noexcept;
[[nodiscard]] std::expected<void, hooking::HookError>
AddPublishedAudioRuntimeHook(hooking::HookPlan&) noexcept;
void ReleaseAudioRuntimeAtOrdinaryAsioClose(std::uintptr_t site_rva) noexcept;
}

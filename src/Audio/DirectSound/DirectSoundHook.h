#pragma once
#include "Audio/AudioBackendController.h"
#include "Platform/Win32/Hooking/HookPlan.h"
namespace gc::audio {
[[nodiscard]] std::expected<void, hooking::HookError> AddDirectSoundHook(
    hooking::HookPlan&, IAudioBackendControllerFactory&, IAudioBackendControllerReporter&) noexcept;
[[nodiscard]] bool IsAudioHookCommitted() noexcept;
}

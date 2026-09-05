#pragma once
#include "Audio/AudioSettings.h"
#include "Platform/Win32/Hooking/HookPlan.h"
namespace gc::audio {
[[nodiscard]] std::expected<void, hooking::HookError>
AddAudioHooks(hooking::HookPlan&, AudioSettings settings) noexcept;
[[nodiscard]] bool IsAudioHookCommitted() noexcept;
}

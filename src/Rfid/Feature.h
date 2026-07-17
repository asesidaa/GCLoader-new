#pragma once

#include "Platform/Win32/Hooking/MinHookTransaction.h"

#include <Windows.h>

#include <expected>

namespace gc::rfid {

enum class FeatureFailureStage {
    configuration,
    allocation,
    hook_installation,
};

struct FeatureError {
    FeatureFailureStage stage{};
    DWORD win32_error{ERROR_SUCCESS};
    gc::win32_hooks::HookInstallError hook{};
};

[[nodiscard]] std::expected<void, FeatureError>
InitializeFeature() noexcept;

} // namespace gc::rfid

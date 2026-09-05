#pragma once
#include "Platform/Win32/Hooking/HookPlan.h"
#include "Rfid/FeatureSettings.h"
#include "SystemPath/SystemRoot.h"
namespace gc::rfid {
enum class FeatureFailureStage { configuration, allocation, hook_plan };
struct FeatureError final {
    FeatureFailureStage stage{};
    DWORD win32_error{};
    hooking::HookError hook{};
};
[[nodiscard]] std::expected<void, FeatureError> AddRfidHooks(
    hooking::HookPlan& plan, const system_path::RuntimeRoot& system_root, FeatureSettings settings) noexcept;
}

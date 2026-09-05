#pragma once
#include "Platform/Win32/Hooking/HookPlan.h"
#include "Rfid/FeatureSettings.h"
namespace gc::rfid {
class Runtime;
enum class FeatureFailureStage { configuration, allocation, hook_plan };
struct FeatureError final {
    FeatureFailureStage stage{};
    DWORD win32_error{};
    hooking::HookError hook{};
};
[[nodiscard]] std::expected<Runtime*, FeatureError> AddRfidHooks(
    hooking::HookPlan& plan, FeatureSettings settings) noexcept;
}

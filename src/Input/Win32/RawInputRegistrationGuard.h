#pragma once
#include "Platform/Win32/Hooking/HookPlan.h"
namespace gc::input {
[[nodiscard]] std::expected<void, hooking::HookError>
AddRawInputRegistrationHook(hooking::HookPlan&) noexcept;
BOOL WINAPI RegisterOwnedRawInputDevices(
    PCRAWINPUTDEVICE devices, UINT device_count, UINT device_size) noexcept;
}

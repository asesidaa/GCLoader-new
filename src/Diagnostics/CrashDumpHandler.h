#pragma once
#include "Platform/Win32/Hooking/HookPlan.h"
namespace gc::crash_dump {
[[nodiscard]] std::expected<void, hooking::HookError> AddCrashDumpHook(hooking::HookPlan&) noexcept;
}

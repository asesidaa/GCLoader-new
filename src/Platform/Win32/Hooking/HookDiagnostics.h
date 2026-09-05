#pragma once
#include "Platform/Win32/Hooking/HookError.h"
#include "Diagnostics/FatalProcess.h"
namespace gc::hooking {
[[nodiscard]] const char* HookStageName(HookStage) noexcept;
[[nodiscard]] diagnostics::FatalProcessReport FormatHookError(const HookError&);
[[noreturn]] void AbortHookError(const HookError&) noexcept;
}

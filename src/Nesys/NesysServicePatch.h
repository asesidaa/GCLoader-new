#pragma once
// Winsock must precede Windows headers.
#include <WinSock2.h>
#include "Platform/Win32/Hooking/HookPlan.h"
#include "Nesys/NesysSettings.h"
#include "Nesys/NesysServiceProcess.h"
namespace gc::nesys_service {
[[nodiscard]] std::expected<void, hooking::HookError> AddNesysHooks(
    hooking::HookPlan&, HMODULE loader_module, ProcessRole, NesysSettings) noexcept;
}

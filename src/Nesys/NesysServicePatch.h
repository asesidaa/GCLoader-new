#pragma once

// Keep Winsock2 ahead of Windows.h for every service-patch translation unit.
// ReSharper disable once CppUnusedIncludeDirective
#include <WinSock2.h>
#include <Windows.h>

#include "Nesys/NesysServiceProcess.h"

namespace gc::nesys_service {

bool NesysServicePatchInit(
    HMODULE loader_module,
    ProcessRole role) noexcept;

} // namespace gc::nesys_service

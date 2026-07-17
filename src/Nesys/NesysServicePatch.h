#pragma once

#include <WinSock2.h>
#include <Windows.h>

#include "Nesys/NesysServiceProcess.h"

namespace gc::nesys_service {

bool NesysServicePatchInit(
    HMODULE loader_module,
    ProcessRole role) noexcept;

} // namespace gc::nesys_service

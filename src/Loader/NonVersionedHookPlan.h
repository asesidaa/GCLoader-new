#pragma once
#include <WinSock2.h>
#include "Nesys/NesysSettings.h"
#include "SystemPath/SystemRoot.h"
namespace gc::config { class ValidatedConfig; }
namespace gc::loader {
void InstallGameNonVersionedHooks(
    HMODULE loader_module, const config::ValidatedConfig&, const system_path::RuntimeRoot&) noexcept;
void InstallNesysNonVersionedHooks(HMODULE loader_module, nesys_service::NesysSettings) noexcept;
}

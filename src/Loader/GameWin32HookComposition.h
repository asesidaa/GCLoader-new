#pragma once
#include "Loader/StartupFailure.h"
#include "SystemPath/SystemRoot.h"
#include "Win32Hooks/HandlerChain.h"
namespace gc::rfid { class Runtime; }
namespace gc::system_path { class SystemPathRouter; }
namespace gc::testmode_storage { class Hooks; }
namespace gc::win32_hooks { class Kernel32Dispatcher; }
namespace gc::loader {
[[nodiscard]] std::expected<void, win32_hooks::RegistrationError> ComposeGameWin32Handlers(
    win32_hooks::Kernel32Dispatcher&, rfid::Runtime&,
    system_path::SystemPathRouter&, testmode_storage::Hooks&) noexcept;
[[nodiscard]] std::expected<void, StartupError> AddGameWin32Hooks(
    hooking::HookPlan&, const system_path::RuntimeRoot&, bool storage_enabled, rfid::Runtime&) noexcept;
}

#pragma once
#include "SystemPath/SystemPathRouter.h"
#include "Win32Hooks/Kernel32Dispatcher.h"
namespace gc::system_path {
[[nodiscard]] std::expected<void, win32_hooks::RegistrationError> AddWin32PathHandlers(
    win32_hooks::Kernel32Dispatcher&, SystemPathRouter&) noexcept;
}

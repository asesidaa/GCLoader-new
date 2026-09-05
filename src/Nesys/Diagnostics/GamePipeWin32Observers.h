#pragma once
#include "Win32Hooks/Kernel32Dispatcher.h"
namespace gc::nesys_service::diagnostics {
[[nodiscard]] std::expected<void, win32_hooks::RegistrationError> AddGamePipeWin32Observers(
    win32_hooks::Kernel32Dispatcher&) noexcept;
}

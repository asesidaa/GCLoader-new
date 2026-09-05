#pragma once
#include "Win32Hooks/Kernel32Dispatcher.h"
#include "Rfid/Runtime.h"
namespace gc::rfid {
[[nodiscard]] std::expected<void, win32_hooks::RegistrationError> AddWin32FileHandlers(
    win32_hooks::Kernel32Dispatcher&, Runtime&) noexcept;
}

#pragma once
#include "TestModeStorage/Hooks.h"
#include "Win32Hooks/Kernel32Dispatcher.h"
namespace gc::testmode_storage {
[[nodiscard]] std::expected<void, win32_hooks::RegistrationError> AddWin32PathHandlers(
    win32_hooks::Kernel32Dispatcher&, Hooks&) noexcept;
}

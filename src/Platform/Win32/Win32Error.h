#pragma once

#include <Windows.h>
#include <expected>
#include <string>

namespace gc::platform::win32 {

struct Win32FormatError final {
    DWORD source_error{ERROR_SUCCESS};
    DWORD format_error{ERROR_SUCCESS};
};

[[nodiscard]] std::expected<std::wstring, Win32FormatError>
FormatWin32Error(DWORD captured_error) noexcept;

} // namespace gc::platform::win32

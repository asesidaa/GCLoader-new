#include "Platform/Win32/Win32Error.h"

namespace gc::platform::win32 {

std::expected<std::wstring, Win32FormatError>
FormatWin32Error(DWORD captured_error) noexcept
{
    struct LocalMessage final {
        wchar_t* data{};
        ~LocalMessage() { if (data) LocalFree(data); }
    } message;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, captured_error, 0,
        reinterpret_cast<wchar_t*>(&message.data), 0, nullptr);
    if (length == 0)
        return std::unexpected(Win32FormatError{captured_error, GetLastError()});
    try
    {
        std::wstring text(message.data, length);
        while (!text.empty() && (text.back() == L'\r' || text.back() == L'\n'))
            text.pop_back();
        return text;
    }
    catch (...)
    {
        return std::unexpected(Win32FormatError{
            captured_error, ERROR_NOT_ENOUGH_MEMORY});
    }
}

} // namespace gc::platform::win32

#include "Platform/Win32/Utf.h"

#include <climits>

namespace gc::platform::win32 {

std::expected<std::wstring, UtfError> Utf8ToWide(std::string_view text) noexcept
{
    constexpr auto direction = UtfDirection::utf8_to_utf16;
    if (text.empty()) return std::wstring{};
    if (text.size() > INT_MAX)
        return std::unexpected(UtfError{direction, ERROR_INVALID_PARAMETER, UtfStage::length});
    const auto length = static_cast<int>(text.size());
    const int required = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), length, nullptr, 0);
    if (required == 0)
        return std::unexpected(UtfError{direction, GetLastError(), UtfStage::sizing});
    try
    {
        std::wstring result(static_cast<std::size_t>(required), L'\0');
        const int written = MultiByteToWideChar(
            CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), length,
            result.data(), required);
        if (written == 0)
            return std::unexpected(UtfError{direction, GetLastError(), UtfStage::writing});
        if (written != required)
            return std::unexpected(UtfError{direction, ERROR_INVALID_DATA, UtfStage::writing});
        return result;
    }
    catch (...)
    {
        return std::unexpected(UtfError{direction, ERROR_NOT_ENOUGH_MEMORY, UtfStage::allocation});
    }
}

std::expected<std::string, UtfError> WideToUtf8(std::wstring_view text) noexcept
{
    constexpr auto direction = UtfDirection::utf16_to_utf8;
    if (text.empty()) return std::string{};
    if (text.size() > INT_MAX)
        return std::unexpected(UtfError{direction, ERROR_INVALID_PARAMETER, UtfStage::length});
    const auto length = static_cast<int>(text.size());
    const int required = WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), length, nullptr, 0, nullptr, nullptr);
    if (required == 0)
        return std::unexpected(UtfError{direction, GetLastError(), UtfStage::sizing});
    try
    {
        std::string result(static_cast<std::size_t>(required), '\0');
        const int written = WideCharToMultiByte(
            CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), length,
            result.data(), required, nullptr, nullptr);
        if (written == 0)
            return std::unexpected(UtfError{direction, GetLastError(), UtfStage::writing});
        if (written != required)
            return std::unexpected(UtfError{direction, ERROR_INVALID_DATA, UtfStage::writing});
        return result;
    }
    catch (...)
    {
        return std::unexpected(UtfError{direction, ERROR_NOT_ENOUGH_MEMORY, UtfStage::allocation});
    }
}

} // namespace gc::platform::win32

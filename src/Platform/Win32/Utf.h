#pragma once

#include <Windows.h>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

namespace gc::platform::win32 {

enum class UtfDirection : std::uint8_t { utf8_to_utf16, utf16_to_utf8 };
enum class UtfStage : std::uint8_t { length, sizing, writing, allocation };

struct UtfError final {
    UtfDirection direction{};
    DWORD win32_error{ERROR_SUCCESS};
    UtfStage stage{};
};

[[nodiscard]] std::expected<std::wstring, UtfError>
Utf8ToWide(std::string_view text) noexcept;

[[nodiscard]] std::expected<std::string, UtfError>
WideToUtf8(std::wstring_view text) noexcept;

} // namespace gc::platform::win32

#pragma once

#include <Windows.h>

#include <string>
#include <string_view>

namespace gc::rfid::card_reader_test_client {

enum class SendStatus {
    accepted,
    invalid,
    pipe_unavailable,
    pipe_busy,
    short_write,
    short_response,
    unexpected_response,
    win32_error,
};

struct SendResult {
    SendStatus status{SendStatus::win32_error};
    DWORD win32_error{ERROR_SUCCESS};

    constexpr bool operator==(const SendResult&) const = default;
};

[[nodiscard]] SendResult SendCardNumber(
    const wchar_t* pipe_name,
    std::string_view card_number) noexcept;

[[nodiscard]] std::wstring FormatStatus(
    const SendResult& result);

} // namespace gc::rfid::card_reader_test_client

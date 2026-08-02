#include "CardReaderTestClient/CardReaderClient.h"

#include "Rfid/CardReaderProtocol.h"

#include <array>
#include <limits>
#include <string_view>

namespace gc::rfid::card_reader_test_client {
namespace {

class UniqueHandle {
public:
    explicit UniqueHandle(HANDLE value) noexcept
        : value_{value}
    {
    }

    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;

    ~UniqueHandle()
    {
        if (value_ != INVALID_HANDLE_VALUE) {
            CloseHandle(value_);
        }
    }

    [[nodiscard]] HANDLE Get() const noexcept
    {
        return value_;
    }

private:
    HANDLE value_{INVALID_HANDLE_VALUE};
};

SendResult Win32Failure(DWORD error) noexcept
{
    if (error == ERROR_FILE_NOT_FOUND) {
        return {.status = SendStatus::pipe_unavailable};
    }
    if (error == ERROR_PIPE_BUSY) {
        return {.status = SendStatus::pipe_busy};
    }
    return {
        .status = SendStatus::win32_error,
        .win32_error = error,
    };
}

bool IsPeerDisconnect(DWORD error) noexcept
{
    return error == ERROR_BROKEN_PIPE ||
        error == ERROR_NO_DATA ||
        error == ERROR_PIPE_NOT_CONNECTED;
}

} // namespace

SendResult SendCardNumber(
    const wchar_t* pipe_name,
    std::string_view card_number) noexcept
{
    if (pipe_name == nullptr || pipe_name[0] == L'\0' ||
        card_number.size() >
            std::numeric_limits<DWORD>::max()) {
        return {
            .status = SendStatus::win32_error,
            .win32_error = ERROR_INVALID_PARAMETER,
        };
    }

    UniqueHandle pipe{CreateFileW(
        pipe_name,
        GENERIC_READ | GENERIC_WRITE,
        0,
        nullptr,
        OPEN_EXISTING,
        0,
        nullptr)};
    if (pipe.Get() == INVALID_HANDLE_VALUE) {
        return Win32Failure(GetLastError());
    }

    DWORD mode = PIPE_READMODE_MESSAGE;
    if (!SetNamedPipeHandleState(
            pipe.Get(), &mode, nullptr, nullptr)) {
        return Win32Failure(GetLastError());
    }

    DWORD bytes_written{};
    if (!WriteFile(
            pipe.Get(),
            card_number.data(),
            static_cast<DWORD>(card_number.size()),
            &bytes_written,
            nullptr)) {
        return Win32Failure(GetLastError());
    }
    if (bytes_written != card_number.size()) {
        return {.status = SendStatus::short_write};
    }

    std::array<char, 8> response{};
    DWORD bytes_read{};
    if (!ReadFile(
            pipe.Get(),
            response.data(),
            static_cast<DWORD>(response.size()),
            &bytes_read,
            nullptr)) {
        const DWORD error = GetLastError();
        if (IsPeerDisconnect(error)) {
            return {.status = SendStatus::short_response};
        }
        if (error == ERROR_MORE_DATA) {
            return {.status = SendStatus::unexpected_response};
        }
        return Win32Failure(error);
    }
    if (bytes_read < card_reader::kAcceptedResponse.size()) {
        return {.status = SendStatus::short_response};
    }

    const std::string_view received{response.data(), bytes_read};
    if (received == card_reader::kAcceptedResponse) {
        return {.status = SendStatus::accepted};
    }
    if (received == card_reader::kInvalidResponse) {
        return {.status = SendStatus::invalid};
    }
    return {.status = SendStatus::unexpected_response};
}

std::wstring FormatStatus(const SendResult& result)
{
    switch (result.status) {
    case SendStatus::accepted:
        return L"OK";
    case SendStatus::invalid:
        return L"INVALID";
    case SendStatus::pipe_unavailable:
        return L"Pipe unavailable";
    case SendStatus::pipe_busy:
        return L"Pipe busy";
    case SendStatus::short_write:
        return L"Short write";
    case SendStatus::short_response:
        return L"Short response";
    case SendStatus::unexpected_response:
        return L"Unexpected response";
    case SendStatus::win32_error:
        return L"Windows error " +
            std::to_wstring(result.win32_error);
    }
    return L"Unexpected response";
}

} // namespace gc::rfid::card_reader_test_client

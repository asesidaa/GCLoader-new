#include "CardReaderTestClient/CardReaderClient.h"

#include "Rfid/CardReaderInterface.h"
#include "Rfid/State.h"

#include <Windows.h>

#include <array>
#include <atomic>
#include <expected>
#include <iostream>
#include <optional>
#include <semaphore>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

namespace {

using gc::rfid::CardData;
using gc::rfid::CardScanState;
using gc::rfid::card_reader::CardReaderConnectionOutcome;
using gc::rfid::card_reader_test_client::SendResult;
using gc::rfid::card_reader_test_client::SendStatus;

class UniqueHandle {
public:
    UniqueHandle() noexcept = default;
    explicit UniqueHandle(HANDLE value) noexcept
        : value_{value}
    {
    }

    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;

    UniqueHandle(UniqueHandle&& other) noexcept
        : value_{std::exchange(other.value_, INVALID_HANDLE_VALUE)}
    {
    }

    UniqueHandle& operator=(UniqueHandle&& other) noexcept
    {
        if (this != &other) {
            Reset();
            value_ = std::exchange(
                other.value_, INVALID_HANDLE_VALUE);
        }
        return *this;
    }

    ~UniqueHandle()
    {
        Reset();
    }

    [[nodiscard]] HANDLE Get() const noexcept
    {
        return value_;
    }

    void Reset() noexcept
    {
        if (value_ != INVALID_HANDLE_VALUE) {
            CloseHandle(value_);
            value_ = INVALID_HANDLE_VALUE;
        }
    }

private:
    HANDLE value_{INVALID_HANDLE_VALUE};
};

struct RealServerExchange {
    SendResult client_result;
    std::expected<CardReaderConnectionOutcome, DWORD> server_result{
        std::unexpected(ERROR_IO_PENDING)};
};

struct RawServerExchange {
    SendResult client_result;
    DWORD server_error{ERROR_SUCCESS};
};

constexpr CardData kExpectedCard{
    0x04, 0xC2, 0x3D, 0xDA, 0x6F, 0x52, 0x80, 0x00,
    '1', '2', '3', '4', '5', '6', '7', '8',
    '9', '0', '1', '2', '3', '4', '5', '6'};

int Expect(bool condition, const char* name)
{
    if (condition) {
        return 0;
    }
    std::cerr << name << " failed\n";
    return 1;
}

std::wstring UniquePipeName(std::wstring_view suffix)
{
    static std::atomic_uint32_t counter{};
    return L"\\\\.\\pipe\\GCLoader.CardReader.ClientTests." +
        std::to_wstring(GetCurrentProcessId()) + L"." +
        std::to_wstring(GetTickCount64()) + L"." +
        std::to_wstring(counter.fetch_add(1)) + L"." +
        std::wstring{suffix};
}

bool WaitForPipe(const wchar_t* pipe_name) noexcept
{
    const auto deadline = GetTickCount64() + 5000;
    for (;;) {
        if (WaitNamedPipeW(pipe_name, 10)) {
            return true;
        }
        const DWORD error = GetLastError();
        if (error != ERROR_FILE_NOT_FOUND &&
            error != ERROR_SEM_TIMEOUT &&
            error != ERROR_PIPE_BUSY) {
            return false;
        }
        if (GetTickCount64() >= deadline) {
            return false;
        }
        Sleep(1);
    }
}

RealServerExchange ExchangeWithRealServer(
    const std::wstring& pipe_name,
    CardScanState& card_scan,
    std::string_view request)
{
    RealServerExchange exchange;
    std::jthread server{[&] {
        exchange.server_result =
            gc::rfid::card_reader::ServeOneCardReaderConnection(
                pipe_name.c_str(), card_scan);
    }};

    if (WaitForPipe(pipe_name.c_str())) {
        exchange.client_result =
            gc::rfid::card_reader_test_client::SendCardNumber(
                pipe_name.c_str(), request);
    } else {
        exchange.client_result = {
            .status = SendStatus::win32_error,
            .win32_error = GetLastError(),
        };
    }
    server.join();
    return exchange;
}

RawServerExchange ExchangeWithRawResponse(
    const std::wstring& pipe_name,
    std::optional<std::string> response)
{
    RawServerExchange exchange;
    std::binary_semaphore ready{0};
    std::jthread server{[&] {
        UniqueHandle pipe{CreateNamedPipeW(
            pipe_name.c_str(),
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_MESSAGE |
                PIPE_READMODE_MESSAGE |
                PIPE_WAIT |
                PIPE_REJECT_REMOTE_CLIENTS,
            1,
            16,
            16,
            0,
            nullptr)};
        if (pipe.Get() == INVALID_HANDLE_VALUE) {
            exchange.server_error = GetLastError();
            ready.release();
            return;
        }
        ready.release();

        if (!ConnectNamedPipe(pipe.Get(), nullptr) &&
            GetLastError() != ERROR_PIPE_CONNECTED) {
            exchange.server_error = GetLastError();
            return;
        }

        std::array<char, 32> request{};
        DWORD bytes_read{};
        if (!ReadFile(
                pipe.Get(),
                request.data(),
                static_cast<DWORD>(request.size()),
                &bytes_read,
                nullptr)) {
            exchange.server_error = GetLastError();
            return;
        }

        if (!response) {
            return;
        }

        DWORD bytes_written{};
        if (!WriteFile(
                pipe.Get(),
                response->data(),
                static_cast<DWORD>(response->size()),
                &bytes_written,
                nullptr) ||
            bytes_written != response->size()) {
            exchange.server_error = GetLastError();
            return;
        }
        if (!FlushFileBuffers(pipe.Get())) {
            exchange.server_error = GetLastError();
        }
    }};

    ready.acquire();
    exchange.client_result =
        gc::rfid::card_reader_test_client::SendCardNumber(
            pipe_name.c_str(), "1234567890123456");
    server.join();
    return exchange;
}

} // namespace

int main()
{
    using gc::rfid::card_reader_test_client::FormatStatus;
    using gc::rfid::card_reader_test_client::SendCardNumber;

    int failures = 0;

    CardScanState accepted_state;
    const auto accepted = ExchangeWithRealServer(
        UniquePipeName(L"accepted"),
        accepted_state,
        "1234567890123456");
    const auto accepted_scan = accepted_state.Snapshot();
    failures += Expect(
        accepted.client_result.status == SendStatus::accepted &&
            accepted.server_result &&
            *accepted.server_result ==
                CardReaderConnectionOutcome::accepted &&
            accepted_scan.card_data == kExpectedCard &&
            FormatStatus(accepted.client_result) == L"OK",
        "client recognizes OK from real server");

    CardScanState invalid_state;
    const auto invalid = ExchangeWithRealServer(
        UniquePipeName(L"invalid"),
        invalid_state,
        "123456789012345");
    failures += Expect(
        invalid.client_result.status == SendStatus::invalid &&
            invalid.server_result &&
            *invalid.server_result ==
                CardReaderConnectionOutcome::invalid &&
            !invalid_state.IsPresent() &&
            FormatStatus(invalid.client_result) == L"INVALID",
        "client recognizes INVALID from real server");

    const auto unavailable = SendCardNumber(
        UniquePipeName(L"unavailable").c_str(),
        "1234567890123456");
    failures += Expect(
        unavailable.status == SendStatus::pipe_unavailable &&
            FormatStatus(unavailable) == L"Pipe unavailable",
        "client distinguishes unavailable pipe");

    const auto busy_name = UniquePipeName(L"busy");
    UniqueHandle busy_server{CreateNamedPipeW(
        busy_name.c_str(),
        PIPE_ACCESS_DUPLEX,
        PIPE_TYPE_MESSAGE |
            PIPE_READMODE_MESSAGE |
            PIPE_WAIT |
            PIPE_REJECT_REMOTE_CLIENTS,
        1,
        16,
        16,
        0,
        nullptr)};
    UniqueHandle holding_client{CreateFileW(
        busy_name.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0,
        nullptr,
        OPEN_EXISTING,
        0,
        nullptr)};
    const auto busy = SendCardNumber(
        busy_name.c_str(), "1234567890123456");
    failures += Expect(
        busy_server.Get() != INVALID_HANDLE_VALUE &&
            holding_client.Get() != INVALID_HANDLE_VALUE &&
            busy.status == SendStatus::pipe_busy &&
            FormatStatus(busy) == L"Pipe busy",
        "client distinguishes occupied pipe");

    const auto closed = ExchangeWithRawResponse(
        UniquePipeName(L"closed"), std::nullopt);
    failures += Expect(
        closed.server_error == ERROR_SUCCESS &&
            closed.client_result.status ==
                SendStatus::short_response &&
            FormatStatus(closed.client_result) == L"Short response",
        "client reports disconnect before response");

    const auto short_response = ExchangeWithRawResponse(
        UniquePipeName(L"short"), std::string{"O"});
    failures += Expect(
        short_response.server_error == ERROR_SUCCESS &&
            short_response.client_result.status ==
                SendStatus::short_response,
        "client reports one-byte response as short");

    const auto unexpected = ExchangeWithRawResponse(
        UniquePipeName(L"unexpected"), std::string{"NO"});
    failures += Expect(
        unexpected.server_error == ERROR_SUCCESS &&
            unexpected.client_result.status ==
                SendStatus::unexpected_response &&
            FormatStatus(unexpected.client_result) ==
                L"Unexpected response",
        "client rejects unknown complete response");

    const auto invalid_name = SendCardNumber(
        nullptr, "1234567890123456");
    failures += Expect(
        invalid_name.status == SendStatus::win32_error &&
            invalid_name.win32_error == ERROR_INVALID_PARAMETER &&
            FormatStatus(invalid_name) == L"Windows error 87",
        "client reports invalid pipe name with Win32 code");

    return failures == 0 ? 0 : 1;
}

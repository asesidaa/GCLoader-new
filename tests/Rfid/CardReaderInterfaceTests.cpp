#include "Rfid/CardReaderInterface.h"
#include "Rfid/CardReaderProtocol.h"
#include "Rfid/State.h"

#include <Windows.h>

#include <array>
#include <cstdint>
#include <expected>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

namespace {

using gc::rfid::CardData;
using gc::rfid::CardScanState;
using gc::rfid::card_reader::CardReaderConnectionOutcome;

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

struct ExchangeResult {
    std::string response;
    DWORD client_error{ERROR_SUCCESS};
    std::expected<CardReaderConnectionOutcome, DWORD> server_result{
        std::unexpected(ERROR_IO_PENDING)};
};

constexpr CardData kSubmittedCard{
    0x04, 0xC2, 0x3D, 0xDA, 0x6F, 0x52, 0x80, 0x00,
    '1', '2', '3', '4', '5', '6', '7', '8',
    '9', '0', '1', '2', '3', '4', '5', '6'};

constexpr CardData kPreservedCard{
    0x04, 0xC2, 0x3D, 0xDA, 0x6F, 0x52, 0x80, 0x00,
    '9', '9', '9', '9', '8', '8', '8', '8',
    '7', '7', '7', '7', '6', '6', '6', '6'};

int Expect(bool condition, const char* name)
{
    if (condition) {
        return 0;
    }
    std::cerr << name << " failed\n";
    return 1;
}

std::wstring UniquePipeName()
{
    return L"\\\\.\\pipe\\GCLoader.CardReader.Tests." +
        std::to_wstring(GetCurrentProcessId()) + L"." +
        std::to_wstring(GetTickCount64());
}

std::expected<UniqueHandle, DWORD> ConnectClient(
    const wchar_t* pipe_name) noexcept
{
    const auto deadline = GetTickCount64() + 5000;
    for (;;) {
        UniqueHandle pipe{CreateFileW(
            pipe_name,
            GENERIC_READ | GENERIC_WRITE,
            0,
            nullptr,
            OPEN_EXISTING,
            0,
            nullptr)};
        if (pipe.Get() != INVALID_HANDLE_VALUE) {
            DWORD mode = PIPE_READMODE_MESSAGE;
            if (!SetNamedPipeHandleState(
                    pipe.Get(), &mode, nullptr, nullptr)) {
                return std::unexpected(GetLastError());
            }
            return pipe;
        }

        const DWORD error = GetLastError();
        if (error != ERROR_FILE_NOT_FOUND &&
            error != ERROR_PIPE_BUSY) {
            return std::unexpected(error);
        }
        if (GetTickCount64() >= deadline) {
            return std::unexpected(error);
        }
        Sleep(1);
    }
}

std::expected<UniqueHandle, DWORD> CreateLowIntegrityToken() noexcept
{
    HANDLE process_token_value{};
    if (!OpenProcessToken(
            GetCurrentProcess(),
            TOKEN_DUPLICATE | TOKEN_QUERY,
            &process_token_value)) {
        return std::unexpected(GetLastError());
    }
    UniqueHandle process_token{process_token_value};

    HANDLE low_integrity_token_value{};
    if (!DuplicateTokenEx(
            process_token.Get(),
            TOKEN_ADJUST_DEFAULT | TOKEN_IMPERSONATE | TOKEN_QUERY,
            nullptr,
            SecurityImpersonation,
            TokenImpersonation,
            &low_integrity_token_value)) {
        return std::unexpected(GetLastError());
    }
    UniqueHandle low_integrity_token{low_integrity_token_value};

    SID_IDENTIFIER_AUTHORITY mandatory_label_authority =
        SECURITY_MANDATORY_LABEL_AUTHORITY;
    PSID low_integrity_sid{};
    if (!AllocateAndInitializeSid(
            &mandatory_label_authority,
            1,
            SECURITY_MANDATORY_LOW_RID,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            &low_integrity_sid)) {
        return std::unexpected(GetLastError());
    }

    TOKEN_MANDATORY_LABEL label{};
    label.Label.Attributes = SE_GROUP_INTEGRITY;
    label.Label.Sid = low_integrity_sid;
    const DWORD label_size = static_cast<DWORD>(
        sizeof(label) + GetLengthSid(low_integrity_sid));
    const bool changed = SetTokenInformation(
        low_integrity_token.Get(),
        TokenIntegrityLevel,
        &label,
        label_size) != FALSE;
    const DWORD change_error = changed
        ? ERROR_SUCCESS
        : GetLastError();
    FreeSid(low_integrity_sid);

    if (!changed) {
        return std::unexpected(change_error);
    }
    return low_integrity_token;
}

std::expected<UniqueHandle, DWORD> ConnectClientAsToken(
    const wchar_t* pipe_name,
    HANDLE token) noexcept
{
    if (!SetThreadToken(nullptr, token)) {
        return std::unexpected(GetLastError());
    }

    auto connected = ConnectClient(pipe_name);
    const bool reverted = RevertToSelf() != FALSE;
    const DWORD revert_error = reverted
        ? ERROR_SUCCESS
        : GetLastError();
    if (!reverted) {
        return std::unexpected(revert_error);
    }
    return std::move(connected);
}

ExchangeResult Exchange(
    const std::wstring& pipe_name,
    CardScanState& card_scan,
    std::string_view request,
    HANDLE client_token = nullptr)
{
    ExchangeResult result;
    std::jthread server{[&] {
        result.server_result =
            gc::rfid::card_reader::ServeOneCardReaderConnection(
                pipe_name.c_str(), card_scan);
    }};

    auto connected = client_token == nullptr
        ? ConnectClient(pipe_name.c_str())
        : ConnectClientAsToken(pipe_name.c_str(), client_token);
    if (!connected) {
        result.client_error = connected.error();
        if (client_token != nullptr) {
            auto release_server = ConnectClient(pipe_name.c_str());
            if (release_server) {
                release_server->Reset();
            }
        }
        server.join();
        return result;
    }

    DWORD bytes_written{};
    if (!WriteFile(
            connected->Get(),
            request.data(),
            static_cast<DWORD>(request.size()),
            &bytes_written,
            nullptr) ||
        bytes_written != request.size()) {
        result.client_error = GetLastError();
        connected->Reset();
        server.join();
        return result;
    }

    std::array<char, 16> response{};
    DWORD bytes_read{};
    if (!ReadFile(
            connected->Get(),
            response.data(),
            static_cast<DWORD>(response.size()),
            &bytes_read,
            nullptr)) {
        result.client_error = GetLastError();
    } else {
        result.response.assign(response.data(), bytes_read);
    }

    server.join();
    return result;
}

std::expected<CardReaderConnectionOutcome, DWORD> DisconnectBeforeRequest(
    const std::wstring& pipe_name,
    CardScanState& card_scan)
{
    std::expected<CardReaderConnectionOutcome, DWORD> server_result{
        std::unexpected(ERROR_IO_PENDING)};
    std::jthread server{[&] {
        server_result =
            gc::rfid::card_reader::ServeOneCardReaderConnection(
                pipe_name.c_str(), card_scan);
    }};

    auto connected = ConnectClient(pipe_name.c_str());
    if (!connected) {
        server.join();
        return std::unexpected(connected.error());
    }
    connected->Reset();
    server.join();
    return server_result;
}

bool HasOutcome(
    const ExchangeResult& result,
    CardReaderConnectionOutcome outcome) noexcept
{
    return result.client_error == ERROR_SUCCESS &&
        result.server_result &&
        *result.server_result == outcome;
}

} // namespace

int main()
{
    int failures = 0;
    const auto pipe_name = UniquePipeName();
    CardScanState card_scan;

    const auto accepted = Exchange(
        pipe_name, card_scan, "1234567890123456");
    const auto first_scan = card_scan.Snapshot();
    failures += Expect(
        HasOutcome(
            accepted, CardReaderConnectionOutcome::accepted) &&
            accepted.response == "OK" &&
            first_scan.present &&
            first_scan.card_data == kSubmittedCard,
        "valid request publishes supplied card and returns OK");

    failures += Expect(
        card_scan.Consume(first_scan.generation),
        "first valid request can be consumed");

    auto low_integrity_token = CreateLowIntegrityToken();
    failures += Expect(
        low_integrity_token.has_value(),
        "low-integrity test token can be created");
    if (low_integrity_token) {
        CardScanState low_integrity_scan;
        const auto low_integrity_exchange = Exchange(
            pipe_name,
            low_integrity_scan,
            "1234567890123456",
            low_integrity_token->Get());
        const auto low_integrity_snapshot =
            low_integrity_scan.Snapshot();
        failures += Expect(
            HasOutcome(
                low_integrity_exchange,
                CardReaderConnectionOutcome::accepted) &&
                low_integrity_exchange.response == "OK" &&
                low_integrity_snapshot.present &&
                low_integrity_snapshot.card_data == kSubmittedCard,
            "lower-integrity authenticated client can submit a card");
    }

    const auto repeated = Exchange(
        pipe_name, card_scan, "1234567890123456");
    const auto repeated_scan = card_scan.Snapshot();
    failures += Expect(
        HasOutcome(
            repeated, CardReaderConnectionOutcome::accepted) &&
            repeated.response == "OK" &&
            repeated_scan.present &&
            repeated_scan.card_data == kSubmittedCard &&
            repeated_scan.generation != first_scan.generation,
        "repeated identical request creates a new scan");

    card_scan.Arm(kPreservedCard);
    const auto preserved = card_scan.Snapshot();
    for (const auto request : {
             std::string{"123456789012345"},
             std::string{"12345678901234567"},
             std::string(32, '1'),
             std::string{"123456789012345X"},
         }) {
        const auto rejected = Exchange(
            pipe_name, card_scan, request);
        const auto after_rejection = card_scan.Snapshot();
        failures += Expect(
            HasOutcome(
                rejected,
                CardReaderConnectionOutcome::invalid) &&
                rejected.response == "INVALID" &&
                after_rejection.present &&
                after_rejection.generation == preserved.generation &&
                after_rejection.card_data == kPreservedCard,
            "malformed request returns INVALID without changing state");
    }

    const auto disconnected = DisconnectBeforeRequest(
        pipe_name, card_scan);
    const auto after_disconnect = card_scan.Snapshot();
    failures += Expect(
        disconnected &&
            *disconnected ==
                CardReaderConnectionOutcome::client_disconnected &&
            after_disconnect.present &&
            after_disconnect.generation == preserved.generation &&
            after_disconnect.card_data == kPreservedCard,
        "disconnect before request leaves pending scan unchanged");

    const auto invalid_then_valid = Exchange(
        pipe_name, card_scan, "123456789012345X");
    const auto valid_after_invalid = Exchange(
        pipe_name, card_scan, "1234567890123456");
    const auto final_scan = card_scan.Snapshot();
    failures += Expect(
        HasOutcome(
            invalid_then_valid,
            CardReaderConnectionOutcome::invalid) &&
            invalid_then_valid.response == "INVALID" &&
            HasOutcome(
                valid_after_invalid,
                CardReaderConnectionOutcome::accepted) &&
            valid_after_invalid.response == "OK" &&
            final_scan.card_data == kSubmittedCard,
        "server accepts a later connection after invalid input");

    return failures == 0 ? 0 : 1;
}

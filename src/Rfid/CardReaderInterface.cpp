#include "Rfid/CardReaderInterface.h"
#include "Platform/Win32/UniqueHandle.h"

#include "Rfid/CardData.h"
#include "Rfid/CardReaderProtocol.h"

#include <sddl.h>

#include <array>
#include <new>
#include <optional>
#include <string_view>
#include <utility>

namespace gc::rfid::card_reader {
namespace {

constexpr wchar_t kPipeSecuritySddl[] =
    L"D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;GRGW;;;AU)"
    L"S:(ML;;NW;;;LW)";

class LocalSecurityDescriptor {
public:
    explicit LocalSecurityDescriptor(PSECURITY_DESCRIPTOR value) noexcept
        : value_{value}
    {
    }

    LocalSecurityDescriptor(const LocalSecurityDescriptor&) = delete;
    LocalSecurityDescriptor& operator=(
        const LocalSecurityDescriptor&) = delete;

    LocalSecurityDescriptor(LocalSecurityDescriptor&& other) noexcept
        : value_{std::exchange(other.value_, nullptr)}
    {
    }

    LocalSecurityDescriptor& operator=(
        LocalSecurityDescriptor&& other) noexcept
    {
        if (this != &other) {
            Reset();
            value_ = std::exchange(other.value_, nullptr);
        }
        return *this;
    }

    ~LocalSecurityDescriptor()
    {
        Reset();
    }

    [[nodiscard]] PSECURITY_DESCRIPTOR Get() const noexcept
    {
        return value_;
    }

private:
    void Reset() noexcept
    {
        if (value_ != nullptr) {
            LocalFree(value_);
            value_ = nullptr;
        }
    }

    PSECURITY_DESCRIPTOR value_{};
};

std::expected<LocalSecurityDescriptor, DWORD>
CreatePipeSecurityDescriptor() noexcept
{
    PSECURITY_DESCRIPTOR descriptor{};
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            kPipeSecuritySddl,
            SDDL_REVISION_1,
            &descriptor,
            nullptr)) {
        return std::unexpected(GetLastError());
    }
    return LocalSecurityDescriptor{descriptor};
}

class PipeHandle {
public:
    explicit PipeHandle(HANDLE value) noexcept
        : value_{value}
    {
    }

    PipeHandle(const PipeHandle&) = delete;
    PipeHandle& operator=(const PipeHandle&) = delete;

    ~PipeHandle()
    {
        if (!value_) {
            return;
        }
        if (connected_) {
            DisconnectNamedPipe(value_.get());
        }

    }

    [[nodiscard]] HANDLE Get() const noexcept
    {
        return value_.get();
    }

    void MarkConnected() noexcept
    {
        connected_ = true;
    }

private:
    gc::platform::win32::UniqueHandle value_;
    bool connected_{};
};

bool IsClientDisconnect(DWORD error) noexcept
{
    return error == ERROR_BROKEN_PIPE ||
        error == ERROR_NO_DATA ||
        error == ERROR_PIPE_NOT_CONNECTED;
}

std::expected<void, DWORD> DrainCurrentMessage(HANDLE pipe) noexcept
{
    std::array<char, 32> discarded{};
    for (;;) {
        DWORD bytes_read{};
        if (ReadFile(
                pipe,
                discarded.data(),
                static_cast<DWORD>(discarded.size()),
                &bytes_read,
                nullptr)) {
            return {};
        }

        const DWORD error = GetLastError();
        if (error != ERROR_MORE_DATA) {
            return std::unexpected(error);
        }
    }
}

std::expected<void, DWORD> WriteResponse(
    HANDLE pipe,
    std::string_view response) noexcept
{
    DWORD bytes_written{};
    if (!WriteFile(
            pipe,
            response.data(),
            static_cast<DWORD>(response.size()),
            &bytes_written,
            nullptr)) {
        return std::unexpected(GetLastError());
    }
    if (bytes_written != response.size()) {
        return std::unexpected(ERROR_WRITE_FAULT);
    }
    if (!FlushFileBuffers(pipe)) {
        return std::unexpected(GetLastError());
    }
    return {};
}

} // namespace

std::expected<CardReaderConnectionOutcome, DWORD>
ServeOneCardReaderConnection(
    const wchar_t* pipe_name,
    CardScanState& card_scan) noexcept
{
    try {
        if (pipe_name == nullptr || pipe_name[0] == L'\0') {
            return std::unexpected(ERROR_INVALID_PARAMETER);
        }

        auto descriptor = CreatePipeSecurityDescriptor();
        if (!descriptor) {
            return std::unexpected(descriptor.error());
        }
        SECURITY_ATTRIBUTES security_attributes{
            .nLength = sizeof(SECURITY_ATTRIBUTES),
            .lpSecurityDescriptor = descriptor->Get(),
            .bInheritHandle = FALSE,
        };

        PipeHandle pipe{CreateNamedPipeW(
            pipe_name,
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_MESSAGE |
                PIPE_READMODE_MESSAGE |
                PIPE_WAIT |
                PIPE_REJECT_REMOTE_CLIENTS,
            1,
            static_cast<DWORD>(kRequestByteCount),
            static_cast<DWORD>(kRequestByteCount + 1),
            0,
            &security_attributes)};
        if (pipe.Get() == nullptr) {
            return std::unexpected(GetLastError());
        }

        if (!ConnectNamedPipe(pipe.Get(), nullptr)) {
            const DWORD error = GetLastError();
            if (error != ERROR_PIPE_CONNECTED) {
                if (IsClientDisconnect(error)) {
                    return CardReaderConnectionOutcome::client_disconnected;
                }
                return std::unexpected(error);
            }
        }
        pipe.MarkConnected();

        std::array<char, kRequestByteCount + 1> request{};
        DWORD bytes_read{};
        const bool read = ReadFile(
            pipe.Get(),
            request.data(),
            static_cast<DWORD>(request.size()),
            &bytes_read,
            nullptr) != FALSE;

        bool invalid = false;
        if (!read) {
            const DWORD error = GetLastError();
            if (error == ERROR_MORE_DATA) {
                invalid = true;
                const auto drained = DrainCurrentMessage(pipe.Get());
                if (!drained) {
                    if (IsClientDisconnect(drained.error())) {
                        return CardReaderConnectionOutcome::client_disconnected;
                    }
                    return std::unexpected(drained.error());
                }
            } else if (IsClientDisconnect(error)) {
                return CardReaderConnectionOutcome::client_disconnected;
            } else {
                return std::unexpected(error);
            }
        } else if (bytes_read != kRequestByteCount) {
            invalid = true;
        }

        std::optional<CardData> parsed;
        if (!invalid) {
            parsed = ParseCardNumber(std::string_view{
                request.data(), kRequestByteCount});
            invalid = !parsed.has_value();
        }

        const auto response = invalid
            ? kInvalidResponse
            : kAcceptedResponse;
        if (!invalid) {
            card_scan.Arm(*parsed);
        }

        const auto written = WriteResponse(pipe.Get(), response);
        if (!written) {
            if (IsClientDisconnect(written.error())) {
                return CardReaderConnectionOutcome::client_disconnected;
            }
            return std::unexpected(written.error());
        }

        return invalid
            ? CardReaderConnectionOutcome::invalid
            : CardReaderConnectionOutcome::accepted;
    } catch (const std::bad_alloc&) {
        return std::unexpected(ERROR_NOT_ENOUGH_MEMORY);
    } catch (...) {
        return std::unexpected(ERROR_GEN_FAILURE);
    }
}

} // namespace gc::rfid::card_reader

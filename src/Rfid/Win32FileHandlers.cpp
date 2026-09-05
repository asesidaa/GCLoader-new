#include "Rfid/Win32FileHandlers.h"
#include "Rfid/Trace.h"
#include "plog/Log.h"
#include <limits>
#include <span>
#include <string_view>

namespace gc::rfid {
namespace {
using namespace win32_hooks;
PreCallDecision<BOOL> Failed(DWORD error) noexcept { return CompleteCall<BOOL>{FALSE, error}; }
PreCallDecision<HANDLE> CreateFileA(void* state, CreateFileAContext& context) noexcept {
    return GuardPreCall<HANDLE>(INVALID_HANDLE_VALUE, [&]() -> PreCallDecision<HANDLE> {
        auto& runtime = *static_cast<Runtime*>(state);
        const auto file_name = context.original_path;

        if (file_name != nullptr && std::string_view{file_name} == "COM2") {
            const auto opened = runtime.OpenCom2();
            if (!opened) {
                PLOG_ERROR
                    << "RFID COM2 trace api=CreateFileA result=failure error="
                    << opened.error();
                return CompleteCall<HANDLE>{INVALID_HANDLE_VALUE, opened.error()};
            }
            return CompleteCall<HANDLE>{*opened, GetLastError()};
        }

        return ContinueCall{};
    });
}
PreCallDecision<HANDLE> CreateFileW(void* state, CreateFileWContext& context) noexcept {
    return GuardPreCall<HANDLE>(INVALID_HANDLE_VALUE, [&]() -> PreCallDecision<HANDLE> {
        auto& runtime = *static_cast<Runtime*>(state);
        const auto file_name = context.original_path;

        if (file_name != nullptr && std::wstring_view{file_name} == L"COM2") {
            const auto opened = runtime.OpenCom2();
            if (!opened) {
                PLOG_ERROR
                    << "RFID COM2 trace api=CreateFileW result=failure error="
                    << opened.error();
                return CompleteCall<HANDLE>{INVALID_HANDLE_VALUE, opened.error()};
            }
            return CompleteCall<HANDLE>{*opened, GetLastError()};
        }

        return ContinueCall{};
    });
}
PreCallDecision<BOOL> WriteFile(void* state, WriteFileContext& context) noexcept {
    return GuardPreCall<BOOL>(FALSE, [&]() -> PreCallDecision<BOOL> {
        auto& runtime = *static_cast<Runtime*>(state);
        const auto& [file, buffer, bytes_to_write, bytes_written, overlapped] = context;

        if (file != gc::rfid::EmulatedComHandle()) { return ContinueCall{}; }
        if (bytes_written != nullptr) {
            *bytes_written = 0;
        }
        if (bytes_written == nullptr || overlapped != nullptr ||
            (buffer == nullptr && bytes_to_write != 0)) {
            PLOG_ERROR
                << "RFID COM2 trace api=WriteFile result=failure error="
                << ERROR_INVALID_PARAMETER
                << " requested=" << bytes_to_write
                << " bytes_written_ptr=" << bytes_written
                << " overlapped=" << overlapped
                << " buffer=" << buffer;
            return Failed(ERROR_INVALID_PARAMETER);
        }

        const auto bytes = std::span<const std::byte>{
            static_cast<const std::byte*>(buffer),
            static_cast<std::size_t>(bytes_to_write)};
        const auto result = runtime.port().Write(bytes, false);
        if (!result) {
            PLOG_ERROR
                << "RFID COM2 trace api=WriteFile result=failure error="
                << result.error()
                << " requested=" << bytes_to_write
                << " bytes=" << gc::rfid::trace::FormatBytes(bytes);
            return Failed(result.error());
        }
        if (*result > std::numeric_limits<DWORD>::max()) {
            PLOG_ERROR
                << "RFID COM2 trace api=WriteFile result=failure error="
                << ERROR_ARITHMETIC_OVERFLOW
                << " transferred=" << *result;
            return Failed(ERROR_ARITHMETIC_OVERFLOW);
        }
        *bytes_written = static_cast<DWORD>(*result);
        return CompleteCall<BOOL>{TRUE, GetLastError()};
    });
}
PreCallDecision<BOOL> ReadFile(void* state, ReadFileContext& context) noexcept {
    return GuardPreCall<BOOL>(FALSE, [&]() -> PreCallDecision<BOOL> {
        auto& runtime = *static_cast<Runtime*>(state);
        const auto& [file, buffer, bytes_to_read, bytes_read, overlapped] = context;

        if (file != gc::rfid::EmulatedComHandle()) { return ContinueCall{}; }
        if (bytes_read != nullptr) {
            *bytes_read = 0;
        }
        if (bytes_read == nullptr || overlapped != nullptr ||
            (buffer == nullptr && bytes_to_read != 0)) {
            PLOG_ERROR
                << "RFID COM2 trace api=ReadFile result=failure error="
                << ERROR_INVALID_PARAMETER
                << " requested=" << bytes_to_read
                << " bytes_read_ptr=" << bytes_read
                << " overlapped=" << overlapped
                << " buffer=" << buffer;
            return Failed(ERROR_INVALID_PARAMETER);
        }

        const auto destination = std::span<std::byte>{
            static_cast<std::byte*>(buffer),
            static_cast<std::size_t>(bytes_to_read)};
        const auto result = runtime.port().Read(destination, false);
        if (!result) {
            PLOG_ERROR
                << "RFID COM2 trace api=ReadFile result=failure error="
                << result.error()
                << " requested=" << bytes_to_read;
            return Failed(result.error());
        }
        if (*result > std::numeric_limits<DWORD>::max()) {
            PLOG_ERROR
                << "RFID COM2 trace api=ReadFile result=failure error="
                << ERROR_ARITHMETIC_OVERFLOW
                << " transferred=" << *result;
            return Failed(ERROR_ARITHMETIC_OVERFLOW);
        }
        *bytes_read = static_cast<DWORD>(*result);
        return CompleteCall<BOOL>{TRUE, GetLastError()};
    });
}
PreCallDecision<BOOL> CloseHandle(void* state, CloseHandleContext& context) noexcept {
    return GuardPreCall<BOOL>(FALSE, [&]() -> PreCallDecision<BOOL> {
        auto& runtime = *static_cast<Runtime*>(state);
        const auto& [object] = context;

        if (object != gc::rfid::EmulatedComHandle()) { return ContinueCall{}; }
        runtime.CloseCom2();
        return CompleteCall<BOOL>{TRUE, GetLastError()};
    });
}
}
std::expected<void, win32_hooks::RegistrationError> AddWin32FileHandlers(
    win32_hooks::Kernel32Dispatcher& dispatcher, Runtime& runtime) noexcept {
    if (const auto result = dispatcher.create_file_a.AddPre(
        {"RFID", "CreateFileA"}, &runtime, CreateFileA); !result) return result;
    if (const auto result = dispatcher.create_file_w.AddPre(
        {"RFID", "CreateFileW"}, &runtime, CreateFileW); !result) return result;
    if (const auto result = dispatcher.write_file.AddPre(
        {"RFID", "WriteFile"}, &runtime, WriteFile); !result) return result;
    if (const auto result = dispatcher.read_file.AddPre(
        {"RFID", "ReadFile"}, &runtime, ReadFile); !result) return result;
    if (const auto result = dispatcher.close_handle.AddPre(
        {"RFID", "CloseHandle"}, &runtime, CloseHandle); !result) return result;
    return {};
}
}

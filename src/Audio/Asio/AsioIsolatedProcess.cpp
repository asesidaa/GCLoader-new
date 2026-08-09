// SPDX-License-Identifier: CC0-1.0

#include "Audio/Asio/AsioIsolatedProcess.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <new>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace gc::audio {
namespace {

class UniqueHandle {
public:
    UniqueHandle() = default;
    explicit UniqueHandle(HANDLE handle) noexcept : handle_(handle) {}
    ~UniqueHandle() { reset(); }

    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;
    UniqueHandle(UniqueHandle&& other) noexcept
        : handle_(other.release()) {}
    UniqueHandle& operator=(UniqueHandle&& other) noexcept {
        if (this != &other) {
            reset(other.release());
        }
        return *this;
    }

    [[nodiscard]] HANDLE get() const noexcept { return handle_; }
    [[nodiscard]] bool valid() const noexcept {
        return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE;
    }
    HANDLE release() noexcept {
        return std::exchange(handle_, nullptr);
    }
    void reset(HANDLE handle = nullptr) noexcept {
        if (valid()) {
            CloseHandle(handle_);
        }
        handle_ = handle;
    }

private:
    HANDLE handle_{};
};

class AttributeList {
public:
    ~AttributeList() {
        if (list_ != nullptr) {
            if (initialized_) {
                DeleteProcThreadAttributeList(list_);
            }
            HeapFree(GetProcessHeap(), 0, list_);
        }
    }

    bool Initialize() noexcept {
        SIZE_T bytes{};
        InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
        if (bytes == 0) {
            return false;
        }
        list_ = static_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(
            HeapAlloc(GetProcessHeap(), 0, bytes));
        initialized_ = list_ != nullptr &&
            InitializeProcThreadAttributeList(list_, 1, 0, &bytes) != FALSE;
        return initialized_;
    }

    bool SetInheritedHandles(std::span<const HANDLE> handles) noexcept {
        return list_ != nullptr &&
            UpdateProcThreadAttribute(
                list_,
                0,
                PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                const_cast<HANDLE*>(handles.data()),
                handles.size_bytes(),
                nullptr,
                nullptr) != FALSE;
    }

    [[nodiscard]] LPPROC_THREAD_ATTRIBUTE_LIST get() const noexcept {
        return list_;
    }

private:
    LPPROC_THREAD_ATTRIBUTE_LIST list_{};
    bool initialized_{};
};

AsioFailure Failure(
    AsioFailureStage stage,
    AsioResultDomain domain,
    std::int64_t result,
    std::string detail) {
    return {
        .stage = stage,
        .domain = domain,
        .result = result,
        .detail = std::move(detail),
    };
}

AsioIsolatedProcessOutcome ProcessOutcome(
    AsioIsolatedProcessStatus status,
    DWORD error) noexcept {
    return {
        .status = status,
        .win32_error = error,
    };
}

void TerminateSuspendedProcess(HANDLE process) noexcept {
    if (process != nullptr) {
        TerminateProcess(process, ERROR_PROCESS_ABORTED);
        WaitForSingleObject(process, INFINITE);
    }
}

bool WriteAll(
    HANDLE pipe,
    std::span<const std::byte> bytes,
    DWORD* error) noexcept {
    std::size_t offset{};
    while (offset < bytes.size()) {
        const auto remaining = bytes.size() - offset;
        const auto chunk = static_cast<DWORD>(std::min<std::size_t>(
            remaining,
            std::numeric_limits<DWORD>::max()));
        DWORD written{};
        if (!WriteFile(
                pipe,
                bytes.data() + offset,
                chunk,
                &written,
                nullptr) ||
            written == 0) {
            *error = GetLastError();
            return false;
        }
        offset += written;
    }
    return true;
}

struct ReaderContext {
    HANDLE pipe{};
    std::uint32_t maximum{};
    std::vector<std::byte> output;
    DWORD error{};
    bool overflow{};
};

DWORD WINAPI DrainOutput(void* raw_context) noexcept {
    auto& context = *static_cast<ReaderContext*>(raw_context);
    try {
        std::array<std::byte, 4096> chunk{};
        for (;;) {
            DWORD read{};
            if (!ReadFile(
                    context.pipe,
                    chunk.data(),
                    static_cast<DWORD>(chunk.size()),
                    &read,
                    nullptr)) {
                const auto error = GetLastError();
                if (error != ERROR_BROKEN_PIPE) {
                    context.error = error;
                }
                break;
            }
            if (read == 0) {
                break;
            }
            const auto maximum = static_cast<std::size_t>(context.maximum);
            if (context.output.size() > maximum ||
                read > maximum - context.output.size()) {
                context.overflow = true;
                continue;
            }
            context.output.insert(
                context.output.end(),
                chunk.begin(),
                chunk.begin() + read);
        }
    } catch (const std::bad_alloc&) {
        context.error = ERROR_NOT_ENOUGH_MEMORY;
    } catch (...) {
        context.error = ERROR_INVALID_DATA;
    }
    return 0;
}

bool ConfigureKillOnCloseJob(HANDLE job) noexcept {
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags =
        JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    return SetInformationJobObject(
        job,
        JobObjectExtendedLimitInformation,
        &limits,
        sizeof(limits)) != FALSE;
}

bool HasValidWaitPolicy(const AsioIsolatedProcessRequest& request) noexcept {
    switch (request.mode) {
    case AsioInternalMode::probe:
        return request.cancellation_event == nullptr &&
            request.timeout.count() >= 0 &&
            request.timeout.count() <= std::numeric_limits<DWORD>::max();
    case AsioInternalMode::control_panel: {
        if (request.timeout.count() != 0 ||
            request.cancellation_event == nullptr ||
            request.cancellation_event == INVALID_HANDLE_VALUE) {
            return false;
        }
        DWORD flags{};
        return GetHandleInformation(request.cancellation_event, &flags) !=
            FALSE;
    }
    }
    return false;
}

AsioIsolatedProcessOutcome FinishTerminated(
    AsioIsolatedProcessStatus status,
    DWORD error,
    UniqueHandle& job,
    HANDLE process,
    HANDLE reader_thread,
    ReaderContext& reader) {
    job.reset();
    WaitForSingleObject(process, INFINITE);
    WaitForSingleObject(reader_thread, INFINITE);
    return {
        .status = status,
        .win32_error = error,
        .standard_output = std::move(reader.output),
    };
}

} // namespace

std::expected<std::filesystem::path, AsioFailure>
ProductionAsioIsolatedProcessActions::CurrentExecutablePath() noexcept {
    try {
        constexpr std::size_t maximum_path_characters = 32'768;
        std::vector<wchar_t> buffer(MAX_PATH);
        for (;;) {
            SetLastError(ERROR_SUCCESS);
            const auto copied = GetModuleFileNameW(
                nullptr,
                buffer.data(),
                static_cast<DWORD>(buffer.size()));
            if (copied == 0) {
                return std::unexpected(Failure(
                    AsioFailureStage::process_launch,
                    AsioResultDomain::win32,
                    GetLastError(),
                    "GetModuleFileNameW(ConfigGUI) failed"));
            }
            if (copied < buffer.size()) {
                return std::filesystem::path{
                    std::wstring_view{buffer.data(), copied}};
            }
            if (buffer.size() == maximum_path_characters) {
                break;
            }
            buffer.resize(std::min(
                buffer.size() * 2,
                maximum_path_characters));
        }
        return std::unexpected(Failure(
            AsioFailureStage::process_launch,
            AsioResultDomain::win32,
            ERROR_INSUFFICIENT_BUFFER,
            "ConfigGUI executable path exceeds the Win32 path bound"));
    } catch (...) {
        return std::unexpected(Failure(
            AsioFailureStage::process_launch,
            AsioResultDomain::none,
            0,
            "ConfigGUI executable path resolution failed"));
    }
}

AsioIsolatedProcessOutcome ProductionAsioIsolatedProcessActions::Run(
    const AsioIsolatedProcessRequest& request) noexcept {
    try {
        constexpr DWORD required_flags =
            CREATE_SUSPENDED | CREATE_NO_WINDOW |
            EXTENDED_STARTUPINFO_PRESENT;
        const auto argument = AsioInternalModeArgument(request.mode);
        if (!request.executable_path.is_absolute() ||
            !request.executable_path.has_filename() || argument.empty() ||
            request.use_shell || !request.inherit_handles ||
            !request.restricted_handle_list || !request.kill_on_job_close ||
            request.creation_flags != required_flags ||
            request.maximum_stdout_bytes == 0 ||
            !HasValidWaitPolicy(request)) {
            return ProcessOutcome(
                AsioIsolatedProcessStatus::create_failed,
                ERROR_INVALID_PARAMETER);
        }

        SECURITY_ATTRIBUTES inheritable{
            sizeof(SECURITY_ATTRIBUTES),
            nullptr,
            TRUE,
        };
        HANDLE child_stdin_raw{};
        HANDLE parent_stdin_raw{};
        if (!CreatePipe(
                &child_stdin_raw,
                &parent_stdin_raw,
                &inheritable,
                0)) {
            return ProcessOutcome(
                AsioIsolatedProcessStatus::create_failed,
                GetLastError());
        }
        UniqueHandle child_stdin{child_stdin_raw};
        UniqueHandle parent_stdin{parent_stdin_raw};
        if (!SetHandleInformation(
                parent_stdin.get(),
                HANDLE_FLAG_INHERIT,
                0)) {
            return ProcessOutcome(
                AsioIsolatedProcessStatus::create_failed,
                GetLastError());
        }

        HANDLE parent_stdout_raw{};
        HANDLE child_stdout_raw{};
        if (!CreatePipe(
                &parent_stdout_raw,
                &child_stdout_raw,
                &inheritable,
                0)) {
            return ProcessOutcome(
                AsioIsolatedProcessStatus::create_failed,
                GetLastError());
        }
        UniqueHandle parent_stdout{parent_stdout_raw};
        UniqueHandle child_stdout{child_stdout_raw};
        if (!SetHandleInformation(
                parent_stdout.get(),
                HANDLE_FLAG_INHERIT,
                0)) {
            return ProcessOutcome(
                AsioIsolatedProcessStatus::create_failed,
                GetLastError());
        }

        UniqueHandle child_stderr{CreateFileW(
            L"NUL",
            GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            &inheritable,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr)};
        if (!child_stderr.valid()) {
            return ProcessOutcome(
                AsioIsolatedProcessStatus::create_failed,
                GetLastError());
        }

        AttributeList attributes;
        if (!attributes.Initialize()) {
            return ProcessOutcome(
                AsioIsolatedProcessStatus::create_failed,
                GetLastError());
        }
        const std::array inherited_handles{
            child_stdin.get(),
            child_stdout.get(),
            child_stderr.get(),
        };
        if (!attributes.SetInheritedHandles(inherited_handles)) {
            return ProcessOutcome(
                AsioIsolatedProcessStatus::create_failed,
                GetLastError());
        }

        STARTUPINFOEXW startup{};
        startup.StartupInfo.cb = sizeof(startup);
        startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
        startup.StartupInfo.hStdInput = child_stdin.get();
        startup.StartupInfo.hStdOutput = child_stdout.get();
        startup.StartupInfo.hStdError = child_stderr.get();
        startup.lpAttributeList = attributes.get();

        const auto executable = request.executable_path.native();
        std::wstring command_line =
            L"\"" + executable + L"\" " + std::wstring{argument};
        std::vector<wchar_t> mutable_command(
            command_line.begin(),
            command_line.end());
        mutable_command.push_back(L'\0');
        const auto current_directory =
            request.executable_path.parent_path().native();
        PROCESS_INFORMATION process_info{};
        if (!CreateProcessW(
                executable.c_str(),
                mutable_command.data(),
                nullptr,
                nullptr,
                TRUE,
                request.creation_flags,
                nullptr,
                current_directory.c_str(),
                &startup.StartupInfo,
                &process_info)) {
            return ProcessOutcome(
                AsioIsolatedProcessStatus::create_failed,
                GetLastError());
        }
        UniqueHandle process{process_info.hProcess};
        UniqueHandle primary_thread{process_info.hThread};
        child_stdin.reset();
        child_stdout.reset();
        child_stderr.reset();

        UniqueHandle job{CreateJobObjectW(nullptr, nullptr)};
        if (!job.valid() || !ConfigureKillOnCloseJob(job.get()) ||
            !AssignProcessToJobObject(job.get(), process.get())) {
            const auto error = GetLastError();
            TerminateSuspendedProcess(process.get());
            return ProcessOutcome(
                AsioIsolatedProcessStatus::job_failed,
                error);
        }

        ReaderContext reader{
            parent_stdout.get(),
            request.maximum_stdout_bytes,
        };
        UniqueHandle reader_thread{CreateThread(
            nullptr,
            0,
            &DrainOutput,
            &reader,
            0,
            nullptr)};
        if (!reader_thread.valid()) {
            const auto error = GetLastError();
            job.reset();
            WaitForSingleObject(process.get(), INFINITE);
            return ProcessOutcome(
                AsioIsolatedProcessStatus::io_failed,
                error);
        }

        if (ResumeThread(primary_thread.get()) == static_cast<DWORD>(-1)) {
            const auto error = GetLastError();
            parent_stdin.reset();
            job.reset();
            WaitForSingleObject(process.get(), INFINITE);
            WaitForSingleObject(reader_thread.get(), INFINITE);
            return ProcessOutcome(
                AsioIsolatedProcessStatus::create_failed,
                error);
        }
        primary_thread.reset();

        DWORD write_error{};
        const bool wrote = WriteAll(
            parent_stdin.get(),
            request.standard_input,
            &write_error);
        parent_stdin.reset();
        if (!wrote) {
            return FinishTerminated(
                AsioIsolatedProcessStatus::io_failed,
                write_error,
                job,
                process.get(),
                reader_thread.get(),
                reader);
        }

        DWORD wait{};
        if (request.mode == AsioInternalMode::probe) {
            wait = WaitForSingleObject(
                process.get(),
                static_cast<DWORD>(request.timeout.count()));
            if (wait == WAIT_TIMEOUT) {
                return FinishTerminated(
                    AsioIsolatedProcessStatus::timed_out,
                    WAIT_TIMEOUT,
                    job,
                    process.get(),
                    reader_thread.get(),
                    reader);
            }
        } else {
            const std::array wait_handles{
                process.get(),
                request.cancellation_event,
            };
            wait = WaitForMultipleObjects(
                static_cast<DWORD>(wait_handles.size()),
                wait_handles.data(),
                FALSE,
                INFINITE);
            if (wait == WAIT_OBJECT_0 + 1) {
                return FinishTerminated(
                    AsioIsolatedProcessStatus::cancelled,
                    ERROR_OPERATION_ABORTED,
                    job,
                    process.get(),
                    reader_thread.get(),
                    reader);
            }
        }
        if (wait != WAIT_OBJECT_0) {
            const auto error = GetLastError();
            return FinishTerminated(
                AsioIsolatedProcessStatus::io_failed,
                error,
                job,
                process.get(),
                reader_thread.get(),
                reader);
        }

        WaitForSingleObject(reader_thread.get(), INFINITE);
        DWORD exit_code{};
        if (!GetExitCodeProcess(process.get(), &exit_code)) {
            return ProcessOutcome(
                AsioIsolatedProcessStatus::io_failed,
                GetLastError());
        }
        if (reader.error != ERROR_SUCCESS) {
            return ProcessOutcome(
                AsioIsolatedProcessStatus::io_failed,
                reader.error);
        }
        if (reader.overflow) {
            return ProcessOutcome(
                AsioIsolatedProcessStatus::output_too_large,
                ERROR_BUFFER_OVERFLOW);
        }
        return {
            .status = AsioIsolatedProcessStatus::exited,
            .win32_error = ERROR_SUCCESS,
            .exit_code = exit_code,
            .standard_output = std::move(reader.output),
        };
    } catch (const std::bad_alloc&) {
        return ProcessOutcome(
            AsioIsolatedProcessStatus::io_failed,
            ERROR_NOT_ENOUGH_MEMORY);
    } catch (...) {
        return ProcessOutcome(
            AsioIsolatedProcessStatus::io_failed,
            ERROR_INVALID_DATA);
    }
}

} // namespace gc::audio

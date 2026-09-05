#include "Win32Hooks/Kernel32Detours.h"
#include "Win32Hooks/Kernel32Dispatcher.h"

namespace gc::win32_hooks {
namespace {
template <class Result, class Invoke>
Result AtWin32Boundary(Result failure, Invoke&& invoke) noexcept {
    const DWORD incoming_error = GetLastError();
    try {
        const auto outcome = invoke(incoming_error);
        // Callback contexts (including replacement strings) have been destroyed.
        SetLastError(outcome.last_error);
        return outcome.result;
    } catch (...) {
        SetLastError(ERROR_UNHANDLED_EXCEPTION);
        return failure;
    }
}
}
HANDLE WINAPI CreateFileADetour(LPCSTR file_name, DWORD desired_access, DWORD share_mode, LPSECURITY_ATTRIBUTES security_attributes, DWORD creation_disposition, DWORD flags_and_attributes, HANDLE template_file) noexcept {
    return AtWin32Boundary<HANDLE>(INVALID_HANDLE_VALUE, [&](DWORD incoming_error) {
        CreateFileAContext context{PathArgumentA{file_name}, {desired_access, share_mode, security_attributes, creation_disposition, flags_and_attributes, template_file}};
        const HANDLE result = Kernel32Dispatcher::ProcessLifetime().Invoke(context, incoming_error);
        return CallOutcome<HANDLE>{result, GetLastError()};
    });
}
HANDLE WINAPI CreateFileWDetour(LPCWSTR file_name, DWORD desired_access, DWORD share_mode, LPSECURITY_ATTRIBUTES security_attributes, DWORD creation_disposition, DWORD flags_and_attributes, HANDLE template_file) noexcept {
    return AtWin32Boundary<HANDLE>(INVALID_HANDLE_VALUE, [&](DWORD incoming_error) {
        CreateFileWContext context{PathArgumentW{file_name}, {desired_access, share_mode, security_attributes, creation_disposition, flags_and_attributes, template_file}};
        const HANDLE result = Kernel32Dispatcher::ProcessLifetime().Invoke(context, incoming_error);
        return CallOutcome<HANDLE>{result, GetLastError()};
    });
}
BOOL WINAPI WriteFileDetour(HANDLE file, LPCVOID buffer, DWORD bytes_to_write, LPDWORD bytes_written, LPOVERLAPPED overlapped) noexcept {
    return AtWin32Boundary<BOOL>(FALSE, [&](DWORD incoming_error) {
        WriteFileContext context{file, buffer, bytes_to_write, bytes_written, overlapped};
        const BOOL result = Kernel32Dispatcher::ProcessLifetime().Invoke(context, incoming_error);
        return CallOutcome<BOOL>{result, GetLastError()};
    });
}
BOOL WINAPI ReadFileDetour(HANDLE file, LPVOID buffer, DWORD bytes_to_read, LPDWORD bytes_read, LPOVERLAPPED overlapped) noexcept {
    return AtWin32Boundary<BOOL>(FALSE, [&](DWORD incoming_error) {
        ReadFileContext context{file, buffer, bytes_to_read, bytes_read, overlapped};
        const BOOL result = Kernel32Dispatcher::ProcessLifetime().Invoke(context, incoming_error);
        return CallOutcome<BOOL>{result, GetLastError()};
    });
}
BOOL WINAPI FlushFileBuffersDetour(HANDLE file) noexcept {
    return AtWin32Boundary<BOOL>(FALSE, [&](DWORD incoming_error) {
        FlushFileBuffersContext context{file};
        const BOOL result = Kernel32Dispatcher::ProcessLifetime().Invoke(context, incoming_error);
        return CallOutcome<BOOL>{result, GetLastError()};
    });
}
BOOL WINAPI CloseHandleDetour(HANDLE object) noexcept {
    return AtWin32Boundary<BOOL>(FALSE, [&](DWORD incoming_error) {
        CloseHandleContext context{object};
        const BOOL result = Kernel32Dispatcher::ProcessLifetime().Invoke(context, incoming_error);
        return CallOutcome<BOOL>{result, GetLastError()};
    });
}
HANDLE WINAPI FindFirstFileADetour(LPCSTR file_name, LPWIN32_FIND_DATAA find_data) noexcept {
    return AtWin32Boundary<HANDLE>(INVALID_HANDLE_VALUE, [&](DWORD incoming_error) {
        FindFirstFileAContext context{PathArgumentA{file_name}, find_data};
        const HANDLE result = Kernel32Dispatcher::ProcessLifetime().Invoke(context, incoming_error);
        return CallOutcome<HANDLE>{result, GetLastError()};
    });
}
HANDLE WINAPI FindFirstFileWDetour(LPCWSTR file_name, LPWIN32_FIND_DATAW find_data) noexcept {
    return AtWin32Boundary<HANDLE>(INVALID_HANDLE_VALUE, [&](DWORD incoming_error) {
        FindFirstFileWContext context{PathArgumentW{file_name}, find_data};
        const HANDLE result = Kernel32Dispatcher::ProcessLifetime().Invoke(context, incoming_error);
        return CallOutcome<HANDLE>{result, GetLastError()};
    });
}
BOOL WINAPI CreateDirectoryADetour(LPCSTR path, LPSECURITY_ATTRIBUTES security_attributes) noexcept {
    return AtWin32Boundary<BOOL>(FALSE, [&](DWORD incoming_error) {
        CreateDirectoryAContext context{PathArgumentA{path}, security_attributes};
        const BOOL result = Kernel32Dispatcher::ProcessLifetime().Invoke(context, incoming_error);
        return CallOutcome<BOOL>{result, GetLastError()};
    });
}
BOOL WINAPI CreateDirectoryWDetour(LPCWSTR path, LPSECURITY_ATTRIBUTES security_attributes) noexcept {
    return AtWin32Boundary<BOOL>(FALSE, [&](DWORD incoming_error) {
        CreateDirectoryWContext context{PathArgumentW{path}, security_attributes};
        const BOOL result = Kernel32Dispatcher::ProcessLifetime().Invoke(context, incoming_error);
        return CallOutcome<BOOL>{result, GetLastError()};
    });
}
BOOL WINAPI DeleteFileADetour(LPCSTR file_name) noexcept {
    return AtWin32Boundary<BOOL>(FALSE, [&](DWORD incoming_error) {
        DeleteFileAContext context{PathArgumentA{file_name}};
        const BOOL result = Kernel32Dispatcher::ProcessLifetime().Invoke(context, incoming_error);
        return CallOutcome<BOOL>{result, GetLastError()};
    });
}
BOOL WINAPI DeleteFileWDetour(LPCWSTR file_name) noexcept {
    return AtWin32Boundary<BOOL>(FALSE, [&](DWORD incoming_error) {
        DeleteFileWContext context{PathArgumentW{file_name}};
        const BOOL result = Kernel32Dispatcher::ProcessLifetime().Invoke(context, incoming_error);
        return CallOutcome<BOOL>{result, GetLastError()};
    });
}
DWORD WINAPI GetFileAttributesADetour(LPCSTR file_name) noexcept {
    return AtWin32Boundary<DWORD>(INVALID_FILE_ATTRIBUTES, [&](DWORD incoming_error) {
        GetFileAttributesAContext context{PathArgumentA{file_name}};
        const DWORD result = Kernel32Dispatcher::ProcessLifetime().Invoke(context, incoming_error);
        return CallOutcome<DWORD>{result, GetLastError()};
    });
}
DWORD WINAPI GetFileAttributesWDetour(LPCWSTR file_name) noexcept {
    return AtWin32Boundary<DWORD>(INVALID_FILE_ATTRIBUTES, [&](DWORD incoming_error) {
        GetFileAttributesWContext context{PathArgumentW{file_name}};
        const DWORD result = Kernel32Dispatcher::ProcessLifetime().Invoke(context, incoming_error);
        return CallOutcome<DWORD>{result, GetLastError()};
    });
}
BOOL WINAPI GetDiskFreeSpaceExADetour(LPCSTR directory, PULARGE_INTEGER available, PULARGE_INTEGER total, PULARGE_INTEGER free) noexcept {
    return AtWin32Boundary<BOOL>(FALSE, [&](DWORD incoming_error) {
        GetDiskFreeSpaceExAContext context{PathArgumentA{directory}, {available, total, free}};
        const BOOL result = Kernel32Dispatcher::ProcessLifetime().Invoke(context, incoming_error);
        return CallOutcome<BOOL>{result, GetLastError()};
    });
}
BOOL WINAPI GetDiskFreeSpaceExWDetour(LPCWSTR directory, PULARGE_INTEGER available, PULARGE_INTEGER total, PULARGE_INTEGER free) noexcept {
    return AtWin32Boundary<BOOL>(FALSE, [&](DWORD incoming_error) {
        GetDiskFreeSpaceExWContext context{PathArgumentW{directory}, {available, total, free}};
        const BOOL result = Kernel32Dispatcher::ProcessLifetime().Invoke(context, incoming_error);
        return CallOutcome<BOOL>{result, GetLastError()};
    });
}
BOOL WINAPI MoveFileADetour(LPCSTR existing_path, LPCSTR new_path) noexcept {
    return AtWin32Boundary<BOOL>(FALSE, [&](DWORD incoming_error) {
        MoveFileAContext context{PathArgumentA{existing_path}, PathArgumentA{new_path}};
        const BOOL result = Kernel32Dispatcher::ProcessLifetime().Invoke(context, incoming_error);
        return CallOutcome<BOOL>{result, GetLastError()};
    });
}
BOOL WINAPI MoveFileWDetour(LPCWSTR existing_path, LPCWSTR new_path) noexcept {
    return AtWin32Boundary<BOOL>(FALSE, [&](DWORD incoming_error) {
        MoveFileWContext context{PathArgumentW{existing_path}, PathArgumentW{new_path}};
        const BOOL result = Kernel32Dispatcher::ProcessLifetime().Invoke(context, incoming_error);
        return CallOutcome<BOOL>{result, GetLastError()};
    });
}
}

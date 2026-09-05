#pragma once
#include <Windows.h>
#include <string>
#include <utility>

namespace gc::win32_hooks {
enum class OriginalVariant { ansi, wide };
struct PathArgumentA {
    explicit PathArgumentA(LPCSTR value) noexcept : original_path(value), path(value) {}
    const LPCSTR original_path;
    LPCSTR path{};
    LPCWSTR wide_path{};
    OriginalVariant variant{OriginalVariant::ansi};
    bool path_claimed{};
    std::string replacement;
    std::wstring wide_replacement;
    void Replace(std::string value) noexcept { replacement = std::move(value); path = replacement.c_str(); }
    void ReplaceWide(std::wstring value) noexcept {
        wide_replacement = std::move(value); wide_path = wide_replacement.c_str();
        variant = OriginalVariant::wide;
    }
};
struct PathArgumentW {
    explicit PathArgumentW(LPCWSTR value) noexcept : original_path(value), path(value) {}
    const LPCWSTR original_path;
    LPCWSTR path{};
    bool path_claimed{};
    std::wstring replacement;
    void Replace(std::wstring value) noexcept { replacement = std::move(value); path = replacement.c_str(); }
};
struct CreateFileArguments {
    DWORD desired_access{};
    DWORD share_mode{};
    LPSECURITY_ATTRIBUTES security_attributes{};
    DWORD creation_disposition{};
    DWORD flags_and_attributes{};
    HANDLE template_file{};
};
struct CreateFileAContext final : PathArgumentA, CreateFileArguments {};
struct CreateFileWContext final : PathArgumentW, CreateFileArguments {};
struct WriteFileContext final {
    HANDLE file{}; LPCVOID buffer{}; DWORD bytes_to_write{};
    LPDWORD bytes_written{}; LPOVERLAPPED overlapped{};
};
struct ReadFileContext final {
    HANDLE file{}; LPVOID buffer{}; DWORD bytes_to_read{};
    LPDWORD bytes_read{}; LPOVERLAPPED overlapped{};
};
struct FlushFileBuffersContext final { HANDLE file{}; };
struct CloseHandleContext final { HANDLE object{}; };
struct FindFirstFileAContext final : PathArgumentA { LPWIN32_FIND_DATAA find_data{}; };
struct FindFirstFileWContext final : PathArgumentW { LPWIN32_FIND_DATAW find_data{}; };
struct CreateDirectoryAContext final : PathArgumentA { LPSECURITY_ATTRIBUTES security_attributes{}; };
struct CreateDirectoryWContext final : PathArgumentW { LPSECURITY_ATTRIBUTES security_attributes{}; };
struct DeleteFileAContext final : PathArgumentA {};
struct DeleteFileWContext final : PathArgumentW {};
struct GetFileAttributesAContext final : PathArgumentA {};
struct GetFileAttributesWContext final : PathArgumentW {};
struct DiskSpaceArguments { PULARGE_INTEGER available{}; PULARGE_INTEGER total{}; PULARGE_INTEGER free{}; };
struct GetDiskFreeSpaceExAContext final : PathArgumentA, DiskSpaceArguments {};
struct GetDiskFreeSpaceExWContext final : PathArgumentW, DiskSpaceArguments {};
struct MoveFileAContext final {
    PathArgumentA existing;
    PathArgumentA destination;
    OriginalVariant variant{OriginalVariant::ansi};
};
struct MoveFileWContext final { PathArgumentW existing; PathArgumentW destination; };
}

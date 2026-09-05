#pragma once
#include <Windows.h>
namespace gc::win32_hooks {
HANDLE WINAPI CreateFileADetour(LPCSTR file_name, DWORD desired_access, DWORD share_mode, LPSECURITY_ATTRIBUTES security_attributes, DWORD creation_disposition, DWORD flags_and_attributes, HANDLE template_file) noexcept;
HANDLE WINAPI CreateFileWDetour(LPCWSTR file_name, DWORD desired_access, DWORD share_mode, LPSECURITY_ATTRIBUTES security_attributes, DWORD creation_disposition, DWORD flags_and_attributes, HANDLE template_file) noexcept;
BOOL WINAPI WriteFileDetour(HANDLE file, LPCVOID buffer, DWORD bytes_to_write, LPDWORD bytes_written, LPOVERLAPPED overlapped) noexcept;
BOOL WINAPI ReadFileDetour(HANDLE file, LPVOID buffer, DWORD bytes_to_read, LPDWORD bytes_read, LPOVERLAPPED overlapped) noexcept;
BOOL WINAPI FlushFileBuffersDetour(HANDLE file) noexcept;
BOOL WINAPI CloseHandleDetour(HANDLE object) noexcept;
HANDLE WINAPI FindFirstFileADetour(LPCSTR file_name, LPWIN32_FIND_DATAA find_data) noexcept;
HANDLE WINAPI FindFirstFileWDetour(LPCWSTR file_name, LPWIN32_FIND_DATAW find_data) noexcept;
BOOL WINAPI CreateDirectoryADetour(LPCSTR path, LPSECURITY_ATTRIBUTES security_attributes) noexcept;
BOOL WINAPI CreateDirectoryWDetour(LPCWSTR path, LPSECURITY_ATTRIBUTES security_attributes) noexcept;
BOOL WINAPI DeleteFileADetour(LPCSTR file_name) noexcept;
BOOL WINAPI DeleteFileWDetour(LPCWSTR file_name) noexcept;
DWORD WINAPI GetFileAttributesADetour(LPCSTR file_name) noexcept;
DWORD WINAPI GetFileAttributesWDetour(LPCWSTR file_name) noexcept;
BOOL WINAPI GetDiskFreeSpaceExADetour(LPCSTR directory, PULARGE_INTEGER available, PULARGE_INTEGER total, PULARGE_INTEGER free) noexcept;
BOOL WINAPI GetDiskFreeSpaceExWDetour(LPCWSTR directory, PULARGE_INTEGER available, PULARGE_INTEGER total, PULARGE_INTEGER free) noexcept;
BOOL WINAPI MoveFileADetour(LPCSTR existing_path, LPCSTR new_path) noexcept;
BOOL WINAPI MoveFileWDetour(LPCWSTR existing_path, LPCWSTR new_path) noexcept;
}

#pragma once

#include "Locale/FilesystemDiagnostics.h"
#include "Platform/Win32/Hooking/MinHookTransaction.h"

#include <Windows.h>

#include <array>
#include <cstddef>
#include <expected>

namespace gc::locale_compatibility {

struct OriginalServiceFilesystemApi {
    decltype(&::CreateFileA) create_file_a{};
    decltype(&::GetFileAttributesA) get_file_attributes_a{};
    decltype(&::FindFirstFileA) find_first_file_a{};
    decltype(&::FindNextFileA) find_next_file_a{};
    decltype(&::CreateDirectoryA) create_directory_a{};
    decltype(&::DeleteFileA) delete_file_a{};
    decltype(&::MoveFileA) move_file_a{};
    decltype(&::CopyFileA) copy_file_a{};
};

inline constexpr std::size_t kServiceFilesystemHookCount = 8;
using ServiceFilesystemHookRequests = std::array<
    gc::win32_hooks::HookRequest,
    kServiceFilesystemHookCount>;

[[nodiscard]] ServiceFilesystemHookRequests
BuildServiceFilesystemHookRequests(
    OriginalServiceFilesystemApi* originals) noexcept;

namespace detail {

HANDLE InvokeCreateFileA(
    LPCSTR,
    DWORD,
    DWORD,
    LPSECURITY_ATTRIBUTES,
    DWORD,
    DWORD,
    HANDLE,
    decltype(&::CreateFileA),
    FilesystemDiagnostics*) noexcept;
DWORD InvokeGetFileAttributesA(
    LPCSTR,
    decltype(&::GetFileAttributesA),
    FilesystemDiagnostics*) noexcept;
HANDLE InvokeFindFirstFileA(
    LPCSTR,
    LPWIN32_FIND_DATAA,
    decltype(&::FindFirstFileA),
    FilesystemDiagnostics*) noexcept;
BOOL InvokeFindNextFileA(
    HANDLE,
    LPWIN32_FIND_DATAA,
    decltype(&::FindNextFileA),
    FilesystemDiagnostics*) noexcept;
BOOL InvokeCreateDirectoryA(
    LPCSTR,
    LPSECURITY_ATTRIBUTES,
    decltype(&::CreateDirectoryA),
    FilesystemDiagnostics*) noexcept;
BOOL InvokeDeleteFileA(
    LPCSTR,
    decltype(&::DeleteFileA),
    FilesystemDiagnostics*) noexcept;
BOOL InvokeMoveFileA(
    LPCSTR,
    LPCSTR,
    decltype(&::MoveFileA),
    FilesystemDiagnostics*) noexcept;
BOOL InvokeCopyFileA(
    LPCSTR,
    LPCSTR,
    BOOL,
    decltype(&::CopyFileA),
    FilesystemDiagnostics*) noexcept;

} // namespace detail

[[nodiscard]] std::expected<
    void,
    gc::win32_hooks::HookInstallError>
InstallServiceFilesystemDiagnostics() noexcept;

} // namespace gc::locale_compatibility

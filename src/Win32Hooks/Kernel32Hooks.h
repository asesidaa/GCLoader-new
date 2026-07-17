#pragma once

#include "Rfid/Runtime.h"
#include "TestModeStorage/Hooks.h"
#include "Platform/Win32/Hooking/MinHookTransaction.h"

#include <Windows.h>

#include <array>
#include <cstddef>
#include <span>

namespace gc::win32_hooks {

struct OriginalKernel32Api {
    decltype(&::CreateFileA) create_file_a{};
    decltype(&::CreateFileW) create_file_w{};
    decltype(&::WriteFile) write_file{};
    decltype(&::ReadFile) read_file{};
    decltype(&::CloseHandle) close_handle{};
    decltype(&::GetCommModemStatus) get_comm_modem_status{};
    decltype(&::EscapeCommFunction) escape_comm_function{};
    decltype(&::ClearCommError) clear_comm_error{};
    decltype(&::SetCommMask) set_comm_mask{};
    decltype(&::SetupComm) setup_comm{};
    decltype(&::GetCommState) get_comm_state{};
    decltype(&::SetCommState) set_comm_state{};
    decltype(&::SetCommTimeouts) set_comm_timeouts{};
    decltype(&::GetCommTimeouts) get_comm_timeouts{};
    decltype(&::FindFirstFileA) find_first_file_a{};
    decltype(&::FindFirstFileW) find_first_file_w{};
    decltype(&::CreateDirectoryA) create_directory_a{};
    decltype(&::CreateDirectoryW) create_directory_w{};
    decltype(&::DeleteFileA) delete_file_a{};
    decltype(&::DeleteFileW) delete_file_w{};
    decltype(&::GetFileAttributesA) get_file_attributes_a{};
    decltype(&::GetFileAttributesW) get_file_attributes_w{};
    decltype(&::GetDiskFreeSpaceExA) get_disk_free_space_ex_a{};
    decltype(&::GetDiskFreeSpaceExW) get_disk_free_space_ex_w{};
};

struct HookRequestSet {
    std::array<HookRequest, kMaxOwnedKernel32Hooks> storage{};
    std::size_t size{};

    [[nodiscard]] std::span<const HookRequest> requests() const noexcept
    {
        return {storage.data(), size};
    }
};

class Kernel32Hooks {
public:
    Kernel32Hooks(
        gc::rfid::Runtime& rfid,
        gc::testmode_storage::Hooks& storage,
        OriginalKernel32Api originals = {}) noexcept;

    void Activate() noexcept;
    void Deactivate() noexcept;
    [[nodiscard]] HookRequestSet BuildRequests(
        bool storage_enabled) noexcept;

    HANDLE CreateFileA(
        LPCSTR file_name, DWORD desired_access, DWORD share_mode,
        LPSECURITY_ATTRIBUTES security_attributes,
        DWORD creation_disposition, DWORD flags_and_attributes,
        HANDLE template_file) noexcept;
    HANDLE CreateFileW(
        LPCWSTR file_name, DWORD desired_access, DWORD share_mode,
        LPSECURITY_ATTRIBUTES security_attributes,
        DWORD creation_disposition, DWORD flags_and_attributes,
        HANDLE template_file) noexcept;
    BOOL WriteFile(
        HANDLE file, LPCVOID buffer, DWORD bytes_to_write,
        LPDWORD bytes_written, LPOVERLAPPED overlapped) noexcept;
    BOOL ReadFile(
        HANDLE file, LPVOID buffer, DWORD bytes_to_read,
        LPDWORD bytes_read, LPOVERLAPPED overlapped) noexcept;
    BOOL CloseHandle(HANDLE object) noexcept;
    BOOL GetCommModemStatus(HANDLE file, LPDWORD modem_status) noexcept;
    BOOL EscapeCommFunction(HANDLE file, DWORD function) noexcept;
    BOOL ClearCommError(
        HANDLE file, LPDWORD errors, LPCOMSTAT status) noexcept;
    BOOL SetCommMask(HANDLE file, DWORD event_mask) noexcept;
    BOOL SetupComm(
        HANDLE file, DWORD input_queue, DWORD output_queue) noexcept;
    BOOL GetCommState(HANDLE file, LPDCB dcb) noexcept;
    BOOL SetCommState(HANDLE file, LPDCB dcb) noexcept;
    BOOL SetCommTimeouts(
        HANDLE file, LPCOMMTIMEOUTS timeouts) noexcept;
    BOOL GetCommTimeouts(
        HANDLE file, LPCOMMTIMEOUTS timeouts) noexcept;

    HANDLE FindFirstFileA(
        LPCSTR file_name, LPWIN32_FIND_DATAA find_data) noexcept;
    HANDLE FindFirstFileW(
        LPCWSTR file_name, LPWIN32_FIND_DATAW find_data) noexcept;
    BOOL CreateDirectoryA(
        LPCSTR path, LPSECURITY_ATTRIBUTES security_attributes) noexcept;
    BOOL CreateDirectoryW(
        LPCWSTR path, LPSECURITY_ATTRIBUTES security_attributes) noexcept;
    BOOL DeleteFileA(LPCSTR file_name) noexcept;
    BOOL DeleteFileW(LPCWSTR file_name) noexcept;
    DWORD GetFileAttributesA(LPCSTR file_name) noexcept;
    DWORD GetFileAttributesW(LPCWSTR file_name) noexcept;
    BOOL GetDiskFreeSpaceExA(
        LPCSTR directory, PULARGE_INTEGER available,
        PULARGE_INTEGER total, PULARGE_INTEGER free) noexcept;
    BOOL GetDiskFreeSpaceExW(
        LPCWSTR directory, PULARGE_INTEGER available,
        PULARGE_INTEGER total, PULARGE_INTEGER free) noexcept;

private:
    static HANDLE WINAPI CreateFileADetour(
        LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
    static HANDLE WINAPI CreateFileWDetour(
        LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
    static BOOL WINAPI WriteFileDetour(
        HANDLE, LPCVOID, DWORD, LPDWORD, LPOVERLAPPED);
    static BOOL WINAPI ReadFileDetour(
        HANDLE, LPVOID, DWORD, LPDWORD, LPOVERLAPPED);
    static BOOL WINAPI CloseHandleDetour(HANDLE);
    static BOOL WINAPI GetCommModemStatusDetour(HANDLE, LPDWORD);
    static BOOL WINAPI EscapeCommFunctionDetour(HANDLE, DWORD);
    static BOOL WINAPI ClearCommErrorDetour(HANDLE, LPDWORD, LPCOMSTAT);
    static BOOL WINAPI SetCommMaskDetour(HANDLE, DWORD);
    static BOOL WINAPI SetupCommDetour(HANDLE, DWORD, DWORD);
    static BOOL WINAPI GetCommStateDetour(HANDLE, LPDCB);
    static BOOL WINAPI SetCommStateDetour(HANDLE, LPDCB);
    static BOOL WINAPI SetCommTimeoutsDetour(HANDLE, LPCOMMTIMEOUTS);
    static BOOL WINAPI GetCommTimeoutsDetour(HANDLE, LPCOMMTIMEOUTS);
    static HANDLE WINAPI FindFirstFileADetour(
        LPCSTR, LPWIN32_FIND_DATAA);
    static HANDLE WINAPI FindFirstFileWDetour(
        LPCWSTR, LPWIN32_FIND_DATAW);
    static BOOL WINAPI CreateDirectoryADetour(
        LPCSTR, LPSECURITY_ATTRIBUTES);
    static BOOL WINAPI CreateDirectoryWDetour(
        LPCWSTR, LPSECURITY_ATTRIBUTES);
    static BOOL WINAPI DeleteFileADetour(LPCSTR);
    static BOOL WINAPI DeleteFileWDetour(LPCWSTR);
    static DWORD WINAPI GetFileAttributesADetour(LPCSTR);
    static DWORD WINAPI GetFileAttributesWDetour(LPCWSTR);
    static BOOL WINAPI GetDiskFreeSpaceExADetour(
        LPCSTR, PULARGE_INTEGER, PULARGE_INTEGER, PULARGE_INTEGER);
    static BOOL WINAPI GetDiskFreeSpaceExWDetour(
        LPCWSTR, PULARGE_INTEGER, PULARGE_INTEGER, PULARGE_INTEGER);

    static Kernel32Hooks* active_;

    gc::rfid::Runtime& rfid_;
    gc::testmode_storage::Hooks& storage_;
    OriginalKernel32Api originals_{};
};

} // namespace gc::win32_hooks

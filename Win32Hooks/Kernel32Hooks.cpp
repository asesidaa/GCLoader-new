#include "Win32Hooks/Kernel32Hooks.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string_view>
#include <utility>

namespace gc::win32_hooks {
namespace {

[[nodiscard]] BOOL Fail(DWORD error) noexcept
{
    SetLastError(error);
    return FALSE;
}

template <typename Function>
[[nodiscard]] LPVOID* OriginalSlot(Function* function) noexcept
{
    return reinterpret_cast<LPVOID*>(function);
}

template <typename Function>
[[nodiscard]] LPVOID DetourAddress(Function function) noexcept
{
    return reinterpret_cast<LPVOID>(function);
}

} // namespace

Kernel32Hooks* Kernel32Hooks::active_{};

Kernel32Hooks::Kernel32Hooks(
    gc::rfid::Runtime& rfid,
    gc::testmode_storage::Hooks& storage,
    OriginalKernel32Api originals) noexcept
    : rfid_{rfid},
      storage_{storage},
      originals_{originals}
{
}

void Kernel32Hooks::Activate() noexcept
{
    active_ = this;
}

void Kernel32Hooks::Deactivate() noexcept
{
    if (active_ == this) {
        active_ = nullptr;
    }
}

HookRequestSet Kernel32Hooks::BuildRequests(bool storage_enabled) noexcept
{
    HookRequestSet result;
    bool valid = true;
    const auto append = [&]<typename Function>(
                            LPCSTR export_name,
                            Function detour,
                            Function* original) {
        if (!valid || result.size >= result.storage.size()) {
            valid = false;
            result.size = 0;
            return;
        }
        result.storage[result.size++] = {
            .module_name = L"kernel32.dll",
            .export_name = export_name,
            .detour = DetourAddress(detour),
            .original = OriginalSlot(original),
        };
    };

    append("CreateFileA", CreateFileADetour, &originals_.create_file_a);
    append("CreateFileW", CreateFileWDetour, &originals_.create_file_w);
    append("WriteFile", WriteFileDetour, &originals_.write_file);
    append("ReadFile", ReadFileDetour, &originals_.read_file);
    append("CloseHandle", CloseHandleDetour, &originals_.close_handle);
    append("GetCommModemStatus", GetCommModemStatusDetour,
           &originals_.get_comm_modem_status);
    append("EscapeCommFunction", EscapeCommFunctionDetour,
           &originals_.escape_comm_function);
    append("ClearCommError", ClearCommErrorDetour,
           &originals_.clear_comm_error);
    append("SetCommMask", SetCommMaskDetour, &originals_.set_comm_mask);
    append("SetupComm", SetupCommDetour, &originals_.setup_comm);
    append("GetCommState", GetCommStateDetour, &originals_.get_comm_state);
    append("SetCommState", SetCommStateDetour, &originals_.set_comm_state);
    append("SetCommTimeouts", SetCommTimeoutsDetour,
           &originals_.set_comm_timeouts);
    append("GetCommTimeouts", GetCommTimeoutsDetour,
           &originals_.get_comm_timeouts);

    if (storage_enabled) {
        append("FindFirstFileA", FindFirstFileADetour,
               &originals_.find_first_file_a);
        append("FindFirstFileW", FindFirstFileWDetour,
               &originals_.find_first_file_w);
        append("CreateDirectoryA", CreateDirectoryADetour,
               &originals_.create_directory_a);
        append("CreateDirectoryW", CreateDirectoryWDetour,
               &originals_.create_directory_w);
        append("DeleteFileA", DeleteFileADetour,
               &originals_.delete_file_a);
        append("DeleteFileW", DeleteFileWDetour,
               &originals_.delete_file_w);
        append("GetFileAttributesA", GetFileAttributesADetour,
               &originals_.get_file_attributes_a);
        append("GetFileAttributesW", GetFileAttributesWDetour,
               &originals_.get_file_attributes_w);
        append("GetDiskFreeSpaceExA", GetDiskFreeSpaceExADetour,
               &originals_.get_disk_free_space_ex_a);
        append("GetDiskFreeSpaceExW", GetDiskFreeSpaceExWDetour,
               &originals_.get_disk_free_space_ex_w);
    }
    return result;
}

HANDLE Kernel32Hooks::CreateFileA(
    LPCSTR file_name,
    DWORD desired_access,
    DWORD share_mode,
    LPSECURITY_ATTRIBUTES security_attributes,
    DWORD creation_disposition,
    DWORD flags_and_attributes,
    HANDLE template_file) noexcept
{
    if (file_name != nullptr && std::string_view{file_name} == "COM2") {
        const auto opened = rfid_.OpenCom2();
        if (!opened) {
            SetLastError(opened.error());
            return INVALID_HANDLE_VALUE;
        }
        return *opened;
    }

    const auto routed = storage_.RoutePathA(file_name);
    return originals_.create_file_a(
        routed.get(), desired_access, share_mode, security_attributes,
        creation_disposition, flags_and_attributes, template_file);
}

HANDLE Kernel32Hooks::CreateFileW(
    LPCWSTR file_name,
    DWORD desired_access,
    DWORD share_mode,
    LPSECURITY_ATTRIBUTES security_attributes,
    DWORD creation_disposition,
    DWORD flags_and_attributes,
    HANDLE template_file) noexcept
{
    if (file_name != nullptr && std::wstring_view{file_name} == L"COM2") {
        const auto opened = rfid_.OpenCom2();
        if (!opened) {
            SetLastError(opened.error());
            return INVALID_HANDLE_VALUE;
        }
        return *opened;
    }

    const auto routed = storage_.RoutePathW(file_name);
    return originals_.create_file_w(
        routed.get(), desired_access, share_mode, security_attributes,
        creation_disposition, flags_and_attributes, template_file);
}

BOOL Kernel32Hooks::WriteFile(
    HANDLE file,
    LPCVOID buffer,
    DWORD bytes_to_write,
    LPDWORD bytes_written,
    LPOVERLAPPED overlapped) noexcept
{
    if (file != gc::rfid::EmulatedComHandle()) {
        return originals_.write_file(
            file, buffer, bytes_to_write, bytes_written, overlapped);
    }
    if (bytes_written != nullptr) {
        *bytes_written = 0;
    }
    if (bytes_written == nullptr || overlapped != nullptr ||
        (buffer == nullptr && bytes_to_write != 0)) {
        return Fail(ERROR_INVALID_PARAMETER);
    }

    const auto bytes = std::span<const std::byte>{
        static_cast<const std::byte*>(buffer),
        static_cast<std::size_t>(bytes_to_write)};
    const auto result = rfid_.port().Write(bytes, false);
    if (!result) {
        return Fail(result.error());
    }
    if (*result > std::numeric_limits<DWORD>::max()) {
        return Fail(ERROR_ARITHMETIC_OVERFLOW);
    }
    *bytes_written = static_cast<DWORD>(*result);
    return TRUE;
}

BOOL Kernel32Hooks::ReadFile(
    HANDLE file,
    LPVOID buffer,
    DWORD bytes_to_read,
    LPDWORD bytes_read,
    LPOVERLAPPED overlapped) noexcept
{
    if (file != gc::rfid::EmulatedComHandle()) {
        return originals_.read_file(
            file, buffer, bytes_to_read, bytes_read, overlapped);
    }
    if (bytes_read != nullptr) {
        *bytes_read = 0;
    }
    if (bytes_read == nullptr || overlapped != nullptr ||
        (buffer == nullptr && bytes_to_read != 0)) {
        return Fail(ERROR_INVALID_PARAMETER);
    }

    const auto destination = std::span<std::byte>{
        static_cast<std::byte*>(buffer),
        static_cast<std::size_t>(bytes_to_read)};
    const auto result = rfid_.port().Read(destination, false);
    if (!result) {
        return Fail(result.error());
    }
    if (*result > std::numeric_limits<DWORD>::max()) {
        return Fail(ERROR_ARITHMETIC_OVERFLOW);
    }
    *bytes_read = static_cast<DWORD>(*result);
    return TRUE;
}

BOOL Kernel32Hooks::CloseHandle(HANDLE object) noexcept
{
    if (object != gc::rfid::EmulatedComHandle()) {
        return originals_.close_handle(object);
    }
    rfid_.CloseCom2();
    return TRUE;
}

BOOL Kernel32Hooks::GetCommModemStatus(
    HANDLE file,
    LPDWORD modem_status) noexcept
{
    if (file != gc::rfid::EmulatedComHandle()) {
        return originals_.get_comm_modem_status(file, modem_status);
    }
    if (modem_status == nullptr) {
        return Fail(ERROR_INVALID_PARAMETER);
    }
    *modem_status = rfid_.port().ModemStatus();
    return TRUE;
}

BOOL Kernel32Hooks::EscapeCommFunction(
    HANDLE file,
    DWORD function) noexcept
{
    if (file != gc::rfid::EmulatedComHandle()) {
        return originals_.escape_comm_function(file, function);
    }
    const auto result = rfid_.port().EscapeCommFunction(function);
    return result ? TRUE : Fail(result.error());
}

BOOL Kernel32Hooks::ClearCommError(
    HANDLE file,
    LPDWORD errors,
    LPCOMSTAT status) noexcept
{
    if (file != gc::rfid::EmulatedComHandle()) {
        return originals_.clear_comm_error(file, errors, status);
    }
    if (errors != nullptr) {
        *errors = 0;
    }
    if (status != nullptr) {
        *status = rfid_.port().CommStatus();
    }
    return TRUE;
}

BOOL Kernel32Hooks::SetCommMask(HANDLE file, DWORD event_mask) noexcept
{
    if (file != gc::rfid::EmulatedComHandle()) {
        return originals_.set_comm_mask(file, event_mask);
    }
    const auto result = rfid_.port().SetCommMask(event_mask);
    return result ? TRUE : Fail(result.error());
}

BOOL Kernel32Hooks::SetupComm(
    HANDLE file,
    DWORD input_queue,
    DWORD output_queue) noexcept
{
    if (file != gc::rfid::EmulatedComHandle()) {
        return originals_.setup_comm(file, input_queue, output_queue);
    }
    const auto result = rfid_.port().SetupComm(input_queue, output_queue);
    return result ? TRUE : Fail(result.error());
}

BOOL Kernel32Hooks::GetCommState(HANDLE file, LPDCB dcb) noexcept
{
    if (file != gc::rfid::EmulatedComHandle()) {
        return originals_.get_comm_state(file, dcb);
    }
    if (dcb == nullptr) {
        return Fail(ERROR_INVALID_PARAMETER);
    }
    *dcb = rfid_.port().GetCommState();
    return TRUE;
}

BOOL Kernel32Hooks::SetCommState(HANDLE file, LPDCB dcb) noexcept
{
    if (file != gc::rfid::EmulatedComHandle()) {
        return originals_.set_comm_state(file, dcb);
    }
    if (dcb == nullptr) {
        return Fail(ERROR_INVALID_PARAMETER);
    }
    const auto result = rfid_.port().SetCommState(*dcb);
    return result ? TRUE : Fail(result.error());
}

BOOL Kernel32Hooks::SetCommTimeouts(
    HANDLE file,
    LPCOMMTIMEOUTS timeouts) noexcept
{
    if (file != gc::rfid::EmulatedComHandle()) {
        return originals_.set_comm_timeouts(file, timeouts);
    }
    if (timeouts == nullptr) {
        return Fail(ERROR_INVALID_PARAMETER);
    }
    const auto result = rfid_.port().SetCommTimeouts(*timeouts);
    return result ? TRUE : Fail(result.error());
}

BOOL Kernel32Hooks::GetCommTimeouts(
    HANDLE file,
    LPCOMMTIMEOUTS timeouts) noexcept
{
    if (file != gc::rfid::EmulatedComHandle()) {
        return originals_.get_comm_timeouts(file, timeouts);
    }
    if (timeouts == nullptr) {
        return Fail(ERROR_INVALID_PARAMETER);
    }
    *timeouts = rfid_.port().GetCommTimeouts();
    return TRUE;
}

HANDLE Kernel32Hooks::FindFirstFileA(
    LPCSTR file_name,
    LPWIN32_FIND_DATAA find_data) noexcept
{
    const auto routed = storage_.RoutePathA(file_name);
    return originals_.find_first_file_a(routed.get(), find_data);
}

HANDLE Kernel32Hooks::FindFirstFileW(
    LPCWSTR file_name,
    LPWIN32_FIND_DATAW find_data) noexcept
{
    const auto routed = storage_.RoutePathW(file_name);
    return originals_.find_first_file_w(routed.get(), find_data);
}

BOOL Kernel32Hooks::CreateDirectoryA(
    LPCSTR path,
    LPSECURITY_ATTRIBUTES security_attributes) noexcept
{
    const auto routed = storage_.RoutePathA(path);
    return originals_.create_directory_a(routed.get(), security_attributes);
}

BOOL Kernel32Hooks::CreateDirectoryW(
    LPCWSTR path,
    LPSECURITY_ATTRIBUTES security_attributes) noexcept
{
    const auto routed = storage_.RoutePathW(path);
    return originals_.create_directory_w(routed.get(), security_attributes);
}

BOOL Kernel32Hooks::DeleteFileA(LPCSTR file_name) noexcept
{
    const auto routed = storage_.RoutePathA(file_name);
    return originals_.delete_file_a(routed.get());
}

BOOL Kernel32Hooks::DeleteFileW(LPCWSTR file_name) noexcept
{
    const auto routed = storage_.RoutePathW(file_name);
    return originals_.delete_file_w(routed.get());
}

DWORD Kernel32Hooks::GetFileAttributesA(LPCSTR file_name) noexcept
{
    const auto routed = storage_.RoutePathA(file_name);
    return originals_.get_file_attributes_a(routed.get());
}

DWORD Kernel32Hooks::GetFileAttributesW(LPCWSTR file_name) noexcept
{
    const auto routed = storage_.RoutePathW(file_name);
    return originals_.get_file_attributes_w(routed.get());
}

BOOL Kernel32Hooks::GetDiskFreeSpaceExA(
    LPCSTR directory,
    PULARGE_INTEGER available,
    PULARGE_INTEGER total,
    PULARGE_INTEGER free) noexcept
{
    return originals_.get_disk_free_space_ex_a(
        storage_.DiskSpaceDirectoryA(directory), available, total, free);
}

BOOL Kernel32Hooks::GetDiskFreeSpaceExW(
    LPCWSTR directory,
    PULARGE_INTEGER available,
    PULARGE_INTEGER total,
    PULARGE_INTEGER free) noexcept
{
    return originals_.get_disk_free_space_ex_w(
        storage_.DiskSpaceDirectoryW(directory), available, total, free);
}

HANDLE WINAPI Kernel32Hooks::CreateFileADetour(
    LPCSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES security,
    DWORD disposition, DWORD flags, HANDLE template_file)
{
    return active_->CreateFileA(
        name, access, share, security, disposition, flags, template_file);
}

HANDLE WINAPI Kernel32Hooks::CreateFileWDetour(
    LPCWSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES security,
    DWORD disposition, DWORD flags, HANDLE template_file)
{
    return active_->CreateFileW(
        name, access, share, security, disposition, flags, template_file);
}

BOOL WINAPI Kernel32Hooks::WriteFileDetour(
    HANDLE file, LPCVOID buffer, DWORD size, LPDWORD count,
    LPOVERLAPPED overlapped)
{
    return active_->WriteFile(file, buffer, size, count, overlapped);
}

BOOL WINAPI Kernel32Hooks::ReadFileDetour(
    HANDLE file, LPVOID buffer, DWORD size, LPDWORD count,
    LPOVERLAPPED overlapped)
{
    return active_->ReadFile(file, buffer, size, count, overlapped);
}

BOOL WINAPI Kernel32Hooks::CloseHandleDetour(HANDLE object)
{
    return active_->CloseHandle(object);
}

BOOL WINAPI Kernel32Hooks::GetCommModemStatusDetour(
    HANDLE file, LPDWORD status)
{
    return active_->GetCommModemStatus(file, status);
}

BOOL WINAPI Kernel32Hooks::EscapeCommFunctionDetour(
    HANDLE file, DWORD function)
{
    return active_->EscapeCommFunction(file, function);
}

BOOL WINAPI Kernel32Hooks::ClearCommErrorDetour(
    HANDLE file, LPDWORD errors, LPCOMSTAT status)
{
    return active_->ClearCommError(file, errors, status);
}

BOOL WINAPI Kernel32Hooks::SetCommMaskDetour(HANDLE file, DWORD mask)
{
    return active_->SetCommMask(file, mask);
}

BOOL WINAPI Kernel32Hooks::SetupCommDetour(
    HANDLE file, DWORD input, DWORD output)
{
    return active_->SetupComm(file, input, output);
}

BOOL WINAPI Kernel32Hooks::GetCommStateDetour(HANDLE file, LPDCB dcb)
{
    return active_->GetCommState(file, dcb);
}

BOOL WINAPI Kernel32Hooks::SetCommStateDetour(HANDLE file, LPDCB dcb)
{
    return active_->SetCommState(file, dcb);
}

BOOL WINAPI Kernel32Hooks::SetCommTimeoutsDetour(
    HANDLE file, LPCOMMTIMEOUTS timeouts)
{
    return active_->SetCommTimeouts(file, timeouts);
}

BOOL WINAPI Kernel32Hooks::GetCommTimeoutsDetour(
    HANDLE file, LPCOMMTIMEOUTS timeouts)
{
    return active_->GetCommTimeouts(file, timeouts);
}

HANDLE WINAPI Kernel32Hooks::FindFirstFileADetour(
    LPCSTR name, LPWIN32_FIND_DATAA data)
{
    return active_->FindFirstFileA(name, data);
}

HANDLE WINAPI Kernel32Hooks::FindFirstFileWDetour(
    LPCWSTR name, LPWIN32_FIND_DATAW data)
{
    return active_->FindFirstFileW(name, data);
}

BOOL WINAPI Kernel32Hooks::CreateDirectoryADetour(
    LPCSTR path, LPSECURITY_ATTRIBUTES security)
{
    return active_->CreateDirectoryA(path, security);
}

BOOL WINAPI Kernel32Hooks::CreateDirectoryWDetour(
    LPCWSTR path, LPSECURITY_ATTRIBUTES security)
{
    return active_->CreateDirectoryW(path, security);
}

BOOL WINAPI Kernel32Hooks::DeleteFileADetour(LPCSTR name)
{
    return active_->DeleteFileA(name);
}

BOOL WINAPI Kernel32Hooks::DeleteFileWDetour(LPCWSTR name)
{
    return active_->DeleteFileW(name);
}

DWORD WINAPI Kernel32Hooks::GetFileAttributesADetour(LPCSTR name)
{
    return active_->GetFileAttributesA(name);
}

DWORD WINAPI Kernel32Hooks::GetFileAttributesWDetour(LPCWSTR name)
{
    return active_->GetFileAttributesW(name);
}

BOOL WINAPI Kernel32Hooks::GetDiskFreeSpaceExADetour(
    LPCSTR directory, PULARGE_INTEGER available, PULARGE_INTEGER total,
    PULARGE_INTEGER free)
{
    return active_->GetDiskFreeSpaceExA(directory, available, total, free);
}

BOOL WINAPI Kernel32Hooks::GetDiskFreeSpaceExWDetour(
    LPCWSTR directory, PULARGE_INTEGER available, PULARGE_INTEGER total,
    PULARGE_INTEGER free)
{
    return active_->GetDiskFreeSpaceExW(directory, available, total, free);
}

} // namespace gc::win32_hooks

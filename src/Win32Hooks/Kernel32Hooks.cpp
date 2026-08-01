#include "Win32Hooks/Kernel32Hooks.h"

#include "Rfid/Trace.h"
#include "plog/Log.h"

#include <cstddef>
#include <functional>
#include <iomanip>
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

template <typename Result, typename Callback>
[[nodiscard]] Result GuardDetour(
    Result failure,
    Callback&& callback) noexcept
{
    try {
        return std::invoke(std::forward<Callback>(callback));
    } catch (...) {
        SetLastError(ERROR_UNHANDLED_EXCEPTION);
        return failure;
    }
}

} // namespace

Kernel32Hooks* Kernel32Hooks::active_{};

Kernel32Hooks::Kernel32Hooks(
    gc::rfid::Runtime& rfid,
    gc::testmode_storage::Hooks& storage,
    gc::system_path::SystemPathRouter& system,
    OriginalKernel32Api originals) noexcept
    : rfid_{rfid},
      storage_{storage},
      system_{system},
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

HookRequestSet Kernel32Hooks::BuildRequests() noexcept
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
    const auto append_if = [&]<typename Function>(
                               bool condition,
                               LPCSTR export_name,
                               Function detour,
                               Function* original) {
        if (condition) {
            append(export_name, detour, original);
        }
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

    const bool storage = storage_.enabled();
    const bool system = system_.enabled();
    append_if(storage, "FindFirstFileA", FindFirstFileADetour,
              &originals_.find_first_file_a);
    append_if(storage || system, "FindFirstFileW", FindFirstFileWDetour,
              &originals_.find_first_file_w);
    append_if(storage, "CreateDirectoryA", CreateDirectoryADetour,
              &originals_.create_directory_a);
    append_if(storage || system, "CreateDirectoryW", CreateDirectoryWDetour,
              &originals_.create_directory_w);
    append_if(storage || system, "DeleteFileA", DeleteFileADetour,
              &originals_.delete_file_a);
    append_if(storage || system, "DeleteFileW", DeleteFileWDetour,
              &originals_.delete_file_w);
    append_if(storage || system, "GetFileAttributesA",
              GetFileAttributesADetour, &originals_.get_file_attributes_a);
    append_if(storage || system, "GetFileAttributesW",
              GetFileAttributesWDetour, &originals_.get_file_attributes_w);
    append_if(storage, "GetDiskFreeSpaceExA", GetDiskFreeSpaceExADetour,
              &originals_.get_disk_free_space_ex_a);
    append_if(storage, "GetDiskFreeSpaceExW", GetDiskFreeSpaceExWDetour,
              &originals_.get_disk_free_space_ex_w);
    append_if(system, "MoveFileA", MoveFileADetour,
              &originals_.move_file_a);
    append_if(system, "MoveFileW", MoveFileWDetour,
              &originals_.move_file_w);
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
            PLOG_ERROR
                << "RFID COM2 trace api=CreateFileA result=failure error="
                << opened.error();
            SetLastError(opened.error());
            return INVALID_HANDLE_VALUE;
        }
        return *opened;
    }

    const DWORD incoming_last_error = GetLastError();
    const auto system = system_.RoutePathA(file_name);
    if (!system) {
        SetLastError(system.error());
        return INVALID_HANDLE_VALUE;
    }
    if (system->matched) {
        SetLastError(incoming_last_error);
        return originals_.create_file_w(
            system->path.c_str(), desired_access, share_mode,
            security_attributes, creation_disposition,
            flags_and_attributes, template_file);
    }

    const auto routed = storage_.RoutePathA(file_name);
    SetLastError(incoming_last_error);
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
            PLOG_ERROR
                << "RFID COM2 trace api=CreateFileW result=failure error="
                << opened.error();
            SetLastError(opened.error());
            return INVALID_HANDLE_VALUE;
        }
        return *opened;
    }

    const DWORD incoming_last_error = GetLastError();
    const auto system = system_.RoutePathW(file_name);
    if (!system) {
        SetLastError(system.error());
        return INVALID_HANDLE_VALUE;
    }
    if (system->matched) {
        SetLastError(incoming_last_error);
        return originals_.create_file_w(
            system->path.c_str(), desired_access, share_mode,
            security_attributes, creation_disposition,
            flags_and_attributes, template_file);
    }

    const auto routed = storage_.RoutePathW(file_name);
    SetLastError(incoming_last_error);
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
        PLOG_ERROR
            << "RFID COM2 trace api=WriteFile result=failure error="
            << ERROR_INVALID_PARAMETER
            << " requested=" << bytes_to_write
            << " bytes_written_ptr=" << bytes_written
            << " overlapped=" << overlapped
            << " buffer=" << buffer;
        return Fail(ERROR_INVALID_PARAMETER);
    }

    const auto bytes = std::span<const std::byte>{
        static_cast<const std::byte*>(buffer),
        static_cast<std::size_t>(bytes_to_write)};
    const auto result = rfid_.port().Write(bytes, false);
    if (!result) {
        PLOG_ERROR
            << "RFID COM2 trace api=WriteFile result=failure error="
            << result.error()
            << " requested=" << bytes_to_write
            << " bytes=" << gc::rfid::trace::FormatBytes(bytes);
        return Fail(result.error());
    }
    if (*result > std::numeric_limits<DWORD>::max()) {
        PLOG_ERROR
            << "RFID COM2 trace api=WriteFile result=failure error="
            << ERROR_ARITHMETIC_OVERFLOW
            << " transferred=" << *result;
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
        PLOG_ERROR
            << "RFID COM2 trace api=ReadFile result=failure error="
            << ERROR_INVALID_PARAMETER
            << " requested=" << bytes_to_read
            << " bytes_read_ptr=" << bytes_read
            << " overlapped=" << overlapped
            << " buffer=" << buffer;
        return Fail(ERROR_INVALID_PARAMETER);
    }

    const auto destination = std::span<std::byte>{
        static_cast<std::byte*>(buffer),
        static_cast<std::size_t>(bytes_to_read)};
    const auto result = rfid_.port().Read(destination, false);
    if (!result) {
        PLOG_ERROR
            << "RFID COM2 trace api=ReadFile result=failure error="
            << result.error()
            << " requested=" << bytes_to_read;
        return Fail(result.error());
    }
    if (*result > std::numeric_limits<DWORD>::max()) {
        PLOG_ERROR
            << "RFID COM2 trace api=ReadFile result=failure error="
            << ERROR_ARITHMETIC_OVERFLOW
            << " transferred=" << *result;
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
        PLOG_ERROR
            << "RFID COM2 trace api=GetCommModemStatus result=failure error="
            << ERROR_INVALID_PARAMETER;
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
    if (!result) {
        PLOG_ERROR
            << "RFID COM2 trace api=EscapeCommFunction result=failure"
            << " function=" << function
            << " error=" << result.error();
        return Fail(result.error());
    }
    return TRUE;
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
    if (!result) {
        PLOG_ERROR
            << "RFID COM2 trace api=SetCommMask result=failure mask=0x"
            << std::hex << event_mask << std::dec
            << " error=" << result.error();
        return Fail(result.error());
    }
    return TRUE;
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
    if (!result) {
        PLOG_ERROR
            << "RFID COM2 trace api=SetupComm result=failure"
            << " input_queue=" << input_queue
            << " output_queue=" << output_queue
            << " error=" << result.error();
        return Fail(result.error());
    }
    return TRUE;
}

BOOL Kernel32Hooks::GetCommState(HANDLE file, LPDCB dcb) noexcept
{
    if (file != gc::rfid::EmulatedComHandle()) {
        return originals_.get_comm_state(file, dcb);
    }
    if (dcb == nullptr) {
        PLOG_ERROR
            << "RFID COM2 trace api=GetCommState result=failure error="
            << ERROR_INVALID_PARAMETER;
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
        PLOG_ERROR
            << "RFID COM2 trace api=SetCommState result=failure error="
            << ERROR_INVALID_PARAMETER << " dcb=<null>";
        return Fail(ERROR_INVALID_PARAMETER);
    }
    const auto result = rfid_.port().SetCommState(*dcb);
    if (!result) {
        PLOG_ERROR
            << "RFID COM2 trace api=SetCommState result=failure"
            << " error=" << result.error()
            << " length=" << dcb->DCBlength
            << " baud=" << dcb->BaudRate
            << " binary=" << dcb->fBinary
            << " parity_check=" << dcb->fParity
            << " outx_cts=" << dcb->fOutxCtsFlow
            << " outx_dsr=" << dcb->fOutxDsrFlow
            << " dtr_control=" << dcb->fDtrControl
            << " dsr_sensitivity=" << dcb->fDsrSensitivity
            << " out_x=" << dcb->fOutX
            << " in_x=" << dcb->fInX
            << " rts_control=" << dcb->fRtsControl
            << " byte_size=" << static_cast<unsigned int>(dcb->ByteSize)
            << " parity=" << static_cast<unsigned int>(dcb->Parity)
            << " stop_bits=" << static_cast<unsigned int>(dcb->StopBits);
        return Fail(result.error());
    }
    return TRUE;
}

BOOL Kernel32Hooks::SetCommTimeouts(
    HANDLE file,
    LPCOMMTIMEOUTS timeouts) noexcept
{
    if (file != gc::rfid::EmulatedComHandle()) {
        return originals_.set_comm_timeouts(file, timeouts);
    }
    if (timeouts == nullptr) {
        PLOG_ERROR
            << "RFID COM2 trace api=SetCommTimeouts result=failure error="
            << ERROR_INVALID_PARAMETER;
        return Fail(ERROR_INVALID_PARAMETER);
    }
    const auto result = rfid_.port().SetCommTimeouts(*timeouts);
    if (!result) {
        PLOG_ERROR
            << "RFID COM2 trace api=SetCommTimeouts result=failure error="
            << result.error();
        return Fail(result.error());
    }
    return TRUE;
}

BOOL Kernel32Hooks::GetCommTimeouts(
    HANDLE file,
    LPCOMMTIMEOUTS timeouts) noexcept
{
    if (file != gc::rfid::EmulatedComHandle()) {
        return originals_.get_comm_timeouts(file, timeouts);
    }
    if (timeouts == nullptr) {
        PLOG_ERROR
            << "RFID COM2 trace api=GetCommTimeouts result=failure error="
            << ERROR_INVALID_PARAMETER;
        return Fail(ERROR_INVALID_PARAMETER);
    }
    *timeouts = rfid_.port().GetCommTimeouts();
    return TRUE;
}

HANDLE Kernel32Hooks::FindFirstFileA(
    LPCSTR file_name,
    LPWIN32_FIND_DATAA find_data) noexcept
{
    const DWORD incoming_last_error = GetLastError();
    const auto routed = storage_.RoutePathA(file_name);
    SetLastError(incoming_last_error);
    return originals_.find_first_file_a(routed.get(), find_data);
}

HANDLE Kernel32Hooks::FindFirstFileW(
    LPCWSTR file_name,
    LPWIN32_FIND_DATAW find_data) noexcept
{
    const DWORD incoming_last_error = GetLastError();
    const auto system = system_.RoutePathW(file_name);
    if (!system) {
        SetLastError(system.error());
        return INVALID_HANDLE_VALUE;
    }
    if (system->matched) {
        SetLastError(incoming_last_error);
        return originals_.find_first_file_w(
            system->path.c_str(), find_data);
    }
    const auto routed = storage_.RoutePathW(file_name);
    SetLastError(incoming_last_error);
    return originals_.find_first_file_w(routed.get(), find_data);
}

BOOL Kernel32Hooks::CreateDirectoryA(
    LPCSTR path,
    LPSECURITY_ATTRIBUTES security_attributes) noexcept
{
    const DWORD incoming_last_error = GetLastError();
    const auto routed = storage_.RoutePathA(path);
    SetLastError(incoming_last_error);
    return originals_.create_directory_a(routed.get(), security_attributes);
}

BOOL Kernel32Hooks::CreateDirectoryW(
    LPCWSTR path,
    LPSECURITY_ATTRIBUTES security_attributes) noexcept
{
    const DWORD incoming_last_error = GetLastError();
    const auto system = system_.RoutePathW(path);
    if (!system) {
        return Fail(system.error());
    }
    if (system->matched) {
        SetLastError(incoming_last_error);
        return originals_.create_directory_w(
            system->path.c_str(), security_attributes);
    }
    const auto routed = storage_.RoutePathW(path);
    SetLastError(incoming_last_error);
    return originals_.create_directory_w(routed.get(), security_attributes);
}

BOOL Kernel32Hooks::DeleteFileA(LPCSTR file_name) noexcept
{
    const DWORD incoming_last_error = GetLastError();
    const auto system = system_.RoutePathA(file_name);
    if (!system) {
        return Fail(system.error());
    }
    if (system->matched) {
        SetLastError(incoming_last_error);
        return originals_.delete_file_w(system->path.c_str());
    }
    const auto routed = storage_.RoutePathA(file_name);
    SetLastError(incoming_last_error);
    return originals_.delete_file_a(routed.get());
}

BOOL Kernel32Hooks::DeleteFileW(LPCWSTR file_name) noexcept
{
    const DWORD incoming_last_error = GetLastError();
    const auto system = system_.RoutePathW(file_name);
    if (!system) {
        return Fail(system.error());
    }
    if (system->matched) {
        SetLastError(incoming_last_error);
        return originals_.delete_file_w(system->path.c_str());
    }
    const auto routed = storage_.RoutePathW(file_name);
    SetLastError(incoming_last_error);
    return originals_.delete_file_w(routed.get());
}

DWORD Kernel32Hooks::GetFileAttributesA(LPCSTR file_name) noexcept
{
    const DWORD incoming_last_error = GetLastError();
    const auto system = system_.RoutePathA(file_name);
    if (!system) {
        SetLastError(system.error());
        return INVALID_FILE_ATTRIBUTES;
    }
    if (system->matched) {
        SetLastError(incoming_last_error);
        return originals_.get_file_attributes_w(system->path.c_str());
    }
    const auto routed = storage_.RoutePathA(file_name);
    SetLastError(incoming_last_error);
    return originals_.get_file_attributes_a(routed.get());
}

DWORD Kernel32Hooks::GetFileAttributesW(LPCWSTR file_name) noexcept
{
    const DWORD incoming_last_error = GetLastError();
    const auto system = system_.RoutePathW(file_name);
    if (!system) {
        SetLastError(system.error());
        return INVALID_FILE_ATTRIBUTES;
    }
    if (system->matched) {
        SetLastError(incoming_last_error);
        return originals_.get_file_attributes_w(system->path.c_str());
    }
    const auto routed = storage_.RoutePathW(file_name);
    SetLastError(incoming_last_error);
    return originals_.get_file_attributes_w(routed.get());
}

BOOL Kernel32Hooks::GetDiskFreeSpaceExA(
    LPCSTR directory,
    PULARGE_INTEGER available,
    PULARGE_INTEGER total,
    PULARGE_INTEGER free) noexcept
{
    const DWORD incoming_last_error = GetLastError();
    const auto* routed = storage_.DiskSpaceDirectoryA(directory);
    SetLastError(incoming_last_error);
    return originals_.get_disk_free_space_ex_a(
        routed, available, total, free);
}

BOOL Kernel32Hooks::GetDiskFreeSpaceExW(
    LPCWSTR directory,
    PULARGE_INTEGER available,
    PULARGE_INTEGER total,
    PULARGE_INTEGER free) noexcept
{
    const DWORD incoming_last_error = GetLastError();
    const auto* routed = storage_.DiskSpaceDirectoryW(directory);
    SetLastError(incoming_last_error);
    return originals_.get_disk_free_space_ex_w(
        routed, available, total, free);
}

BOOL Kernel32Hooks::MoveFileA(
    LPCSTR existing_path,
    LPCSTR new_path) noexcept
{
    const DWORD incoming_last_error = GetLastError();
    const auto existing = system_.RoutePathA(existing_path);
    if (!existing) {
        return Fail(existing.error());
    }
    const auto destination = system_.RoutePathA(new_path);
    if (!destination) {
        return Fail(destination.error());
    }

    if (!existing->matched && !destination->matched) {
        SetLastError(incoming_last_error);
        return originals_.move_file_a(existing_path, new_path);
    }

    std::filesystem::path converted_existing;
    std::filesystem::path converted_destination;
    LPCWSTR existing_w{};
    LPCWSTR destination_w{};

    if (existing->matched) {
        existing_w = existing->path.c_str();
    } else if (existing_path != nullptr) {
        auto converted = system_.ConvertAnsiPath(existing_path);
        if (!converted) {
            return Fail(converted.error());
        }
        converted_existing = std::move(*converted);
        existing_w = converted_existing.c_str();
    }

    if (destination->matched) {
        destination_w = destination->path.c_str();
    } else if (new_path != nullptr) {
        auto converted = system_.ConvertAnsiPath(new_path);
        if (!converted) {
            return Fail(converted.error());
        }
        converted_destination = std::move(*converted);
        destination_w = converted_destination.c_str();
    }

    SetLastError(incoming_last_error);
    return originals_.move_file_w(existing_w, destination_w);
}

BOOL Kernel32Hooks::MoveFileW(
    LPCWSTR existing_path,
    LPCWSTR new_path) noexcept
{
    const DWORD incoming_last_error = GetLastError();
    const auto existing = system_.RoutePathW(existing_path);
    if (!existing) {
        return Fail(existing.error());
    }
    const auto destination = system_.RoutePathW(new_path);
    if (!destination) {
        return Fail(destination.error());
    }

    if (!existing->matched && !destination->matched) {
        SetLastError(incoming_last_error);
        return originals_.move_file_w(existing_path, new_path);
    }

    SetLastError(incoming_last_error);
    return originals_.move_file_w(
        existing->matched ? existing->path.c_str() : existing_path,
        destination->matched ? destination->path.c_str() : new_path);
}

HANDLE WINAPI Kernel32Hooks::CreateFileADetour(
    LPCSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES security,
    DWORD disposition, DWORD flags, HANDLE template_file)
{
    return GuardDetour(INVALID_HANDLE_VALUE, [&] {
        return active_->CreateFileA(
            name, access, share, security, disposition, flags, template_file);
    });
}

HANDLE WINAPI Kernel32Hooks::CreateFileWDetour(
    LPCWSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES security,
    DWORD disposition, DWORD flags, HANDLE template_file)
{
    return GuardDetour(INVALID_HANDLE_VALUE, [&] {
        return active_->CreateFileW(
            name, access, share, security, disposition, flags, template_file);
    });
}

BOOL WINAPI Kernel32Hooks::WriteFileDetour(
    HANDLE file, LPCVOID buffer, DWORD size, LPDWORD count,
    LPOVERLAPPED overlapped)
{
    return GuardDetour(FALSE, [&] {
        return active_->WriteFile(file, buffer, size, count, overlapped);
    });
}

BOOL WINAPI Kernel32Hooks::ReadFileDetour(
    HANDLE file, LPVOID buffer, DWORD size, LPDWORD count,
    LPOVERLAPPED overlapped)
{
    return GuardDetour(FALSE, [&] {
        return active_->ReadFile(file, buffer, size, count, overlapped);
    });
}

BOOL WINAPI Kernel32Hooks::CloseHandleDetour(HANDLE object)
{
    return GuardDetour(FALSE, [&] { return active_->CloseHandle(object); });
}

BOOL WINAPI Kernel32Hooks::GetCommModemStatusDetour(
    HANDLE file, LPDWORD status)
{
    return GuardDetour(FALSE, [&] {
        return active_->GetCommModemStatus(file, status);
    });
}

BOOL WINAPI Kernel32Hooks::EscapeCommFunctionDetour(
    HANDLE file, DWORD function)
{
    return GuardDetour(FALSE, [&] {
        return active_->EscapeCommFunction(file, function);
    });
}

BOOL WINAPI Kernel32Hooks::ClearCommErrorDetour(
    HANDLE file, LPDWORD errors, LPCOMSTAT status)
{
    return GuardDetour(FALSE, [&] {
        return active_->ClearCommError(file, errors, status);
    });
}

BOOL WINAPI Kernel32Hooks::SetCommMaskDetour(HANDLE file, DWORD mask)
{
    return GuardDetour(FALSE, [&] {
        return active_->SetCommMask(file, mask);
    });
}

BOOL WINAPI Kernel32Hooks::SetupCommDetour(
    HANDLE file, DWORD input, DWORD output)
{
    return GuardDetour(FALSE, [&] {
        return active_->SetupComm(file, input, output);
    });
}

BOOL WINAPI Kernel32Hooks::GetCommStateDetour(HANDLE file, LPDCB dcb)
{
    return GuardDetour(FALSE, [&] {
        return active_->GetCommState(file, dcb);
    });
}

BOOL WINAPI Kernel32Hooks::SetCommStateDetour(HANDLE file, LPDCB dcb)
{
    return GuardDetour(FALSE, [&] {
        return active_->SetCommState(file, dcb);
    });
}

BOOL WINAPI Kernel32Hooks::SetCommTimeoutsDetour(
    HANDLE file, LPCOMMTIMEOUTS timeouts)
{
    return GuardDetour(FALSE, [&] {
        return active_->SetCommTimeouts(file, timeouts);
    });
}

BOOL WINAPI Kernel32Hooks::GetCommTimeoutsDetour(
    HANDLE file, LPCOMMTIMEOUTS timeouts)
{
    return GuardDetour(FALSE, [&] {
        return active_->GetCommTimeouts(file, timeouts);
    });
}

HANDLE WINAPI Kernel32Hooks::FindFirstFileADetour(
    LPCSTR name, LPWIN32_FIND_DATAA data)
{
    return GuardDetour(INVALID_HANDLE_VALUE, [&] {
        return active_->FindFirstFileA(name, data);
    });
}

HANDLE WINAPI Kernel32Hooks::FindFirstFileWDetour(
    LPCWSTR name, LPWIN32_FIND_DATAW data)
{
    return GuardDetour(INVALID_HANDLE_VALUE, [&] {
        return active_->FindFirstFileW(name, data);
    });
}

BOOL WINAPI Kernel32Hooks::CreateDirectoryADetour(
    LPCSTR path, LPSECURITY_ATTRIBUTES security)
{
    return GuardDetour(FALSE, [&] {
        return active_->CreateDirectoryA(path, security);
    });
}

BOOL WINAPI Kernel32Hooks::CreateDirectoryWDetour(
    LPCWSTR path, LPSECURITY_ATTRIBUTES security)
{
    return GuardDetour(FALSE, [&] {
        return active_->CreateDirectoryW(path, security);
    });
}

BOOL WINAPI Kernel32Hooks::DeleteFileADetour(LPCSTR name)
{
    return GuardDetour(FALSE, [&] { return active_->DeleteFileA(name); });
}

BOOL WINAPI Kernel32Hooks::DeleteFileWDetour(LPCWSTR name)
{
    return GuardDetour(FALSE, [&] { return active_->DeleteFileW(name); });
}

DWORD WINAPI Kernel32Hooks::GetFileAttributesADetour(LPCSTR name)
{
    return GuardDetour(INVALID_FILE_ATTRIBUTES, [&] {
        return active_->GetFileAttributesA(name);
    });
}

DWORD WINAPI Kernel32Hooks::GetFileAttributesWDetour(LPCWSTR name)
{
    return GuardDetour(INVALID_FILE_ATTRIBUTES, [&] {
        return active_->GetFileAttributesW(name);
    });
}

BOOL WINAPI Kernel32Hooks::GetDiskFreeSpaceExADetour(
    LPCSTR directory, PULARGE_INTEGER available, PULARGE_INTEGER total,
    PULARGE_INTEGER free)
{
    return GuardDetour(FALSE, [&] {
        return active_->GetDiskFreeSpaceExA(
            directory, available, total, free);
    });
}

BOOL WINAPI Kernel32Hooks::GetDiskFreeSpaceExWDetour(
    LPCWSTR directory, PULARGE_INTEGER available, PULARGE_INTEGER total,
    PULARGE_INTEGER free)
{
    return GuardDetour(FALSE, [&] {
        return active_->GetDiskFreeSpaceExW(
            directory, available, total, free);
    });
}

BOOL WINAPI Kernel32Hooks::MoveFileADetour(
    LPCSTR existing_path,
    LPCSTR new_path)
{
    return GuardDetour(FALSE, [&] {
        return active_->MoveFileA(existing_path, new_path);
    });
}

BOOL WINAPI Kernel32Hooks::MoveFileWDetour(
    LPCWSTR existing_path,
    LPCWSTR new_path)
{
    return GuardDetour(FALSE, [&] {
        return active_->MoveFileW(existing_path, new_path);
    });
}

} // namespace gc::win32_hooks

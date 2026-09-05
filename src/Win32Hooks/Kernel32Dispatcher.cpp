#include "Win32Hooks/Kernel32Dispatcher.h"
#include "Win32Hooks/Kernel32Detours.h"

namespace gc::win32_hooks {
Kernel32Dispatcher& Kernel32Dispatcher::ProcessLifetime() {
    static auto* dispatcher = new Kernel32Dispatcher;
    return *dispatcher;
}
std::expected<void, RegistrationError> Kernel32Dispatcher::Publish() noexcept {
    if (const auto result = create_file_a.Publish(); !result) return result;
    if (const auto result = create_file_w.Publish(); !result) return result;
    if (const auto result = write_file.Publish(); !result) return result;
    if (const auto result = read_file.Publish(); !result) return result;
    if (const auto result = flush_file_buffers.Publish(); !result) return result;
    if (const auto result = close_handle.Publish(); !result) return result;
    if (const auto result = find_first_file_a.Publish(); !result) return result;
    if (const auto result = find_first_file_w.Publish(); !result) return result;
    if (const auto result = create_directory_a.Publish(); !result) return result;
    if (const auto result = create_directory_w.Publish(); !result) return result;
    if (const auto result = delete_file_a.Publish(); !result) return result;
    if (const auto result = delete_file_w.Publish(); !result) return result;
    if (const auto result = get_file_attributes_a.Publish(); !result) return result;
    if (const auto result = get_file_attributes_w.Publish(); !result) return result;
    if (const auto result = get_disk_free_space_ex_a.Publish(); !result) return result;
    if (const auto result = get_disk_free_space_ex_w.Publish(); !result) return result;
    if (const auto result = move_file_a.Publish(); !result) return result;
    if (const auto result = move_file_w.Publish(); !result) return result;
    return {};
}
HANDLE Kernel32Dispatcher::Invoke(CreateFileAContext& c, DWORD incoming_error) {
    return create_file_a.Dispatch(c, incoming_error, [&] {
        if (c.variant == OriginalVariant::wide) return original_create_file_w_(c.wide_path, c.desired_access, c.share_mode, c.security_attributes, c.creation_disposition, c.flags_and_attributes, c.template_file);
        return original_create_file_a_(c.path, c.desired_access, c.share_mode, c.security_attributes, c.creation_disposition, c.flags_and_attributes, c.template_file);
    });
}
HANDLE Kernel32Dispatcher::Invoke(CreateFileWContext& c, DWORD incoming_error) {
    return create_file_w.Dispatch(c, incoming_error, [&] {
        return original_create_file_w_(c.path, c.desired_access, c.share_mode, c.security_attributes, c.creation_disposition, c.flags_and_attributes, c.template_file);
    });
}
BOOL Kernel32Dispatcher::Invoke(WriteFileContext& c, DWORD incoming_error) {
    return write_file.Dispatch(c, incoming_error, [&] {
        return original_write_file_(c.file, c.buffer, c.bytes_to_write, c.bytes_written, c.overlapped);
    });
}
BOOL Kernel32Dispatcher::Invoke(ReadFileContext& c, DWORD incoming_error) {
    return read_file.Dispatch(c, incoming_error, [&] {
        return original_read_file_(c.file, c.buffer, c.bytes_to_read, c.bytes_read, c.overlapped);
    });
}
BOOL Kernel32Dispatcher::Invoke(FlushFileBuffersContext& c, DWORD incoming_error) {
    return flush_file_buffers.Dispatch(c, incoming_error, [&] {
        return original_flush_file_buffers_(c.file);
    });
}
BOOL Kernel32Dispatcher::Invoke(CloseHandleContext& c, DWORD incoming_error) {
    return close_handle.Dispatch(c, incoming_error, [&] {
        return original_close_handle_(c.object);
    });
}
HANDLE Kernel32Dispatcher::Invoke(FindFirstFileAContext& c, DWORD incoming_error) {
    return find_first_file_a.Dispatch(c, incoming_error, [&] {
        return original_find_first_file_a_(c.path, c.find_data);
    });
}
HANDLE Kernel32Dispatcher::Invoke(FindFirstFileWContext& c, DWORD incoming_error) {
    return find_first_file_w.Dispatch(c, incoming_error, [&] {
        return original_find_first_file_w_(c.path, c.find_data);
    });
}
BOOL Kernel32Dispatcher::Invoke(CreateDirectoryAContext& c, DWORD incoming_error) {
    return create_directory_a.Dispatch(c, incoming_error, [&] {
        return original_create_directory_a_(c.path, c.security_attributes);
    });
}
BOOL Kernel32Dispatcher::Invoke(CreateDirectoryWContext& c, DWORD incoming_error) {
    return create_directory_w.Dispatch(c, incoming_error, [&] {
        return original_create_directory_w_(c.path, c.security_attributes);
    });
}
BOOL Kernel32Dispatcher::Invoke(DeleteFileAContext& c, DWORD incoming_error) {
    return delete_file_a.Dispatch(c, incoming_error, [&] {
        if (c.variant == OriginalVariant::wide) return original_delete_file_w_(c.wide_path);
        return original_delete_file_a_(c.path);
    });
}
BOOL Kernel32Dispatcher::Invoke(DeleteFileWContext& c, DWORD incoming_error) {
    return delete_file_w.Dispatch(c, incoming_error, [&] {
        return original_delete_file_w_(c.path);
    });
}
DWORD Kernel32Dispatcher::Invoke(GetFileAttributesAContext& c, DWORD incoming_error) {
    return get_file_attributes_a.Dispatch(c, incoming_error, [&] {
        if (c.variant == OriginalVariant::wide) return original_get_file_attributes_w_(c.wide_path);
        return original_get_file_attributes_a_(c.path);
    });
}
DWORD Kernel32Dispatcher::Invoke(GetFileAttributesWContext& c, DWORD incoming_error) {
    return get_file_attributes_w.Dispatch(c, incoming_error, [&] {
        return original_get_file_attributes_w_(c.path);
    });
}
BOOL Kernel32Dispatcher::Invoke(GetDiskFreeSpaceExAContext& c, DWORD incoming_error) {
    return get_disk_free_space_ex_a.Dispatch(c, incoming_error, [&] {
        return original_get_disk_free_space_ex_a_(c.path, c.available, c.total, c.free);
    });
}
BOOL Kernel32Dispatcher::Invoke(GetDiskFreeSpaceExWContext& c, DWORD incoming_error) {
    return get_disk_free_space_ex_w.Dispatch(c, incoming_error, [&] {
        return original_get_disk_free_space_ex_w_(c.path, c.available, c.total, c.free);
    });
}
BOOL Kernel32Dispatcher::Invoke(MoveFileAContext& c, DWORD incoming_error) {
    return move_file_a.Dispatch(c, incoming_error, [&] {
        if (c.variant == OriginalVariant::wide) return original_move_file_w_(c.existing.wide_path, c.destination.wide_path);
        return original_move_file_a_(c.existing.path, c.destination.path);
    });
}
BOOL Kernel32Dispatcher::Invoke(MoveFileWContext& c, DWORD incoming_error) {
    return move_file_w.Dispatch(c, incoming_error, [&] {
        return original_move_file_w_(c.existing.path, c.destination.path);
    });
}
std::expected<void, hooking::HookError> AddSharedKernel32Hooks(
    hooking::HookPlan& plan, Kernel32Dispatcher& dispatcher) noexcept {
    std::expected<void, hooking::HookError> result;
    const auto append = [&]<class Chain, class Detour, class Function>(
        const Chain& chain, LPCSTR name, Detour detour, Function* original) {
        if (!result) return;
        if (!chain.published()) {
            result = std::unexpected(hooking::HookError{
                .stage = hooking::HookStage::invalid_plan,
                .identity = {"Kernel32Dispatcher", name}});
        } else if (!chain.empty()) {
            result = plan.AddInlineExport({"Kernel32Dispatcher", name},
                {L"kernel32.dll", name}, detour, original,
                hooking::HookSharing::named_dispatcher, name);
        }
    };
    append(dispatcher.create_file_a, "CreateFileA", CreateFileADetour, &dispatcher.original_create_file_a_);
    append(dispatcher.create_file_w, "CreateFileW", CreateFileWDetour, &dispatcher.original_create_file_w_);
    append(dispatcher.write_file, "WriteFile", WriteFileDetour, &dispatcher.original_write_file_);
    append(dispatcher.read_file, "ReadFile", ReadFileDetour, &dispatcher.original_read_file_);
    append(dispatcher.flush_file_buffers, "FlushFileBuffers", FlushFileBuffersDetour, &dispatcher.original_flush_file_buffers_);
    append(dispatcher.close_handle, "CloseHandle", CloseHandleDetour, &dispatcher.original_close_handle_);
    append(dispatcher.find_first_file_a, "FindFirstFileA", FindFirstFileADetour, &dispatcher.original_find_first_file_a_);
    append(dispatcher.find_first_file_w, "FindFirstFileW", FindFirstFileWDetour, &dispatcher.original_find_first_file_w_);
    append(dispatcher.create_directory_a, "CreateDirectoryA", CreateDirectoryADetour, &dispatcher.original_create_directory_a_);
    append(dispatcher.create_directory_w, "CreateDirectoryW", CreateDirectoryWDetour, &dispatcher.original_create_directory_w_);
    append(dispatcher.delete_file_a, "DeleteFileA", DeleteFileADetour, &dispatcher.original_delete_file_a_);
    append(dispatcher.delete_file_w, "DeleteFileW", DeleteFileWDetour, &dispatcher.original_delete_file_w_);
    append(dispatcher.get_file_attributes_a, "GetFileAttributesA", GetFileAttributesADetour, &dispatcher.original_get_file_attributes_a_);
    append(dispatcher.get_file_attributes_w, "GetFileAttributesW", GetFileAttributesWDetour, &dispatcher.original_get_file_attributes_w_);
    append(dispatcher.get_disk_free_space_ex_a, "GetDiskFreeSpaceExA", GetDiskFreeSpaceExADetour, &dispatcher.original_get_disk_free_space_ex_a_);
    append(dispatcher.get_disk_free_space_ex_w, "GetDiskFreeSpaceExW", GetDiskFreeSpaceExWDetour, &dispatcher.original_get_disk_free_space_ex_w_);
    append(dispatcher.move_file_a, "MoveFileA", MoveFileADetour, &dispatcher.original_move_file_a_);
    append(dispatcher.move_file_w, "MoveFileW", MoveFileWDetour, &dispatcher.original_move_file_w_);
    return result;
}
}

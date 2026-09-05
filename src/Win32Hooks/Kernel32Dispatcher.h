#pragma once
#include "Win32Hooks/HandlerChain.h"
#include "Win32Hooks/Kernel32CallContexts.h"
#include "Platform/Win32/Hooking/HookPlan.h"

namespace gc::win32_hooks {
class Kernel32Dispatcher final {
public:
    [[nodiscard]] static Kernel32Dispatcher& ProcessLifetime();
    [[nodiscard]] std::expected<void, RegistrationError> Publish() noexcept;
    HandlerChain<CreateFileAContext, HANDLE, 4> create_file_a;
    [[nodiscard]] HANDLE Invoke(CreateFileAContext&, DWORD incoming_error);
    HandlerChain<CreateFileWContext, HANDLE, 4> create_file_w;
    [[nodiscard]] HANDLE Invoke(CreateFileWContext&, DWORD incoming_error);
    HandlerChain<WriteFileContext, BOOL, 2> write_file;
    [[nodiscard]] BOOL Invoke(WriteFileContext&, DWORD incoming_error);
    HandlerChain<ReadFileContext, BOOL, 1> read_file;
    [[nodiscard]] BOOL Invoke(ReadFileContext&, DWORD incoming_error);
    HandlerChain<FlushFileBuffersContext, BOOL, 1> flush_file_buffers;
    [[nodiscard]] BOOL Invoke(FlushFileBuffersContext&, DWORD incoming_error);
    HandlerChain<CloseHandleContext, BOOL, 2> close_handle;
    [[nodiscard]] BOOL Invoke(CloseHandleContext&, DWORD incoming_error);
    HandlerChain<FindFirstFileAContext, HANDLE, 1> find_first_file_a;
    [[nodiscard]] HANDLE Invoke(FindFirstFileAContext&, DWORD incoming_error);
    HandlerChain<FindFirstFileWContext, HANDLE, 2> find_first_file_w;
    [[nodiscard]] HANDLE Invoke(FindFirstFileWContext&, DWORD incoming_error);
    HandlerChain<CreateDirectoryAContext, BOOL, 1> create_directory_a;
    [[nodiscard]] BOOL Invoke(CreateDirectoryAContext&, DWORD incoming_error);
    HandlerChain<CreateDirectoryWContext, BOOL, 2> create_directory_w;
    [[nodiscard]] BOOL Invoke(CreateDirectoryWContext&, DWORD incoming_error);
    HandlerChain<DeleteFileAContext, BOOL, 2> delete_file_a;
    [[nodiscard]] BOOL Invoke(DeleteFileAContext&, DWORD incoming_error);
    HandlerChain<DeleteFileWContext, BOOL, 2> delete_file_w;
    [[nodiscard]] BOOL Invoke(DeleteFileWContext&, DWORD incoming_error);
    HandlerChain<GetFileAttributesAContext, DWORD, 2> get_file_attributes_a;
    [[nodiscard]] DWORD Invoke(GetFileAttributesAContext&, DWORD incoming_error);
    HandlerChain<GetFileAttributesWContext, DWORD, 2> get_file_attributes_w;
    [[nodiscard]] DWORD Invoke(GetFileAttributesWContext&, DWORD incoming_error);
    HandlerChain<GetDiskFreeSpaceExAContext, BOOL, 1> get_disk_free_space_ex_a;
    [[nodiscard]] BOOL Invoke(GetDiskFreeSpaceExAContext&, DWORD incoming_error);
    HandlerChain<GetDiskFreeSpaceExWContext, BOOL, 1> get_disk_free_space_ex_w;
    [[nodiscard]] BOOL Invoke(GetDiskFreeSpaceExWContext&, DWORD incoming_error);
    HandlerChain<MoveFileAContext, BOOL, 1> move_file_a;
    [[nodiscard]] BOOL Invoke(MoveFileAContext&, DWORD incoming_error);
    HandlerChain<MoveFileWContext, BOOL, 1> move_file_w;
    [[nodiscard]] BOOL Invoke(MoveFileWContext&, DWORD incoming_error);
private:
    Kernel32Dispatcher() = default;
    Kernel32Dispatcher(const Kernel32Dispatcher&) = delete;
    Kernel32Dispatcher& operator=(const Kernel32Dispatcher&) = delete;
    friend std::expected<void, hooking::HookError> AddSharedKernel32Hooks(
        hooking::HookPlan&, Kernel32Dispatcher&) noexcept;
    decltype(&::CreateFileA) original_create_file_a_{};
    decltype(&::CreateFileW) original_create_file_w_{};
    decltype(&::WriteFile) original_write_file_{};
    decltype(&::ReadFile) original_read_file_{};
    decltype(&::FlushFileBuffers) original_flush_file_buffers_{};
    decltype(&::CloseHandle) original_close_handle_{};
    decltype(&::FindFirstFileA) original_find_first_file_a_{};
    decltype(&::FindFirstFileW) original_find_first_file_w_{};
    decltype(&::CreateDirectoryA) original_create_directory_a_{};
    decltype(&::CreateDirectoryW) original_create_directory_w_{};
    decltype(&::DeleteFileA) original_delete_file_a_{};
    decltype(&::DeleteFileW) original_delete_file_w_{};
    decltype(&::GetFileAttributesA) original_get_file_attributes_a_{};
    decltype(&::GetFileAttributesW) original_get_file_attributes_w_{};
    decltype(&::GetDiskFreeSpaceExA) original_get_disk_free_space_ex_a_{};
    decltype(&::GetDiskFreeSpaceExW) original_get_disk_free_space_ex_w_{};
    decltype(&::MoveFileA) original_move_file_a_{};
    decltype(&::MoveFileW) original_move_file_w_{};
};
[[nodiscard]] std::expected<void, hooking::HookError> AddSharedKernel32Hooks(
    hooking::HookPlan&, Kernel32Dispatcher&) noexcept;
}

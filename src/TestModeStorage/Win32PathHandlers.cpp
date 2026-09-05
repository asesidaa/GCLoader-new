#include "TestModeStorage/Win32PathHandlers.h"
#include <type_traits>
#include <utility>
namespace gc::testmode_storage {
namespace {
using namespace win32_hooks;
template <class Context>
ContinueCall RoutePath(Hooks& storage, Context& context) {
    if (context.path_claimed) return {};
    auto routed = [&] {
        if constexpr (std::is_base_of_v<PathArgumentA, Context>)
            return storage.RoutePathA(context.path);
        else return storage.RoutePathW(context.path);
    }();
    if (routed.redirected) context.Replace(std::move(*routed.redirected));
    return {};
}
template <class Context>
ContinueCall RouteDiskSpace(Hooks& storage, Context& context) {
    const auto* routed = [&] {
        if constexpr (std::is_base_of_v<PathArgumentA, Context>)
            return storage.DiskSpaceDirectoryA(context.path);
        else return storage.DiskSpaceDirectoryW(context.path);
    }();
    if (!routed) context.path = nullptr;
    else if (routed != context.path) context.Replace(routed);
    return {};
}

PreCallDecision<HANDLE> CreateFileA(void* state, CreateFileAContext& context) noexcept {
    return GuardPreCall<HANDLE>(INVALID_HANDLE_VALUE, [&]() -> PreCallDecision<HANDLE> {
        return RoutePath(*static_cast<Hooks*>(state), context);
    });
}
PreCallDecision<HANDLE> CreateFileW(void* state, CreateFileWContext& context) noexcept {
    return GuardPreCall<HANDLE>(INVALID_HANDLE_VALUE, [&]() -> PreCallDecision<HANDLE> {
        return RoutePath(*static_cast<Hooks*>(state), context);
    });
}
PreCallDecision<HANDLE> FindFirstFileA(void* state, FindFirstFileAContext& context) noexcept {
    return GuardPreCall<HANDLE>(INVALID_HANDLE_VALUE, [&]() -> PreCallDecision<HANDLE> {
        return RoutePath(*static_cast<Hooks*>(state), context);
    });
}
PreCallDecision<HANDLE> FindFirstFileW(void* state, FindFirstFileWContext& context) noexcept {
    return GuardPreCall<HANDLE>(INVALID_HANDLE_VALUE, [&]() -> PreCallDecision<HANDLE> {
        return RoutePath(*static_cast<Hooks*>(state), context);
    });
}
PreCallDecision<BOOL> CreateDirectoryA(void* state, CreateDirectoryAContext& context) noexcept {
    return GuardPreCall<BOOL>(FALSE, [&]() -> PreCallDecision<BOOL> {
        return RoutePath(*static_cast<Hooks*>(state), context);
    });
}
PreCallDecision<BOOL> CreateDirectoryW(void* state, CreateDirectoryWContext& context) noexcept {
    return GuardPreCall<BOOL>(FALSE, [&]() -> PreCallDecision<BOOL> {
        return RoutePath(*static_cast<Hooks*>(state), context);
    });
}
PreCallDecision<BOOL> DeleteFileA(void* state, DeleteFileAContext& context) noexcept {
    return GuardPreCall<BOOL>(FALSE, [&]() -> PreCallDecision<BOOL> {
        return RoutePath(*static_cast<Hooks*>(state), context);
    });
}
PreCallDecision<BOOL> DeleteFileW(void* state, DeleteFileWContext& context) noexcept {
    return GuardPreCall<BOOL>(FALSE, [&]() -> PreCallDecision<BOOL> {
        return RoutePath(*static_cast<Hooks*>(state), context);
    });
}
PreCallDecision<DWORD> GetFileAttributesA(void* state, GetFileAttributesAContext& context) noexcept {
    return GuardPreCall<DWORD>(INVALID_FILE_ATTRIBUTES, [&]() -> PreCallDecision<DWORD> {
        return RoutePath(*static_cast<Hooks*>(state), context);
    });
}
PreCallDecision<DWORD> GetFileAttributesW(void* state, GetFileAttributesWContext& context) noexcept {
    return GuardPreCall<DWORD>(INVALID_FILE_ATTRIBUTES, [&]() -> PreCallDecision<DWORD> {
        return RoutePath(*static_cast<Hooks*>(state), context);
    });
}
PreCallDecision<BOOL> GetDiskFreeSpaceExA(void* state, GetDiskFreeSpaceExAContext& context) noexcept {
    return GuardPreCall<BOOL>(FALSE, [&]() -> PreCallDecision<BOOL> {
        return RouteDiskSpace(*static_cast<Hooks*>(state), context);
    });
}
PreCallDecision<BOOL> GetDiskFreeSpaceExW(void* state, GetDiskFreeSpaceExWContext& context) noexcept {
    return GuardPreCall<BOOL>(FALSE, [&]() -> PreCallDecision<BOOL> {
        return RouteDiskSpace(*static_cast<Hooks*>(state), context);
    });
}
}
std::expected<void, win32_hooks::RegistrationError> AddWin32PathHandlers(
    win32_hooks::Kernel32Dispatcher& dispatcher, Hooks& owner) noexcept {
    if (!owner.enabled()) return {};
    if (const auto result = dispatcher.create_file_a.AddPre(
        {"TestModeStorage", "CreateFileA"}, &owner, CreateFileA); !result) return result;
    if (const auto result = dispatcher.create_file_w.AddPre(
        {"TestModeStorage", "CreateFileW"}, &owner, CreateFileW); !result) return result;
    if (const auto result = dispatcher.find_first_file_a.AddPre(
        {"TestModeStorage", "FindFirstFileA"}, &owner, FindFirstFileA); !result) return result;
    if (const auto result = dispatcher.find_first_file_w.AddPre(
        {"TestModeStorage", "FindFirstFileW"}, &owner, FindFirstFileW); !result) return result;
    if (const auto result = dispatcher.create_directory_a.AddPre(
        {"TestModeStorage", "CreateDirectoryA"}, &owner, CreateDirectoryA); !result) return result;
    if (const auto result = dispatcher.create_directory_w.AddPre(
        {"TestModeStorage", "CreateDirectoryW"}, &owner, CreateDirectoryW); !result) return result;
    if (const auto result = dispatcher.delete_file_a.AddPre(
        {"TestModeStorage", "DeleteFileA"}, &owner, DeleteFileA); !result) return result;
    if (const auto result = dispatcher.delete_file_w.AddPre(
        {"TestModeStorage", "DeleteFileW"}, &owner, DeleteFileW); !result) return result;
    if (const auto result = dispatcher.get_file_attributes_a.AddPre(
        {"TestModeStorage", "GetFileAttributesA"}, &owner, GetFileAttributesA); !result) return result;
    if (const auto result = dispatcher.get_file_attributes_w.AddPre(
        {"TestModeStorage", "GetFileAttributesW"}, &owner, GetFileAttributesW); !result) return result;
    if (const auto result = dispatcher.get_disk_free_space_ex_a.AddPre(
        {"TestModeStorage", "GetDiskFreeSpaceExA"}, &owner, GetDiskFreeSpaceExA); !result) return result;
    if (const auto result = dispatcher.get_disk_free_space_ex_w.AddPre(
        {"TestModeStorage", "GetDiskFreeSpaceExW"}, &owner, GetDiskFreeSpaceExW); !result) return result;
    return {};
}
}

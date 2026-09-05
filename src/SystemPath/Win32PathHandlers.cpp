#include "SystemPath/Win32PathHandlers.h"
#include <type_traits>
#include <utility>
namespace gc::system_path {
namespace {
using namespace win32_hooks;
template <class Context, class Result>
PreCallDecision<Result> RoutePath(SystemPathRouter& router, Context& context, Result failure) {
    auto routed = [&] {
        if constexpr (std::is_base_of_v<PathArgumentA, Context>)
            return router.RoutePathA(context.path);
        else return router.RoutePathW(context.path);
    }();
    if (!routed) return CompleteCall<Result>{failure, routed.error()};
    if (routed->matched) {
        if constexpr (std::is_base_of_v<PathArgumentA, Context>)
            context.ReplaceWide(routed->path.native());
        else context.Replace(routed->path.native());
        // Matching system roots bypass subsequent storage routing and pipe observation.
        context.path_claimed = true;
    }
    return ContinueCall{};
}
PreCallDecision<BOOL> RouteMove(SystemPathRouter& router, MoveFileAContext& context) {
    const auto existing = router.RoutePathA(context.existing.original_path);
    if (!existing) return CompleteCall<BOOL>{FALSE, existing.error()};
    const auto destination = router.RoutePathA(context.destination.original_path);
    if (!destination) return CompleteCall<BOOL>{FALSE, destination.error()};
    if (!existing->matched && !destination->matched) return ContinueCall{};
    if (existing->matched) {
        context.existing.ReplaceWide(existing->path.native());
    } else if (context.existing.original_path) {
        const auto converted = SystemPathRouter::ConvertAnsiPath(context.existing.original_path);
        if (!converted) return CompleteCall<BOOL>{FALSE, converted.error()};
        context.existing.ReplaceWide(converted->native());
    }
    if (destination->matched) {
        context.destination.ReplaceWide(destination->path.native());
    } else if (context.destination.original_path) {
        const auto converted = SystemPathRouter::ConvertAnsiPath(context.destination.original_path);
        if (!converted) return CompleteCall<BOOL>{FALSE, converted.error()};
        context.destination.ReplaceWide(converted->native());
    }
    context.variant = OriginalVariant::wide;
    return ContinueCall{};
}
PreCallDecision<BOOL> RouteMove(SystemPathRouter& router, MoveFileWContext& context) {
    const auto existing = router.RoutePathW(context.existing.original_path);
    if (!existing) return CompleteCall<BOOL>{FALSE, existing.error()};
    const auto destination = router.RoutePathW(context.destination.original_path);
    if (!destination) return CompleteCall<BOOL>{FALSE, destination.error()};
    if (existing->matched) context.existing.Replace(existing->path.native());
    if (destination->matched) context.destination.Replace(destination->path.native());
    return ContinueCall{};
}

PreCallDecision<HANDLE> CreateFileA(void* state, CreateFileAContext& context) noexcept {
    return GuardPreCall<HANDLE>(INVALID_HANDLE_VALUE, [&]() -> PreCallDecision<HANDLE> {
        return RoutePath(*static_cast<SystemPathRouter*>(state), context, INVALID_HANDLE_VALUE);
    });
}
PreCallDecision<HANDLE> CreateFileW(void* state, CreateFileWContext& context) noexcept {
    return GuardPreCall<HANDLE>(INVALID_HANDLE_VALUE, [&]() -> PreCallDecision<HANDLE> {
        return RoutePath(*static_cast<SystemPathRouter*>(state), context, INVALID_HANDLE_VALUE);
    });
}
PreCallDecision<HANDLE> FindFirstFileW(void* state, FindFirstFileWContext& context) noexcept {
    return GuardPreCall<HANDLE>(INVALID_HANDLE_VALUE, [&]() -> PreCallDecision<HANDLE> {
        return RoutePath(*static_cast<SystemPathRouter*>(state), context, INVALID_HANDLE_VALUE);
    });
}
PreCallDecision<BOOL> CreateDirectoryW(void* state, CreateDirectoryWContext& context) noexcept {
    return GuardPreCall<BOOL>(FALSE, [&]() -> PreCallDecision<BOOL> {
        return RoutePath(*static_cast<SystemPathRouter*>(state), context, FALSE);
    });
}
PreCallDecision<BOOL> DeleteFileA(void* state, DeleteFileAContext& context) noexcept {
    return GuardPreCall<BOOL>(FALSE, [&]() -> PreCallDecision<BOOL> {
        return RoutePath(*static_cast<SystemPathRouter*>(state), context, FALSE);
    });
}
PreCallDecision<BOOL> DeleteFileW(void* state, DeleteFileWContext& context) noexcept {
    return GuardPreCall<BOOL>(FALSE, [&]() -> PreCallDecision<BOOL> {
        return RoutePath(*static_cast<SystemPathRouter*>(state), context, FALSE);
    });
}
PreCallDecision<DWORD> GetFileAttributesA(void* state, GetFileAttributesAContext& context) noexcept {
    return GuardPreCall<DWORD>(INVALID_FILE_ATTRIBUTES, [&]() -> PreCallDecision<DWORD> {
        return RoutePath(*static_cast<SystemPathRouter*>(state), context, INVALID_FILE_ATTRIBUTES);
    });
}
PreCallDecision<DWORD> GetFileAttributesW(void* state, GetFileAttributesWContext& context) noexcept {
    return GuardPreCall<DWORD>(INVALID_FILE_ATTRIBUTES, [&]() -> PreCallDecision<DWORD> {
        return RoutePath(*static_cast<SystemPathRouter*>(state), context, INVALID_FILE_ATTRIBUTES);
    });
}
PreCallDecision<BOOL> MoveFileA(void* state, MoveFileAContext& context) noexcept {
    return GuardPreCall<BOOL>(FALSE, [&]() -> PreCallDecision<BOOL> {
        return RouteMove(*static_cast<SystemPathRouter*>(state), context);
    });
}
PreCallDecision<BOOL> MoveFileW(void* state, MoveFileWContext& context) noexcept {
    return GuardPreCall<BOOL>(FALSE, [&]() -> PreCallDecision<BOOL> {
        return RouteMove(*static_cast<SystemPathRouter*>(state), context);
    });
}
}
std::expected<void, win32_hooks::RegistrationError> AddWin32PathHandlers(
    win32_hooks::Kernel32Dispatcher& dispatcher, SystemPathRouter& owner) noexcept {
    if (!owner.enabled()) return {};
    if (const auto result = dispatcher.create_file_a.AddPre(
        {"SystemPath", "CreateFileA"}, &owner, CreateFileA); !result) return result;
    if (const auto result = dispatcher.create_file_w.AddPre(
        {"SystemPath", "CreateFileW"}, &owner, CreateFileW); !result) return result;
    if (const auto result = dispatcher.find_first_file_w.AddPre(
        {"SystemPath", "FindFirstFileW"}, &owner, FindFirstFileW); !result) return result;
    if (const auto result = dispatcher.create_directory_w.AddPre(
        {"SystemPath", "CreateDirectoryW"}, &owner, CreateDirectoryW); !result) return result;
    if (const auto result = dispatcher.delete_file_a.AddPre(
        {"SystemPath", "DeleteFileA"}, &owner, DeleteFileA); !result) return result;
    if (const auto result = dispatcher.delete_file_w.AddPre(
        {"SystemPath", "DeleteFileW"}, &owner, DeleteFileW); !result) return result;
    if (const auto result = dispatcher.get_file_attributes_a.AddPre(
        {"SystemPath", "GetFileAttributesA"}, &owner, GetFileAttributesA); !result) return result;
    if (const auto result = dispatcher.get_file_attributes_w.AddPre(
        {"SystemPath", "GetFileAttributesW"}, &owner, GetFileAttributesW); !result) return result;
    if (const auto result = dispatcher.move_file_a.AddPre(
        {"SystemPath", "MoveFileA"}, &owner, MoveFileA); !result) return result;
    if (const auto result = dispatcher.move_file_w.AddPre(
        {"SystemPath", "MoveFileW"}, &owner, MoveFileW); !result) return result;
    return {};
}
}

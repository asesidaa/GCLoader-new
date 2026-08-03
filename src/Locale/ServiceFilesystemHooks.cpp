#include "Locale/ServiceFilesystemHooks.h"

#include <memory>
#include <span>

namespace gc::locale_compatibility {
namespace {

OriginalServiceFilesystemApi g_originals{};
std::unique_ptr<gc::win32_hooks::MinHookTransaction> g_transaction;
FilesystemDiagnostics g_diagnostics{
    FilesystemDiagnosticRole::service,
    ProductionFilesystemDiagnosticActions()};

template <typename Function>
LPVOID DetourAddress(Function function) noexcept {
    return reinterpret_cast<LPVOID>(function);
}

template <typename Function>
LPVOID* OriginalSlot(Function* function) noexcept {
    return reinterpret_cast<LPVOID*>(function);
}

void RestoreAfterObservation(
    FilesystemDiagnostics* diagnostics,
    const AnsiFilesystemObservation& observation,
    DWORD error) noexcept {
    if (diagnostics != nullptr) {
        diagnostics->Observe(observation);
    }
    SetLastError(error);
}

HANDLE WINAPI CreateFileADetour(
    LPCSTR path,
    DWORD desired_access,
    DWORD share_mode,
    LPSECURITY_ATTRIBUTES security,
    DWORD disposition,
    DWORD flags,
    HANDLE template_file) noexcept {
    return detail::InvokeCreateFileA(
        path,
        desired_access,
        share_mode,
        security,
        disposition,
        flags,
        template_file,
        g_originals.create_file_a,
        &g_diagnostics);
}

DWORD WINAPI GetFileAttributesADetour(LPCSTR path) noexcept {
    return detail::InvokeGetFileAttributesA(
        path,
        g_originals.get_file_attributes_a,
        &g_diagnostics);
}

HANDLE WINAPI FindFirstFileADetour(
    LPCSTR pattern,
    LPWIN32_FIND_DATAA data) noexcept {
    return detail::InvokeFindFirstFileA(
        pattern,
        data,
        g_originals.find_first_file_a,
        &g_diagnostics);
}

BOOL WINAPI FindNextFileADetour(
    HANDLE find,
    LPWIN32_FIND_DATAA data) noexcept {
    return detail::InvokeFindNextFileA(
        find,
        data,
        g_originals.find_next_file_a,
        &g_diagnostics);
}

BOOL WINAPI CreateDirectoryADetour(
    LPCSTR path,
    LPSECURITY_ATTRIBUTES security) noexcept {
    return detail::InvokeCreateDirectoryA(
        path,
        security,
        g_originals.create_directory_a,
        &g_diagnostics);
}

BOOL WINAPI DeleteFileADetour(LPCSTR path) noexcept {
    return detail::InvokeDeleteFileA(
        path,
        g_originals.delete_file_a,
        &g_diagnostics);
}

BOOL WINAPI MoveFileADetour(
    LPCSTR existing_path,
    LPCSTR new_path) noexcept {
    return detail::InvokeMoveFileA(
        existing_path,
        new_path,
        g_originals.move_file_a,
        &g_diagnostics);
}

BOOL WINAPI CopyFileADetour(
    LPCSTR existing_path,
    LPCSTR new_path,
    BOOL fail_if_exists) noexcept {
    return detail::InvokeCopyFileA(
        existing_path,
        new_path,
        fail_if_exists,
        g_originals.copy_file_a,
        &g_diagnostics);
}

gc::win32_hooks::HookInstallError AllocationError() noexcept {
    return {
        .stage = gc::win32_hooks::HookInstallStage::initialize,
        .win32_error = ERROR_NOT_ENOUGH_MEMORY,
        .minhook_status = MH_ERROR_MEMORY_ALLOC,
    };
}

gc::win32_hooks::HookInstallError UnexpectedError() noexcept {
    return {
        .stage = gc::win32_hooks::HookInstallStage::initialize,
        .win32_error = ERROR_UNHANDLED_EXCEPTION,
        .minhook_status = MH_UNKNOWN,
    };
}

} // namespace

ServiceFilesystemHookRequests BuildServiceFilesystemHookRequests(
    OriginalServiceFilesystemApi* originals) noexcept {
    auto slot = [originals]<typename Function>(
                    Function OriginalServiceFilesystemApi::* member) noexcept
        -> LPVOID* {
        return originals != nullptr
            ? OriginalSlot(&(originals->*member))
            : nullptr;
    };

    return {{
        {
            L"kernel32.dll",
            "CreateFileA",
            DetourAddress(&CreateFileADetour),
            slot(&OriginalServiceFilesystemApi::create_file_a),
        },
        {
            L"kernel32.dll",
            "GetFileAttributesA",
            DetourAddress(&GetFileAttributesADetour),
            slot(&OriginalServiceFilesystemApi::get_file_attributes_a),
        },
        {
            L"kernel32.dll",
            "FindFirstFileA",
            DetourAddress(&FindFirstFileADetour),
            slot(&OriginalServiceFilesystemApi::find_first_file_a),
        },
        {
            L"kernel32.dll",
            "FindNextFileA",
            DetourAddress(&FindNextFileADetour),
            slot(&OriginalServiceFilesystemApi::find_next_file_a),
        },
        {
            L"kernel32.dll",
            "CreateDirectoryA",
            DetourAddress(&CreateDirectoryADetour),
            slot(&OriginalServiceFilesystemApi::create_directory_a),
        },
        {
            L"kernel32.dll",
            "DeleteFileA",
            DetourAddress(&DeleteFileADetour),
            slot(&OriginalServiceFilesystemApi::delete_file_a),
        },
        {
            L"kernel32.dll",
            "MoveFileA",
            DetourAddress(&MoveFileADetour),
            slot(&OriginalServiceFilesystemApi::move_file_a),
        },
        {
            L"kernel32.dll",
            "CopyFileA",
            DetourAddress(&CopyFileADetour),
            slot(&OriginalServiceFilesystemApi::copy_file_a),
        },
    }};
}

HANDLE detail::InvokeCreateFileA(
    LPCSTR path,
    DWORD desired_access,
    DWORD share_mode,
    LPSECURITY_ATTRIBUTES security,
    DWORD disposition,
    DWORD flags,
    HANDLE template_file,
    decltype(&::CreateFileA) original,
    FilesystemDiagnostics* diagnostics) noexcept {
    const auto result = original(
        path,
        desired_access,
        share_mode,
        security,
        disposition,
        flags,
        template_file);
    const DWORD error = GetLastError();
    RestoreAfterObservation(
        diagnostics,
        {
            .api = AnsiFilesystemApi::create_file,
            .first_path = path,
            .succeeded = result != INVALID_HANDLE_VALUE,
            .last_error = error,
        },
        error);
    return result;
}

DWORD detail::InvokeGetFileAttributesA(
    LPCSTR path,
    decltype(&::GetFileAttributesA) original,
    FilesystemDiagnostics* diagnostics) noexcept {
    const auto result = original(path);
    const DWORD error = GetLastError();
    RestoreAfterObservation(
        diagnostics,
        {
            .api = AnsiFilesystemApi::get_file_attributes,
            .first_path = path,
            .succeeded = result != INVALID_FILE_ATTRIBUTES,
            .last_error = error,
        },
        error);
    return result;
}

HANDLE detail::InvokeFindFirstFileA(
    LPCSTR pattern,
    LPWIN32_FIND_DATAA data,
    decltype(&::FindFirstFileA) original,
    FilesystemDiagnostics* diagnostics) noexcept {
    const auto result = original(pattern, data);
    const DWORD error = GetLastError();
    RestoreAfterObservation(
        diagnostics,
        {
            .api = AnsiFilesystemApi::find_first_file,
            .first_path = pattern,
            .succeeded = result != INVALID_HANDLE_VALUE,
            .last_error = error,
        },
        error);
    return result;
}

BOOL detail::InvokeFindNextFileA(
    HANDLE find,
    LPWIN32_FIND_DATAA data,
    decltype(&::FindNextFileA) original,
    FilesystemDiagnostics* diagnostics) noexcept {
    const auto result = original(find, data);
    const DWORD error = GetLastError();
    RestoreAfterObservation(
        diagnostics,
        {
            .api = AnsiFilesystemApi::find_next_file,
            .first_path = result != FALSE && data != nullptr
                ? data->cFileName
                : nullptr,
            .succeeded = result != FALSE,
            .last_error = error,
        },
        error);
    return result;
}

BOOL detail::InvokeCreateDirectoryA(
    LPCSTR path,
    LPSECURITY_ATTRIBUTES security,
    decltype(&::CreateDirectoryA) original,
    FilesystemDiagnostics* diagnostics) noexcept {
    const auto result = original(path, security);
    const DWORD error = GetLastError();
    RestoreAfterObservation(
        diagnostics,
        {
            .api = AnsiFilesystemApi::create_directory,
            .first_path = path,
            .succeeded = result != FALSE,
            .last_error = error,
        },
        error);
    return result;
}

BOOL detail::InvokeDeleteFileA(
    LPCSTR path,
    decltype(&::DeleteFileA) original,
    FilesystemDiagnostics* diagnostics) noexcept {
    const auto result = original(path);
    const DWORD error = GetLastError();
    RestoreAfterObservation(
        diagnostics,
        {
            .api = AnsiFilesystemApi::delete_file,
            .first_path = path,
            .succeeded = result != FALSE,
            .last_error = error,
        },
        error);
    return result;
}

BOOL detail::InvokeMoveFileA(
    LPCSTR existing_path,
    LPCSTR new_path,
    decltype(&::MoveFileA) original,
    FilesystemDiagnostics* diagnostics) noexcept {
    const auto result = original(existing_path, new_path);
    const DWORD error = GetLastError();
    RestoreAfterObservation(
        diagnostics,
        {
            .api = AnsiFilesystemApi::move_file,
            .first_path = existing_path,
            .second_path = new_path,
            .succeeded = result != FALSE,
            .last_error = error,
        },
        error);
    return result;
}

BOOL detail::InvokeCopyFileA(
    LPCSTR existing_path,
    LPCSTR new_path,
    BOOL fail_if_exists,
    decltype(&::CopyFileA) original,
    FilesystemDiagnostics* diagnostics) noexcept {
    const auto result = original(
        existing_path, new_path, fail_if_exists);
    const DWORD error = GetLastError();
    RestoreAfterObservation(
        diagnostics,
        {
            .api = AnsiFilesystemApi::copy_file,
            .first_path = existing_path,
            .second_path = new_path,
            .succeeded = result != FALSE,
            .last_error = error,
        },
        error);
    return result;
}

std::expected<void, gc::win32_hooks::HookInstallError>
InstallServiceFilesystemDiagnostics() noexcept {
    if (g_transaction != nullptr) {
        return {};
    }

    try {
        auto candidate =
            std::make_unique<gc::win32_hooks::MinHookTransaction>();
        const auto requests =
            BuildServiceFilesystemHookRequests(&g_originals);
        const auto installed = candidate->Install(
            std::span<const gc::win32_hooks::HookRequest>{requests});
        if (!installed) {
            g_originals = {};
            return std::unexpected(installed.error());
        }

        g_transaction = std::move(candidate);
        g_diagnostics.Start(kObservedAnsiFilesystemApis);
        return {};
    } catch (const std::bad_alloc&) {
        g_originals = {};
        return std::unexpected(AllocationError());
    } catch (...) {
        g_originals = {};
        return std::unexpected(UnexpectedError());
    }
}

} // namespace gc::locale_compatibility

#include "Nesys/Launcher/NesysServiceLauncher.h"

#include "Nesys/NesysServiceProcess.h"

#include <string>
#include <string_view>
#include <vector>

#include "plog/Log.h"

namespace gc::nesys_service {
namespace {

using CreateProcessAFn = BOOL(WINAPI*)(
    LPCSTR,
    LPSTR,
    LPSECURITY_ATTRIBUTES,
    LPSECURITY_ATTRIBUTES,
    BOOL,
    DWORD,
    LPVOID,
    LPCSTR,
    LPSTARTUPINFOA,
    LPPROCESS_INFORMATION);

HMODULE g_loader_module = nullptr;
CreateProcessAFn g_original_create_process_a = nullptr;

std::wstring loader_module_path() {
    std::vector<wchar_t> buffer(MAX_PATH);
    for (;;) {
        const DWORD copied = GetModuleFileNameW(
            g_loader_module,
            buffer.data(),
            static_cast<DWORD>(buffer.size()));
        if (copied == 0) {
            return {};
        }
        if (copied < buffer.size() - 1) {
            return std::wstring{buffer.data(), copied};
        }
        buffer.resize(buffer.size() * 2);
    }
}

bool inject_current_dll(HANDLE process) noexcept {
    try {
        const auto path = loader_module_path();
        if (path.empty()) {
            return false;
        }

        const SIZE_T byte_count =
            (path.size() + 1) * sizeof(wchar_t);
        LPVOID remote_path = VirtualAllocEx(
            process,
            nullptr,
            byte_count,
            MEM_COMMIT | MEM_RESERVE,
            PAGE_READWRITE);
        if (remote_path == nullptr) {
            return false;
        }

        SIZE_T written = 0;
        if (WriteProcessMemory(
                process,
                remote_path,
                path.c_str(),
                byte_count,
                &written) == FALSE ||
            written != byte_count) {
            VirtualFreeEx(process, remote_path, 0, MEM_RELEASE);
            return false;
        }

        HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
        const auto load_library = kernel32 != nullptr
            ? reinterpret_cast<LPTHREAD_START_ROUTINE>(
                  GetProcAddress(kernel32, "LoadLibraryW"))
            : nullptr;
        if (load_library == nullptr) {
            VirtualFreeEx(process, remote_path, 0, MEM_RELEASE);
            return false;
        }

        HANDLE injection_thread = CreateRemoteThread(
            process,
            nullptr,
            0,
            load_library,
            remote_path,
            0,
            nullptr);
        if (injection_thread == nullptr) {
            VirtualFreeEx(process, remote_path, 0, MEM_RELEASE);
            return false;
        }

        const DWORD wait =
            WaitForSingleObject(injection_thread, 5000);
        if (wait != WAIT_OBJECT_0) {
            CloseHandle(injection_thread);
            return false;
        }

        DWORD remote_module = 0;
        const BOOL got_exit_code =
            GetExitCodeThread(injection_thread, &remote_module);
        CloseHandle(injection_thread);
        VirtualFreeEx(process, remote_path, 0, MEM_RELEASE);
        if (got_exit_code == FALSE || remote_module == 0) {
            return false;
        }

        PLOG_INFO
            << "NesysServiceLauncher: child DLL initialization succeeded";
        return true;
    } catch (...) {
        return false;
    }
}

BOOL WINAPI create_process_a_detour(
    LPCSTR application_name,
    LPSTR command_line,
    LPSECURITY_ATTRIBUTES process_attributes,
    LPSECURITY_ATTRIBUTES thread_attributes,
    BOOL inherit_handles,
    DWORD creation_flags,
    LPVOID environment,
    LPCSTR current_directory,
    LPSTARTUPINFOA startup_info,
    LPPROCESS_INFORMATION process_information) {
    if (!IsNesysServiceLaunchA(application_name, command_line)) {
        return g_original_create_process_a(
            application_name,
            command_line,
            process_attributes,
            thread_attributes,
            inherit_handles,
            creation_flags,
            environment,
            current_directory,
            startup_info,
            process_information);
    }

    if (process_information == nullptr) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    PLOG_INFO
        << "NesysServiceLauncher: intercepting suspended service child";
    const bool caller_requested_suspended =
        WasCreateSuspendedRequested(creation_flags);
    const BOOL created = g_original_create_process_a(
        application_name,
        command_line,
        process_attributes,
        thread_attributes,
        inherit_handles,
        AddCreateSuspendedFlag(creation_flags),
        environment,
        current_directory,
        startup_info,
        process_information);
    const DWORD original_error = GetLastError();
    if (created == FALSE) {
        SetLastError(original_error);
        return FALSE;
    }

    const bool injected =
        process_information->hProcess != nullptr &&
        inject_current_dll(process_information->hProcess);
    const auto finalized = FinalizeInjectedServiceChild(
        process_information,
        caller_requested_suspended,
        injected,
        ProductionServiceChildApi());
    if (!finalized.success) {
        PLOG_ERROR
            << "NesysServiceLauncher: child initialization failed;"
            << " terminated suspended child";
        SetLastError(finalized.error);
        return FALSE;
    }

    PLOG_INFO
        << "NesysServiceLauncher: child ready resume="
        << finalized.resumed;
    SetLastError(original_error);
    return TRUE;
}

} // namespace

ServiceChildApi ProductionServiceChildApi() noexcept {
    return {
        TerminateProcess,
        WaitForSingleObject,
        ResumeThread,
        CloseHandle,
    };
}

ServiceChildResult FinalizeInjectedServiceChild(
    LPPROCESS_INFORMATION process_information,
    bool caller_requested_suspended,
    bool injection_succeeded,
    const ServiceChildApi& api) noexcept {
    const auto fail_closed = [&]() noexcept {
        if (process_information != nullptr &&
            process_information->hProcess != nullptr) {
            api.terminate_process(
                process_information->hProcess,
                ERROR_DLL_INIT_FAILED);
            api.wait_for_single_object(
                process_information->hProcess,
                INFINITE);
        }
        if (process_information != nullptr &&
            process_information->hThread != nullptr) {
            api.close_handle(process_information->hThread);
        }
        if (process_information != nullptr &&
            process_information->hProcess != nullptr) {
            api.close_handle(process_information->hProcess);
        }
        if (process_information != nullptr) {
            *process_information = {};
        }
        return ServiceChildResult{
            false,
            false,
            ERROR_DLL_INIT_FAILED,
        };
    };

    if (!injection_succeeded ||
        process_information == nullptr ||
        process_information->hProcess == nullptr ||
        process_information->hThread == nullptr) {
        return fail_closed();
    }

    if (caller_requested_suspended) {
        return {true, false, ERROR_SUCCESS};
    }

    if (api.resume_thread(process_information->hThread) ==
        static_cast<DWORD>(-1)) {
        return fail_closed();
    }
    return {true, true, ERROR_SUCCESS};
}

bool InitializeNesysServiceLauncher(
    HMODULE loader_module) noexcept {
    if (loader_module == nullptr) {
        return false;
    }
    g_loader_module = loader_module;
    return true;
}

void AppendNesysServiceLauncherHookRequest(
    std::vector<ApiHookRequest>& requests) {
    requests.push_back({
        L"kernel32.dll",
        "CreateProcessA",
        reinterpret_cast<LPVOID>(&create_process_a_detour),
        reinterpret_cast<LPVOID*>(&g_original_create_process_a),
    });
}

} // namespace gc::nesys_service

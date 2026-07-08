#include "NesysServicePatch.h"

#include "NesysServiceProcess.h"
#include "config.h"

#include <Iphlpapi.h>
#include <Windows.h>
#include <atomic>
#include <string>
#include <string_view>
#include <vector>

#include "MinHook.h"
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

using IpReleaseAddressFn = DWORD(WINAPI*)(PIP_ADAPTER_INDEX_MAP);
using IpRenewAddressFn = DWORD(WINAPI*)(PIP_ADAPTER_INDEX_MAP);

std::atomic_bool g_initialized{false};
HMODULE g_loader_module = nullptr;
CreateProcessAFn g_original_create_process_a = nullptr;
IpReleaseAddressFn g_original_ip_release_address = nullptr;
IpRenewAddressFn g_original_ip_renew_address = nullptr;

bool EnsureMinHookInitialized() {
    const auto status = MH_Initialize();
    if (status == MH_OK || status == MH_ERROR_ALREADY_INITIALIZED) {
        return true;
    }

    PLOG_ERROR << "NesysServicePatch: MH_Initialize failed status=" << static_cast<int>(status);
    return false;
}

std::wstring GetLoaderModulePath() {
    std::vector<wchar_t> buffer(MAX_PATH);
    while (true) {
        const DWORD copied = GetModuleFileNameW(g_loader_module, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (copied == 0) {
            PLOG_ERROR << "NesysServicePatch: GetModuleFileNameW failed gle=" << GetLastError();
            return {};
        }

        if (copied < buffer.size() - 1) {
            return std::wstring{buffer.data(), copied};
        }

        buffer.resize(buffer.size() * 2);
    }
}

void LogWin32Failure(const char* step) {
    PLOG_ERROR << "NesysServicePatch: " << step << " failed gle=" << GetLastError();
}

std::string WideToUtf8(std::wstring_view value) {
    if (value.empty()) {
        return {};
    }

    const int size = WideCharToMultiByte(
        CP_UTF8,
        0,
        value.data(),
        static_cast<int>(value.size()),
        nullptr,
        0,
        nullptr,
        nullptr);
    if (size <= 0) {
        return {};
    }

    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(
        CP_UTF8,
        0,
        value.data(),
        static_cast<int>(value.size()),
        result.data(),
        size,
        nullptr,
        nullptr);
    return result;
}

bool InjectCurrentDllIntoProcess(HANDLE process) {
    const auto dll_path = GetLoaderModulePath();
    if (dll_path.empty()) {
        return false;
    }

    const SIZE_T bytes = (dll_path.size() + 1) * sizeof(wchar_t);
    LPVOID remote_path = VirtualAllocEx(process, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (remote_path == nullptr) {
        LogWin32Failure("VirtualAllocEx");
        return false;
    }

    SIZE_T written = 0;
    if (WriteProcessMemory(process, remote_path, dll_path.c_str(), bytes, &written) == FALSE || written != bytes) {
        LogWin32Failure("WriteProcessMemory");
        VirtualFreeEx(process, remote_path, 0, MEM_RELEASE);
        return false;
    }

    HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
    if (kernel32 == nullptr) {
        LogWin32Failure("GetModuleHandleW(kernel32.dll)");
        VirtualFreeEx(process, remote_path, 0, MEM_RELEASE);
        return false;
    }

    auto load_library_w = reinterpret_cast<LPTHREAD_START_ROUTINE>(
        GetProcAddress(kernel32, "LoadLibraryW"));
    if (load_library_w == nullptr) {
        LogWin32Failure("GetProcAddress(LoadLibraryW)");
        VirtualFreeEx(process, remote_path, 0, MEM_RELEASE);
        return false;
    }

    HANDLE thread = CreateRemoteThread(process, nullptr, 0, load_library_w, remote_path, 0, nullptr);
    if (thread == nullptr) {
        LogWin32Failure("CreateRemoteThread");
        VirtualFreeEx(process, remote_path, 0, MEM_RELEASE);
        return false;
    }

    const DWORD wait_result = WaitForSingleObject(thread, 5000);
    if (wait_result != WAIT_OBJECT_0) {
        PLOG_ERROR << "NesysServicePatch: injection thread wait failed result=" << wait_result
                   << " gle=" << GetLastError();
        CloseHandle(thread);
        VirtualFreeEx(process, remote_path, 0, MEM_RELEASE);
        return false;
    }

    DWORD remote_result = 0;
    if (GetExitCodeThread(thread, &remote_result) == FALSE) {
        LogWin32Failure("GetExitCodeThread(LoadLibraryW)");
        CloseHandle(thread);
        VirtualFreeEx(process, remote_path, 0, MEM_RELEASE);
        return false;
    }

    if (remote_result == 0) {
        PLOG_ERROR << "NesysServicePatch: LoadLibraryW returned null in child process";
        CloseHandle(thread);
        VirtualFreeEx(process, remote_path, 0, MEM_RELEASE);
        return false;
    }

    CloseHandle(thread);
    VirtualFreeEx(process, remote_path, 0, MEM_RELEASE);
    PLOG_INFO << "NesysServicePatch: child DLL injection succeeded path=" << WideToUtf8(dll_path);
    return true;
}

DWORD AdapterIndexOrZero(PIP_ADAPTER_INDEX_MAP adapter_info) {
    return adapter_info != nullptr ? adapter_info->Index : 0;
}

DWORD WINAPI IpReleaseAddressWrap(PIP_ADAPTER_INDEX_MAP adapter_info) {
    PLOG_INFO << "NesysServicePatch: suppressed IpReleaseAddress adapter_index="
              << AdapterIndexOrZero(adapter_info);
    return NO_ERROR;
}

DWORD WINAPI IpRenewAddressWrap(PIP_ADAPTER_INDEX_MAP adapter_info) {
    PLOG_INFO << "NesysServicePatch: suppressed IpRenewAddress adapter_index="
              << AdapterIndexOrZero(adapter_info);
    return NO_ERROR;
}

BOOL WINAPI CreateProcessAWrap(
    LPCSTR lpApplicationName,
    LPSTR lpCommandLine,
    LPSECURITY_ATTRIBUTES lpProcessAttributes,
    LPSECURITY_ATTRIBUTES lpThreadAttributes,
    BOOL bInheritHandles,
    DWORD dwCreationFlags,
    LPVOID lpEnvironment,
    LPCSTR lpCurrentDirectory,
    LPSTARTUPINFOA lpStartupInfo,
    LPPROCESS_INFORMATION lpProcessInformation) {

    if (!IsNesysServiceLaunchA(lpApplicationName, lpCommandLine)) {
        return g_original_create_process_a(
            lpApplicationName,
            lpCommandLine,
            lpProcessAttributes,
            lpThreadAttributes,
            bInheritHandles,
            dwCreationFlags,
            lpEnvironment,
            lpCurrentDirectory,
            lpStartupInfo,
            lpProcessInformation);
    }

    PLOG_INFO << "NesysServicePatch: intercepting NesysService.exe -app command="
              << (lpCommandLine != nullptr ? lpCommandLine : "<null>");

    const bool caller_requested_suspended = WasCreateSuspendedRequested(dwCreationFlags);
    const DWORD suspended_flags = AddCreateSuspendedFlag(dwCreationFlags);
    const BOOL result = g_original_create_process_a(
        lpApplicationName,
        lpCommandLine,
        lpProcessAttributes,
        lpThreadAttributes,
        bInheritHandles,
        suspended_flags,
        lpEnvironment,
        lpCurrentDirectory,
        lpStartupInfo,
        lpProcessInformation);
    const DWORD create_process_error = GetLastError();

    if (result == FALSE) {
        PLOG_WARNING << "NesysServicePatch: original CreateProcessA failed gle=" << create_process_error;
        SetLastError(create_process_error);
        return result;
    }

    bool injected = false;
    if (lpProcessInformation != nullptr && lpProcessInformation->hProcess != nullptr) {
        injected = InjectCurrentDllIntoProcess(lpProcessInformation->hProcess);
    } else {
        PLOG_ERROR << "NesysServicePatch: CreateProcessA returned success without process information";
    }

    const bool should_resume = !caller_requested_suspended || !injected;
    if (should_resume &&
        lpProcessInformation != nullptr &&
        lpProcessInformation->hThread != nullptr) {
        const DWORD resume_result = ResumeThread(lpProcessInformation->hThread);
        if (resume_result == static_cast<DWORD>(-1)) {
            LogWin32Failure("ResumeThread");
        } else {
            PLOG_INFO << "NesysServicePatch: resumed NesysService.exe main thread";
        }
    }

    if (!injected) {
        PLOG_ERROR << "NesysServicePatch: child DLL injection failed; service resumed fail-open";
    }

    SetLastError(create_process_error);
    return result;
}

bool InstallServiceIpHelperHooks() {
    if (!EnsureMinHookInitialized()) {
        return false;
    }

    const auto release_status = MH_CreateHookApi(
        L"iphlpapi.dll",
        "IpReleaseAddress",
        reinterpret_cast<LPVOID>(&IpReleaseAddressWrap),
        reinterpret_cast<LPVOID*>(&g_original_ip_release_address));
    if (release_status != MH_OK && release_status != MH_ERROR_ALREADY_CREATED) {
        PLOG_ERROR << "NesysServicePatch: MH_CreateHookApi(IpReleaseAddress) failed status="
                   << static_cast<int>(release_status);
        return false;
    }

    const auto renew_status = MH_CreateHookApi(
        L"iphlpapi.dll",
        "IpRenewAddress",
        reinterpret_cast<LPVOID>(&IpRenewAddressWrap),
        reinterpret_cast<LPVOID*>(&g_original_ip_renew_address));
    if (renew_status != MH_OK && renew_status != MH_ERROR_ALREADY_CREATED) {
        PLOG_ERROR << "NesysServicePatch: MH_CreateHookApi(IpRenewAddress) failed status="
                   << static_cast<int>(renew_status);
        return false;
    }

    const auto enable_status = MH_EnableHook(MH_ALL_HOOKS);
    if (enable_status != MH_OK && enable_status != MH_ERROR_ENABLED) {
        PLOG_ERROR << "NesysServicePatch: MH_EnableHook(IP Helper hooks) failed status="
                   << static_cast<int>(enable_status);
        return false;
    }

    PLOG_INFO << "NesysServicePatch: service IP Helper hooks installed";
    return true;
}

bool InstallGameCreateProcessHook() {
    if (!EnsureMinHookInitialized()) {
        return false;
    }

    const auto create_status = MH_CreateHookApi(
        L"kernel32.dll",
        "CreateProcessA",
        reinterpret_cast<LPVOID>(&CreateProcessAWrap),
        reinterpret_cast<LPVOID*>(&g_original_create_process_a));
    if (create_status != MH_OK && create_status != MH_ERROR_ALREADY_CREATED) {
        PLOG_ERROR << "NesysServicePatch: MH_CreateHookApi(CreateProcessA) failed status="
                   << static_cast<int>(create_status);
        return false;
    }

    const auto enable_status = MH_EnableHook(MH_ALL_HOOKS);
    if (enable_status != MH_OK && enable_status != MH_ERROR_ENABLED) {
        PLOG_ERROR << "NesysServicePatch: MH_EnableHook(CreateProcessA) failed status="
                   << static_cast<int>(enable_status);
        return false;
    }

    PLOG_INFO << "NesysServicePatch: CreateProcessA hook installed";
    return true;
}

} // namespace

void NesysServicePatchInit(HMODULE loader_module) {
    bool expected = false;
    if (!g_initialized.compare_exchange_strong(expected, true)) {
        return;
    }

    g_loader_module = loader_module;
    const auto role = DetectCurrentProcessRole();
    const bool enabled = ConfigManager::instance().GetEnableNesysServiceAdapterPatch();

    PLOG_INFO << "NesysServicePatch: init role=" << ProcessRoleName(role)
              << " enable_nesys_service_adapter_patch=" << enabled
              << " loader_module=" << reinterpret_cast<void*>(g_loader_module);

    if (!enabled) {
        PLOG_INFO << "NesysServicePatch: disabled by config";
        return;
    }

    if (role == ProcessRole::Service) {
        if (!InstallServiceIpHelperHooks()) {
            PLOG_ERROR << "NesysServicePatch: service IP Helper hook installation failed; continuing unpatched";
        }
        return;
    }

    InstallGameCreateProcessHook();
}

} // namespace gc::nesys_service

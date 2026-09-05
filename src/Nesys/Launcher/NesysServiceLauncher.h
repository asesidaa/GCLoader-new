#pragma once

#include <Windows.h>

#include "Platform/Win32/Hooking/HookPlan.h"

#include <vector>

namespace gc::nesys_service {

struct ServiceChildApi {
    decltype(&TerminateProcess) terminate_process;
    decltype(&WaitForSingleObject) wait_for_single_object;
    decltype(&ResumeThread) resume_thread;
    decltype(&CloseHandle) close_handle;
};

struct ServiceChildResult {
    bool success{false};
    bool resumed{false};
    DWORD error{ERROR_SUCCESS};
};

ServiceChildApi ProductionServiceChildApi() noexcept;

ServiceChildResult FinalizeInjectedServiceChild(
    LPPROCESS_INFORMATION process_information,
    bool caller_requested_suspended,
    bool injection_succeeded,
    const ServiceChildApi& api) noexcept;

bool InitializeNesysServiceLauncher(HMODULE loader_module) noexcept;
[[nodiscard]] std::expected<void, hooking::HookError> AddNesysServiceLauncherHook(
    hooking::HookPlan& hooks) noexcept;

} // namespace gc::nesys_service

#pragma once

#include <Windows.h>

#include "Nesys/NesysHookTransaction.h"

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
void AppendNesysServiceLauncherHookRequest(
    std::vector<ApiHookRequest>& requests);

} // namespace gc::nesys_service

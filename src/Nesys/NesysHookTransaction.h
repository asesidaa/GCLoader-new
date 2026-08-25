#pragma once

#include <Windows.h>
#include <MinHook.h>

#include <span>
#include <vector>

namespace gc::nesys_service {

struct ApiHookRequest {
    LPCWSTR module_name;
    LPCSTR export_name;
    LPVOID detour;
    LPVOID* original;
};

struct ResolvedApiHook {
    ApiHookRequest request;
    LPVOID target;
};

enum class HookInstallStage {
    None,
    ResolveModule,
    ResolveExport,
    Initialize,
    Create,
    QueueEnable,
    ApplyQueued,
};

struct HookInstallError {
    HookInstallStage stage{HookInstallStage::None};
    MH_STATUS minhook_status{MH_OK};
    DWORD win32_error{ERROR_SUCCESS};
    LPCSTR export_name{nullptr};
    LPVOID target{nullptr};
};

bool ResolveApiHooks(
    std::span<const ApiHookRequest> requests,
    std::vector<ResolvedApiHook>* resolved,
    HookInstallError* error) noexcept;

struct MinHookApi {
    decltype(&MH_Initialize) initialize;
    decltype(&MH_CreateHook) create_hook;
    decltype(&MH_QueueEnableHook) queue_enable_hook;
    decltype(&MH_ApplyQueued) apply_queued;
    decltype(&MH_DisableHook) disable_hook;
    decltype(&MH_RemoveHook) remove_hook;
};

MinHookApi ProductionMinHookApi() noexcept;

class OwnedMinHookTransaction {
public:
    explicit OwnedMinHookTransaction(const MinHookApi& api) noexcept;
    ~OwnedMinHookTransaction();

    OwnedMinHookTransaction(const OwnedMinHookTransaction&) = delete;
    OwnedMinHookTransaction& operator=(const OwnedMinHookTransaction&) = delete;

    bool Initialize() noexcept;
    bool CreateAll(std::span<const ResolvedApiHook> hooks) noexcept;
    bool Commit() noexcept;
    void Rollback() noexcept;

    const HookInstallError& error() const noexcept { return error_; }
    bool committed() const noexcept { return committed_; }

private:
    MinHookApi api_;
    std::vector<LPVOID> owned_targets_;
    HookInstallError error_{};
    bool committed_{false};
};

} // namespace gc::nesys_service

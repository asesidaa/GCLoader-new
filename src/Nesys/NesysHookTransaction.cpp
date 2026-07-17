#include "Nesys/NesysHookTransaction.h"

#include <new>

namespace gc::nesys_service {
namespace {

void set_error(
    HookInstallError* error,
    HookInstallStage stage,
    MH_STATUS status,
    DWORD win32_error,
    LPCSTR export_name,
    LPVOID target) noexcept {
    if (error != nullptr) {
        *error = {stage, status, win32_error, export_name, target};
    }
}

} // namespace

bool ResolveApiHooks(
    std::span<const ApiHookRequest> requests,
    std::vector<ResolvedApiHook>* resolved,
    HookInstallError* error) noexcept {
    if (resolved == nullptr) {
        set_error(
            error,
            HookInstallStage::ResolveExport,
            MH_UNKNOWN,
            ERROR_INVALID_PARAMETER,
            nullptr,
            nullptr);
        return false;
    }

    try {
        resolved->clear();
        resolved->reserve(requests.size());
        for (const auto& request : requests) {
            HMODULE module = GetModuleHandleW(request.module_name);
            if (module == nullptr) {
                set_error(
                    error,
                    HookInstallStage::ResolveModule,
                    MH_ERROR_MODULE_NOT_FOUND,
                    ERROR_MOD_NOT_FOUND,
                    request.export_name,
                    nullptr);
                resolved->clear();
                return false;
            }

            const auto target = reinterpret_cast<LPVOID>(
                GetProcAddress(module, request.export_name));
            if (target == nullptr) {
                set_error(
                    error,
                    HookInstallStage::ResolveExport,
                    MH_ERROR_FUNCTION_NOT_FOUND,
                    ERROR_PROC_NOT_FOUND,
                    request.export_name,
                    nullptr);
                resolved->clear();
                return false;
            }
            resolved->push_back({request, target});
        }
    } catch (const std::bad_alloc&) {
        set_error(
            error,
            HookInstallStage::ResolveExport,
            MH_ERROR_MEMORY_ALLOC,
            ERROR_NOT_ENOUGH_MEMORY,
            nullptr,
            nullptr);
        resolved->clear();
        return false;
    }

    if (error != nullptr) {
        *error = {};
    }
    return true;
}

MinHookApi ProductionMinHookApi() noexcept {
    return {
        MH_Initialize,
        MH_CreateHook,
        MH_QueueEnableHook,
        MH_ApplyQueued,
        MH_DisableHook,
        MH_RemoveHook,
    };
}

OwnedMinHookTransaction::OwnedMinHookTransaction(
    MinHookApi api) noexcept
    : api_(api) {
}

OwnedMinHookTransaction::~OwnedMinHookTransaction() {
    if (!committed_) {
        Rollback();
    }
}

bool OwnedMinHookTransaction::Initialize() noexcept {
    const auto status = api_.initialize();
    if (status == MH_OK || status == MH_ERROR_ALREADY_INITIALIZED) {
        return true;
    }
    error_ = {
        HookInstallStage::Initialize,
        status,
        ERROR_SUCCESS,
        nullptr,
        nullptr,
    };
    return false;
}

bool OwnedMinHookTransaction::CreateAll(
    std::span<const ResolvedApiHook> hooks) noexcept {
    try {
        owned_targets_.reserve(hooks.size());
    } catch (const std::bad_alloc&) {
        error_ = {
            HookInstallStage::Create,
            MH_ERROR_MEMORY_ALLOC,
            ERROR_NOT_ENOUGH_MEMORY,
            nullptr,
            nullptr,
        };
        return false;
    }

    for (const auto& hook : hooks) {
        const auto status = api_.create_hook(
            hook.target,
            hook.request.detour,
            hook.request.original);
        if (status != MH_OK) {
            error_ = {
                HookInstallStage::Create,
                status,
                ERROR_SUCCESS,
                hook.request.export_name,
                hook.target,
            };
            Rollback();
            return false;
        }
        owned_targets_.push_back(hook.target);
    }
    return true;
}

bool OwnedMinHookTransaction::Commit() noexcept {
    for (const auto target : owned_targets_) {
        const auto status = api_.queue_enable_hook(target);
        if (status != MH_OK) {
            error_ = {
                HookInstallStage::QueueEnable,
                status,
                ERROR_SUCCESS,
                nullptr,
                target,
            };
            Rollback();
            return false;
        }
    }

    const auto status = api_.apply_queued();
    if (status != MH_OK) {
        error_ = {
            HookInstallStage::ApplyQueued,
            status,
            ERROR_SUCCESS,
            nullptr,
            nullptr,
        };
        Rollback();
        return false;
    }

    committed_ = true;
    return true;
}

void OwnedMinHookTransaction::Rollback() noexcept {
    for (auto iterator = owned_targets_.rbegin();
         iterator != owned_targets_.rend();
         ++iterator) {
        api_.disable_hook(*iterator);
        api_.remove_hook(*iterator);
    }
    owned_targets_.clear();
    committed_ = false;
}

} // namespace gc::nesys_service

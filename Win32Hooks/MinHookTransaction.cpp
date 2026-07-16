#include "Win32Hooks/MinHookTransaction.h"

#include "plog/Log.h"

#include <array>
#include <cstddef>

namespace gc::win32_hooks {
namespace {

struct ResolvedHook {
    const HookRequest* request{};
    LPVOID target{};
};

} // namespace

ResolverApi ProductionResolverApi() noexcept
{
    return {
        .get_module_handle_w = GetModuleHandleW,
        .get_proc_address = GetProcAddress,
    };
}

MinHookApi ProductionMinHookApi() noexcept
{
    return {
        .initialize = MH_Initialize,
        .create = MH_CreateHook,
        .enable = MH_EnableHook,
        .disable = MH_DisableHook,
        .remove = MH_RemoveHook,
    };
}

MinHookTransaction::MinHookTransaction(
    ResolverApi resolver,
    MinHookApi minhook) noexcept
    : resolver_{resolver},
      minhook_{minhook}
{
}

std::expected<void, HookInstallError> MinHookTransaction::Install(
    std::span<const HookRequest> requests) noexcept
{
    if (committed_) {
        return {};
    }
    if (requests.size() > kMaxOwnedKernel32Hooks) {
        PLOG_ERROR
            << "RFID hooks: transaction rejected count="
            << requests.size() << " capacity=" << kMaxOwnedKernel32Hooks;
        return std::unexpected(HookInstallError{
            .stage = HookInstallStage::too_many_hooks,
            .win32_error = ERROR_INSUFFICIENT_BUFFER,
        });
    }
    if (requests.empty()) {
        committed_ = true;
        return {};
    }

    std::array<ResolvedHook, kMaxOwnedKernel32Hooks> resolved{};
    std::size_t resolved_count = 0;
    for (const auto& request : requests) {
        const auto module =
            resolver_.get_module_handle_w(request.module_name);
        if (module == nullptr) {
            const auto error = GetLastError();
            PLOG_ERROR
                << "RFID hooks: resolve module failed export="
                << request.export_name << " win32_error=" << error;
            return std::unexpected(HookInstallError{
                .stage = HookInstallStage::resolve_module,
                .export_name = request.export_name,
                .win32_error = error,
            });
        }

        const auto procedure =
            resolver_.get_proc_address(module, request.export_name);
        if (procedure == nullptr) {
            const auto error = GetLastError();
            PLOG_ERROR
                << "RFID hooks: resolve export failed export="
                << request.export_name << " win32_error=" << error;
            return std::unexpected(HookInstallError{
                .stage = HookInstallStage::resolve_export,
                .export_name = request.export_name,
                .win32_error = error,
            });
        }

        resolved[resolved_count++] = {
            .request = &request,
            .target = reinterpret_cast<LPVOID>(procedure),
        };
    }

    const auto initialize_status = minhook_.initialize();
    if (initialize_status != MH_OK &&
        initialize_status != MH_ERROR_ALREADY_INITIALIZED) {
        PLOG_ERROR
            << "RFID hooks: MinHook initialization failed status="
            << static_cast<int>(initialize_status);
        return std::unexpected(HookInstallError{
            .stage = HookInstallStage::initialize,
            .minhook_status = initialize_status,
        });
    }
    for (std::size_t index = 0; index < resolved_count; ++index) {
        const auto& hook = resolved[index];
        const auto status = minhook_.create(
            hook.target, hook.request->detour, hook.request->original);
        if (status != MH_OK) {
            const HookInstallError error{
                .stage = HookInstallStage::create,
                .export_name = hook.request->export_name,
                .target = hook.target,
                .minhook_status = status,
            };
            PLOG_ERROR
                << "RFID hooks: create failed export="
                << hook.request->export_name
                << " target=" << hook.target
                << " status=" << static_cast<int>(status);
            Rollback();
            return std::unexpected(error);
        }
        created_[created_count_++] = hook.target;
    }

    for (std::size_t index = 0; index < resolved_count; ++index) {
        const auto& hook = resolved[index];
        const auto status = minhook_.enable(hook.target);
        if (status != MH_OK) {
            const HookInstallError error{
                .stage = HookInstallStage::enable,
                .export_name = hook.request->export_name,
                .target = hook.target,
                .minhook_status = status,
            };
            PLOG_ERROR
                << "RFID hooks: enable failed export="
                << hook.request->export_name
                << " target=" << hook.target
                << " status=" << static_cast<int>(status);
            Rollback();
            return std::unexpected(error);
        }
        enabled_[enabled_count_++] = hook.target;
    }

    committed_ = true;
    return {};
}

void MinHookTransaction::Rollback() noexcept
{
    PLOG_WARNING
        << "RFID hooks: rollback begin enabled=" << enabled_count_
        << " created=" << created_count_;
    while (enabled_count_ != 0) {
        --enabled_count_;
        static_cast<void>(minhook_.disable(enabled_[enabled_count_]));
        enabled_[enabled_count_] = nullptr;
    }
    while (created_count_ != 0) {
        --created_count_;
        static_cast<void>(minhook_.remove(created_[created_count_]));
        created_[created_count_] = nullptr;
    }
    committed_ = false;
    PLOG_WARNING << "RFID hooks: rollback complete";
}

} // namespace gc::win32_hooks

#include "NesysServicePatch.h"

#include "NesysHookTransaction.h"
#include "NesysServiceLauncher.h"
#include "RegistryConfigOverride.h"
#include "ServerAddressOverride.h"
#include "SyntheticNetworkAdapter.h"
#include "config.h"

#include <Windows.h>

#include <atomic>
#include <cstdint>
#include <iomanip>
#include <intrin.h>
#include <memory>
#include <vector>

#include "plog/Log.h"

namespace gc::nesys_service {
namespace {

enum class InitializationState {
    Uninitialized,
    Initializing,
    Succeeded,
    Failed,
};

std::atomic<InitializationState> g_initialization{
    InitializationState::Uninitialized};
std::unique_ptr<OwnedMinHookTransaction> g_owned_hooks;
decltype(&ExitProcess) g_original_exit_process = nullptr;

__declspec(noreturn) void WINAPI service_exit_process_detour(
    UINT exit_code) {
    const auto caller = reinterpret_cast<std::uintptr_t>(
        _ReturnAddress());
    const auto image_base = reinterpret_cast<std::uintptr_t>(
        GetModuleHandleW(nullptr));
    const auto caller_rva = image_base != 0 && caller >= image_base
        ? caller - image_base
        : caller;

    try {
        PLOG_INFO
            << "NesysServicePatch: service ExitProcess"
            << " exit_code=0x" << std::hex << exit_code
            << " caller_rva=0x" << caller_rva
            << std::dec;
    } catch (...) {
    }

    g_original_exit_process(exit_code);
    __assume(0);
}

void append_service_exit_diagnostic_hook_request(
    std::vector<ApiHookRequest>& requests) {
    requests.push_back({
        L"kernel32.dll",
        "ExitProcess",
        reinterpret_cast<LPVOID>(&service_exit_process_detour),
        reinterpret_cast<LPVOID*>(&g_original_exit_process),
    });
}

const char* stage_name(HookInstallStage stage) noexcept {
    switch (stage) {
    case HookInstallStage::None:
        return "none";
    case HookInstallStage::ResolveModule:
        return "resolve_module";
    case HookInstallStage::ResolveExport:
        return "resolve_export";
    case HookInstallStage::Initialize:
        return "initialize_minhook";
    case HookInstallStage::Create:
        return "create_hook";
    case HookInstallStage::QueueEnable:
        return "queue_enable";
    case HookInstallStage::ApplyQueued:
        return "apply_queued";
    }
    return "unknown";
}

void log_hook_error(const HookInstallError& error) noexcept {
    try {
        PLOG_ERROR
            << "NesysServicePatch: hook install failed"
            << " stage=" << stage_name(error.stage)
            << " export="
            << (error.export_name != nullptr
                    ? error.export_name
                    : "<none>")
            << " target=" << error.target
            << " minhook_status="
            << static_cast<int>(error.minhook_status)
            << " win32_error=" << error.win32_error;
    } catch (...) {
    }
}

bool initialize_feature_plan(
    HMODULE loader_module,
    ProcessRole role,
    const NesysFeaturePlan& plan,
    const ConfigManager& config) {
    if (plan.server_address_override &&
        !InitializeServerAddressOverride(config.GetNesysServerIp())) {
        PLOG_ERROR
            << "NesysServicePatch: invalid NESYS server IPv4";
        return false;
    }

    if (plan.registry_config_override &&
        !InitializeRegistryConfigOverride(
            role,
            config.GetRegistryConfig())) {
        PLOG_ERROR
            << "NesysServicePatch: registry override state initialization failed";
        return false;
    }

    std::uintptr_t executable_base = 0;
    if (plan.service_ping_redirect) {
        executable_base = reinterpret_cast<std::uintptr_t>(
            GetModuleHandleW(nullptr));
        if (executable_base == 0) {
            PLOG_ERROR
                << "NesysServicePatch: main executable module unavailable";
            return false;
        }
    }

    std::vector<ApiHookRequest> requests;
    requests.reserve(plan.api_hook_count);
    if (plan.synthetic_adapter) {
        AppendSyntheticAdapterHookRequests(role, requests);
    }
    if (plan.server_address_override) {
        AppendServerAddressHookRequests(role, requests);
    }
    if (plan.registry_config_override) {
        AppendRegistryOverrideHookRequests(requests);
    }
    if (role == ProcessRole::Service) {
        append_service_exit_diagnostic_hook_request(requests);
    }
    if (plan.service_launcher) {
        AppendNesysServiceLauncherHookRequest(requests);
    }
    if (requests.size() != plan.api_hook_count) {
        PLOG_ERROR
            << "NesysServicePatch: role hook count mismatch"
            << " expected=" << plan.api_hook_count
            << " actual=" << requests.size();
        return false;
    }

    std::vector<ResolvedApiHook> resolved;
    HookInstallError resolve_error{};
    if (!ResolveApiHooks(requests, &resolved, &resolve_error)) {
        log_hook_error(resolve_error);
        return false;
    }
    if (plan.service_ping_redirect &&
        !PreflightServicePingRedirect(executable_base)) {
        return false;
    }
    if (plan.service_launcher &&
        !InitializeNesysServiceLauncher(loader_module)) {
        PLOG_ERROR
            << "NesysServicePatch: launcher state initialization failed";
        return false;
    }

    auto transaction = std::make_unique<OwnedMinHookTransaction>(
        ProductionMinHookApi());
    if (!transaction->Initialize()) {
        log_hook_error(transaction->error());
        return false;
    }
    if (!transaction->CreateAll(resolved)) {
        log_hook_error(transaction->error());
        return false;
    }
    if (plan.service_ping_redirect &&
        !PrepareServicePingRedirect(executable_base)) {
        transaction->Rollback();
        RollbackServicePingRedirect();
        return false;
    }
    if (!transaction->Commit()) {
        log_hook_error(transaction->error());
        RollbackServicePingRedirect();
        return false;
    }

    if (plan.service_ping_redirect &&
        !ActivateServicePingRedirect()) {
        transaction->Rollback();
        RollbackServicePingRedirect();
        return false;
    }

    g_owned_hooks = std::move(transaction);
    try {
        if (plan.synthetic_adapter) {
            PLOG_INFO
                << "NesysServicePatch: component active"
                << " name=synthetic_network_adapter";
        }
        if (plan.server_address_override) {
            PLOG_INFO
                << "NesysServicePatch: component active"
                << " name=server_address_override";
        }
        if (plan.registry_config_override) {
            PLOG_INFO
                << "NesysServicePatch: component active"
                << " name=registry_config_override"
                << " owned_api_hooks=3";
        }
        if (plan.service_launcher) {
            PLOG_INFO
                << "NesysServicePatch: component active"
                << " name=nesys_service_launcher";
        }
        if (plan.service_ping_redirect) {
            PLOG_INFO
                << "NesysServicePatch: component active"
                << " name=service_ping_redirect";
        }
        if (role == ProcessRole::Service) {
            PLOG_INFO
                << "NesysServicePatch: component active"
                << " name=service_exit_diagnostics";
        }
        PLOG_INFO
            << "NesysServicePatch: all role hooks active"
            << " role=" << ProcessRoleName(role)
            << " network=" << plan.network_virtualization
            << " registry=" << plan.registry_virtualization
            << " api_hooks=" << plan.api_hook_count;
        if (plan.synthetic_adapter) {
            PLOG_INFO
                << "NesysServicePatch: synthetic adapter"
                << " name=" << kSyntheticAdapterName
                << " mac=DE-AD-BE-EF-00-01"
                << " index=0x" << std::hex
                << kSyntheticInterfaceIndex
                << " ipv4=" << kSyntheticIpv4
                << " link_state=up"
                << std::dec;
        }
    } catch (...) {
    }
    return true;
}

} // namespace

bool NesysServicePatchInit(
    HMODULE loader_module,
    ProcessRole role) noexcept {
    InitializationState expected =
        InitializationState::Uninitialized;
    if (!g_initialization.compare_exchange_strong(
            expected,
            InitializationState::Initializing)) {
        return g_initialization.load() ==
            InitializationState::Succeeded;
    }

    bool success = false;
    try {
        const auto& config = ConfigManager::instance();
        const bool network_enabled =
            config.GetEnableNesysServiceAdapterPatch();
        const bool registry_enabled =
            config.GetEnableRegistryConfigOverride();
        const auto plan = ResolveNesysFeaturePlan(
            role,
            network_enabled,
            registry_enabled);
        PLOG_INFO
            << "NesysServicePatch: init"
            << " role=" << ProcessRoleName(role)
            << " network=" << network_enabled
            << " registry=" << registry_enabled;

        success = !plan.enabled ||
            initialize_feature_plan(
                loader_module,
                role,
                plan,
                config);
        if (!plan.enabled) {
            PLOG_INFO
                << "NesysServicePatch: all policies disabled; installed no hooks";
        }
    } catch (const std::exception& error) {
        try {
            PLOG_ERROR
                << "NesysServicePatch: initialization exception="
                << error.what();
        } catch (...) {
        }
        success = false;
    } catch (...) {
        success = false;
    }

    g_initialization.store(
        success
            ? InitializationState::Succeeded
            : InitializationState::Failed);
    return success;
}

} // namespace gc::nesys_service

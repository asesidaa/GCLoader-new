#include "Nesys/NesysServicePatch.h"
#include "Nesys/ThreadPriorityOverride.h"
#include "Nesys/Diagnostics/RequestHooks.h"
#include "Nesys/Launcher/NesysServiceLauncher.h"
#include "Nesys/Registry/RegistryConfigOverride.h"
#include "Nesys/Network/ServerAddressOverride.h"
#include "Nesys/Network/SyntheticNetworkAdapter.h"
#include "Platform/Win32/Hooking/HookDiagnostics.h"
#include <atomic>
#include <intrin.h>
#include <utility>
#include "plog/Log.h"

namespace gc::nesys_service {
namespace {
std::atomic_bool g_prepared{};
decltype(&ExitProcess) g_original_exit_process{};
        __declspec(noreturn) void WINAPI service_exit_process_detour(
            UINT exit_code)
        {
            const auto caller = reinterpret_cast<std::uintptr_t>(
                _ReturnAddress());
            const auto image_base = reinterpret_cast<std::uintptr_t>(
                GetModuleHandleW(nullptr));
            const auto caller_rva = image_base != 0 && caller >= image_base
                                        ? caller - image_base
                                        : caller;

            try
            {
                PLOG_INFO
                    << "NesysServicePatch: service ExitProcess"
                    << " exit_code=0x" << std::hex << exit_code
                    << " caller_rva=0x" << caller_rva
                    << std::dec;
            }
            catch (...)
            {
            }

            g_original_exit_process(exit_code);
            __assume(0);
        }


hooking::HookError StateError(std::string_view site, DWORD code = ERROR_INVALID_DATA) noexcept {
    return {.stage = hooking::HookStage::invalid_plan,
        .identity = {"Nesys", site}, .win32_error = code};
}
}
std::expected<void, hooking::HookError> AddNesysHooks(
    hooking::HookPlan& hooks, HMODULE loader_module, ProcessRole role, NesysSettings settings) noexcept {
    if (g_prepared.exchange(true))
        return std::unexpected(StateError("prepare", ERROR_ALREADY_INITIALIZED));
    try {
        const auto plan = ResolveNesysFeaturePlan(
            role, settings.adapter_patch_enabled(), settings.registry_override().has_value());
        if (!plan.enabled) return {};
        if (plan.server_address_override &&
            !InitializeServerAddressOverride(std::move(settings).server_address()))
            return std::unexpected(StateError("server_address"));
        if (plan.registry_config_override) {
            auto values = std::move(settings).registry_override();
            if (!values || !InitializeRegistryConfigOverride(role, std::move(*values)))
                return std::unexpected(StateError("registry_values"));
        }
        if (plan.thread_priority_override && !InitializeThreadPriorityOverride(role))
            return std::unexpected(StateError("thread_priority"));
        if (plan.service_launcher && !InitializeNesysServiceLauncher(loader_module))
            return std::unexpected(StateError("launcher"));
        const auto before = hooks.size();
        if (plan.synthetic_adapter) {
            if (const auto added = AddSyntheticAdapterHooks(role, hooks); !added) return added;
        }
        if (plan.server_address_override) {
            if (const auto added = AddServerAddressHooks(role, hooks); !added) return added;
        }
        if (plan.registry_config_override) {
            if (const auto added = AddRegistryOverrideHooks(hooks); !added) return added;
        }
        if (plan.thread_priority_override) {
            if (const auto added = AddThreadPriorityHook(hooks); !added) return added;
        }
        if (plan.request_pipeline_diagnostics) {
            if (const auto added = diagnostics::AddServiceRequestPipelineHooks(hooks); !added) return added;
        }
        if (role == ProcessRole::Service) {
            if (const auto added = hooks.AddInlineExport(
                    {"NesysExit", "ExitProcess"}, {L"kernel32.dll", "ExitProcess"},
                    &service_exit_process_detour, &g_original_exit_process); !added) return added;
        }
        if (plan.service_launcher) {
            if (const auto added = AddNesysServiceLauncherHook(hooks); !added) return added;
        }
        if (hooks.size() - before != plan.api_hook_count)
            return std::unexpected(StateError("hook_count"));
        PLOG_INFO << "NesysServicePatch: role hooks prepared"
            << " role=" << ProcessRoleName(role)
            << " network=" << plan.network_virtualization
            << " registry=" << plan.registry_virtualization
            << " api_hooks=" << plan.api_hook_count;
        return {};
    } catch (...) {
        return std::unexpected(StateError("prepare", ERROR_NOT_ENOUGH_MEMORY));
    }
}
} // namespace gc::nesys_service

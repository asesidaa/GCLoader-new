#include "Win32Hooks/MinHookTransaction.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <iostream>
#include <span>
#include <string_view>
#include <vector>

namespace {

using gc::win32_hooks::HookInstallError;
using gc::win32_hooks::HookInstallStage;
using gc::win32_hooks::HookRequest;
using gc::win32_hooks::MinHookApi;
using gc::win32_hooks::ResolverApi;

enum class OperationKind {
    resolve_module,
    resolve_export,
    initialize,
    create,
    enable,
    disable,
    remove,
};

struct Operation {
    OperationKind kind{};
    LPVOID target{};
    std::size_t index{};
};

struct FakeBackend {
    int fail_module{-1};
    int fail_export{-1};
    int fail_create{-1};
    int fail_enable{-1};
    MH_STATUS initialize_status{MH_OK};
    std::size_t module_calls{};
    std::size_t export_calls{};
    std::size_t create_calls{};
    std::size_t enable_calls{};
    std::vector<Operation> operations;
    std::vector<LPVOID> created;
    std::vector<LPVOID> enabled;
    std::vector<LPVOID> disabled;
    std::vector<LPVOID> removed;
};

FakeBackend* g_fake{};

LPVOID target_at(std::size_t index)
{
    return reinterpret_cast<LPVOID>(
        std::uintptr_t{0x1000} + index * 0x100);
}

HMODULE WINAPI FakeGetModuleHandleW(LPCWSTR)
{
    const auto index = g_fake->module_calls++;
    g_fake->operations.push_back({
        .kind = OperationKind::resolve_module,
        .index = index,
    });
    if (static_cast<int>(index) == g_fake->fail_module) {
        SetLastError(static_cast<DWORD>(1200 + index));
        return nullptr;
    }
    return reinterpret_cast<HMODULE>(
        std::uintptr_t{0x5000} + index * 0x100);
}

FARPROC WINAPI FakeGetProcAddress(HMODULE, LPCSTR)
{
    const auto index = g_fake->export_calls++;
    g_fake->operations.push_back({
        .kind = OperationKind::resolve_export,
        .index = index,
    });
    if (static_cast<int>(index) == g_fake->fail_export) {
        SetLastError(static_cast<DWORD>(2200 + index));
        return nullptr;
    }
    return reinterpret_cast<FARPROC>(target_at(index));
}

MH_STATUS WINAPI FakeInitialize()
{
    g_fake->operations.push_back({.kind = OperationKind::initialize});
    return g_fake->initialize_status;
}

MH_STATUS WINAPI FakeCreate(LPVOID target, LPVOID, LPVOID* original)
{
    const auto index = g_fake->create_calls++;
    g_fake->operations.push_back({
        .kind = OperationKind::create,
        .target = target,
        .index = index,
    });
    if (static_cast<int>(index) == g_fake->fail_create) {
        return MH_ERROR_MEMORY_ALLOC;
    }
    g_fake->created.push_back(target);
    if (original != nullptr) {
        *original = reinterpret_cast<LPVOID>(
            reinterpret_cast<std::uintptr_t>(target) + 1);
    }
    return MH_OK;
}

MH_STATUS WINAPI FakeEnable(LPVOID target)
{
    const auto index = g_fake->enable_calls++;
    g_fake->operations.push_back({
        .kind = OperationKind::enable,
        .target = target,
        .index = index,
    });
    if (static_cast<int>(index) == g_fake->fail_enable) {
        return MH_ERROR_MEMORY_PROTECT;
    }
    g_fake->enabled.push_back(target);
    return MH_OK;
}

MH_STATUS WINAPI FakeDisable(LPVOID target)
{
    g_fake->operations.push_back({
        .kind = OperationKind::disable,
        .target = target,
    });
    g_fake->disabled.push_back(target);
    return MH_OK;
}

MH_STATUS WINAPI FakeRemove(LPVOID target)
{
    g_fake->operations.push_back({
        .kind = OperationKind::remove,
        .target = target,
    });
    g_fake->removed.push_back(target);
    return MH_OK;
}

ResolverApi FakeResolver()
{
    return {
        .get_module_handle_w = FakeGetModuleHandleW,
        .get_proc_address = FakeGetProcAddress,
    };
}

MinHookApi FakeMinHook()
{
    return {
        .initialize = FakeInitialize,
        .create = FakeCreate,
        .enable = FakeEnable,
        .disable = FakeDisable,
        .remove = FakeRemove,
    };
}

struct Requests {
    std::array<LPVOID, 3> originals{};
    std::array<HookRequest, 3> values{
        HookRequest{L"kernel32.dll", "One",
                    reinterpret_cast<LPVOID>(0x7100), &originals[0]},
        HookRequest{L"kernel32.dll", "Two",
                    reinterpret_cast<LPVOID>(0x7200), &originals[1]},
        HookRequest{L"kernel32.dll", "Three",
                    reinterpret_cast<LPVOID>(0x7300), &originals[2]},
    };
};

int expect(bool condition, std::string_view name)
{
    if (condition) {
        return 0;
    }
    std::cerr << name << " failed\n";
    return 1;
}

int expect_error(
    const std::expected<void, HookInstallError>& result,
    HookInstallStage stage,
    LPCSTR export_name,
    LPVOID target,
    DWORD win32_error,
    MH_STATUS minhook_status,
    std::string_view name)
{
    if (result) {
        std::cerr << name << ": expected failure\n";
        return 1;
    }

    const auto& error = result.error();
    const bool same_name =
        export_name == nullptr
            ? error.export_name == nullptr
            : error.export_name != nullptr &&
                std::string_view{error.export_name} == export_name;
    return expect(
        error.stage == stage && same_name && error.target == target &&
            error.win32_error == win32_error &&
            error.minhook_status == minhook_status,
        name);
}

std::vector<LPVOID> reversed_targets(std::size_t count)
{
    std::vector<LPVOID> result;
    for (std::size_t index = count; index != 0; --index) {
        result.push_back(target_at(index - 1));
    }
    return result;
}

bool all_disables_precede_removes(const FakeBackend& fake)
{
    const auto first_remove = std::ranges::find_if(
        fake.operations,
        [](const Operation& operation) {
            return operation.kind == OperationKind::remove;
        });
    return std::ranges::none_of(
        std::ranges::subrange{first_remove, fake.operations.end()},
        [](const Operation& operation) {
            return operation.kind == OperationKind::disable;
        });
}

int test_success_and_already_initialized()
{
    int failures = 0;
    Requests requests;

    FakeBackend success;
    g_fake = &success;
    gc::win32_hooks::MinHookTransaction transaction{
        FakeResolver(), FakeMinHook()};
    failures += expect(transaction.Install(requests.values).has_value(),
                       "successful transaction");
    failures += expect(
        success.created == std::vector<LPVOID>{
            target_at(0), target_at(1), target_at(2)},
        "success creates each resolved target once");
    failures += expect(success.enabled == success.created,
                       "success enables each resolved target once");
    failures += expect(success.disabled.empty() && success.removed.empty(),
                       "success does not roll back");
    failures += expect(
        std::ranges::none_of(
            success.enabled,
            [](LPVOID target) {
                return target == MH_ALL_HOOKS ||
                    target == reinterpret_cast<LPVOID>(0x9999);
            }),
        "transaction never enables all or unrelated hooks");

    FakeBackend already_initialized{
        .initialize_status = MH_ERROR_ALREADY_INITIALIZED,
    };
    g_fake = &already_initialized;
    gc::win32_hooks::MinHookTransaction shared{
        FakeResolver(), FakeMinHook()};
    failures += expect(shared.Install(requests.values).has_value(),
                       "already initialized MinHook is accepted");

    const auto production_resolver =
        gc::win32_hooks::ProductionResolverApi();
    const auto production_minhook =
        gc::win32_hooks::ProductionMinHookApi();
    failures += expect(
        production_resolver.get_module_handle_w != nullptr &&
            production_resolver.get_proc_address != nullptr &&
            production_minhook.initialize != nullptr &&
            production_minhook.create != nullptr &&
            production_minhook.enable != nullptr &&
            production_minhook.disable != nullptr &&
            production_minhook.remove != nullptr,
        "production transaction APIs are complete");
    return failures;
}

int test_resolution_failures()
{
    int failures = 0;
    Requests requests;

    for (std::size_t index = 0; index < requests.values.size(); ++index) {
        FakeBackend module_failure{
            .fail_module = static_cast<int>(index),
        };
        g_fake = &module_failure;
        gc::win32_hooks::MinHookTransaction transaction{
            FakeResolver(), FakeMinHook()};
        const auto result = transaction.Install(requests.values);
        failures += expect_error(
            result, HookInstallStage::resolve_module,
            requests.values[index].export_name, nullptr,
            static_cast<DWORD>(1200 + index), MH_OK,
            "module resolution error detail");
        failures += expect(
            module_failure.module_calls == index + 1 &&
                module_failure.export_calls == index &&
                module_failure.create_calls == 0 &&
                module_failure.enable_calls == 0,
            "module failure performs no MinHook operation");

        FakeBackend export_failure{
            .fail_export = static_cast<int>(index),
        };
        g_fake = &export_failure;
        gc::win32_hooks::MinHookTransaction export_transaction{
            FakeResolver(), FakeMinHook()};
        const auto export_result =
            export_transaction.Install(requests.values);
        failures += expect_error(
            export_result, HookInstallStage::resolve_export,
            requests.values[index].export_name, nullptr,
            static_cast<DWORD>(2200 + index), MH_OK,
            "export resolution error detail");
        failures += expect(
            export_failure.module_calls == index + 1 &&
                export_failure.export_calls == index + 1 &&
                export_failure.create_calls == 0 &&
                export_failure.enable_calls == 0,
            "export failure performs no MinHook operation");
    }
    return failures;
}

int test_initialize_and_capacity_failures()
{
    int failures = 0;
    Requests requests;

    FakeBackend initialize_failure{
        .initialize_status = MH_ERROR_MEMORY_ALLOC,
    };
    g_fake = &initialize_failure;
    gc::win32_hooks::MinHookTransaction transaction{
        FakeResolver(), FakeMinHook()};
    failures += expect_error(
        transaction.Install(requests.values),
        HookInstallStage::initialize, nullptr, nullptr,
        ERROR_SUCCESS, MH_ERROR_MEMORY_ALLOC,
        "initialize error detail");
    failures += expect(
        initialize_failure.module_calls == requests.values.size() &&
            initialize_failure.export_calls == requests.values.size() &&
            initialize_failure.create_calls == 0,
        "initialize happens after complete resolution");

    std::array<HookRequest,
               gc::win32_hooks::kMaxOwnedKernel32Hooks + 1> too_many{};
    FakeBackend capacity_failure;
    g_fake = &capacity_failure;
    gc::win32_hooks::MinHookTransaction bounded{
        FakeResolver(), FakeMinHook()};
    failures += expect_error(
        bounded.Install(too_many), HookInstallStage::too_many_hooks,
        nullptr, nullptr, ERROR_INSUFFICIENT_BUFFER, MH_OK,
        "fixed transaction capacity");
    failures += expect(capacity_failure.operations.empty(),
                       "capacity failure performs no backend work");
    return failures;
}

int test_create_failures()
{
    int failures = 0;
    Requests requests;

    for (std::size_t index = 0; index < requests.values.size(); ++index) {
        FakeBackend fake{
            .fail_create = static_cast<int>(index),
        };
        g_fake = &fake;
        gc::win32_hooks::MinHookTransaction transaction{
            FakeResolver(), FakeMinHook()};
        const auto result = transaction.Install(requests.values);
        failures += expect_error(
            result, HookInstallStage::create,
            requests.values[index].export_name, target_at(index),
            ERROR_SUCCESS, MH_ERROR_MEMORY_ALLOC,
            "create error detail");
        failures += expect(fake.removed == reversed_targets(index),
                           "create rollback removes earlier targets in reverse");
        failures += expect(fake.disabled.empty(),
                           "create rollback has no enabled targets");
    }
    return failures;
}

int test_enable_failures()
{
    int failures = 0;
    Requests requests;

    for (std::size_t index = 0; index < requests.values.size(); ++index) {
        FakeBackend fake{
            .fail_enable = static_cast<int>(index),
        };
        g_fake = &fake;
        gc::win32_hooks::MinHookTransaction transaction{
            FakeResolver(), FakeMinHook()};
        const auto result = transaction.Install(requests.values);
        failures += expect_error(
            result, HookInstallStage::enable,
            requests.values[index].export_name, target_at(index),
            ERROR_SUCCESS, MH_ERROR_MEMORY_PROTECT,
            "enable error detail");
        failures += expect(fake.disabled == reversed_targets(index),
                           "enable rollback disables earlier targets in reverse");
        failures += expect(
            fake.removed == reversed_targets(requests.values.size()),
            "enable rollback removes every created target in reverse");
        failures += expect(all_disables_precede_removes(fake),
                           "enable rollback disables before removing");
    }
    return failures;
}

} // namespace

int main()
{
    const int failures =
        test_success_and_already_initialized() +
        test_resolution_failures() +
        test_initialize_and_capacity_failures() +
        test_create_failures() +
        test_enable_failures();
    return failures == 0 ? 0 : 1;
}

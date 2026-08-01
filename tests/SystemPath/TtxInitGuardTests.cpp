#include "SystemPath/TtxInitGuard.h"

#include <Windows.h>

#include <array>
#include <cstdint>
#include <expected>
#include <iostream>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace {

int Expect(bool condition, std::string_view message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        return 1;
    }
    return 0;
}

enum class RuntimeOperation {
    call_original,
    get_last_error,
    publish_failure,
};

struct RuntimeFake {
    int original_result{1};
    DWORD last_error{ERROR_PATH_NOT_FOUND};
    std::array<unsigned int, 4> args{};
    int original_calls{};
    int get_last_error_calls{};
    int publish_calls{};
    DWORD captured_error{ERROR_SUCCESS};
    std::string configured_path;
    std::filesystem::path resolved_path;
    std::vector<RuntimeOperation> order;
};

int CallOriginal(
    void* context,
    unsigned int priority,
    unsigned int game_version,
    unsigned int update_step,
    unsigned int update_options) noexcept
{
    auto& fake = *static_cast<RuntimeFake*>(context);
    ++fake.original_calls;
    fake.args = {priority, game_version, update_step, update_options};
    fake.order.push_back(RuntimeOperation::call_original);
    return fake.original_result;
}

DWORD GetRuntimeLastError(void* context) noexcept
{
    auto& fake = *static_cast<RuntimeFake*>(context);
    ++fake.get_last_error_calls;
    fake.order.push_back(RuntimeOperation::get_last_error);
    return fake.last_error;
}

void PublishFailure(
    void* context,
    DWORD error,
    const gc::system_path::RuntimeRoot& root) noexcept
{
    auto& fake = *static_cast<RuntimeFake*>(context);
    ++fake.publish_calls;
    fake.captured_error = error;
    fake.configured_path = root.configured_path;
    fake.resolved_path = root.resolved_path;
    fake.order.push_back(RuntimeOperation::publish_failure);
}

gc::system_path::TtxGuardRuntimeActions RuntimeActions(RuntimeFake& fake)
{
    return {
        .context = &fake,
        .call_original = &CallOriginal,
        .get_last_error = &GetRuntimeLastError,
        .publish_failure = &PublishFailure,
    };
}

enum class InstallOperation {
    get_module,
    get_export,
    get_last_error,
    create_disabled,
    enable,
    reset,
};

struct InstallFake {
    bool module_missing{};
    bool export_missing{};
    std::uint32_t create_error{};
    std::uint32_t enable_error{};
    DWORD last_error{ERROR_MOD_NOT_FOUND};
    std::wstring module_name;
    std::string export_name;
    HMODULE export_module{};
    void* create_target{};
    void* create_detour{};
    int module_calls{};
    int export_calls{};
    int get_last_error_calls{};
    int create_disabled_calls{};
    int enable_calls{};
    int reset_calls{};
    std::vector<InstallOperation> order;
};

const auto kFakeModule = reinterpret_cast<HMODULE>(0x51000000);
const auto kFakeTarget = reinterpret_cast<FARPROC>(0x510010F0);
const auto kFakeDetour = reinterpret_cast<void*>(0x62002000);

HMODULE GetModule(void* context, LPCWSTR name) noexcept
{
    auto& fake = *static_cast<InstallFake*>(context);
    ++fake.module_calls;
    fake.module_name = name == nullptr ? std::wstring{} : std::wstring{name};
    fake.order.push_back(InstallOperation::get_module);
    return fake.module_missing ? nullptr : kFakeModule;
}

FARPROC GetExport(
    void* context,
    HMODULE module,
    LPCSTR name) noexcept
{
    auto& fake = *static_cast<InstallFake*>(context);
    ++fake.export_calls;
    fake.export_module = module;
    fake.export_name = name == nullptr ? std::string{} : std::string{name};
    fake.order.push_back(InstallOperation::get_export);
    return fake.export_missing ? nullptr : kFakeTarget;
}

DWORD GetInstallLastError(void* context) noexcept
{
    auto& fake = *static_cast<InstallFake*>(context);
    ++fake.get_last_error_calls;
    fake.order.push_back(InstallOperation::get_last_error);
    return fake.last_error;
}

std::expected<void, std::uint32_t> CreateDisabled(
    void* context,
    void* target,
    void* detour) noexcept
{
    auto& fake = *static_cast<InstallFake*>(context);
    ++fake.create_disabled_calls;
    fake.create_target = target;
    fake.create_detour = detour;
    fake.order.push_back(InstallOperation::create_disabled);
    if (fake.create_error != 0) {
        return std::unexpected(fake.create_error);
    }
    return {};
}

std::expected<void, std::uint32_t> Enable(void* context) noexcept
{
    auto& fake = *static_cast<InstallFake*>(context);
    ++fake.enable_calls;
    fake.order.push_back(InstallOperation::enable);
    if (fake.enable_error != 0) {
        return std::unexpected(fake.enable_error);
    }
    return {};
}

void Reset(void* context) noexcept
{
    auto& fake = *static_cast<InstallFake*>(context);
    ++fake.reset_calls;
    fake.order.push_back(InstallOperation::reset);
}

gc::system_path::TtxGuardInstallActions InstallActions(InstallFake& fake)
{
    return {
        .context = &fake,
        .detour = kFakeDetour,
        .get_module = &GetModule,
        .get_export = &GetExport,
        .get_last_error = &GetInstallLastError,
        .create_disabled = &CreateDisabled,
        .enable = &Enable,
        .reset = &Reset,
    };
}

int TestRuntimeBoundary()
{
    static_assert(std::is_same_v<
        gc::system_path::TtxUdlInitFn,
        int(__cdecl*)(
            unsigned int, unsigned int, unsigned int, unsigned int)>);

    int failures = 0;
    failures += Expect(
        std::wstring_view{gc::system_path::kTtxModuleName} ==
            L"TtxUpdateDownloader.dll",
        "Ttx guard uses exact observed module");
    failures += Expect(
        std::string_view{gc::system_path::kTtxUdlInitExport} ==
            "?TtxUDLInit@@YAHKKKK@Z",
        "Ttx guard uses observed decorated export");

    const gc::system_path::RuntimeRoot root{
        .configured_path = ".\\system",
        .resolved_path = L"H:\\遊戲\\system",
        .redirect_enabled = true,
    };

    RuntimeFake success{
        .original_result = 7,
    };
    const int success_result = gc::system_path::InvokeTtxUdlInitGuard(
        3, 471, 9, 0x20, root, RuntimeActions(success));
    failures += Expect(
        success_result == 7 && success.original_calls == 1 &&
            success.args ==
                std::array<unsigned int, 4>{3, 471, 9, 0x20} &&
            success.get_last_error_calls == 0 && success.publish_calls == 0 &&
            success.order ==
                std::vector{RuntimeOperation::call_original},
        "successful Ttx init returns unchanged");

    RuntimeFake failed{
        .original_result = 0,
        .last_error = ERROR_PATH_NOT_FOUND,
    };
    const int failed_result = gc::system_path::InvokeTtxUdlInitGuard(
        3, 471, 9, 0x20, root, RuntimeActions(failed));
    failures += Expect(
        failed_result == 0 && failed.original_calls == 1 &&
            failed.get_last_error_calls == 1 &&
            failed.captured_error == ERROR_PATH_NOT_FOUND &&
            failed.publish_calls == 1 &&
            failed.configured_path == ".\\system" &&
            failed.resolved_path == L"H:\\遊戲\\system" &&
            failed.order == std::vector{
                RuntimeOperation::call_original,
                RuntimeOperation::get_last_error,
                RuntimeOperation::publish_failure,
            },
        "failed Ttx init captures last error before publication");
    return failures;
}

int TestInstallValidationAndResolution()
{
    using gc::system_path::InstallTtxInitGuard;
    using gc::system_path::TtxGuardInstallStage;

    int failures = 0;
    const auto invalid = InstallTtxInitGuard({});
    failures += Expect(
        !invalid &&
            invalid.error().stage == TtxGuardInstallStage::invalid_actions,
        "missing install actions are rejected");

    InstallFake module_failure{
        .module_missing = true,
        .last_error = ERROR_MOD_NOT_FOUND,
    };
    const auto missing_module =
        InstallTtxInitGuard(InstallActions(module_failure));
    failures += Expect(
        !missing_module &&
            missing_module.error().stage ==
                TtxGuardInstallStage::resolve_module &&
            missing_module.error().win32_error == ERROR_MOD_NOT_FOUND &&
            module_failure.module_name == L"TtxUpdateDownloader.dll" &&
            module_failure.module_calls == 1 &&
            module_failure.get_last_error_calls == 1 &&
            module_failure.export_calls == 0 &&
            module_failure.create_disabled_calls == 0 &&
            module_failure.enable_calls == 0 &&
            module_failure.reset_calls == 0,
        "module resolution failure stops before export lookup");

    InstallFake export_failure{
        .export_missing = true,
        .last_error = ERROR_PROC_NOT_FOUND,
    };
    const auto missing_export =
        InstallTtxInitGuard(InstallActions(export_failure));
    failures += Expect(
        !missing_export &&
            missing_export.error().stage ==
                TtxGuardInstallStage::resolve_export &&
            missing_export.error().win32_error == ERROR_PROC_NOT_FOUND &&
            export_failure.module_name == L"TtxUpdateDownloader.dll" &&
            export_failure.export_name == "?TtxUDLInit@@YAHKKKK@Z" &&
            export_failure.export_module == kFakeModule &&
            export_failure.export_calls == 1 &&
            export_failure.get_last_error_calls == 1 &&
            export_failure.create_disabled_calls == 0 &&
            export_failure.reset_calls == 0,
        "exact export resolution failure performs no hook creation");
    return failures;
}

int TestInstallTransaction()
{
    using gc::system_path::InstallTtxInitGuard;
    using gc::system_path::TtxGuardInstallStage;

    int failures = 0;
    InstallFake create_failure{
        .create_error = 41,
    };
    const auto not_created =
        InstallTtxInitGuard(InstallActions(create_failure));
    failures += Expect(
        !not_created &&
            not_created.error().stage ==
                TtxGuardInstallStage::create_hook &&
            not_created.error().safetyhook_error == 41 &&
            create_failure.create_target ==
                reinterpret_cast<void*>(kFakeTarget) &&
            create_failure.create_detour == kFakeDetour &&
            create_failure.create_disabled_calls == 1 &&
            create_failure.enable_calls == 0 &&
            create_failure.reset_calls == 1,
        "creation failure resets without enabling");

    InstallFake enable_failure{
        .enable_error = 52,
    };
    const auto not_enabled =
        InstallTtxInitGuard(InstallActions(enable_failure));
    failures += Expect(
        !not_enabled &&
            not_enabled.error().stage ==
                TtxGuardInstallStage::enable_hook &&
            not_enabled.error().safetyhook_error == 52 &&
            enable_failure.create_disabled_calls == 1 &&
            enable_failure.enable_calls == 1 &&
            enable_failure.reset_calls == 1 &&
            enable_failure.order == std::vector{
                InstallOperation::get_module,
                InstallOperation::get_export,
                InstallOperation::create_disabled,
                InstallOperation::enable,
                InstallOperation::reset,
            },
        "enable failure resets the disabled hook");

    InstallFake success;
    const auto installed = InstallTtxInitGuard(InstallActions(success));
    failures += Expect(
        installed && success.module_name == L"TtxUpdateDownloader.dll" &&
            success.export_name == "?TtxUDLInit@@YAHKKKK@Z" &&
            success.create_disabled_calls == 1 && success.enable_calls == 1 &&
            success.reset_calls == 0 &&
            success.order == std::vector{
                InstallOperation::get_module,
                InstallOperation::get_export,
                InstallOperation::create_disabled,
                InstallOperation::enable,
            },
        "exact supported Ttx export installs disabled then enables");
    return failures;
}

} // namespace

int main()
{
    const int failures =
        TestRuntimeBoundary() +
        TestInstallValidationAndResolution() +
        TestInstallTransaction();
    return failures == 0 ? 0 : 1;
}

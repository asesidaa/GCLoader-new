#include "Rfid/Feature.h"

#include <Windows.h>

#include <cstdint>
#include <expected>
#include <iostream>
#include <string_view>
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

enum class Operation {
    install_kernel32,
    install_ttx,
    rollback_kernel32,
    deactivate_kernel32,
};

struct Fake {
    bool kernel_fails{};
    bool ttx_fails{};
    gc::win32_hooks::HookInstallError kernel_error{
        .stage = gc::win32_hooks::HookInstallStage::enable,
        .export_name = "CreateFileW",
        .target = reinterpret_cast<void*>(0x4100),
        .win32_error = ERROR_SUCCESS,
        .minhook_status = MH_ERROR_MEMORY_PROTECT,
    };
    gc::system_path::TtxGuardInstallError ttx_error{
        .stage = gc::system_path::TtxGuardInstallStage::enable_hook,
        .win32_error = ERROR_SUCCESS,
        .safetyhook_error = 6,
    };
    std::vector<Operation> order;
};

std::expected<void, gc::win32_hooks::HookInstallError>
InstallKernel32(void* context) noexcept
{
    auto& fake = *static_cast<Fake*>(context);
    fake.order.push_back(Operation::install_kernel32);
    if (fake.kernel_fails) {
        return std::unexpected(fake.kernel_error);
    }
    return {};
}

std::expected<void, gc::system_path::TtxGuardInstallError>
InstallTtx(void* context) noexcept
{
    auto& fake = *static_cast<Fake*>(context);
    fake.order.push_back(Operation::install_ttx);
    if (fake.ttx_fails) {
        return std::unexpected(fake.ttx_error);
    }
    return {};
}

void RollbackKernel32(void* context) noexcept
{
    static_cast<Fake*>(context)->order.push_back(
        Operation::rollback_kernel32);
}

void DeactivateKernel32(void* context) noexcept
{
    static_cast<Fake*>(context)->order.push_back(
        Operation::deactivate_kernel32);
}

gc::rfid::FeatureHookLayerActions Actions(Fake& fake)
{
    return {
        .context = &fake,
        .install_kernel32 = &InstallKernel32,
        .install_ttx = &InstallTtx,
        .rollback_kernel32 = &RollbackKernel32,
        .deactivate_kernel32 = &DeactivateKernel32,
    };
}

int TestKernelFailure()
{
    Fake fake{
        .kernel_fails = true,
    };
    const auto result = gc::rfid::InstallFeatureHookLayers(Actions(fake));
    return Expect(
        !result &&
            result.error().stage ==
                gc::rfid::FeatureFailureStage::hook_installation &&
            result.error().hook.stage ==
                gc::win32_hooks::HookInstallStage::enable &&
            result.error().hook.export_name == fake.kernel_error.export_name &&
            result.error().hook.minhook_status ==
                fake.kernel_error.minhook_status &&
            fake.order == std::vector{
                Operation::install_kernel32,
                Operation::deactivate_kernel32,
            },
        "Kernel32 failure deactivates dispatch and avoids Ttx");
}

int TestTtxFailureRollsBackKernel32()
{
    Fake fake{
        .ttx_fails = true,
    };
    const auto result = gc::rfid::InstallFeatureHookLayers(Actions(fake));
    return Expect(
        !result &&
            result.error().stage ==
                gc::rfid::FeatureFailureStage::ttx_guard_installation &&
            result.error().ttx.stage ==
                gc::system_path::TtxGuardInstallStage::enable_hook &&
            result.error().ttx.safetyhook_error ==
                fake.ttx_error.safetyhook_error &&
            fake.order == std::vector{
                Operation::install_kernel32,
                Operation::install_ttx,
                Operation::rollback_kernel32,
                Operation::deactivate_kernel32,
            },
        "Ttx failure rolls back committed Kernel32 layer");
}

int TestSuccessAlwaysInstallsTtx()
{
    Fake fake;
    const auto result = gc::rfid::InstallFeatureHookLayers(Actions(fake));
    return Expect(
        result && fake.order == std::vector{
            Operation::install_kernel32,
            Operation::install_ttx,
        },
        "feature installs Kernel32 before Ttx for every root policy");
}

} // namespace

int main()
{
    const int failures =
        TestKernelFailure() +
        TestTtxFailureRollsBackKernel32() +
        TestSuccessAlwaysInstallsTtx();
    return failures == 0 ? 0 : 1;
}

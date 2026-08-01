#include "Rfid/Feature.h"

#include "Rfid/Runtime.h"
#include "SystemPath/SystemPathRouter.h"
#include "TestModeStorage/Hooks.h"
#include "Win32Hooks/Kernel32Hooks.h"
#include "Config/config.h"
#include "Input/Types/PhysicalKey.h"
#include "Input/Win32/PhysicalKeyWin32.h"
#include "plog/Log.h"

#include <iomanip>
#include <memory>
#include <new>
#include <span>
#include <string>
#include <string_view>

namespace gc::rfid {
namespace {

struct FeatureState {
    FeatureState(
        int virtual_key,
        bool storage_enabled,
        const gc::system_path::RuntimeRoot& system_root)
        : rfid{virtual_key},
          storage{storage_enabled},
          system{system_root},
          kernel32{rfid, storage, system},
          ttx{system_root}
    {
    }

    Runtime rfid;
    gc::testmode_storage::Hooks storage;
    gc::system_path::SystemPathRouter system;
    gc::win32_hooks::Kernel32Hooks kernel32;
    gc::win32_hooks::MinHookTransaction transaction;
    gc::system_path::TtxInitGuard ttx;
};

struct ProductionHookLayerContext {
    FeatureState* state{};
    std::span<const gc::win32_hooks::HookRequest> requests;
};

FeatureState* g_feature_state{};

std::string WideToUtf8(std::wstring_view value)
{
    if (value.empty()) {
        return {};
    }
    const int count = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        nullptr,
        0,
        nullptr,
        nullptr);
    if (count <= 0) {
        return {};
    }
    std::string result(static_cast<std::size_t>(count), '\0');
    if (WideCharToMultiByte(
            CP_UTF8,
            WC_ERR_INVALID_CHARS,
            value.data(),
            static_cast<int>(value.size()),
            result.data(),
            count,
            nullptr,
            nullptr) != count) {
        return {};
    }
    return result;
}

} // namespace

std::expected<void, FeatureError> InstallFeatureHookLayers(
    FeatureHookLayerActions actions) noexcept
{
    if (actions.install_kernel32 == nullptr ||
        actions.install_ttx == nullptr ||
        actions.rollback_kernel32 == nullptr ||
        actions.deactivate_kernel32 == nullptr) {
        if (actions.deactivate_kernel32 != nullptr) {
            actions.deactivate_kernel32(actions.context);
        }
        return std::unexpected(FeatureError{
            .stage = FeatureFailureStage::hook_installation,
            .win32_error = ERROR_INVALID_PARAMETER,
            .hook = gc::win32_hooks::HookInstallError{
                .stage = gc::win32_hooks::HookInstallStage::none,
                .win32_error = ERROR_INVALID_PARAMETER,
            },
        });
    }

    const auto kernel32 = actions.install_kernel32(actions.context);
    if (!kernel32) {
        actions.deactivate_kernel32(actions.context);
        return std::unexpected(FeatureError{
            .stage = FeatureFailureStage::hook_installation,
            .hook = kernel32.error(),
        });
    }

    const auto ttx = actions.install_ttx(actions.context);
    if (!ttx) {
        actions.rollback_kernel32(actions.context);
        actions.deactivate_kernel32(actions.context);
        return std::unexpected(FeatureError{
            .stage = FeatureFailureStage::ttx_guard_installation,
            .ttx = ttx.error(),
        });
    }
    return {};
}

std::expected<void, FeatureError> InitializeFeature(
    const gc::system_path::RuntimeRoot& system_root) noexcept
{
    if (g_feature_state != nullptr) {
        return {};
    }

    int card_virtual_key{};
    bool storage_enabled{};
    try {
        const auto card_read_key =
            ConfigManager::instance().GetCardReadKey();
        card_virtual_key = static_cast<int>(
            gc::input::PhysicalKeyToVirtualKey(card_read_key));
        storage_enabled = ConfigManager::instance()
                              .GetEnableTestModeStorageRedirect();

        const auto token = gc::input::FormatPhysicalKey(card_read_key);
        const auto label = WideToUtf8(
            gc::input::PhysicalKeyLabel(card_read_key));

        PLOG_INFO << "Test-mode storage redirect: "
                  << (storage_enabled ? "enabled" : "disabled");
        if (card_virtual_key == 0) {
            PLOG_WARNING
                << "RFID: configured card_read physical key token="
                << token << " label=" << label
                << " cannot be mapped to a Win32 virtual key; "
                   "card scan disabled";
        } else {
            PLOG_INFO << "RFID: card_read token=" << token
                      << " label=" << label
                      << " vk=0x" << std::hex << card_virtual_key
                      << std::dec;
        }
    } catch (...) {
        return std::unexpected(FeatureError{
            .stage = FeatureFailureStage::configuration,
            .win32_error = ERROR_INVALID_DATA,
        });
    }

    if (!CreateDirectoryA("OpenParrot", nullptr)) {
        const auto error = GetLastError();
        if (error != ERROR_ALREADY_EXISTS) {
            PLOG_WARNING
                << "RFID: could not create legacy OpenParrot directory; "
                   "continuing, error="
                << error;
        }
    }

    std::unique_ptr<FeatureState> state;
    try {
        state = std::make_unique<FeatureState>(
            card_virtual_key, storage_enabled, system_root);
    } catch (const std::bad_alloc&) {
        return std::unexpected(FeatureError{
            .stage = FeatureFailureStage::allocation,
            .win32_error = ERROR_NOT_ENOUGH_MEMORY,
        });
    } catch (...) {
        return std::unexpected(FeatureError{
            .stage = FeatureFailureStage::allocation,
            .win32_error = ERROR_NOT_ENOUGH_MEMORY,
        });
    }

    state->kernel32.Activate();
    const auto requests = state->kernel32.BuildRequests();
    ProductionHookLayerContext hook_context{
        .state = state.get(),
        .requests = requests.requests(),
    };
    const auto installed = InstallFeatureHookLayers(
        FeatureHookLayerActions{
            .context = &hook_context,
            .install_kernel32 = +[](void* context) noexcept {
                auto& owner =
                    *static_cast<ProductionHookLayerContext*>(context);
                return owner.state->transaction.Install(owner.requests);
            },
            .install_ttx = +[](void* context) noexcept {
                auto& owner =
                    *static_cast<ProductionHookLayerContext*>(context);
                return owner.state->ttx.Install();
            },
            .rollback_kernel32 = +[](void* context) noexcept {
                auto& owner =
                    *static_cast<ProductionHookLayerContext*>(context);
                owner.state->transaction.Rollback();
            },
            .deactivate_kernel32 = +[](void* context) noexcept {
                auto& owner =
                    *static_cast<ProductionHookLayerContext*>(context);
                owner.state->kernel32.Deactivate();
            },
        });
    if (!installed) {
        const auto& error = installed.error();
        if (error.stage == FeatureFailureStage::hook_installation) {
            PLOG_ERROR
                << "Game Kernel32 hooks: installation failed rfid=true "
                   "storage="
                << storage_enabled
                << " system=" << state->system.enabled()
                << " stage="
                << gc::win32_hooks::HookInstallStageName(error.hook.stage)
                << " export="
                << (error.hook.export_name == nullptr
                        ? "<none>"
                        : error.hook.export_name)
                << " target=" << error.hook.target
                << " win32_error=" << error.hook.win32_error
                << " minhook_status="
                << static_cast<int>(error.hook.minhook_status);
        } else {
            PLOG_ERROR
                << "Ttx init guard: installation failed stage="
                << gc::system_path::TtxGuardInstallStageName(
                       error.ttx.stage)
                << " win32_error=" << error.ttx.win32_error
                << " safetyhook_error=" << error.ttx.safetyhook_error;
        }
        return std::unexpected(error);
    }

    g_feature_state = state.release();
    PLOG_INFO
        << "RFID/JVS feature active hooks=" << requests.requests().size();
    return {};
}

} // namespace gc::rfid

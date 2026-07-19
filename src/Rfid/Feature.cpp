#include "Rfid/Feature.h"

#include "Rfid/Runtime.h"
#include "TestModeStorage/Hooks.h"
#include "Win32Hooks/Kernel32Hooks.h"
#include "Config/config.h"
#include "Input/Types/PhysicalKey.h"
#include "Input/Win32/PhysicalKeyWin32.h"
#include "plog/Log.h"

#include <iomanip>
#include <memory>
#include <new>
#include <string>
#include <string_view>

namespace gc::rfid {
namespace {

struct FeatureState {
    FeatureState(int virtual_key, bool storage_enabled) noexcept
        : rfid{virtual_key},
          storage{storage_enabled},
          kernel32{rfid, storage}
    {
    }

    Runtime rfid;
    gc::testmode_storage::Hooks storage;
    gc::win32_hooks::Kernel32Hooks kernel32;
    gc::win32_hooks::MinHookTransaction transaction;
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

std::expected<void, FeatureError> InitializeFeature() noexcept
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
            card_virtual_key, storage_enabled);
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
    const auto requests =
        state->kernel32.BuildRequests(storage_enabled);
    const auto installed = state->transaction.Install(requests.requests());
    if (!installed) {
        const auto& error = installed.error();
        PLOG_ERROR
            << "RFID hooks: installation failed stage="
            << gc::win32_hooks::HookInstallStageName(error.stage)
            << " export="
            << (error.export_name == nullptr ? "<none>" : error.export_name)
            << " target=" << error.target
            << " win32_error=" << error.win32_error
            << " minhook_status="
            << static_cast<int>(error.minhook_status);
        state->kernel32.Deactivate();
        return std::unexpected(FeatureError{
            .stage = FeatureFailureStage::hook_installation,
            .hook = installed.error(),
        });
    }

    g_feature_state = state.release();
    PLOG_INFO
        << "RFID/JVS feature active hooks=" << requests.requests().size();
    return {};
}

} // namespace gc::rfid

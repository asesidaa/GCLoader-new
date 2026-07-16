#include "Rfid/Feature.h"

#include "Rfid/Runtime.h"
#include "TestModeStorage/Hooks.h"
#include "Win32Hooks/Kernel32Hooks.h"
#include "WinKeyMapping.h"
#include "config.h"
#include "plog/Log.h"

#include <iomanip>
#include <memory>
#include <new>

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
        card_virtual_key = SdlKeycodeToVirtualKey(card_read_key);
        storage_enabled = ConfigManager::instance()
                              .GetEnableTestModeStorageRedirect();

        PLOG_INFO << "Test-mode storage redirect: "
                  << (storage_enabled ? "enabled" : "disabled");
        if (card_virtual_key == 0) {
            PLOG_WARNING
                << "RFID: configured card_read key '"
                << KeycodeToString(card_read_key)
                << "' cannot be mapped to a Win32 virtual key; "
                   "card scan disabled";
        } else {
            PLOG_INFO << "RFID: card_read key="
                      << KeycodeToString(card_read_key)
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
        state->kernel32.Deactivate();
        return std::unexpected(FeatureError{
            .stage = FeatureFailureStage::hook_installation,
            .hook = installed.error(),
        });
    }

    g_feature_state = state.release();
    return {};
}

} // namespace gc::rfid

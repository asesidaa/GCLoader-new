#include "Rfid/Feature.h"

#include "Rfid/Runtime.h"
#include "SystemPath/TtxInitGuard.h"
#include "SystemPath/SystemPathRouter.h"
#include "TestModeStorage/Hooks.h"
#include "Win32Hooks/Kernel32Hooks.h"
#include "Input/Types/PhysicalKey.h"
#include "Input/Win32/PhysicalKeyWin32.h"
#include "plog/Log.h"

// ReSharper disable once CppUnusedIncludeDirective
#include <iomanip>
#include <memory>
#include <new>
#include <span>
#include <string>
#include <string_view>

namespace gc::rfid
{
    namespace
    {
        struct FeatureState
        {
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
            gc::system_path::TtxInitGuard ttx;
        };

        FeatureState* g_feature_state{};

        std::string WideToUtf8(std::wstring_view value)
        {
            if (value.empty())
            {
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
            if (count <= 0)
            {
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
                nullptr) != count)
            {
                return {};
            }
            return result;
        }
    } // namespace

    std::expected<void, FeatureError> AddRfidHooks(
        hooking::HookPlan& plan,
        const gc::system_path::RuntimeRoot& system_root,
        FeatureSettings settings) noexcept
    {
        if (g_feature_state != nullptr)
        {
            return std::unexpected(FeatureError{.stage = FeatureFailureStage::hook_plan,
                .win32_error = ERROR_ALREADY_INITIALIZED});
        }

        int card_virtual_key{};
        bool storage_enabled{};
        try
        {
            const auto card_read_key = settings.card_read_key();
            card_virtual_key = static_cast<int>(
                gc::input::PhysicalKeyToVirtualKey(card_read_key));
            storage_enabled = settings.testmode_storage_redirect_enabled();

            const auto token = gc::input::FormatPhysicalKey(card_read_key);
            const auto label = WideToUtf8(
                gc::input::PhysicalKeyLabel(card_read_key));

            PLOG_INFO << "Test-mode storage redirect: "
                << (storage_enabled ? "enabled" : "disabled");
            if (card_virtual_key == 0)
            {
                PLOG_WARNING
                    << "RFID: configured card_read physical key token="
                    << token << " label=" << label
                    << " cannot be mapped to a Win32 virtual key; "
                    "card scan disabled";
            }
            else
            {
                PLOG_INFO << "RFID: card_read token=" << token
                    << " label=" << label
                    << " vk=0x" << std::hex << card_virtual_key
                    << std::dec;
            }
        }
        catch (...)
        {
            return std::unexpected(FeatureError{
                .stage = FeatureFailureStage::configuration,
                .win32_error = ERROR_INVALID_DATA,
            });
        }

        std::unique_ptr<FeatureState> state;
        try
        {
            state = std::make_unique<FeatureState>(
                card_virtual_key, storage_enabled, system_root);
        }
        catch (const std::bad_alloc&)
        {
            return std::unexpected(FeatureError{
                .stage = FeatureFailureStage::allocation,
                .win32_error = ERROR_NOT_ENOUGH_MEMORY,
            });
        }
        catch (...)
        {
            return std::unexpected(FeatureError{
                .stage = FeatureFailureStage::allocation,
                .win32_error = ERROR_NOT_ENOUGH_MEMORY,
            });
        }

        // Runtime and routing state outlive every registered original slot.
        g_feature_state = state.release();
        g_feature_state->kernel32.Activate();
        const auto kernel32 = g_feature_state->kernel32.AddHooks(plan);
        if (!kernel32)
            return std::unexpected(FeatureError{.stage = FeatureFailureStage::hook_plan,
                .hook = kernel32.error()});
        const auto ttx = g_feature_state->ttx.AddHook(plan);
        if (!ttx)
            return std::unexpected(FeatureError{.stage = FeatureFailureStage::hook_plan,
                .hook = ttx.error()});
        return {};
    }
} // namespace gc::rfid

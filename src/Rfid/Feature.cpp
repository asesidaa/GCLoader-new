#include "Rfid/Feature.h"

#include "Rfid/Runtime.h"
#include "Rfid/Win32ComHooks.h"
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
            explicit FeatureState(int virtual_key)
                : rfid{virtual_key}, com{rfid} {}
            Runtime rfid;
            Win32ComHooks com;
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

    std::expected<Runtime*, FeatureError> AddRfidHooks(
        hooking::HookPlan& plan,
        FeatureSettings settings) noexcept
    {
        if (g_feature_state != nullptr)
        {
            return std::unexpected(FeatureError{.stage = FeatureFailureStage::hook_plan,
                .win32_error = ERROR_ALREADY_INITIALIZED});
        }

        int card_virtual_key{};
        try
        {
            const auto card_read_key = settings.card_read_key();
            card_virtual_key = static_cast<int>(
                gc::input::PhysicalKeyToVirtualKey(card_read_key));

            const auto token = gc::input::FormatPhysicalKey(card_read_key);
            const auto label = WideToUtf8(
                gc::input::PhysicalKeyLabel(card_read_key));

            PLOG_INFO << "Test-mode storage redirect: "
                << (settings.testmode_storage_redirect_enabled() ? "enabled" : "disabled");
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
                card_virtual_key);
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

        // Runtime and COM original storage outlive every registered hook.
        g_feature_state = state.release();
        const auto com = g_feature_state->com.AddHooks(plan);
        if (!com)
            return std::unexpected(FeatureError{.stage = FeatureFailureStage::hook_plan,
                .hook = com.error()});
        return &g_feature_state->rfid;
    }
} // namespace gc::rfid

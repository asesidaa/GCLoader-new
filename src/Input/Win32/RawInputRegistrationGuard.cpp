#include "Input/Win32/RawInputRegistrationGuard.h"


#include <array>
// ReSharper disable once CppUnusedIncludeDirective
#include <cstddef>
#include <format>
#include <string>
#include <vector>

#include "plog/Log.h"

namespace gc::input
{
    namespace
    {
        constexpr std::array<USHORT, 4> kProtectedUsages{0x06, 0x05, 0x04, 0x08};

        decltype(&::RegisterRawInputDevices) g_original_register{};

        bool IsProtected(const RAWINPUTDEVICE& device) noexcept
        {
            if (device.usUsagePage != 0x01)
            {
                return false;
            }

            for (const USHORT usage : kProtectedUsages)
            {
                if (device.usUsage == usage)
                {
                    return true;
                }
            }

            return false;
        }

        BOOL CallOriginal(
            PCRAWINPUTDEVICE devices,
            UINT device_count,
            UINT device_size) noexcept
        {
            const auto original = g_original_register != nullptr
                ? g_original_register : &::RegisterRawInputDevices;
            return original(devices, device_count, device_size);
        }

BOOL WINAPI RegisterRawInputDevicesDetour(
            PCRAWINPUTDEVICE devices,
            UINT device_count,
            UINT device_size) noexcept
        {
            if (devices == nullptr || device_count == 0 ||
                device_size != sizeof(RAWINPUTDEVICE))
            {
                return CallOriginal(devices, device_count, device_size);
            }

            try
            {
                std::vector<RAWINPUTDEVICE> filtered;
                filtered.reserve(device_count);
                std::size_t blocked = 0;
                for (UINT index = 0; index < device_count; ++index)
                {
                    if (IsProtected(devices[index]))
                    {
                        ++blocked;
                    }
                    else
                    {
                        filtered.push_back(devices[index]);
                    }
                }

                if (blocked == 0)
                {
                    return CallOriginal(devices, device_count, device_size);
                }

                PLOG_INFO << "Input registration guard blocked external usages="
                    << blocked << " passed=" << filtered.size();
                if (filtered.empty())
                {
                    return TRUE;
                }
                return CallOriginal(
                    filtered.data(),
                    static_cast<UINT>(filtered.size()),
                    device_size);
            }
            catch (...)
            {
                SetLastError(ERROR_NOT_ENOUGH_MEMORY);
                return FALSE;
            }
        }

    } // namespace

    std::expected<void, hooking::HookError> AddRawInputRegistrationHook(hooking::HookPlan& plan) noexcept
    {
        return plan.AddInlineExport({"RawInput", "RegisterRawInputDevices"},
            {L"user32.dll", "RegisterRawInputDevices"},
            &RegisterRawInputDevicesDetour, &g_original_register);
    }

BOOL WINAPI RegisterOwnedRawInputDevices(
        PCRAWINPUTDEVICE devices,
        UINT device_count,
        UINT device_size) noexcept
    {
        return CallOriginal(devices, device_count, device_size);
    }
} // namespace gc::input

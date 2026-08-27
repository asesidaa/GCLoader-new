#include "Input/Win32/RawInputRegistrationGuard.h"

#include <safetyhook.hpp>

#include <array>
// ReSharper disable once CppUnusedIncludeDirective
#include <cstddef>
#include <format>
#include <mutex>
#include <string>
#include <vector>

#include "plog/Log.h"

namespace gc::input
{
    namespace
    {
        constexpr std::array<USHORT, 4> kProtectedUsages{0x06, 0x05, 0x04, 0x08};

        struct GuardState
        {
            std::mutex install_mutex;
            safetyhook::InlineHook hook;
        };

        GuardState& State()
        {
            static GuardState* state = new GuardState();
            return *state;
        }

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
            auto& hook = State().hook;
            if (!hook)
            {
                return ::RegisterRawInputDevices(
                    devices, device_count, device_size);
            }
            return hook.unsafe_stdcall<BOOL>(
                devices, device_count, device_size);
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

        std::string SafetyHookFailure(
            const char* operation,
            const safetyhook::InlineHook::Error& error)
        {
            return std::format(
                "{} failed with SafetyHook error {}",
                operation,
                static_cast<unsigned int>(error.type));
        }
    } // namespace

    std::expected<void, std::string> InstallRawInputRegistrationGuard()
    {
        auto& state = State();
        std::lock_guard lock(state.install_mutex);
        if (state.hook)
        {
            return {};
        }

        const HMODULE user32 = GetModuleHandleW(L"user32.dll");
        if (user32 == nullptr)
        {
            return std::unexpected(std::format(
                "GetModuleHandleW(user32.dll) failed with Win32 error {}",
                GetLastError()));
        }
        const FARPROC target = GetProcAddress(user32, "RegisterRawInputDevices");
        if (target == nullptr)
        {
            return std::unexpected(std::format(
                "GetProcAddress(RegisterRawInputDevices) failed with Win32 error {}",
                GetLastError()));
        }

        auto created = safetyhook::InlineHook::create(
            reinterpret_cast<void*>(target),
            reinterpret_cast<void*>(RegisterRawInputDevicesDetour),
            safetyhook::InlineHook::StartDisabled);
        if (!created)
        {
            return std::unexpected(
                SafetyHookFailure("Raw Input guard hook creation", created.error()));
        }
        state.hook = std::move(*created);
        const auto enabled = state.hook.enable();
        if (!enabled)
        {
            const auto error = SafetyHookFailure(
                "Raw Input guard hook enable", enabled.error());
            state.hook.reset();
            return std::unexpected(error);
        }

        PLOG_INFO << "Input registration guard active usages=06,05,04,08";
        return {};
    }

BOOL WINAPI RegisterOwnedRawInputDevices(
        PCRAWINPUTDEVICE devices,
        UINT device_count,
        UINT device_size) noexcept
    {
        return CallOriginal(devices, device_count, device_size);
    }
} // namespace gc::input

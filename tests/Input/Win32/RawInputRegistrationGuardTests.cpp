#include "Input/Win32/RawInputRegistrationGuard.h"

#include <Windows.h>

#include <array>
#include <iostream>
#include <vector>

namespace {

constexpr std::array<USHORT, 4> kProtectedUsages{0x06, 0x05, 0x04, 0x08};

const RAWINPUTDEVICE* FindUsage(
    const std::vector<RAWINPUTDEVICE>& devices,
    USHORT usage)
{
    for (const auto& device : devices)
    {
        if (device.usUsagePage == 0x01 && device.usUsage == usage)
        {
            return &device;
        }
    }
    return nullptr;
}

std::vector<RAWINPUTDEVICE> Registrations()
{
    UINT count = 0;
    if (GetRegisteredRawInputDevices(
            nullptr, &count, sizeof(RAWINPUTDEVICE)) == UINT_MAX)
    {
        return {};
    }
    std::vector<RAWINPUTDEVICE> devices(count);
    if (count != 0 && GetRegisteredRawInputDevices(
            devices.data(), &count, sizeof(RAWINPUTDEVICE)) == UINT_MAX)
    {
        return {};
    }
    devices.resize(count);
    return devices;
}

void RemoveRegistrations() noexcept
{
    std::array<RAWINPUTDEVICE, kProtectedUsages.size()> protected_devices{};
    for (std::size_t index = 0; index < protected_devices.size(); ++index)
    {
        protected_devices[index] = RAWINPUTDEVICE{
            .usUsagePage = 0x01,
            .usUsage = kProtectedUsages[index],
            .dwFlags = RIDEV_REMOVE,
            .hwndTarget = nullptr,
        };
    }
    gc::input::RegisterOwnedRawInputDevices(
        protected_devices.data(),
        static_cast<UINT>(protected_devices.size()),
        sizeof(RAWINPUTDEVICE));

    RAWINPUTDEVICE mouse{
        .usUsagePage = 0x01,
        .usUsage = 0x02,
        .dwFlags = RIDEV_REMOVE,
        .hwndTarget = nullptr,
    };
    RegisterRawInputDevices(&mouse, 1, sizeof(RAWINPUTDEVICE));
}

int Fail(const char* message, HWND owned, HWND external)
{
    std::cerr << message << '\n';
    RemoveRegistrations();
    DestroyWindow(external);
    DestroyWindow(owned);
    return 1;
}

} // namespace

int main()
{
    const auto installed = gc::input::InstallRawInputRegistrationGuard();
    if (!installed)
    {
        std::cerr << "Guard installation failed: " << installed.error() << '\n';
        return 1;
    }

    const HINSTANCE instance = GetModuleHandleW(nullptr);
    const HWND owned = CreateWindowExW(
        0, L"STATIC", L"", 0, 0, 0, 0, 0,
        HWND_MESSAGE, nullptr, instance, nullptr);
    const HWND external = CreateWindowExW(
        0, L"STATIC", L"", 0, 0, 0, 0, 0,
        HWND_MESSAGE, nullptr, instance, nullptr);
    if (owned == nullptr || external == nullptr)
    {
        if (external != nullptr)
        {
            DestroyWindow(external);
        }
        if (owned != nullptr)
        {
            DestroyWindow(owned);
        }
        std::cerr << "Could not create registration target windows\n";
        return 1;
    }

    std::array<RAWINPUTDEVICE, kProtectedUsages.size()> owned_devices{};
    for (std::size_t index = 0; index < owned_devices.size(); ++index)
    {
        owned_devices[index] = RAWINPUTDEVICE{
            .usUsagePage = 0x01,
            .usUsage = kProtectedUsages[index],
            .dwFlags = RIDEV_INPUTSINK | RIDEV_DEVNOTIFY,
            .hwndTarget = owned,
        };
    }
    if (!gc::input::RegisterOwnedRawInputDevices(
            owned_devices.data(),
            static_cast<UINT>(owned_devices.size()),
            sizeof(RAWINPUTDEVICE)))
    {
        return Fail("Owned registrations failed", owned, external);
    }

    std::array<RAWINPUTDEVICE, kProtectedUsages.size() + 1>
        external_devices{};
    for (std::size_t index = 0; index < kProtectedUsages.size(); ++index)
    {
        external_devices[index] = RAWINPUTDEVICE{
            .usUsagePage = 0x01,
            .usUsage = kProtectedUsages[index],
            .dwFlags = RIDEV_INPUTSINK,
            .hwndTarget = external,
        };
    }
    external_devices.back() = RAWINPUTDEVICE{
        .usUsagePage = 0x01,
        .usUsage = 0x02,
        .dwFlags = RIDEV_INPUTSINK,
        .hwndTarget = external,
    };
    if (!RegisterRawInputDevices(
            external_devices.data(),
            static_cast<UINT>(external_devices.size()),
            sizeof(RAWINPUTDEVICE)))
    {
        return Fail(
            "Filtered external registration did not report success",
            owned,
            external);
    }

    const auto observed = Registrations();
    for (const USHORT usage : kProtectedUsages)
    {
        const auto* registration = FindUsage(observed, usage);
        if (registration == nullptr || registration->hwndTarget != owned)
        {
            return Fail(
                "An external caller replaced a protected registration",
                owned,
                external);
        }
    }

    std::array<RAWINPUTDEVICE, kProtectedUsages.size()> external_removals{};
    for (std::size_t index = 0; index < external_removals.size(); ++index)
    {
        external_removals[index] = RAWINPUTDEVICE{
            .usUsagePage = 0x01,
            .usUsage = kProtectedUsages[index],
            .dwFlags = RIDEV_REMOVE,
            .hwndTarget = nullptr,
        };
    }
    if (!RegisterRawInputDevices(
            external_removals.data(),
            static_cast<UINT>(external_removals.size()),
            sizeof(RAWINPUTDEVICE)))
    {
        return Fail(
            "Filtered external removal did not report success",
            owned,
            external);
    }

    const auto after_external_removal = Registrations();
    for (const USHORT usage : kProtectedUsages)
    {
        const auto* registration = FindUsage(after_external_removal, usage);
        if (registration == nullptr || registration->hwndTarget != owned)
        {
            return Fail(
                "An external caller removed a protected registration",
                owned,
                external);
        }
    }

    const auto* mouse = FindUsage(after_external_removal, 0x02);
    if (mouse == nullptr || mouse->hwndTarget != external)
    {
        return Fail(
            "The guard blocked an unprotected mouse registration",
            owned,
            external);
    }

    RemoveRegistrations();
    DestroyWindow(external);
    DestroyWindow(owned);
    std::cout << "RawInputRegistrationGuardTests passed\n";
    return 0;
}

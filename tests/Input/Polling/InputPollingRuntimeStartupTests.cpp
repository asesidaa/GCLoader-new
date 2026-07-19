#include "Input/Polling/InputPollingRuntime.h"

#include <Windows.h>

#include <array>
#include <cstdint>
#include <iostream>
#include <optional>
#include <vector>

namespace {

constexpr std::array<USHORT, 4> kUsages{0x06, 0x05, 0x04, 0x08};

std::optional<std::vector<RAWINPUTDEVICE>> Registrations()
{
    UINT count = 0;
    if (GetRegisteredRawInputDevices(
            nullptr, &count, sizeof(RAWINPUTDEVICE)) == UINT_MAX)
    {
        return std::nullopt;
    }
    std::vector<RAWINPUTDEVICE> devices(count);
    if (count != 0 && GetRegisteredRawInputDevices(
            devices.data(), &count, sizeof(RAWINPUTDEVICE)) == UINT_MAX)
    {
        return std::nullopt;
    }
    devices.resize(count);
    return devices;
}

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

bool IsMessageOnlyWindow(HWND target)
{
    for (HWND window = FindWindowExW(
             HWND_MESSAGE, nullptr, nullptr, nullptr);
         window != nullptr;
         window = FindWindowExW(HWND_MESSAGE, window, nullptr, nullptr))
    {
        if (window == target)
        {
            return true;
        }
    }
    return false;
}

int CheckRegistrations(HWND expected_target, const char* stage)
{
    const auto devices = Registrations();
    if (!devices)
    {
        std::cerr << stage << ": registration query failed with "
                  << GetLastError() << '\n';
        return 1;
    }
    for (const USHORT usage : kUsages)
    {
        const auto* registration = FindUsage(*devices, usage);
        if (registration == nullptr ||
            registration->hwndTarget != expected_target ||
            (registration->dwFlags & RIDEV_INPUTSINK) == 0 ||
            (registration->dwFlags & RIDEV_NOLEGACY) != 0 ||
            (registration->dwFlags & RIDEV_NOHOTKEYS) != 0)
        {
            std::cerr << stage
                      << ": missing legacy-preserving input-sink registration for usage "
                      << usage << '\n';
            return 1;
        }
    }
    return 0;
}

} // namespace

int main()
{
    if (gc::input::ReadPublishedInput() != 0)
    {
        std::cerr << "Published input was not initially clear\n";
        return 1;
    }

    const auto first_open = gc::input::OpenInputPollingRuntime();
    if (!first_open.success)
    {
        std::cerr << "First open failed: " << first_open.message << '\n';
        return 1;
    }

    const auto first_registrations = Registrations();
    if (!first_registrations)
    {
        std::cerr << "Could not query first registrations\n";
        gc::input::CloseInputPollingRuntime();
        return 1;
    }
    const auto* keyboard = FindUsage(*first_registrations, 0x06);
    const HWND target = keyboard == nullptr ? nullptr : keyboard->hwndTarget;
    if (target == nullptr || IsMessageOnlyWindow(target) ||
        GetAncestor(target, GA_ROOT) != target || GetParent(target) != nullptr ||
        IsWindowVisible(target))
    {
        std::cerr << "Expected a hidden top-level Raw Input target\n";
        gc::input::CloseInputPollingRuntime();
        return 1;
    }
    if (CheckRegistrations(target, "first open") != 0)
    {
        gc::input::CloseInputPollingRuntime();
        return 1;
    }

    const auto second_open = gc::input::OpenInputPollingRuntime();
    if (!second_open.success ||
        CheckRegistrations(target, "second open") != 0)
    {
        std::cerr << "Second open did not retain the same worker\n";
        gc::input::CloseInputPollingRuntime();
        gc::input::CloseInputPollingRuntime();
        return 1;
    }

    gc::input::CloseInputPollingRuntime();
    if (CheckRegistrations(target, "first close") != 0)
    {
        gc::input::CloseInputPollingRuntime();
        return 1;
    }

    if (gc::input::ReadPublishedInput() != 0)
    {
        std::cerr << "Published input was not clear before final close\n";
        gc::input::CloseInputPollingRuntime();
        return 1;
    }

    gc::input::CloseInputPollingRuntime();
    const auto final_registrations = Registrations();
    if (!final_registrations)
    {
        std::cerr << "Could not query final registrations\n";
        return 1;
    }
    for (const USHORT usage : kUsages)
    {
        const auto* registration = FindUsage(*final_registrations, usage);
        if (registration != nullptr && registration->hwndTarget == target)
        {
            std::cerr << "Registration remained after final close for usage "
                      << usage << '\n';
            return 1;
        }
    }
    if (gc::input::ReadPublishedInput() != 0)
    {
        std::cerr << "Published input was not clear after final close\n";
        return 1;
    }

    std::cout << "InputPollingRuntimeStartupTests passed\n";
    return 0;
}

#include "InputPollingRuntime.h"

#include <Windows.h>

#include <cstdint>
#include <iostream>
#include <vector>

namespace {

LRESULT CALLBACK game_window_proc(
    HWND window,
    UINT message,
    WPARAM wparam,
    LPARAM lparam)
{
    return DefWindowProcA(window, message, wparam, lparam);
}

}

int main()
{
    const HINSTANCE instance = GetModuleHandleA(nullptr);
    WNDCLASSA window_class{};
    window_class.lpfnWndProc = game_window_proc;
    window_class.hInstance = instance;
    window_class.lpszClassName = "GameWare";

    if (RegisterClassA(&window_class) == 0 &&
        GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
    {
        std::cerr << "RegisterClassA failed: " << GetLastError() << '\n';
        return 1;
    }

    const HWND game_window = CreateWindowExA(
        0,
        "GameWare",
        "GameWare",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        640,
        480,
        nullptr,
        nullptr,
        instance,
        nullptr);
    if (game_window == nullptr)
    {
        std::cerr << "CreateWindowExA failed: " << GetLastError() << '\n';
        return 1;
    }

    const auto result = gc::input::OpenInputPollingRuntime();
    if (!result.success)
    {
        std::cerr << "Input runtime failed to open: "
                  << result.message << '\n';
        DestroyWindow(game_window);
        return 1;
    }

    UINT registration_count = 0;
    if (GetRegisteredRawInputDevices(
            nullptr,
            &registration_count,
            sizeof(RAWINPUTDEVICE)) == static_cast<UINT>(-1))
    {
        std::cerr << "GetRegisteredRawInputDevices count failed: "
                  << GetLastError() << '\n';
        gc::input::CloseInputPollingRuntime();
        DestroyWindow(game_window);
        return 1;
    }

    std::vector<RAWINPUTDEVICE> registrations(registration_count);
    if (registration_count != 0 &&
        GetRegisteredRawInputDevices(
            registrations.data(),
            &registration_count,
            sizeof(RAWINPUTDEVICE)) == static_cast<UINT>(-1))
    {
        std::cerr << "GetRegisteredRawInputDevices failed: "
                  << GetLastError() << '\n';
        gc::input::CloseInputPollingRuntime();
        DestroyWindow(game_window);
        return 1;
    }

    const RAWINPUTDEVICE* keyboard_registration = nullptr;
    for (const auto& registration : registrations)
    {
        if (registration.usUsagePage == 0x01 &&
            registration.usUsage == 0x06)
        {
            keyboard_registration = &registration;
            break;
        }
    }

    if (keyboard_registration == nullptr ||
        keyboard_registration->hwndTarget == nullptr ||
        keyboard_registration->hwndTarget == game_window ||
        (keyboard_registration->dwFlags & RIDEV_NOLEGACY) !=
            RIDEV_NOLEGACY ||
        (keyboard_registration->dwFlags & RIDEV_INPUTSINK) != 0)
    {
        std::cerr
            << "Expected a foreground-only, no-legacy keyboard registration "
            << "targeting the hidden input window\n";
        gc::input::CloseInputPollingRuntime();
        DestroyWindow(game_window);
        return 1;
    }

    const std::uint32_t published = gc::input::ReadPublishedInput();
    gc::input::CloseInputPollingRuntime();
    DestroyWindow(game_window);

    if (published != 0)
    {
        std::cerr << "Expected an initially clear input snapshot, got 0x"
                  << std::hex << published << '\n';
        return 1;
    }

    std::cout << "InputPollingRuntimeStartupTests passed\n";
    return 0;
}

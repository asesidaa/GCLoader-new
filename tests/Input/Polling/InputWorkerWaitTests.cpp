#include "Input/Polling/InputWorkerWait.h"

#include <Windows.h>

#include <iostream>
#include <string>

namespace {

constexpr UINT kProbeMessage = WM_APP + 1;
int g_probe_count = 0;

LRESULT CALLBACK ProbeWindowProc(
    HWND window,
    UINT message,
    WPARAM wparam,
    LPARAM lparam)
{
    if (message == kProbeMessage)
    {
        ++g_probe_count;
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

int Fail(const char* message)
{
    std::cerr << message << '\n';
    return 1;
}

} // namespace

int main()
{
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    const std::wstring class_name =
        L"GCLoader.InputWorkerWaitTests." +
        std::to_wstring(GetCurrentProcessId());
    const WNDCLASSW window_class{
        .lpfnWndProc = ProbeWindowProc,
        .hInstance = instance,
        .lpszClassName = class_name.c_str(),
    };
    if (RegisterClassW(&window_class) == 0)
    {
        return Fail("Could not register the probe window class");
    }

    const HWND window = CreateWindowExW(
        0,
        class_name.c_str(),
        L"",
        0,
        0,
        0,
        0,
        0,
        HWND_MESSAGE,
        nullptr,
        instance,
        nullptr);
    if (window == nullptr)
    {
        UnregisterClassW(class_name.c_str(), instance);
        return Fail("Could not create the probe window");
    }

    const HANDLE stop_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    const HANDLE ready_timer = CreateEventW(nullptr, TRUE, TRUE, nullptr);
    if (stop_event == nullptr || ready_timer == nullptr)
    {
        if (stop_event != nullptr)
        {
            CloseHandle(stop_event);
        }
        if (ready_timer != nullptr)
        {
            CloseHandle(ready_timer);
        }
        DestroyWindow(window);
        UnregisterClassW(class_name.c_str(), instance);
        return Fail("Could not create probe wait handles");
    }

    if (!PostMessageW(window, kProbeMessage, 0, 0))
    {
        CloseHandle(ready_timer);
        CloseHandle(stop_event);
        DestroyWindow(window);
        UnregisterClassW(class_name.c_str(), instance);
        return Fail("Could not queue the probe message");
    }

    const auto wake = gc::input::WaitForInputWorkerWake(
        stop_event, ready_timer);

    CloseHandle(ready_timer);
    CloseHandle(stop_event);
    DestroyWindow(window);
    UnregisterClassW(class_name.c_str(), instance);

    if (!wake)
    {
        std::cerr << "Input worker wait failed: " << wake.error() << '\n';
        return 1;
    }
    if (*wake != gc::input::InputWorkerWake::Timer)
    {
        return Fail("Expected the ready timer to wake the worker");
    }
    if (g_probe_count != 1)
    {
        return Fail(
            "A ready polling timer starved a pending window message");
    }

    std::cout << "InputWorkerWaitTests passed\n";
    return 0;
}

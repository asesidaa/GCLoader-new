#include "Input/Win32/ImeSuppression.h"

#include <Windows.h>

#include <cstdlib>
#include <iostream>
#include <string_view>

namespace
{
    int g_failures{};
    int g_key_down_messages{};

    void Expect(const bool condition, const std::string_view message)
    {
        if (!condition)
        {
            std::cerr << "FAIL: " << message << '\n';
            ++g_failures;
        }
    }

    // Win32 fixes the WNDPROC callback signature, including the HWND value type.
    // ReSharper disable once CppParameterMayBeConst
    LRESULT CALLBACK TestWindowProc(
        HWND window,
        const UINT message,
        const WPARAM wparam,
        const LPARAM lparam) noexcept
    {
        if (message == WM_KEYDOWN)
        {
            ++g_key_down_messages;
            return 0;
        }
        return DefWindowProcW(window, message, wparam, lparam);
    }

    void RealWin32SuppressionPrecedesTheFirstWindowAndPreservesKeyboardMessages()
    {
        const auto disabled = gc::input::DisableProcessIme();
        Expect(disabled.has_value(), "real process-wide IME suppression succeeds");
        if (!disabled)
        {
            return;
        }

        const HINSTANCE instance = GetModuleHandleW(nullptr);
        constexpr wchar_t class_name[] = L"GCLoader.ImeSuppressionTestWindow";
        const WNDCLASSEXW window_class{
            .cbSize = sizeof(WNDCLASSEXW),
            .lpfnWndProc = TestWindowProc,
            .hInstance = instance,
            .lpszClassName = class_name,
        };
        const ATOM registered = RegisterClassExW(&window_class);
        Expect(registered != 0, "test window class registers");
        if (registered == 0)
        {
            return;
        }

        const HWND window = CreateWindowExW(
            0,
            class_name,
            L"",
            WS_OVERLAPPED,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            1,
            1,
            nullptr,
            nullptr,
            instance,
            nullptr);
        Expect(window != nullptr, "first top-level test window is created");
        if (window != nullptr)
        {
            static_cast<void>(SendMessageW(window, WM_KEYDOWN, 'A', 0));
            Expect(
                g_key_down_messages == 1,
                "IME suppression does not block ordinary keyboard messages");
            static_cast<void>(DestroyWindow(window));
        }
        static_cast<void>(UnregisterClassW(class_name, instance));
    }
} // namespace

int main()
{
    RealWin32SuppressionPrecedesTheFirstWindowAndPreservesKeyboardMessages();
    return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

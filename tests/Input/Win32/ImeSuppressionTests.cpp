#include "Input/Win32/ImeSuppression.h"

#include <Windows.h>

#include <cstdlib>
#include <iostream>
#include <limits>
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

    struct FakeImeApi
    {
        BOOL disable_result{TRUE};
        DWORD failure{ERROR_SUCCESS};
        DWORD observed_thread_id{};
        int disable_calls{};
        int get_last_error_calls{};
    };

    BOOL DisableIme(void* context, const DWORD thread_id) noexcept
    {
        auto& fake = *static_cast<FakeImeApi*>(context);
        fake.observed_thread_id = thread_id;
        ++fake.disable_calls;
        return fake.disable_result;
    }

    DWORD GetImeLastError(void* context) noexcept
    {
        auto& fake = *static_cast<FakeImeApi*>(context);
        ++fake.get_last_error_calls;
        return fake.failure;
    }

    void PolicyDisablesImeForEveryProcessThread()
    {
        FakeImeApi fake{};
        const auto result = gc::input::DisableProcessIme({
            .context = &fake,
            .disable_ime = DisableIme,
            .get_last_error = GetImeLastError,
        });

        Expect(result.has_value(), "IME suppression succeeds when ImmDisableIME succeeds");
        Expect(fake.disable_calls == 1, "ImmDisableIME is called exactly once");
        Expect(
            fake.observed_thread_id == (std::numeric_limits<DWORD>::max)(),
            "ImmDisableIME targets every thread in the current process");
        Expect(
            fake.get_last_error_calls == 0,
            "GetLastError is not consulted after successful suppression");
    }

    void PolicyPreservesTheWin32Failure()
    {
        FakeImeApi fake{
            .disable_result = FALSE,
            .failure = ERROR_INVALID_ACCESS,
        };
        const auto result = gc::input::DisableProcessIme({
            .context = &fake,
            .disable_ime = DisableIme,
            .get_last_error = GetImeLastError,
        });

        Expect(!result.has_value(), "IME suppression reports ImmDisableIME failure");
        if (!result)
        {
            Expect(
                result.error().win32_error == ERROR_INVALID_ACCESS,
                "IME suppression preserves the exact Win32 failure");
        }
        Expect(fake.disable_calls == 1, "failed ImmDisableIME is not retried");
        Expect(
            fake.get_last_error_calls == 1,
            "failed ImmDisableIME captures GetLastError exactly once");
    }

    void PolicyRejectsAnIncompleteApiContract()
    {
        FakeImeApi fake{};
        const auto result = gc::input::DisableProcessIme({
            .context = &fake,
            .disable_ime = nullptr,
            .get_last_error = GetImeLastError,
        });

        Expect(!result.has_value(), "IME suppression rejects missing platform actions");
        if (!result)
        {
            Expect(
                result.error().win32_error == ERROR_INVALID_PARAMETER,
                "invalid IME actions report ERROR_INVALID_PARAMETER");
        }
        Expect(fake.disable_calls == 0, "invalid IME actions are not invoked");
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
    PolicyDisablesImeForEveryProcessThread();
    PolicyPreservesTheWin32Failure();
    PolicyRejectsAnIncompleteApiContract();
    RealWin32SuppressionPrecedesTheFirstWindowAndPreservesKeyboardMessages();
    return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

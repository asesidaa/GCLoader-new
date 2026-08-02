#include "CardReaderTestClient/CardReaderClient.h"

#include "Rfid/CardData.h"
#include "Rfid/CardReaderProtocol.h"

#include <Windows.h>

#include <string>

namespace {

constexpr wchar_t kWindowClassName[] =
    L"GCLoaderCardReaderTestClient";
constexpr wchar_t kWindowTitle[] =
    L"GCLoader Card Reader Test";
constexpr int kSendButtonId = 1001;
constexpr int kClientWidth = 420;
constexpr int kClientHeight = 150;

HWND g_status_label{};

std::wstring CardLabel()
{
    std::wstring label{L"Test card: "};
    label.reserve(
        label.size() + gc::rfid::kDefaultCardNumber.size());
    for (const char digit : gc::rfid::kDefaultCardNumber) {
        label.push_back(static_cast<wchar_t>(digit));
    }
    return label;
}

void SetControlFont(HWND control) noexcept
{
    SendMessageW(
        control,
        WM_SETFONT,
        reinterpret_cast<WPARAM>(
            GetStockObject(DEFAULT_GUI_FONT)),
        TRUE);
}

LRESULT CALLBACK WindowProcedure(
    HWND window,
    UINT message,
    WPARAM w_param,
    LPARAM l_param) noexcept
{
    try {
        switch (message) {
        case WM_CREATE: {
            const auto* creation =
                reinterpret_cast<const CREATESTRUCTW*>(l_param);
            const auto card_label = CardLabel();

            const HWND card = CreateWindowExW(
                0,
                L"STATIC",
                card_label.c_str(),
                WS_CHILD | WS_VISIBLE | SS_LEFT,
                20,
                20,
                360,
                22,
                window,
                nullptr,
                creation->hInstance,
                nullptr);
            const HWND button = CreateWindowExW(
                0,
                L"BUTTON",
                L"Send Test Card",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                20,
                55,
                150,
                32,
                window,
                reinterpret_cast<HMENU>(
                    static_cast<INT_PTR>(kSendButtonId)),
                creation->hInstance,
                nullptr);
            g_status_label = CreateWindowExW(
                0,
                L"STATIC",
                L"Not sent",
                WS_CHILD | WS_VISIBLE | SS_LEFT,
                20,
                105,
                360,
                22,
                window,
                nullptr,
                creation->hInstance,
                nullptr);

            if (card == nullptr || button == nullptr ||
                g_status_label == nullptr) {
                return -1;
            }
            SetControlFont(card);
            SetControlFont(button);
            SetControlFont(g_status_label);
            return 0;
        }

        case WM_COMMAND:
            if (LOWORD(w_param) == kSendButtonId &&
                HIWORD(w_param) == BN_CLICKED) {
                const auto result =
                    gc::rfid::card_reader_test_client::
                        SendCardNumber(
                            gc::rfid::card_reader::kPipeName,
                            gc::rfid::kDefaultCardNumber);
                const auto status =
                    gc::rfid::card_reader_test_client::
                        FormatStatus(result);
                SetWindowTextW(g_status_label, status.c_str());
                return 0;
            }
            break;

        case WM_DESTROY:
            g_status_label = nullptr;
            PostQuitMessage(0);
            return 0;

        default:
            break;
        }
    } catch (...) {
        if (g_status_label != nullptr) {
            SetWindowTextW(
                g_status_label, L"Unexpected client error");
        }
        return message == WM_CREATE ? -1 : 0;
    }

    return DefWindowProcW(window, message, w_param, l_param);
}

} // namespace

int WINAPI wWinMain(
    HINSTANCE instance,
    HINSTANCE,
    PWSTR,
    int show_command)
{
    const WNDCLASSEXW window_class{
        .cbSize = sizeof(WNDCLASSEXW),
        .style = CS_HREDRAW | CS_VREDRAW,
        .lpfnWndProc = WindowProcedure,
        .hInstance = instance,
        .hCursor = LoadCursorW(
            nullptr, MAKEINTRESOURCEW(32512)),
        .hbrBackground = reinterpret_cast<HBRUSH>(
            static_cast<INT_PTR>(COLOR_WINDOW + 1)),
        .lpszClassName = kWindowClassName,
    };
    if (RegisterClassExW(&window_class) == 0) {
        return 1;
    }

    RECT window_rect{0, 0, kClientWidth, kClientHeight};
    constexpr DWORD window_style =
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU |
        WS_MINIMIZEBOX;
    if (!AdjustWindowRectEx(
            &window_rect, window_style, FALSE, 0)) {
        return 1;
    }

    const HWND window = CreateWindowExW(
        0,
        kWindowClassName,
        kWindowTitle,
        window_style,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        window_rect.right - window_rect.left,
        window_rect.bottom - window_rect.top,
        nullptr,
        nullptr,
        instance,
        nullptr);
    if (window == nullptr) {
        return 1;
    }

    ShowWindow(window, show_command);
    UpdateWindow(window);

    MSG message{};
    BOOL result{};
    while ((result = GetMessageW(
                &message, nullptr, 0, 0)) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return result == -1
        ? 1
        : static_cast<int>(message.wParam);
}

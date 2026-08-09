// SPDX-License-Identifier: CC0-1.0

#include "AsioModeHost.h"

#include "Audio/Asio/AsioProbeProtocol.h"

#include <Windows.h>

#include <array>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <span>
#include <string_view>
#include <thread>
#include <vector>

namespace {

constexpr UINT kDestroyMessage = WM_APP + 17;

int Expect(bool condition, std::string_view name) {
    if (condition) {
        return 0;
    }
    std::cerr << "Expectation failed: " << name << '\n';
    return 1;
}

LRESULT CALLBACK PanelWindowProc(
    HWND window,
    UINT message,
    WPARAM wparam,
    LPARAM lparam) {
    if (message == kDestroyMessage) {
        DestroyWindow(window);
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

int TestHiddenOwnerContract() {
    AsioHiddenOwnerWindow owner;
    const bool created = owner.Create();
    if (!created) {
        return Expect(false, "hidden ASIO owner window creates");
    }
    const LONG_PTR ex_style = GetWindowLongPtrW(owner.get(), GWL_EXSTYLE);
    return Expect(
        IsWindow(owner.get()) && !IsWindowVisible(owner.get()) &&
            (ex_style & WS_EX_TOOLWINDOW) != 0 &&
            (ex_style & WS_EX_APPWINDOW) == 0,
        "ASIO owner remains unshown and excluded from the taskbar");
}

int TestModelessWindowPump() {
    constexpr wchar_t class_name[] =
        L"GCLoader.AsioModeHostTests.Panel";
    WNDCLASSW window_class{};
    window_class.lpfnWndProc = &PanelWindowProc;
    window_class.hInstance = GetModuleHandleW(nullptr);
    window_class.lpszClassName = class_name;
    if (RegisterClassW(&window_class) == 0) {
        return 1;
    }

    AsioHiddenOwnerWindow owner;
    if (!owner.Create()) {
        UnregisterClassW(class_name, window_class.hInstance);
        return 1;
    }
    HWND panel = CreateWindowExW(
        WS_EX_TOOLWINDOW,
        class_name,
        L"Synthetic vendor panel",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        320,
        180,
        owner.get(),
        nullptr,
        window_class.hInstance,
        nullptr);
    if (panel == nullptr) {
        UnregisterClassW(class_name, window_class.hInstance);
        return 1;
    }
    ShowWindow(panel, SW_SHOWNOACTIVATE);
    const bool posted = PostMessageW(panel, kDestroyMessage, 0, 0) != FALSE;
    WaitForVisiblePanelWindows(owner.get());
    const bool destroyed = !IsWindow(panel);
    UnregisterClassW(class_name, window_class.hInstance);
    return Expect(
        posted && destroyed,
        "modeless panel pump dispatches closure and returns afterward");
}

int TestBoundedModeMessages() {
    const auto encoded = gc::audio::EncodeAsioControlPanelRequest({
        .driver_name = "任意 ASIO 驱动",
    });
    if (!encoded) {
        return 1;
    }

    SECURITY_ATTRIBUTES inheritable{
        sizeof(SECURITY_ATTRIBUTES),
        nullptr,
        TRUE,
    };
    HANDLE read_handle{};
    HANDLE write_handle{};
    if (!CreatePipe(
            &read_handle,
            &write_handle,
            &inheritable,
            0)) {
        return 1;
    }
    DWORD written{};
    const bool wrote = WriteFile(
        write_handle,
        encoded->data(),
        static_cast<DWORD>(encoded->size()),
        &written,
        nullptr) != FALSE;
    CloseHandle(write_handle);
    const auto read = ReadAsioModeMessage(read_handle);
    CloseHandle(read_handle);

    int failures = Expect(
        wrote && written == encoded->size() && read && *read == *encoded,
        "mode host reads one exact bounded message through a pipe");

    HANDLE output_read{};
    HANDLE output_write{};
    if (!CreatePipe(
            &output_read,
            &output_write,
            &inheritable,
            0)) {
        return failures + 1;
    }
    std::vector<std::byte> received(encoded->size());
    DWORD received_count{};
    bool received_ok{};
    std::thread reader([&] {
        received_ok = ReadFile(
            output_read,
            received.data(),
            static_cast<DWORD>(received.size()),
            &received_count,
            nullptr) != FALSE;
        CloseHandle(output_read);
    });
    const bool output_ok = WriteAsioModeMessage(output_write, *encoded);
    CloseHandle(output_write);
    reader.join();
    failures += Expect(
        output_ok && received_ok && received_count == received.size() &&
            received == *encoded,
        "mode host writes and flushes one exact bounded message");
    return failures;
}

} // namespace

int main() {
    int failures{};
    failures += TestHiddenOwnerContract();
    failures += TestModelessWindowPump();
    failures += TestBoundedModeMessages();
    return failures == 0 ? 0 : 1;
}

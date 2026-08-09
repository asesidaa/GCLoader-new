// SPDX-License-Identifier: CC0-1.0

#include "AsioModeHost.h"

#include "Audio/Asio/AsioProbeProtocol.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <span>
#include <vector>

namespace {

bool ReadExact(
    HANDLE input,
    std::span<std::byte> destination) noexcept {
    std::size_t offset{};
    while (offset < destination.size()) {
        const auto remaining = destination.size() - offset;
        const auto chunk = static_cast<DWORD>(std::min<std::size_t>(
            remaining,
            std::numeric_limits<DWORD>::max()));
        DWORD read{};
        if (!ReadFile(
                input,
                destination.data() + offset,
                chunk,
                &read,
                nullptr) ||
            read == 0) {
            return false;
        }
        offset += read;
    }
    return true;
}

std::uint32_t ReadU32(
    std::span<const std::byte> bytes,
    std::size_t offset) noexcept {
    std::uint32_t value{};
    for (std::size_t index = 0; index < 4; ++index) {
        value |= std::to_integer<std::uint32_t>(bytes[offset + index])
            << (index * 8U);
    }
    return value;
}

bool WriteAll(
    HANDLE output,
    std::span<const std::byte> bytes) noexcept {
    std::size_t offset{};
    while (offset < bytes.size()) {
        const auto remaining = bytes.size() - offset;
        const auto chunk = static_cast<DWORD>(std::min<std::size_t>(
            remaining,
            std::numeric_limits<DWORD>::max()));
        DWORD written{};
        if (!WriteFile(
                output,
                bytes.data() + offset,
                chunk,
                &written,
                nullptr) ||
            written == 0) {
            return false;
        }
        offset += written;
    }
    return true;
}

struct WindowSearch {
    DWORD process_id{};
    HWND hidden_owner{};
    bool found{};
};

BOOL CALLBACK FindVisibleWindow(HWND window, LPARAM parameter) noexcept {
    auto& search = *reinterpret_cast<WindowSearch*>(parameter);
    DWORD process_id{};
    GetWindowThreadProcessId(window, &process_id);
    if (process_id == search.process_id &&
        window != search.hidden_owner &&
        IsWindowVisible(window)) {
        search.found = true;
        return FALSE;
    }
    return TRUE;
}

bool HasVisiblePanelWindow(HWND hidden_owner) noexcept {
    WindowSearch search{
        .process_id = GetCurrentProcessId(),
        .hidden_owner = hidden_owner,
    };
    EnumWindows(&FindVisibleWindow, reinterpret_cast<LPARAM>(&search));
    return search.found;
}

bool DrainMessages() noexcept {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        if (message.message == WM_QUIT) {
            return false;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return true;
}

} // namespace

std::expected<std::vector<std::byte>, AsioModeHostError>
ReadAsioModeMessage(HANDLE input) noexcept {
    try {
        std::array<std::byte, gc::audio::kAsioProbeEnvelopeBytes> envelope{};
        if (!ReadExact(input, envelope)) {
            return std::unexpected(AsioModeHostError::input_io);
        }
        const auto payload_size = ReadU32(envelope, 8);
        if (payload_size > gc::audio::kAsioProbeMaxPayloadBytes) {
            return std::unexpected(AsioModeHostError::protocol);
        }
        std::vector<std::byte> message(
            gc::audio::kAsioProbeEnvelopeBytes + payload_size);
        std::copy(envelope.begin(), envelope.end(), message.begin());
        if (payload_size != 0 &&
            !ReadExact(
                input,
                std::span<std::byte>{message}.subspan(
                    gc::audio::kAsioProbeEnvelopeBytes))) {
            return std::unexpected(AsioModeHostError::input_io);
        }

        std::byte trailing{};
        DWORD trailing_bytes{};
        if (ReadFile(input, &trailing, 1, &trailing_bytes, nullptr)) {
            if (trailing_bytes != 0) {
                return std::unexpected(AsioModeHostError::protocol);
            }
        } else if (GetLastError() != ERROR_BROKEN_PIPE) {
            return std::unexpected(AsioModeHostError::input_io);
        }
        return message;
    } catch (const std::bad_alloc&) {
        return std::unexpected(AsioModeHostError::allocation);
    } catch (...) {
        return std::unexpected(AsioModeHostError::protocol);
    }
}

bool WriteAsioModeMessage(
    HANDLE output,
    std::span<const std::byte> bytes) noexcept {
    return WriteAll(output, bytes) &&
        FlushFileBuffers(output) != FALSE;
}

AsioStaApartment::AsioStaApartment() noexcept
    : result_(CoInitializeEx(
          nullptr,
          COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE)) {}

AsioStaApartment::~AsioStaApartment() {
    if (SUCCEEDED(result_)) {
        CoUninitialize();
    }
}

bool AsioStaApartment::ready() const noexcept {
    return SUCCEEDED(result_);
}

AsioHiddenOwnerWindow::~AsioHiddenOwnerWindow() {
    if (window_ != nullptr) {
        DestroyWindow(window_);
    }
    if (registered_) {
        UnregisterClassW(kClassName, instance_);
    }
}

bool AsioHiddenOwnerWindow::Create() noexcept {
    if (window_ != nullptr) {
        return true;
    }
    instance_ = GetModuleHandleW(nullptr);
    WNDCLASSW window_class{};
    window_class.lpfnWndProc = DefWindowProcW;
    window_class.hInstance = instance_;
    window_class.lpszClassName = kClassName;
    if (RegisterClassW(&window_class) == 0) {
        return false;
    }
    registered_ = true;
    window_ = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        kClassName,
        L"GCLoader ASIO host",
        WS_POPUP,
        0,
        0,
        0,
        0,
        nullptr,
        nullptr,
        instance_,
        nullptr);
    return window_ != nullptr;
}

HWND AsioHiddenOwnerWindow::get() const noexcept {
    return window_;
}

void WaitForVisiblePanelWindows(HWND hidden_owner) noexcept {
    try {
        // Some drivers post their final ShowWindow work just before returning
        // from controlPanel(). Give that queue one turn before enumeration.
        (void)MsgWaitForMultipleObjectsEx(
            0,
            nullptr,
            0,
            QS_ALLINPUT,
            MWMO_INPUTAVAILABLE);
        if (!DrainMessages()) {
            return;
        }

        while (HasVisiblePanelWindow(hidden_owner)) {
            const DWORD wait = MsgWaitForMultipleObjectsEx(
                0,
                nullptr,
                100,
                QS_ALLINPUT,
                MWMO_INPUTAVAILABLE);
            if (wait == WAIT_FAILED) {
                return;
            }
            if (!DrainMessages()) {
                return;
            }
        }
    } catch (...) {
    }
}

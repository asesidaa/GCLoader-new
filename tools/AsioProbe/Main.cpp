// SPDX-License-Identifier: CC0-1.0

#include "Audio/Asio/AsioCapabilityProbe.h"
#include "Audio/Asio/AsioDriver.h"
#include "Audio/Asio/AsioDriverCatalog.h"
#include "Audio/Asio/AsioProbeProtocol.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <span>
#include <utility>
#include <vector>

namespace {

enum class ExitCode : int {
    success = 0,
    input_io = 10,
    request_protocol = 11,
    com_initialization = 12,
    window_creation = 13,
    response_protocol = 14,
    output_io = 15,
    unexpected = 16,
};

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

std::expected<std::vector<std::byte>, ExitCode> ReadRequest(
    HANDLE input) {
    std::array<std::byte, gc::audio::kAsioProbeEnvelopeBytes> envelope{};
    if (!ReadExact(input, envelope)) {
        return std::unexpected(ExitCode::input_io);
    }
    const auto payload_size = ReadU32(envelope, 8);
    if (payload_size > gc::audio::kAsioProbeMaxPayloadBytes) {
        return std::unexpected(ExitCode::request_protocol);
    }
    std::vector<std::byte> message(
        gc::audio::kAsioProbeEnvelopeBytes + payload_size);
    std::copy(envelope.begin(), envelope.end(), message.begin());
    if (payload_size != 0 &&
        !ReadExact(
            input,
            std::span<std::byte>{message}.subspan(
                gc::audio::kAsioProbeEnvelopeBytes))) {
        return std::unexpected(ExitCode::input_io);
    }
    std::byte trailing{};
    DWORD trailing_bytes{};
    if (ReadFile(input, &trailing, 1, &trailing_bytes, nullptr)) {
        if (trailing_bytes != 0) {
            return std::unexpected(ExitCode::request_protocol);
        }
    } else if (GetLastError() != ERROR_BROKEN_PIPE) {
        return std::unexpected(ExitCode::input_io);
    }
    return message;
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
    return FlushFileBuffers(output) != FALSE;
}

class ComApartment {
public:
    explicit ComApartment(HRESULT result) noexcept : result_(result) {}
    ~ComApartment() {
        if (SUCCEEDED(result_)) {
            CoUninitialize();
        }
    }
    [[nodiscard]] bool ready() const noexcept {
        return SUCCEEDED(result_);
    }

private:
    HRESULT result_{};
};

class HiddenWindow {
public:
    HiddenWindow() = default;
    ~HiddenWindow() {
        if (window_ != nullptr) {
            DestroyWindow(window_);
        }
        if (registered_) {
            UnregisterClassW(kClassName, instance_);
        }
    }

    bool Create() noexcept {
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
            0,
            kClassName,
            L"GCLoader ASIO validation",
            WS_OVERLAPPED,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            nullptr,
            nullptr,
            instance_,
            nullptr);
        return window_ != nullptr;
    }

    [[nodiscard]] HWND get() const noexcept {
        return window_;
    }

private:
    inline static constexpr wchar_t kClassName[] =
        L"GCLoader.AsioProbe.HiddenWindow";
    HINSTANCE instance_{};
    HWND window_{};
    bool registered_{};
};

} // namespace

int wmain() {
    try {
        const auto input = ReadRequest(GetStdHandle(STD_INPUT_HANDLE));
        if (!input.has_value()) {
            return static_cast<int>(input.error());
        }
        auto request = gc::audio::DecodeAsioProbeRequest(*input);
        if (!request.has_value()) {
            return static_cast<int>(ExitCode::request_protocol);
        }

        ComApartment com{CoInitializeEx(
            nullptr,
            COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE)};
        if (!com.ready()) {
            return static_cast<int>(ExitCode::com_initialization);
        }
        HiddenWindow window;
        if (!window.Create()) {
            return static_cast<int>(ExitCode::window_creation);
        }

        gc::audio::ProductionAsioRegistrySource registry;
        gc::audio::ProductionAsioDriverFactory factory;
        const gc::audio::AsioStreamRequest stream_request{
            request->driver_name,
            request->buffer_frames,
            request->output_base_channel,
        };
        auto probe = gc::audio::ProbeAsioCapability(
            registry,
            factory,
            stream_request,
            window.get(),
            request->mode);
        gc::audio::AsioProbeResult result = probe.has_value()
            ? gc::audio::AsioProbeResult{std::move(*probe)}
            : gc::audio::AsioProbeResult{
                  std::unexpected(std::move(probe.error()))};
        auto response = gc::audio::EncodeAsioProbeResult(result);
        if (!response.has_value()) {
            return static_cast<int>(ExitCode::response_protocol);
        }
        if (!WriteAll(GetStdHandle(STD_OUTPUT_HANDLE), *response)) {
            return static_cast<int>(ExitCode::output_io);
        }
        return static_cast<int>(ExitCode::success);
    } catch (...) {
        return static_cast<int>(ExitCode::unexpected);
    }
}

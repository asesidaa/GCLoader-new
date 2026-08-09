#pragma once
// SPDX-License-Identifier: CC0-1.0

#include <Windows.h>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <vector>

enum class AsioModeHostError : std::uint8_t {
    input_io,
    protocol,
    allocation,
};

[[nodiscard]] std::expected<std::vector<std::byte>, AsioModeHostError>
ReadAsioModeMessage(HANDLE input) noexcept;

[[nodiscard]] bool WriteAsioModeMessage(
    HANDLE output,
    std::span<const std::byte> bytes) noexcept;

class AsioStaApartment final {
public:
    AsioStaApartment() noexcept;
    ~AsioStaApartment();

    AsioStaApartment(const AsioStaApartment&) = delete;
    AsioStaApartment& operator=(const AsioStaApartment&) = delete;

    [[nodiscard]] bool ready() const noexcept;

private:
    HRESULT result_{};
};

class AsioHiddenOwnerWindow final {
public:
    AsioHiddenOwnerWindow() = default;
    ~AsioHiddenOwnerWindow();

    AsioHiddenOwnerWindow(const AsioHiddenOwnerWindow&) = delete;
    AsioHiddenOwnerWindow& operator=(const AsioHiddenOwnerWindow&) = delete;

    [[nodiscard]] bool Create() noexcept;
    [[nodiscard]] HWND get() const noexcept;

private:
    inline static constexpr wchar_t kClassName[] =
        L"GCLoader.AsioMode.HiddenOwner";
    HINSTANCE instance_{};
    HWND window_{};
    bool registered_{};
};

void WaitForVisiblePanelWindows(HWND hidden_owner) noexcept;

#pragma once
// SPDX-License-Identifier: CC0-1.0

#include "Audio/Asio/AsioDriver.h"
#include "Audio/Asio/AsioDriverCatalog.h"

#include <Windows.h>

#include <expected>
#include <string>

namespace gc::audio {

struct AsioControlPanelRequest {
    std::string driver_name;
};

struct AsioControlPanelActions {
    void* context{};
    void (*wait_for_visible_windows)(void*, HWND) noexcept{};
};

[[nodiscard]] std::expected<void, AsioFailure> OpenAsioControlPanel(
    IAsioRegistrySource& registry,
    IAsioDriverFactory& factory,
    const AsioControlPanelRequest& request,
    HWND owner,
    AsioControlPanelActions actions) noexcept;

} // namespace gc::audio

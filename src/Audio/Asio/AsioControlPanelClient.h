#pragma once
// SPDX-License-Identifier: CC0-1.0

#include "Audio/Asio/AsioControlPanel.h"
#include "Audio/Asio/AsioIsolatedProcess.h"

#include <Windows.h>

#include <cstdint>
#include <expected>
#include <memory>

namespace gc::audio {

enum class AsioControlPanelCompletion : std::uint8_t {
    closed,
    cancelled,
};

class IAsioControlPanelClient {
public:
    virtual ~IAsioControlPanelClient() = default;

    virtual std::expected<AsioControlPanelCompletion, AsioFailure> Run(
        const AsioControlPanelRequest& request,
        HANDLE cancellation_event) noexcept = 0;
};

class AsioControlPanelClient final : public IAsioControlPanelClient {
public:
    AsioControlPanelClient();
    explicit AsioControlPanelClient(
        std::unique_ptr<IAsioIsolatedProcessActions> actions) noexcept;

    std::expected<AsioControlPanelCompletion, AsioFailure> Run(
        const AsioControlPanelRequest& request,
        HANDLE cancellation_event) noexcept override;

private:
    std::unique_ptr<IAsioIsolatedProcessActions> actions_;
};

} // namespace gc::audio

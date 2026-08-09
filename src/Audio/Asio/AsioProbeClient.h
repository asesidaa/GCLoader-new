#pragma once
// SPDX-License-Identifier: CC0-1.0

#include "Audio/Asio/AsioIsolatedProcess.h"
#include "Audio/Asio/AsioProbeProtocol.h"

#include <chrono>
#include <expected>
#include <memory>
#include <string_view>

namespace gc::audio {

inline constexpr std::chrono::milliseconds kDefaultAsioProbeTimeout{5'000};
inline constexpr std::wstring_view kAsioProbeModeArgument{L"--asio-probe"};

class IAsioProbeClient {
public:
    virtual ~IAsioProbeClient() = default;
    virtual std::expected<AsioCapabilityReport, AsioFailure> Run(
        const AsioProbeRequest& request,
        std::chrono::milliseconds timeout) noexcept = 0;
};

using AsioProbeProcessStatus = AsioIsolatedProcessStatus;
using AsioProbeProcessRequest = AsioIsolatedProcessRequest;
using AsioProbeProcessOutcome = AsioIsolatedProcessOutcome;
using IAsioProbeProcessActions = IAsioIsolatedProcessActions;
using ProductionAsioProbeProcessActions =
    ProductionAsioIsolatedProcessActions;

class AsioProbeClient final : public IAsioProbeClient {
public:
    AsioProbeClient();
    explicit AsioProbeClient(
        std::unique_ptr<IAsioProbeProcessActions> actions) noexcept;

    std::expected<AsioCapabilityReport, AsioFailure> Run(
        const AsioProbeRequest& request,
        std::chrono::milliseconds timeout) noexcept override;

private:
    std::unique_ptr<IAsioProbeProcessActions> actions_;
};

} // namespace gc::audio

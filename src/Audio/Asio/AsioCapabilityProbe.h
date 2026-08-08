#pragma once
// SPDX-License-Identifier: CC0-1.0

#include "Audio/Asio/AsioDriver.h"
#include "Audio/Asio/AsioDriverCatalog.h"
#include "Audio/Asio/AsioTypes.h"

#include <Windows.h>

#include <expected>

namespace gc::audio {

[[nodiscard]] std::expected<AsioCapabilityReport, AsioFailure>
ProbeAsioCapability(
    IAsioRegistrySource& registry,
    IAsioDriverFactory& factory,
    const AsioStreamRequest& request,
    HWND system_reference,
    AsioProbeMode mode) noexcept;

} // namespace gc::audio

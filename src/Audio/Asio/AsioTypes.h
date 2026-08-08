#pragma once
// SPDX-License-Identifier: CC0-1.0

#include <Windows.h>

#include "iasiodrv.h"

#include <cstdint>
#include <string>

namespace gc::audio {

struct AsioBufferLimits {
    long minimum{};
    long maximum{};
    long preferred{};
    long granularity{};
};

struct AsioDriverRegistration {
    std::string registry_name;
    CLSID clsid{};
};

enum class AsioFailureStage : std::uint8_t {
    none,
    registry,
    clsid,
    com,
    init,
    identity,
    channels,
    sample_rate,
    buffer_metadata,
    channel_info,
    output_ready_probe,
    callback_prepare,
    create_buffers,
    latency,
    render_core,
    start,
    startup_clock,
    callback,
    conversion,
    runtime_clock,
    output_ready,
    stop,
    dispose,
    restore_sample_rate,
    protocol,
    process_launch,
    process_job,
    probe_timeout,
    probe_crash,
};

enum class AsioResultDomain : std::uint8_t {
    none,
    asio,
    hresult,
    win32,
};

struct AsioFailure {
    AsioFailureStage stage{};
    AsioResultDomain domain{};
    std::int64_t result{};
    std::string driver_message;
    std::string detail;
};

} // namespace gc::audio

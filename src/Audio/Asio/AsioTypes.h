#pragma once
// SPDX-License-Identifier: CC0-1.0

#include <Windows.h>

#include "iasiodrv.h"

#include <cstdint>
#include <string>
#include <vector>

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
    control_panel,
    control_panel_crash,
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

enum class AsioProbeMode : std::uint8_t {
    inspect,
    validate,
};

struct AsioStreamRequest {
    std::string driver_name;
    std::uint32_t buffer_frames{};
    std::uint32_t output_base_channel{};
};

inline constexpr std::uint32_t kMaxAsioReportedChannels = 256;

struct AsioChannelDescriptor {
    std::uint32_t index{};
    std::string name;
    ASIOSampleType sample_type{};
};

struct AsioCapabilityReport {
    AsioDriverRegistration registration;
    std::string reported_driver_name;
    long driver_version{};
    double original_sample_rate{};
    double sample_rate{};
    AsioBufferLimits buffer_limits;
    std::uint32_t input_channels{};
    std::vector<AsioChannelDescriptor> output_channels;
    std::uint32_t selected_base_channel{};
    std::uint32_t effective_buffer_frames{};
    std::uint32_t input_latency_frames{};
    std::uint32_t output_latency_frames{};
    bool output_ready_supported{};
    bool overload_reporting_supported{};
};

} // namespace gc::audio

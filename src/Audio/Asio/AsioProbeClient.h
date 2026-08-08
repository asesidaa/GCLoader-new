#pragma once
// SPDX-License-Identifier: CC0-1.0

#include "Audio/Asio/AsioProbeProtocol.h"

#include <Windows.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <span>
#include <vector>

namespace gc::audio {

inline constexpr std::chrono::milliseconds kDefaultAsioProbeTimeout{5'000};

class IAsioProbeClient {
public:
    virtual ~IAsioProbeClient() = default;
    virtual std::expected<AsioCapabilityReport, AsioFailure> Run(
        const AsioProbeRequest& request,
        std::chrono::milliseconds timeout) noexcept = 0;
};

enum class AsioProbeProcessStatus : std::uint8_t {
    exited,
    timed_out,
    create_failed,
    job_failed,
    io_failed,
    output_too_large,
};

struct AsioProbeProcessRequest {
    std::filesystem::path executable_path;
    std::span<const std::byte> standard_input;
    std::chrono::milliseconds timeout{};
    std::uint32_t maximum_stdout_bytes{};
    DWORD creation_flags{};
    bool inherit_handles{};
    bool restricted_handle_list{};
    bool kill_on_job_close{};
    bool use_shell{};
};

struct AsioProbeProcessOutcome {
    AsioProbeProcessStatus status{AsioProbeProcessStatus::exited};
    DWORD win32_error{};
    DWORD exit_code{};
    std::vector<std::byte> standard_output;
};

class IAsioProbeProcessActions {
public:
    virtual ~IAsioProbeProcessActions() = default;
    virtual std::expected<std::filesystem::path, AsioFailure>
        CurrentExecutablePath() noexcept = 0;
    virtual AsioProbeProcessOutcome Run(
        const AsioProbeProcessRequest& request) noexcept = 0;
};

class ProductionAsioProbeProcessActions final
    : public IAsioProbeProcessActions {
public:
    std::expected<std::filesystem::path, AsioFailure>
        CurrentExecutablePath() noexcept override;
    AsioProbeProcessOutcome Run(
        const AsioProbeProcessRequest& request) noexcept override;
};

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

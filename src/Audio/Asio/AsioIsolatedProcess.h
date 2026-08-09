#pragma once
// SPDX-License-Identifier: CC0-1.0

#include "Audio/Asio/AsioTypes.h"

#include <Windows.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <span>
#include <string_view>
#include <vector>

namespace gc::audio {

enum class AsioInternalMode : std::uint8_t {
    probe,
    control_panel,
};

[[nodiscard]] constexpr std::wstring_view AsioInternalModeArgument(
    AsioInternalMode mode) noexcept {
    switch (mode) {
    case AsioInternalMode::probe:
        return L"--asio-probe";
    case AsioInternalMode::control_panel:
        return L"--asio-control-panel";
    }
    return {};
}

enum class AsioIsolatedProcessStatus : std::uint8_t {
    exited,
    timed_out,
    cancelled,
    create_failed,
    job_failed,
    io_failed,
    output_too_large,
};

struct AsioIsolatedProcessRequest {
    std::filesystem::path executable_path;
    AsioInternalMode mode{};
    std::span<const std::byte> standard_input;
    std::chrono::milliseconds timeout{};
    HANDLE cancellation_event{};
    std::uint32_t maximum_stdout_bytes{};
    DWORD creation_flags{};
    bool inherit_handles{};
    bool restricted_handle_list{};
    bool kill_on_job_close{};
    bool use_shell{};
};

struct AsioIsolatedProcessOutcome {
    AsioIsolatedProcessStatus status{AsioIsolatedProcessStatus::exited};
    DWORD win32_error{};
    DWORD exit_code{};
    std::vector<std::byte> standard_output;
};

class IAsioIsolatedProcessActions {
public:
    virtual ~IAsioIsolatedProcessActions() = default;

    virtual std::expected<std::filesystem::path, AsioFailure>
        CurrentExecutablePath() noexcept = 0;
    virtual AsioIsolatedProcessOutcome Run(
        const AsioIsolatedProcessRequest& request) noexcept = 0;
};

class ProductionAsioIsolatedProcessActions final
    : public IAsioIsolatedProcessActions {
public:
    std::expected<std::filesystem::path, AsioFailure>
        CurrentExecutablePath() noexcept override;
    AsioIsolatedProcessOutcome Run(
        const AsioIsolatedProcessRequest& request) noexcept override;
};

} // namespace gc::audio

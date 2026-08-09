// SPDX-License-Identifier: CC0-1.0

#include "Audio/Asio/AsioControlPanelClient.h"

#include "Audio/Asio/AsioProbeProtocol.h"

#include <Windows.h>

#include <cstdint>
#include <memory>
#include <new>
#include <string>
#include <utility>

namespace gc::audio {
namespace {

AsioFailure Failure(
    AsioFailureStage stage,
    AsioResultDomain domain,
    std::int64_t result,
    std::string detail) {
    return {
        .stage = stage,
        .domain = domain,
        .result = result,
        .detail = std::move(detail),
    };
}

AsioFailure ProtocolFailure(
    AsioProbeProtocolError error,
    std::string detail) {
    return Failure(
        AsioFailureStage::protocol,
        AsioResultDomain::none,
        static_cast<std::int64_t>(error),
        std::move(detail));
}

} // namespace

AsioControlPanelClient::AsioControlPanelClient()
    : actions_(std::make_unique<ProductionAsioIsolatedProcessActions>()) {}

AsioControlPanelClient::AsioControlPanelClient(
    std::unique_ptr<IAsioIsolatedProcessActions> actions) noexcept
    : actions_(std::move(actions)) {}

std::expected<AsioControlPanelCompletion, AsioFailure>
AsioControlPanelClient::Run(
    const AsioControlPanelRequest& request,
    HANDLE cancellation_event) noexcept {
    try {
        const auto encoded = EncodeAsioControlPanelRequest(request);
        if (!encoded) {
            return std::unexpected(ProtocolFailure(
                encoded.error(),
                "ASIO control-panel request is invalid"));
        }
        if (actions_ == nullptr) {
            return std::unexpected(Failure(
                AsioFailureStage::process_launch,
                AsioResultDomain::none,
                0,
                "ASIO control-panel process actions are unavailable"));
        }
        if (cancellation_event == nullptr ||
            cancellation_event == INVALID_HANDLE_VALUE) {
            return std::unexpected(Failure(
                AsioFailureStage::process_launch,
                AsioResultDomain::win32,
                ERROR_INVALID_HANDLE,
                "ASIO control-panel cancellation event is invalid"));
        }

        auto current_executable = actions_->CurrentExecutablePath();
        if (!current_executable) {
            return std::unexpected(std::move(current_executable.error()));
        }
        if (!current_executable->is_absolute() ||
            !current_executable->has_filename()) {
            return std::unexpected(Failure(
                AsioFailureStage::process_launch,
                AsioResultDomain::none,
                ERROR_BAD_PATHNAME,
                "ConfigGUI executable path is not absolute"));
        }

        constexpr DWORD creation_flags =
            CREATE_SUSPENDED | CREATE_NO_WINDOW |
            EXTENDED_STARTUPINFO_PRESENT;
        const AsioIsolatedProcessRequest process_request{
            .executable_path = *current_executable,
            .mode = AsioInternalMode::control_panel,
            .standard_input = *encoded,
            .timeout = std::chrono::milliseconds{0},
            .cancellation_event = cancellation_event,
            .maximum_stdout_bytes = kAsioProbeMaxMessageBytes,
            .creation_flags = creation_flags,
            .inherit_handles = true,
            .restricted_handle_list = true,
            .kill_on_job_close = true,
            .use_shell = false,
        };
        auto outcome = actions_->Run(process_request);
        switch (outcome.status) {
        case AsioIsolatedProcessStatus::cancelled:
            return AsioControlPanelCompletion::cancelled;
        case AsioIsolatedProcessStatus::create_failed:
        case AsioIsolatedProcessStatus::io_failed:
        case AsioIsolatedProcessStatus::timed_out:
            return std::unexpected(Failure(
                AsioFailureStage::process_launch,
                AsioResultDomain::win32,
                outcome.win32_error,
                outcome.status == AsioIsolatedProcessStatus::create_failed
                    ? "ASIO control-panel process launch failed"
                    : "ASIO control-panel pipe or wait failed"));
        case AsioIsolatedProcessStatus::job_failed:
            return std::unexpected(Failure(
                AsioFailureStage::process_job,
                AsioResultDomain::win32,
                outcome.win32_error,
                "ASIO control-panel Job Object setup failed"));
        case AsioIsolatedProcessStatus::output_too_large:
            return std::unexpected(Failure(
                AsioFailureStage::protocol,
                AsioResultDomain::win32,
                outcome.win32_error,
                "ASIO control-panel output exceeded the protocol bound"));
        case AsioIsolatedProcessStatus::exited:
            break;
        }

        if (outcome.exit_code != 0) {
            return std::unexpected(Failure(
                AsioFailureStage::control_panel_crash,
                AsioResultDomain::win32,
                outcome.exit_code,
                "ASIO control-panel host exited abnormally"));
        }
        if (outcome.standard_output.size() > kAsioProbeMaxMessageBytes) {
            return std::unexpected(Failure(
                AsioFailureStage::protocol,
                AsioResultDomain::none,
                0,
                "ASIO control-panel output exceeded the protocol bound"));
        }
        auto decoded = DecodeAsioControlPanelResult(
            outcome.standard_output);
        if (!decoded) {
            return std::unexpected(ProtocolFailure(
                decoded.error(),
                "ASIO control-panel host returned malformed output"));
        }
        if (!decoded->has_value()) {
            return std::unexpected(std::move(decoded->error()));
        }
        return AsioControlPanelCompletion::closed;
    } catch (const std::bad_alloc&) {
        return std::unexpected(Failure(
            AsioFailureStage::process_launch,
            AsioResultDomain::win32,
            ERROR_NOT_ENOUGH_MEMORY,
            "ASIO control-panel client allocation failed"));
    } catch (...) {
        return std::unexpected(Failure(
            AsioFailureStage::process_launch,
            AsioResultDomain::none,
            0,
            "ASIO control-panel client failed unexpectedly"));
    }
}

} // namespace gc::audio

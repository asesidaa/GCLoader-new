// SPDX-License-Identifier: CC0-1.0

#include "Audio/Asio/AsioProbeClient.h"

#include <Windows.h>

#include <cstdint>
#include <limits>
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

AsioProbeClient::AsioProbeClient()
    : actions_(std::make_unique<ProductionAsioIsolatedProcessActions>()) {}

AsioProbeClient::AsioProbeClient(
    std::unique_ptr<IAsioProbeProcessActions> actions) noexcept
    : actions_(std::move(actions)) {}

std::expected<AsioCapabilityReport, AsioFailure>
AsioProbeClient::Run(
    const AsioProbeRequest& request,
    std::chrono::milliseconds timeout) noexcept {
    try {
        const auto encoded = EncodeAsioProbeRequest(request);
        if (!encoded) {
            return std::unexpected(ProtocolFailure(
                encoded.error(),
                "ASIO probe request is invalid"));
        }
        if (actions_ == nullptr) {
            return std::unexpected(Failure(
                AsioFailureStage::process_launch,
                AsioResultDomain::none,
                0,
                "ASIO probe process actions are unavailable"));
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
        const AsioProbeProcessRequest process_request{
            .executable_path = *current_executable,
            .mode = AsioInternalMode::probe,
            .standard_input = *encoded,
            .timeout = timeout,
            .cancellation_event = nullptr,
            .maximum_stdout_bytes = kAsioProbeMaxMessageBytes,
            .creation_flags = creation_flags,
            .inherit_handles = true,
            .restricted_handle_list = true,
            .kill_on_job_close = true,
            .use_shell = false,
        };
        auto outcome = actions_->Run(process_request);
        switch (outcome.status) {
        case AsioProbeProcessStatus::create_failed:
        case AsioProbeProcessStatus::io_failed:
            return std::unexpected(Failure(
                AsioFailureStage::process_launch,
                AsioResultDomain::win32,
                outcome.win32_error,
                outcome.status == AsioProbeProcessStatus::create_failed
                    ? "ASIO probe process launch failed"
                    : "ASIO probe pipe communication failed"));
        case AsioProbeProcessStatus::job_failed:
            return std::unexpected(Failure(
                AsioFailureStage::process_job,
                AsioResultDomain::win32,
                outcome.win32_error,
                "ASIO probe Job Object setup failed"));
        case AsioProbeProcessStatus::timed_out:
            return std::unexpected(Failure(
                AsioFailureStage::probe_timeout,
                AsioResultDomain::win32,
                outcome.win32_error,
                "ASIO probe timed out and was terminated"));
        case AsioProbeProcessStatus::cancelled:
            return std::unexpected(Failure(
                AsioFailureStage::process_launch,
                AsioResultDomain::win32,
                outcome.win32_error,
                "ASIO probe process was cancelled unexpectedly"));
        case AsioProbeProcessStatus::output_too_large:
            return std::unexpected(Failure(
                AsioFailureStage::protocol,
                AsioResultDomain::win32,
                outcome.win32_error,
                "ASIO probe output exceeded the protocol bound"));
        case AsioProbeProcessStatus::exited:
            break;
        }

        if (outcome.standard_output.size() > kAsioProbeMaxMessageBytes) {
            return std::unexpected(Failure(
                AsioFailureStage::protocol,
                AsioResultDomain::none,
                0,
                "ASIO probe output exceeded the protocol bound"));
        }
        auto decoded = DecodeAsioProbeResult(outcome.standard_output);
        if (decoded.has_value()) {
            if (decoded->has_value()) {
                return std::move(**decoded);
            }
            return std::unexpected(std::move(decoded->error()));
        }
        if (outcome.exit_code != 0) {
            return std::unexpected(Failure(
                AsioFailureStage::probe_crash,
                AsioResultDomain::win32,
                outcome.exit_code,
                "ASIO probe exited without a valid structured response"));
        }
        return std::unexpected(ProtocolFailure(
            decoded.error(),
            "ASIO probe returned a malformed or truncated response"));
    } catch (const std::bad_alloc&) {
        return std::unexpected(Failure(
            AsioFailureStage::process_launch,
            AsioResultDomain::win32,
            ERROR_NOT_ENOUGH_MEMORY,
            "ASIO probe client allocation failed"));
    } catch (...) {
        return std::unexpected(Failure(
            AsioFailureStage::process_launch,
            AsioResultDomain::none,
            0,
            "ASIO probe client failed unexpectedly"));
    }
}

} // namespace gc::audio

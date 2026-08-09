// SPDX-License-Identifier: CC0-1.0

#include "Audio/Asio/AsioControlPanelClient.h"
#include "Audio/Asio/AsioProbeProtocol.h"

#include <Windows.h>

#include <chrono>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using gc::audio::AsioControlPanelClient;
using gc::audio::AsioControlPanelCompletion;
using gc::audio::AsioControlPanelRequest;
using gc::audio::AsioControlPanelResult;
using gc::audio::AsioFailure;
using gc::audio::AsioFailureStage;
using gc::audio::AsioInternalMode;
using gc::audio::AsioIsolatedProcessOutcome;
using gc::audio::AsioIsolatedProcessRequest;
using gc::audio::AsioIsolatedProcessStatus;
using gc::audio::AsioResultDomain;
using gc::audio::DecodeAsioControlPanelRequest;
using gc::audio::EncodeAsioControlPanelResult;
using gc::audio::IAsioIsolatedProcessActions;

int Expect(bool condition, std::string_view name) {
    if (condition) {
        return 0;
    }
    std::cerr << "Expectation failed: " << name << '\n';
    return 1;
}

class FakeActions final : public IAsioIsolatedProcessActions {
public:
    std::expected<std::filesystem::path, AsioFailure>
    CurrentExecutablePath() noexcept override {
        ++path_calls;
        return path_result;
    }

    AsioIsolatedProcessOutcome Run(
        const AsioIsolatedProcessRequest& request) noexcept override {
        ++run_calls;
        executable = request.executable_path;
        mode = request.mode;
        timeout = request.timeout;
        cancellation_event = request.cancellation_event;
        maximum_stdout_bytes = request.maximum_stdout_bytes;
        creation_flags = request.creation_flags;
        inherit_handles = request.inherit_handles;
        restricted_handle_list = request.restricted_handle_list;
        kill_on_job_close = request.kill_on_job_close;
        use_shell = request.use_shell;
        const auto decoded = DecodeAsioControlPanelRequest(
            request.standard_input);
        if (decoded) {
            decoded_request = *decoded;
            input_decoded = true;
        }
        return outcome;
    }

    std::expected<std::filesystem::path, AsioFailure> path_result{
        std::filesystem::path{L"C:\\Arcade\\ConfigGUI.exe"}};
    AsioIsolatedProcessOutcome outcome;
    int path_calls{};
    int run_calls{};
    std::filesystem::path executable;
    AsioInternalMode mode{AsioInternalMode::probe};
    std::chrono::milliseconds timeout{};
    HANDLE cancellation_event{};
    std::uint32_t maximum_stdout_bytes{};
    DWORD creation_flags{};
    bool inherit_handles{};
    bool restricted_handle_list{};
    bool kill_on_job_close{};
    bool use_shell{true};
    bool input_decoded{};
    AsioControlPanelRequest decoded_request;
};

std::unique_ptr<FakeActions> SuccessfulActions() {
    auto actions = std::make_unique<FakeActions>();
    const auto encoded = EncodeAsioControlPanelResult(
        AsioControlPanelResult{});
    if (encoded) {
        actions->outcome = {
            .status = AsioIsolatedProcessStatus::exited,
            .exit_code = 0,
            .standard_output = *encoded,
        };
    }
    return actions;
}

int TestSuccessfulLaunchContract() {
    HANDLE cancellation = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (cancellation == nullptr) {
        return 1;
    }
    auto actions = SuccessfulActions();
    auto* observed = actions.get();
    AsioControlPanelClient client{std::move(actions)};
    const AsioControlPanelRequest request{
        .driver_name = "用户输入的 ASIO 名称",
    };
    const auto result = client.Run(request, cancellation);
    CloseHandle(cancellation);

    int failures{};
    failures += Expect(
        result && *result == AsioControlPanelCompletion::closed,
        "valid panel response returns closed completion");
    failures += Expect(
        observed->path_calls == 1 && observed->run_calls == 1 &&
            observed->executable ==
                std::filesystem::path{L"C:\\Arcade\\ConfigGUI.exe"} &&
            observed->mode == AsioInternalMode::control_panel,
        "panel client uses current ConfigGUI and fixed internal mode");
    failures += Expect(
        observed->input_decoded &&
            observed->decoded_request.driver_name == request.driver_name,
        "arbitrary driver name travels only in bounded stdin");
    failures += Expect(
        observed->timeout == std::chrono::milliseconds{0} &&
            observed->cancellation_event == cancellation &&
            observed->maximum_stdout_bytes ==
                gc::audio::kAsioProbeMaxMessageBytes,
        "panel client uses cancellation without a normal timeout");
    failures += Expect(
        observed->creation_flags ==
            (CREATE_SUSPENDED | CREATE_NO_WINDOW |
             EXTENDED_STARTUPINFO_PRESENT) &&
            observed->inherit_handles &&
            observed->restricted_handle_list &&
            observed->kill_on_job_close && !observed->use_shell,
        "panel client preserves the hardened launch contract");
    return failures;
}

AsioFailure RunStatus(
    AsioIsolatedProcessStatus status,
    DWORD win32_error,
    DWORD exit_code = 0,
    std::vector<std::byte> output = {}) {
    HANDLE cancellation = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    auto actions = std::make_unique<FakeActions>();
    actions->outcome = {
        .status = status,
        .win32_error = win32_error,
        .exit_code = exit_code,
        .standard_output = std::move(output),
    };
    AsioControlPanelClient client{std::move(actions)};
    const auto result = client.Run({.driver_name = "FlexASIO"}, cancellation);
    CloseHandle(cancellation);
    return result ? AsioFailure{} : result.error();
}

int TestStructuredFailureAndProcessTaxonomy() {
    const AsioFailure driver_failure{
        .stage = AsioFailureStage::control_panel,
        .domain = AsioResultDomain::asio,
        .result = ASE_NotPresent,
        .driver_message = "not present",
        .detail = "panel unavailable",
    };
    const auto encoded = EncodeAsioControlPanelResult(
        AsioControlPanelResult{std::unexpected(driver_failure)});
    if (!encoded) {
        return 1;
    }

    int failures{};
    const auto structured = RunStatus(
        AsioIsolatedProcessStatus::exited,
        ERROR_SUCCESS,
        0,
        *encoded);
    failures += Expect(
        structured.stage == driver_failure.stage &&
            structured.domain == driver_failure.domain &&
            structured.result == driver_failure.result &&
            structured.driver_message == driver_failure.driver_message &&
            structured.detail == driver_failure.detail,
        "zero-exit structured panel failure is authoritative");

    const auto launch = RunStatus(
        AsioIsolatedProcessStatus::create_failed,
        ERROR_FILE_NOT_FOUND);
    failures += Expect(
        launch.stage == AsioFailureStage::process_launch &&
            launch.result == ERROR_FILE_NOT_FOUND,
        "panel launch failure preserves Win32 result");
    const auto job = RunStatus(
        AsioIsolatedProcessStatus::job_failed,
        ERROR_ACCESS_DENIED);
    failures += Expect(
        job.stage == AsioFailureStage::process_job &&
            job.result == ERROR_ACCESS_DENIED,
        "panel Job Object failure stays distinct");
    const auto io = RunStatus(
        AsioIsolatedProcessStatus::io_failed,
        ERROR_BROKEN_PIPE);
    failures += Expect(
        io.stage == AsioFailureStage::process_launch &&
            io.result == ERROR_BROKEN_PIPE,
        "panel pipe failure preserves Win32 result");
    const auto overflow = RunStatus(
        AsioIsolatedProcessStatus::output_too_large,
        ERROR_BUFFER_OVERFLOW);
    failures += Expect(
        overflow.stage == AsioFailureStage::protocol,
        "oversized panel response is a protocol failure");
    const auto crash = RunStatus(
        AsioIsolatedProcessStatus::exited,
        ERROR_SUCCESS,
        0xC0000005U,
        *encoded);
    failures += Expect(
        crash.stage == AsioFailureStage::control_panel_crash &&
            static_cast<std::uint32_t>(crash.result) == 0xC0000005U,
        "abnormal panel host exit is a crash even with output");
    return failures;
}

int TestCancellationAndMalformedOutput() {
    HANDLE cancellation = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (cancellation == nullptr) {
        return 1;
    }
    auto actions = std::make_unique<FakeActions>();
    actions->outcome = {
        .status = AsioIsolatedProcessStatus::cancelled,
        .win32_error = ERROR_OPERATION_ABORTED,
    };
    AsioControlPanelClient client{std::move(actions)};
    const auto cancelled = client.Run(
        {.driver_name = "FlexASIO"},
        cancellation);
    CloseHandle(cancellation);

    int failures = Expect(
        cancelled && *cancelled == AsioControlPanelCompletion::cancelled,
        "operator cancellation is a non-error completion");
    for (const std::vector<std::byte> output : {
             std::vector<std::byte>{},
             std::vector<std::byte>{std::byte{'A'}},
             std::vector<std::byte>(12, std::byte{0xFF})}) {
        const auto malformed = RunStatus(
            AsioIsolatedProcessStatus::exited,
            ERROR_SUCCESS,
            0,
            output);
        failures += Expect(
            malformed.stage == AsioFailureStage::protocol,
            "malformed panel response is a protocol failure");
    }
    return failures;
}

int TestPathAndInputFailuresStopBeforeLaunch() {
    HANDLE cancellation = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (cancellation == nullptr) {
        return 1;
    }
    auto actions = std::make_unique<FakeActions>();
    auto* observed = actions.get();
    actions->path_result = std::unexpected(AsioFailure{
        .stage = AsioFailureStage::process_launch,
        .domain = AsioResultDomain::win32,
        .result = ERROR_INSUFFICIENT_BUFFER,
        .detail = "module path failed",
    });
    AsioControlPanelClient client{std::move(actions)};
    const auto no_path = client.Run(
        {.driver_name = "FlexASIO"},
        cancellation);
    int failures = Expect(
        !no_path && observed->run_calls == 0,
        "panel path failure prevents process creation");

    auto invalid_actions = std::make_unique<FakeActions>();
    auto* invalid_observed = invalid_actions.get();
    AsioControlPanelClient invalid_client{std::move(invalid_actions)};
    const auto invalid = invalid_client.Run(
        {.driver_name = {}},
        cancellation);
    CloseHandle(cancellation);
    failures += Expect(
        !invalid && invalid.error().stage == AsioFailureStage::protocol &&
            invalid_observed->path_calls == 0 &&
            invalid_observed->run_calls == 0,
        "invalid panel request fails before process actions");
    return failures;
}

} // namespace

int main() {
    int failures{};
    failures += TestSuccessfulLaunchContract();
    failures += TestStructuredFailureAndProcessTaxonomy();
    failures += TestCancellationAndMalformedOutput();
    failures += TestPathAndInputFailuresStopBeforeLaunch();
    return failures == 0 ? 0 : 1;
}

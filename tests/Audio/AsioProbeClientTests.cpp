// SPDX-License-Identifier: CC0-1.0

#include "Audio/Asio/AsioProbeClient.h"

#include <Windows.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <iostream>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using gc::audio::AsioCapabilityReport;
using gc::audio::AsioFailure;
using gc::audio::AsioFailureStage;
using gc::audio::AsioProbeClient;
using gc::audio::AsioProbeMode;
using gc::audio::AsioProbeProcessOutcome;
using gc::audio::AsioProbeProcessRequest;
using gc::audio::AsioProbeProcessStatus;
using gc::audio::AsioProbeRequest;
using gc::audio::AsioProbeResult;
using gc::audio::AsioResultDomain;
using gc::audio::DecodeAsioProbeRequest;
using gc::audio::EncodeAsioProbeResult;
using gc::audio::IAsioProbeProcessActions;
using gc::audio::ProductionAsioProbeProcessActions;

int Expect(bool condition, std::string_view name) {
    if (condition) {
        return 0;
    }
    std::cerr << "Expectation failed: " << name << '\n';
    return 1;
}

AsioCapabilityReport SampleReport() {
    AsioCapabilityReport report;
    report.registration.registry_name = "XONAR SOUND CARD";
    report.registration.clsid = {
        0x12345678,
        0x1234,
        0x5678,
        {1, 2, 3, 4, 5, 6, 7, 8},
    };
    report.reported_driver_name = "Xonar ASIO";
    report.driver_version = 7;
    report.original_sample_rate = 48'000.0;
    report.sample_rate = 48'000.0;
    report.buffer_limits = {192, 2400, 192, 1};
    report.output_channels = {
        {0, "Left", ASIOSTInt24LSB},
        {1, "Right", ASIOSTInt24LSB},
    };
    report.effective_buffer_frames = 192;
    report.output_latency_frames = 384;
    report.output_ready_supported = true;
    return report;
}

class FakeProcessActions final : public IAsioProbeProcessActions {
public:
    std::expected<std::filesystem::path, AsioFailure>
    CurrentExecutablePath() noexcept override {
        ++path_calls;
        return path_result;
    }

    AsioProbeProcessOutcome Run(
        const AsioProbeProcessRequest& request) noexcept override {
        ++run_calls;
        executable = request.executable_path;
        fixed_argument = std::wstring{
            gc::audio::AsioInternalModeArgument(request.mode)};
        timeout = request.timeout;
        cancellation_event = request.cancellation_event;
        maximum_stdout_bytes = request.maximum_stdout_bytes;
        creation_flags = request.creation_flags;
        inherit_handles = request.inherit_handles;
        restricted_handle_list = request.restricted_handle_list;
        kill_on_job_close = request.kill_on_job_close;
        use_shell = request.use_shell;
        const auto decoded = DecodeAsioProbeRequest(request.standard_input);
        if (decoded.has_value()) {
            decoded_request = *decoded;
            input_decoded = true;
        }
        return outcome;
    }

    std::expected<std::filesystem::path, AsioFailure> path_result{
        std::filesystem::path{L"C:\\Arcade\\ConfigGUI.exe"}};
    AsioProbeProcessOutcome outcome{};
    int path_calls{};
    int run_calls{};
    std::filesystem::path executable;
    std::wstring fixed_argument;
    std::chrono::milliseconds timeout{};
    HANDLE cancellation_event{};
    std::uint32_t maximum_stdout_bytes{};
    DWORD creation_flags{};
    bool inherit_handles{};
    bool restricted_handle_list{};
    bool kill_on_job_close{};
    bool use_shell{true};
    bool input_decoded{};
    AsioProbeRequest decoded_request;
};

std::unique_ptr<FakeProcessActions> MakeSuccessfulActions() {
    auto actions = std::make_unique<FakeProcessActions>();
    const auto encoded = EncodeAsioProbeResult(AsioProbeResult{SampleReport()});
    if (encoded.has_value()) {
        actions->outcome.status = AsioProbeProcessStatus::exited;
        actions->outcome.standard_output = *encoded;
        actions->outcome.exit_code = 0;
    }
    return actions;
}

int TestSuccessfulLaunchContract() {
    auto actions = MakeSuccessfulActions();
    auto* observed = actions.get();
    AsioProbeClient client{std::move(actions)};
    const AsioProbeRequest request{
        AsioProbeMode::validate,
        "用户输入的 ASIO 名称",
        192,
        4,
    };
    const auto result = client.Run(request, std::chrono::milliseconds{5'000});

    int failures{};
    failures += Expect(
        result.has_value() &&
            result->registration.registry_name == "XONAR SOUND CARD" &&
            result->effective_buffer_frames == 192,
        "valid helper response returns capability report");
    failures += Expect(
        observed->path_calls == 1 && observed->run_calls == 1 &&
            observed->executable ==
                std::filesystem::path{L"C:\\Arcade\\ConfigGUI.exe"} &&
            observed->executable.is_absolute() &&
            observed->fixed_argument == L"--asio-probe",
        "ConfigGUI self-probe uses only the fixed internal argument");
    failures += Expect(
        observed->input_decoded &&
            observed->decoded_request.mode == request.mode &&
            observed->decoded_request.driver_name == request.driver_name &&
            observed->decoded_request.buffer_frames == request.buffer_frames &&
            observed->decoded_request.output_base_channel ==
                request.output_base_channel,
        "driver-controlled data travels only in bounded stdin payload");
    failures += Expect(
        observed->timeout == std::chrono::milliseconds{5'000} &&
            observed->cancellation_event == nullptr &&
            observed->maximum_stdout_bytes ==
                gc::audio::kAsioProbeMaxMessageBytes,
        "caller timeout and bounded response size reach process layer");
    failures += Expect(
        observed->creation_flags ==
            (CREATE_SUSPENDED | CREATE_NO_WINDOW |
             EXTENDED_STARTUPINFO_PRESENT) &&
            observed->inherit_handles &&
            observed->restricted_handle_list &&
            observed->kill_on_job_close &&
            !observed->use_shell,
        "launch contract requires suspended no-shell child, handle list, and kill job");
    return failures;
}

int TestStructuredNegativeIsAuthoritative() {
    const AsioFailure driver_failure{
        .stage = AsioFailureStage::buffer_metadata,
        .domain = AsioResultDomain::asio,
        .result = ASE_InvalidMode,
        .driver_message = "driver says no",
        .detail = "192 frames unsupported",
    };
    auto actions = std::make_unique<FakeProcessActions>();
    auto* observed = actions.get();
    const auto encoded = EncodeAsioProbeResult(
        AsioProbeResult{std::unexpected(driver_failure)});
    if (!encoded.has_value()) {
        return 1;
    }
    actions->outcome = {
        AsioProbeProcessStatus::exited,
        ERROR_SUCCESS,
        77,
        *encoded,
    };
    AsioProbeClient client{std::move(actions)};
    const auto result = client.Run(
        {AsioProbeMode::validate, "Anything", 192, 0},
        std::chrono::milliseconds{5'000});

    int failures{};
    failures += Expect(
        !result.has_value() &&
            result.error().stage == driver_failure.stage &&
            result.error().domain == driver_failure.domain &&
            result.error().result == driver_failure.result &&
            result.error().detail == driver_failure.detail,
        "valid structured driver failure is authoritative even with nonzero exit");
    failures += Expect(
        observed->run_calls == 1,
        "negative result still executes exactly one helper");
    return failures;
}

AsioFailure RunWithStatus(
    AsioProbeProcessStatus status,
    DWORD win32_error,
    DWORD exit_code = 0) {
    auto actions = std::make_unique<FakeProcessActions>();
    actions->outcome = {status, win32_error, exit_code, {}};
    AsioProbeClient client{std::move(actions)};
    const auto result = client.Run(
        {AsioProbeMode::inspect, "FlexASIO", 0, 0},
        std::chrono::milliseconds{5'000});
    return result.has_value() ? AsioFailure{} : result.error();
}

int TestProcessFailureTaxonomy() {
    int failures{};
    const auto create = RunWithStatus(
        AsioProbeProcessStatus::create_failed,
        ERROR_FILE_NOT_FOUND);
    failures += Expect(
        create.stage == AsioFailureStage::process_launch &&
            create.domain == AsioResultDomain::win32 &&
            create.result == ERROR_FILE_NOT_FOUND,
        "CreateProcess failure remains distinct");

    const auto job = RunWithStatus(
        AsioProbeProcessStatus::job_failed,
        ERROR_ACCESS_DENIED);
    failures += Expect(
        job.stage == AsioFailureStage::process_job &&
            job.result == ERROR_ACCESS_DENIED,
        "job assignment failure remains distinct");

    const auto timeout = RunWithStatus(
        AsioProbeProcessStatus::timed_out,
        WAIT_TIMEOUT);
    failures += Expect(
        timeout.stage == AsioFailureStage::probe_timeout,
        "hung helper returns typed timeout");

    const auto io = RunWithStatus(
        AsioProbeProcessStatus::io_failed,
        ERROR_BROKEN_PIPE);
    failures += Expect(
        io.stage == AsioFailureStage::process_launch &&
            io.result == ERROR_BROKEN_PIPE,
        "pipe failure remains distinct from driver rejection");

    const auto oversized = RunWithStatus(
        AsioProbeProcessStatus::output_too_large,
        ERROR_BUFFER_OVERFLOW);
    failures += Expect(
        oversized.stage == AsioFailureStage::protocol,
        "oversized helper output is a protocol failure");

    const auto crash = RunWithStatus(
        AsioProbeProcessStatus::exited,
        ERROR_SUCCESS,
        0xC0000005U);
    failures += Expect(
        crash.stage == AsioFailureStage::probe_crash &&
            static_cast<std::uint32_t>(crash.result) == 0xC0000005U,
        "nonzero exit without structured output is a helper crash");
    return failures;
}

int TestMalformedAndTruncatedResponses() {
    int failures{};
    for (const std::vector<std::byte> output : {
             std::vector<std::byte>{},
             std::vector<std::byte>{std::byte{'A'}},
             std::vector<std::byte>(12, std::byte{0xFF})}) {
        auto actions = std::make_unique<FakeProcessActions>();
        actions->outcome = {
            AsioProbeProcessStatus::exited,
            ERROR_SUCCESS,
            0,
            output,
        };
        AsioProbeClient client{std::move(actions)};
        const auto result = client.Run(
            {AsioProbeMode::inspect, "FlexASIO", 0, 0},
            std::chrono::milliseconds{5'000});
        failures += Expect(
            !result.has_value() &&
                result.error().stage == AsioFailureStage::protocol,
            "malformed or truncated output is a protocol failure");
    }
    return failures;
}

int TestPathAndInputFailuresStopBeforeLaunch() {
    auto actions = std::make_unique<FakeProcessActions>();
    auto* observed = actions.get();
    actions->path_result = std::unexpected(AsioFailure{
        .stage = AsioFailureStage::process_launch,
        .domain = AsioResultDomain::win32,
        .result = ERROR_INSUFFICIENT_BUFFER,
        .detail = "module path failed",
    });
    AsioProbeClient client{std::move(actions)};
    const auto no_path = client.Run(
        {AsioProbeMode::inspect, "FlexASIO", 0, 0},
        std::chrono::milliseconds{5'000});
    int failures = Expect(
        !no_path.has_value() &&
            no_path.error().stage == AsioFailureStage::process_launch &&
            observed->run_calls == 0,
        "module path failure prevents process creation");

    auto relative_actions = std::make_unique<FakeProcessActions>();
    auto* relative_observed = relative_actions.get();
    relative_actions->path_result = L"ConfigGUI.exe";
    AsioProbeClient relative_client{std::move(relative_actions)};
    const auto relative = relative_client.Run(
        {AsioProbeMode::inspect, "FlexASIO", 0, 0},
        std::chrono::milliseconds{5'000});
    failures += Expect(
        !relative.has_value() &&
            relative.error().stage == AsioFailureStage::process_launch &&
            relative_observed->run_calls == 0,
        "relative executable path is rejected before launch");

    auto input_actions = std::make_unique<FakeProcessActions>();
    auto* input_observed = input_actions.get();
    AsioProbeClient input_client{std::move(input_actions)};
    const auto invalid_input = input_client.Run(
        {
            AsioProbeMode::inspect,
            std::string(gc::audio::kAsioProbeMaxDriverNameBytes + 1, 'A'),
            0,
            0,
        },
        std::chrono::milliseconds{5'000});
    failures += Expect(
        !invalid_input.has_value() &&
            invalid_input.error().stage == AsioFailureStage::protocol &&
            input_observed->path_calls == 0 &&
            input_observed->run_calls == 0,
        "invalid request fails before any process action");
    return failures;
}

int TestProductionHelperBoundary() {
    const AsioProbeRequest request{
        AsioProbeMode::inspect,
        "GCLoader deliberately absent ASIO driver 7B71D10A",
        0,
        0,
    };
    const auto encoded = gc::audio::EncodeAsioProbeRequest(request);
    if (!encoded.has_value()) {
        return 1;
    }

    ProductionAsioProbeProcessActions actions;
    const AsioProbeProcessRequest process_request{
        .executable_path =
            std::filesystem::path{GC_ASIO_PROBE_TEST_PATH},
        .mode = gc::audio::AsioInternalMode::probe,
        .standard_input = *encoded,
        .timeout = std::chrono::milliseconds{5'000},
        .cancellation_event = nullptr,
        .maximum_stdout_bytes = gc::audio::kAsioProbeMaxMessageBytes,
        .creation_flags = CREATE_SUSPENDED | CREATE_NO_WINDOW |
            EXTENDED_STARTUPINFO_PRESENT,
        .inherit_handles = true,
        .restricted_handle_list = true,
        .kill_on_job_close = true,
        .use_shell = false,
    };
    const auto outcome = actions.Run(process_request);
    int failures = Expect(
        outcome.status == AsioProbeProcessStatus::exited &&
            outcome.exit_code == 0,
        "production helper completes through restricted Job Object boundary");
    if (outcome.status != AsioProbeProcessStatus::exited) {
        std::cerr << "Production helper status="
                  << static_cast<int>(outcome.status)
                  << " win32_error=" << outcome.win32_error << '\n';
        return failures;
    }
    const auto decoded = gc::audio::DecodeAsioProbeResult(
        outcome.standard_output);
    failures += Expect(
        decoded.has_value() && !decoded->has_value() &&
            decoded->error().stage == AsioFailureStage::registry,
        "real helper returns a structured missing-registration failure");
    return failures;
}

} // namespace

int main() {
    int failures{};
    failures += TestSuccessfulLaunchContract();
    failures += TestStructuredNegativeIsAuthoritative();
    failures += TestProcessFailureTaxonomy();
    failures += TestMalformedAndTruncatedResponses();
    failures += TestPathAndInputFailuresStopBeforeLaunch();
    failures += TestProductionHelperBoundary();
    return failures == 0 ? 0 : 1;
}

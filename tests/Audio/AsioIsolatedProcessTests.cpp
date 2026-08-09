// SPDX-License-Identifier: CC0-1.0

#include "Audio/Asio/AsioIsolatedProcess.h"

#include <Windows.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <span>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {

using gc::audio::AsioInternalMode;
using gc::audio::AsioIsolatedProcessOutcome;
using gc::audio::AsioIsolatedProcessRequest;
using gc::audio::AsioIsolatedProcessStatus;
using gc::audio::ProductionAsioIsolatedProcessActions;

constexpr DWORD kRequiredFlags =
    CREATE_SUSPENDED | CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT;

int Expect(bool condition, std::string_view name) {
    if (condition) {
        return 0;
    }
    std::cerr << "Expectation failed: " << name << '\n';
    return 1;
}

AsioIsolatedProcessRequest Request(
    AsioInternalMode mode,
    HANDLE cancellation_event = nullptr) {
    static constexpr std::array input{
        std::byte{0x47}, std::byte{0x43}, std::byte{0x41}, std::byte{0x53}};
    return {
        .executable_path =
            std::filesystem::path{GC_ASIO_ISOLATED_TEST_CHILD_PATH},
        .mode = mode,
        .standard_input = input,
        .timeout = mode == AsioInternalMode::probe
            ? std::chrono::milliseconds{250}
            : std::chrono::milliseconds{0},
        .cancellation_event = cancellation_event,
        .maximum_stdout_bytes = 64,
        .creation_flags = kRequiredFlags,
        .inherit_handles = true,
        .restricted_handle_list = true,
        .kill_on_job_close = true,
        .use_shell = false,
    };
}

DWORD OutputPid(const AsioIsolatedProcessOutcome& outcome) {
    DWORD pid{};
    if (outcome.standard_output.size() >= sizeof(pid)) {
        std::memcpy(&pid, outcome.standard_output.data(), sizeof(pid));
    }
    return pid;
}

bool ProcessIsNotRunning(DWORD pid) {
    if (pid == 0) {
        return false;
    }
    HANDLE process = OpenProcess(
        SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION,
        FALSE,
        pid);
    if (process == nullptr) {
        return true;
    }
    DWORD exit_code{STILL_ACTIVE};
    const bool stopped =
        GetExitCodeProcess(process, &exit_code) &&
        exit_code != STILL_ACTIVE &&
        WaitForSingleObject(process, 0) == WAIT_OBJECT_0;
    CloseHandle(process);
    return stopped;
}

int TestModeMapping() {
    int failures{};
    failures += Expect(
        gc::audio::AsioInternalModeArgument(AsioInternalMode::probe) ==
            L"--asio-probe",
        "probe mode has a closed internal argument");
    failures += Expect(
        gc::audio::AsioInternalModeArgument(
            AsioInternalMode::control_panel) ==
            L"--asio-control-panel",
        "panel mode has a closed internal argument");
    return failures;
}

int TestRealTimeout() {
    ProductionAsioIsolatedProcessActions actions;
    const auto started = std::chrono::steady_clock::now();
    const auto outcome = actions.Run(Request(AsioInternalMode::probe));
    const auto elapsed = std::chrono::steady_clock::now() - started;
    const DWORD pid = OutputPid(outcome);
    const bool valid =
        outcome.status == AsioIsolatedProcessStatus::timed_out &&
            outcome.win32_error == WAIT_TIMEOUT && pid != 0 &&
            ProcessIsNotRunning(pid) && elapsed < std::chrono::seconds{2};
    if (!valid) {
        std::cerr << "timeout status=" << static_cast<int>(outcome.status)
                  << " error=" << outcome.win32_error
                  << " bytes=" << outcome.standard_output.size()
                  << " pid=" << pid << '\n';
    }
    return Expect(
        valid,
        "probe timeout kills and joins the real child within a bound");
}

int TestRealCancellation() {
    HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (event == nullptr) {
        return 1;
    }
    std::thread canceller([event] {
        std::this_thread::sleep_for(std::chrono::milliseconds{250});
        SetEvent(event);
    });

    ProductionAsioIsolatedProcessActions actions;
    const auto started = std::chrono::steady_clock::now();
    const auto outcome = actions.Run(
        Request(AsioInternalMode::control_panel, event));
    const auto elapsed = std::chrono::steady_clock::now() - started;
    canceller.join();
    CloseHandle(event);
    const DWORD pid = OutputPid(outcome);
    const bool valid =
        outcome.status == AsioIsolatedProcessStatus::cancelled &&
            pid != 0 && ProcessIsNotRunning(pid) &&
            elapsed < std::chrono::seconds{2};
    if (!valid) {
        std::cerr << "cancel status=" << static_cast<int>(outcome.status)
                  << " error=" << outcome.win32_error
                  << " bytes=" << outcome.standard_output.size()
                  << " pid=" << pid << '\n';
    }
    return Expect(
        valid,
        "panel cancellation kills and joins the real child within a bound");
}

int TestInvalidContractsFailBeforeLaunch() {
    HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (event == nullptr) {
        return 1;
    }
    std::vector<AsioIsolatedProcessRequest> invalid;

    auto relative = Request(AsioInternalMode::probe);
    relative.executable_path = L"AsioIsolatedProcessTestChild.exe";
    invalid.push_back(relative);

    auto shell = Request(AsioInternalMode::probe);
    shell.use_shell = true;
    invalid.push_back(shell);

    auto no_bound = Request(AsioInternalMode::probe);
    no_bound.maximum_stdout_bytes = 0;
    invalid.push_back(no_bound);

    auto conflicting_probe = Request(AsioInternalMode::probe);
    conflicting_probe.cancellation_event = event;
    invalid.push_back(conflicting_probe);

    auto timed_panel = Request(AsioInternalMode::control_panel, event);
    timed_panel.timeout = std::chrono::milliseconds{1};
    invalid.push_back(timed_panel);

    invalid.push_back(Request(AsioInternalMode::control_panel));

    auto flags = Request(AsioInternalMode::probe);
    flags.creation_flags = CREATE_NO_WINDOW;
    invalid.push_back(flags);

    auto inheritance = Request(AsioInternalMode::probe);
    inheritance.inherit_handles = false;
    invalid.push_back(inheritance);

    auto handle_list = Request(AsioInternalMode::probe);
    handle_list.restricted_handle_list = false;
    invalid.push_back(handle_list);

    auto kill_job = Request(AsioInternalMode::probe);
    kill_job.kill_on_job_close = false;
    invalid.push_back(kill_job);

    ProductionAsioIsolatedProcessActions actions;
    int failures{};
    for (const auto& request : invalid) {
        const auto outcome = actions.Run(request);
        failures += Expect(
            outcome.status ==
                    AsioIsolatedProcessStatus::create_failed &&
                outcome.win32_error == ERROR_INVALID_PARAMETER &&
                outcome.standard_output.empty(),
            "invalid isolation contract is rejected before child launch");
    }
    CloseHandle(event);
    return failures;
}

} // namespace

int main() {
    int failures{};
    failures += TestModeMapping();
    failures += TestRealTimeout();
    failures += TestRealCancellation();
    failures += TestInvalidContractsFailBeforeLaunch();
    return failures == 0 ? 0 : 1;
}

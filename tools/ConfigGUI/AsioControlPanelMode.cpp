// SPDX-License-Identifier: CC0-1.0

#include "AsioControlPanelMode.h"

#include "AsioModeHost.h"
#include "Audio/Asio/AsioControlPanel.h"
#include "Audio/Asio/AsioDriver.h"
#include "Audio/Asio/AsioDriverCatalog.h"
#include "Audio/Asio/AsioProbeProtocol.h"

#include <Windows.h>

#include <expected>
#include <utility>

namespace {

enum class ExitCode : int {
    success = 0,
    input_io = 20,
    request_protocol = 21,
    com_initialization = 22,
    window_creation = 23,
    response_protocol = 24,
    output_io = 25,
    unexpected = 26,
};

int ReadExitCode(AsioModeHostError error) noexcept {
    return error == AsioModeHostError::input_io
        ? static_cast<int>(ExitCode::input_io)
        : error == AsioModeHostError::protocol
            ? static_cast<int>(ExitCode::request_protocol)
            : static_cast<int>(ExitCode::unexpected);
}

void WaitForPanel(void*, HWND owner) noexcept {
    WaitForVisiblePanelWindows(owner);
}

} // namespace

int RunAsioControlPanelMode() noexcept {
    try {
        const auto input = ReadAsioModeMessage(
            GetStdHandle(STD_INPUT_HANDLE));
        if (!input) {
            return ReadExitCode(input.error());
        }
        auto request = gc::audio::DecodeAsioControlPanelRequest(*input);
        if (!request) {
            return static_cast<int>(ExitCode::request_protocol);
        }

        AsioStaApartment com;
        if (!com.ready()) {
            return static_cast<int>(ExitCode::com_initialization);
        }
        AsioHiddenOwnerWindow owner;
        if (!owner.Create()) {
            return static_cast<int>(ExitCode::window_creation);
        }

        gc::audio::ProductionAsioRegistrySource registry;
        gc::audio::ProductionAsioDriverFactory factory;
        auto opened = gc::audio::OpenAsioControlPanel(
            registry,
            factory,
            *request,
            owner.get(),
            {
                .wait_for_visible_windows = &WaitForPanel,
            });
        gc::audio::AsioControlPanelResult result = opened
            ? gc::audio::AsioControlPanelResult{}
            : gc::audio::AsioControlPanelResult{
                  std::unexpected(std::move(opened.error()))};
        auto response = gc::audio::EncodeAsioControlPanelResult(result);
        if (!response) {
            return static_cast<int>(ExitCode::response_protocol);
        }
        if (!WriteAsioModeMessage(
                GetStdHandle(STD_OUTPUT_HANDLE),
                *response)) {
            return static_cast<int>(ExitCode::output_io);
        }
        return static_cast<int>(ExitCode::success);
    } catch (...) {
        return static_cast<int>(ExitCode::unexpected);
    }
}

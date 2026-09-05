// SPDX-License-Identifier: CC0-1.0

#include "AsioControlPanelMode.h"

#include "AsioModeHost.h"
#include "Audio/Asio/AsioControlPanel.h"
#include "Audio/Asio/AsioDriver.h"
#include "Audio/Asio/AsioDriverCatalog.h"
#include "Audio/Asio/AsioProbeProtocol.h"

#include <Windows.h>

#include <exception>
#include <expected>
#include <memory>
#include <optional>
#include <string>
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

using namespace gc::audio;

AsioFailure ValidationFailure(
    const AsioFailureStage stage,
    std::string detail)
{
    return {
        .stage = stage,
        .domain = AsioResultDomain::none,
        .detail = std::move(detail),
    };
}

AsioFailure DriverFailure(
    IAsioDriver& driver,
    const AsioFailureStage stage,
    const ASIOError result,
    std::string detail)
{
    char message[124]{};
    driver.GetErrorMessage(message);
    return {
        .stage = stage,
        .domain = AsioResultDomain::asio,
        .result = static_cast<std::int64_t>(result),
        .driver_message = AsioDisplayTextToUtf8(message),
        .detail = std::move(detail),
    };
}

std::expected<void, AsioFailure> OpenAsioControlPanel(
    IAsioRegistrySource& registry,
    IAsioDriverFactory& factory,
    const AsioControlPanelRequest& request,
    HWND owner) noexcept
{
    std::unique_ptr<IAsioDriver> driver;
    std::optional<AsioFailure> failure;
    const auto finish = [&]() -> std::expected<void, AsioFailure>
    {
        if (driver != nullptr)
        {
            const auto result = driver->Exit();
            if (result != ASE_OK && !failure)
            {
                failure = AsioFailure{
                    .stage = AsioFailureStage::exit,
                    .domain = AsioResultDomain::asio,
                    .result = static_cast<std::int64_t>(result),
                    .detail = "ASIO control-panel Exit failed",
                };
            }
        }
        if (failure)
        {
            return std::unexpected(std::move(*failure));
        }
        return {};
    };

    try
    {
        if (request.driver_name.empty())
        {
            return std::unexpected(ValidationFailure(
                AsioFailureStage::registry,
                "ASIO control panel requires a driver name"));
        }
        if (owner == nullptr)
        {
            return std::unexpected(ValidationFailure(
                AsioFailureStage::init,
                "ASIO control panel requires a valid Windows owner"));
        }
        auto registration = ResolveAsioDriver(
            registry,
            request.driver_name);
        if (!registration)
        {
            return std::unexpected(std::move(registration.error()));
        }
        auto created = factory.Create(registration->clsid);
        if (!created)
        {
            return std::unexpected(std::move(created.error()));
        }
        driver = std::move(*created);

        if (driver->Init(owner) != ASIOTrue)
        {
            failure = DriverFailure(
                *driver,
                AsioFailureStage::init,
                ASE_NotPresent,
                "ASIO driver rejected init(owner) for its control panel");
            return finish();
        }

        const auto result = driver->ControlPanel();
        if (result != ASE_OK && result != ASE_SUCCESS)
        {
            failure = DriverFailure(
                *driver,
                AsioFailureStage::control_panel,
                result,
                "ASIO driver controlPanel failed");
            return finish();
        }
        WaitForVisiblePanelWindows(owner);
        return finish();
    }
    catch (const std::exception& error)
    {
        failure = ValidationFailure(
            AsioFailureStage::control_panel,
            "ASIO control panel failed: " + std::string{error.what()});
        return finish();
    }
    catch (...)
    {
        failure = ValidationFailure(
            AsioFailureStage::control_panel,
            "ASIO control panel failed unexpectedly");
        return finish();
    }
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
        auto opened = OpenAsioControlPanel(
            registry,
            factory,
            *request,
            owner.get());
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

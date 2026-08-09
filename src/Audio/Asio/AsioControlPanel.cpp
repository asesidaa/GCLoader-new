// SPDX-License-Identifier: CC0-1.0

#include "Audio/Asio/AsioControlPanel.h"

#include <cstdint>
#include <exception>
#include <string>
#include <utility>

namespace gc::audio {
namespace {

AsioFailure ValidationFailure(
    AsioFailureStage stage,
    std::string detail) {
    return {
        .stage = stage,
        .domain = AsioResultDomain::none,
        .detail = std::move(detail),
    };
}

AsioFailure DriverFailure(
    IAsioDriver& driver,
    AsioFailureStage stage,
    ASIOError result,
    std::string detail) {
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

} // namespace

std::expected<void, AsioFailure> OpenAsioControlPanel(
    IAsioRegistrySource& registry,
    IAsioDriverFactory& factory,
    const AsioControlPanelRequest& request,
    HWND owner,
    AsioControlPanelActions actions) noexcept {
    try {
        if (request.driver_name.empty()) {
            return std::unexpected(ValidationFailure(
                AsioFailureStage::registry,
                "ASIO control panel requires a driver name"));
        }
        if (owner == nullptr) {
            return std::unexpected(ValidationFailure(
                AsioFailureStage::init,
                "ASIO control panel requires a valid Windows owner"));
        }
        if (actions.wait_for_visible_windows == nullptr) {
            return std::unexpected(ValidationFailure(
                AsioFailureStage::control_panel,
                "ASIO control panel requires a window wait callback"));
        }

        auto registration = ResolveAsioDriver(
            registry,
            request.driver_name);
        if (!registration) {
            return std::unexpected(std::move(registration.error()));
        }

        auto driver = factory.Create(registration->clsid);
        if (!driver) {
            return std::unexpected(std::move(driver.error()));
        }
        if ((*driver)->Init(owner) != ASIOTrue) {
            return std::unexpected(DriverFailure(
                **driver,
                AsioFailureStage::init,
                ASIOFalse,
                "ASIO driver rejected init(owner) for its control panel"));
        }

        const ASIOError result = (*driver)->ControlPanel();
        if (result != ASE_OK && result != ASE_SUCCESS) {
            return std::unexpected(DriverFailure(
                **driver,
                AsioFailureStage::control_panel,
                result,
                "ASIO driver controlPanel failed"));
        }

        actions.wait_for_visible_windows(actions.context, owner);
        return {};
    } catch (const std::exception& error) {
        return std::unexpected(ValidationFailure(
            AsioFailureStage::control_panel,
            "ASIO control panel failed: " + std::string{error.what()}));
    } catch (...) {
        return std::unexpected(ValidationFailure(
            AsioFailureStage::control_panel,
            "ASIO control panel failed unexpectedly"));
    }
}

} // namespace gc::audio

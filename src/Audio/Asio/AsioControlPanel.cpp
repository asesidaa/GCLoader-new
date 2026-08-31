// SPDX-License-Identifier: CC0-1.0

#include "Audio/Asio/AsioControlPanel.h"

#include <exception>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace gc::audio
{
    namespace
    {
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
    } // namespace

    std::expected<void, AsioFailure> OpenAsioControlPanel(
        IAsioRegistrySource& registry,
        IAsioDriverFactory& factory,
        const AsioControlPanelRequest& request,
        HWND owner,
        const AsioControlPanelActions actions) noexcept
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
            if (actions.wait_for_visible_windows == nullptr)
            {
                return std::unexpected(ValidationFailure(
                    AsioFailureStage::control_panel,
                    "ASIO control panel requires a window wait callback"));
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
            actions.wait_for_visible_windows(actions.context, owner);
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
} // namespace gc::audio

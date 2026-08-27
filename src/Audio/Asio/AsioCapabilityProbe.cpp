// SPDX-License-Identifier: CC0-1.0

#include "Audio/Asio/AsioCapabilityProbe.h"

#include "Audio/Asio/AsioSession.h"

#include <memory>
#include <string>
#include <utility>

namespace gc::audio
{
    namespace
    {
        void InertBufferSwitch(long, ASIOBool)
        {
        }

        ASIOTime* InertBufferSwitchTimeInfo(
            ASIOTime* parameters,
            long,
            ASIOBool)
        {
            return parameters;
        }

        void InertSampleRateDidChange(ASIOSampleRate)
        {
        }

        long InertAsioMessage(long, long, void*, double*)
        {
            return 0;
        }

        ASIOCallbacks InertCallbacks() noexcept
        {
            return {
                .bufferSwitch = &InertBufferSwitch,
                .sampleRateDidChange = &InertSampleRateDidChange,
                .asioMessage = &InertAsioMessage,
                .bufferSwitchTimeInfo = &InertBufferSwitchTimeInfo,
            };
        }

        AsioFailure ProbeFailure(std::string detail)
        {
            return {
                .stage = AsioFailureStage::protocol,
                .domain = AsioResultDomain::none,
                .detail = std::move(detail),
            };
        }
    } // namespace

    std::expected<AsioCapabilityReport, AsioFailure>
    ProbeAsioCapability(
        IAsioRegistrySource& registry,
        IAsioDriverFactory& factory,
        const AsioStreamRequest& request,
        HWND system_reference,
        AsioProbeMode mode) noexcept
    {
        try
        {
            auto registration = ResolveAsioDriver(
                registry,
                request.driver_name);
            if (!registration)
            {
                return std::unexpected(std::move(registration.error()));
            }
            auto driver = factory.Create(registration->clsid);
            if (!driver)
            {
                return std::unexpected(std::move(driver.error()));
            }
            auto session = AsioSession::Prepare(
                std::move(*registration),
                std::move(*driver),
                request,
                system_reference,
                mode,
                AsioAdoptCurrentRate{});
            if (!session)
            {
                return std::unexpected(std::move(session.error().failure));
            }

            ASIOCallbacks callbacks = InertCallbacks();
            auto buffers = (*session)->CreateOutputBuffers(&callbacks);
            if (!buffers)
            {
                return std::unexpected(std::move(buffers.error()));
            }
            AsioCapabilityReport report = (*session)->report();
            auto closed = (*session)->Close();
            if (!closed)
            {
                return std::unexpected(std::move(closed.error()));
            }
            return report;
        }
        catch (const std::exception& error)
        {
            return std::unexpected(ProbeFailure(
                "ASIO capability probe failed: " +
                std::string{error.what()}));
        }
        catch (...)
        {
            return std::unexpected(ProbeFailure(
                "ASIO capability probe failed unexpectedly"));
        }
    }
} // namespace gc::audio

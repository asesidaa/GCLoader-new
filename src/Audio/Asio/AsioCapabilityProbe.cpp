// SPDX-License-Identifier: CC0-1.0

#include "Audio/Asio/AsioCapabilityProbe.h"

#include "Audio/Asio/AsioBufferRules.h"
#include "Audio/Asio/AsioSampleConverter.h"
#include "Audio/SupportedOutputSampleRate.h"

#include <array>
#include <cmath>
#include <exception>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace gc::audio
{
    namespace
    {
        void InertBufferSwitch(long, ASIOBool) noexcept
        {
        }

        ASIOTime* InertBufferSwitchTimeInfo(
            ASIOTime*,
            long,
            ASIOBool) noexcept
        {
            return nullptr;
        }

        void InertSampleRateDidChange(ASIOSampleRate) noexcept
        {
        }

        long InertAsioMessage(long, long, void*, double*) noexcept
        {
            return 0;
        }

        constexpr ASIOCallbacks kInertCallbacks{
            .bufferSwitch = &InertBufferSwitch,
            .sampleRateDidChange = &InertSampleRateDidChange,
            .asioMessage = &InertAsioMessage,
            .bufferSwitchTimeInfo = &InertBufferSwitchTimeInfo,
        };

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

        AsioFailure ExitFailure(const ASIOError result)
        {
            return {
                .stage = AsioFailureStage::exit,
                .domain = AsioResultDomain::asio,
                .result = static_cast<std::int64_t>(result),
                .detail = "ASIO driver Exit failed",
            };
        }

        std::optional<std::uint32_t> IntegralSampleRate(
            const double rate) noexcept
        {
            if (!std::isfinite(rate) || rate <= 0.0 ||
                std::trunc(rate) != rate ||
                rate > static_cast<double>(
                    (std::numeric_limits<std::uint32_t>::max)()))
            {
                return std::nullopt;
            }
            return static_cast<std::uint32_t>(rate);
        }

        bool ValidBufferLimits(const AsioBufferLimits& limits) noexcept
        {
            return limits.minimum > 0 &&
                limits.maximum >= limits.minimum &&
                limits.preferred >= limits.minimum &&
                limits.preferred <= limits.maximum;
        }

        std::expected<AsioChannelDescriptor, AsioFailure>
        QueryOutputChannel(
            IAsioDriver& driver,
            const std::uint32_t index,
            const bool require_active = false)
        {
            ASIOChannelInfo info{
                .channel = static_cast<long>(index),
                .isInput = ASIOFalse,
            };
            const auto result = driver.GetChannelInfo(&info);
            if (result != ASE_OK)
            {
                return std::unexpected(DriverFailure(
                    driver,
                    AsioFailureStage::channel_info,
                    result,
                    "ASIO getChannelInfo failed for an output channel"));
            }
            if (require_active && info.isActive != ASIOTrue)
            {
                return std::unexpected(ValidationFailure(
                    AsioFailureStage::channel_info,
                    "ASIO did not mark a created output channel active"));
            }
            return AsioChannelDescriptor{
                .index = index,
                .name = AsioDisplayTextToUtf8(info.name),
                .sample_type = info.type,
            };
        }
    } // namespace

    std::expected<AsioCapabilityReport, AsioFailure>
    ProbeAsioCapability(
        IAsioRegistrySource& registry,
        IAsioDriverFactory& factory,
        const AsioStreamRequest& request,
        HWND system_reference,
        const AsioProbeMode mode) noexcept
    {
        std::unique_ptr<IAsioDriver> driver;
        bool buffers_created{};
        std::optional<AsioFailure> failure;

        const auto finish = [&](std::optional<AsioCapabilityReport>&& report)
            -> std::expected<AsioCapabilityReport, AsioFailure>
        {
            if (driver != nullptr && buffers_created)
            {
                const auto result = driver->DisposeBuffers();
                if (result != ASE_OK && !failure)
                {
                    failure = DriverFailure(
                        *driver,
                        AsioFailureStage::dispose,
                        result,
                        "ASIO validation-buffer disposal failed");
                }
            }
            if (driver != nullptr)
            {
                const auto result = driver->Exit();
                if (result != ASE_OK && !failure)
                {
                    failure = ExitFailure(result);
                }
            }
            if (failure)
            {
                return std::unexpected(std::move(*failure));
            }
            if (!report)
            {
                return std::unexpected(ValidationFailure(
                    AsioFailureStage::protocol,
                    "ASIO probe ended without a report"));
            }
            return std::move(*report);
        };

        try
        {
            auto registration = ResolveAsioDriver(registry, request.driver_name);
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

            if (driver->Init(system_reference) != ASIOTrue)
            {
                failure = DriverFailure(
                    *driver,
                    AsioFailureStage::init,
                    ASE_NotPresent,
                    "ASIO driver rejected init(system_reference)");
                return finish(std::nullopt);
            }

            AsioCapabilityReport report{
                .registration = std::move(*registration),
                .selected_base_channel = request.output_base_channel,
            };
            char driver_name[32]{};
            driver->GetDriverName(driver_name);
            report.reported_driver_name = AsioDisplayTextToUtf8(driver_name);
            report.driver_version = driver->GetDriverVersion();

            const auto rate_result = driver->GetSampleRate(&report.sample_rate);
            if (rate_result != ASE_OK ||
                !std::isfinite(report.sample_rate) ||
                report.sample_rate <= 0.0)
            {
                failure = DriverFailure(
                    *driver,
                    AsioFailureStage::sample_rate,
                    rate_result,
                    "ASIO getSampleRate did not return a finite positive rate");
                return finish(std::nullopt);
            }

            const auto buffer_result = driver->GetBufferSize(
                &report.buffer_limits.minimum,
                &report.buffer_limits.maximum,
                &report.buffer_limits.preferred,
                &report.buffer_limits.granularity);
            if (buffer_result != ASE_OK ||
                !ValidBufferLimits(report.buffer_limits))
            {
                failure = DriverFailure(
                    *driver,
                    AsioFailureStage::buffer_metadata,
                    buffer_result,
                    "ASIO getBufferSize returned invalid limits");
                return finish(std::nullopt);
            }
            report.effective_buffer_frames = request.buffer_frames != 0
                                                 ? request.buffer_frames
                                                 : static_cast<std::uint32_t>(report.buffer_limits.preferred);

            long input_channels{};
            long output_channels{};
            const auto channel_result = driver->GetChannels(
                &input_channels,
                &output_channels);
            if (channel_result != ASE_OK || input_channels < 0 ||
                output_channels < 0 ||
                input_channels > static_cast<long>(kMaxAsioReportedChannels) ||
                output_channels > static_cast<long>(kMaxAsioReportedChannels))
            {
                failure = DriverFailure(
                    *driver,
                    AsioFailureStage::channels,
                    channel_result,
                    "ASIO getChannels returned invalid channel counts");
                return finish(std::nullopt);
            }
            report.input_channels = static_cast<std::uint32_t>(input_channels);
            report.output_channels.reserve(
                static_cast<std::size_t>(output_channels));
            for (std::uint32_t index{};
                 index < static_cast<std::uint32_t>(output_channels);
                 ++index)
            {
                auto descriptor = QueryOutputChannel(*driver, index);
                if (!descriptor)
                {
                    failure = std::move(descriptor.error());
                    return finish(std::nullopt);
                }
                report.output_channels.push_back(std::move(*descriptor));
            }

            if (mode == AsioProbeMode::validate)
            {
                const auto integral_rate = IntegralSampleRate(report.sample_rate);
                if (!integral_rate ||
                    !IsSupportedOutputSampleRate(*integral_rate))
                {
                    failure = ValidationFailure(
                        AsioFailureStage::sample_rate,
                        "ASIO current sample rate is not supported by the mixer");
                    return finish(std::nullopt);
                }
                if (!ValidateAsioBufferFrames(
                    report.buffer_limits,
                    request.buffer_frames))
                {
                    failure = ValidationFailure(
                        AsioFailureStage::buffer_metadata,
                        "Configured ASIO buffer frame count is not supported exactly");
                    return finish(std::nullopt);
                }
                const auto output_count =
                    static_cast<std::uint32_t>(output_channels);
                if (output_count < 2 ||
                    request.output_base_channel >= output_count - 1)
                {
                    failure = ValidationFailure(
                        AsioFailureStage::channels,
                        "Configured ASIO output pair is not in range and adjacent");
                    return finish(std::nullopt);
                }

                std::array<ASIOBufferInfo, 2> buffers{
                    ASIOBufferInfo{
                        .isInput = ASIOFalse,
                        .channelNum = static_cast<long>(
                            request.output_base_channel),
                    },
                    ASIOBufferInfo{
                        .isInput = ASIOFalse,
                        .channelNum = static_cast<long>(
                            request.output_base_channel + 1),
                    },
                };
                const auto create_result = driver->CreateBuffers(
                    buffers.data(),
                    static_cast<long>(buffers.size()),
                    static_cast<long>(request.buffer_frames),
                    &kInertCallbacks);
                if (create_result != ASE_OK)
                {
                    failure = DriverFailure(
                        *driver,
                        AsioFailureStage::create_buffers,
                        create_result,
                        "ASIO createBuffers failed for the configured pair");
                    return finish(std::nullopt);
                }
                buffers_created = true;

                for (std::uint32_t offset{}; offset < 2; ++offset)
                {
                    auto descriptor = QueryOutputChannel(
                        *driver,
                        request.output_base_channel + offset,
                        true);
                    if (!descriptor)
                    {
                        failure = std::move(descriptor.error());
                        return finish(std::nullopt);
                    }
                    if (!IsSupportedAsioOutputType(descriptor->sample_type))
                    {
                        failure = ValidationFailure(
                            AsioFailureStage::channel_info,
                            "Configured ASIO output pair uses an unsupported sample type");
                        return finish(std::nullopt);
                    }
                    report.output_channels[request.output_base_channel + offset] =
                        std::move(*descriptor);
                }
            }

            return finish(std::move(report));
        }
        catch (const std::exception& error)
        {
            failure = ValidationFailure(
                AsioFailureStage::protocol,
                "ASIO capability probe failed: " +
                std::string{error.what()});
            return finish(std::nullopt);
        }
        catch (...)
        {
            failure = ValidationFailure(
                AsioFailureStage::protocol,
                "ASIO capability probe failed unexpectedly");
            return finish(std::nullopt);
        }
    }
} // namespace gc::audio

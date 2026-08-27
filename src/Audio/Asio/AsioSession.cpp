// SPDX-License-Identifier: CC0-1.0

#include "Audio/Asio/AsioSession.h"

#include "Audio/Asio/AsioBufferRules.h"
#include "Audio/Asio/AsioSampleConverter.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <format>
#include <memory>
#include <string>
#include <utility>

namespace gc::audio
{
    namespace
    {
        constexpr ASIOSampleRate kAsioSampleRate = 48'000.0;

        AsioFailure ValidationFailure(
            AsioFailureStage stage,
            std::string detail)
        {
            return {
                .stage = stage,
                .domain = AsioResultDomain::none,
                .detail = std::move(detail),
            };
        }

        AsioFailure DriverFailure(
            IAsioDriver* driver,
            AsioFailureStage stage,
            ASIOError result,
            std::string detail)
        {
            std::string driver_message;
            if (driver != nullptr)
            {
                char message[124]{};
                driver->GetErrorMessage(message);
                driver_message = AsioDisplayTextToUtf8(message);
            }
            return {
                .stage = stage,
                .domain = AsioResultDomain::asio,
                .result = static_cast<std::int64_t>(result),
                .driver_message = std::move(driver_message),
                .detail = std::move(detail),
            };
        }

        const char* BufferRuleName(AsioBufferRuleError error) noexcept
        {
            switch (error)
            {
            case AsioBufferRuleError::invalid_metadata:
                return "driver metadata is internally inconsistent";
            case AsioBufferRuleError::below_minimum:
                return "requested frames are below the driver minimum";
            case AsioBufferRuleError::above_maximum:
                return "requested frames are above the driver maximum";
            case AsioBufferRuleError::not_power_of_two:
                return "driver requires a power-of-two frame count";
            case AsioBufferRuleError::not_granular:
                return "requested frames do not match driver granularity";
            }
            return "unknown buffer rule failure";
        }
    } // namespace

    AsioSession::AsioSession(
        AsioDriverRegistration registration,
        std::unique_ptr<IAsioDriver> driver,
        bool restore_sample_rate) noexcept
        : driver_(std::move(driver)),
          creating_thread_id_(GetCurrentThreadId()),
          restore_sample_rate_(restore_sample_rate)
    {
        report_.registration = std::move(registration);
    }

    AsioSession::~AsioSession()
    {
#ifndef NDEBUG
        assert(
            creating_thread_id_ == GetCurrentThreadId() &&
            "ASIO session must be destroyed on its creating thread");
#endif
        if (auto closed = Close(); !closed)
        {
            cleanup_failure_ = std::move(closed.error());
        }
    }

    std::expected<std::unique_ptr < AsioSession>
    ,
    AsioFailure
    >
    AsioSession::Prepare(
        AsioDriverRegistration registration,
        std::unique_ptr<IAsioDriver> driver,
        const AsioStreamRequest& request,
        HWND system_reference,
        AsioProbeMode mode,
        bool restore_sample_rate) noexcept
    {
        try
        {
            if (!driver)
            {
                return std::unexpected(ValidationFailure(
                    AsioFailureStage::com,
                    "ASIO session requires a driver interface"));
            }
            auto session = std::unique_ptr < AsioSession > (new AsioSession(
                std::move(registration),
                std::move(driver),
                restore_sample_rate));
            auto prepared = session->PrepareDriver(
                request,
                system_reference,
                mode);
            if (!prepared)
            {
                return std::unexpected(std::move(prepared.error()));
            }
            return session;
        }
        catch (const std::exception& error)
        {
            return std::unexpected(ValidationFailure(
                AsioFailureStage::protocol,
                "ASIO session preparation failed: " +
                std::string{error.what()}));
        }
        catch (...)
        {
            return std::unexpected(ValidationFailure(
                AsioFailureStage::protocol,
                "ASIO session preparation failed unexpectedly"));
        }
    }

    std::expected<void, AsioFailure> AsioSession::PrepareDriver(
        const AsioStreamRequest& request,
        HWND system_reference,
        AsioProbeMode mode)
    {
        if (system_reference == nullptr)
        {
            return std::unexpected(ValidationFailure(
                AsioFailureStage::init,
                "ASIO init requires a valid Windows system reference"));
        }
        if (driver_->Init(system_reference) != ASIOTrue)
        {
            return std::unexpected(DriverFailure(
                driver_.get(),
                AsioFailureStage::init,
                ASIOFalse,
                "ASIO driver rejected init(system_reference)"));
        }

        char reported_name[32]{};
        driver_->GetDriverName(reported_name);
        report_.reported_driver_name =
            AsioDisplayTextToUtf8(reported_name);
        report_.driver_version = driver_->GetDriverVersion();

        const ASIOError overload = driver_->Future(
            kAsioCanReportOverload,
            nullptr);
        if (overload == ASE_SUCCESS)
        {
            report_.overload_reporting_supported = true;
        }
        else if (overload == ASE_OK || overload == ASE_NotPresent)
        {
            report_.overload_reporting_supported = false;
        }
        else
        {
            return std::unexpected(DriverFailure(
                driver_.get(),
                AsioFailureStage::identity,
                overload,
                "ASIO overload-reporting capability probe failed"));
        }

        long input_channels{};
        long output_channels{};
        const ASIOError channels = driver_->GetChannels(
            &input_channels,
            &output_channels);
        if (channels != ASE_OK)
        {
            return std::unexpected(DriverFailure(
                driver_.get(),
                AsioFailureStage::channels,
                channels,
                "ASIO getChannels failed"));
        }
        if (input_channels < 0 || output_channels < 0 ||
            input_channels > static_cast<long>(kMaxAsioReportedChannels) ||
            output_channels > static_cast<long>(kMaxAsioReportedChannels))
        {
            return std::unexpected(ValidationFailure(
                AsioFailureStage::channels,
                "ASIO channel counts must be between 0 and 256"));
        }
        report_.input_channels = static_cast<std::uint32_t>(input_channels);

        ASIOSampleRate current_rate{};
        ASIOError sample_rate = driver_->GetSampleRate(&current_rate);
        if (sample_rate != ASE_OK)
        {
            return std::unexpected(DriverFailure(
                driver_.get(),
                AsioFailureStage::sample_rate,
                sample_rate,
                "ASIO getSampleRate failed"));
        }
        if (!std::isfinite(current_rate) || current_rate <= 0.0)
        {
            return std::unexpected(ValidationFailure(
                AsioFailureStage::sample_rate,
                "ASIO driver reported an invalid current sample rate"));
        }
        report_.original_sample_rate = current_rate;

        const ASIOError supported = driver_->CanSampleRate(kAsioSampleRate);
        if (supported != ASE_OK)
        {
            return std::unexpected(DriverFailure(
                driver_.get(),
                AsioFailureStage::sample_rate,
                supported,
                "ASIO driver does not accept exact 48000 Hz output"));
        }
        if (current_rate != kAsioSampleRate)
        {
            const ASIOError changed = driver_->SetSampleRate(kAsioSampleRate);
            if (changed != ASE_OK)
            {
                return std::unexpected(DriverFailure(
                    driver_.get(),
                    AsioFailureStage::sample_rate,
                    changed,
                    "ASIO setSampleRate(48000) failed"));
            }
            sample_rate_changed_ = true;
        }

        ASIOSampleRate verified_rate{};
        sample_rate = driver_->GetSampleRate(&verified_rate);
        if (sample_rate != ASE_OK)
        {
            return std::unexpected(DriverFailure(
                driver_.get(),
                AsioFailureStage::sample_rate,
                sample_rate,
                "ASIO sample-rate verification query failed"));
        }
        if (!std::isfinite(verified_rate) ||
            verified_rate != kAsioSampleRate)
        {
            return std::unexpected(ValidationFailure(
                AsioFailureStage::sample_rate,
                "ASIO driver did not retain exact 48000 Hz output"));
        }
        report_.sample_rate = verified_rate;

        const ASIOError buffer_size = driver_->GetBufferSize(
            &report_.buffer_limits.minimum,
            &report_.buffer_limits.maximum,
            &report_.buffer_limits.preferred,
            &report_.buffer_limits.granularity);
        if (buffer_size != ASE_OK)
        {
            return std::unexpected(DriverFailure(
                driver_.get(),
                AsioFailureStage::buffer_metadata,
                buffer_size,
                "ASIO getBufferSize failed"));
        }
        if (mode == AsioProbeMode::validate && request.buffer_frames == 0)
        {
            return std::unexpected(ValidationFailure(
                AsioFailureStage::buffer_metadata,
                "ASIO validation requires a positive exact frame count"));
        }
        const std::uint32_t effective_frames =
            mode == AsioProbeMode::inspect && request.buffer_frames == 0
                ? report_.buffer_limits.preferred > 0
                      ? static_cast<std::uint32_t>(
                          report_.buffer_limits.preferred)
                      : 0U
                : request.buffer_frames;
        const auto frame_validation = ValidateAsioBufferFrames(
            report_.buffer_limits,
            effective_frames);
        if (!frame_validation)
        {
            return std::unexpected(ValidationFailure(
                AsioFailureStage::buffer_metadata,
                std::string{"Invalid exact ASIO buffer frames: "} +
                BufferRuleName(frame_validation.error())));
        }
        report_.effective_buffer_frames = effective_frames;

        report_.output_channels.reserve(
            static_cast<std::size_t>(output_channels));
        for (long channel = 0; channel < output_channels; ++channel)
        {
            ASIOChannelInfo info{};
            info.channel = channel;
            info.isInput = ASIOFalse;
            const ASIOError channel_result = driver_->GetChannelInfo(&info);
            if (channel_result != ASE_OK)
            {
                return std::unexpected(DriverFailure(
                    driver_.get(),
                    AsioFailureStage::channel_info,
                    channel_result,
                    std::format(
                        "ASIO getChannelInfo failed for output channel {}",
                        channel)));
            }
            report_.output_channels.push_back({
                .index = static_cast<std::uint32_t>(channel),
                .name = AsioDisplayTextToUtf8(info.name),
                .sample_type = info.type,
            });
        }

        const std::uint64_t selected_second =
            static_cast<std::uint64_t>(request.output_base_channel) + 1U;
        if (selected_second >= report_.output_channels.size())
        {
            return std::unexpected(ValidationFailure(
                AsioFailureStage::channel_info,
                "ASIO output base channel does not identify two adjacent outputs"));
        }
        const auto& left = report_.output_channels[
            request.output_base_channel];
        const auto& right = report_.output_channels[
            static_cast<std::size_t>(selected_second)];
        if (!IsSupportedAsioOutputType(left.sample_type) ||
            !IsSupportedAsioOutputType(right.sample_type))
        {
            return std::unexpected(ValidationFailure(
                AsioFailureStage::channel_info,
                "Selected ASIO output pair uses an unsupported sample type"));
        }
        report_.selected_base_channel = request.output_base_channel;

        const ASIOError output_ready = driver_->OutputReady();
        if (output_ready == ASE_OK)
        {
            report_.output_ready_supported = true;
        }
        else if (output_ready == ASE_NotPresent)
        {
            report_.output_ready_supported = false;
        }
        else
        {
            return std::unexpected(DriverFailure(
                driver_.get(),
                AsioFailureStage::output_ready_probe,
                output_ready,
                "ASIO outputReady capability probe failed"));
        }
        return {};
    }

    std::expected<void, AsioFailure>
    AsioSession::RequireCreatingThread() const
    {
        if (creating_thread_id_ == GetCurrentThreadId())
        {
            return {};
        }
        return std::unexpected(AsioFailure{
            .stage = AsioFailureStage::protocol,
            .domain = AsioResultDomain::win32,
            .result = ERROR_INVALID_THREAD_ID,
            .detail = "ASIO lifecycle call must run on the creating thread",
        });
    }

    std::expected<void, AsioFailure> AsioSession::CreateOutputBuffers(
        ASIOCallbacks* callbacks) noexcept
    {
        try
        {
            if (auto thread = RequireCreatingThread(); !thread)
            {
                return thread;
            }
            if (closed_ || driver_ == nullptr)
            {
                return std::unexpected(ValidationFailure(
                    AsioFailureStage::protocol,
                    "Closed ASIO session cannot create buffers"));
            }
            if (buffers_created_)
            {
                return std::unexpected(ValidationFailure(
                    AsioFailureStage::protocol,
                    "ASIO output buffers may be created exactly once"));
            }
            if (callbacks == nullptr)
            {
                return std::unexpected(ValidationFailure(
                    AsioFailureStage::callback_prepare,
                    "ASIO createBuffers requires callbacks"));
            }

            buffers_ = {};
            buffers_[0].isInput = ASIOFalse;
            buffers_[0].channelNum =
                static_cast<long>(report_.selected_base_channel);
            buffers_[1].isInput = ASIOFalse;
            buffers_[1].channelNum = buffers_[0].channelNum + 1L;
            const ASIOError created = driver_->CreateBuffers(
                buffers_.data(),
                static_cast<long>(buffers_.size()),
                static_cast<long>(report_.effective_buffer_frames),
                callbacks);
            if (created != ASE_OK)
            {
                return std::unexpected(DriverFailure(
                    driver_.get(),
                    AsioFailureStage::create_buffers,
                    created,
                    "ASIO createBuffers failed for the exact selected pair"));
            }
            buffers_created_ = true;

            long input_latency{};
            long output_latency{};
            const ASIOError latency = driver_->GetLatencies(
                &input_latency,
                &output_latency);
            if (latency != ASE_OK)
            {
                return std::unexpected(DriverFailure(
                    driver_.get(),
                    AsioFailureStage::latency,
                    latency,
                    "ASIO getLatencies failed after createBuffers"));
            }
            if (input_latency < 0 || output_latency < 0)
            {
                return std::unexpected(ValidationFailure(
                    AsioFailureStage::latency,
                    "ASIO driver reported a negative latency"));
            }
            report_.input_latency_frames =
                static_cast<std::uint32_t>(input_latency);
            report_.output_latency_frames =
                static_cast<std::uint32_t>(output_latency);
            return {};
        }
        catch (const std::exception& error)
        {
            return std::unexpected(ValidationFailure(
                AsioFailureStage::protocol,
                "ASIO buffer preparation failed: " +
                std::string{error.what()}));
        }
        catch (...)
        {
            return std::unexpected(ValidationFailure(
                AsioFailureStage::protocol,
                "ASIO buffer preparation failed unexpectedly"));
        }
    }

    std::expected<void, AsioFailure> AsioSession::Start() noexcept
    {
        try
        {
            if (auto thread = RequireCreatingThread(); !thread)
            {
                return thread;
            }
            if (closed_ || !buffers_created_ || started_)
            {
                return std::unexpected(ValidationFailure(
                    AsioFailureStage::protocol,
                    "ASIO start requires prepared, non-started buffers"));
            }
            const ASIOError result = driver_->Start();
            if (result != ASE_OK)
            {
                return std::unexpected(DriverFailure(
                    driver_.get(),
                    AsioFailureStage::start,
                    result,
                    "ASIO start failed"));
            }
            started_ = true;
            return {};
        }
        catch (...)
        {
            return std::unexpected(ValidationFailure(
                AsioFailureStage::start,
                "ASIO start failed unexpectedly"));
        }
    }

    std::expected<void, AsioFailure> AsioSession::Stop() noexcept
    {
        try
        {
            if (auto thread = RequireCreatingThread(); !thread)
            {
                return thread;
            }
            if (!started_)
            {
                return {};
            }
            const ASIOError result = driver_->Stop();
            if (result != ASE_OK)
            {
                return std::unexpected(DriverFailure(
                    driver_.get(),
                    AsioFailureStage::stop,
                    result,
                    "ASIO stop failed"));
            }
            started_ = false;
            return {};
        }
        catch (...)
        {
            return std::unexpected(ValidationFailure(
                AsioFailureStage::stop,
                "ASIO stop failed unexpectedly"));
        }
    }

    std::expected<void, AsioFailure> AsioSession::Close() noexcept
    {
        try
        {
            if (closed_)
            {
                return {};
            }
            if (auto thread = RequireCreatingThread(); !thread)
            {
                return thread;
            }
            if (auto stopped = Stop(); !stopped)
            {
                return stopped;
            }
            if (buffers_created_)
            {
                const ASIOError disposed = driver_->DisposeBuffers();
                if (disposed != ASE_OK)
                {
                    return std::unexpected(DriverFailure(
                        driver_.get(),
                        AsioFailureStage::dispose,
                        disposed,
                        "ASIO disposeBuffers failed"));
                }
                buffers_created_ = false;
            }
            if (sample_rate_changed_ && restore_sample_rate_)
            {
                const ASIOError restored = driver_->SetSampleRate(
                    report_.original_sample_rate);
                if (restored != ASE_OK)
                {
                    return std::unexpected(DriverFailure(
                        driver_.get(),
                        AsioFailureStage::restore_sample_rate,
                        restored,
                        "ASIO original sample rate could not be restored"));
                }
            }
            sample_rate_changed_ = false;
            driver_.reset();
            closed_ = true;
            return {};
        }
        catch (...)
        {
            return std::unexpected(ValidationFailure(
                AsioFailureStage::protocol,
                "ASIO close failed unexpectedly"));
        }
    }

    const AsioCapabilityReport& AsioSession::report() const noexcept
    {
        return report_;
    }

    std::span<ASIOBufferInfo> AsioSession::buffers() noexcept
    {
        return buffers_;
    }

    // This accessor intentionally grants mutable driver control.
    // ReSharper disable once CppMemberFunctionMayBeConst
    IAsioDriver& AsioSession::driver() noexcept
    {
        return *driver_;
    }
} // namespace gc::audio

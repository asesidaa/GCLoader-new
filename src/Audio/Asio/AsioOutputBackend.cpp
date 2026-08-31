// SPDX-License-Identifier: CC0-1.0

#include "Audio/Asio/AsioOutputBackend.h"

#include "Audio/Asio/AsioBufferRules.h"
#include "Audio/Asio/AsioSampleConverter.h"
#include "Audio/AudioContractFatal.h"
#include "Audio/SupportedOutputSampleRate.h"

#include <plog/Log.h>

#include <bit>
// ReSharper disable once CppUnusedIncludeDirective
#include <cmath>
// ReSharper disable once CppUnusedIncludeDirective
#include <cstring>
#include <limits>
#include <new>
#include <utility>

namespace gc::audio
{
    namespace
    {
        enum class StartupOperation : std::uint64_t
        {
            allocate_backend = 1,
            resolve_driver,
            create_driver,
            initialize_driver,
            query_sample_rate,
            validate_sample_rate,
            query_buffer_size,
            validate_buffer_size,
            query_channels,
            validate_channel_pair,
            publish_callback_target,
            create_buffers,
            query_channel_info,
            validate_channel_type,
            create_render_core,
            allocate_conversion_storage,
            validate_driver_buffer,
            clear_driver_buffer,
            probe_output_ready,
            start_driver,
            log_format,
        };

        enum class CallbackOperation : std::uint64_t
        {
            missing_target = 1,
            concurrent_callback,
            invalid_buffer_index,
            invalid_time_info,
            changed_time_info,
            invalid_sample_rate,
            invalid_speed,
            render_failure,
            render_short_read,
            render_shape,
            non_finite_pcm,
            conversion_failure,
            invalid_driver_buffer,
            output_ready,
        };

        enum class RuntimeNotification : std::uint64_t
        {
            sample_rate_changed = 1,
            reset,
            buffer_size_changed,
            resync,
            latencies_changed,
            overload,
        };

        enum class ShutdownOperation : std::uint64_t
        {
            stop = 1,
            dispose_buffers,
            exit,
            clear_callback_target,
        };

        template <typename Value>
        [[nodiscard]] constexpr std::uint64_t Bits(Value value) noexcept
        {
            return static_cast<std::uint64_t>(
                static_cast<std::int64_t>(value));
        }

        [[noreturn]] void StartupFatal(
            const StartupOperation operation,
            const std::uint64_t operand1 = 0,
            const std::uint64_t operand2 = 0,
            const std::uint64_t operand3 = 0) noexcept
        {
            FailAudioContract(
                AudioContractFatalReason::AsioDriverContractFailure,
                static_cast<std::uint64_t>(operation),
                operand1,
                operand2,
                operand3);
        }

        [[noreturn]] void StartupFatal(
            const StartupOperation operation,
            const AsioFailure& failure) noexcept
        {
            StartupFatal(
                operation,
                static_cast<std::uint64_t>(failure.stage),
                static_cast<std::uint64_t>(failure.domain),
                Bits(failure.result));
        }

        [[noreturn]] void CallbackFatal(
            const CallbackOperation operation,
            const std::uint64_t operand1 = 0,
            const std::uint64_t operand2 = 0) noexcept
        {
            FailAudioContract(
                AudioContractFatalReason::AsioCallbackContractFailure,
                static_cast<std::uint64_t>(operation),
                operand1,
                operand2);
        }

        [[nodiscard]] std::size_t ConversionBytesOrFatal(
            const std::uint32_t frame_count,
            const ASIOSampleType sample_type) noexcept
        {
            const auto bytes_per_sample = AsioBytesPerSample(sample_type);
            if (!bytes_per_sample ||
                frame_count >
                (std::numeric_limits<std::size_t>::max)() /
                *bytes_per_sample)
            {
                StartupFatal(
                    StartupOperation::validate_channel_type,
                    Bits(sample_type),
                    frame_count);
            }
            return static_cast<std::size_t>(frame_count) *
                *bytes_per_sample;
        }
    } // namespace

    const ASIOCallbacks AsioOutputBackend::callbacks_{
        .bufferSwitch = &AsioOutputBackend::BufferSwitch,
        .sampleRateDidChange = &AsioOutputBackend::SampleRateDidChange,
        .asioMessage = &AsioOutputBackend::AsioMessage,
        .bufferSwitchTimeInfo = &AsioOutputBackend::BufferSwitchTimeInfo,
    };

    std::atomic<AsioOutputBackend*> AsioOutputBackend::callback_target_{};
    std::atomic_flag AsioOutputBackend::callback_active_ = ATOMIC_FLAG_INIT;

    std::unique_ptr<AsioOutputBackend> AsioOutputBackend::Start(
        HWND game_window,
        const AsioStreamRequest& request,
        IAsioRegistrySource& registry,
        IAsioDriverFactory& factory,
        std::shared_ptr<const ma_allocation_callbacks> allocation_callbacks) noexcept
    {
        auto* const backend = new(std::nothrow) AsioOutputBackend;
        if (backend == nullptr)
        {
            StartupFatal(StartupOperation::allocate_backend);
        }

        auto registration = ResolveAsioDriver(registry, request.driver_name);
        if (!registration)
        {
            StartupFatal(StartupOperation::resolve_driver, registration.error());
        }

        auto created = factory.Create(registration->clsid);
        if (!created)
        {
            StartupFatal(StartupOperation::create_driver, created.error());
        }
        backend->driver_ = std::move(*created);

        if (backend->driver_->Init(game_window) != ASIOTrue)
        {
            StartupFatal(StartupOperation::initialize_driver);
        }

        char reported_name[32]{};
        backend->driver_->GetDriverName(reported_name);
        const long driver_version = backend->driver_->GetDriverVersion();

        ASIOSampleRate reported_rate{};
        const ASIOError rate_result =
            backend->driver_->GetSampleRate(&reported_rate);
        if (rate_result != ASE_OK)
        {
            StartupFatal(
                StartupOperation::query_sample_rate,
                Bits(rate_result));
        }
        if (!std::isfinite(reported_rate) || reported_rate <= 0.0 ||
            std::trunc(reported_rate) != reported_rate ||
            reported_rate >
            static_cast<double>((std::numeric_limits<std::uint32_t>::max)()))
        {
            StartupFatal(StartupOperation::validate_sample_rate);
        }
        const auto sample_rate = static_cast<std::uint32_t>(reported_rate);
        if (!IsSupportedOutputSampleRate(sample_rate))
        {
            StartupFatal(
                StartupOperation::validate_sample_rate,
                sample_rate);
        }

        AsioBufferLimits limits{};
        const ASIOError buffer_result = backend->driver_->GetBufferSize(
            &limits.minimum,
            &limits.maximum,
            &limits.preferred,
            &limits.granularity);
        if (buffer_result != ASE_OK)
        {
            StartupFatal(
                StartupOperation::query_buffer_size,
                Bits(buffer_result));
        }
        const auto buffer_validation =
            ValidateAsioBufferFrames(limits, request.buffer_frames);
        if (!buffer_validation)
        {
            StartupFatal(
                StartupOperation::validate_buffer_size,
                static_cast<std::uint64_t>(buffer_validation.error()),
                request.buffer_frames);
        }

        long input_channels{};
        long output_channels{};
        const ASIOError channels_result = backend->driver_->GetChannels(
            &input_channels,
            &output_channels);
        if (channels_result != ASE_OK)
        {
            StartupFatal(
                StartupOperation::query_channels,
                Bits(channels_result));
        }
        const std::uint64_t selected_last =
            static_cast<std::uint64_t>(request.output_base_channel) + 1;
        if (input_channels < 0 || output_channels < 2 ||
            selected_last >= static_cast<std::uint64_t>(output_channels) ||
            selected_last >
            static_cast<std::uint64_t>((std::numeric_limits<long>::max)()))
        {
            StartupFatal(
                StartupOperation::validate_channel_pair,
                request.output_base_channel,
                Bits(output_channels));
        }

        backend->format_.sample_rate = sample_rate;
        backend->format_.frame_count = request.buffer_frames;
        backend->format_.channels = {
            request.output_base_channel,
            request.output_base_channel + 1,
        };
        for (std::size_t channel{}; channel < backend->buffers_.size(); ++channel)
        {
            backend->buffers_[channel] = {
                .isInput = ASIOFalse,
                .channelNum = static_cast<long>(backend->format_.channels[channel]),
                .buffers = {},
            };
        }

        AsioOutputBackend* expected_target{};
        if (!callback_target_.compare_exchange_strong(
            expected_target,
            backend,
            std::memory_order_release,
            std::memory_order_relaxed))
        {
            StartupFatal(
                StartupOperation::publish_callback_target,
                reinterpret_cast<std::uintptr_t>(expected_target));
        }

        const ASIOError create_result = backend->driver_->CreateBuffers(
            backend->buffers_.data(),
            static_cast<long>(backend->buffers_.size()),
            static_cast<long>(request.buffer_frames),
            &callbacks_);
        if (create_result != ASE_OK)
        {
            StartupFatal(
                StartupOperation::create_buffers,
                Bits(create_result));
        }

        for (std::size_t channel{}; channel < backend->buffers_.size(); ++channel)
        {
            ASIOChannelInfo info{
                .channel = static_cast<long>(backend->format_.channels[channel]),
                .isInput = ASIOFalse,
            };
            const ASIOError info_result =
                backend->driver_->GetChannelInfo(&info);
            if (info_result != ASE_OK)
            {
                StartupFatal(
                    StartupOperation::query_channel_info,
                    channel,
                    Bits(info_result));
            }
            if (info.isActive != ASIOTrue ||
                !IsSupportedAsioOutputType(info.type))
            {
                StartupFatal(
                    StartupOperation::validate_channel_type,
                    channel,
                    Bits(info.type));
            }
            backend->format_.sample_types[channel] = info.type;
        }

        ma_result render_result = MA_ERROR;
        backend->render_core_ = AudioRenderCore::Create(
            request.buffer_frames,
            sample_rate,
            std::move(allocation_callbacks),
            nullptr,
            &render_result);
        if (backend->render_core_ == nullptr || render_result != MA_SUCCESS)
        {
            StartupFatal(
                StartupOperation::create_render_core,
                Bits(render_result));
        }

        try
        {
            for (std::size_t channel{};
                 channel < backend->conversion_storage_.size();
                 ++channel)
            {
                backend->conversion_storage_[channel].resize(
                    ConversionBytesOrFatal(
                        request.buffer_frames,
                        backend->format_.sample_types[channel]));
            }
        }
        catch (...)
        {
            StartupFatal(StartupOperation::allocate_conversion_storage);
        }

        for (std::size_t channel{}; channel < backend->buffers_.size(); ++channel)
        {
            const auto byte_count = backend->conversion_storage_[channel].size();
            for (std::size_t half{}; half < 2; ++half)
            {
                void* const raw_buffer = backend->buffers_[channel].buffers[half];
                if (raw_buffer == nullptr)
                {
                    StartupFatal(
                        StartupOperation::validate_driver_buffer,
                        channel,
                        half);
                }
                if (!ClearAsioChannel(
                    backend->format_.sample_types[channel],
                    {static_cast<std::byte*>(raw_buffer), byte_count},
                    request.buffer_frames))
                {
                    StartupFatal(
                        StartupOperation::clear_driver_buffer,
                        channel,
                        half);
                }
            }
        }

        const ASIOError ready_result = backend->driver_->OutputReady();
        if (ready_result == ASE_OK)
        {
            backend->output_ready_supported_ = true;
        }
        else if (ready_result == ASE_NotPresent)
        {
            backend->output_ready_supported_ = false;
        }
        else
        {
            StartupFatal(
                StartupOperation::probe_output_ready,
                Bits(ready_result));
        }

        const ASIOError start_result = backend->driver_->Start();
        if (start_result != ASE_OK)
        {
            StartupFatal(
                StartupOperation::start_driver,
                Bits(start_result));
        }

        try
        {
            PLOG_INFO
                << "ASIO session started: configured='" << request.driver_name
                << "', reported='"
                << AsioDisplayTextToUtf8(std::span{reported_name})
                << "', version=" << driver_version
                << ", rate=" << sample_rate
                << ", frames=" << request.buffer_frames
                << ", outputs=" << request.output_base_channel << '-'
                << request.output_base_channel + 1
                << ", types=" << static_cast<long>(backend->format_.sample_types[0])
                << '/' << static_cast<long>(backend->format_.sample_types[1])
                << ", outputReady="
                << (backend->output_ready_supported_ ? "yes" : "no");
        }
        catch (...)
        {
            StartupFatal(StartupOperation::log_format);
        }

        return std::unique_ptr<AsioOutputBackend>{backend};
    }

    AsioOutputBackend::~AsioOutputBackend()
    {
        const ASIOError stop_result = driver_->Stop();
        if (stop_result != ASE_OK)
        {
            FailAudioContract(
                AudioContractFatalReason::AsioShutdownFailure,
                static_cast<std::uint64_t>(ShutdownOperation::stop),
                Bits(stop_result));
        }

        const ASIOError dispose_result = driver_->DisposeBuffers();
        if (dispose_result != ASE_OK)
        {
            FailAudioContract(
                AudioContractFatalReason::AsioShutdownFailure,
                static_cast<std::uint64_t>(ShutdownOperation::dispose_buffers),
                Bits(dispose_result));
        }

        const ASIOError exit_result = driver_->Exit();
        if (exit_result != ASE_OK)
        {
            FailAudioContract(
                AudioContractFatalReason::AsioShutdownFailure,
                static_cast<std::uint64_t>(ShutdownOperation::exit),
                Bits(exit_result));
        }

        AsioOutputBackend* expected_target = this;
        if (!callback_target_.compare_exchange_strong(
            expected_target,
            nullptr,
            std::memory_order_release,
            std::memory_order_relaxed))
        {
            FailAudioContract(
                AudioContractFatalReason::AsioOwnershipFailure,
                static_cast<std::uint64_t>(ShutdownOperation::clear_callback_target),
                reinterpret_cast<std::uintptr_t>(expected_target));
        }
    }

    std::unique_ptr<MixerVoice> AsioOutputBackend::CreateVoice(
        const NormalizedSourceFormat& format,
        std::shared_ptr<AudioSnapshot> snapshot,
        std::shared_ptr<AudioCursorTimeline>,
        const VoiceUsage usage,
        ma_result* const result) noexcept
    {
        return render_core_->CreateVoice(
            format,
            std::move(snapshot),
            nullptr,
            usage,
            result);
    }

    std::optional<std::uint64_t> AsioOutputBackend::CurrentOutputFrame() noexcept
    {
        return std::nullopt;
    }

    std::uint32_t AsioOutputBackend::endpoint_buffer_frames() const noexcept
    {
        return format_.frame_count;
    }

    std::uint32_t AsioOutputBackend::output_sample_rate() const noexcept
    {
        return format_.sample_rate;
    }

    void AsioOutputBackend::CountPendingCursorQuery() noexcept
    {
    }

    void AsioOutputBackend::CountUnmappedCursorFailure() noexcept
    {
    }

    // Rendering advances decoder and mixer state owned by the core.
    // ReSharper disable once CppMemberFunctionMayBeConst
    std::span<const float> AsioOutputBackend::RenderPcm(
        const std::uint32_t frame_count) noexcept
    {
        if (render_core_ == nullptr)
        {
            CallbackFatal(CallbackOperation::render_failure);
        }
        const AudioRenderBlock block = render_core_->RenderSequential(frame_count);
        if (block.mixer_result != MA_SUCCESS ||
            block.silence_reason == AudioRenderSilenceReason::mixer_error ||
            block.silence_reason == AudioRenderSilenceReason::render_contract_error)
        {
            CallbackFatal(
                CallbackOperation::render_failure,
                Bits(block.mixer_result),
                static_cast<std::uint64_t>(block.silence_reason));
        }
        if (block.frames_read != frame_count ||
            block.silence_reason == AudioRenderSilenceReason::active_short_read)
        {
            CallbackFatal(
                CallbackOperation::render_short_read,
                block.frames_read,
                frame_count);
        }
        const auto expected_samples =
            static_cast<std::size_t>(frame_count) * 2;
        if (block.interleaved_stereo.size() != expected_samples)
        {
            CallbackFatal(
                CallbackOperation::render_shape,
                block.interleaved_stereo.size(),
                expected_samples);
        }
        for (const float sample : block.interleaved_stereo)
        {
            if (!std::isfinite(sample))
            {
                CallbackFatal(CallbackOperation::non_finite_pcm);
            }
        }
        return block.interleaved_stereo;
    }

    void AsioOutputBackend::FillBuffer(const long buffer_index) noexcept
    {
        if (buffer_index < 0 || buffer_index > 1)
        {
            CallbackFatal(
                CallbackOperation::invalid_buffer_index,
                Bits(buffer_index));
        }

        const auto pcm = RenderPcm(format_.frame_count);
        const std::array<std::span<std::byte>, 2> destinations{
            std::span<std::byte>{conversion_storage_[0]},
            std::span<std::byte>{conversion_storage_[1]},
        };
        const auto converted = ConvertFloatStereoToAsio(
            pcm,
            format_.sample_types,
            destinations);
        if (!converted.converted || converted.stats.non_finite)
        {
            CallbackFatal(
                CallbackOperation::conversion_failure,
                converted.stats.non_finite ? 1 : 0);
        }

        for (std::size_t channel{}; channel < buffers_.size(); ++channel)
        {
            void* const driver_buffer =
                buffers_[channel].buffers[static_cast<std::size_t>(buffer_index)];
            if (driver_buffer == nullptr)
            {
                CallbackFatal(
                    CallbackOperation::invalid_driver_buffer,
                    channel,
                    static_cast<std::uint64_t>(buffer_index));
            }
            std::memcpy(
                driver_buffer,
                conversion_storage_[channel].data(),
                conversion_storage_[channel].size());
        }

        if (output_ready_supported_)
        {
            const ASIOError ready_result = driver_->OutputReady();
            if (ready_result != ASE_OK)
            {
                CallbackFatal(
                    CallbackOperation::output_ready,
                    Bits(ready_result));
            }
        }
    }

    AsioOutputBackend* AsioOutputBackend::CallbackTarget() noexcept
    {
        auto* const target = callback_target_.load(std::memory_order_acquire);
        if (target == nullptr)
        {
            CallbackFatal(CallbackOperation::missing_target);
        }
        return target;
    }

    void AsioOutputBackend::BufferSwitch(
        const long buffer_index,
        ASIOBool) noexcept
    {
        if (callback_active_.test_and_set(std::memory_order_acquire))
        {
            CallbackFatal(CallbackOperation::concurrent_callback);
        }
        CallbackTarget()->FillBuffer(buffer_index);
        callback_active_.clear(std::memory_order_release);
    }

    void AsioOutputBackend::SampleRateDidChange(
        const ASIOSampleRate sample_rate) noexcept
    {
        auto* const target = CallbackTarget();
        FailAudioContract(
            AudioContractFatalReason::AsioRuntimeNotification,
            static_cast<std::uint64_t>(RuntimeNotification::sample_rate_changed),
            target->format_.sample_rate,
            std::bit_cast<std::uint64_t>(sample_rate));
    }

    long AsioOutputBackend::AsioMessage(
        const long selector,
        const long value,
        void*,
        double*) noexcept
    {
        (void)CallbackTarget();

        if (selector == kAsioSelectorSupported)
        {
            switch (value)
            {
            case kAsioEngineVersion:
            case kAsioResetRequest:
            case kAsioResyncRequest:
            case kAsioLatenciesChanged:
            case kAsioOverload:
            case kAsioSupportsTimeInfo:
            case kAsioSupportsTimeCode:
                return 1;
            default:
                return 0;
            }
        }

        switch (selector)
        {
        case kAsioEngineVersion:
            return 2;
        case kAsioSupportsTimeInfo:
            return 1;
        case kAsioSupportsTimeCode:
            return 0;
        case kAsioResetRequest:
            FailAudioContract(
                AudioContractFatalReason::AsioRuntimeNotification,
                static_cast<std::uint64_t>(RuntimeNotification::reset));
        case kAsioBufferSizeChange:
            FailAudioContract(
                AudioContractFatalReason::AsioRuntimeNotification,
                static_cast<std::uint64_t>(RuntimeNotification::buffer_size_changed),
                Bits(value));
        case kAsioResyncRequest:
            FailAudioContract(
                AudioContractFatalReason::AsioRuntimeNotification,
                static_cast<std::uint64_t>(RuntimeNotification::resync));
        case kAsioLatenciesChanged:
            FailAudioContract(
                AudioContractFatalReason::AsioRuntimeNotification,
                static_cast<std::uint64_t>(RuntimeNotification::latencies_changed));
        case kAsioOverload:
            FailAudioContract(
                AudioContractFatalReason::AsioRuntimeNotification,
                static_cast<std::uint64_t>(RuntimeNotification::overload));
        default:
            return 0;
        }
    }

    // The ASIO SDK fixes this callback parameter to mutable ASIOTime*.
    // ReSharper disable once CppParameterMayBeConstPtrOrRef
    ASIOTime* AsioOutputBackend::BufferSwitchTimeInfo(
        ASIOTime* time_info,
        const long buffer_index,
        const ASIOBool direct_process) noexcept
    {
        static_cast<void>(direct_process);
        if (callback_active_.test_and_set(std::memory_order_acquire))
        {
            CallbackFatal(CallbackOperation::concurrent_callback);
        }
        auto* const target = CallbackTarget();
        if (time_info == nullptr)
        {
            CallbackFatal(CallbackOperation::invalid_time_info);
        }

        const auto flags = time_info->timeInfo.flags;
        if ((flags & (kSampleRateChanged | kClockSourceChanged)) != 0)
        {
            CallbackFatal(
                CallbackOperation::changed_time_info,
                flags);
        }
        if ((flags & kSampleRateValid) != 0 &&
            (!std::isfinite(time_info->timeInfo.sampleRate) ||
                time_info->timeInfo.sampleRate !=
                static_cast<double>(target->format_.sample_rate)))
        {
            CallbackFatal(
                CallbackOperation::invalid_sample_rate,
                std::bit_cast<std::uint64_t>(time_info->timeInfo.sampleRate),
                target->format_.sample_rate);
        }
        if ((flags & kSpeedValid) != 0 &&
            (!std::isfinite(time_info->timeInfo.speed) ||
                time_info->timeInfo.speed != 1.0))
        {
            CallbackFatal(
                CallbackOperation::invalid_speed,
                std::bit_cast<std::uint64_t>(time_info->timeInfo.speed));
        }

        target->FillBuffer(buffer_index);
        callback_active_.clear(std::memory_order_release);
        return nullptr;
    }
} // namespace gc::audio

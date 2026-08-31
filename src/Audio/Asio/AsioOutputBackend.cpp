// SPDX-License-Identifier: CC0-1.0

#include "Audio/Asio/AsioOutputBackend.h"

#include "Audio/Asio/AsioBufferRules.h"
#include "Audio/Asio/AsioSampleConverter.h"
#include "Audio/AudioContractFatal.h"
#include "Audio/SupportedOutputSampleRate.h"

#include <plog/Log.h>

#include <array>
#include <atomic>
#include <bit>
// ReSharper disable once CppUnusedIncludeDirective
#include <cmath>
#include <cstddef>
#include <cstdint>
// ReSharper disable once CppUnusedIncludeDirective
#include <cstring>
#include <iomanip>
#include <limits>
#include <memory>
#include <new>
#include <span>
#include <utility>
#include <vector>

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
            create_startup_event,
            create_shutdown_event,
            start_owner_thread,
            wait_for_startup,
            initialize_com,
            signal_startup,
            validate_service_view,
            allocate_live_session,
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
            signal_shutdown,
            join_owner,
            close_startup_event,
            close_shutdown_event,
        };

        enum class OwnershipOperation : std::uint64_t
        {
            message_wait = 1,
            unexpected_wait_result,
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

        [[noreturn]] void ShutdownFatal(
            const ShutdownOperation operation,
            const std::uint64_t operand1 = 0) noexcept
        {
            FailAudioContract(
                AudioContractFatalReason::AsioShutdownFailure,
                static_cast<std::uint64_t>(operation),
                operand1);
        }

        [[noreturn]] void OwnershipFatal(
            const OwnershipOperation operation,
            const std::uint64_t operand1 = 0) noexcept
        {
            FailAudioContract(
                AudioContractFatalReason::AsioOwnershipFailure,
                static_cast<std::uint64_t>(operation),
                operand1);
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

        struct FrozenFormat final
        {
            std::uint32_t sample_rate{};
            std::uint32_t frame_count{};
            std::array<std::uint32_t, 2> channels{};
            std::array<ASIOSampleType, 2> sample_types{};
        };

        struct LiveAsioSession final
        {
            [[nodiscard]] std::span<const float> RenderPcm(
                std::uint32_t frame_count) noexcept;
            void FillBuffer(long buffer_index) noexcept;

            std::unique_ptr<IAsioDriver> driver;
            std::array<ASIOBufferInfo, 2> buffers{};
            FrozenFormat format{};
            std::unique_ptr<AudioRenderCore> render_core;
            std::array<std::vector<std::byte>, 2> conversion_storage;
            bool output_ready_supported{};
        };

        std::atomic<LiveAsioSession*> callback_target{};
        std::atomic_flag callback_active = ATOMIC_FLAG_INIT;
        std::atomic_uint32_t callback_mode_mask{};
        std::atomic_uint32_t callback_count{};
        std::atomic_uint32_t long_buffer_diagnostic_count{};

        constexpr std::uint32_t kDirectProcessTrue = 1U << 0U;
        constexpr std::uint32_t kDirectProcessFalse = 1U << 1U;
        constexpr std::uint32_t kDirectProcessInvalid = 1U << 2U;
        constexpr std::uint32_t kLegacyCallback = 1U << 3U;
        constexpr std::uint32_t kTimeInfoCallback = 1U << 4U;

        void RecordCallbackMode(
            const ASIOBool direct_process,
            const std::uint32_t callback_form) noexcept
        {
            const auto direct_process_bit =
                direct_process == ASIOTrue
                    ? kDirectProcessTrue
                    : direct_process == ASIOFalse
                    ? kDirectProcessFalse
                    : kDirectProcessInvalid;
            callback_mode_mask.fetch_or(
                direct_process_bit | callback_form,
                std::memory_order_relaxed);
            callback_count.fetch_add(1, std::memory_order_relaxed);
        }

        [[nodiscard]] LiveAsioSession* CallbackTarget() noexcept
        {
            auto* const target =
                callback_target.load(std::memory_order_acquire);
            if (target == nullptr)
            {
                CallbackFatal(CallbackOperation::missing_target);
            }
            return target;
        }

        void BufferSwitch(
            long buffer_index,
            ASIOBool direct_process) noexcept;
        void SampleRateDidChange(ASIOSampleRate sample_rate) noexcept;
        long AsioMessage(
            long selector,
            long value,
            void* message,
            double* optional) noexcept;
        ASIOTime* BufferSwitchTimeInfo(
            ASIOTime* time_info,
            long buffer_index,
            ASIOBool direct_process) noexcept;

        constexpr ASIOCallbacks callbacks{
            .bufferSwitch = &BufferSwitch,
            .sampleRateDidChange = &SampleRateDidChange,
            .asioMessage = &AsioMessage,
            .bufferSwitchTimeInfo = &BufferSwitchTimeInfo,
        };

        // Rendering advances decoder and mixer state owned by the core.
        // ReSharper disable once CppMemberFunctionMayBeConst
        // NOLINTNEXTLINE(readability-make-member-function-const)
        std::span<const float> LiveAsioSession::RenderPcm(
            const std::uint32_t frame_count) noexcept
        {
            if (render_core == nullptr)
            {
                CallbackFatal(CallbackOperation::render_failure);
            }
            const AudioRenderBlock block =
                render_core->RenderSequential(frame_count);
            if (block.mixer_result != MA_SUCCESS ||
                block.silence_reason == AudioRenderSilenceReason::mixer_error ||
                block.silence_reason ==
                AudioRenderSilenceReason::render_contract_error)
            {
                CallbackFatal(
                    CallbackOperation::render_failure,
                    Bits(block.mixer_result),
                    static_cast<std::uint64_t>(block.silence_reason));
            }
            if (block.frames_read != frame_count ||
                block.silence_reason ==
                AudioRenderSilenceReason::active_short_read)
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

        void LiveAsioSession::FillBuffer(const long buffer_index) noexcept
        {
            if (buffer_index < 0 || buffer_index > 1)
            {
                CallbackFatal(
                    CallbackOperation::invalid_buffer_index,
                    Bits(buffer_index));
            }

            const auto pcm = RenderPcm(format.frame_count);
            const std::array<std::span<std::byte>, 2> destinations{
                std::span<std::byte>{conversion_storage[0]},
                std::span<std::byte>{conversion_storage[1]},
            };
            const auto converted = ConvertFloatStereoToAsio(
                pcm,
                format.sample_types,
                destinations);
            if (!converted.converted || converted.stats.non_finite)
            {
                CallbackFatal(
                    CallbackOperation::conversion_failure,
                    converted.stats.non_finite ? 1 : 0);
            }

            for (std::size_t channel{}; channel < buffers.size(); ++channel)
            {
                void* const driver_buffer =
                    buffers[channel].buffers[
                        static_cast<std::size_t>(buffer_index)];
                if (driver_buffer == nullptr)
                {
                    CallbackFatal(
                        CallbackOperation::invalid_driver_buffer,
                        channel,
                        static_cast<std::uint64_t>(buffer_index));
                }
                std::memcpy(
                    driver_buffer,
                    conversion_storage[channel].data(),
                    conversion_storage[channel].size());
            }

            if (output_ready_supported)
            {
                const ASIOError ready_result = driver->OutputReady();
                if (ready_result != ASE_OK)
                {
                    CallbackFatal(
                        CallbackOperation::output_ready,
                        Bits(ready_result));
                }
            }
        }

        void BufferSwitch(
            const long buffer_index,
            const ASIOBool direct_process) noexcept
        {
            RecordCallbackMode(direct_process, kLegacyCallback);
            auto* const target = CallbackTarget();
            if (callback_active.test_and_set(std::memory_order_acquire))
            {
                CallbackFatal(CallbackOperation::concurrent_callback);
            }
            target->FillBuffer(buffer_index);
            callback_active.clear(std::memory_order_release);
        }

        void SampleRateDidChange(
            const ASIOSampleRate sample_rate) noexcept
        {
            FailAudioContract(
                AudioContractFatalReason::AsioRuntimeNotification,
                static_cast<std::uint64_t>(
                    RuntimeNotification::sample_rate_changed),
                std::bit_cast<std::uint64_t>(sample_rate));
        }

        long AsioMessage(
            const long selector,
            const long value,
            void*,
            double*) noexcept
        {
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
                    static_cast<std::uint64_t>(
                        RuntimeNotification::reset));
            case kAsioBufferSizeChange:
                FailAudioContract(
                    AudioContractFatalReason::AsioRuntimeNotification,
                    static_cast<std::uint64_t>(
                        RuntimeNotification::buffer_size_changed),
                    Bits(value));
            case kAsioResyncRequest:
                FailAudioContract(
                    AudioContractFatalReason::AsioRuntimeNotification,
                    static_cast<std::uint64_t>(
                        RuntimeNotification::resync));
            case kAsioLatenciesChanged:
                FailAudioContract(
                    AudioContractFatalReason::AsioRuntimeNotification,
                    static_cast<std::uint64_t>(
                        RuntimeNotification::latencies_changed));
            case kAsioOverload:
                FailAudioContract(
                    AudioContractFatalReason::AsioRuntimeNotification,
                    static_cast<std::uint64_t>(
                        RuntimeNotification::overload));
            default:
                return 0;
            }
        }

        // The ASIO SDK fixes this callback parameter to mutable ASIOTime*.
        // ReSharper disable once CppParameterMayBeConstPtrOrRef
        ASIOTime* BufferSwitchTimeInfo(
            ASIOTime* time_info,
            const long buffer_index,
            const ASIOBool direct_process) noexcept
        {
            RecordCallbackMode(direct_process, kTimeInfoCallback);
            auto* const target = CallbackTarget();
            if (callback_active.test_and_set(std::memory_order_acquire))
            {
                CallbackFatal(CallbackOperation::concurrent_callback);
            }
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
                    static_cast<double>(target->format.sample_rate)))
            {
                CallbackFatal(
                    CallbackOperation::invalid_sample_rate,
                    std::bit_cast<std::uint64_t>(
                        time_info->timeInfo.sampleRate),
                    target->format.sample_rate);
            }
            if ((flags & kSpeedValid) != 0 &&
                (!std::isfinite(time_info->timeInfo.speed) ||
                    time_info->timeInfo.speed != 1.0))
            {
                CallbackFatal(
                    CallbackOperation::invalid_speed,
                    std::bit_cast<std::uint64_t>(
                        time_info->timeInfo.speed));
            }

            target->FillBuffer(buffer_index);
            callback_active.clear(std::memory_order_release);
            return nullptr;
        }
    } // namespace

    std::unique_ptr<AsioOutputBackend> AsioOutputBackend::Start(
        HWND game_window,
        const AsioStreamRequest& request,
        std::unique_ptr<IAsioRegistrySource> registry,
        std::unique_ptr<IAsioDriverFactory> driver_factory,
        std::shared_ptr<const ma_allocation_callbacks>
        allocation_callbacks) noexcept
    {
        auto* const backend = new(std::nothrow) AsioOutputBackend;
        if (backend == nullptr)
        {
            StartupFatal(StartupOperation::allocate_backend);
        }
        if (registry == nullptr)
        {
            StartupFatal(StartupOperation::resolve_driver);
        }
        if (driver_factory == nullptr)
        {
            StartupFatal(StartupOperation::create_driver);
        }

        backend->startup_complete_ =
            CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (backend->startup_complete_ == nullptr)
        {
            StartupFatal(
                StartupOperation::create_startup_event,
                GetLastError());
        }

        backend->shutdown_requested_ =
            CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (backend->shutdown_requested_ == nullptr)
        {
            StartupFatal(
                StartupOperation::create_shutdown_event,
                GetLastError());
        }

        try
        {
            backend->owner_thread_ = std::thread{
                &AsioOutputBackend::OwnerThreadMain,
                game_window,
                request,
                std::move(registry),
                std::move(driver_factory),
                std::move(allocation_callbacks),
                backend->startup_complete_,
                backend->shutdown_requested_,
                &backend->services_,
            };
        }
        catch (...)
        {
            StartupFatal(StartupOperation::start_owner_thread);
        }

        const DWORD wait_result =
            WaitForSingleObject(backend->startup_complete_, INFINITE);
        if (wait_result != WAIT_OBJECT_0)
        {
            const DWORD error =
                wait_result == WAIT_FAILED ? GetLastError() : ERROR_SUCCESS;
            StartupFatal(
                StartupOperation::wait_for_startup,
                wait_result,
                error);
        }
        if (backend->services_.render_core == nullptr ||
            backend->services_.endpoint_buffer_frames == 0 ||
            backend->services_.output_sample_rate == 0)
        {
            StartupFatal(StartupOperation::validate_service_view);
        }

        return std::unique_ptr<AsioOutputBackend>{backend};
    }

    AsioOutputBackend::~AsioOutputBackend()
    {
        if (SetEvent(shutdown_requested_) == FALSE)
        {
            ShutdownFatal(
                ShutdownOperation::signal_shutdown,
                GetLastError());
        }
        if (!owner_thread_.joinable())
        {
            ShutdownFatal(ShutdownOperation::join_owner);
        }
        try
        {
            owner_thread_.join();
        }
        catch (...)
        {
            ShutdownFatal(ShutdownOperation::join_owner);
        }

        if (CloseHandle(startup_complete_) == FALSE)
        {
            ShutdownFatal(
                ShutdownOperation::close_startup_event,
                GetLastError());
        }
        if (CloseHandle(shutdown_requested_) == FALSE)
        {
            ShutdownFatal(
                ShutdownOperation::close_shutdown_event,
                GetLastError());
        }
    }

    std::unique_ptr<MixerVoice> AsioOutputBackend::CreateVoice(
        const NormalizedSourceFormat& format,
        std::shared_ptr<AudioSnapshot> snapshot,
        std::shared_ptr<AudioCursorTimeline>,
        const VoiceUsage usage,
        ma_result* const result) noexcept
    {
        const auto source_bytes = snapshot != nullptr
                                      ? snapshot->byte_length()
                                      : 0;
        auto voice = services_.render_core->CreateVoice(
            format,
            std::move(snapshot),
            nullptr,
            usage,
            result);
        const auto source_frames = format.block_align == 0
                                       ? 0
                                       : source_bytes / format.block_align;
        const auto long_buffer_threshold =
            static_cast<std::uint64_t>(format.sample_rate) * 5U;
        if (voice != nullptr && format.sample_rate != 0 &&
            source_frames >= long_buffer_threshold)
        {
            const auto diagnostic_index =
                long_buffer_diagnostic_count.fetch_add(
                    1, std::memory_order_relaxed);
            if (diagnostic_index < 16)
            {
                try
                {
                    PLOG_INFO
                        << "ASIO long-buffer diagnostic: index="
                        << diagnostic_index
                        << ", sourceFrames=" << source_frames
                        << ", sourceRate=" << format.sample_rate
                        << ", channels=" << format.channels
                        << ", bits=" << format.bits_per_sample
                        << ", usage=" << static_cast<unsigned>(usage)
                        << ", callbackCount="
                        << callback_count.load(std::memory_order_relaxed)
                        << ", callbackModeMask=0x" << std::hex
                        << callback_mode_mask.load(std::memory_order_relaxed)
                        << std::dec;
                }
                catch (...)
                {
                }
            }
        }
        return voice;
    }

    std::optional<std::uint64_t>
    AsioOutputBackend::CurrentOutputFrame() noexcept
    {
        return std::nullopt;
    }

    std::uint32_t AsioOutputBackend::endpoint_buffer_frames() const noexcept
    {
        return services_.endpoint_buffer_frames;
    }

    std::uint32_t AsioOutputBackend::output_sample_rate() const noexcept
    {
        return services_.output_sample_rate;
    }

    void AsioOutputBackend::CountPendingCursorQuery() noexcept
    {
    }

    void AsioOutputBackend::CountUnmappedCursorFailure() noexcept
    {
    }

    void AsioOutputBackend::OwnerThreadMain(
        HWND game_window,
        // The owner entry intentionally takes permanent ownership of this copy.
        AsioStreamRequest request, // NOLINT(performance-unnecessary-value-param)
        std::unique_ptr<IAsioRegistrySource> registry,
        std::unique_ptr<IAsioDriverFactory> driver_factory,
        std::shared_ptr<const ma_allocation_callbacks> allocation_callbacks,
        HANDLE startup_complete,
        HANDLE shutdown_requested,
        PublishedServices* published_services) noexcept
    {
        const HRESULT com_result = CoInitializeEx(
            nullptr,
            COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        if (com_result != S_OK && com_result != S_FALSE)
        {
            StartupFatal(
                StartupOperation::initialize_com,
                Bits(com_result));
        }

        MSG queued_message{};
        static_cast<void>(PeekMessageW(
            &queued_message,
            nullptr,
            0,
            0,
            PM_NOREMOVE));

        auto* const raw_session = new(std::nothrow) LiveAsioSession;
        if (raw_session == nullptr)
        {
            StartupFatal(StartupOperation::allocate_live_session);
        }
        std::unique_ptr<LiveAsioSession> session{raw_session};

        auto registration =
            ResolveAsioDriver(*registry, request.driver_name);
        if (!registration)
        {
            StartupFatal(
                StartupOperation::resolve_driver,
                registration.error());
        }

        auto created = driver_factory->Create(registration->clsid);
        if (!created)
        {
            StartupFatal(
                StartupOperation::create_driver,
                created.error());
        }
        session->driver = std::move(*created);
        registry.reset();
        driver_factory.reset();

        if (session->driver->Init(game_window) != ASIOTrue)
        {
            StartupFatal(StartupOperation::initialize_driver);
        }

        char reported_name[32]{};
        session->driver->GetDriverName(reported_name);
        const long driver_version =
            session->driver->GetDriverVersion();

        ASIOSampleRate reported_rate{};
        const ASIOError rate_result =
            session->driver->GetSampleRate(&reported_rate);
        if (rate_result != ASE_OK)
        {
            StartupFatal(
                StartupOperation::query_sample_rate,
                Bits(rate_result));
        }
        if (!std::isfinite(reported_rate) || reported_rate <= 0.0 ||
            std::trunc(reported_rate) != reported_rate ||
            reported_rate >
            static_cast<double>(
                (std::numeric_limits<std::uint32_t>::max)()))
        {
            StartupFatal(StartupOperation::validate_sample_rate);
        }
        const auto sample_rate =
            static_cast<std::uint32_t>(reported_rate);
        if (!IsSupportedOutputSampleRate(sample_rate))
        {
            StartupFatal(
                StartupOperation::validate_sample_rate,
                sample_rate);
        }

        AsioBufferLimits limits{};
        const ASIOError buffer_result =
            session->driver->GetBufferSize(
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
                static_cast<std::uint64_t>(
                    buffer_validation.error()),
                request.buffer_frames);
        }

        long input_channels{};
        long output_channels{};
        const ASIOError channels_result =
            session->driver->GetChannels(
                &input_channels,
                &output_channels);
        if (channels_result != ASE_OK)
        {
            StartupFatal(
                StartupOperation::query_channels,
                Bits(channels_result));
        }
        const std::uint64_t selected_last =
            static_cast<std::uint64_t>(
                request.output_base_channel) + 1;
        if (input_channels < 0 || output_channels < 2 ||
            selected_last >=
            static_cast<std::uint64_t>(output_channels) ||
            selected_last >
            static_cast<std::uint64_t>(
                (std::numeric_limits<long>::max)()))
        {
            StartupFatal(
                StartupOperation::validate_channel_pair,
                request.output_base_channel,
                Bits(output_channels));
        }

        session->format.sample_rate = sample_rate;
        session->format.frame_count = request.buffer_frames;
        session->format.channels = {
            request.output_base_channel,
            request.output_base_channel + 1,
        };
        for (std::size_t channel{};
             channel < session->buffers.size();
             ++channel)
        {
            session->buffers[channel] = {
                .isInput = ASIOFalse,
                .channelNum = static_cast<long>(
                    session->format.channels[channel]),
                .buffers = {},
            };
        }

        if (auto* const existing_target =
                callback_target.load(std::memory_order_acquire);
            existing_target != nullptr)
        {
            StartupFatal(
                StartupOperation::publish_callback_target,
                reinterpret_cast<std::uintptr_t>(existing_target));
        }

        const ASIOError create_result =
            session->driver->CreateBuffers(
                session->buffers.data(),
                static_cast<long>(session->buffers.size()),
                static_cast<long>(request.buffer_frames),
                &callbacks);
        if (create_result != ASE_OK)
        {
            StartupFatal(
                StartupOperation::create_buffers,
                Bits(create_result));
        }

        for (std::size_t channel{};
             channel < session->buffers.size();
             ++channel)
        {
            ASIOChannelInfo info{
                .channel = static_cast<long>(
                    session->format.channels[channel]),
                .isInput = ASIOFalse,
            };
            const ASIOError info_result =
                session->driver->GetChannelInfo(&info);
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
            session->format.sample_types[channel] = info.type;
        }

        ma_result render_result = MA_ERROR;
        session->render_core = AudioRenderCore::Create(
            request.buffer_frames,
            sample_rate,
            std::move(allocation_callbacks),
            nullptr,
            &render_result);
        if (session->render_core == nullptr ||
            render_result != MA_SUCCESS)
        {
            StartupFatal(
                StartupOperation::create_render_core,
                Bits(render_result));
        }

        try
        {
            for (std::size_t channel{};
                 channel < session->conversion_storage.size();
                 ++channel)
            {
                session->conversion_storage[channel].resize(
                    ConversionBytesOrFatal(
                        request.buffer_frames,
                        session->format.sample_types[channel]));
            }
        }
        catch (...)
        {
            StartupFatal(
                StartupOperation::allocate_conversion_storage);
        }

        for (std::size_t channel{};
             channel < session->buffers.size();
             ++channel)
        {
            const auto byte_count =
                session->conversion_storage[channel].size();
            for (std::size_t half{}; half < 2; ++half)
            {
                void* const raw_buffer =
                    session->buffers[channel].buffers[half];
                if (raw_buffer == nullptr)
                {
                    StartupFatal(
                        StartupOperation::validate_driver_buffer,
                        channel,
                        half);
                }
                if (!ClearAsioChannel(
                    session->format.sample_types[channel],
                    {
                        static_cast<std::byte*>(raw_buffer),
                        byte_count,
                    },
                    request.buffer_frames))
                {
                    StartupFatal(
                        StartupOperation::clear_driver_buffer,
                        channel,
                        half);
                }
            }
        }

        const ASIOError ready_result =
            session->driver->OutputReady();
        if (ready_result == ASE_OK)
        {
            session->output_ready_supported = true;
        }
        else if (ready_result == ASE_NotPresent)
        {
            session->output_ready_supported = false;
        }
        else
        {
            StartupFatal(
                StartupOperation::probe_output_ready,
                Bits(ready_result));
        }

        LiveAsioSession* expected_target{};
        callback_mode_mask.store(0, std::memory_order_relaxed);
        callback_count.store(0, std::memory_order_relaxed);
        long_buffer_diagnostic_count.store(0, std::memory_order_relaxed);
        if (!callback_target.compare_exchange_strong(
            expected_target,
            session.get(),
            std::memory_order_release,
            std::memory_order_relaxed))
        {
            StartupFatal(
                StartupOperation::publish_callback_target,
                reinterpret_cast<std::uintptr_t>(expected_target));
        }

        const ASIOError start_result = session->driver->Start();
        if (start_result != ASE_OK)
        {
            StartupFatal(
                StartupOperation::start_driver,
                Bits(start_result));
        }

        try
        {
            PLOG_INFO
                << "ASIO session started: configured='"
                << request.driver_name << "', reported='"
                << AsioDisplayTextToUtf8(std::span{reported_name})
                << "', version=" << driver_version
                << ", rate=" << sample_rate
                << ", frames=" << request.buffer_frames
                << ", outputs=" << request.output_base_channel << '-'
                << request.output_base_channel + 1
                << ", types="
                << session->format.sample_types[0]
                << '/'
                << session->format.sample_types[1]
                << ", outputReady="
                << (session->output_ready_supported ? "yes" : "no");
        }
        catch (...)
        {
            StartupFatal(StartupOperation::log_format);
        }

        if (published_services == nullptr)
        {
            StartupFatal(
                StartupOperation::validate_service_view);
        }
        *published_services = {
            .render_core = session->render_core.get(),
            .endpoint_buffer_frames = request.buffer_frames,
            .output_sample_rate = sample_rate,
        };
        if (SetEvent(startup_complete) == FALSE)
        {
            StartupFatal(
                StartupOperation::signal_startup,
                GetLastError());
        }
        published_services = nullptr;

        for (;;)
        {
            const HANDLE handles[]{shutdown_requested};
            const DWORD wait_result =
                MsgWaitForMultipleObjectsEx(
                    1,
                    handles,
                    INFINITE,
                    QS_ALLINPUT,
                    MWMO_INPUTAVAILABLE);
            if (wait_result == WAIT_OBJECT_0)
            {
                break;
            }
            if (wait_result == WAIT_OBJECT_0 + 1)
            {
                MSG message{};
                while (PeekMessageW(
                    &message,
                    nullptr,
                    0,
                    0,
                    PM_REMOVE) != FALSE)
                {
                    static_cast<void>(TranslateMessage(&message));
                    static_cast<void>(DispatchMessageW(&message));
                }
                continue;
            }
            if (wait_result == WAIT_FAILED)
            {
                OwnershipFatal(
                    OwnershipOperation::message_wait,
                    GetLastError());
            }
            OwnershipFatal(
                OwnershipOperation::unexpected_wait_result,
                wait_result);
        }

        const ASIOError stop_result = session->driver->Stop();
        if (stop_result != ASE_OK)
        {
            ShutdownFatal(
                ShutdownOperation::stop,
                Bits(stop_result));
        }

        const ASIOError dispose_result =
            session->driver->DisposeBuffers();
        if (dispose_result != ASE_OK)
        {
            ShutdownFatal(
                ShutdownOperation::dispose_buffers,
                Bits(dispose_result));
        }

        const ASIOError exit_result = session->driver->Exit();
        if (exit_result != ASE_OK)
        {
            ShutdownFatal(
                ShutdownOperation::exit,
                Bits(exit_result));
        }

        callback_target.store(nullptr, std::memory_order_release);
        session.reset();
        CoUninitialize();
    }
} // namespace gc::audio

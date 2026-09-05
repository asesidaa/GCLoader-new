#include "Audio/AudioPatch.h"

#include "Audio/AudioContractFatal.h"

#include "Audio/Asio/AsioDriver.h"
#include "Audio/Asio/AsioDriverCatalog.h"
#include "Audio/Asio/AsioOutputBackend.h"
#include "Audio/DirectSound/DirectSoundHook.h"
#include "Audio/Wasapi/ExclusiveAudioEngine.h"
#include "Audio/AudioPatchInternal.h"

#include "plog/Log.h"

#include <safetyhook.hpp>

#include <dsound.h>

#include <algorithm>
#include <array>
#include <atomic>
// ReSharper disable once CppUnusedIncludeDirective
#include <cstddef>
#include <cstdint>
// ReSharper disable once CppUnusedIncludeDirective
#include <cstring>
#include <cstdlib>
#include <format>
#include <iomanip>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace gc::audio
{
    namespace
    {
        safetyhook::MidHook g_asio_close_hook{};

        constexpr std::uintptr_t kSupportedImageBase = 0x00400000U;
        constexpr std::uintptr_t kAsioCloseRva = 0x0023C853U;
        constexpr std::array<unsigned char, 16> kAsioCloseBytes{
            0xFF, 0x15, 0x3C, 0xD6, 0x6A, 0x00, 0x8B, 0xE5,
            0x5D, 0xC3, 0xCC, 0xCC, 0xCC, 0x55, 0x8B, 0xEC,
        };

        bool ReadExecutableBytes(
            const void* source,
            unsigned char* destination,
            const std::size_t size) noexcept
        {
            if (source == nullptr || destination == nullptr || size == 0)
            {
                return false;
            }
            __try
            {
                std::memcpy(destination, source, size);
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
        }

        static_assert(std::is_nothrow_move_constructible_v<AudioStartupFailure>);
        static_assert(std::is_nothrow_move_assignable_v<AudioStartupFailure>);

        constexpr std::string_view kFailureMessage =
            "WASAPI exclusive low-latency audio failed.\n"
            "Restart the game after setting audio_backend = 'directsound'\n"
            "to restore the original DirectSound backend.";
        constexpr std::string_view kPacingFailureMessage =
            "WASAPI exclusive low-latency audio pacing failed.\n"
            "Please increase wasapi_exclusive_buffer_ms and restart the game.\n"
            "Set audio_backend = 'directsound' and restart to restore "
            "the original DirectSound backend.";
        constexpr std::string_view kGenericStartupFailureMessage =
            "Low-latency audio could not start.\n"
            "Correct the ASIO/WASAPI settings, or select DirectSound in ConfigGUI, "
            "then restart the game.";
        constexpr REFERENCE_TIME kReferenceTimePerMillisecond = 10'000;

        constexpr REFERENCE_TIME BufferMillisecondsToReferenceTime(
            std::uint32_t milliseconds) noexcept
        {
            return static_cast<REFERENCE_TIME>(milliseconds) *
                kReferenceTimePerMillisecond;
        }

        const char* audio_failure_stage_name(AudioFailureStage stage) noexcept
        {
            switch (stage)
            {
            case AudioFailureStage::None: return "None";
            case AudioFailureStage::InitializationTimeout: return "InitializationTimeout";
            case AudioFailureStage::InitializeMixer: return "InitializeMixer";
            case AudioFailureStage::CoInitialize: return "CoInitialize";
            case AudioFailureStage::OpenDefaultEndpoint: return "OpenDefaultEndpoint";
            case AudioFailureStage::ActivateAudioClient: return "ActivateAudioClient";
            case AudioFailureStage::IsFormatSupported: return "IsFormatSupported";
            case AudioFailureStage::InvalidConfiguredDuration: return "InvalidConfiguredDuration";
            case AudioFailureStage::GetDevicePeriod: return "GetDevicePeriod";
            case AudioFailureStage::ConfiguredDurationBelowMinimum: return "ConfiguredDurationBelowMinimum";
            case AudioFailureStage::InitializeExclusive: return "InitializeExclusive";
            case AudioFailureStage::GetAlignedBufferSize: return "GetAlignedBufferSize";
            case AudioFailureStage::ReactivateAudioClient: return "ReactivateAudioClient";
            case AudioFailureStage::RetryInitializeExclusive: return "RetryInitializeExclusive";
            case AudioFailureStage::GetActualBufferSize: return "GetActualBufferSize";
            case AudioFailureStage::CreateRenderEvent: return "CreateRenderEvent";
            case AudioFailureStage::SetEventHandle: return "SetEventHandle";
            case AudioFailureStage::GetRenderService: return "GetRenderService";
            case AudioFailureStage::GetClockService: return "GetClockService";
            case AudioFailureStage::GetClockFrequency: return "GetClockFrequency";
            case AudioFailureStage::PrefillGetBuffer: return "PrefillGetBuffer";
            case AudioFailureStage::PrefillReleaseBuffer: return "PrefillReleaseBuffer";
            case AudioFailureStage::RegisterMmcss: return "RegisterMmcss";
            case AudioFailureStage::SetMmcssPriority: return "SetMmcssPriority";
            case AudioFailureStage::StartEndpoint: return "StartEndpoint";
            case AudioFailureStage::WaitRenderEvent: return "WaitRenderEvent";
            case AudioFailureStage::GetRenderBuffer: return "GetRenderBuffer";
            case AudioFailureStage::ReleaseRenderBuffer: return "ReleaseRenderBuffer";
            case AudioFailureStage::GetClockPosition: return "GetClockPosition";
            case AudioFailureStage::InvalidClockPosition: return "InvalidClockPosition";
            case AudioFailureStage::ChronicOutputGap: return "ChronicOutputGap";
            }
            return "Unknown";
        }

        const char* mixer_render_failure_source_name(
            MixerRenderFailureSource source) noexcept
        {
            switch (source)
            {
            case MixerRenderFailureSource::None: return "none";
            case MixerRenderFailureSource::InvalidArguments:
                return "invalid_arguments";
            case MixerRenderFailureSource::ReentrantRender:
                return "reentrant_render";
            case MixerRenderFailureSource::EngineRead: return "engine_read";
            }
            return "unknown";
        }

        const char* mixer_exact_publication_stage_name(
            MixerExactPublicationStage stage) noexcept
        {
            switch (stage)
            {
            case MixerExactPublicationStage::None: return "none";
            case MixerExactPublicationStage::InvalidOutputSpan:
                return "invalid_output_span";
            case MixerExactPublicationStage::EpochAheadOfOutput:
                return "epoch_ahead_of_output";
            case MixerExactPublicationStage::EpochOffsetOverflow:
                return "epoch_offset_overflow";
            case MixerExactPublicationStage::SourceMappingFailed:
                return "source_mapping_failed";
            case MixerExactPublicationStage::TimelineRejected:
                return "timeline_rejected";
            case MixerExactPublicationStage::EpochAdvanceOverflow:
                return "epoch_advance_overflow";
            }
            return "unknown";
        }

        const char* exact_mapped_span_failure_name(
            ExactMappedSpanPublicationFailure failure) noexcept
        {
            switch (failure)
            {
            case ExactMappedSpanPublicationFailure::None: return "none";
            case ExactMappedSpanPublicationFailure::InvalidArguments:
                return "invalid_arguments";
            case ExactMappedSpanPublicationFailure::
            NaturalEndTailUnrepresentable:
                return "natural_end_tail_unrepresentable";
            case ExactMappedSpanPublicationFailure::EpochCounterOverflow:
                return "epoch_counter_overflow";
            case ExactMappedSpanPublicationFailure::
            PlaybackGenerationNotIncreasing:
                return "playback_generation_not_increasing";
            case ExactMappedSpanPublicationFailure::PreviousEpochUnavailable:
                return "previous_epoch_unavailable";
            case ExactMappedSpanPublicationFailure::PreviousEpochAlreadyClosed:
                return "previous_epoch_already_closed";
            case ExactMappedSpanPublicationFailure::
            PreviousEpochTailUnrepresentable:
                return "previous_epoch_tail_unrepresentable";
            case ExactMappedSpanPublicationFailure::CurrentEpochUnavailable:
                return "current_epoch_unavailable";
            case ExactMappedSpanPublicationFailure::CurrentEpochClosed:
                return "current_epoch_closed";
            case ExactMappedSpanPublicationFailure::BufferInstanceChanged:
                return "buffer_instance_changed";
            case ExactMappedSpanPublicationFailure::TimelineGenerationChanged:
                return "timeline_generation_changed";
            case ExactMappedSpanPublicationFailure::PlaybackGenerationChanged:
                return "playback_generation_changed";
            case ExactMappedSpanPublicationFailure::OriginChanged:
                return "origin_changed";
            case ExactMappedSpanPublicationFailure::OutputOriginChanged:
                return "output_origin_changed";
            case ExactMappedSpanPublicationFailure::SourceOriginChanged:
                return "source_origin_changed";
            case ExactMappedSpanPublicationFailure::OutputRateChanged:
                return "output_rate_changed";
            case ExactMappedSpanPublicationFailure::SourceRateChanged:
                return "source_rate_changed";
            case ExactMappedSpanPublicationFailure::
            MappedOutputTailNotIncreasing:
                return "mapped_output_tail_not_increasing";
            case ExactMappedSpanPublicationFailure::
            PublicationSequenceUnavailable:
                return "publication_sequence_unavailable";
            case ExactMappedSpanPublicationFailure::SlotStoreFailed:
                return "slot_store_failed";
            }
            return "unknown";
        }

        void append_mixer_diagnostics(
            std::ostringstream& stream,
            const MixerDiagnosticsSnapshot& mixer)
        {
            stream
                << " native_rate_buffers=" << mixer.native_rate_buffers
                << " sample_format_converted_buffers="
                << mixer.sample_format_converted_buffers
                << " sample_rate_converted_buffers="
                << mixer.sample_rate_converted_buffers
                << " native_gameplay_buffers=" << mixer.native_gameplay_buffers
                << " active_voices=" << mixer.active_voices
                << " maximum_simultaneous_voices="
                << mixer.maximum_simultaneous_voices
                << " first_mixer_failure_source="
                << mixer_render_failure_source_name(
                    mixer.first_render_failure_source)
                << " first_engine_read_error=" << mixer.first_engine_read_error
                << " first_exact_publication_stage="
                << mixer_exact_publication_stage_name(
                    mixer.first_exact_publication_stage)
                << " first_exact_timeline_failure="
                << exact_mapped_span_failure_name(
                    mixer.first_exact_timeline_failure)
                << " first_exact_timeline_expected="
                << mixer.first_exact_timeline_expected
                << " first_exact_timeline_actual="
                << mixer.first_exact_timeline_actual
                << " first_exact_buffer_instance_id="
                << mixer.first_exact_buffer_instance_id
                << " first_exact_playback_generation="
                << mixer.first_exact_playback_generation
                << " first_exact_output_begin=" << mixer.first_exact_output_begin
                << " first_exact_output_frames="
                << mixer.first_exact_output_frames
                << " first_exact_epoch_output_frames="
                << mixer.first_exact_epoch_output_frames
                << " first_exact_epoch_source_start="
                << mixer.first_exact_epoch_source_start
                << " first_exact_source_length_frames="
                << mixer.first_exact_source_length_frames
                << " first_exact_output_rate=" << mixer.first_exact_output_rate
                << " first_exact_source_rate=" << mixer.first_exact_source_rate;
        }

        std::string utf8(std::wstring_view value)
        {
            if (value.empty())
            {
                return {};
            }
            const int size = WideCharToMultiByte(
                CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                nullptr, 0, nullptr, nullptr);
            if (size <= 0)
            {
                return "<conversion-failed>";
            }
            std::string result(static_cast<std::size_t>(size), '\0');
            if (WideCharToMultiByte(
                CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                result.data(), size, nullptr, nullptr) != size)
            {
                return "<conversion-failed>";
            }
            return result;
        }

        std::string counters_text(const AudioRuntimeCountersSnapshot& counters)
        {
            std::ostringstream stream;
            stream
                << "render_callbacks=" << counters.render_callbacks
                << " late_event_wakes=" << counters.late_event_wakes
                << " silence_fallbacks=" << counters.silence_fallbacks
                << " pending_cursor_queries=" << counters.pending_cursor_queries
                << " unmapped_cursor_failures="
                << counters.unmapped_cursor_failures
                << " confirmed_gap_events=" << counters.confirmed_gap_events
                << " skipped_output_frames=" << counters.skipped_output_frames
                << " maximum_skipped_output_frames="
                << counters.maximum_skipped_output_frames
                << " chronic_pacing_failures=" << counters.chronic_pacing_failures
                << " current_submitted_lead_frames="
                << counters.current_submitted_lead_frames
                << " minimum_submitted_lead_frames="
                << counters.minimum_submitted_lead_frames
                << " endpoint_hresult_failures="
                << counters.endpoint_hresult_failures;
            append_mixer_diagnostics(stream, counters.mixer);
            return stream.str();
        }

        std::string hresult_hex(HRESULT result)
        {
            return std::format(
                "0x{:08X}", static_cast<std::uint32_t>(result));
        }

        const char* descriptor_name(EndpointFormatKind kind) noexcept
        {
            return kind == EndpointFormatKind::LegacyPcm
                       ? "legacy_pcm"
                       : "extensible_pcm";
        }

        std::string selected_format_text(
            const EndpointInitialization& initialization)
        {
            if (!initialization.has_selected_format ||
                !initialization.selected_format.valid())
            {
                return "format=<none> descriptor=<none> fallback_rate=false";
            }

            const auto& wave = initialization.selected_format.wave_format();
            return std::format(
                "format=pcm16/{}Hz/{}ch/{}bit descriptor={} fallback_rate={}",
                wave.nSamplesPerSec,
                wave.nChannels,
                wave.wBitsPerSample,
                descriptor_name(initialization.selected_format.kind),
                wave.nSamplesPerSec != kGamePrimarySampleRate);
        }

        std::string format_attempts_text(
            const EndpointInitialization& initialization)
        {
            const auto attempt_count = std::min<std::size_t>(
                initialization.format_attempt_count,
                initialization.format_attempts.size());
            std::string text = std::format(
                "format_attempt_count={} format_attempts=\"", attempt_count);
            for (std::size_t index = 0; index < attempt_count; ++index)
            {
                if (index != 0)
                {
                    text.push_back(',');
                }
                const auto& attempt = initialization.format_attempts[index];
                std::format_to(
                    std::back_inserter(text),
                    "{}/{}:{}",
                    attempt.format.wave_format().nSamplesPerSec,
                    descriptor_name(attempt.format.kind),
                    hresult_hex(attempt.result));
            }
            text.push_back('\"');
            return text;
        }

        std::string startup_text(
            const EndpointInitialization& initialization,
            AudioBackend requested_backend = AudioBackend::wasapi_exclusive)
        {
            const auto output_sample_rate = initialization.has_selected_format
                                                ? initialization.selected_format.wave_format().nSamplesPerSec
                                                : 0;
            const double actual_ms = output_sample_rate == 0
                                         ? 0.0
                                         : static_cast<double>(initialization.actual_buffer_frames) * 1000.0 /
                                         static_cast<double>(output_sample_rate);
            std::ostringstream stream;
            stream << std::fixed << std::setprecision(3)
                << "Audio startup requested_backend="
                << AudioBackendName(requested_backend)
                << " active_backend=wasapi_exclusive"
                << " endpoint_name=\"" << utf8(initialization.endpoint_name) << "\""
                << " endpoint_id=\"" << utf8(initialization.endpoint_id) << "\""
                << ' ' << selected_format_text(initialization)
                << ' ' << format_attempts_text(initialization)
                << " default_period_100ns=" << initialization.default_period
                << " default_period_ms="
                << static_cast<double>(initialization.default_period) / 10'000.0
                << " minimum_period_100ns=" << initialization.minimum_period
                << " minimum_period_ms="
                << static_cast<double>(initialization.minimum_period) / 10'000.0
                << " configured_duration_100ns="
                << initialization.configured_duration
                << " configured_duration_ms="
                << static_cast<double>(initialization.configured_duration) / 10'000.0
                << " requested_duration_100ns=" << initialization.requested_duration
                << " requested_duration_ms="
                << static_cast<double>(initialization.requested_duration) / 10'000.0;
            if (initialization.stream_latency_available)
            {
                stream
                    << " stream_latency_100ns=" << initialization.stream_latency
                    << " stream_latency_ms="
                    << static_cast<double>(initialization.stream_latency) / 10'000.0;
            }
            else
            {
                stream
                    << " stream_latency=unavailable"
                    << " stream_latency_hresult="
                    << hresult_hex(initialization.stream_latency_result);
            }
            stream
                << " actual_buffer_frames=" << initialization.actual_buffer_frames
                << " actual_buffer_ms=" << actual_ms
                << " exclusive_event_driven=true"
                << " alignment_retry="
                << (initialization.alignment_retry ? "true" : "false")
                << " mmcss_profile=\"Pro Audio\""
                << " mmcss_priority=\"Critical\""
                << " mixer_rate_hz=" << output_sample_rate
                << " mixer_channels=" << kOutputChannels
                << " wasapi_buffer_ms="
                << initialization.configured_duration / kReferenceTimePerMillisecond;
            return stream.str();
        }

        std::string failure_text(
            std::string_view kind,
            const EndpointInitialization& initialization,
            const AudioFailure& failure)
        {
            return std::format(
                "{} endpoint_id=\"{}\" stage={} hresult={} {} {} "
                "default_period_100ns={} minimum_period_100ns={} "
                "configured_duration_100ns={} requested_duration_100ns={} "
                "actual_buffer_frames={}",
                kind,
                initialization.endpoint_id.empty()
                    ? "<unknown>"
                    : utf8(initialization.endpoint_id),
                audio_failure_stage_name(failure.stage),
                hresult_hex(failure.result),
                selected_format_text(initialization),
                format_attempts_text(initialization),
                initialization.default_period,
                initialization.minimum_period,
                initialization.configured_duration,
                initialization.requested_duration,
                initialization.actual_buffer_frames);
        }

        std::string audio_config_text(
            AudioBackend requested_backend,
            std::uint32_t configured_buffer_ms)
        {
            const bool enabled =
                requested_backend != AudioBackend::directsound;
            return std::format(
                "Audio config requested_backend={} active_backend={} "
                "hook_requested={} enabled={} configured_buffer_ms={} "
                "configured_duration_100ns={}",
                AudioBackendName(requested_backend),
                enabled ? "pending" : "directsound",
                enabled,
                enabled,
                configured_buffer_ms,
                BufferMillisecondsToReferenceTime(configured_buffer_ms));
        }

        void emit_info(
            const detail::AudioPatchPlatformActions& actions,
            const char* text) noexcept
        {
            if (actions.log_info != nullptr)
            {
                try { actions.log_info(text); }
                catch (...)
                {
                }
            }
        }

        void emit_error(
            const detail::AudioPatchPlatformActions& actions,
            const char* text) noexcept
        {
            if (actions.log_error != nullptr)
            {
                try { actions.log_error(text); }
                catch (...)
                {
                }
            }
        }

        void report_audio_buffer_handoff(
            const detail::AudioPatchPlatformActions& actions,
            std::string_view stage,
            REFERENCE_TIME configured_duration) noexcept
        {
            try
            {
                const auto text = std::format(
                    "WASAPI audio buffer handoff stage={} configured_buffer_ms={} "
                    "configured_duration_100ns={}",
                    stage,
                    configured_duration / kReferenceTimePerMillisecond,
                    configured_duration);
                emit_info(actions, text.c_str());
            }
            catch (...)
            {
                emit_error(
                    actions,
                    "WASAPI audio buffer handoff diagnostics formatting failed");
            }
        }

        void show_error(const detail::AudioPatchPlatformActions& actions) noexcept
        {
            if (actions.show_error != nullptr)
            {
                try { actions.show_error(kFailureMessage.data()); }
                catch (...)
                {
                }
            }
        }

        void show_generic_startup_error(
            const detail::AudioPatchPlatformActions& actions) noexcept
        {
            if (actions.show_error != nullptr)
            {
                try
                {
                    actions.show_error(kGenericStartupFailureMessage.data());
                }
                catch (...)
                {
                }
            }
        }

        void show_runtime_error(
            const detail::AudioPatchPlatformActions& actions,
            AudioFailureStage stage) noexcept
        {
            if (actions.show_error == nullptr)
            {
                return;
            }
            const auto message = stage == AudioFailureStage::ChronicOutputGap
                                     ? kPacingFailureMessage
                                     : kFailureMessage;
            try { actions.show_error(message.data()); }
            catch (...)
            {
            }
        }

        void production_log_info(const char* text)
        {
            PLOG_INFO << (text == nullptr ? "" : text);
        }

        void production_log_error(const char* text)
        {
            PLOG_ERROR << (text == nullptr ? "" : text);
        }

        void production_show_error(const char* text)
        {
            MessageBoxA(
                nullptr,
                text == nullptr ? "" : text,
                "GCLoader audio error",
                MB_OK | MB_ICONERROR);
        }

        void production_terminate_process(DWORD exit_code)
        {
            TerminateProcess(GetCurrentProcess(), exit_code);
        }

        [[noreturn]] void production_fail_fast()
        {
            RaiseFailFastException(nullptr, nullptr, 0);
            std::abort();
        }

        detail::AudioPatchPlatformActions production_platform_actions() noexcept
        {
            return {
                production_log_info,
                production_log_error,
                production_show_error,
                production_terminate_process,
                production_fail_fast,
            };
        }

        struct ProductionDiagnosticContext
        {
            AudioBackend requested_backend{AudioBackend::directsound};
        };

        class ProductionAudioObserver final : public IAudioEngineObserver
        {
        public:
            explicit ProductionAudioObserver(
                const detail::AudioPatchPlatformActions& actions,
                ProductionDiagnosticContext& diagnostics) noexcept
                : actions_(actions), diagnostics_(diagnostics)
            {
            }

            void StartupSucceeded(
                const EndpointInitialization& initialization) noexcept override
            {
                try
                {
                    initialization_ = initialization;
                    const auto text = startup_text(
                        initialization,
                        diagnostics_.requested_backend);
                    emit_info(actions_, text.c_str());
                }
                catch (...)
                {
                    emit_error(actions_, "Audio startup diagnostics formatting failed");
                }
            }

            void RuntimeSummary(
                const AudioRuntimeCountersSnapshot& counters) noexcept override
            {
                detail::ReportAudioRuntimeSummary(counters, actions_);
            }

            void RuntimeFailed(
                const AudioFailure& failure,
                const AudioRuntimeCountersSnapshot& counters) noexcept override
            {
                const EndpointInitialization unknown{};
                detail::ReportAudioRuntimeFailure(
                    initialization_ ? *initialization_ : unknown,
                    failure,
                    counters,
                    actions_);
            }

        private:
            detail::AudioPatchPlatformActions actions_{};
            ProductionDiagnosticContext& diagnostics_;
            std::optional<EndpointInitialization> initialization_;
        };

        class ProductionWasapiOutputBackendFactory final
            : public IWasapiOutputBackendFactory
        {
        public:
            ProductionWasapiOutputBackendFactory(
                const detail::AudioPatchPlatformActions& actions,
                ProductionDiagnosticContext& diagnostics,
                bool enable_absolute_time_judgement) noexcept
                : actions_(actions),
                  diagnostics_(diagnostics),
                  enable_absolute_time_judgement_(
                      enable_absolute_time_judgement)
            {
            }

            std::unique_ptr<IAudioEngineServices> Start(
                REFERENCE_TIME configured_duration,
                AudioStartupFailure* startup_failure) noexcept override
            {
                std::shared_ptr<ProductionAudioObserver> observer;
                try
                {
                    observer = std::make_shared<ProductionAudioObserver>(
                        actions_, diagnostics_);
                }
                catch (...)
                {
                    if (startup_failure != nullptr)
                    {
                        *startup_failure = {};
                        startup_failure->failure = {
                            AudioFailureStage::InitializeMixer,
                            E_OUTOFMEMORY,
                        };
                    }
                    return nullptr;
                }

                auto engine = detail::StartProductionExclusiveAudioEngine(
                    CreateProductionWasapiApi,
                    &ExclusiveAudioEngine::StartAndWait,
                    configured_duration,
                    enable_absolute_time_judgement_,
                    actions_,
                    std::move(observer),
                    startup_failure);
                return engine;
            }

        private:
            detail::AudioPatchPlatformActions actions_{};
            ProductionDiagnosticContext& diagnostics_;
            bool enable_absolute_time_judgement_{};
        };

        class ProductionAsioOutputBackendFactory final
            : public IAsioOutputBackendFactory
        {
        public:
            std::unique_ptr<IAudioEngineServices> Start(
                HWND game_window,
                const AsioStreamRequest& request) noexcept override
            {
                return AsioOutputBackend::Start(
                    game_window,
                    request,
                    std::move(registry_),
                    std::move(driver_factory_));
            }

        private:
            std::unique_ptr<IAsioRegistrySource> registry_ =
                std::make_unique<ProductionAsioRegistrySource>();
            std::unique_ptr<IAsioDriverFactory> driver_factory_ =
                std::make_unique<ProductionAsioDriverFactory>();
        };

        class ProductionAudioBackendControllerReporter final
            : public IAudioBackendControllerReporter
        {
        public:
            ProductionAudioBackendControllerReporter(
                const detail::AudioPatchPlatformActions& actions,
                ProductionDiagnosticContext* diagnostics = nullptr) noexcept
                : actions_(actions), diagnostics_(diagnostics)
            {
            }

            void FatalStartupFailure(
                const AudioBackendStartupFailure& failure) noexcept override
            {
                try
                {
                    const auto detail = failure_text(
                        "Audio startup fatal",
                        failure.wasapi_failure.attempted,
                        failure.wasapi_failure.failure);
                    const auto text = std::format(
                        "{} requested_backend={} active_backend=failed",
                        detail,
                        AudioBackendName(failure.requested_backend));
                    emit_error(actions_, text.c_str());
                }
                catch (...)
                {
                    emit_error(actions_, "Audio startup fatal formatting failed");
                }
                show_generic_startup_error(actions_);
                if (actions_.terminate_process != nullptr)
                {
                    try
                    {
                        actions_.terminate_process(ERROR_DEVICE_NOT_AVAILABLE);
                    }
                    catch (...)
                    {
                    }
                }
                if (actions_.fail_fast != nullptr)
                {
                    try { actions_.fail_fast(); }
                    catch (...)
                    {
                    }
                }
            }

            void FatalControllerAllocationFailure() noexcept override
            {
                emit_error(
                    actions_,
                    "Audio controller allocation fatal hresult=0x8007000E");
                show_generic_startup_error(actions_);
                if (actions_.terminate_process != nullptr)
                {
                    try
                    {
                        actions_.terminate_process(ERROR_NOT_ENOUGH_MEMORY);
                    }
                    catch (...)
                    {
                    }
                }
                if (actions_.fail_fast != nullptr)
                {
                    try { actions_.fail_fast(); }
                    catch (...)
                    {
                    }
                }
            }

        private:
            detail::AudioPatchPlatformActions actions_{};
            ProductionDiagnosticContext* diagnostics_{};
        };

        class ProductionAudioBackendControllerFactory final
            : public IAudioBackendControllerFactory
        {
        public:
            ProductionAudioBackendControllerFactory(
                AudioBackendControllerConfig config,
                IWasapiOutputBackendFactory& wasapi,
                IAsioOutputBackendFactory& asio,
                IAudioBackendControllerReporter& reporter)
                : config_(std::move(config)),
                  wasapi_(wasapi),
                  asio_(asio),
                  reporter_(reporter)
            {
                if (config_.requested_backend == AudioBackend::asio)
                {
                    controller_ = std::make_unique<AudioBackendController>(
                        config_, wasapi_, asio_, reporter_);
                }
            }

            IAudioEngineController* GetOrCreate() noexcept override
            {
                if (config_.requested_backend == AudioBackend::asio)
                {
                    return controller_.get();
                }
                std::lock_guard lock(mutex_);
                if (attempted_)
                {
                    return controller_.get();
                }
                attempted_ = true;
                try
                {
                    controller_ = std::make_unique<AudioBackendController>(
                        config_, wasapi_, asio_, reporter_);
                }
                catch (...)
                {
                    controller_.reset();
                }
                return controller_.get();
            }

        private:
            AudioBackendControllerConfig config_;
            IWasapiOutputBackendFactory& wasapi_;
            IAsioOutputBackendFactory& asio_;
            IAudioBackendControllerReporter& reporter_;
            std::mutex mutex_;
            bool attempted_{};
            std::unique_ptr<AudioBackendController> controller_;
        };

        std::uint32_t configured_wasapi_buffer_ms(
            const AudioSettings& settings) noexcept
        {
            if (const auto* wasapi = std::get_if<WasapiExclusiveSettings>(
                &settings.selection()))
            {
                return wasapi->buffer_ms();
            }
            return 0;
        }

        bool configured_wasapi_exact_history(
            const AudioSettings& settings) noexcept
        {
            if (const auto* wasapi = std::get_if<WasapiExclusiveSettings>(
                &settings.selection()))
            {
                return wasapi->exact_history_required();
            }
            return false;
        }

        AudioBackendControllerConfig production_controller_config(
            const AudioSettings& settings)
        {
            AudioBackendControllerConfig config{
                .requested_backend = settings.backend(),
                .wasapi_configured_duration = BufferMillisecondsToReferenceTime(
                    configured_wasapi_buffer_ms(settings)),
            };
            if (const auto* asio = std::get_if<AsioSettings>(
                &settings.selection()))
            {
                config.asio_request = {
                    .driver_name = asio->driver_name(),
                    .buffer_frames = asio->buffer_frames(),
                    .output_base_channel = asio->output_base_channel(),
                };
            }
            return config;
        }

        struct ProductionDetourState
        {
            explicit ProductionDetourState(AudioSettings startup_settings)
                : settings(std::move(startup_settings)),
                  config(production_controller_config(settings)),
                  diagnostics{config.requested_backend},
                  reporter(production_platform_actions(), &diagnostics),
                  wasapi(
                      production_platform_actions(),
                      diagnostics,
                      configured_wasapi_exact_history(settings)),
                  factory(config, wasapi, asio, reporter)
            {
                if (config.requested_backend == AudioBackend::wasapi_exclusive)
                {
                    report_audio_buffer_handoff(
                        production_platform_actions(),
                        "detour_state",
                        config.wasapi_configured_duration);
                }
            }

            AudioSettings settings;
            AudioBackendControllerConfig config;
            ProductionDiagnosticContext diagnostics;
            ProductionAudioBackendControllerReporter reporter;
            ProductionWasapiOutputBackendFactory wasapi;
            ProductionAsioOutputBackendFactory asio;
            ProductionAudioBackendControllerFactory factory;
        };

        std::atomic<ProductionDetourState*> g_production_detour_state{};

        // SafetyHook's mid-hook ABI requires a mutable context reference.
        void OnAsioOrdinaryClose(safetyhook::Context&) noexcept
        {
            auto* const owner = g_production_detour_state.exchange(
                nullptr,
                std::memory_order_acq_rel);
            if (owner == nullptr)
            {
                FailAudioContract(
                    AudioContractFatalReason::AsioOwnershipFailure,
                    kAsioCloseRva);
            }
            delete owner;
        }

        bool CreateAsioOrdinaryCloseHook(
            safetyhook::MidHook& candidate) noexcept
        {
            const auto module = reinterpret_cast<std::uintptr_t>(
                GetModuleHandleW(nullptr));
            if (module != kSupportedImageBase)
            {
                return false;
            }

            const auto address = module + kAsioCloseRva;
            std::array<unsigned char, kAsioCloseBytes.size()> observed{};
            if (!ReadExecutableBytes(
                    reinterpret_cast<const void*>(address),
                    observed.data(),
                    observed.size()) ||
                observed != kAsioCloseBytes)
            {
                return false;
            }

            try
            {
                candidate = safetyhook::create_mid(
                    reinterpret_cast<void*>(address),
                    OnAsioOrdinaryClose);
                return static_cast<bool>(candidate);
            }
            catch (...)
            {
                return false;
            }
        }

    } // namespace

    namespace detail
    {
        void ReportAudioStartupSucceeded(
            const EndpointInitialization& initialization,
            const AudioPatchPlatformActions& actions) noexcept
        {
            try
            {
                const auto text = startup_text(initialization);
                emit_info(actions, text.c_str());
            }
            catch (...)
            {
                emit_error(
                    actions,
                    "WASAPI audio startup diagnostics formatting failed");
            }
        }

        void ReportAudioRuntimeSummary(
            const AudioRuntimeCountersSnapshot& counters,
            const AudioPatchPlatformActions& actions) noexcept
        {
            try
            {
                const auto text = std::string{"WASAPI audio runtime summary "} +
                    counters_text(counters);
                emit_info(actions, text.c_str());
            }
            catch (...)
            {
                emit_error(
                    actions,
                    "WASAPI audio runtime summary formatting failed");
            }
        }

        void ReportAudioRuntimeFailure(
            const EndpointInitialization& initialization,
            const AudioFailure& failure,
            const AudioRuntimeCountersSnapshot& counters,
            const AudioPatchPlatformActions& actions) noexcept
        {
            try
            {
                const auto text = failure_text(
                    "WASAPI audio runtime fatal",
                    initialization,
                    failure) + " " + counters_text(counters);
                emit_error(actions, text.c_str());
            }
            catch (...)
            {
                emit_error(actions, "WASAPI audio runtime fatal formatting failed");
            }
            show_runtime_error(actions, failure.stage);
            if (actions.terminate_process != nullptr)
            {
                try
                {
                    actions.terminate_process(ERROR_DEVICE_NOT_AVAILABLE);
                }
                catch (...)
                {
                }
            }
            if (actions.fail_fast != nullptr)
            {
                try { actions.fail_fast(); }
                catch (...)
                {
                }
            }
        }

        void ReportAudioStartupFailure(
            const AudioStartupFailure& failure,
            const AudioPatchPlatformActions& actions) noexcept
        {
            try
            {
                const auto text = failure_text(
                    "WASAPI audio startup fatal",
                    failure.attempted,
                    failure.failure);
                emit_error(actions, text.c_str());
            }
            catch (...)
            {
                emit_error(actions, "WASAPI audio startup fatal formatting failed");
            }
            show_error(actions);
            if (actions.terminate_process != nullptr)
            {
                try
                {
                    actions.terminate_process(ERROR_DEVICE_NOT_AVAILABLE);
                }
                catch (...)
                {
                }
            }
            if (actions.fail_fast != nullptr)
            {
                try { actions.fail_fast(); }
                catch (...)
                {
                }
            }
        }

        std::unique_ptr<ExclusiveAudioEngine> StartProductionExclusiveAudioEngine(
            CreateWasapiApiFn create_api,
            StartExclusiveAudioEngineFn start_engine,
            REFERENCE_TIME configured_duration,
            bool enable_absolute_time_judgement,
            const AudioPatchPlatformActions& actions,
            std::shared_ptr<IAudioEngineObserver> observer,
            AudioStartupFailure* startup_failure) noexcept
        {
            report_audio_buffer_handoff(
                actions,
                "production_engine_start",
                configured_duration);
            if (startup_failure != nullptr)
            {
                *startup_failure = {};
            }
            if (create_api == nullptr || start_engine == nullptr)
            {
                if (startup_failure != nullptr)
                {
                    startup_failure->failure = {
                        AudioFailureStage::InitializeMixer,
                        E_INVALIDARG,
                    };
                }
                return nullptr;
            }

            auto api = create_api();
            if (api == nullptr)
            {
                if (startup_failure != nullptr)
                {
                    startup_failure->failure = {
                        AudioFailureStage::InitializeMixer,
                        E_OUTOFMEMORY,
                    };
                }
                return nullptr;
            }

            return start_engine(
                std::move(api),
                std::move(observer),
                10'000,
                configured_duration,
                enable_absolute_time_judgement,
                std::shared_ptr<const ma_allocation_callbacks>{},
                startup_failure);
        }

    } // namespace detail

    std::expected<void, hooking::HookError> AddAudioHooks(
        hooking::HookPlan& hooks, AudioSettings settings) noexcept
    {
        try
        {
            const auto backend = settings.backend();
            const auto buffer_ms = configured_wasapi_buffer_ms(settings);
            const auto config_text = audio_config_text(backend, buffer_ms);
            emit_info(production_platform_actions(), config_text.c_str());
            if (backend == AudioBackend::directsound) return {};

            // Factory/reporter and the ASIO callback's state are process-lived.
            auto* state = new ProductionDetourState(std::move(settings));
            g_production_detour_state.store(state, std::memory_order_release);
            if (backend == AudioBackend::asio &&
                !CreateAsioOrdinaryCloseHook(g_asio_close_hook))
                return std::unexpected(hooking::HookError{
                    .stage = hooking::HookStage::create,
                    .identity = {"AsioClose", "ordinary_close"},
                    .win32_error = ERROR_INVALID_DATA,
                });
            return AddDirectSoundHook(hooks, state->factory, state->reporter);
        }
        catch (...)
        {
            return std::unexpected(hooking::HookError{
                .stage = hooking::HookStage::invalid_plan,
                .identity = {"Audio", "prepare"},
                .win32_error = ERROR_NOT_ENOUGH_MEMORY,
            });
        }
    }
} // namespace gc::audio

#include "Audio/AudioPatch.h"

#include "Audio/Asio/AsioDriver.h"
#include "Audio/Asio/AsioDriverCatalog.h"
#include "Audio/Asio/AsioOutputBackend.h"
#include "Audio/DirectSound/DirectSoundFacade.h"
#include "Audio/Wasapi/ExclusiveAudioEngine.h"
#include "Audio/AudioPatchInternal.h"

#include "plog/Log.h"

#include <dsound.h>

#include <algorithm>
#include <atomic>
// ReSharper disable once CppUnusedIncludeDirective
#include <cmath>
#include <cstdlib>
#include <format>
#include <iomanip>
#include <iterator>
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
        LPVOID g_original_direct_sound_create8{};
        LPVOID g_committed_target{};

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
        constexpr std::string_view kAsioRuntimeFailureMessage =
            "ASIO low-latency audio failed after startup.\n"
            "Restart the game. You may select WASAPI Exclusive in ConfigGUI.";
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

        const char* asio_failure_stage_name(AsioFailureStage stage) noexcept
        {
            switch (stage)
            {
            case AsioFailureStage::none: return "none";
            case AsioFailureStage::registry: return "registry";
            case AsioFailureStage::clsid: return "clsid";
            case AsioFailureStage::com: return "com";
            case AsioFailureStage::init: return "init";
            case AsioFailureStage::identity: return "identity";
            case AsioFailureStage::channels: return "channels";
            case AsioFailureStage::sample_rate: return "sample_rate";
            case AsioFailureStage::buffer_metadata: return "buffer_metadata";
            case AsioFailureStage::channel_info: return "channel_info";
            case AsioFailureStage::output_ready_probe: return "output_ready_probe";
            case AsioFailureStage::callback_prepare: return "callback_prepare";
            case AsioFailureStage::foreground_monitor: return "foreground_monitor";
            case AsioFailureStage::create_buffers: return "create_buffers";
            case AsioFailureStage::latency: return "latency";
            case AsioFailureStage::render_core: return "render_core";
            case AsioFailureStage::start: return "start";
            case AsioFailureStage::startup_clock: return "startup_clock";
            case AsioFailureStage::callback: return "callback";
            case AsioFailureStage::conversion: return "conversion";
            case AsioFailureStage::runtime_clock: return "runtime_clock";
            case AsioFailureStage::output_ready: return "output_ready";
            case AsioFailureStage::stop: return "stop";
            case AsioFailureStage::dispose: return "dispose";
            case AsioFailureStage::restore_sample_rate: return "restore_sample_rate";
            case AsioFailureStage::protocol: return "protocol";
            case AsioFailureStage::process_launch: return "process_launch";
            case AsioFailureStage::process_job: return "process_job";
            case AsioFailureStage::probe_timeout: return "probe_timeout";
            case AsioFailureStage::probe_crash: return "probe_crash";
            case AsioFailureStage::control_panel: return "control_panel";
            case AsioFailureStage::control_panel_crash: return "control_panel_crash";
            case AsioFailureStage::multimedia_timer: return "multimedia_timer";
            }
            return "unknown";
        }

        const char* asio_result_domain_name(AsioResultDomain domain) noexcept
        {
            switch (domain)
            {
            case AsioResultDomain::none: return "none";
            case AsioResultDomain::asio: return "asio";
            case AsioResultDomain::hresult: return "hresult";
            case AsioResultDomain::win32: return "win32";
            case AsioResultDomain::winmm: return "winmm";
            }
            return "unknown";
        }

        const char* asio_sample_type_name(ASIOSampleType type) noexcept
        {
            switch (type)
            {
            case ASIOSTInt16MSB: return "Int16MSB";
            case ASIOSTInt24MSB: return "Int24MSB";
            case ASIOSTInt32MSB: return "Int32MSB";
            case ASIOSTFloat32MSB: return "Float32MSB";
            case ASIOSTFloat64MSB: return "Float64MSB";
            case ASIOSTInt32MSB16: return "Int32MSB16";
            case ASIOSTInt32MSB18: return "Int32MSB18";
            case ASIOSTInt32MSB20: return "Int32MSB20";
            case ASIOSTInt32MSB24: return "Int32MSB24";
            case ASIOSTInt16LSB: return "Int16LSB";
            case ASIOSTInt24LSB: return "Int24LSB";
            case ASIOSTInt32LSB: return "Int32LSB";
            case ASIOSTFloat32LSB: return "Float32LSB";
            case ASIOSTFloat64LSB: return "Float64LSB";
            case ASIOSTInt32LSB16: return "Int32LSB16";
            case ASIOSTInt32LSB18: return "Int32LSB18";
            case ASIOSTInt32LSB20: return "Int32LSB20";
            case ASIOSTInt32LSB24: return "Int32LSB24";
            case ASIOSTDSDInt8LSB1: return "DSDInt8LSB1";
            case ASIOSTDSDInt8MSB1: return "DSDInt8MSB1";
            case ASIOSTDSDInt8NER8: return "DSDInt8NER8";
            default: return "Unknown";
            }
        }

        const char* asio_physical_session_reason_name(
            const AsioPhysicalSessionReason reason) noexcept
        {
            switch (reason)
            {
            case AsioPhysicalSessionReason::startup: return "startup";
            case AsioPhysicalSessionReason::focus_recovery:
                return "focus_recovery";
            }
            return "unknown";
        }

        const char* asio_attachment_disposition_name(
            const AsioPhysicalAttachmentDisposition disposition) noexcept
        {
            switch (disposition)
            {
            case AsioPhysicalAttachmentDisposition::Aligned: return "aligned";
            case AsioPhysicalAttachmentDisposition::WaitForPhysical:
                return "wait_for_physical";
            case AsioPhysicalAttachmentDisposition::CatchUpLogical:
                return "catch_up_logical";
            }
            return "unknown";
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

        const char* audio_hook_stage_name(AudioHookStage stage) noexcept
        {
            switch (stage)
            {
            case AudioHookStage::None: return "None";
            case AudioHookStage::ValidateApi: return "ValidateApi";
            case AudioHookStage::ResolveModule: return "ResolveModule";
            case AudioHookStage::ResolveExport: return "ResolveExport";
            case AudioHookStage::InitializeMinHook: return "InitializeMinHook";
            case AudioHookStage::CreateHook: return "CreateHook";
            case AudioHookStage::QueueEnable: return "QueueEnable";
            case AudioHookStage::ApplyQueued: return "ApplyQueued";
            }
            return "Unknown";
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

        std::string hook_failure_text(const AudioHookFailure& failure)
        {
            return std::format(
                "AudioPatch: hook install failed stage={} status={} "
                "win32_error={} target=0x{:0{}X} rollback_attempted={} "
                "rollback_disable_status={} rollback_remove_status={} "
                "rollback_complete={}",
                audio_hook_stage_name(failure.stage),
                static_cast<int>(failure.status),
                failure.win32_error,
                reinterpret_cast<std::uintptr_t>(failure.target),
                sizeof(std::uintptr_t) * 2,
                failure.rollback_attempted,
                static_cast<int>(failure.rollback_disable_status),
                static_cast<int>(failure.rollback_remove_status),
                failure.rollback_complete);
        }

        std::string audio_config_text(
            AudioBackend requested_backend,
            std::uint32_t configured_buffer_ms)
        {
            const bool enabled =
                requested_backend != AudioBackend::directsound;
            return std::format(
                "Audio config requested_backend={} active_backend={} "
                "hook_installed={} enabled={} configured_buffer_ms={} "
                "configured_duration_100ns={}",
                AudioBackendName(requested_backend),
                enabled ? "pending" : "directsound",
                enabled,
                enabled,
                configured_buffer_ms,
                BufferMillisecondsToReferenceTime(configured_buffer_ms));
        }

        std::string clsid_text(const CLSID& clsid)
        {
            wchar_t value[39]{};
            return StringFromGUID2(clsid, value, static_cast<int>(std::size(value))) > 0
                       ? utf8(value)
                       : "<conversion-failed>";
        }

        std::string asio_failure_text(
            std::string_view kind,
            const AsioFailure& failure)
        {
            return std::format(
                "{} asio_failure_stage={} asio_failure_domain={} "
                "asio_failure_result={} asio_driver_message=\"{}\" "
                "asio_failure_detail=\"{}\"",
                kind,
                asio_failure_stage_name(failure.stage),
                asio_result_domain_name(failure.domain),
                failure.result,
                failure.driver_message,
                failure.detail);
        }

        std::string asio_startup_text(
            const AsioCapabilityReport& report,
            const AsioLogicalBackendRecord& logical)
        {
            const auto base = static_cast<std::size_t>(report.selected_base_channel);
            const auto& left = report.output_channels.at(base);
            const auto& right = report.output_channels.at(base + 1U);
            const double expected_callback_us =
                std::isfinite(report.sample_rate) && report.sample_rate > 0.0
                    ? static_cast<double>(report.effective_buffer_frames) *
                    1'000'000.0 / report.sample_rate
                    : 0.0;
            std::ostringstream stream;
            stream << "Audio startup requested_backend=asio active_backend=asio"
                << " alternate_backend_selected="
                << (logical.alternate_backend_selected ? "true" : "false")
                << " asio_registry_name=\"" << report.registration.registry_name << '"'
                << " asio_reported_name=\"" << report.reported_driver_name << '"'
                << " asio_clsid=\"" << clsid_text(report.registration.clsid) << '"'
                << " asio_driver_version=" << report.driver_version
                << " sample_rate_source=driver_current"
                << " sample_rate=" << logical.sample_rate
                << " asio_requested_buffer_frames=" << report.effective_buffer_frames
                << " asio_buffer_frames=" << report.effective_buffer_frames
                << " asio_expected_callback_us=" << expected_callback_us
                << " asio_buffer_minimum_frames=" << report.buffer_limits.minimum
                << " asio_buffer_maximum_frames=" << report.buffer_limits.maximum
                << " asio_buffer_preferred_frames=" << report.buffer_limits.preferred
                << " asio_buffer_granularity=" << report.buffer_limits.granularity
                << " asio_output_base_channel=" << report.selected_base_channel
                << " asio_left_channel_index=" << left.index
                << " asio_left_channel_name=\"" << left.name << '"'
                << " asio_left_sample_type="
                << asio_sample_type_name(left.sample_type)
                << '(' << static_cast<long>(left.sample_type) << ')'
                << " asio_right_channel_index=" << right.index
                << " asio_right_channel_name=\"" << right.name << '"'
                << " asio_right_sample_type="
                << asio_sample_type_name(right.sample_type)
                << '(' << static_cast<long>(right.sample_type) << ')'
                << " asio_input_latency_frames=" << report.input_latency_frames
                << " asio_output_latency_frames=" << report.output_latency_frames
                << " logical_period_frames=" << logical.period_frames
                << " logical_output_latency_frames="
                << logical.output_latency_frames
                << " logical_origin_raw_ms=" << logical.origin_raw_ms
                << " logical_origin_unwrapped_ms="
                << logical.origin_unwrapped_ms
                << " logical_origin_presented_frame="
                << logical.origin_presented_frame
                << " logical_timeline_generation="
                << logical.timeline_generation
                << " exact_domain=asio_multimedia_ms"
                << " exact_timestamp_quantum_ms=1"
                << " asio_time_info_mode=preferred_with_legacy_fallback"
                << " asio_overload_notifications="
                << (report.overload_reporting_supported ? "true" : "false")
                << " asio_output_ready="
                << (report.output_ready_supported ? "true" : "false");
            return stream.str();
        }

        std::string asio_counters_text(const AsioRuntimeCountersSnapshot& counters)
        {
            const auto ticks_to_microseconds = [&](std::uint64_t ticks)
            {
                return counters.qpc_frequency == 0
                           ? 0.0
                           : static_cast<double>(ticks) * 1'000'000.0 /
                           static_cast<double>(counters.qpc_frequency);
            };
            const auto average_ticks_microseconds = [&](std::uint64_t ticks,
                                                        std::uint64_t samples)
            {
                return samples == 0
                           ? 0.0
                           : ticks_to_microseconds(ticks) /
                           static_cast<double>(samples);
            };
            std::ostringstream stream;
            stream << "callbacks=" << counters.callbacks
                << " time_info_callbacks=" << counters.time_info_callbacks
                << " legacy_callbacks=" << counters.legacy_callbacks
                << " deferred_callbacks=" << counters.deferred_callbacks
                << " deadline_misses=" << counters.deadline_misses
                << " silence_substitutions=" << counters.silence_substitutions
                << " overload_messages=" << counters.overload_messages
                << " reset_requests=" << counters.reset_requests
                << " resync_requests=" << counters.resync_requests
                << " latency_change_requests=" << counters.latency_change_requests
                << " buffer_size_change_requests="
                << counters.buffer_size_change_requests
                << " sample_rate_change_requests="
                << counters.sample_rate_change_requests
                << " sample_position_discontinuities="
                << counters.sample_position_discontinuities
                << " render_gap_frames=" << counters.render_gap_frames
                << " foreground_losses=" << counters.foreground_losses
                << " consumed_focus_loss_generation="
                << counters.consumed_focus_loss_generation
                << " physical_session_generation="
                << counters.physical_session_generation
                << " session_releases=" << counters.session_releases
                << " recovery_attempts=" << counters.recovery_attempts
                << " recovery_failures=" << counters.recovery_failures
                << " session_recoveries=" << counters.session_recoveries
                << " submitted_tail_publications="
                << counters.submitted_tail_publications
                << " submitted_output_tail="
                << counters.submitted_output_tail
                << " total_logically_advanced_frames="
                << counters.total_logically_advanced_frames
                << " detached_discarded_frames="
                << counters.detached_discarded_frames
                << " priming_discarded_frames="
                << counters.priming_discarded_frames
                << " driver_timeline_residual_samples="
                << counters.driver_timeline_residual_samples
                << " maximum_absolute_driver_timeline_residual_ns="
                << counters.maximum_absolute_driver_timeline_residual_ns
                << " driver_timeline_residual_diagnostic_only=true"
                << " asio_expected_callback_us="
                << static_cast<double>(counters.expected_period_ns) / 1'000.0
                << " callback_interval_samples="
                << counters.callback_interval_samples
                << " total_callback_interval_ticks="
                << counters.total_callback_interval_ticks
                << " maximum_callback_interval_ticks="
                << counters.maximum_callback_interval_ticks
                << " average_callback_interval_us="
                << average_ticks_microseconds(
                    counters.total_callback_interval_ticks,
                    counters.callback_interval_samples)
                << " maximum_callback_interval_us="
                << ticks_to_microseconds(counters.maximum_callback_interval_ticks)
                << " early_callback_intervals="
                << counters.early_callback_intervals
                << " late_callback_intervals=" << counters.late_callback_intervals
                << " severe_callback_intervals="
                << counters.severe_callback_intervals
                << " timed_callback_work_samples="
                << counters.timed_callback_work_samples
                << " total_callback_ticks=" << counters.total_callback_ticks
                << " average_callback_us="
                << average_ticks_microseconds(
                    counters.total_callback_ticks,
                    counters.timed_callback_work_samples)
                << " maximum_callback_ticks=" << counters.maximum_callback_ticks
                << " maximum_callback_us="
                << ticks_to_microseconds(counters.maximum_callback_ticks)
                << " timed_render_work_samples="
                << counters.timed_render_work_samples
                << " total_render_ticks=" << counters.total_render_ticks
                << " average_render_us="
                << average_ticks_microseconds(
                    counters.total_render_ticks,
                    counters.timed_render_work_samples)
                << " maximum_render_ticks=" << counters.maximum_render_ticks
                << " maximum_render_us="
                << ticks_to_microseconds(counters.maximum_render_ticks)
                << " qpc_frequency=" << counters.qpc_frequency
                << " driver_interval_samples=" << counters.driver_interval_samples
                << " maximum_driver_period_error_us="
                << static_cast<double>(counters.maximum_driver_period_error_ns) /
                1'000.0
                << " maximum_host_driver_interval_skew_us="
                << static_cast<double>(
                    counters.maximum_host_driver_interval_skew_ns) / 1'000.0
                << " buffer_alternation_violations="
                << counters.buffer_alternation_violations
                << " no_active_voice_silence_blocks="
                << counters.no_active_voice_silence_blocks
                << " active_short_read_blocks="
                << counters.active_short_read_blocks
                << " mixer_error_blocks=" << counters.mixer_error_blocks
                << " render_contract_error_blocks="
                << counters.render_contract_error_blocks
                << " short_read_missing_frames="
                << counters.short_read_missing_frames
                << " first_mixer_error=" << counters.first_mixer_error
                << " clipped_output_blocks=" << counters.clipped_output_blocks
                << " clipped_output_samples=" << counters.clipped_output_samples
                << " zero_output_blocks_with_active_voice="
                << counters.zero_output_blocks_with_active_voice
                << " zero_output_blocks_without_active_voice="
                << counters.zero_output_blocks_without_active_voice
                << " non_finite_output_blocks="
                << counters.non_finite_output_blocks
                << " maximum_absolute_output_sample="
                << counters.maximum_absolute_output_sample
                << " pending_cursor_queries=" << counters.pending_cursor_queries
                << " unmapped_cursor_failures="
                << counters.unmapped_cursor_failures;
            append_mixer_diagnostics(stream, counters.mixer);
            stream << " exact_resolved_queries=" << counters.exact_resolved_queries
                << " exact_pending_queries=" << counters.exact_pending_queries
                << " exact_temporarily_unavailable_queries="
                << counters.exact_temporarily_unavailable_queries
                << " exact_history_lost_queries="
                << counters.exact_history_lost_queries
                << " exact_discontinuous_queries="
                << counters.exact_discontinuous_queries;
            return stream.str();
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

        class ProductionAsioObserver final : public IAsioOutputObserver
        {
        public:
            explicit ProductionAsioObserver(
                const detail::AudioPatchPlatformActions& actions) noexcept
                : actions_(actions)
            {
            }

            void StartupSucceeded(
                const AsioCapabilityReport& report,
                const AsioLogicalBackendRecord& logical) noexcept override
            {
                try
                {
                    report_ = report;
                }
                catch (...)
                {
                    report_.reset();
                }
                detail::ReportAsioStartupSucceeded(report, logical, actions_);
            }

            void RuntimeSummary(
                const AsioRuntimeCountersSnapshot& counters) noexcept override
            {
                detail::ReportAsioRuntimeSummary(counters, actions_);
            }

            void RuntimeFailed(
                const AsioFailure& failure,
                const AsioRuntimeCountersSnapshot& counters) noexcept override
            {
                detail::ReportAsioRuntimeFailure(
                    report_ ? &*report_ : nullptr,
                    failure,
                    counters,
                    actions_);
            }

            void SessionLifecycleChanged(
                const AsioSessionLifecycleRecord& record,
                const AsioFailure* failure) noexcept override
            {
                try
                {
                    const auto event_name = [&record]() noexcept
                    {
                        switch (record.event)
                        {
                        case AsioSessionLifecycleEvent::foreground_lost:
                            return "foreground_lost";
                        case AsioSessionLifecycleEvent::session_released:
                            return "session_released";
                        case AsioSessionLifecycleEvent::foreground_regained:
                            return "foreground_regained";
                        case AsioSessionLifecycleEvent::recovery_attempt_started:
                            return "recovery_attempt_started";
                        case AsioSessionLifecycleEvent::recovery_attempt_failed:
                            return "recovery_attempt_failed";
                        case AsioSessionLifecycleEvent::physical_session_started:
                            return "physical_session_started";
                        case AsioSessionLifecycleEvent::session_recovered:
                            return "session_recovered";
                        }
                        return "unknown";
                    }();
                    std::ostringstream stream;
                    stream << "ASIO session lifecycle event=" << event_name
                        << " foreground="
                        << (record.foreground ? "true" : "false")
                        << " focus_loss_generation="
                        << record.focus_loss_generation
                        << " physical_session_generation="
                        << record.physical_session_generation
                        << " recovery_attempt=" << record.recovery_attempt
                        << " retry_delay_ms=" << record.retry_delay_ms
                        << " logical_render_origin="
                        << record.logical_render_origin
                        << " physical_render_origin="
                        << record.physical_render_origin;
                    if (record.event ==
                        AsioSessionLifecycleEvent::physical_session_started ||
                        record.event ==
                        AsioSessionLifecycleEvent::session_recovered)
                    {
                        stream << " reason="
                            << asio_physical_session_reason_name(record.reason)
                            << " observed_sample_rate="
                            << record.observed_sample_rate
                            << " active_sample_rate=" << record.active_sample_rate
                            << " frozen_rate_requested="
                            << (record.frozen_rate_requested ? "true" : "false")
                            << " sample_rate_changed="
                            << (record.sample_rate_changed ? "true" : "false")
                            << " raw_sample_origin=" << record.raw_sample_origin
                            << " attachment_disposition="
                            << asio_attachment_disposition_name(
                                record.attachment_disposition)
                            << " attachment_interval_frames="
                            << record.attachment_interval_frames
                            << " silent_priming_callbacks="
                            << record.silent_priming_callbacks;
                    }
                    if (record.event ==
                        AsioSessionLifecycleEvent::session_released)
                    {
                        stream << " reason="
                            << asio_physical_session_reason_name(record.reason)
                            << " observed_sample_rate="
                            << record.observed_sample_rate
                            << " active_sample_rate=" << record.active_sample_rate
                            << " frozen_rate_requested="
                            << (record.frozen_rate_requested ? "true" : "false")
                            << " sample_rate_changed="
                            << (record.sample_rate_changed ? "true" : "false")
                            << " restoration_attempted="
                            << (record.restoration_attempted ? "true" : "false")
                            << " restoration_succeeded="
                            << (record.restoration_succeeded ? "true" : "false")
                            << " callback_quiesced="
                            << (record.callback_quiesced ? "true" : "false")
                            << " buffers_disposed="
                            << (record.buffers_disposed ? "true" : "false");
                    }
                    if (failure != nullptr)
                    {
                        stream << " asio_failure_stage="
                            << asio_failure_stage_name(failure->stage)
                            << " asio_failure_domain="
                            << asio_result_domain_name(failure->domain)
                            << " asio_failure_result=" << failure->result
                            << " asio_driver_message=\""
                            << failure->driver_message << '"'
                            << " asio_failure_detail=\""
                            << failure->detail << '"';
                    }
                    const auto text = stream.str();
                    emit_info(actions_, text.c_str());
                }
                catch (...)
                {
                    emit_error(actions_, "ASIO lifecycle diagnostics formatting failed");
                }
            }

        private:
            detail::AudioPatchPlatformActions actions_{};
            std::optional<AsioCapabilityReport> report_;
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
            ProductionAsioOutputBackendFactory(
                const detail::AudioPatchPlatformActions& actions,
                const bool enable_absolute_time_judgement) noexcept
                : actions_(actions),
                  enable_absolute_time_judgement_(
                      enable_absolute_time_judgement)
            {
            }

            std::unique_ptr<IAudioEngineServices> Start(
                HWND game_window,
                const AsioStreamRequest& request,
                AsioFailure* failure) noexcept override
            {
                try
                {
                    auto observer = std::make_shared<ProductionAsioObserver>(actions_);
                    return AsioOutputBackend::StartAndWait(
                        game_window,
                        request,
                        std::make_unique<ProductionAsioRegistrySource>(),
                        std::make_unique<ProductionAsioDriverFactory>(),
                        std::move(observer),
                        {},
                        2'000,
                        enable_absolute_time_judgement_,
                        failure);
                }
                catch (...)
                {
                    if (failure != nullptr)
                    {
                        *failure = {
                            .stage = AsioFailureStage::render_core,
                            .detail = "Could not allocate ASIO runtime dependencies",
                        };
                    }
                    return nullptr;
                }
            }

        private:
            detail::AudioPatchPlatformActions actions_{};
            bool enable_absolute_time_judgement_{};
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
                    const auto detail = failure.asio_failure
                                            ? asio_failure_text(
                                                "ASIO startup fatal", *failure.asio_failure)
                                            : failure_text(
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
                IAudioBackendControllerReporter& reporter) noexcept
                : config_(std::move(config)),
                  wasapi_(wasapi),
                  asio_(asio),
                  reporter_(reporter)
            {
            }

            IAudioEngineController* GetOrCreate() noexcept override
            {
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
                  enable_absolute_time_judgement(settings.exact_clock_required()),
                  diagnostics{config.requested_backend},
                  reporter(production_platform_actions(), &diagnostics),
                  wasapi(
                      production_platform_actions(),
                      diagnostics,
                      enable_absolute_time_judgement),
                  asio(
                      production_platform_actions(),
                      enable_absolute_time_judgement),
                  factory(config, wasapi, asio, reporter)
            {
                report_audio_buffer_handoff(
                    production_platform_actions(),
                    "detour_state",
                    config.wasapi_configured_duration);
            }

            AudioSettings settings;
            AudioBackendControllerConfig config;
            bool enable_absolute_time_judgement{};
            ProductionDiagnosticContext diagnostics;
            ProductionAudioBackendControllerReporter reporter;
            ProductionWasapiOutputBackendFactory wasapi;
            ProductionAsioOutputBackendFactory asio;
            ProductionAudioBackendControllerFactory factory;
        };

        std::atomic<ProductionDetourState*> g_production_detour_state{};

        ProductionAudioBackendControllerReporter g_startup_failure_reporter{
            production_platform_actions()
        };

        ProductionDetourState* production_detour_state() noexcept
        {
            // Deliberately process-lifetime: DirectSound facade release and process
            // detach must not tear down the engine or join its endpoint threads.
            return g_production_detour_state.load(std::memory_order_acquire);
        }

        struct RollbackResult
        {
            MH_STATUS disable_status{MH_OK};
            MH_STATUS remove_status{MH_OK};
            bool complete{true};
        };

        HRESULT WINAPI DirectSoundCreate8Detour(
            LPCGUID device_guid,
            LPDIRECTSOUND8* output,
            LPUNKNOWN outer)
        {
            if (output == nullptr)
            {
                return DSERR_INVALIDPARAM;
            }
            *output = nullptr;
            if (device_guid != nullptr)
            {
                return DSERR_NODRIVER;
            }
            if (outer != nullptr)
            {
                return DSERR_NOAGGREGATION;
            }

            auto* state = production_detour_state();
            if (state == nullptr)
            {
                g_startup_failure_reporter.FatalControllerAllocationFailure();
                return DSERR_OUTOFMEMORY;
            }
            return detail::InvokeDirectSoundCreate8Detour(
                device_guid,
                output,
                outer,
                state->factory,
                state->reporter,
                reinterpret_cast<DirectSoundCreate8Fn>(
                    g_original_direct_sound_create8));
        }

        void set_failure(
            AudioHookFailure* failure,
            AudioHookStage stage,
            MH_STATUS status,
            DWORD win32_error,
            LPVOID target) noexcept
        {
            if (failure != nullptr)
            {
                *failure = {stage, status, win32_error, target};
            }
        }

        RollbackResult rollback(const AudioMinHookApi& api, LPVOID target) noexcept
        {
            RollbackResult result{};
            result.disable_status = api.disable(target);
            result.remove_status = api.remove(target);
            result.complete = result.remove_status == MH_OK ||
                result.remove_status == MH_ERROR_NOT_CREATED;
            return result;
        }

        void record_rollback(
            AudioHookFailure* failure,
            // This small status aggregate is intentionally passed by value.
            // ReSharper disable once CppPassValueParameterByConstReference
            RollbackResult rollback_result) noexcept
        {
            if (failure != nullptr)
            {
                failure->rollback_attempted = true;
                failure->rollback_disable_status = rollback_result.disable_status;
                failure->rollback_remove_status = rollback_result.remove_status;
                failure->rollback_complete = rollback_result.complete;
            }
        }

        bool complete_api_tables(
            const AudioMinHookApi& minhook,
            detail::AudioResolverApi resolver) noexcept
        {
            return resolver.get_module_handle != nullptr &&
                resolver.get_proc_address != nullptr &&
                minhook.initialize != nullptr && minhook.create != nullptr &&
                minhook.queue_enable != nullptr && minhook.apply != nullptr &&
                minhook.disable != nullptr && minhook.remove != nullptr;
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

        void ReportAsioStartupSucceeded(
            const AsioCapabilityReport& report,
            const AsioLogicalBackendRecord& logical,
            const AudioPatchPlatformActions& actions) noexcept
        {
            try
            {
                const auto text = asio_startup_text(report, logical);
                emit_info(actions, text.c_str());
            }
            catch (...)
            {
                emit_error(actions, "ASIO startup diagnostics formatting failed");
            }
        }

        void ReportAsioRuntimeSummary(
            const AsioRuntimeCountersSnapshot& counters,
            const AudioPatchPlatformActions& actions) noexcept
        {
            try
            {
                const auto text = std::string{"ASIO audio runtime summary "} +
                    asio_counters_text(counters);
                emit_info(actions, text.c_str());
            }
            catch (...)
            {
                emit_error(actions, "ASIO runtime summary formatting failed");
            }
        }

        void ReportAsioRuntimeFailure(
            const AsioCapabilityReport* report,
            const AsioFailure& failure,
            const AsioRuntimeCountersSnapshot& counters,
            const AudioPatchPlatformActions& actions) noexcept
        {
            try
            {
                std::ostringstream stream;
                stream << asio_failure_text("ASIO audio runtime fatal", failure)
                    << ' ' << asio_counters_text(counters);
                if (report != nullptr)
                {
                    stream << " asio_registry_name=\""
                        << report->registration.registry_name << '"'
                        << " asio_reported_name=\""
                        << report->reported_driver_name << '"';
                }
                const auto text = stream.str();
                emit_error(actions, text.c_str());
            }
            catch (...)
            {
                emit_error(actions, "ASIO runtime failure formatting failed");
            }
            if (actions.show_error != nullptr)
            {
                try
                {
                    actions.show_error(kAsioRuntimeFailureMessage.data());
                }
                catch (...)
                {
                }
            }
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

        bool AudioPatchInitWithDependencies(
            AudioBackend requested_backend,
            std::uint32_t configured_buffer_ms,
            const AudioPatchInitDependencies& dependencies)
        {
            const bool enabled =
                requested_backend != AudioBackend::directsound;
            AudioHookFailure failure{};
            if (!InstallAudioHookWithResolver(
                enabled,
                dependencies.minhook,
                dependencies.resolver,
                enabled ? &failure : nullptr))
            {
                try
                {
                    const auto text = hook_failure_text(failure);
                    emit_error(dependencies.platform, text.c_str());
                }
                catch (...)
                {
                    emit_error(
                        dependencies.platform,
                        "AudioPatch: hook failure formatting failed");
                }
                show_error(dependencies.platform);
                if (failure.rollback_complete)
                {
                    return false;
                }

                if (dependencies.platform.terminate_process != nullptr)
                {
                    dependencies.platform.terminate_process(ERROR_DLL_INIT_FAILED);
                }
                if (dependencies.platform.fail_fast != nullptr)
                {
                    dependencies.platform.fail_fast();
                }
                std::abort();
            }

            try
            {
                const auto text = audio_config_text(
                    requested_backend,
                    configured_buffer_ms);
                emit_info(dependencies.platform, text.c_str());
            }
            catch (...)
            {
                emit_error(
                    dependencies.platform,
                    "WASAPI audio config diagnostics formatting failed");
            }
            return true;
        }

        HRESULT InvokeDirectSoundCreate8Detour(
            LPCGUID device_guid,
            LPDIRECTSOUND8* output,
            LPUNKNOWN outer,
            IAudioBackendControllerFactory& factory,
            IAudioBackendControllerReporter& reporter,
            DirectSoundCreate8Fn saved_original) noexcept
        {
            static_cast<void>(saved_original);
            if (output == nullptr)
            {
                return DSERR_INVALIDPARAM;
            }
            *output = nullptr;
            if (device_guid != nullptr)
            {
                return DSERR_NODRIVER;
            }
            if (outer != nullptr)
            {
                return DSERR_NOAGGREGATION;
            }

            auto* controller = factory.GetOrCreate();
            if (controller == nullptr)
            {
                reporter.FatalControllerAllocationFailure();
                return DSERR_OUTOFMEMORY;
            }
            return CreateDirectSoundDevice(*controller, output);
        }

        bool InstallAudioHookWithResolver(
            bool enabled,
            const AudioMinHookApi& minhook,
            AudioResolverApi resolver,
            AudioHookFailure* failure) noexcept
        {
            if (!enabled)
            {
                if (failure != nullptr)
                {
                    *failure = {};
                }
                return true;
            }
            if (failure == nullptr)
            {
                return false;
            }
            *failure = {};
            if (!complete_api_tables(minhook, resolver))
            {
                set_failure(
                    failure,
                    AudioHookStage::ValidateApi,
                    MH_UNKNOWN,
                    ERROR_INVALID_PARAMETER,
                    nullptr);
                return false;
            }

            const auto module = resolver.get_module_handle(L"dsound.dll");
            if (module == nullptr)
            {
                set_failure(
                    failure,
                    AudioHookStage::ResolveModule,
                    MH_OK,
                    ERROR_MOD_NOT_FOUND,
                    nullptr);
                return false;
            }

            const auto target = reinterpret_cast<LPVOID>(
                resolver.get_proc_address(module, "DirectSoundCreate8"));
            if (target == nullptr)
            {
                set_failure(
                    failure,
                    AudioHookStage::ResolveExport,
                    MH_OK,
                    ERROR_PROC_NOT_FOUND,
                    nullptr);
                return false;
            }

            auto status = minhook.initialize();
            if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED)
            {
                set_failure(
                    failure,
                    AudioHookStage::InitializeMinHook,
                    status,
                    ERROR_SUCCESS,
                    target);
                return false;
            }

            status = minhook.create(
                target,
                reinterpret_cast<LPVOID>(&DirectSoundCreate8Detour),
                &g_original_direct_sound_create8);
            if (status != MH_OK)
            {
                set_failure(
                    failure,
                    AudioHookStage::CreateHook,
                    status,
                    ERROR_SUCCESS,
                    target);
                return false;
            }

            status = minhook.queue_enable(target);
            if (status != MH_OK)
            {
                set_failure(
                    failure,
                    AudioHookStage::QueueEnable,
                    status,
                    ERROR_SUCCESS,
                    target);
                record_rollback(failure, rollback(minhook, target));
                return false;
            }

            status = minhook.apply();
            if (status != MH_OK)
            {
                set_failure(
                    failure,
                    AudioHookStage::ApplyQueued,
                    status,
                    ERROR_SUCCESS,
                    target);
                record_rollback(failure, rollback(minhook, target));
                return false;
            }

            g_committed_target = target;
            return true;
        }
    } // namespace detail

    bool InstallAudioHook(
        bool enabled,
        const AudioMinHookApi& api,
        AudioHookFailure* failure) noexcept
    {
        return detail::InstallAudioHookWithResolver(
            enabled,
            api,
            {GetModuleHandleW, GetProcAddress},
            failure);
    }

    bool AudioPatchInit(AudioSettings settings) noexcept
    {
        const auto actions = production_platform_actions();
        try
        {
            const auto requested_backend = settings.backend();
            const auto configured_buffer_ms =
                configured_wasapi_buffer_ms(settings);
            std::unique_ptr<ProductionDetourState> pending_state;
            if (requested_backend != AudioBackend::directsound)
            {
                pending_state = std::make_unique<ProductionDetourState>(
                    std::move(settings));
                g_production_detour_state.store(
                    pending_state.get(), std::memory_order_release);
            }

            const auto initialized = detail::AudioPatchInitWithDependencies(
                requested_backend,
                configured_buffer_ms,
                {
                    {
                        MH_Initialize,
                        MH_CreateHook,
                        MH_QueueEnableHook,
                        MH_ApplyQueued,
                        MH_DisableHook,
                        MH_RemoveHook,
                    },
                    {GetModuleHandleW, GetProcAddress},
                    actions,
                });
            if (!initialized)
            {
                if (pending_state)
                {
                    g_production_detour_state.store(
                        nullptr, std::memory_order_release);
                }
                return false;
            }
            if (pending_state)
            {
                auto* const committed_state = pending_state.release();
                g_production_detour_state.store(
                    committed_state, std::memory_order_release);
            }
            return true;
        }
        catch (...)
        {
            g_production_detour_state.store(nullptr, std::memory_order_release);
            emit_error(
                actions,
                "AudioPatch: unexpected attach initialization failure");
            show_error(actions);
            actions.terminate_process(ERROR_DLL_INIT_FAILED);
            actions.fail_fast();
            std::abort();
        }
    }

    bool IsAudioHookCommitted() noexcept
    {
        return g_committed_target != nullptr;
    }
} // namespace gc::audio

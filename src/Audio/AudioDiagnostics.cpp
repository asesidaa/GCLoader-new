#include "Audio/AudioDiagnostics.h"
#include "Audio/AudioBackendController.h"
#include "Diagnostics/FatalProcess.h"
#include "Platform/Win32/Utf.h"

#include "plog/Log.h"

#include <format>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>

namespace gc::audio {
namespace {
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
    return gc::platform::win32::WideToUtf8(value).value_or("<conversion-failed>");
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


void EmitInfo(const char* text) noexcept
{
    try { PLOG_INFO << (text ? text : ""); } catch (...) {}
}
void EmitError(const char* text) noexcept
{
    try { PLOG_ERROR << (text ? text : ""); } catch (...) {}
}

[[noreturn]] void AbortAudio(std::string log, std::string_view message) noexcept
{
    try {
        diagnostics::AbortProcess({
            std::move(log),
            platform::win32::Utf8ToWide(message).value_or(L"Low-latency audio failed."),
            L"GCLoader audio error"});
    } catch (...) { diagnostics::AbortProcess({}); }
}

class ProductionAudioObserver final : public IAudioEngineObserver {
public:
    explicit ProductionAudioObserver(AudioBackend backend) noexcept : backend_(backend) {}

    void StartupSucceeded(const EndpointInitialization& initialization) noexcept override
    {
        try
        {
            initialization_ = initialization;
            EmitInfo(startup_text(initialization, backend_).c_str());
        }
        catch (...)
        {
            EmitError("Audio startup diagnostics formatting failed");
        }
    }

    void RuntimeSummary(const AudioRuntimeCountersSnapshot& counters) noexcept override
    {
        try
        {
            EmitInfo((std::string{"WASAPI audio runtime summary "} + counters_text(counters)).c_str());
        }
        catch (...)
        {
            EmitError("WASAPI audio runtime summary formatting failed");
        }
    }

    void RuntimeFailed(const AudioFailure& failure,
                       const AudioRuntimeCountersSnapshot& counters) noexcept override
    {
        const auto message = failure.stage == AudioFailureStage::ChronicOutputGap
            ? kPacingFailureMessage : kFailureMessage;
        try
        {
            const EndpointInitialization unknown{};
            AbortAudio(failure_text("WASAPI audio runtime fatal",
                initialization_ ? *initialization_ : unknown, failure) + " " +
                counters_text(counters), message);
        }
        catch (...)
        {
            AbortAudio("WASAPI audio runtime fatal formatting failed", message);
        }
    }

private:
    const AudioBackend backend_;
    std::optional<EndpointInitialization> initialization_;
};
} // namespace

void ReportAudioConfig(AudioBackend backend, std::uint32_t buffer_ms)
{
    EmitInfo(audio_config_text(backend, buffer_ms).c_str());
}

void ReportAudioBufferHandoff(std::string_view stage, REFERENCE_TIME duration) noexcept
{
    try
    {
        EmitInfo(std::format(
            "WASAPI audio buffer handoff stage={} configured_buffer_ms={} "
            "configured_duration_100ns={}",
            stage, duration / kReferenceTimePerMillisecond, duration).c_str());
    }
    catch (...)
    {
        EmitError("WASAPI audio buffer handoff diagnostics formatting failed");
    }
}

std::shared_ptr<IAudioEngineObserver> MakeAudioObserver(AudioBackend backend)
{
    return std::make_shared<ProductionAudioObserver>(backend);
}

[[noreturn]] void AbortAudioBackendStartup(const AudioBackendStartupFailure& failure) noexcept
{
    try
    {
        AbortAudio(std::format("{} requested_backend={} active_backend=failed",
            failure_text("Audio startup fatal", failure.wasapi_failure.attempted,
                         failure.wasapi_failure.failure),
            AudioBackendName(failure.requested_backend)), kGenericStartupFailureMessage);
    }
    catch (...)
    {
        AbortAudio("Audio startup fatal formatting failed", kGenericStartupFailureMessage);
    }
}

[[noreturn]] void AbortAudioControllerAllocation() noexcept
{
    try {
        AbortAudio("Audio controller allocation fatal hresult=0x8007000E",
                   kGenericStartupFailureMessage);
    } catch (...) { diagnostics::AbortProcess({}); }
}
} // namespace gc::audio

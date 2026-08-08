#pragma once

#include "Audio/DirectSound/DirectSoundFacade.h"
#include "Audio/Mixer/AudioRenderCore.h"
#include "Audio/Wasapi/OutputPacingTracker.h"
#include "Audio/Wasapi/WasapiEndpoint.h"
#include "Audio/Wasapi/WasapiPresentedOutputClock.h"

#include <Windows.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <thread>
#include <vector>

namespace gc::audio {

class ExclusiveAudioEngine;
class IAudioEngineObserver;

namespace detail {

struct ExclusiveAudioEngineTiming;

std::unique_ptr<ExclusiveAudioEngine> StartExclusiveAudioEngineAndWait(
    std::unique_ptr<IWasapiApi>,
    std::shared_ptr<IAudioEngineObserver>,
    DWORD timeout_ms,
    REFERENCE_TIME configured_duration,
    std::shared_ptr<const ma_allocation_callbacks>,
    const ExclusiveAudioEngineTiming&,
    AudioStartupFailure*) noexcept;

} // namespace detail

struct AudioRuntimeCountersSnapshot {
    std::uint64_t render_callbacks{};
    std::uint64_t late_event_wakes{};
    std::uint64_t silence_fallbacks{};
    std::uint64_t pending_cursor_queries{};
    std::uint64_t unmapped_cursor_failures{};
    std::uint64_t confirmed_gap_events{};
    std::uint64_t skipped_output_frames{};
    std::uint64_t maximum_skipped_output_frames{};
    std::uint64_t chronic_pacing_failures{};
    std::int64_t current_submitted_lead_frames{};
    std::int64_t minimum_submitted_lead_frames{};
    std::uint64_t endpoint_hresult_failures{};
    MixerDiagnosticsSnapshot mixer{};
};

class IAudioEngineObserver {
public:
    virtual ~IAudioEngineObserver() = default;
    virtual void StartupSucceeded(
        const EndpointInitialization&) noexcept = 0;
    virtual void RuntimeSummary(
        const AudioRuntimeCountersSnapshot&) noexcept = 0;
    virtual void RuntimeFailed(
        const AudioFailure&,
        const AudioRuntimeCountersSnapshot&) noexcept = 0;
};

class ExclusiveAudioEngine final : public IAudioEngineServices {
public:
    ~ExclusiveAudioEngine();

    ExclusiveAudioEngine(const ExclusiveAudioEngine&) = delete;
    ExclusiveAudioEngine& operator=(const ExclusiveAudioEngine&) = delete;
    ExclusiveAudioEngine(ExclusiveAudioEngine&&) = delete;
    ExclusiveAudioEngine& operator=(ExclusiveAudioEngine&&) = delete;

    static std::unique_ptr<ExclusiveAudioEngine> StartAndWait(
        std::unique_ptr<IWasapiApi>,
        std::shared_ptr<IAudioEngineObserver>,
        DWORD timeout_ms,
        REFERENCE_TIME configured_duration,
        std::shared_ptr<const ma_allocation_callbacks> mixer_allocations,
        AudioStartupFailure*) noexcept;

    std::unique_ptr<MixerVoice> CreateVoice(
        const NormalizedSourceFormat&,
        std::shared_ptr<AudioSnapshot>,
        std::shared_ptr<AudioCursorTimeline>,
        VoiceUsage,
        ma_result*) noexcept override;
    std::optional<std::uint64_t> CurrentOutputFrame() noexcept override;
    std::uint32_t endpoint_buffer_frames() const noexcept override;
    std::uint32_t output_sample_rate() const noexcept override;
    void CountPendingCursorQuery() noexcept override;
    void CountUnmappedCursorFailure() noexcept override;

private:
    friend std::unique_ptr<ExclusiveAudioEngine>
        detail::StartExclusiveAudioEngineAndWait(
            std::unique_ptr<IWasapiApi>,
            std::shared_ptr<IAudioEngineObserver>,
            DWORD,
            REFERENCE_TIME,
            std::shared_ptr<const ma_allocation_callbacks>,
            const detail::ExclusiveAudioEngineTiming&,
            AudioStartupFailure*) noexcept;

    ExclusiveAudioEngine(
        std::unique_ptr<IWasapiApi>,
        std::shared_ptr<IAudioEngineObserver>,
        REFERENCE_TIME configured_duration,
        std::shared_ptr<const ma_allocation_callbacks>,
        DWORD summary_interval_ms) noexcept;

    bool CreateControlEvents() noexcept;
    bool StartThreads() noexcept;
    void AudioThreadMain() noexcept;
    void MonitorThreadMain() noexcept;
    void RenderLoop() noexcept;
    void CleanupEndpointOnAudioThread() noexcept;
    void SignalInitializationFailure(
        AudioFailure,
        EndpointInitialization) noexcept;
    void RecordRuntimeFailure(const AudioFailure&) noexcept;
    void RecordPacingDecision(
        const OutputPacingDecision&) noexcept;
    void CountLateWake(std::uint64_t qpc_100ns) noexcept;
    AudioRuntimeCountersSnapshot SnapshotCounters() const noexcept;
    bool ShutdownRequested() const noexcept;
    void CloseControlEvents() noexcept;

    std::unique_ptr<IWasapiApi> pending_api_;
    REFERENCE_TIME configured_duration_{};
    std::unique_ptr<WasapiEndpoint> endpoint_;
    std::shared_ptr<IAudioEngineObserver> observer_;
    std::shared_ptr<const ma_allocation_callbacks> mixer_allocations_;
    std::unique_ptr<AudioRenderCore> render_core_;
    WasapiPresentedOutputClock* presented_clock_{};
    std::vector<std::int16_t> pcm16_mix_;
    EndpointClockMapper clock_mapper_;
    std::optional<OutputPacingTracker> pacing_tracker_;
    std::thread audio_thread_;
    std::thread monitor_thread_;
    HANDLE initialization_event_{};
    HANDLE startup_reported_event_{};
    HANDLE fatal_event_{};
    HANDLE shutdown_event_{};
    HANDLE audio_exited_event_{};
    AudioStartupFailure startup_failure_{};
    EndpointInitialization initialization_{};
    std::atomic_bool initialization_succeeded_{};
    std::atomic_bool failure_claimed_{};
    std::atomic_uint32_t failure_stage_{};
    std::atomic_long failure_result_{S_OK};
    std::atomic_uint32_t endpoint_buffer_frames_{};
    std::atomic_uint32_t output_sample_rate_{};
    std::atomic_uint64_t render_callbacks_{};
    std::atomic_uint64_t late_event_wakes_{};
    std::atomic_uint64_t silence_fallbacks_{};
    std::atomic_uint64_t pending_cursor_queries_{};
    std::atomic_uint64_t unmapped_cursor_failures_{};
    std::atomic_uint64_t confirmed_gap_events_{};
    std::atomic_uint64_t skipped_output_frames_{};
    std::atomic_uint64_t maximum_skipped_output_frames_{};
    std::atomic_uint64_t chronic_pacing_failures_{};
    std::atomic_int64_t current_submitted_lead_frames_{};
    std::atomic_int64_t minimum_submitted_lead_frames_{};
    std::atomic_uint64_t endpoint_hresult_failures_{};
    std::uint64_t last_qpc_100ns_{};
    bool has_last_qpc_sample_{};
    REFERENCE_TIME actual_period_100ns_{};
    DWORD summary_interval_ms_{};
};

} // namespace gc::audio

#pragma once

#include "AudioCursorTimeline.h"
#include "DirectSoundFacade.h"
#include "WasapiEndpoint.h"

#include <Windows.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <thread>
#include <vector>

namespace gc::audio {

struct AudioRuntimeCountersSnapshot {
    std::uint64_t render_callbacks{};
    std::uint64_t late_event_wakes{};
    std::uint64_t silence_fallbacks{};
    std::uint64_t cursor_timeline_failures{};
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
        const ma_allocation_callbacks* mixer_allocations,
        AudioStartupFailure*) noexcept;

    std::unique_ptr<MixerVoice> CreateVoice(
        const NormalizedSourceFormat&,
        std::shared_ptr<AudioSnapshot>,
        std::shared_ptr<AudioCursorTimeline>,
        VoiceUsage,
        ma_result*) noexcept override;
    std::optional<std::uint64_t> CurrentOutputFrame() noexcept override;
    std::uint32_t endpoint_buffer_frames() const noexcept override;
    void CountCursorTimelineFailure() noexcept override;

private:
    ExclusiveAudioEngine(
        std::unique_ptr<IWasapiApi>,
        std::shared_ptr<IAudioEngineObserver>,
        const ma_allocation_callbacks*) noexcept;

    bool CreateControlEvents() noexcept;
    bool StartThreads() noexcept;
    void AudioThreadMain() noexcept;
    void MonitorThreadMain() noexcept;
    void RenderLoop() noexcept;
    void CleanupEndpointOnAudioThread() noexcept;
    void SignalInitializationFailure(
        const AudioFailure&,
        const EndpointInitialization&) noexcept;
    void RecordRuntimeFailure(const AudioFailure&) noexcept;
    void CountLateWake(std::uint64_t qpc_100ns) noexcept;
    AudioRuntimeCountersSnapshot SnapshotCounters() const noexcept;
    bool ShutdownRequested() const noexcept;
    void CloseControlEvents() noexcept;

    std::unique_ptr<IWasapiApi> pending_api_;
    std::unique_ptr<WasapiEndpoint> endpoint_;
    std::unique_ptr<MiniaudioMixer> mixer_;
    std::shared_ptr<IAudioEngineObserver> observer_;
    const ma_allocation_callbacks* mixer_allocations_{};
    std::vector<float> float_mix_;
    std::vector<std::int16_t> pcm16_mix_;
    EndpointClockMapper clock_mapper_;
    std::thread audio_thread_;
    std::thread monitor_thread_;
    HANDLE initialization_event_{};
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
    std::atomic_uint64_t submitted_frames_{};
    std::atomic_uint64_t render_callbacks_{};
    std::atomic_uint64_t late_event_wakes_{};
    std::atomic_uint64_t silence_fallbacks_{};
    std::atomic_uint64_t cursor_timeline_failures_{};
    std::atomic_uint64_t endpoint_hresult_failures_{};
    std::uint64_t last_qpc_100ns_{};
    REFERENCE_TIME actual_period_100ns_{};
};

} // namespace gc::audio

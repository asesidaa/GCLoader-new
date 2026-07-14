#pragma once

#include "AudioCursorTimeline.h"
#include "AudioSnapshot.h"
#include "WasapiAudioTypes.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>

namespace gc::audio {

namespace detail {

struct AudibleDrainRecord {
    std::uint64_t exclusive_end_output_frame{};
    std::uint64_t run_token{};
    std::uint64_t epoch{};
};

class AudibleDrainPublication {
public:
    // The render callback is the sole publisher; readers never wait.
    void Publish(const AudibleDrainRecord&) noexcept;
    std::optional<std::uint64_t> Observe(
        std::uint64_t current_draining_run,
        std::uint64_t latest_accepted_epoch) const noexcept;

private:
    std::atomic_uint64_t sequence_{};
    std::atomic_uint64_t exclusive_end_output_frame_{};
    std::atomic_uint64_t run_token_{};
    std::atomic_uint64_t epoch_{};
};

struct VoicePlayTransition {
    std::uint64_t run_token{};
    bool needs_active_increment{};
};

class VoicePlaybackStateMachine {
public:
    VoicePlayTransition BeginPlay() noexcept;
    void CommitPlay(std::uint64_t run_token) noexcept;
    std::uint64_t BeginStop() noexcept;
    void CompleteStop(std::uint64_t run_token) noexcept;
    bool BeginEnd(std::uint64_t run_token) noexcept;
    void CompleteEnd(std::uint64_t run_token) noexcept;
    std::uint64_t CapturePlayingRun() const noexcept;
    std::uint64_t CaptureDrainingRun() const noexcept;
    bool playing() const noexcept;

private:
    std::atomic_uint64_t packed_state_{};
};

} // namespace detail

struct MixerDiagnosticsSnapshot {
    std::uint64_t native_rate_buffers{};
    std::uint64_t sample_format_converted_buffers{};
    std::uint64_t sample_rate_converted_buffers{};
    std::uint64_t native_gameplay_buffers{};
    std::uint32_t active_voices{};
    std::uint32_t maximum_simultaneous_voices{};
};

struct MixerRenderResult {
    ma_result result;
    std::uint64_t frames_read;
};

struct MixerRenderTimeline {
    std::uint64_t output_frame_begin{};
    std::uint64_t discontinuity_frames{};
};

enum class VoiceUsage : std::uint8_t {
    General,
    GameplayNativeCandidate,
};

struct MixerVoiceState;
struct MiniaudioMixerState;

class MixerVoice final {
public:
    ~MixerVoice();

    MixerVoice(const MixerVoice&) = delete;
    MixerVoice& operator=(const MixerVoice&) = delete;
    MixerVoice(MixerVoice&&) = delete;
    MixerVoice& operator=(MixerVoice&&) = delete;

    HRESULT Play(bool looping, std::uint64_t epoch) noexcept;
    void Stop() noexcept;
    HRESULT Seek(std::uint64_t source_frame, std::uint64_t epoch) noexcept;
    void SetGain(float gain) noexcept;
    bool playing() const noexcept;
    bool looping() const noexcept;
    bool at_end() const noexcept;
    std::optional<std::uint64_t>
        audible_until_output_frame() const noexcept;

private:
    friend class MiniaudioMixer;

    explicit MixerVoice(std::unique_ptr<MixerVoiceState>) noexcept;

    std::unique_ptr<MixerVoiceState> state_;
};

class MiniaudioMixer final {
public:
    ~MiniaudioMixer();

    MiniaudioMixer(const MiniaudioMixer&) = delete;
    MiniaudioMixer& operator=(const MiniaudioMixer&) = delete;
    MiniaudioMixer(MiniaudioMixer&&) = delete;
    MiniaudioMixer& operator=(MiniaudioMixer&&) = delete;

    static std::unique_ptr<MiniaudioMixer> Create(
        std::uint32_t period_frames,
        const ma_allocation_callbacks* callbacks,
        ma_result* result) noexcept;
    static std::unique_ptr<MiniaudioMixer> Create(
        std::uint32_t period_frames,
        std::shared_ptr<const ma_allocation_callbacks> callbacks,
        ma_result* result) noexcept;
    std::unique_ptr<MixerVoice> CreateVoice(
        const NormalizedSourceFormat& format,
        std::shared_ptr<AudioSnapshot> snapshot,
        std::shared_ptr<AudioCursorTimeline> timeline,
        VoiceUsage usage,
        ma_result* result) noexcept;
    MixerRenderResult Render(
        std::span<float> stereo,
        const MixerRenderTimeline& timeline) noexcept;
    MixerDiagnosticsSnapshot diagnostics() const noexcept;

private:
    static std::unique_ptr<MiniaudioMixer> CreateWithOwner(
        std::uint32_t period_frames,
        const ma_allocation_callbacks* callbacks,
        std::shared_ptr<const ma_allocation_callbacks> callback_owner,
        ma_result* result) noexcept;
    explicit MiniaudioMixer(
        std::shared_ptr<MiniaudioMixerState>) noexcept;

    std::shared_ptr<MiniaudioMixerState> state_;
};

void ConvertFloatToPcm16(
    std::span<const float> input,
    std::span<std::int16_t> output) noexcept;

} // namespace gc::audio

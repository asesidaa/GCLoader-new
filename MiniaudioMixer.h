#pragma once

#include "AudioCursorTimeline.h"
#include "AudioSnapshot.h"
#include "WasapiAudioTypes.h"

#include <cstdint>
#include <memory>
#include <span>

namespace gc::audio {

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
    std::unique_ptr<MixerVoice> CreateVoice(
        const NormalizedSourceFormat& format,
        AudioSnapshot& snapshot,
        AudioCursorTimeline& timeline,
        VoiceUsage usage,
        ma_result* result) noexcept;
    MixerRenderResult Render(
        std::span<float> stereo,
        std::uint64_t output_frame_begin) noexcept;
    MixerDiagnosticsSnapshot diagnostics() const noexcept;

private:
    explicit MiniaudioMixer(
        std::unique_ptr<MiniaudioMixerState>) noexcept;

    std::unique_ptr<MiniaudioMixerState> state_;
};

void ConvertFloatToPcm16(
    std::span<const float> input,
    std::span<std::int16_t> output) noexcept;

} // namespace gc::audio

#pragma once
// SPDX-License-Identifier: CC0-1.0

#include "Audio/Mixer/MiniaudioMixer.h"
#include "Audio/Mixer/PresentedOutputClock.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace gc::audio {

enum class AudioRenderSilenceReason : std::uint8_t {
    none,
    no_active_voice,
    active_short_read,
    mixer_error,
    render_contract_error,
};

struct AudioRenderBlock
{
    std::span<const float> interleaved_stereo;
    ma_result mixer_result{MA_ERROR};
    std::uint64_t frames_read{};
    std::uint32_t active_voices{};
    std::uint32_t missing_frames{};
    AudioRenderSilenceReason silence_reason{
        AudioRenderSilenceReason::none};
    bool silence_substituted{};
};

class AudioRenderCore final
{
public:
    static std::unique_ptr<AudioRenderCore> Create(
        std::uint32_t period_frames,
        std::uint32_t output_sample_rate,
        std::shared_ptr<const ma_allocation_callbacks>,
        std::unique_ptr<IPresentedOutputClock>,
        ma_result*) noexcept;

    std::unique_ptr<MixerVoice> CreateVoice(
        const NormalizedSourceFormat&,
        std::shared_ptr<AudioSnapshot>,
        std::shared_ptr<AudioCursorTimeline>,
        VoiceUsage,
        ma_result*) noexcept;
    [[nodiscard]] AudioRenderBlock Render(
        const MixerRenderTimeline&) noexcept;
    [[nodiscard]] std::optional<std::uint64_t>
        CurrentOutputFrame() noexcept;
    void InvalidatePresentationClock() noexcept;
    [[nodiscard]] MixerDiagnosticsSnapshot diagnostics() const noexcept;
    [[nodiscard]] std::uint32_t period_frames() const noexcept;
    [[nodiscard]] std::uint32_t output_sample_rate() const noexcept;

private:
    AudioRenderCore(
        std::uint32_t period_frames,
        std::uint32_t output_sample_rate,
        std::unique_ptr<MiniaudioMixer>,
        std::vector<float>,
        std::unique_ptr<IPresentedOutputClock>) noexcept;

    std::uint32_t period_frames_{};
    std::uint32_t output_sample_rate_{};
    std::unique_ptr<MiniaudioMixer> mixer_;
    std::vector<float> float_mix_;
    std::unique_ptr<IPresentedOutputClock> presented_clock_;
};

} // namespace gc::audio

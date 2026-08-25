#pragma once
// SPDX-License-Identifier: CC0-1.0

#include "Audio/Mixer/AudioRenderCore.h"

#include <algorithm>
// ReSharper disable once CppUnusedIncludeDirective
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace gc::audio::detail {

constexpr bool CanAddressAudioRenderSamples(
    std::uint32_t frames) noexcept
{
    return frames <=
        (std::numeric_limits<std::size_t>::max)() / kOutputChannels;
}

inline AudioRenderBlock FinalizeAudioRenderBlock(
    std::span<float> stereo,
    std::uint32_t expected_frames,
    // This small result aggregate is intentionally passed by value.
    // ReSharper disable once CppPassValueParameterByConstReference
    MixerRenderResult rendered) noexcept
{
    AudioRenderSilenceReason reason{};
    const bool valid_contract =
        CanAddressAudioRenderSamples(expected_frames) &&
        stereo.size() ==
            static_cast<std::size_t>(expected_frames) * kOutputChannels &&
        rendered.frames_read <= expected_frames;
    if (!valid_contract)
    {
        reason = AudioRenderSilenceReason::render_contract_error;
    }
    else if (rendered.result != MA_SUCCESS)
    {
        reason = AudioRenderSilenceReason::mixer_error;
    }
    else if (rendered.frames_read < expected_frames)
    {
        reason = rendered.active_voices == 0
            ? AudioRenderSilenceReason::no_active_voice
            : AudioRenderSilenceReason::active_short_read;
    }

    const auto missing_frames =
        valid_contract && rendered.result == MA_SUCCESS &&
            rendered.frames_read < expected_frames
        ? static_cast<std::uint32_t>(
              expected_frames - rendered.frames_read)
        : 0U;
    if (reason != AudioRenderSilenceReason::none)
    {
        std::ranges::fill(stereo, 0.0F);
    }
    return {
        .interleaved_stereo = std::span<const float>{stereo},
        .mixer_result = rendered.result,
        .frames_read = rendered.frames_read,
        .active_voices = rendered.active_voices,
        .missing_frames = missing_frames,
        .silence_reason = reason,
        .silence_substituted =
            reason != AudioRenderSilenceReason::none,
    };
}

} // namespace gc::audio::detail

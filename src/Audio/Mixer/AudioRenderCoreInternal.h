#pragma once
// SPDX-License-Identifier: CC0-1.0

#include "Audio/Mixer/AudioRenderCore.h"

#include <algorithm>
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
    MixerRenderResult rendered) noexcept
{
    const bool complete =
        CanAddressAudioRenderSamples(expected_frames) &&
        stereo.size() ==
            static_cast<std::size_t>(expected_frames) * kOutputChannels &&
        rendered.result == MA_SUCCESS &&
        rendered.frames_read == expected_frames;
    if (!complete)
    {
        std::fill(stereo.begin(), stereo.end(), 0.0F);
    }
    return {
        std::span<const float>{stereo},
        rendered.result,
        !complete,
    };
}

} // namespace gc::audio::detail

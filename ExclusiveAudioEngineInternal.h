#pragma once

#include "MiniaudioMixer.h"

#include <Windows.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace gc::audio::detail {

inline constexpr DWORD kExclusiveAudioMaxStartupTimeoutMs = 10'000;
inline constexpr DWORD kExclusiveAudioSummaryIntervalMs = 30'000;

struct ExclusiveAudioEngineTiming {
    DWORD summary_interval_ms{kExclusiveAudioSummaryIntervalMs};
};

constexpr DWORD ClampExclusiveAudioStartupTimeout(DWORD timeout_ms) noexcept {
    return std::min(timeout_ms, kExclusiveAudioMaxStartupTimeoutMs);
}

constexpr bool CanAddressOutputSamples(std::uint32_t frames) noexcept {
    return frames <=
        std::numeric_limits<std::size_t>::max() / kOutputChannels;
}

inline bool FinalizeMixerRenderBlock(
    std::span<float> stereo,
    std::uint32_t expected_frames,
    MixerRenderResult rendered) noexcept {
    if (rendered.result != MA_SUCCESS) {
        std::fill(stereo.begin(), stereo.end(), 0.0F);
        return true;
    }
    if (rendered.frames_read == expected_frames) {
        return false;
    }

    const auto bounded_frames = std::min<std::uint64_t>(
        rendered.frames_read,
        expected_frames);
    const auto first_missing = std::min(
        stereo.size(),
        static_cast<std::size_t>(bounded_frames) * kOutputChannels);
    std::fill(
        stereo.begin() + static_cast<std::ptrdiff_t>(first_missing),
        stereo.end(),
        0.0F);
    return true;
}

} // namespace gc::audio::detail

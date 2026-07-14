#pragma once

#include <Windows.h>
#include <dsound.h>
#include <mmreg.h>

#include <cstdint>

#include "miniaudio.h"

namespace gc::audio {

inline constexpr std::uint32_t kOutputSampleRate = 44100;
inline constexpr std::uint16_t kOutputChannels = 2;
inline constexpr std::uint16_t kOutputBitsPerSample = 16;
inline constexpr std::uint16_t kOutputBlockAlign = 4;
inline constexpr std::uint32_t kOutputAverageBytesPerSecond = 176400;
inline constexpr REFERENCE_TIME kReferenceTimesPerSecond = 10'000'000;

enum class SourceSampleFormat : std::uint8_t { Pcm16, Pcm24 };
enum class ConversionPath : std::uint8_t {
    NativeRatePcm16,
    NativeRatePcm24,
    LinearResampledPcm16,
    LinearResampledPcm24,
};

struct NormalizedSourceFormat {
    WAVEFORMATEXTENSIBLE wave{};
    std::uint32_t wave_format_size{};
    SourceSampleFormat sample_format{};
    ConversionPath path{};
    ma_format miniaudio_format{ma_format_unknown};
    std::uint16_t channels{};
    std::uint16_t bits_per_sample{};
    std::uint16_t block_align{};
    std::uint32_t sample_rate{};
    std::uint32_t average_bytes_per_second{};
    bool sample_format_converted{};
    bool sample_rate_converted{};
    bool native_rate_pcm16{};
};

HRESULT NormalizeSourceFormat(
    const WAVEFORMATEX*, NormalizedSourceFormat*) noexcept;
bool IsExactOutputFormat(const WAVEFORMATEX&) noexcept;
float DirectSoundVolumeToLinearGain(LONG) noexcept;
REFERENCE_TIME FramesToReferenceTime(
    std::uint64_t, std::uint32_t) noexcept;
std::uint64_t ReferenceTimeToFramesFloor(
    REFERENCE_TIME, std::uint32_t) noexcept;
std::uint64_t ReferenceTimeToFramesCeil(
    REFERENCE_TIME, std::uint32_t) noexcept;

} // namespace gc::audio

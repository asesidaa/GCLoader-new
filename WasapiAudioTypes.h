#pragma once

#include <Windows.h>
#include <dsound.h>
#include <mmreg.h>

#include <cstdint>

#include "miniaudio.h"

namespace gc::audio {

inline constexpr std::uint32_t kGamePrimarySampleRate = 44'100;
inline constexpr std::uint32_t kFallbackEndpointSampleRate = 48'000;
inline constexpr std::uint16_t kOutputChannels = 2;
inline constexpr std::uint16_t kOutputBitsPerSample = 16;
inline constexpr std::uint16_t kOutputBlockAlign = 4;
inline constexpr std::uint32_t kGamePrimaryAverageBytesPerSecond =
    kGamePrimarySampleRate * kOutputBlockAlign;
inline constexpr REFERENCE_TIME kReferenceTimesPerSecond = 10'000'000;

enum class SourceSampleFormat : std::uint8_t { Pcm16, Pcm24 };
enum class EndpointFormatKind : std::uint8_t {
    LegacyPcm,
    ExtensiblePcm,
};

struct EndpointPcmFormat {
    WAVEFORMATEXTENSIBLE storage{};
    std::uint32_t size{};
    EndpointFormatKind kind{EndpointFormatKind::LegacyPcm};

    bool valid() const noexcept { return size != 0; }
    const WAVEFORMATEX& wave_format() const noexcept {
        return storage.Format;
    }
};

struct NormalizedSourceFormat {
    WAVEFORMATEXTENSIBLE wave{};
    std::uint32_t wave_format_size{};
    SourceSampleFormat sample_format{};
    ma_format miniaudio_format{ma_format_unknown};
    std::uint16_t channels{};
    std::uint16_t bits_per_sample{};
    std::uint16_t block_align{};
    std::uint32_t sample_rate{};
    std::uint32_t average_bytes_per_second{};
    bool sample_format_converted{};
    bool game_native_pcm16{};
};

HRESULT NormalizeSourceFormat(
    const WAVEFORMATEX*, NormalizedSourceFormat*) noexcept;
bool IsSupportedEndpointSampleRate(std::uint32_t) noexcept;
EndpointPcmFormat MakeEndpointPcm16Format(
    std::uint32_t, EndpointFormatKind) noexcept;
bool IsExactGamePrimaryFormat(const WAVEFORMATEX&) noexcept;
float DirectSoundVolumeToLinearGain(LONG) noexcept;
REFERENCE_TIME FramesToReferenceTime(
    std::uint64_t, std::uint32_t) noexcept;
std::uint64_t ReferenceTimeToFramesFloor(
    REFERENCE_TIME, std::uint32_t) noexcept;
std::uint64_t ReferenceTimeToFramesCeil(
    REFERENCE_TIME, std::uint32_t) noexcept;

} // namespace gc::audio

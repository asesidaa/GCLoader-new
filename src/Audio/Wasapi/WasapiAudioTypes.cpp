#include "Audio/Wasapi/WasapiAudioTypes.h"

#include <ks.h>
#include <ksmedia.h>

#include <algorithm>
#include <cstring>

namespace gc::audio {
namespace {

bool IsObservedChannelCount(WORD channels) noexcept {
    return channels == 1 || channels == 2;
}

bool IsObservedSampleRate(DWORD rate) noexcept {
    return rate == 22050 || rate == kGamePrimarySampleRate ||
        rate == kFallbackEndpointSampleRate;
}

bool IsObservedBitsPerSample(WORD bits) noexcept {
    return bits == 16 || bits == 24;
}

bool IsCanonicalChannelMask(WORD channels, DWORD mask) noexcept {
    if (mask == 0) {
        return true;
    }

    if (channels == 1) {
        return mask == SPEAKER_FRONT_CENTER;
    }

    return channels == 2 &&
        mask == (SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT);
}

} // namespace

bool IsSupportedEndpointSampleRate(std::uint32_t rate) noexcept {
    return rate == kGamePrimarySampleRate ||
        rate == kFallbackEndpointSampleRate;
}

EndpointPcmFormat MakeEndpointPcm16Format(
    std::uint32_t sample_rate,
    EndpointFormatKind kind) noexcept {
    if (!IsSupportedEndpointSampleRate(sample_rate)) {
        return {};
    }

    EndpointPcmFormat result{};
    result.kind = kind;
    auto& format = result.storage.Format;
    format.wFormatTag = kind == EndpointFormatKind::LegacyPcm
        ? WAVE_FORMAT_PCM
        : WAVE_FORMAT_EXTENSIBLE;
    format.nChannels = kOutputChannels;
    format.nSamplesPerSec = sample_rate;
    format.nAvgBytesPerSec = sample_rate * kOutputBlockAlign;
    format.nBlockAlign = kOutputBlockAlign;
    format.wBitsPerSample = kOutputBitsPerSample;
    if (kind == EndpointFormatKind::LegacyPcm) {
        format.cbSize = 0;
        result.size = sizeof(WAVEFORMATEX);
        return result;
    }

    format.cbSize = static_cast<WORD>(
        sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX));
    result.storage.Samples.wValidBitsPerSample = kOutputBitsPerSample;
    result.storage.dwChannelMask =
        SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
    result.storage.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
    result.size = sizeof(WAVEFORMATEXTENSIBLE);
    return result;
}

HRESULT NormalizeSourceFormat(
    const WAVEFORMATEX* source,
    NormalizedSourceFormat* normalized) noexcept {
    if (source == nullptr || normalized == nullptr) {
        return DSERR_INVALIDPARAM;
    }

    const bool extensible = source->wFormatTag == WAVE_FORMAT_EXTENSIBLE;
    if (source->wFormatTag != WAVE_FORMAT_PCM && !extensible) {
        return DSERR_BADFORMAT;
    }

    if (extensible) {
        if (source->cbSize != 22) {
            return DSERR_BADFORMAT;
        }

        const auto* extended =
            reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(source);
        if (!IsEqualGUID(extended->SubFormat, KSDATAFORMAT_SUBTYPE_PCM) ||
            extended->Samples.wValidBitsPerSample !=
                source->wBitsPerSample ||
            !IsCanonicalChannelMask(
                source->nChannels,
                extended->dwChannelMask)) {
            return DSERR_BADFORMAT;
        }
    }

    if (!IsObservedChannelCount(source->nChannels) ||
        !IsObservedSampleRate(source->nSamplesPerSec) ||
        !IsObservedBitsPerSample(source->wBitsPerSample)) {
        return DSERR_BADFORMAT;
    }

    const auto expected_block_align = static_cast<WORD>(
        source->nChannels * (source->wBitsPerSample / 8));
    const auto expected_average_bytes_per_second =
        source->nSamplesPerSec * expected_block_align;
    if (source->nBlockAlign != expected_block_align ||
        source->nAvgBytesPerSec != expected_average_bytes_per_second) {
        return DSERR_BADFORMAT;
    }

    NormalizedSourceFormat result{};
    const auto wave_format_size = extensible
        ? sizeof(WAVEFORMATEXTENSIBLE)
        : sizeof(WAVEFORMATEX);
    std::memcpy(&result.wave, source, wave_format_size);
    result.wave_format_size =
        static_cast<std::uint32_t>(wave_format_size);
    result.sample_format = source->wBitsPerSample == 16
        ? SourceSampleFormat::Pcm16
        : SourceSampleFormat::Pcm24;
    result.miniaudio_format = source->wBitsPerSample == 16
        ? ma_format_s16
        : ma_format_s24;
    result.channels = source->nChannels;
    result.bits_per_sample = source->wBitsPerSample;
    result.block_align = source->nBlockAlign;
    result.sample_rate = source->nSamplesPerSec;
    result.average_bytes_per_second = source->nAvgBytesPerSec;
    result.sample_format_converted = true;
    result.game_native_pcm16 =
        source->nSamplesPerSec == kGamePrimarySampleRate &&
        source->wBitsPerSample == 16;

    *normalized = result;
    return DS_OK;
}

bool IsExactGamePrimaryFormat(const WAVEFORMATEX& format) noexcept {
    return format.wFormatTag == WAVE_FORMAT_PCM &&
        format.nChannels == kOutputChannels &&
        format.nSamplesPerSec == kGamePrimarySampleRate &&
        format.wBitsPerSample == kOutputBitsPerSample &&
        format.nBlockAlign == kOutputBlockAlign &&
        format.nAvgBytesPerSec == kGamePrimaryAverageBytesPerSecond &&
        (format.cbSize == 0 || format.cbSize == sizeof(WAVEFORMATEX));
}

float DirectSoundVolumeToLinearGain(LONG volume) noexcept {
    const auto clamped = std::clamp<LONG>(
        volume, DSBVOLUME_MIN, DSBVOLUME_MAX);
    return ma_volume_db_to_linear(
        static_cast<float>(clamped) / 100.0F);
}

REFERENCE_TIME FramesToReferenceTime(
    std::uint64_t frames,
    std::uint32_t rate) noexcept {
    return rate == 0 ? 0 : static_cast<REFERENCE_TIME>(
        (frames * kReferenceTimesPerSecond + rate / 2) / rate);
}

std::uint64_t ReferenceTimeToFramesFloor(
    REFERENCE_TIME duration,
    std::uint32_t rate) noexcept {
    if (duration <= 0 || rate == 0) {
        return 0;
    }
    const auto value = static_cast<std::uint64_t>(duration);
    return value / kReferenceTimesPerSecond * rate +
        value % kReferenceTimesPerSecond * rate /
            kReferenceTimesPerSecond;
}

std::uint64_t ReferenceTimeToFramesCeil(
    REFERENCE_TIME duration,
    std::uint32_t rate) noexcept {
    return duration <= 0 || rate == 0 ? 0 :
        (static_cast<std::uint64_t>(duration) * rate +
         kReferenceTimesPerSecond - 1) /
        kReferenceTimesPerSecond;
}

} // namespace gc::audio

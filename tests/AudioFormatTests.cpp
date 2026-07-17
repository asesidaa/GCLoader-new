#include "WasapiAudioTypes.h"

#include <ks.h>
#include <ksmedia.h>

#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>

namespace {

WAVEFORMATEX pcm(WORD channels, DWORD rate, WORD bits) {
    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = channels;
    format.nSamplesPerSec = rate;
    format.wBitsPerSample = bits;
    format.nBlockAlign = static_cast<WORD>(channels * bits / 8);
    format.nAvgBytesPerSec = rate * format.nBlockAlign;
    return format;
}

WAVEFORMATEXTENSIBLE extensible_pcm(
    WORD channels,
    DWORD rate,
    WORD bits,
    WORD valid_bits,
    DWORD channel_mask) {
    WAVEFORMATEXTENSIBLE format{};
    format.Format = pcm(channels, rate, bits);
    format.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    format.Format.cbSize = 22;
    format.Samples.wValidBitsPerSample = valid_bits;
    format.dwChannelMask = channel_mask;
    format.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
    return format;
}

int expect(bool condition, std::string_view name) {
    if (condition) {
        return 0;
    }

    std::cerr << "Expected " << name << "\n";
    return 1;
}

} // namespace

int main() {
    int failures = 0;

    for (const DWORD rate : {22050UL, 44100UL, 48000UL}) {
        for (const WORD channels : {WORD{1}, WORD{2}}) {
            for (const WORD bits : {WORD{16}, WORD{24}}) {
                const auto source = pcm(channels, rate, bits);
                gc::audio::NormalizedSourceFormat normalized{};
                const std::string case_name =
                    std::to_string(rate) + " Hz, " +
                    std::to_string(channels) + " channel, PCM" +
                    std::to_string(bits);

                failures += expect(
                    gc::audio::NormalizeSourceFormat(&source, &normalized) ==
                        DS_OK,
                    case_name + " observed PCM accepted");
                failures += expect(
                    normalized.sample_rate_converted ==
                        (rate != gc::audio::kGamePrimarySampleRate),
                    case_name + " rate conversion classification");
                failures += expect(
                    normalized.game_native_pcm16 ==
                        (rate == gc::audio::kGamePrimarySampleRate &&
                         bits == 16),
                    case_name + " game-native PCM16 classification");
                failures += expect(
                    normalized.sample_format_converted,
                    case_name + " integer-to-float classification");
                failures += expect(
                    normalized.sample_format ==
                        (bits == 16
                             ? gc::audio::SourceSampleFormat::Pcm16
                             : gc::audio::SourceSampleFormat::Pcm24),
                    case_name + " source sample format");
                failures += expect(
                    normalized.miniaudio_format ==
                        (bits == 16 ? ma_format_s16 : ma_format_s24),
                    case_name + " miniaudio format");
                failures += expect(
                    normalized.wave_format_size == sizeof(WAVEFORMATEX),
                    case_name + " legacy format copy size");
                failures += expect(
                    normalized.channels == channels &&
                        normalized.sample_rate == rate &&
                        normalized.bits_per_sample == bits &&
                        normalized.block_align == source.nBlockAlign &&
                        normalized.average_bytes_per_second ==
                            source.nAvgBytesPerSec,
                    case_name + " normalized scalar fields");
            }
        }
    }

    auto extensible = extensible_pcm(
        2,
        48000,
        24,
        24,
        SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT);
    gc::audio::NormalizedSourceFormat normalized_extensible{};
    failures += expect(
        gc::audio::NormalizeSourceFormat(
            &extensible.Format,
            &normalized_extensible) == DS_OK,
        "valid extensible stereo PCM24 accepted");
    failures += expect(
        normalized_extensible.wave_format_size ==
            sizeof(WAVEFORMATEXTENSIBLE),
        "extensible format copy size");
    failures += expect(
        normalized_extensible.wave.dwChannelMask ==
                (SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT) &&
            IsEqualGUID(
                normalized_extensible.wave.SubFormat,
                KSDATAFORMAT_SUBTYPE_PCM),
        "extensible fields preserved");
    const auto valid_source = pcm(2, 44100, 16);
    failures += expect(
        gc::audio::NormalizeSourceFormat(nullptr, &normalized_extensible) ==
            DSERR_INVALIDPARAM,
        "null source rejected as invalid parameter");
    failures += expect(
        gc::audio::NormalizeSourceFormat(&valid_source, nullptr) ==
            DSERR_INVALIDPARAM,
        "null destination rejected as invalid parameter");

    auto three_channels = pcm(3, 44100, 16);
    failures += expect(
        gc::audio::NormalizeSourceFormat(
            &three_channels,
            &normalized_extensible) == DSERR_BADFORMAT,
        "three-channel source rejected");

    auto unsupported_rate = pcm(2, 32000, 16);
    failures += expect(
        gc::audio::NormalizeSourceFormat(
            &unsupported_rate,
            &normalized_extensible) == DSERR_BADFORMAT,
        "32 kHz source rejected");

    auto unsupported_bits = pcm(2, 44100, 32);
    failures += expect(
        gc::audio::NormalizeSourceFormat(
            &unsupported_bits,
            &normalized_extensible) == DSERR_BADFORMAT,
        "32-bit integer source rejected");

    auto bad_block_alignment = pcm(2, 44100, 16);
    ++bad_block_alignment.nBlockAlign;
    failures += expect(
        gc::audio::NormalizeSourceFormat(
            &bad_block_alignment,
            &normalized_extensible) == DSERR_BADFORMAT,
        "bad block alignment rejected");

    auto bad_average_rate = pcm(2, 44100, 16);
    ++bad_average_rate.nAvgBytesPerSec;
    failures += expect(
        gc::audio::NormalizeSourceFormat(
            &bad_average_rate,
            &normalized_extensible) == DSERR_BADFORMAT,
        "bad average byte rate rejected");

    auto float_subtype = extensible_pcm(
        2,
        44100,
        24,
        24,
        SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT);
    float_subtype.SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
    failures += expect(
        gc::audio::NormalizeSourceFormat(
            &float_subtype.Format,
            &normalized_extensible) == DSERR_BADFORMAT,
        "IEEE-float subtype rejected");

    auto mismatched_valid_bits = extensible_pcm(
        2,
        44100,
        24,
        20,
        SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT);
    failures += expect(
        gc::audio::NormalizeSourceFormat(
            &mismatched_valid_bits.Format,
            &normalized_extensible) == DSERR_BADFORMAT,
        "mismatched extensible valid bits rejected");

    auto noncanonical_mask = extensible_pcm(
        2,
        44100,
        24,
        24,
        SPEAKER_FRONT_CENTER);
    failures += expect(
        gc::audio::NormalizeSourceFormat(
            &noncanonical_mask.Format,
            &normalized_extensible) == DSERR_BADFORMAT,
        "noncanonical nonzero channel mask rejected");

    auto bad_extensible_size = extensible;
    bad_extensible_size.Format.cbSize = 20;
    failures += expect(
        gc::audio::NormalizeSourceFormat(
            &bad_extensible_size.Format,
            &normalized_extensible) == DSERR_BADFORMAT,
        "noncanonical extensible size rejected");

    auto legacy_float = pcm(2, 44100, 16);
    legacy_float.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
    failures += expect(
        gc::audio::NormalizeSourceFormat(
            &legacy_float,
            &normalized_extensible) == DSERR_BADFORMAT,
        "legacy IEEE-float tag rejected");

    failures += expect(
        gc::audio::IsExactGamePrimaryFormat(valid_source),
        "exact PCM16 stereo 44.1 kHz game primary accepted");
    const auto non_output_rate = pcm(2, 48000, 16);
    failures += expect(
        !gc::audio::IsExactGamePrimaryFormat(non_output_rate),
        "48 kHz game primary rejected");
    const auto mono_output = pcm(1, 44100, 16);
    failures += expect(
        !gc::audio::IsExactGamePrimaryFormat(mono_output),
        "mono game primary rejected");
    failures += expect(
        !gc::audio::IsExactGamePrimaryFormat(legacy_float),
        "float game primary rejected");

    for (const DWORD rate : {
             gc::audio::kGamePrimarySampleRate,
             gc::audio::kFallbackEndpointSampleRate}) {
        for (const auto kind : {
                 gc::audio::EndpointFormatKind::LegacyPcm,
                 gc::audio::EndpointFormatKind::ExtensiblePcm}) {
            const auto endpoint = gc::audio::MakeEndpointPcm16Format(
                rate,
                kind);
            const auto& wave = endpoint.wave_format();
            const bool extensible_kind =
                kind == gc::audio::EndpointFormatKind::ExtensiblePcm;
            failures += expect(
                endpoint.valid() &&
                    endpoint.size ==
                        (extensible_kind
                             ? sizeof(WAVEFORMATEXTENSIBLE)
                             : sizeof(WAVEFORMATEX)) &&
                    wave.wFormatTag ==
                        (extensible_kind
                             ? WAVE_FORMAT_EXTENSIBLE
                             : WAVE_FORMAT_PCM) &&
                    wave.nChannels == gc::audio::kOutputChannels &&
                    wave.nSamplesPerSec == rate &&
                    wave.wBitsPerSample ==
                        gc::audio::kOutputBitsPerSample &&
                    wave.nBlockAlign == gc::audio::kOutputBlockAlign &&
                    wave.nAvgBytesPerSec ==
                        rate * gc::audio::kOutputBlockAlign &&
                    wave.cbSize == (extensible_kind ? 22 : 0),
                "canonical endpoint PCM16 scalar fields");
            if (extensible_kind) {
                failures += expect(
                    endpoint.storage.Samples.wValidBitsPerSample == 16 &&
                        endpoint.storage.dwChannelMask ==
                            (SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT) &&
                        IsEqualGUID(
                            endpoint.storage.SubFormat,
                            KSDATAFORMAT_SUBTYPE_PCM),
                    "canonical extensible endpoint PCM16 fields");
            }
        }
    }
    failures += expect(
        !gc::audio::MakeEndpointPcm16Format(
             32000,
             gc::audio::EndpointFormatKind::LegacyPcm).valid(),
        "unsupported endpoint rate rejected");

    failures += expect(
        gc::audio::ReferenceTimeToFramesCeil(30'000, 44100) == 133,
        "3 ms is 133 whole frames");
    failures += expect(
        gc::audio::ReferenceTimeToFramesFloor(30'000, 44100) == 132 &&
            gc::audio::ReferenceTimeToFramesFloor(100'000, 44100) == 441,
        "reference time floor preserves ordinary frame bounds");
    const auto maximum_duration =
        std::numeric_limits<REFERENCE_TIME>::max();
    const auto expected_maximum_floor =
        static_cast<std::uint64_t>(
            maximum_duration / gc::audio::kReferenceTimesPerSecond) *
            44100 +
        static_cast<std::uint64_t>(
            maximum_duration % gc::audio::kReferenceTimesPerSecond) *
            44100 / gc::audio::kReferenceTimesPerSecond;
    failures += expect(
        gc::audio::ReferenceTimeToFramesFloor(maximum_duration, 44100) ==
            expected_maximum_floor,
        "reference time floor avoids multiplication overflow");
    failures += expect(
        gc::audio::FramesToReferenceTime(133, 44100) == 30'159,
        "133 frames uses documented nearest hns value");
    failures += expect(
        gc::audio::ReferenceTimeToFramesCeil(-1, 44100) == 0 &&
            gc::audio::ReferenceTimeToFramesCeil(30'000, 0) == 0 &&
            gc::audio::ReferenceTimeToFramesFloor(-1, 44100) == 0 &&
            gc::audio::ReferenceTimeToFramesFloor(30'000, 0) == 0 &&
            gc::audio::FramesToReferenceTime(133, 0) == 0,
        "invalid duration and rate arithmetic returns zero");
    failures += expect(
        std::abs(gc::audio::DirectSoundVolumeToLinearGain(-600) -
                 std::pow(10.0F, -6.0F / 20.0F)) < 0.000001F,
        "DirectSound hundredths of dB conversion");
    failures += expect(
        gc::audio::DirectSoundVolumeToLinearGain(100) == 1.0F,
        "DirectSound volume clamps above maximum");

    return failures == 0 ? 0 : 1;
}

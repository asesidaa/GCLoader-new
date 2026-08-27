#include "Audio/Mixer/AudioSnapshot.h"
#include "Audio/Mixer/MiniaudioMixer.h"
#include "Audio/Wasapi/WasapiAudioTypes.h"

#include <Windows.h>
// ReSharper disable once CppUnusedIncludeDirective
#include <mmreg.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
// ReSharper disable once CppUnusedIncludeDirective
#include <cstring>
#include <iostream>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace
{
    using gc::audio::AudioCursorTimeline;
    using gc::audio::AudioLockRegions;
    using gc::audio::AudioSnapshot;
    using gc::audio::ExactMappedSpanPublicationFailure;
    using gc::audio::MiniaudioMixer;
    using gc::audio::MixerRenderTimeline;
    using gc::audio::NormalizedSourceFormat;
    using gc::audio::VoiceUsage;

    constexpr std::uint32_t kPeriodFrames = 192;
    constexpr std::uint32_t kOutputRate = 48'000;
    constexpr std::uint32_t kSourceRate = 44'100;
    constexpr std::uint32_t kSourceFrames = 44'100;
    constexpr std::uint16_t kBlockAlign = 4;

    int g_failures{};

    void Expect(const bool condition, const std::string_view message)
    {
        if (!condition)
        {
            std::cerr << "FAIL: " << message << '\n';
            ++g_failures;
        }
    }

    NormalizedSourceFormat NativeStereoPcm16()
    {
        WAVEFORMATEX wave{
            .wFormatTag = WAVE_FORMAT_PCM,
            .nChannels = 2,
            .nSamplesPerSec = kSourceRate,
            .nAvgBytesPerSec = kSourceRate * kBlockAlign,
            .nBlockAlign = kBlockAlign,
            .wBitsPerSample = 16,
            .cbSize = 0,
        };
        NormalizedSourceFormat normalized{};
        Expect(SUCCEEDED(gc::audio::NormalizeSourceFormat(&wave, &normalized)),
               "the production format normalizer accepts native stereo PCM16");
        return normalized;
    }

    std::shared_ptr<AudioSnapshot> AudibleSnapshot()
    {
        constexpr auto byte_length = kSourceFrames * kBlockAlign;
        auto snapshot = std::make_shared<AudioSnapshot>(byte_length, kBlockAlign);
        AudioLockRegions regions{};
        Expect(SUCCEEDED(snapshot->Lock(0, byte_length, 0, &regions)),
               "the production snapshot exposes a writable full-buffer lock");
        if (regions.first != nullptr)
        {
            std::memset(regions.first, 0x10, regions.first_bytes);
        }
        if (regions.second != nullptr)
        {
            std::memset(regions.second, 0x10, regions.second_bytes);
        }
        Expect(SUCCEEDED(snapshot->Unlock(
                   regions.first,
                   regions.first_bytes,
                   regions.second,
                   regions.second_bytes)),
               "the audible snapshot publishes through the production unlock path");
        return snapshot;
    }

    bool ContainsAudibleSample(const std::span<const float> block)
    {
        return std::ranges::any_of(block, [](const float sample)
        {
            return std::abs(sample) > 0.001F;
        });
    }

    void CandidateHistoryFailureDoesNotPoisonTheEndpoint()
    {
        ma_result create_result = MA_ERROR;
        auto mixer = MiniaudioMixer::Create(
            kPeriodFrames,
            kOutputRate,
            static_cast<const ma_allocation_callbacks*>(nullptr),
            &create_result);
        Expect(mixer != nullptr && create_result == MA_SUCCESS,
               "the production miniaudio mixer initializes");
        if (!mixer)
        {
            return;
        }

        const auto format = NativeStereoPcm16();
        auto ordinary_timeline = std::make_shared<AudioCursorTimeline>();
        auto candidate_timeline = std::make_shared<AudioCursorTimeline>();
        Expect(candidate_timeline->AssignBufferInstanceId(2),
               "the candidate timeline accepts its facade buffer identity");
        Expect(candidate_timeline->ConfigureExactPlaybackHistory(2, 1),
               "the candidate timeline accepts its exact identity");
        Expect(candidate_timeline->ExpectExactPlaybackGeneration(1),
               "the candidate timeline accepts playback generation one");

        ma_result voice_result = MA_ERROR;
        auto ordinary = mixer->CreateVoice(
            format,
            AudibleSnapshot(),
            ordinary_timeline,
            VoiceUsage::General,
            &voice_result);
        Expect(ordinary != nullptr && voice_result == MA_SUCCESS,
               "the ordinary audible voice initializes");
        auto candidate = mixer->CreateVoice(
            format,
            AudibleSnapshot(),
            candidate_timeline,
            VoiceUsage::GameplayNativeCandidate,
            &voice_result);
        Expect(candidate != nullptr && voice_result == MA_SUCCESS,
               "the exact candidate voice initializes");
        if (!ordinary || !candidate)
        {
            return;
        }

        candidate->SetGain(0.0F);
        Expect(SUCCEEDED(ordinary->Play(true, 1)), "the ordinary voice starts");
        Expect(SUCCEEDED(candidate->Play(true, 1)), "the exact candidate starts");

        std::vector<float> output(kPeriodFrames * 2);
        const auto first = mixer->Render(output, MixerRenderTimeline{0, 0});
        Expect(first.result == MA_SUCCESS, "the initial mixed block succeeds");
        Expect(ContainsAudibleSample(output), "the ordinary voice is audible before the candidate failure");

        candidate->Stop();
        Expect(candidate_timeline->ExpectExactPlaybackGeneration(1),
               "the deliberately invalid replay remains in generation one");
        Expect(SUCCEEDED(candidate->Play(true, 1)),
               "the candidate re-enters the mixer before violating its exact origin contract");

        std::ranges::fill(output, 0.0F);
        const auto violating = mixer->Render(
            output, MixerRenderTimeline{kPeriodFrames, 0});
        const auto failure =
            candidate_timeline->exact_mapped_span_publication_failure();
        Expect(failure.reason ==
               ExactMappedSpanPublicationFailure::OutputOriginChanged,
               "the candidate timeline records the observed output-origin violation");
        Expect(violating.result == MA_SUCCESS,
               "one candidate timeline failure does not become an endpoint render failure");
        Expect(ContainsAudibleSample(output),
               "the unrelated ordinary voice remains audible in the violating block");

        std::ranges::fill(output, 0.0F);
        const auto after = mixer->Render(
            output, MixerRenderTimeline{kPeriodFrames * 2, 0});
        Expect(after.result == MA_SUCCESS,
               "the candidate's sticky diagnostic cannot poison later endpoint blocks");
        Expect(ContainsAudibleSample(output),
               "the ordinary voice remains audible after the exact-history failure");
    }
} // namespace

int main()
{
    CandidateHistoryFailureDoesNotPoisonTheEndpoint();
    return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

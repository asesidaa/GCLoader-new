// SPDX-License-Identifier: CC0-1.0

#include "Audio/Mixer/AudioRenderCore.h"
#include "Audio/Mixer/AudioRenderCoreInternal.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace {

using gc::audio::AudioCursorResolutionKind;
using gc::audio::AudioCursorTimeline;
using gc::audio::AudioLockRegions;
using gc::audio::AudioRenderCore;
using gc::audio::AudioRenderSilenceReason;
using gc::audio::AudioSnapshot;
using gc::audio::IPresentedOutputClock;
using gc::audio::MixerRenderTimeline;
using gc::audio::NormalizedSourceFormat;
using gc::audio::VoiceUsage;

constexpr std::uint32_t kFrames = 8;
constexpr std::size_t kSamples =
    kFrames * gc::audio::kOutputChannels;

int Expect(bool condition, std::string_view name)
{
    if (condition)
    {
        return 0;
    }
    std::cerr << "Expected " << name << '\n';
    return 1;
}

struct ClockState
{
    std::optional<std::uint64_t> current{1234};
    int reads{};
    int invalidations{};
    bool destroyed{};
};

class FakeClock final : public IPresentedOutputClock
{
public:
    explicit FakeClock(std::shared_ptr<ClockState> state)
        : state_(std::move(state))
    {
    }

    ~FakeClock() override
    {
        state_->destroyed = true;
    }

    std::optional<std::uint64_t> CurrentOutputFrame() noexcept override
    {
        ++state_->reads;
        return state_->current;
    }

    void Invalidate() noexcept override
    {
        ++state_->invalidations;
    }

private:
    std::shared_ptr<ClockState> state_;
};

std::unique_ptr<AudioRenderCore> MakeCore(
    const std::shared_ptr<ClockState>& clock,
    ma_result& result,
    std::uint32_t sample_rate = gc::audio::kGamePrimarySampleRate)
{
    return AudioRenderCore::Create(
        kFrames,
        sample_rate,
        {},
        std::make_unique<FakeClock>(clock),
        &result);
}

int TestRenderFinalization()
{
    int failures{};
    std::array<float, kSamples> exact{};
    for (std::size_t index = 0; index < exact.size(); ++index)
    {
        exact[index] = static_cast<float>(index + 1) / 32.0F;
    }
    const auto original = exact;
    const auto successful = gc::audio::detail::FinalizeAudioRenderBlock(
        exact,
        kFrames,
        {MA_SUCCESS, kFrames, 1});
    failures += Expect(
        successful.mixer_result == MA_SUCCESS &&
            successful.frames_read == kFrames &&
            successful.active_voices == 1 &&
            successful.missing_frames == 0 &&
            successful.silence_reason == AudioRenderSilenceReason::none &&
            !successful.silence_substituted &&
            successful.interleaved_stereo.data() == exact.data() &&
            successful.interleaved_stereo.size() == exact.size() &&
            exact == original,
        "exact successful render preserves the complete block");

    std::array<float, kSamples> short_read{};
    short_read.fill(0.75F);
    const auto no_voice = gc::audio::detail::FinalizeAudioRenderBlock(
        short_read,
        kFrames,
        {MA_SUCCESS, kFrames - 1, 0});
    failures += Expect(
        no_voice.mixer_result == MA_SUCCESS &&
            no_voice.frames_read == kFrames - 1 &&
            no_voice.active_voices == 0 &&
            no_voice.missing_frames == 1 &&
            no_voice.silence_reason ==
                AudioRenderSilenceReason::no_active_voice &&
            no_voice.silence_substituted &&
            std::ranges::all_of(short_read, [](float sample)
            {
                return sample == 0.0F;
            }),
        "inactive short read reports no-active-voice silence");

    std::array<float, kSamples> active_short{};
    active_short.fill(0.5F);
    const auto active_shortened =
        gc::audio::detail::FinalizeAudioRenderBlock(
            active_short,
            kFrames,
            {MA_SUCCESS, kFrames - 3, 1});
    failures += Expect(
        active_shortened.missing_frames == 3 &&
            active_shortened.active_voices == 1 &&
            active_shortened.silence_reason ==
                AudioRenderSilenceReason::active_short_read &&
            active_shortened.silence_substituted &&
            std::ranges::all_of(active_short, [](float sample)
            {
                return sample == 0.0F;
            }),
        "active short read reports exact missing frames and clears all samples");

    std::array<float, kSamples> error{};
    error.fill(-0.5F);
    const auto failed = gc::audio::detail::FinalizeAudioRenderBlock(
        error,
        kFrames,
        {MA_ERROR, kFrames - 2, 1});
    failures += Expect(
        failed.mixer_result == MA_ERROR &&
            failed.missing_frames == 0 &&
            failed.silence_reason ==
                AudioRenderSilenceReason::mixer_error &&
            failed.silence_substituted &&
            std::ranges::all_of(error, [](float sample)
            {
                return sample == 0.0F;
            }),
        "mixer error substitutes silence for the complete fixed block");

    std::array<float, kSamples - 1> wrong_span{};
    wrong_span.fill(0.25F);
    const auto wrong_contract =
        gc::audio::detail::FinalizeAudioRenderBlock(
            wrong_span,
            kFrames,
            {MA_SUCCESS, kFrames, 1});
    failures += Expect(
        wrong_contract.missing_frames == 0 &&
            wrong_contract.silence_reason ==
                AudioRenderSilenceReason::render_contract_error &&
            wrong_contract.silence_substituted &&
            std::ranges::all_of(wrong_span, [](float sample)
            {
                return sample == 0.0F;
            }),
        "wrong stereo span is a cleared render contract error");

    std::array<float, kSamples> excessive{};
    excessive.fill(-0.25F);
    const auto excessive_frames =
        gc::audio::detail::FinalizeAudioRenderBlock(
            excessive,
            kFrames,
            {MA_SUCCESS, kFrames + 1, 0});
    failures += Expect(
        excessive_frames.missing_frames == 0 &&
            excessive_frames.silence_reason ==
                AudioRenderSilenceReason::render_contract_error &&
            std::ranges::all_of(excessive, [](float sample)
            {
                return sample == 0.0F;
            }),
        "excessive frame count is a cleared render contract error");
    return failures;
}

int TestCreationContractsAndClockOwnership()
{
    int failures{};
    ma_result result = MA_ERROR;
    auto state = std::make_shared<ClockState>();
    auto core = MakeCore(state, result);
    failures += Expect(
        result == MA_SUCCESS && core != nullptr &&
            core->period_frames() == kFrames &&
            core->output_sample_rate() ==
                gc::audio::kGamePrimarySampleRate,
        "core creates for the accepted 44.1 kHz WASAPI rate");
    failures += Expect(
        core->CurrentOutputFrame() == state->current && state->reads == 1,
        "current output frame delegates to the owned clock");
    core->InvalidatePresentationClock();
    failures += Expect(
        state->invalidations == 1 && !state->destroyed,
        "clock invalidation delegates while ownership remains in the core");
    core.reset();
    failures += Expect(
        state->destroyed,
        "owned presentation clock lifetime ends with the core");

    result = MA_SUCCESS;
    auto missing_clock = AudioRenderCore::Create(
        kFrames,
        gc::audio::kFallbackEndpointSampleRate,
        {},
        {},
        &result);
    failures += Expect(
        missing_clock == nullptr && result == MA_INVALID_ARGS,
        "missing presentation clock is rejected");

    result = MA_SUCCESS;
    auto zero_frames = AudioRenderCore::Create(
        0,
        gc::audio::kFallbackEndpointSampleRate,
        {},
        std::make_unique<FakeClock>(std::make_shared<ClockState>()),
        &result);
    failures += Expect(
        zero_frames == nullptr && result == MA_INVALID_ARGS,
        "zero period is rejected");

    result = MA_SUCCESS;
    auto unsupported_rate = AudioRenderCore::Create(
        kFrames,
        22'050,
        {},
        std::make_unique<FakeClock>(std::make_shared<ClockState>()),
        &result);
    failures += Expect(
        unsupported_rate == nullptr && result == MA_INVALID_ARGS,
        "unsupported endpoint output rate is rejected");

    result = MA_SUCCESS;
    auto overflowing = AudioRenderCore::Create(
        (std::numeric_limits<std::uint32_t>::max)(),
        gc::audio::kFallbackEndpointSampleRate,
        {},
        std::make_unique<FakeClock>(std::make_shared<ClockState>()),
        &result);
    failures += Expect(
        overflowing == nullptr && result == MA_TOO_BIG,
        "stereo allocation size overflow is rejected before allocation");
    return failures;
}

WAVEFORMATEX StereoPcm16(std::uint32_t sample_rate)
{
    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = 2;
    format.nSamplesPerSec = sample_rate;
    format.wBitsPerSample = 16;
    format.nBlockAlign = 4;
    format.nAvgBytesPerSec = sample_rate * format.nBlockAlign;
    return format;
}

int TestRenderStorageTimelineVoiceAndDiagnosticsDelegation()
{
    int failures{};
    ma_result result = MA_ERROR;
    auto state = std::make_shared<ClockState>();
    auto core = MakeCore(state, result);
    if (core == nullptr)
    {
        return Expect(false, "render delegation core creation");
    }

    NormalizedSourceFormat normalized{};
    const auto wave = StereoPcm16(gc::audio::kGamePrimarySampleRate);
    failures += Expect(
        gc::audio::NormalizeSourceFormat(&wave, &normalized) == DS_OK,
        "render delegation source normalization");

    std::vector<std::int16_t> samples(64);
    for (std::size_t index = 0; index < samples.size(); ++index)
    {
        samples[index] = static_cast<std::int16_t>(1000 + index * 100);
    }
    auto snapshot = std::make_shared<AudioSnapshot>(
        static_cast<std::uint32_t>(samples.size() * sizeof(std::int16_t)),
        normalized.block_align);
    AudioLockRegions regions{};
    failures += Expect(
        snapshot->Lock(
            0,
            static_cast<DWORD>(samples.size() * sizeof(std::int16_t)),
            DSBLOCK_ENTIREBUFFER,
            &regions) == DS_OK,
        "render delegation snapshot lock");
    std::memcpy(regions.first, samples.data(), regions.first_bytes);
    if (regions.second_bytes != 0)
    {
        std::memcpy(
            regions.second,
            reinterpret_cast<const std::byte*>(samples.data()) +
                regions.first_bytes,
            regions.second_bytes);
    }
    failures += Expect(
        snapshot->Unlock(
            regions.first,
            regions.first_bytes,
            regions.second,
            regions.second_bytes) == DS_OK,
        "render delegation snapshot unlock");

    auto timeline = std::make_shared<AudioCursorTimeline>();
    auto voice = core->CreateVoice(
        normalized,
        snapshot,
        timeline,
        VoiceUsage::GameplayNativeCandidate,
        &result);
    failures += Expect(
        result == MA_SUCCESS && voice != nullptr,
        "voice creation delegates to the mixer");
    failures += Expect(
        voice != nullptr && voice->Play(false, 77) == DS_OK,
        "delegated voice starts");

    const auto first = core->Render(MixerRenderTimeline{1000, 0});
    const auto* allocation = first.interleaved_stereo.data();
    const auto second = core->Render(MixerRenderTimeline{1008, 0});
    failures += Expect(
        first.mixer_result == MA_SUCCESS &&
            !first.silence_substituted &&
            first.interleaved_stereo.size() == kSamples &&
            second.interleaved_stereo.data() == allocation &&
            second.interleaved_stereo.size() == kSamples,
        "repeated renders reuse one full-period float allocation");
    const auto resolved = timeline->ResolveSourceFrame(1000, 77, 32);
    failures += Expect(
        resolved.kind == AudioCursorResolutionKind::Resolved &&
            resolved.source_frame == 0,
        "render timeline reaches the mixer unchanged");

    const auto diagnostics = core->diagnostics();
    if (!(diagnostics.native_rate_buffers == 1 &&
          diagnostics.sample_format_converted_buffers == 1 &&
          diagnostics.sample_rate_converted_buffers == 0 &&
          diagnostics.native_gameplay_buffers == 1 &&
          diagnostics.active_voices == 1 &&
          diagnostics.maximum_simultaneous_voices == 1))
    {
        std::cerr << "Diagnostics: native_rate="
                  << diagnostics.native_rate_buffers
                  << " format_converted="
                  << diagnostics.sample_format_converted_buffers
                  << " rate_converted="
                  << diagnostics.sample_rate_converted_buffers
                  << " native_gameplay="
                  << diagnostics.native_gameplay_buffers
                  << " active=" << diagnostics.active_voices
                  << " maximum="
                  << diagnostics.maximum_simultaneous_voices << '\n';
    }
    failures += Expect(
        diagnostics.native_rate_buffers == 1 &&
            diagnostics.sample_format_converted_buffers == 1 &&
            diagnostics.sample_rate_converted_buffers == 0 &&
            diagnostics.native_gameplay_buffers == 1 &&
            diagnostics.active_voices == 1 &&
            diagnostics.maximum_simultaneous_voices == 1,
        "voice and diagnostic counters delegate unchanged");
    return failures;
}

} // namespace

int main()
{
    int failures{};
    failures += TestRenderFinalization();
    failures += TestCreationContractsAndClockOwnership();
    failures += TestRenderStorageTimelineVoiceAndDiagnosticsDelegation();
    return failures == 0 ? 0 : 1;
}

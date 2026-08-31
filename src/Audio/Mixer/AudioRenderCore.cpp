// SPDX-License-Identifier: CC0-1.0

#include "Audio/Mixer/AudioRenderCore.h"
#include "Audio/Mixer/AudioRenderCoreInternal.h"

#include "Audio/Wasapi/WasapiAudioTypes.h"

// ReSharper disable once CppUnusedIncludeDirective
#include <cstddef>
#include <memory>
#include <new>
#include <utility>
#include <vector>

namespace gc::audio
{
    AudioRenderCore::AudioRenderCore(
        std::uint32_t period_frames,
        std::uint32_t output_sample_rate,
        std::unique_ptr<MiniaudioMixer> mixer,
        std::vector<float> float_mix,
        std::unique_ptr<IPresentedOutputClock> presented_clock) noexcept
        : period_frames_(period_frames),
          output_sample_rate_(output_sample_rate),
          mixer_(std::move(mixer)),
          float_mix_(std::move(float_mix)),
          presented_clock_(std::move(presented_clock))
    {
    }

    std::unique_ptr<AudioRenderCore> AudioRenderCore::Create(
        std::uint32_t period_frames,
        std::uint32_t output_sample_rate,
        std::shared_ptr<const ma_allocation_callbacks> allocation_callbacks,
        std::unique_ptr<IPresentedOutputClock> presented_clock,
        ma_result* result) noexcept
    {
        if (result != nullptr)
        {
            *result = MA_INVALID_ARGS;
        }
        if (period_frames == 0 ||
            !IsSupportedEndpointSampleRate(output_sample_rate))
        {
            return nullptr;
        }
        if (!detail::CanAddressAudioRenderSamples(period_frames))
        {
            if (result != nullptr)
            {
                *result = MA_TOO_BIG;
            }
            return nullptr;
        }

        ma_result mixer_result = MA_ERROR;
        auto mixer = MiniaudioMixer::Create(
            period_frames,
            output_sample_rate,
            std::move(allocation_callbacks),
            &mixer_result);
        if (mixer == nullptr)
        {
            if (result != nullptr)
            {
                *result = mixer_result;
            }
            return nullptr;
        }

        try
        {
            std::vector<float> float_mix(
                static_cast<std::size_t>(period_frames) * kOutputChannels);
            auto core = std::unique_ptr<AudioRenderCore>(
                new AudioRenderCore(
                    period_frames,
                    output_sample_rate,
                    std::move(mixer),
                    std::move(float_mix),
                    std::move(presented_clock)));
            if (result != nullptr)
            {
                *result = MA_SUCCESS;
            }
            return core;
        }
        catch (const std::bad_alloc&)
        {
            if (result != nullptr)
            {
                *result = MA_OUT_OF_MEMORY;
            }
            return nullptr;
        }
    }

    // These operations mutate logical mixer or presentation-clock state through
    // owned implementation objects.
    // ReSharper disable CppMemberFunctionMayBeConst
    std::unique_ptr<MixerVoice> AudioRenderCore::CreateVoice(
        const NormalizedSourceFormat& format,
        std::shared_ptr<AudioSnapshot> snapshot,
        std::shared_ptr<AudioCursorTimeline> timeline,
        VoiceUsage usage,
        ma_result* result) noexcept
    {
        if (mixer_ == nullptr)
        {
            if (result != nullptr)
            {
                *result = MA_INVALID_OPERATION;
            }
            return nullptr;
        }
        return mixer_->CreateVoice(
            format,
            std::move(snapshot),
            std::move(timeline),
            usage,
            result);
    }

    AudioRenderBlock AudioRenderCore::Render(
        const MixerRenderTimeline& timeline) noexcept
    {
        if (mixer_ == nullptr)
        {
            return detail::FinalizeAudioRenderBlock(
                float_mix_, period_frames_, {MA_INVALID_OPERATION, 0});
        }
        return detail::FinalizeAudioRenderBlock(
            float_mix_,
            period_frames_,
            mixer_->Render(float_mix_, timeline));
    }

    AudioRenderBlock AudioRenderCore::RenderSequential(
        const std::uint32_t frame_count) noexcept
    {
        if (mixer_ == nullptr || frame_count != period_frames_)
        {
            return detail::FinalizeAudioRenderBlock(
                float_mix_, period_frames_, {MA_INVALID_ARGS, 0});
        }
        return detail::FinalizeAudioRenderBlock(
            float_mix_,
            period_frames_,
            mixer_->RenderSequential(float_mix_));
    }

    std::optional<std::uint64_t>
    AudioRenderCore::CurrentOutputFrame() noexcept
    {
        return presented_clock_ != nullptr
                   ? presented_clock_->CurrentOutputFrame()
                   : std::nullopt;
    }

    void AudioRenderCore::InvalidatePresentationClock() noexcept
    {
        if (presented_clock_ != nullptr)
        {
            presented_clock_->Invalidate();
        }
    }

    // ReSharper restore CppMemberFunctionMayBeConst

    MixerDiagnosticsSnapshot AudioRenderCore::diagnostics() const noexcept
    {
        return mixer_ != nullptr ? mixer_->diagnostics() : MixerDiagnosticsSnapshot{};
    }

    std::uint32_t AudioRenderCore::period_frames() const noexcept
    {
        return period_frames_;
    }

    std::uint32_t AudioRenderCore::output_sample_rate() const noexcept
    {
        return output_sample_rate_;
    }
} // namespace gc::audio

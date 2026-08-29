#include "Audio/Mixer/LogicalRenderStream.h"

#include "Audio/Mixer/AudioSnapshot.h"
#include "Audio/Wasapi/WasapiAudioTypes.h"

#include <Windows.h>
#include <mmreg.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <string_view>

namespace
{
    using gc::audio::AudioCursorTimeline;
    using gc::audio::AudioRenderCore;
    using gc::audio::AudioSnapshot;
    using gc::audio::ExactClockStatus;
    using gc::audio::ExactPlaybackEpoch;
    using gc::audio::ExactPlaybackHistoryStatus;
    using gc::audio::ExactPlaybackOrigin;
    using gc::audio::IPresentedOutputClock;
    using gc::audio::LogicalRenderFailure;
    using gc::audio::LogicalRenderLease;
    using gc::audio::LogicalRenderOwner;
    using gc::audio::LogicalRenderPlan;
    using gc::audio::LogicalRenderStream;
    using gc::audio::NormalizedSourceFormat;
    using gc::audio::VoiceUsage;

    constexpr std::uint32_t kPeriodFrames = 192;
    constexpr std::uint32_t kLogicalRate = 48'000;
    constexpr std::uint32_t kSourceRate = 44'100;
    constexpr std::uint16_t kBlockAlign = 4;
    constexpr std::uint64_t kSourceFrames = 44'100;
    constexpr std::uint64_t kBufferInstance = 7;
    constexpr std::uint64_t kTimelineGeneration = 41;

    int failures = 0;

    void Expect(const bool condition, const std::string_view message)
    {
        if (!condition)
        {
            std::cerr << "FAIL: " << message << '\n';
            ++failures;
        }
    }

    class FixedPresentedClock final : public IPresentedOutputClock
    {
    public:
        std::optional<std::uint64_t>
        CurrentOutputFrame() noexcept override
        {
            return invalidated_ ? std::nullopt
                                : std::optional<std::uint64_t>{0};
        }

        void Invalidate() noexcept override
        {
            invalidated_ = true;
        }

    private:
        bool invalidated_{};
    };

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
        Expect(SUCCEEDED(gc::audio::NormalizeSourceFormat(
                   &wave, &normalized)),
               "production format normalization succeeds");
        return normalized;
    }

    std::shared_ptr<AudioSnapshot> AudibleSnapshot()
    {
        constexpr auto byte_length =
            static_cast<std::uint32_t>(kSourceFrames * kBlockAlign);
        auto snapshot =
            std::make_shared<AudioSnapshot>(byte_length, kBlockAlign);
        gc::audio::AudioLockRegions regions{};
        Expect(SUCCEEDED(snapshot->Lock(
                   0, byte_length, 0, &regions)),
               "production snapshot lock succeeds");
        if (regions.first)
        {
            std::memset(regions.first, 0x10, regions.first_bytes);
        }
        if (regions.second)
        {
            std::memset(regions.second, 0x10, regions.second_bytes);
        }
        Expect(SUCCEEDED(snapshot->Unlock(
                   regions.first,
                   regions.first_bytes,
                   regions.second,
                   regions.second_bytes)),
               "production snapshot publication succeeds");
        return snapshot;
    }

    void ExpectPlan(
        const LogicalRenderPlan& plan,
        const std::uint64_t begin,
        const std::uint64_t tail,
        const std::string_view message)
    {
        Expect(plan.timeline.output_frame_begin == begin, message);
        Expect(plan.timeline.discontinuity_frames == 0,
               "logical render plan never skips");
        Expect(plan.committed_tail_after == tail,
               "logical render plan advances by one fixed period");
    }

    void RenderAndCommit(
        LogicalRenderStream& stream,
        const LogicalRenderPlan& plan)
    {
        const auto block = stream.Render(plan);
        Expect(block.mixer_result == MA_SUCCESS,
               "production mixer render succeeds");
        Expect(block.frames_read == kPeriodFrames,
               "production mixer renders one complete period");
        Expect(stream.Commit(plan), "rendered plan commits exactly once");
    }

    void SingleOwnerTransfersAtTheExactCommittedTail()
    {
        ma_result create_result = MA_ERROR;
        auto core = AudioRenderCore::Create(
            kPeriodFrames,
            kLogicalRate,
            {},
            std::make_unique<FixedPresentedClock>(),
            &create_result);
        Expect(core != nullptr && create_result == MA_SUCCESS,
               "production render core initializes");
        if (!core)
        {
            return;
        }

        auto history = std::make_shared<AudioCursorTimeline>();
        Expect(history->AssignBufferInstanceId(kBufferInstance),
               "history buffer identity is assigned");
        Expect(history->ConfigureExactPlaybackHistory(
                   kBufferInstance, kTimelineGeneration),
               "history is keyed to one logical timeline");
        Expect(history->ExpectExactPlaybackGeneration(1),
               "one playback generation is expected");

        ma_result voice_result = MA_ERROR;
        auto voice = core->CreateVoice(
            NativeStereoPcm16(),
            AudibleSnapshot(),
            history,
            VoiceUsage::GameplayNativeCandidate,
            &voice_result);
        Expect(voice != nullptr && voice_result == MA_SUCCESS,
               "production mixer voice initializes");
        if (!voice)
        {
            return;
        }
        Expect(SUCCEEDED(voice->Play(true, 1)),
               "one looping playback epoch starts");

        auto stream = LogicalRenderStream::Create(*core);
        Expect(stream != nullptr, "logical render stream initializes");
        if (!stream)
        {
            return;
        }

        const auto acquired =
            stream->AcquireInitial(LogicalRenderOwner::Pump);
        Expect(acquired.has_value(), "pump acquires the initial lease");
        if (!acquired)
        {
            return;
        }
        LogicalRenderLease pump = *acquired;
        Expect(pump.acquired_tail == 0,
               "initial pump lease starts at tail zero");

        for (std::uint64_t index = 0; index < 3; ++index)
        {
            const auto plan = stream->BeginRender(pump);
            Expect(plan.has_value(), "pump plans one sequential period");
            if (!plan)
            {
                return;
            }
            ExpectPlan(*plan,
                       index * kPeriodFrames,
                       (index + 1) * kPeriodFrames,
                       "pump render origin is exact");
            RenderAndCommit(*stream, *plan);
        }

        const LogicalRenderLease forged_bridge{
            .owner = LogicalRenderOwner::AsioBridge,
            .generation = pump.generation,
            .acquired_tail = pump.acquired_tail,
        };
        const auto concurrent = stream->BeginRender(forged_bridge);
        Expect(!concurrent &&
                   concurrent.error() == LogicalRenderFailure::InvalidLease,
               "a second owner cannot plan under the pump lease");

        const auto abandoned = stream->BeginRender(pump);
        Expect(abandoned.has_value(),
               "pump can reserve the next logical interval");
        if (!abandoned)
        {
            return;
        }
        ExpectPlan(*abandoned, 576, 768,
                   "abandoned interval begins at committed tail");
        Expect(stream->Abandon(*abandoned),
               "unrendered plan can be abandoned");
        Expect(stream->committed_tail() == 576,
               "abandon leaves committed tail unchanged");

        const auto retry = stream->BeginRender(pump);
        Expect(retry.has_value(),
               "abandoned interval can be planned again");
        if (!retry)
        {
            return;
        }
        ExpectPlan(*retry, 576, 768,
                   "retry begins at the unchanged tail");
        RenderAndCommit(*stream, *retry);

        const auto bridge_result = stream->Transfer(
            pump, LogicalRenderOwner::AsioBridge, 768);
        Expect(bridge_result.has_value(),
               "pump transfers ownership to the bridge");
        if (!bridge_result)
        {
            return;
        }
        const auto bridge = *bridge_result;
        Expect(bridge.acquired_tail == 768,
               "bridge acquires the exact committed tail");
        const auto stale_pump = stream->BeginRender(pump);
        Expect(!stale_pump &&
                   stale_pump.error() == LogicalRenderFailure::InvalidLease,
               "pump lease is invalid immediately after transfer");

        const auto bridge_plan = stream->BeginRender(bridge);
        Expect(bridge_plan.has_value(), "bridge continues the sequence");
        if (!bridge_plan)
        {
            return;
        }
        ExpectPlan(*bridge_plan, 768, 960,
                   "bridge starts at the transferred tail");
        RenderAndCommit(*stream, *bridge_plan);

        const auto pump_result = stream->Transfer(
            bridge, LogicalRenderOwner::Pump, 960);
        Expect(pump_result.has_value(),
               "bridge transfers ownership back to the pump");
        if (!pump_result)
        {
            return;
        }
        pump = *pump_result;
        Expect(pump.acquired_tail == 960,
               "pump reacquires the exact committed tail");
        const auto stale_bridge = stream->BeginRender(bridge);
        Expect(!stale_bridge &&
                   stale_bridge.error() == LogicalRenderFailure::InvalidLease,
               "bridge lease is invalid immediately after transfer");

        const auto final_plan = stream->BeginRender(pump);
        Expect(final_plan.has_value(),
               "pump continues after the reverse transfer");
        if (!final_plan)
        {
            return;
        }
        ExpectPlan(*final_plan, 960, 1'152,
                   "reverse transfer preserves the sequence");
        RenderAndCommit(*stream, *final_plan);

        std::array<ExactPlaybackEpoch, 4> epochs{};
        ExactPlaybackHistoryStatus history_status{};
        const auto count =
            history->CopyExactPlaybackEpochs(epochs, &history_status);
        Expect(history_status.status == ExactClockStatus::Resolved,
               "production mixer history remains resolvable");
        Expect(count == 1,
               "ownership transfer creates no playback generation");
        if (count == 1)
        {
            Expect(epochs[0].timeline_generation ==
                       kTimelineGeneration,
                   "playback epoch keeps the logical timeline");
            Expect(epochs[0].playback_generation == 1,
                   "playback generation remains unchanged");
            Expect(epochs[0].origin == ExactPlaybackOrigin::Play,
                   "playback origin remains Play");
            Expect(epochs[0].output_origin == 0,
                   "ownership transfer does not rebase output origin");
            Expect(epochs[0].mapped_output_tail ==
                       stream->committed_tail(),
                   "history tail equals the committed logical tail");
        }
    }
} // namespace

int main()
{
    SingleOwnerTransfersAtTheExactCommittedTail();
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

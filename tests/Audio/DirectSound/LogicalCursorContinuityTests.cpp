#include "Audio/DirectSound/DirectSoundFacade.h"
#include "Audio/Logical/LogicalPresentationClock.h"
#include "Audio/Logical/LogicalPresentedOutputClock.h"
#include "Audio/Mixer/AudioRenderCore.h"

#include <Windows.h>
#include <dsound.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <string_view>

namespace
{
    using gc::audio::AudioCursorTimeline;
    using gc::audio::AudioRenderCore;
    using gc::audio::AudioSnapshot;
    using gc::audio::IAudioEngineServices;
    using gc::audio::LogicalPresentationClock;
    using gc::audio::LogicalPresentedOutputClock;
    using gc::audio::LogicalPresentedOutputClockActions;
    using gc::audio::MixerRenderTimeline;
    using gc::audio::MixerVoice;
    using gc::audio::NormalizedSourceFormat;
    using gc::audio::SecondarySoundBuffer;
    using gc::audio::VoiceUsage;

    constexpr std::uint64_t kTimelineGeneration = 73;
    constexpr std::uint32_t kLogicalRate = 48'000;
    constexpr std::uint32_t kSourceRate = 44'100;
    constexpr std::uint32_t kPeriodFrames = 480;
    constexpr std::uint16_t kChannels = 2;
    constexpr std::uint16_t kBitsPerSample = 16;
    constexpr std::uint16_t kBlockAlign =
        kChannels * (kBitsPerSample / 8);
    constexpr std::uint64_t kSourceFrames = 44'100;
    constexpr DWORD kBufferBytes =
        static_cast<DWORD>(kSourceFrames * kBlockAlign);

    int failures = 0;

    void Expect(const bool condition, const std::string_view message)
    {
        if (!condition)
        {
            std::cerr << "FAIL: " << message << '\n';
            ++failures;
        }
    }

    class LogicalEngineHarness final : public IAudioEngineServices
    {
    public:
        LogicalEngineHarness()
        {
            timeline_ = LogicalPresentationClock::Create(
                kTimelineGeneration, 1'000, kLogicalRate, 10'000'000);
            if (!timeline_)
            {
                return;
            }

            auto cursor = std::make_unique<LogicalPresentedOutputClock>(
                LogicalPresentedOutputClockActions{
                    .context = this,
                    .time_get_time_ms = &ReadNow,
                },
                timeline_);
            ma_result result = MA_ERROR;
            core_ = AudioRenderCore::Create(
                kPeriodFrames,
                kLogicalRate,
                {},
                std::move(cursor),
                &result);
            initialized_ = core_ != nullptr && result == MA_SUCCESS;
        }

        [[nodiscard]] bool initialized() const noexcept
        {
            return initialized_;
        }

        void AdvanceLogicalTime(const std::uint32_t now_ms)
        {
            now_ms_ = now_ms;
            Expect(timeline_->ObserveNow(now_ms).has_value(),
                   "logical clock observation succeeds");
        }

        void RenderPeriods(const std::uint32_t count)
        {
            for (std::uint32_t index = 0; index < count; ++index)
            {
                const auto block = core_->Render({
                    .output_frame_begin = rendered_tail_,
                    .discontinuity_frames = 0,
                });
                Expect(block.mixer_result == MA_SUCCESS,
                       "production render core succeeds");
                Expect(block.frames_read == kPeriodFrames,
                       "production render core returns one complete period");
                rendered_tail_ += kPeriodFrames;
            }
        }

        std::unique_ptr<MixerVoice> CreateVoice(
            const NormalizedSourceFormat& format,
            std::shared_ptr<AudioSnapshot> snapshot,
            std::shared_ptr<AudioCursorTimeline> history,
            const VoiceUsage usage,
            ma_result* result) noexcept override
        {
            if (usage == VoiceUsage::GameplayNativeCandidate)
            {
                const auto buffer_instance_id =
                    history ? history->exact_buffer_instance_id() : 0;
                if (!history || buffer_instance_id == 0 ||
                    !history->ConfigureExactPlaybackHistory(
                        buffer_instance_id, kTimelineGeneration))
                {
                    if (result)
                    {
                        *result = MA_INVALID_OPERATION;
                    }
                    return {};
                }
            }
            return core_->CreateVoice(
                format,
                std::move(snapshot),
                std::move(history),
                usage,
                result);
        }

        std::optional<std::uint64_t>
        CurrentOutputFrame() noexcept override
        {
            return core_->CurrentOutputFrame();
        }

        std::uint32_t endpoint_buffer_frames() const noexcept override
        {
            return kPeriodFrames;
        }

        std::uint32_t output_sample_rate() const noexcept override
        {
            return kLogicalRate;
        }

        void CountPendingCursorQuery() noexcept override
        {
            ++pending_queries_;
        }

        void CountUnmappedCursorFailure() noexcept override
        {
            ++unmapped_queries_;
        }

        [[nodiscard]] std::uint64_t pending_queries() const noexcept
        {
            return pending_queries_;
        }

        [[nodiscard]] std::uint64_t unmapped_queries() const noexcept
        {
            return unmapped_queries_;
        }

    private:
        static std::uint32_t ReadNow(void* context) noexcept
        {
            return static_cast<LogicalEngineHarness*>(context)->now_ms_;
        }

        std::shared_ptr<LogicalPresentationClock> timeline_;
        std::unique_ptr<AudioRenderCore> core_;
        std::uint32_t now_ms_{1'000};
        std::uint64_t rendered_tail_{};
        std::uint64_t pending_queries_{};
        std::uint64_t unmapped_queries_{};
        bool initialized_{};
    };

    WAVEFORMATEX SourceFormat()
    {
        return {
            .wFormatTag = WAVE_FORMAT_PCM,
            .nChannels = kChannels,
            .nSamplesPerSec = kSourceRate,
            .nAvgBytesPerSec = kSourceRate * kBlockAlign,
            .nBlockAlign = kBlockAlign,
            .wBitsPerSample = kBitsPerSample,
            .cbSize = 0,
        };
    }

    DWORD CurrentPlayCursor(SecondarySoundBuffer& buffer)
    {
        DWORD play_cursor = (std::numeric_limits<DWORD>::max)();
        Expect(buffer.GetCurrentPosition(&play_cursor, nullptr) == DS_OK,
               "GetCurrentPosition succeeds");
        return play_cursor;
    }

    DWORD CurrentStatus(SecondarySoundBuffer& buffer)
    {
        DWORD status = (std::numeric_limits<DWORD>::max)();
        Expect(buffer.GetStatus(&status) == DS_OK,
               "GetStatus succeeds");
        return status;
    }

    void ExpectSourceFrame(
        SecondarySoundBuffer& buffer,
        const std::uint64_t expected_frame,
        const std::string_view message)
    {
        Expect(CurrentPlayCursor(buffer) ==
                   static_cast<DWORD>(expected_frame * kBlockAlign),
               message);
    }

    void CursorAndDrainFollowLogicalTimeWithoutPhysicalPublication()
    {
        LogicalEngineHarness engine;
        Expect(engine.initialized(), "production logical engine initializes");
        if (!engine.initialized())
        {
            return;
        }

        auto format = SourceFormat();
        const DSBUFFERDESC descriptor{
            .dwSize = sizeof(DSBUFFERDESC),
            .dwFlags = DSBCAPS_STATIC | DSBCAPS_CTRLVOLUME |
                DSBCAPS_GETCURRENTPOSITION2 | DSBCAPS_LOCDEFER,
            .dwBufferBytes = kBufferBytes,
            .dwReserved = 0,
            .lpwfxFormat = &format,
            .guid3DAlgorithm = GUID_NULL,
        };

        SecondarySoundBuffer* raw_buffer{};
        Expect(SecondarySoundBuffer::Create(
                   engine, descriptor, &raw_buffer) == DS_OK,
               "secondary sound buffer creation succeeds");
        std::unique_ptr<SecondarySoundBuffer, void(*)(SecondarySoundBuffer*)>
            buffer(raw_buffer, [](SecondarySoundBuffer* value)
            {
                if (value)
                {
                    value->Release();
                }
            });
        if (!buffer)
        {
            return;
        }

        void* first{};
        DWORD first_bytes{};
        void* second{};
        DWORD second_bytes{};
        Expect(buffer->Lock(
                   0,
                   kBufferBytes,
                   &first,
                   &first_bytes,
                   &second,
                   &second_bytes,
                   DSBLOCK_ENTIREBUFFER) == DS_OK,
               "buffer lock succeeds");
        if (first)
        {
            std::fill_n(
                static_cast<std::int16_t*>(first),
                first_bytes / sizeof(std::int16_t),
                std::int16_t{1'000});
        }
        if (second)
        {
            std::fill_n(
                static_cast<std::int16_t*>(second),
                second_bytes / sizeof(std::int16_t),
                std::int16_t{1'000});
        }
        Expect(buffer->Unlock(
                   first, first_bytes, second, second_bytes) == DS_OK,
               "buffer unlock succeeds");

        Expect(buffer->Play(0, 0, 0) == DS_OK,
               "initial non-looping playback starts");
        engine.RenderPeriods(30);

        engine.AdvanceLogicalTime(1'250);
        ExpectSourceFrame(*buffer, 11'025,
                          "quarter-second cursor is source frame 11025");

        // No render or device callback occurs between these observations.
        engine.AdvanceLogicalTime(1'280);
        ExpectSourceFrame(*buffer, 12'348,
                          "cursor advances while output activity is stopped");
        Expect((CurrentStatus(*buffer) & DSBSTATUS_PLAYING) != 0,
               "pre-rendered audio remains logically audible");

        Expect(buffer->SetCurrentPosition(
                   static_cast<DWORD>(11'025 * kBlockAlign)) == DS_OK,
               "seek to quarter-second source position succeeds");
        engine.RenderPeriods(20);
        engine.AdvanceLogicalTime(1'400);
        ExpectSourceFrame(*buffer, 15'435,
                          "seek epoch advances by exact logical elapsed time");

        Expect(buffer->Stop() == DS_OK, "loop setup stop succeeds");
        Expect(buffer->SetCurrentPosition(
                   static_cast<DWORD>(39'690 * kBlockAlign)) == DS_OK,
               "loop starts one tenth-second before source end");
        Expect(buffer->Play(0, 0, DSBPLAY_LOOPING) == DS_OK,
               "looping playback starts");
        engine.RenderPeriods(20);
        engine.AdvanceLogicalTime(1'650);
        ExpectSourceFrame(*buffer, 2'205,
                          "loop wrap preserves exact source position");
        const auto looping_status = CurrentStatus(*buffer);
        Expect((looping_status & DSBSTATUS_PLAYING) != 0,
               "loop remains playing");
        Expect((looping_status & DSBSTATUS_LOOPING) != 0,
               "loop status remains set");

        Expect(buffer->Stop() == DS_OK, "natural-end setup stop succeeds");
        Expect(buffer->SetCurrentPosition(
                   static_cast<DWORD>(39'690 * kBlockAlign)) == DS_OK,
               "natural-end playback starts near source end");
        Expect(buffer->Play(0, 0, 0) == DS_OK,
               "natural-end playback starts");
        engine.RenderPeriods(15);

        engine.AdvanceLogicalTime(1'750);
        ExpectSourceFrame(*buffer, 41'895,
                          "draining cursor follows logical time before natural end");
        Expect((CurrentStatus(*buffer) & DSBSTATUS_PLAYING) != 0,
               "natural-end voice remains audible while draining");

        engine.AdvanceLogicalTime(1'900);
        Expect((CurrentStatus(*buffer) & DSBSTATUS_PLAYING) == 0,
               "logical drain completion clears playing status");
        Expect(engine.pending_queries() == 0,
               "logical coverage avoids pending cursor fallbacks");
        Expect(engine.unmapped_queries() == 0,
               "logical coverage avoids unmapped cursor fallbacks");
    }
} // namespace

int main()
{
    CursorAndDrainFollowLogicalTimeWithoutPhysicalPublication();
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

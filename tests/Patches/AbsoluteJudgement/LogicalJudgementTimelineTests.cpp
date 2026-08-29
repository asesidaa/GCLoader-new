#include "Audio/Logical/LogicalPresentationClock.h"
#include "Audio/Mixer/AudioCursorTimeline.h"
#include "Patches/AbsoluteJudgement/JudgementClockResolver.h"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string_view>

namespace
{
    using gc::absolute_judgement::JudgementClockResolver;
    using gc::absolute_judgement::JudgementClockStatus;
    using gc::audio::ExactClockResolveIntent;
    using gc::audio::ExactPlaybackEpoch;
    using gc::audio::ExactPlaybackOrigin;
    using gc::audio::GameplayAudioCursorObservation;
    using gc::audio::GameplayAudioCursorState;
    using gc::audio::LogicalPresentationClock;
    using gc::audio::AudioCursorTimeline;
    using gc::timing::AbsoluteHostTime;

    constexpr std::uint64_t kTimelineGeneration = 41;
    constexpr std::uint64_t kBufferInstance = 7;
    constexpr std::uint64_t kPlaybackGeneration = 1;
    constexpr std::uint32_t kLogicalRate = 48'000;
    constexpr std::uint32_t kSourceRate = 44'100;

    int failures = 0;

    void Expect(const bool condition, const std::string_view message)
    {
        if (!condition)
        {
            std::cerr << "FAIL: " << message << '\n';
            ++failures;
        }
    }

    void ExpectRational(
        const gc::timing::CheckedRational& actual,
        const std::int64_t numerator,
        const std::uint64_t denominator,
        const std::string_view message)
    {
        Expect(actual.numerator() == numerator &&
               actual.denominator() == denominator,
               message);
    }

    void ResolvesCapturedInputOnOneLogicalTimeline(
        const std::int32_t game_time_offset_ms,
        const std::int64_t expected_numerator,
        const std::uint64_t expected_denominator)
    {
        const auto timeline = LogicalPresentationClock::Create(
            kTimelineGeneration, 1'000, kLogicalRate, 10'000'000);
        auto history = std::make_shared<AudioCursorTimeline>();
        Expect(timeline != nullptr, "logical timeline creation succeeds");
        Expect(history != nullptr, "playback history creation succeeds");
        if (!timeline || !history)
        {
            return;
        }

        Expect(history->AssignBufferInstanceId(kBufferInstance),
               "buffer instance is assigned exactly once");
        Expect(history->ConfigureExactPlaybackHistory(
                   kBufferInstance, kTimelineGeneration),
               "history is keyed to the logical timeline");
        Expect(history->ExpectExactPlaybackGeneration(kPlaybackGeneration),
               "playback generation is expected");
        Expect(history->PublishExactMappedSpan(
                   kPlaybackGeneration,
                   ExactPlaybackOrigin::Play,
                   4'800,
                   0,
                   kLogicalRate,
                   kSourceRate,
                   9'600,
                   false,
                   0),
               "initial logical playback span is published");

        const GameplayAudioCursorObservation selected{
            .query_serial = 1,
            .state = GameplayAudioCursorState::Exact,
            .source_frame_unwrapped = 0,
            .source_sample_rate = kSourceRate,
            .buffer_instance_id = kBufferInstance,
            .timeline_generation = kTimelineGeneration,
            .playback_generation = kPlaybackGeneration,
            .origin = ExactPlaybackOrigin::Play,
            .output_frame = 4'800,
            .exact_history = history,
        };
        JudgementClockResolver resolver;
        resolver.Reset(
            9,
            AbsoluteHostTime{
                .qpc_ticks = 1,
                .multimedia_time_ms = 1'100,
            },
            game_time_offset_ms);

        std::array<ExactPlaybackEpoch, 4> scratch{};
        const auto bound = resolver.TryBind(selected, timeline, scratch);
        Expect(bound.status == JudgementClockStatus::Resolved,
               "stage binds to the logical playback epoch");
        Expect(resolver.bound(), "resolver stores the logical anchor");
        if (!resolver.bound())
        {
            return;
        }

        const auto& anchor = resolver.anchor();
        Expect(anchor.timeline_generation == kTimelineGeneration,
               "anchor stores the logical timeline generation");
        Expect(anchor.logical_output_origin == 4'800,
               "anchor stores the logical output origin");
        Expect(anchor.logical_output_rate == kLogicalRate,
               "anchor stores the logical output rate");

        constexpr AbsoluteHostTime captured{
            .qpc_ticks = 2,
            .multimedia_time_ms = 1'150,
        };
        const auto first = resolver.Resolve(
            captured, ExactClockResolveIntent::FinalizedTimestamp);
        Expect(first.status == JudgementClockStatus::Resolved,
               "captured input resolves");
        Expect(first.output_frame.has_value(),
               "logical output frame is present");
        Expect(first.judgement_seconds.has_value(),
               "source judgement time is present");
        if (!first.output_frame || !first.judgement_seconds)
        {
            return;
        }

        ExpectRational(*first.output_frame, 7'200, 1,
                       "L(1150 ms) is exactly frame 7200");
        ExpectRational(*first.judgement_seconds,
                       expected_numerator,
                       expected_denominator,
                       "GameTimeOffset is applied exactly once");

        Expect(timeline->ObserveNow(50'000).has_value(),
               "later wrap bookkeeping succeeds");
        Expect(history->PublishExactMappedSpan(
                   kPlaybackGeneration,
                   ExactPlaybackOrigin::Play,
                   4'800,
                   0,
                   kLogicalRate,
                   kSourceRate,
                   19'200,
                   false,
                   0),
               "later logical history coverage is published");

        const auto repeated = resolver.Resolve(
            captured, ExactClockResolveIntent::FinalizedTimestamp);
        Expect(repeated.status == JudgementClockStatus::Resolved,
               "captured input remains resolvable");
        Expect(repeated.output_frame.has_value() &&
               repeated.judgement_seconds.has_value(),
               "repeated exact values are present");
        if (repeated.output_frame && repeated.judgement_seconds)
        {
            Expect(repeated.output_frame->numerator() ==
                   first.output_frame->numerator() &&
                   repeated.output_frame->denominator() ==
                   first.output_frame->denominator(),
                   "logical output coordinate is bit-identical");
            Expect(repeated.judgement_seconds->numerator() ==
                   first.judgement_seconds->numerator() &&
                   repeated.judgement_seconds->denominator() ==
                   first.judgement_seconds->denominator(),
                   "judgement coordinate is bit-identical");
        }
    }
} // namespace

int main()
{
    ResolvesCapturedInputOnOneLogicalTimeline(0, 1, 20);
    ResolvesCapturedInputOnOneLogicalTimeline(7, 57, 1'000);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

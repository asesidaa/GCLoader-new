#include "Audio/Asio/ExactAsioClock.h"

#include "Audio/Asio/AsioClock.h"
#include "Audio/Asio/AsioLogicalTimeline.h"
#include "Audio/Asio/AsioSubmittedOutputTail.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string_view>

namespace
{
    using gc::audio::AsioClockNowActions;
    using gc::audio::AsioLogicalTimeline;
    using gc::audio::AsioPresentedClockPublication;
    using gc::audio::AsioSubmittedOutputTail;
    using gc::audio::ExactAsioClock;
    using gc::audio::ExactClockResolveIntent;
    using gc::audio::ExactClockStatus;
    using gc::audio::ExactJudgementTimelineResult;
    using gc::timing::AbsoluteHostTime;
    using gc::timing::CheckedRational;

    int failures = 0;

    struct FakeClock final
    {
        std::uint32_t now_ms{};

        static std::uint32_t Read(void* context) noexcept
        {
            return static_cast<FakeClock*>(context)->now_ms;
        }
    };

    void Expect(const bool condition, const std::string_view message)
    {
        if (!condition)
        {
            std::cerr << "FAIL: " << message << '\n';
            ++failures;
        }
    }

    [[nodiscard]] AbsoluteHostTime AtMillisecond(
        const std::uint32_t multimedia_time_ms) noexcept
    {
        return {
            .qpc_ticks = static_cast<std::int64_t>(multimedia_time_ms) *
            10'000,
            .multimedia_time_ms = multimedia_time_ms,
        };
    }

    void ExpectFrame(const ExactJudgementTimelineResult& result,
                     const std::int64_t numerator,
                     const std::uint64_t denominator,
                     const std::string_view message)
    {
        const auto expected = CheckedRational::Create(numerator, denominator);
        Expect(expected.has_value(), "test expectation is representable");
        Expect(result.status == ExactClockStatus::Resolved, message);
        Expect(result.logical_output_frame.has_value(), message);
        if (expected && result.logical_output_frame)
        {
            Expect(result.logical_output_frame->Compare(*expected) == 0, message);
        }
    }

    void ExpectSameFrame(const ExactJudgementTimelineResult& actual,
                         const ExactJudgementTimelineResult& expected,
                         const std::string_view message)
    {
        Expect(actual.status == ExactClockStatus::Resolved, message);
        Expect(expected.status == ExactClockStatus::Resolved, message);
        Expect(actual.logical_output_frame.has_value(), message);
        Expect(expected.logical_output_frame.has_value(), message);
        if (actual.logical_output_frame && expected.logical_output_frame)
        {
            Expect(actual.logical_output_frame->Compare(*expected.logical_output_frame) == 0,
                   message);
        }
    }

    void ResolvesDirectlyWithoutSubmittedTailGate()
    {
        const auto timeline = AsioLogicalTimeline::Create(1'000, 44'100);
        const auto clock = ExactAsioClock::Create(
            7, timeline, 10'000'000, 192, 384);
        Expect(timeline != nullptr, "timeline creation succeeds");
        Expect(clock != nullptr, "persistent exact clock creation succeeds");
        if (!timeline || !clock)
        {
            return;
        }

        const auto initial = clock->Resolve(
            AtMillisecond(1'001),
            ExactClockResolveIntent::FinalizedTimestamp);
        ExpectFrame(initial,
                    441,
                    10,
                    "logical judgement resolves without physical output evidence");

        const auto finalized = clock->Resolve(
            AtMillisecond(1'001),
            ExactClockResolveIntent::FinalizedTimestamp);
        ExpectFrame(finalized,
                    441,
                    10,
                    "finalized timestamp uses the immutable timeline");

        const auto horizon = clock->Resolve(
            AtMillisecond(1'001),
            ExactClockResolveIntent::ProvisionalHorizon);
        ExpectSameFrame(horizon,
                        finalized,
                        "intent does not select different callback evidence");

        Expect(timeline->AdvanceNow(2'000).has_value(),
               "writer advances the wrap snapshot");
        const auto after_snapshot = clock->Resolve(
            AtMillisecond(1'001),
            ExactClockResolveIntent::FinalizedTimestamp);
        ExpectSameFrame(after_snapshot,
                        finalized,
                        "later observations cannot rewrite a finalized event");
        Expect(clock->info().logical_output_rate == 44'100,
               "clock reports the driver-owned logical sample rate");
    }

    void LogicalClockInvalidationIsExplicit()
    {
        const auto timeline = AsioLogicalTimeline::Create(100, 48'000);
        const auto clock = ExactAsioClock::Create(
            7, timeline, 10'000'000, 192, 384);
        Expect(timeline != nullptr, "invalidation test timeline creation succeeds");
        Expect(clock != nullptr, "invalidation test exact clock creation succeeds");
        if (!timeline || !clock)
        {
            return;
        }

        const auto resolved = clock->Resolve(
            AtMillisecond(101),
            ExactClockResolveIntent::FinalizedTimestamp);
        ExpectFrame(resolved,
                    48,
                    1,
                    "logical judgement resolves from multimedia time alone");
        Expect(resolved.timeline_generation == 7,
               "logical endpoint identity is persistent");
        Expect(resolved.available_output_tail == 0,
               "logical judgement exposes no physical submitted tail");
        Expect(resolved.provider_anchor_sequence == 0,
               "logical judgement exposes no physical publication sequence");
        Expect(!resolved.provider_position.has_value(),
               "there is no physical callback anchor");
        Expect(clock->counters().publication_count == 0,
               "logical judgement has no physical publication counter");
        Expect(clock->counters().history_lost_queries == 0,
               "a non-existent callback history cannot be lost");

        clock->Invalidate();
        const auto invalid = clock->Resolve(
            AtMillisecond(100),
            ExactClockResolveIntent::FinalizedTimestamp);
        Expect(invalid.status == ExactClockStatus::Discontinuous,
               "explicit final invalidation is visible");
    }

    void DirectSoundCursorUsesOnlyThePhysicalPresentationClock()
    {
        const auto timeline = AsioLogicalTimeline::Create(1'000, 48'000);
        const auto tail = std::make_shared<AsioSubmittedOutputTail>();
        FakeClock now{.now_ms = 1'004};
        AsioPresentedClockPublication presented(
            AsioClockNowActions{
                .context = &now,
                .time_get_time_ms = &FakeClock::Read,
            },
            timeline,
            tail);
        const auto exact = ExactAsioClock::Create(
            17, timeline, 10'000'000, 192, 384);
        Expect(exact != nullptr, "separate exact clock creation succeeds");
        if (!exact)
        {
            return;
        }

        Expect(tail->Publish(676), "shared submitted output commits");
        const auto exact_before = exact->Resolve(
            AtMillisecond(now.now_ms),
            ExactClockResolveIntent::FinalizedTimestamp);
        ExpectFrame(exact_before,
                    192,
                    1,
                    "absolute judgement starts at the 48 kHz logical coordinate");

        presented.PublishPhysicalAnchor(100, 676, 1'000'000'000);
        const auto frame = presented.CurrentOutputFrame();
        Expect(frame == 292,
               "DirectSound cursor advances from the physical presentation phase");

        const auto nominal = timeline->WholePresentedFrameAt(now.now_ms);
        Expect(nominal.has_value(), "nominal cursor comparison is available");
        if (frame && nominal)
        {
            Expect(*frame != *nominal,
                   "physical presentation remains separate from the logical timeline");
        }
        Expect(frame && *frame < 676,
               "physical cursor remains inside the submitted half-open span");

        const auto exact_after = exact->Resolve(
            AtMillisecond(now.now_ms),
            ExactClockResolveIntent::FinalizedTimestamp);
        ExpectSameFrame(
            exact_after,
            exact_before,
            "publishing a physical presentation anchor cannot rewrite judgement");
        Expect(!exact_after.provider_position.has_value(),
               "physical presentation metadata never enters judgement");

        now.now_ms = 1'020;
        const auto saturated = presented.CurrentOutputFrame();
        Expect(saturated == 675,
               "DirectSound cursor cannot reach the exclusive submitted tail");
    }
} // namespace

int main()
{
    ResolvesDirectlyWithoutSubmittedTailGate();
    LogicalClockInvalidationIsExplicit();
    DirectSoundCursorUsesOnlyThePhysicalPresentationClock();
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

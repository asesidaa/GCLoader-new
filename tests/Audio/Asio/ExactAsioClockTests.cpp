#include "Audio/Asio/ExactAsioClock.h"

#include "Audio/Asio/AsioLogicalTimeline.h"
#include "Audio/Asio/AsioSubmittedOutputTail.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string_view>

namespace
{
    using gc::audio::AsioLogicalTimeline;
    using gc::audio::AsioSubmittedOutputTail;
    using gc::audio::ExactAsioClock;
    using gc::audio::ExactClockResolveIntent;
    using gc::audio::ExactClockStatus;
    using gc::audio::ExactOutputClockResult;
    using gc::timing::AbsoluteHostTime;
    using gc::timing::CheckedRational;

    int failures = 0;

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

    void ExpectFrame(const ExactOutputClockResult& result,
                     const std::int64_t numerator,
                     const std::uint64_t denominator,
                     const std::string_view message)
    {
        const auto expected = CheckedRational::Create(numerator, denominator);
        Expect(expected.has_value(), "test expectation is representable");
        Expect(result.status == ExactClockStatus::Resolved, message);
        Expect(result.output_frame.has_value(), message);
        if (expected && result.output_frame)
        {
            Expect(result.output_frame->Compare(*expected) == 0, message);
        }
    }

    void ExpectSameFrame(const ExactOutputClockResult& actual,
                         const ExactOutputClockResult& expected,
                         const std::string_view message)
    {
        Expect(actual.status == ExactClockStatus::Resolved, message);
        Expect(expected.status == ExactClockStatus::Resolved, message);
        Expect(actual.output_frame.has_value(), message);
        Expect(expected.output_frame.has_value(), message);
        if (actual.output_frame && expected.output_frame)
        {
            Expect(actual.output_frame->Compare(*expected.output_frame) == 0,
                   message);
        }
    }

    void ResolvesDirectlyAndOnlyBehindCommittedTail()
    {
        auto timeline = AsioLogicalTimeline::Create(1'000, 44'100);
        auto tail = std::make_shared<AsioSubmittedOutputTail>();
        auto clock = ExactAsioClock::Create(
            7, timeline, tail, 10'000'000, 192, 384);
        Expect(timeline != nullptr, "timeline creation succeeds");
        Expect(tail != nullptr, "submitted-tail creation succeeds");
        Expect(clock != nullptr, "persistent exact clock creation succeeds");
        if (!timeline || !tail || !clock)
        {
            return;
        }

        const auto before_commit = clock->Resolve(
            AtMillisecond(1'001),
            ExactClockResolveIntent::FinalizedTimestamp);
        Expect(before_commit.status == ExactClockStatus::Pending,
               "projection waits only for committed render evidence");

        Expect(tail->Publish(1'000), "first submitted tail commits");
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
        Expect(clock->info().output_sample_rate == 44'100,
               "clock reports the driver-owned logical sample rate");
    }

    void TailAndInvalidationAreExplicit()
    {
        auto timeline = AsioLogicalTimeline::Create(100, 48'000);
        auto tail = std::make_shared<AsioSubmittedOutputTail>();
        auto clock = ExactAsioClock::Create(
            7, timeline, tail, 10'000'000, 192, 384);
        Expect(timeline != nullptr, "tail test timeline creation succeeds");
        Expect(tail != nullptr, "tail test publication creation succeeds");
        Expect(clock != nullptr, "tail test exact clock creation succeeds");
        if (!timeline || !tail || !clock)
        {
            return;
        }

        Expect(tail->Publish(48), "one millisecond of output commits");
        const auto at_tail = clock->Resolve(
            AtMillisecond(101),
            ExactClockResolveIntent::FinalizedTimestamp);
        Expect(at_tail.status == ExactClockStatus::Pending,
               "a frame at the exclusive submitted tail is pending");
        Expect(at_tail.endpoint_generation == 7,
               "logical endpoint identity is persistent");
        Expect(at_tail.anchor_sequence == 1,
               "tail publication sequence remains observable");
        Expect(!at_tail.anchor_endpoint_position.has_value(),
               "there is no physical callback anchor");
        Expect(clock->counters().publication_count == 1,
               "clock publication count comes from the shared tail");
        Expect(clock->counters().history_lost_queries == 0,
               "a non-existent callback history cannot be lost");

        clock->Invalidate();
        const auto invalid = clock->Resolve(
            AtMillisecond(100),
            ExactClockResolveIntent::FinalizedTimestamp);
        Expect(invalid.status == ExactClockStatus::Discontinuous,
               "explicit final invalidation is visible");
    }
} // namespace

int main()
{
    ResolvesDirectlyAndOnlyBehindCommittedTail();
    TailAndInvalidationAreExplicit();
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

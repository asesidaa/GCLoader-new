#include "Audio/Logical/LogicalPresentationClock.h"

#include "Timing/CheckedRational.h"

#include <cstdint>
#include <cstdlib>
#include <expected>
#include <iostream>
#include <limits>
#include <string_view>

namespace
{
    using gc::audio::ExactClockResolveIntent;
    using gc::audio::ExactClockStatus;
    using gc::audio::ExactJudgementTimelineResult;
    using gc::audio::LogicalPresentationClock;
    using gc::audio::LogicalPresentationClockFailure;
    using gc::timing::AbsoluteHostTime;
    using gc::timing::CheckedRational;
    using gc::timing::RationalError;

    constexpr std::int64_t kQpcFrequency = 10'000'000;
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
        const std::expected<CheckedRational, LogicalPresentationClockFailure>& actual,
        const std::int64_t numerator,
        const std::uint64_t denominator,
        const std::string_view message)
    {
        if (!actual)
        {
            std::cerr << "FAIL: " << message << " (projection failed)\n";
            ++failures;
            return;
        }

        if (actual->numerator() != numerator ||
            actual->denominator() != denominator)
        {
            std::cerr << "FAIL: " << message << " (got "
                      << actual->numerator() << '/' << actual->denominator()
                      << ")\n";
            ++failures;
        }
    }

    void ExpectEquivalent(
        const std::expected<CheckedRational, LogicalPresentationClockFailure>& actual,
        const std::expected<CheckedRational, RationalError>& expected,
        const std::string_view message)
    {
        if (!actual || !expected)
        {
            std::cerr << "FAIL: " << message << " (projection failed)\n";
            ++failures;
            return;
        }

        Expect(actual->Compare(*expected) == 0, message);
    }

    void ExpectResolvedFrame(
        const ExactJudgementTimelineResult& actual,
        const std::int64_t numerator,
        const std::uint64_t denominator,
        const std::string_view message)
    {
        const auto expected = CheckedRational::Create(numerator, denominator);
        Expect(expected.has_value(), "expected frame is representable");
        Expect(actual.status == ExactClockStatus::Resolved, message);
        Expect(actual.logical_output_frame.has_value(), message);
        if (expected && actual.logical_output_frame)
        {
            Expect(actual.logical_output_frame->Compare(*expected) == 0, message);
        }
    }

    void ProjectsFromOneOriginWithoutFractionalDrift()
    {
        auto timeline = LogicalPresentationClock::Create(
            41, 1'000, 44'100, kQpcFrequency);
        Expect(timeline != nullptr, "44.1-kHz logical clock is valid");
        if (!timeline)
        {
            return;
        }

        ExpectRational(timeline->ProjectMultimediaMilliseconds(1'001),
                       441,
                       10,
                       "1 ms is exactly 441/10 frames");
        ExpectRational(timeline->ProjectMultimediaMilliseconds(1'010),
                       441,
                       1,
                       "10 ms is exactly 441 frames");
        ExpectRational(timeline->ProjectMultimediaMilliseconds(2'000),
                       44'100,
                       1,
                       "1000 ms is exactly 44100 frames");

        for (std::uint32_t raw = 1'001; raw <= 2'000; ++raw)
        {
            const auto direct = CheckedRational::Create(
                static_cast<std::int64_t>(raw - 1'000) * 44'100,
                1'000);
            ExpectEquivalent(timeline->ProjectMultimediaMilliseconds(raw),
                             direct,
                             "repeated queries equal direct origin projection");
        }

        auto forty_eight = LogicalPresentationClock::Create(
            42, 50, 48'000, kQpcFrequency);
        Expect(forty_eight != nullptr, "48-kHz logical clock is valid");
        if (forty_eight)
        {
            ExpectRational(forty_eight->ProjectMultimediaMilliseconds(51),
                           48,
                           1,
                           "48-kHz projection remains exact");
        }
    }

    void WrapObservationDoesNotRebaseAnOldEvent()
    {
        constexpr auto origin = (std::numeric_limits<std::uint32_t>::max)() - 5;
        auto timeline = LogicalPresentationClock::Create(
            43, origin, 44'100, kQpcFrequency);
        Expect(timeline != nullptr, "wrap logical clock is valid");
        if (!timeline)
        {
            return;
        }

        Expect(timeline->ObserveNow(3).has_value(),
               "writer crosses UINT32_MAX exactly");
        const auto before =
            timeline->ProjectMultimediaMilliseconds(origin + 3);
        ExpectRational(before,
                       1'323,
                       10,
                       "three pre-wrap milliseconds are 1323/10 frames");

        Expect(timeline->ObserveNow(20).has_value(),
               "writer advances the stable snapshot after wrap");
        const auto after =
            timeline->ProjectMultimediaMilliseconds(origin + 3);
        if (!before)
        {
            Expect(false, "pre-wrap projection exists for invariance comparison");
            return;
        }
        const auto expected = CheckedRational::Create(
            before->numerator(), before->denominator());
        ExpectEquivalent(after,
                         expected,
                         "later wrap bookkeeping cannot rebase an old event");
    }

    void CapturedEventCoordinateIsImmutableAndPhysicalFree()
    {
        auto clock = LogicalPresentationClock::Create(
            41, 1'000, 44'100, kQpcFrequency);
        Expect(clock != nullptr, "logical judgement provider is valid");
        if (!clock)
        {
            return;
        }

        const AbsoluteHostTime event{
            .qpc_ticks = 10'010'000,
            .multimedia_time_ms = 1'001,
        };
        const auto before = clock->Resolve(
            event, ExactClockResolveIntent::FinalizedTimestamp);
        Expect(clock->ObserveNow(50'000).has_value(),
               "later wrap observation succeeds");
        const auto after = clock->Resolve(
            event, ExactClockResolveIntent::FinalizedTimestamp);
        const auto horizon = clock->Resolve(
            event, ExactClockResolveIntent::ProvisionalHorizon);

        ExpectResolvedFrame(before, 441, 10,
                            "captured event resolves to 441/10 frames");
        ExpectResolvedFrame(after, 441, 10,
                            "later observation preserves captured coordinate");
        ExpectResolvedFrame(horizon, 441, 10,
                            "resolve intent does not change logical coordinate");
        if (before.logical_output_frame && after.logical_output_frame &&
            horizon.logical_output_frame)
        {
            Expect(before.logical_output_frame->Compare(
                       *after.logical_output_frame) == 0,
                   "finalized result is bit-stable after observation");
            Expect(before.logical_output_frame->Compare(
                       *horizon.logical_output_frame) == 0,
                   "both intents share one logical coordinate");
        }

        const auto info = clock->info();
        Expect(info.timeline_generation == 41,
               "logical timeline generation is persistent");
        Expect(info.logical_output_rate == 44'100,
               "logical output rate is driver-owned");
        Expect(info.provider_period_frames == 0,
               "logical provider has no physical period");
        Expect(info.provider_output_latency_frames == 0,
               "logical provider has no physical latency");
        Expect(info.timestamp_quantum_ns == 1'000'000,
               "multimedia timestamp quantum is one millisecond");
        Expect(before.available_output_tail == 0,
               "logical provider has no submitted tail");
        Expect(before.provider_anchor_sequence == 0,
               "logical provider has no physical anchor sequence");
        Expect(!before.provider_position.has_value(),
               "logical provider has no physical position");

        clock->Invalidate();
        const auto invalid = clock->Resolve(
            event, ExactClockResolveIntent::FinalizedTimestamp);
        Expect(invalid.status == ExactClockStatus::Discontinuous,
               "explicit final invalidation is visible");
    }
} // namespace

int main()
{
    ProjectsFromOneOriginWithoutFractionalDrift();
    WrapObservationDoesNotRebaseAnOldEvent();
    CapturedEventCoordinateIsImmutableAndPhysicalFree();
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

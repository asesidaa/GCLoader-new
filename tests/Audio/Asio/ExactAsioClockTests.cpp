#include "Audio/Asio/ExactAsioClock.h"

#include <cstdlib>
#include <iostream>
#include <string_view>

namespace
{
    using gc::audio::ExactAsioAnchor;
    using gc::audio::ExactAsioClock;
    using gc::audio::ExactClockResolveIntent;
    using gc::audio::ExactClockStatus;
    using gc::audio::ExactOutputClockResult;
    using gc::timing::AbsoluteHostTime;
    using gc::timing::CheckedRational;

    int g_failures{};

    void Expect(const bool condition, const std::string_view message)
    {
        if (!condition)
        {
            std::cerr << "FAIL: " << message << '\n';
            ++g_failures;
        }
    }

    [[nodiscard]] AbsoluteHostTime AtMillisecond(const std::uint32_t multimedia_time_ms) noexcept
    {
        return {
            .qpc_ticks = static_cast<std::int64_t>(multimedia_time_ms) * 10'000,
            .multimedia_time_ms = multimedia_time_ms,
        };
    }

    void ExpectFrame(const ExactOutputClockResult& result, const std::int64_t numerator,
                     const std::uint64_t denominator, const std::string_view message)
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

    void FinalizedResolutionWaitsForBracketAndInterpolates()
    {
        auto clock = ExactAsioClock::Create(1, 48'000, 10'000'000, 192, 384);
        Expect(clock != nullptr, "clock creation succeeds");
        if (!clock)
        {
            return;
        }

        Expect(clock->Publish({
                   .sequence = 1,
                   .endpoint_generation = 1,
                   .presented_output_frame = 1'000,
                   .system_time_ns = 1'000'000'000,
                   .submitted_output_tail = 1'576,
               }),
               "first anchor publishes");

        const auto before_bracket = clock->Resolve(AtMillisecond(1'006), ExactClockResolveIntent::FinalizedTimestamp);
        Expect(before_bracket.status == ExactClockStatus::Pending, "an unbracketed event timestamp remains pending");

        Expect(clock->Publish({
                   .sequence = 2,
                   .endpoint_generation = 1,
                   .presented_output_frame = 1'192,
                   .system_time_ns = 1'007'000'000,
                   .submitted_output_tail = 1'768,
               }),
               "second anchor publishes");

        const auto bracketed = clock->Resolve(AtMillisecond(1'006), ExactClockResolveIntent::FinalizedTimestamp);
        ExpectFrame(bracketed, 8'152, 7, "the event frame is interpolated from the measured anchor pair");

        const auto latest_exact = clock->Resolve(AtMillisecond(1'007), ExactClockResolveIntent::FinalizedTimestamp);
        Expect(latest_exact.status == ExactClockStatus::Pending,
               "the newest timestamp waits for a strictly later anchor");

        Expect(clock->Publish({
                   .sequence = 3,
                   .endpoint_generation = 1,
                   .presented_output_frame = 1'384,
                   .system_time_ns = 1'011'000'000,
                   .submitted_output_tail = 1'960,
               }),
               "third anchor publishes");

        const auto finalized_exact = clock->Resolve(AtMillisecond(1'007), ExactClockResolveIntent::FinalizedTimestamp);
        ExpectFrame(finalized_exact, 1'192, 1, "an exact anchor timestamp finalizes after a later observation");

        if (bracketed.output_frame && finalized_exact.output_frame)
        {
            Expect(bracketed.output_frame->Compare(*finalized_exact.output_frame) < 0,
                   "successive event timestamps cannot regress");
        }
    }

    void ReadyHorizonRetainsProvisionalProjection()
    {
        auto clock = ExactAsioClock::Create(1, 48'000, 10'000'000, 192, 384);
        Expect(clock != nullptr, "horizon clock creation succeeds");
        if (!clock)
        {
            return;
        }

        Expect(clock->Publish({
                   .sequence = 1,
                   .endpoint_generation = 1,
                   .presented_output_frame = 1'000,
                   .system_time_ns = 1'000'000'000,
                   .submitted_output_tail = 1'576,
               }),
               "horizon anchor publishes");

        const auto horizon = clock->Resolve(AtMillisecond(1'006), ExactClockResolveIntent::ProvisionalHorizon);
        ExpectFrame(horizon, 1'288, 1, "the scheduler horizon may use the bounded provisional projection");
    }
} // namespace

int main()
{
    FinalizedResolutionWaitsForBracketAndInterpolates();
    ReadyHorizonRetainsProvisionalProjection();
    return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

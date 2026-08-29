#include "Audio/Wasapi/ExactWasapiClock.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string_view>

namespace
{
    using gc::audio::EndpointClockMapping;
    using gc::audio::ExactClockResolveIntent;
    using gc::audio::ExactClockStatus;
    using gc::audio::ExactWasapiAnchor;
    using gc::audio::ExactWasapiClock;
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

    void PreservesAcceptedQpcToOutputFrameProjection()
    {
        constexpr std::uint64_t generation = 9;
        constexpr std::uint32_t output_rate = 48'000;
        constexpr std::uint64_t endpoint_clock_rate = 48'000;
        constexpr std::int64_t qpc_rate = 10'000'000;

        const auto clock = ExactWasapiClock::Create(
            generation, output_rate, endpoint_clock_rate, qpc_rate, 192);
        Expect(clock != nullptr, "WASAPI exact clock creation succeeds");
        if (clock == nullptr)
        {
            return;
        }

        clock->Publish(ExactWasapiAnchor{
            .sequence = 1,
            .endpoint_generation = generation,
            .endpoint_position = 1'480,
            .qpc_100ns = 100'000,
            .mapping = EndpointClockMapping{
                .origin_position = 1'000,
                .clock_frequency = endpoint_clock_rate,
                .origin_output_frame = 0,
                .output_sample_rate = output_rate,
            },
            .submitted_output_tail = 960,
        });

        constexpr AbsoluteHostTime event{
            .qpc_ticks = 150'000,
            .multimedia_time_ms = 0,
        };
        const auto result = clock->Resolve(
            event, ExactClockResolveIntent::FinalizedTimestamp);
        const auto expected_frame = CheckedRational::Whole(720);

        Expect(result.status == ExactClockStatus::Resolved,
               "event inside submitted output resolves");
        Expect(result.timeline_generation == generation,
               "resolved event preserves endpoint generation");
        Expect(result.logical_output_frame.has_value(),
               "resolved event has an exact output frame");
        if (result.logical_output_frame.has_value())
        {
            Expect(result.logical_output_frame->Compare(expected_frame) == 0,
                   "480 endpoint frames plus five milliseconds equals frame 720");
        }
        Expect(result.provider_anchor_sequence == 1,
               "resolved event identifies the published anchor");
        Expect(result.provider_position == 1'480,
               "resolved event identifies the provider position");
        Expect(result.available_output_tail == 960,
               "resolved event reports the submitted output tail");

        constexpr AbsoluteHostTime at_tail{
            .qpc_ticks = 200'000,
            .multimedia_time_ms = 0,
        };
        const auto pending = clock->Resolve(
            at_tail, ExactClockResolveIntent::FinalizedTimestamp);
        Expect(pending.status == ExactClockStatus::Pending,
               "event at the exclusive submitted tail remains pending");
        Expect(pending.available_output_tail == 960,
               "pending event retains the submitted output tail");
    }
} // namespace

int main()
{
    PreservesAcceptedQpcToOutputFrameProjection();
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

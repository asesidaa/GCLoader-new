#include "Audio/Asio/AsioLogicalRenderSequencer.h"

#include <cstdlib>
#include <iostream>
#include <string_view>

namespace
{
    using gc::audio::AsioClockDecision;
    using gc::audio::AsioClockDecisionKind;
    using gc::audio::AsioLogicalRenderPlanFailure;
    using gc::audio::AsioLogicalRenderSequencer;

    int g_failures{};

    void Expect(const bool condition, const std::string_view message)
    {
        if (!condition)
        {
            std::cerr << "FAIL: " << message << '\n';
            ++g_failures;
        }
    }

    AsioClockDecision StableDecision(
        const std::uint64_t presented,
        const std::uint64_t render_begin,
        const std::uint64_t system_time_ns) noexcept
    {
        return {
            .kind = AsioClockDecisionKind::stable,
            .presented_output_frame = presented,
            .render_output_frame_begin = render_begin,
            .system_time_ns = system_time_ns,
        };
    }

    void RecoveryPreservesEveryElapsedFrame()
    {
        AsioLogicalRenderSequencer sequencer{192, 48'000};

        const auto first_generation = sequencer.BeginPhysicalSession();
        Expect(first_generation.has_value(), "the first physical session receives a generation");
        if (!first_generation)
        {
            return;
        }

        auto first = sequencer.TryPlanPhysical(
            *first_generation,
            StableDecision(0, 384, 1'000'000'000));
        Expect(first.has_value(), "the first physical callback obtains a logical plan");
        if (!first)
        {
            return;
        }
        Expect(first->timeline.output_frame_begin == 0, "the initial logical stream starts at frame zero");
        Expect(first->timeline.discontinuity_frames == 0, "the initial block has no discontinuity");
        Expect(first->submitted_output_tail == 192, "the initial block commits one hardware period");
        Expect(sequencer.Commit(*first), "the initial physical plan commits");
        Expect(sequencer.EndPhysicalSession(*first_generation), "the first physical session ends cleanly");

        const auto second_generation = sequencer.BeginPhysicalSession();
        Expect(second_generation.has_value(), "a replacement session receives a generation");
        Expect(second_generation && *second_generation > *first_generation,
               "physical generations increase without resetting logical time");
        if (!second_generation)
        {
            return;
        }

        auto after_twenty_ms = sequencer.TryPlanPhysical(
            *second_generation,
            StableDecision(0, 384, 1'020'000'000));
        Expect(after_twenty_ms.has_value(), "the recovered callback obtains a plan");
        if (!after_twenty_ms)
        {
            return;
        }
        Expect(after_twenty_ms->timeline.output_frame_begin == 960,
               "twenty elapsed milliseconds map to 960 logical frames");
        Expect(after_twenty_ms->timeline.discontinuity_frames == 768,
               "the recovered block represents every frame after the prior submitted tail");
        Expect(after_twenty_ms->presented_output_frame == 576,
               "physical latency is rebased into the persistent logical domain");
        Expect(sequencer.Commit(*after_twenty_ms), "the recovered physical plan commits");

        auto contiguous = sequencer.TryPlanPhysical(
            *second_generation,
            StableDecision(192, 576, 1'024'000'000));
        Expect(contiguous.has_value(), "the next physical period obtains a plan");
        if (!contiguous)
        {
            return;
        }
        Expect(contiguous->timeline.output_frame_begin == 1'152,
               "the replacement session advances from its committed mapping");
        Expect(contiguous->timeline.discontinuity_frames == 0,
               "the next physical period is contiguous");
        Expect(sequencer.Commit(*contiguous), "the contiguous physical plan commits");
        Expect(sequencer.EndPhysicalSession(*second_generation), "the replacement session ends cleanly");

        const auto third_generation = sequencer.BeginPhysicalSession();
        Expect(third_generation.has_value(), "a second replacement session receives a generation");
        if (!third_generation)
        {
            return;
        }

        auto after_seven_ms = sequencer.TryPlanPhysical(
            *third_generation,
            StableDecision(0, 384, 1'031'000'000));
        Expect(after_seven_ms.has_value(), "the sub-period recovery gap obtains a plan");
        if (!after_seven_ms)
        {
            return;
        }
        Expect(after_seven_ms->timeline.output_frame_begin == 1'488,
               "seven elapsed milliseconds add all 336 logical frames");
        Expect(after_seven_ms->timeline.discontinuity_frames == 144,
               "the sub-period remainder is preserved instead of discarded");
        Expect(sequencer.Commit(*after_seven_ms), "the frame-accurate replacement plan commits");
    }

    void ClaimsAreExclusiveAndTransactional()
    {
        AsioLogicalRenderSequencer sequencer{192, 48'000};
        const auto generation = sequencer.BeginPhysicalSession();
        Expect(generation.has_value(), "the ownership test receives a physical generation");
        if (!generation)
        {
            return;
        }

        auto first = sequencer.TryPlanPhysical(
            *generation,
            StableDecision(0, 384, 2'000'000'000));
        Expect(first.has_value(), "the first caller acquires the render claim");
        if (!first)
        {
            return;
        }

        const auto competing = sequencer.TryPlanPhysical(
            *generation,
            StableDecision(0, 384, 2'000'000'000));
        Expect(!competing.has_value(), "a second caller cannot enter the logical renderer");
        Expect(!competing && competing.error() == AsioLogicalRenderPlanFailure::Busy,
               "claim contention is reported distinctly from a clock fault");

        Expect(sequencer.Abandon(*first), "an unrendered plan can be abandoned");
        auto retried = sequencer.TryPlanPhysical(
            *generation,
            StableDecision(0, 384, 2'000'000'000));
        Expect(retried.has_value(), "abandoning releases the claim");
        if (!retried)
        {
            return;
        }
        Expect(retried->timeline.output_frame_begin == 0,
               "abandoning does not advance the logical cursor");
        Expect(retried->timeline.discontinuity_frames == 0,
               "abandoning does not install a hidden discontinuity");
        Expect(sequencer.Commit(*retried), "the retried plan commits normally");
    }
} // namespace

int main()
{
    RecoveryPreservesEveryElapsedFrame();
    ClaimsAreExclusiveAndTransactional();
    return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

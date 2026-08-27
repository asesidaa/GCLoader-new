#include "Audio/Asio/AsioLogicalRenderSequencer.h"

#include <cstdlib>
#include <iostream>
#include <string_view>

namespace
{
    using gc::audio::AsioLogicalRenderPlan;
    using gc::audio::AsioLogicalRenderPlanFailure;
    using gc::audio::AsioLogicalRenderSequencer;
    using gc::audio::AsioPhysicalAttachmentDisposition;

    int g_failures{};

    void Expect(const bool condition, const std::string_view message)
    {
        if (!condition)
        {
            std::cerr << "FAIL: " << message << '\n';
            ++g_failures;
        }
    }

    void ExpectPlan(
        const std::expected<AsioLogicalRenderPlan,
                            AsioLogicalRenderPlanFailure>& plan,
        const std::uint64_t begin,
        const std::uint64_t discontinuity,
        const std::uint64_t tail,
        const std::string_view message)
    {
        Expect(plan.has_value(), message);
        if (!plan)
        {
            return;
        }
        Expect(plan->timeline.output_frame_begin == begin, message);
        Expect(plan->timeline.discontinuity_frames == discontinuity, message);
        Expect(plan->submitted_output_tail == tail, message);
    }

    void OneAttachmentOwnsEveryLaterPhysicalCoordinate()
    {
        AsioLogicalRenderSequencer sequencer{192};
        const auto generation = sequencer.BeginPhysicalSession();
        Expect(generation.has_value(), "physical generation begins");
        if (!generation)
        {
            return;
        }

        const auto attached = sequencer.AttachPhysicalSession(
            *generation,
            0,
            500);
        Expect(attached.has_value(), "first mapping attaches");

        auto first = sequencer.TryPlanPhysical(*generation, 500);
        ExpectPlan(first, 0, 0, 192, "origin maps exactly");
        Expect(first && sequencer.Commit(*first), "origin block commits");

        auto second = sequencer.TryPlanPhysical(*generation, 692);
        ExpectPlan(second,
                   192,
                   0,
                   384,
                   "later mapping uses only the 192-sample delta");
        Expect(second && sequencer.Commit(*second), "second block commits");

        const auto duplicate_attach = sequencer.AttachPhysicalSession(
            *generation,
            99'000,
            692);
        Expect(
            !duplicate_attach &&
            duplicate_attach.error() ==
            AsioLogicalRenderPlanFailure::PhysicalSessionAlreadyAttached,
            "later callback evidence cannot re-anchor the session");
    }

    void DetachedCoverageProducesOneWaitOrCatchUp()
    {
        AsioLogicalRenderSequencer sequencer{192};
        const auto first_generation = sequencer.BeginPhysicalSession();
        Expect(first_generation.has_value(), "initial generation begins");
        if (!first_generation)
        {
            return;
        }
        Expect(sequencer.AttachPhysicalSession(
                            *first_generation,
                            0,
                            384)
                        .has_value(),
               "initial physical session attaches");
        auto initial = sequencer.TryPlanPhysical(*first_generation, 384);
        ExpectPlan(initial, 0, 0, 192, "initial block starts at zero");
        Expect(initial && sequencer.Commit(*initial), "initial block commits");
        Expect(sequencer.EndPhysicalSession(*first_generation),
               "initial physical session ends");

        auto detached = sequencer.TryPlanDetached(960);
        ExpectPlan(detached,
                   960,
                   768,
                   1'152,
                   "detached absolute target catches up once");
        Expect(detached && sequencer.Commit(*detached),
               "detached block commits");

        const auto second_generation = sequencer.BeginPhysicalSession();
        Expect(second_generation.has_value(), "replacement generation begins");
        if (!second_generation)
        {
            return;
        }
        const auto attachment = sequencer.AttachPhysicalSession(
            *second_generation,
            1'056,
            384);
        Expect(attachment &&
               attachment->disposition ==
               AsioPhysicalAttachmentDisposition::WaitForPhysical &&
               attachment->interval_frames == 96,
               "replacement reports its one wait interval");

        const auto behind = sequencer.TryPlanPhysical(
            *second_generation,
            384);
        Expect(!behind &&
               behind.error() == AsioLogicalRenderPlanFailure::NotDue,
               "covered physical output waits without replay");

        auto caught_up = sequencer.TryPlanPhysical(
            *second_generation,
            576);
        ExpectPlan(caught_up,
                   1'248,
                   96,
                   1'440,
                   "first ahead callback catches the remaining interval once");
        Expect(caught_up && sequencer.Commit(*caught_up),
               "catch-up block commits");

        auto contiguous = sequencer.TryPlanPhysical(
            *second_generation,
            768);
        ExpectPlan(contiguous,
                   1'440,
                   0,
                   1'632,
                   "next callback is contiguous rather than re-corrected");
        Expect(contiguous && sequencer.Commit(*contiguous),
               "contiguous block commits");
    }

    void ClaimsRemainExclusiveAndAbandonIsTransactional()
    {
        AsioLogicalRenderSequencer sequencer{192};
        const auto generation = sequencer.BeginPhysicalSession();
        Expect(generation.has_value(), "claim-test generation begins");
        if (!generation)
        {
            return;
        }
        Expect(sequencer.AttachPhysicalSession(*generation, 0, 0).has_value(),
               "claim test attaches");
        auto held = sequencer.TryPlanPhysical(*generation, 0);
        Expect(held.has_value(), "first caller owns the claim");
        if (!held)
        {
            return;
        }
        const auto competing = sequencer.TryPlanPhysical(*generation, 0);
        Expect(!competing &&
               competing.error() == AsioLogicalRenderPlanFailure::Busy,
               "only one renderer owns the claim");
        Expect(sequencer.Abandon(*held), "abandon releases the claim");
        auto retried = sequencer.TryPlanPhysical(*generation, 0);
        ExpectPlan(retried,
                   0,
                   0,
                   192,
                   "abandon does not advance hidden state");
        Expect(retried && sequencer.Commit(*retried),
               "retried block commits");
    }
} // namespace

int main()
{
    OneAttachmentOwnsEveryLaterPhysicalCoordinate();
    DetachedCoverageProducesOneWaitOrCatchUp();
    ClaimsRemainExclusiveAndAbandonIsTransactional();
    return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

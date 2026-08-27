#include "Audio/Asio/AsioLogicalTimeline.h"

#include "Timing/CheckedRational.h"

#include <cstdint>
#include <cstdlib>
#include <expected>
#include <iostream>
#include <limits>
#include <string_view>

namespace
{
    using gc::audio::AsioLogicalTimeline;
    using gc::audio::AsioLogicalTimelineFailure;
    using gc::timing::CheckedRational;
    using gc::timing::RationalError;

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
        const std::expected<CheckedRational, AsioLogicalTimelineFailure>& actual,
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
        const std::expected<CheckedRational, AsioLogicalTimelineFailure>& actual,
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

    void ProjectsFromOneOriginWithoutFractionalDrift()
    {
        auto timeline = AsioLogicalTimeline::Create(1'000, 44'100);
        Expect(timeline != nullptr, "44.1-kHz timeline is valid");
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

        auto forty_eight = AsioLogicalTimeline::Create(50, 48'000);
        Expect(forty_eight != nullptr, "48-kHz timeline is valid");
        if (!forty_eight)
        {
            return;
        }
        ExpectRational(forty_eight->ProjectMultimediaMilliseconds(51),
                       48,
                       1,
                       "48-kHz projection remains exact");
    }

    void WrapAndSnapshotAdvanceDoNotRebaseAnOldEvent()
    {
        constexpr auto origin = (std::numeric_limits<std::uint32_t>::max)() - 5;
        auto timeline = AsioLogicalTimeline::Create(origin, 44'100);
        Expect(timeline != nullptr, "wrap timeline is valid");
        if (!timeline)
        {
            return;
        }

        Expect(timeline->AdvanceNow(3).has_value(),
               "writer crosses UINT32_MAX exactly");
        const auto before = timeline->ProjectMultimediaMilliseconds(origin + 3);
        ExpectRational(before,
                       1'323,
                       10,
                       "three pre-wrap milliseconds are 1323/10 frames");

        Expect(timeline->AdvanceNow(20).has_value(),
               "writer advances the stable snapshot after wrap");
        const auto after = timeline->ProjectMultimediaMilliseconds(origin + 3);
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
} // namespace

int main()
{
    ProjectsFromOneOriginWithoutFractionalDrift();
    WrapAndSnapshotAdvanceDoNotRebaseAnOldEvent();
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

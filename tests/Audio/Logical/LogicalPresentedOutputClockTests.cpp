#include "Audio/Logical/LogicalPresentedOutputClock.h"

#include "Audio/Logical/LogicalPresentationClock.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string_view>

namespace
{
    using gc::audio::LogicalPresentationClock;
    using gc::audio::LogicalPresentedOutputClock;
    using gc::audio::LogicalPresentedOutputClockActions;

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

    void CursorIsTheMonotonicFloorOfLogicalNow()
    {
        const auto timeline = LogicalPresentationClock::Create(
            17, 1'000, 48'000, 10'000'000);
        Expect(timeline != nullptr, "logical clock creation succeeds");
        if (!timeline)
        {
            return;
        }

        FakeClock now{.now_ms = 1'004};
        LogicalPresentedOutputClock presented(
            LogicalPresentedOutputClockActions{
                .context = &now,
                .time_get_time_ms = &FakeClock::Read,
            },
            timeline);

        const auto first = presented.CurrentOutputFrame();
        Expect(first == 192,
               "four logical milliseconds at 48 kHz is frame 192");

        now.now_ms = 1'003;
        const auto nondecreasing = presented.CurrentOutputFrame();
        Expect(nondecreasing == 192,
               "a stale observation cannot move the cursor backwards");

        now.now_ms = 1'020;
        const auto later = presented.CurrentOutputFrame();
        Expect(later == 960,
               "twenty logical milliseconds at 48 kHz is frame 960");

        presented.Invalidate();
        Expect(!presented.CurrentOutputFrame().has_value(),
               "invalidated logical cursor becomes unavailable");
    }
} // namespace

int main()
{
    CursorIsTheMonotonicFloorOfLogicalNow();
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

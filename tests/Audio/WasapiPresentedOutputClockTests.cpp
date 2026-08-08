// SPDX-License-Identifier: CC0-1.0

#include "Audio/Wasapi/WasapiPresentedOutputClock.h"

#include <cstdint>
#include <iostream>
#include <string_view>

namespace {

using gc::audio::WasapiPresentedOutputClock;

int Expect(bool condition, std::string_view name)
{
    if (condition)
    {
        return 0;
    }
    std::cerr << "Expected " << name << '\n';
    return 1;
}

struct FakeNow
{
    bool succeeds{true};
    std::uint64_t ticks{};
    int calls{};
};

bool ReadFakeNow(void* context, std::uint64_t* ticks) noexcept
{
    auto& now = *static_cast<FakeNow*>(context);
    ++now.calls;
    if (!now.succeeds || ticks == nullptr)
    {
        return false;
    }
    *ticks = now.ticks;
    return true;
}

int TestProjectionCapMonotonicFailureAndInvalidation()
{
    FakeNow now{};
    WasapiPresentedOutputClock clock(
        gc::audio::kFallbackEndpointSampleRate,
        {&now, &ReadFakeNow, 1'000});
    int failures{};
    failures += Expect(
        !clock.CurrentOutputFrame().has_value() && now.calls == 1,
        "clock has no frame before the first endpoint observation");

    clock.Publish(100, 9'000'000, 5'000);
    now.ticks = 1'000;
    failures += Expect(
        clock.CurrentOutputFrame() == 4'900,
        "QPC projection uses cached frequency and output rate");

    now.ticks = 2'000;
    failures += Expect(
        clock.CurrentOutputFrame() == 4'999,
        "projection is capped below the submitted render tail");

    now.ticks = 950;
    failures += Expect(
        clock.CurrentOutputFrame() == 4'999,
        "regressing QPC input cannot regress the public frame");

    now.succeeds = false;
    failures += Expect(
        clock.CurrentOutputFrame() == 4'999,
        "counter failure retains the last monotonic public frame");

    clock.Invalidate();
    failures += Expect(
        clock.CurrentOutputFrame() == 4'999,
        "invalidation removes projection but preserves the last returned frame");
    return failures;
}

int TestInvalidActionFrequencyAndAnchor()
{
    FakeNow now{};
    int failures{};
    WasapiPresentedOutputClock no_action(
        gc::audio::kGamePrimarySampleRate,
        {nullptr, nullptr, 10'000'000});
    no_action.Publish(10, 10, 20);
    failures += Expect(
        !no_action.CurrentOutputFrame().has_value(),
        "missing counter action cannot manufacture an output frame");

    WasapiPresentedOutputClock no_frequency(
        gc::audio::kGamePrimarySampleRate,
        {&now, &ReadFakeNow, 0});
    no_frequency.Publish(10, 10, 20);
    failures += Expect(
        !no_frequency.CurrentOutputFrame().has_value(),
        "zero cached QPC frequency cannot manufacture an output frame");

    WasapiPresentedOutputClock invalid_anchor(
        gc::audio::kGamePrimarySampleRate,
        {&now, &ReadFakeNow, 10'000'000});
    invalid_anchor.Publish(20, 10, 20);
    failures += Expect(
        !invalid_anchor.CurrentOutputFrame().has_value(),
        "invalid presentation anchor remains unpublished");
    return failures;
}

} // namespace

int main()
{
    int failures{};
    failures += TestProjectionCapMonotonicFailureAndInvalidation();
    failures += TestInvalidActionFrequencyAndAnchor();
    return failures == 0 ? 0 : 1;
}

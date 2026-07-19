#include "Patches/Framerate/FramerateAuthoredClock.h"
#include "Patches/Framerate/FramerateProfile.h"

#include <array>
#include <cstdint>
#include <iostream>
#include <limits>

namespace {

int Expect(bool condition, const char* name) {
    if (condition) {
        return 0;
    }
    std::cerr << "Expectation failed: " << name << "\n";
    return 1;
}

} // namespace

int main() {
    using namespace gc::framerate;
    int failures = 0;

    for (const std::uint32_t target :
         {60U, 61U, 120U, 144U, 165U, 240U, 360U, 500U}) {
        const auto profile = FramerateProfile::Create(target).value();
        Authored60PhaseClock clock{profile};
        std::uint32_t ticks = 0;
        for (std::uint32_t call = 0; call < target; ++call) {
            ticks += clock.Advance() ? 1U : 0U;
        }
        failures += Expect(
            ticks == 60,
            "phase clock emits 60 ticks per target second");

        Authored60PhaseClock left{profile};
        Authored60PhaseClock right{profile};
        for (std::uint32_t call = 0; call < target * 2; ++call) {
            failures += Expect(
                left.Advance() == right.Advance(),
                "phase clock reconstruction is deterministic");
        }
    }

    {
        const auto profile = FramerateProfile::Create(240).value();
        Authored60PhaseClock clock{profile};
        constexpr std::array expected{
            true, false, false, false, true, false, false, false};
        for (const bool value : expected) {
            failures += Expect(
                clock.Advance() == value,
                "240 phase sequence");
        }
    }

    {
        const auto profile = FramerateProfile::Create(144).value();
        failures += Expect(
            ScalePositiveDuration(profile, 25).value() == 60,
            "positive duration scales rationally");
        failures += Expect(
            ScalePositiveDuration(profile, 0).value() == 0 &&
                ScalePositiveDuration(profile, UINT32_MAX).value() ==
                    UINT32_MAX,
            "signed nonpositive duration sentinels survive");
    }

    {
        const auto profile = FramerateProfile::Create(240).value();
        failures += Expect(
            MapPlayerPositionElapsedToAuthored60(
                profile, 120, 480).value() == 0,
            "player position starts at authored frame zero");
        failures += Expect(
            MapPlayerPositionElapsedToAuthored60(
                profile, 120, 476).value() == 1,
            "player position maps target elapsed to authored frame");
        failures += Expect(
            MapPlayerPositionElapsedToAuthored60(
                profile, 120, 0).value() == 120,
            "player position completes at authored duration");
        failures += Expect(
            MapPlayerPositionElapsedToAuthored60(
                profile, 120, 481).value() == UINT32_MAX,
            "player position preserves negative elapsed sentinel");
    }

    constexpr std::array periods{4U, 5U, 6U, 8U, 16U};
    for (const std::uint32_t target :
         {60U, 120U, 144U, 165U, 240U, 360U, 500U}) {
        const auto profile = FramerateProfile::Create(target).value();
        std::uint32_t boundaries = 0;
        std::array<std::uint32_t, periods.size()> events{};
        for (std::uint32_t frame = 0; frame < target; ++frame) {
            const auto boundary =
                IsAuthored60FrameBoundary(profile, frame);
            failures += Expect(
                boundary.has_value(), "boundary mapping succeeds");
            if (boundary) {
                boundaries += boundary.value();
            }
            for (std::size_t index = 0; index < periods.size(); ++index) {
                const auto run = ShouldRunAuthored60Cadence(
                    profile, frame, 0, periods[index]);
                failures += Expect(
                    run.has_value(), "cadence mapping succeeds");
                if (run) {
                    events[index] += run.value();
                }
            }
        }
        failures += Expect(boundaries == 60, "60 boundaries per second");
        failures += Expect(
            events == std::array<std::uint32_t, 5>{15, 12, 10, 8, 4},
            "native authored cadence counts");
    }

    const auto profile240 = FramerateProfile::Create(240).value();
    failures += Expect(
        IsAuthored60FrameBoundary(profile240, 0).value() &&
            !IsAuthored60FrameBoundary(profile240, 1).value() &&
            !IsAuthored60FrameBoundary(profile240, 3).value() &&
            IsAuthored60FrameBoundary(profile240, 4).value(),
        "240 boundary sequence");
    failures += Expect(
        ShouldRunAuthored60Cadence(profile240, 12, 1, 4).value() &&
            ShouldRunAuthored60Cadence(profile240, 4, -1, 4).value(),
        "signed phase uses mathematical remainder");
    failures += Expect(
        !ShouldRunAuthored60Cadence(profile240, 0, 0, 0),
        "zero period rejected");

    failures += Expect(
        ReconstructUnsignedModuloDividend(7, 3, 4).value() == 31,
        "remote dividend reconstruction");
    failures += Expect(
        !ReconstructUnsignedModuloDividend(7, 4, 4),
        "remainder equal to divisor rejected");
    failures += Expect(
        !ReconstructUnsignedModuloDividend(7, 0, 0),
        "zero divisor rejected");
    failures += Expect(
        !ReconstructUnsignedModuloDividend(
            std::numeric_limits<std::uint32_t>::max(), 3, 4),
        "remote dividend overflow rejected");

    failures += Expect(
        MapPositiveTargetFrameToAuthored60(profile240, 240).value() == 60 &&
            MapPositiveTargetFrameToAuthored60(profile240, 0).value() == 0 &&
            MapPositiveTargetFrameToAuthored60(
                profile240,
                std::numeric_limits<std::uint32_t>::max()).value() ==
                std::numeric_limits<std::uint32_t>::max(),
        "positive mapping preserves nonpositive signed values");

    return failures == 0 ? 0 : 1;
}

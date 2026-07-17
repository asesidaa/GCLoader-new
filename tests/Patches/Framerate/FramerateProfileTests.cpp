#include "Patches/Framerate/FramerateProfile.h"

#include <cmath>
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

bool Near(float actual, float expected, float tolerance = 0.00001F) {
    return std::fabs(actual - expected) <= tolerance;
}

} // namespace

int main() {
    using gc::framerate::FramerateProfile;
    int failures = 0;

    failures += Expect(!FramerateProfile::Create(59), "reject 59");
    failures += Expect(!FramerateProfile::Create(501), "reject 501");

    constexpr std::uint32_t validated[]{60, 120, 144, 165, 240, 360};
    for (const auto target : validated) {
        const auto created = FramerateProfile::Create(target);
        failures += Expect(created.has_value(), "create validated profile");
        if (!created) {
            continue;
        }
        const auto& profile = created.value();
        failures += Expect(profile.target_fps() == target, "retain target");
        failures += Expect(
            profile.gameplay_validated(), "validated target marker");
        failures += Expect(
            Near(profile.frame_milliseconds(), 1000.0F / target),
            "frame milliseconds");
        failures += Expect(
            Near(profile.frame_seconds(), 1.0F / target),
            "frame seconds");
        failures += Expect(
            profile.two_second_frames() == target * 2,
            "two-second frames");
        failures += Expect(
            profile.palette_frame_cap() == target,
            "palette frame cap");
        failures += Expect(
            Near(profile.render_smoothing_step(), 4.0F * 60.0F / target),
            "render smoothing");
        failures += Expect(
            Near(profile.render_offset_decay_step(), 5.0F * 60.0F / target),
            "render decay");
    }

    const auto native = FramerateProfile::Create(60).value();
    failures += Expect(native.native_timing(), "60 is native");
    const auto formula_only = FramerateProfile::Create(200).value();
    failures += Expect(
        !formula_only.gameplay_validated(), "200 is warning-only");

    const auto target120 = FramerateProfile::Create(120).value();
    failures += Expect(
        target120.ScaleDurationFrames(16).value() == 32,
        "120 initial repeat");
    failures += Expect(
        target120.ScaleDurationFrames(8).value() == 16,
        "120 next repeat");

    const auto target144 = FramerateProfile::Create(144).value();
    failures += Expect(
        target144.ScaleDurationFrames(16).value() == 38,
        "144 initial repeat nearest");
    failures += Expect(
        target144.ScaleDurationFrames(8).value() == 19,
        "144 next repeat nearest");

    const auto half_up = FramerateProfile::Create(90).value();
    failures += Expect(
        half_up.ScaleDurationFrames(1).value() == 2,
        "positive half rounds up");
    failures += Expect(
        half_up.ScaleDurationFrames(0).value() == 0,
        "zero sentinel preserved");
    failures += Expect(
        half_up.ScaleDurationFrames(-1).value() == -1,
        "negative sentinel preserved");

    const auto overflow = FramerateProfile::Create(500).value()
        .ScaleDurationFrames(std::numeric_limits<std::int32_t>::max());
    failures += Expect(!overflow, "duration destination overflow rejected");

    for (std::uint32_t target = 60; target <= 500; ++target) {
        const auto profile = FramerateProfile::Create(target).value();
        std::uint32_t previous = 0;
        for (std::uint32_t frame = 0; frame <= target; ++frame) {
            const auto mapped = profile.MapToAuthored60(frame);
            failures += Expect(mapped.has_value(), "authored mapping succeeds");
            if (!mapped) {
                continue;
            }
            failures += Expect(mapped.value() >= previous, "mapping monotonic");
            failures += Expect(
                static_cast<std::uint64_t>(mapped.value()) * target <=
                    static_cast<std::uint64_t>(frame) * 60,
                "mapping never selects future frame");
            previous = mapped.value();
        }
        failures += Expect(
            profile.MapToAuthored60(target).value() == 60,
            "one-second boundary maps to 60");
        for (std::int32_t duration = 1; duration <= 120; ++duration) {
            const auto scaled = profile.ScaleDurationFrames(duration);
            failures += Expect(scaled.has_value(), "duration scaling succeeds");
            if (!scaled) {
                continue;
            }
            const double wall_clock_error = std::fabs(
                static_cast<double>(scaled.value()) / target -
                static_cast<double>(duration) / 60.0);
            failures += Expect(
                wall_clock_error <= 0.5 / target + 1.0e-12,
                "duration error is at most half a target frame");
        }
    }

    return failures == 0 ? 0 : 1;
}

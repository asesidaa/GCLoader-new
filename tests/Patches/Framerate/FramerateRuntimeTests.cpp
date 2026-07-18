#include "Patches/Framerate/FrameratePatch.h"
#include "Patches/Framerate/FramerateAuthoredClock.h"
#include "Patches/Framerate/FrameratePatchPlan.h"
#include "Patches/Framerate/FrameratePatchTransaction.h"
#include "Patches/Framerate/FramerateProfile.h"

#include <concepts>
#include <cstdint>
#include <iostream>

using namespace gc::framerate;

int Expect(bool condition, const char* name) {
    if (condition) {
        return 0;
    }
    std::cerr << "Expectation failed: " << name << "\n";
    return 1;
}

int main() {
int failures = 0;

for (const std::uint32_t target : {120U, 144U, 165U, 240U, 360U}) {
    const auto profile = FramerateProfile::Create(target).value();
    for (std::uint32_t frame = 0; frame < target * 3; ++frame) {
        const auto mapped = profile.MapToAuthored60(frame).value();
        failures += Expect(
            mapped == static_cast<std::uint64_t>(frame) * 60 / target,
            "stage clip uses rational floor mapping");
    }
    failures += Expect(
        profile.ScaleDurationFrames(16).value() ==
            (16 * static_cast<std::int64_t>(target) + 30) / 60,
        "input delay uses nearest rational scaling");
}

const auto profile144 = FramerateProfile::Create(144).value();
failures += Expect(
    profile144.ScaleDurationFrames(25).value() == 60,
    "audio interval scales without integer ratio");
failures += Expect(
    profile144.ScaleDurationFrames(0).value() == 0 &&
        profile144.ScaleDurationFrames(-1).value() == -1,
    "runtime counter sentinels survive");

const auto profile240 = FramerateProfile::Create(240).value();
for (const auto frame : {0U, 4U, 8U}) {
    failures += Expect(
        IsAuthored60FrameBoundary(profile240, frame).value(),
        "effect advance accepts authored boundary");
}
for (const auto frame : {1U, 2U, 3U, 5U, 6U, 7U}) {
    failures += Expect(
        !IsAuthored60FrameBoundary(profile240, frame).value(),
        "effect advance rejects duplicate target frame");
}
failures += Expect(
    ShouldRunAuthored60Cadence(profile240, 24, 0, 6).value() &&
        !ShouldRunAuthored60Cadence(profile240, 20, 0, 6).value() &&
        !ShouldRunAuthored60Cadence(profile240, 25, 0, 6).value(),
    "period-six effect cadence uses authored boundaries");
failures += Expect(
    ReconstructUnsignedModuloDividend(15, 0, 4).value() == 60,
    "remote modulo reconstructs target frame");
failures += Expect(
    MapPositiveTargetFrameToAuthored60(profile240, 8).value() == 2,
    "blink maps target frames to authored frames");

static_assert(kMaximumFramerateHooks >= 25);
for (const auto& contract : FramerateHookContracts(true)) {
    failures += Expect(
        FramerateHookHasRuntimeBinding(contract.id),
        "every transformed contract has a runtime binding");
}

for (const std::uint32_t cap : {120U, 144U, 165U, 240U, 360U, 500U}) {
    const auto below = ApplyCmp32Flags(0x202, cap - 1, cap);
    const auto equal = ApplyCmp32Flags(0x202, cap, cap);
    const auto above = ApplyCmp32Flags(0x202, cap + 1, cap);
    failures += Expect((below & 0x40U) == 0, "palette below is not equal");
    failures += Expect((equal & 0x40U) != 0, "palette equal sets ZF");
    failures += Expect((above & 0x40U) == 0, "palette above is not equal");
    failures += Expect(
        ((below >> 7U) & 1U) != ((below >> 11U) & 1U),
        "signed JGE sees below as less");
    failures += Expect(
        ((equal >> 7U) & 1U) == ((equal >> 11U) & 1U) &&
            ((above >> 7U) & 1U) == ((above >> 11U) & 1U),
        "signed JGE sees equal and above as not less");
}

static_assert(std::same_as<
    decltype(gc::framerate::FrameratePatchInit()), bool>);

return failures == 0 ? 0 : 1;
}

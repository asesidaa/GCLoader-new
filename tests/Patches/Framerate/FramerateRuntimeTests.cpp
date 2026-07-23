#include "Patches/Framerate/FrameratePatch.h"
#include "Patches/Framerate/FramerateAuthoredClock.h"
#include "Patches/Framerate/FramerateHookTransforms.h"
#include "Patches/Framerate/FrameratePatchPlan.h"
#include "Patches/Framerate/FrameratePatchTransaction.h"
#include "Patches/Framerate/FramerateProfile.h"

#include <concepts>
#include <cstddef>
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

namespace {

std::uintptr_t g_read_address{};
std::uint32_t g_read_value{};
bool g_read_succeeds{true};

bool ReadTransformValue(
    std::uintptr_t address,
    std::uint32_t& value) noexcept {
    g_read_address = address;
    if (!g_read_succeeds) {
        return false;
    }
    value = g_read_value;
    return true;
}

} // namespace

int main() {
int failures = 0;

static_assert(offsetof(AuthoredFrameOperand, frame_milliseconds) == 0x18);
static_assert(offsetof(PlayerPositionDurationOperand, duration_frames) == 0xC4);

AuthoredFrameOperand authored_operand{};
safetyhook::Context redirected{};
redirected.eax = 1;
redirected.ecx = 2;
redirected.edx = 3;
redirected.eip = 4;
RedirectEcxToAuthoredOperand(redirected, authored_operand);
failures += Expect(
    redirected.eax == 1 &&
        redirected.ecx == reinterpret_cast<std::uintptr_t>(&authored_operand) &&
        redirected.edx == 3 && redirected.eip == 4,
    "authored operand changes only selected register");

const auto profile240 = FramerateProfile::Create(240).value();
safetyhook::Context countdown{};
countdown.ecx = 480;
countdown.eip = 0x1111;
failures += Expect(
    MapCountdownAssetFrame(countdown, profile240).has_value() &&
        countdown.ecx == 120 && countdown.eip == 0x1111,
    "countdown maps final asset frame and executes original store");

safetyhook::Context initializer{};
initializer.eax = 120;
initializer.eip = 0x2222;
failures += Expect(
    ScalePlayerPositionDurationEax(initializer, profile240).has_value() &&
        initializer.eax == 480 && initializer.eip == 0x2222,
    "player initializer scales EAX and executes original store");

safetyhook::Context asset{};
asset.eax = 120;
asset.edx = 0x1000;
asset.ecx = 3;
asset.eip = 0x2000;
g_read_value = 476;
g_read_succeeds = true;
failures += Expect(
    MapPlayerPositionAssetFrame(
        asset, profile240, ReadTransformValue).has_value() &&
        g_read_address == 0x1000 + 3 * 4 + 0x1D54 &&
        asset.eax == 1 && asset.eip == 0x2007,
    "player asset hook reads indexed remaining and skips seven bytes");

PlayerPositionDurationOperand duration_operand{};
safetyhook::Context denominator{};
denominator.eax = 0x3000;
denominator.eip = 0x4000;
g_read_value = 120;
failures += Expect(
    PreparePlayerPositionDenominator(
        denominator,
        profile240,
        duration_operand,
        ReadTransformValue).has_value() &&
        g_read_address == 0x30C4 &&
        duration_operand.duration_frames == 480 &&
        denominator.eax ==
            reinterpret_cast<std::uintptr_t>(&duration_operand) &&
        denominator.eip == 0x4000,
    "denominator redirects EAX and leaves original fild active");

g_read_succeeds = false;
safetyhook::Context failed_asset{};
failed_asset.eax = 120;
failed_asset.edx = 0x5000;
failed_asset.ecx = 1;
failed_asset.eip = 0x6000;
failures += Expect(
    !MapPlayerPositionAssetFrame(
        failed_asset, profile240, ReadTransformValue) &&
        failed_asset.eax == 120 && failed_asset.eip == 0x6000,
    "read failure leaves player context unchanged");

safetyhook::Context eax_redirect{};
eax_redirect.eax = 1;
eax_redirect.ecx = 2;
eax_redirect.edx = 3;
RedirectEaxToAuthoredOperand(eax_redirect, authored_operand);
failures += Expect(
    eax_redirect.eax == reinterpret_cast<std::uintptr_t>(&authored_operand) &&
        eax_redirect.ecx == 2 && eax_redirect.edx == 3,
    "EAX authored redirect preserves ECX and EDX");

safetyhook::Context edx_redirect{};
edx_redirect.eax = 1;
edx_redirect.ecx = 2;
edx_redirect.edx = 3;
RedirectEdxToAuthoredOperand(edx_redirect, authored_operand);
failures += Expect(
    edx_redirect.eax == 1 && edx_redirect.ecx == 2 &&
        edx_redirect.edx ==
            reinterpret_cast<std::uintptr_t>(&authored_operand),
    "EDX authored redirect preserves EAX and ECX");

for (const std::uint32_t sentinel : {0U, UINT32_MAX}) {
    safetyhook::Context sentinel_context{};
    sentinel_context.eax = sentinel;
    failures += Expect(
        ScalePlayerPositionDurationEax(
            sentinel_context, profile240).has_value() &&
            sentinel_context.eax == sentinel,
        "player initializer preserves signed nonpositive sentinel");
}

safetyhook::Context overflow_context{};
overflow_context.eax = static_cast<std::uint32_t>(INT32_MAX);
const auto profile500 = FramerateProfile::Create(500).value();
failures += Expect(
    !ScalePlayerPositionDurationEax(overflow_context, profile500) &&
        overflow_context.eax == static_cast<std::uint32_t>(INT32_MAX),
    "player initializer rejects overflow without mutation");

safetyhook::Context completed_asset{};
completed_asset.eax = 120;
completed_asset.edx = 0x7000;
completed_asset.ecx = 0;
completed_asset.eip = 0x8000;
g_read_value = 0;
g_read_succeeds = true;
failures += Expect(
    MapPlayerPositionAssetFrame(
        completed_asset, profile240, ReadTransformValue).has_value() &&
        completed_asset.eax == 120 && completed_asset.eip == 0x8007,
    "completed player duration maps to authored frame 120");

PlayerPositionDurationOperand unchanged_operand{};
unchanged_operand.duration_frames = 77;
safetyhook::Context failed_denominator{};
failed_denominator.eax = 0x9000;
failed_denominator.eip = 0xA000;
g_read_succeeds = false;
failures += Expect(
    !PreparePlayerPositionDenominator(
        failed_denominator,
        profile240,
        unchanged_operand,
        ReadTransformValue) &&
        failed_denominator.eax == 0x9000 &&
        failed_denominator.eip == 0xA000 &&
        unchanged_operand.duration_frames == 77,
    "denominator read failure leaves context and operand unchanged");

for (const std::uint32_t target :
     {60U, 61U, 120U, 144U, 165U, 240U, 360U, 500U}) {
    const auto profile = FramerateProfile::Create(target).value();
    std::uint32_t previous = 0;
    for (std::uint32_t frame = 0; frame <= target * 2U; ++frame) {
        safetyhook::Context mapped_context{};
        mapped_context.ecx = frame;
        failures += Expect(
            MapCountdownAssetFrame(mapped_context, profile).has_value() &&
                mapped_context.ecx >= previous,
            "countdown asset mapping is monotonic");
        previous = mapped_context.ecx;
    }
    failures += Expect(
        previous == 120,
        "two target seconds map to 120 authored countdown frames");
}

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

static_assert(kMaximumFramerateHooks == 42);
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
    decltype(gc::framerate::FrameratePatchInit(false)), bool>);

return failures == 0 ? 0 : 1;
}

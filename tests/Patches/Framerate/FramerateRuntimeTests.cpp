#include "Patches/Framerate/FrameratePatch.h"
#include "Patches/Framerate/FramerateAuthoredClock.h"
#include "Patches/Framerate/FramerateHookTransforms.h"
#include "Patches/Framerate/FrameratePatchPlan.h"
#include "Patches/Framerate/FrameratePatchTransaction.h"
#include "Patches/Framerate/FramerateProfile.h"

#include <array>
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

using gc::audio::GameplayAudioCursorObservation;
using gc::audio::GameplayAudioCursorState;
using gc::framerate::detail::GameplaySongClockInputState;

const GameplayAudioCursorObservation exact_cursor{
    .query_serial = 17,
    .state = GameplayAudioCursorState::Exact,
    .source_frame_unwrapped = 88'200,
    .source_sample_rate = 44'100,
    .playback_generation = 23,
    .output_frame = 96'000,
};
const auto exact_input =
    gc::framerate::detail::SelectGameplaySongClockInput(
        2'000, exact_cursor);
failures += Expect(
    exact_input.state == GameplaySongClockInputState::Exact &&
        exact_input.observation.has_value() &&
        exact_input.observation->kind ==
            SongClockObservationKind::ExactSourceFrame &&
        exact_input.observation->position == 88'200 &&
        exact_input.observation->source_sample_rate == 44'100 &&
        exact_input.observation->playback_generation == 23 &&
        exact_input.output_frame == 96'000,
    "fresh exact cursor wins over rounded group milliseconds");

const auto rounded_input =
    gc::framerate::detail::SelectGameplaySongClockInput(
        2'000, std::nullopt);
failures += Expect(
    rounded_input.state == GameplaySongClockInputState::Rounded &&
        rounded_input.observation.has_value() &&
        rounded_input.observation->kind ==
            SongClockObservationKind::RoundedMilliseconds &&
        rounded_input.observation->position == 2'000,
    "successful group getter falls back to absolute milliseconds");

const GameplayAudioCursorObservation inactive_cursor{
    .query_serial = 18,
    .state = GameplayAudioCursorState::Inactive,
};
const auto inactive_input =
    gc::framerate::detail::SelectGameplaySongClockInput(
        -1, inactive_cursor);
const auto failed_input =
    gc::framerate::detail::SelectGameplaySongClockInput(
        -1, std::nullopt);
failures += Expect(
    inactive_input.state == GameplaySongClockInputState::Inactive &&
        !inactive_input.observation &&
        failed_input.state == GameplaySongClockInputState::Failed &&
        !failed_input.observation,
    "negative group result distinguishes inactive from failed");

auto exact_clock = GameplaySongClock::Create(60, 1).value();
const auto exact_resolution =
    gc::framerate::detail::ResolveGameplaySongClockStep(
        exact_clock, 119, 0, 2'000, exact_cursor);
failures += Expect(
    exact_resolution.input.state ==
            GameplaySongClockInputState::Exact &&
        exact_resolution.decision.has_value() &&
        exact_resolution.step == 1 &&
        !exact_resolution.observation_rejected,
    "valid exact selection resolves a gameplay step");

auto fallback_clock = GameplaySongClock::Create(60, 1).value();
const auto inactive_resolution =
    gc::framerate::detail::ResolveGameplaySongClockStep(
        fallback_clock, 120, 0, -1, inactive_cursor);
const auto failed_resolution =
    gc::framerate::detail::ResolveGameplaySongClockStep(
        fallback_clock, 120, 0, -1, std::nullopt);
auto ended_buffer_clock = GameplaySongClock::Create(240, 1).value();
const auto ended_buffer_resolution =
    gc::framerate::detail::ResolveGameplaySongClockStep(
        ended_buffer_clock,
        32'654,
        0,
        136'062,
        inactive_cursor);
const GameplayAudioCursorObservation invalid_exact_cursor{
    .query_serial = 19,
    .state = GameplayAudioCursorState::Exact,
    .source_frame_unwrapped = 88'200,
    .source_sample_rate = 0,
    .playback_generation = 24,
    .output_frame = 96'001,
};
const auto invalid_resolution =
    gc::framerate::detail::ResolveGameplaySongClockStep(
        fallback_clock, 120, 0, 2'000, invalid_exact_cursor);
failures += Expect(
    inactive_resolution.step == 1 &&
        !inactive_resolution.decision &&
        !inactive_resolution.observation_rejected &&
        failed_resolution.step == 1 &&
        !failed_resolution.decision &&
        !failed_resolution.observation_rejected &&
        invalid_resolution.step == 1 &&
        !invalid_resolution.decision &&
        invalid_resolution.observation_rejected,
    "inactive failed and invalid observations preserve initialized step one");
failures += Expect(
    ended_buffer_resolution.input.state ==
            GameplaySongClockInputState::Inactive &&
        ended_buffer_resolution.step == 1 &&
        !ended_buffer_resolution.decision &&
        !ended_buffer_resolution.observation_rejected,
    "inactive publication overrides a nonnegative final group cursor");

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
const auto shared_mode =
    GameplayAudioClockPlan::WasapiSharedSongClock;
const auto original_mode =
    GameplayAudioClockPlan::OriginalWatchdog;

failures += Expect(
    !gc::framerate::detail::ShouldRunGameplayCadence(
         FramerateProfile::Create(60).value(),
         shared_mode,
         0,
         0,
         0,
         4).value() &&
        gc::framerate::detail::CountGameplayEffectAdvances(
            FramerateProfile::Create(60).value(),
            shared_mode,
            0,
            0).value() == 0,
    "shared target 60 step zero runs no gameplay consumer");
failures += Expect(
    gc::framerate::detail::ShouldRunGameplayCadence(
        FramerateProfile::Create(60).value(),
        shared_mode,
        4,
        1,
        0,
        4).value() &&
        gc::framerate::detail::CountGameplayEffectAdvances(
            FramerateProfile::Create(60).value(),
            shared_mode,
            4,
            1).value() == 1,
    "shared target 60 step one preserves a normal event tick");
failures += Expect(
    gc::framerate::detail::ShouldRunGameplayCadence(
        FramerateProfile::Create(60).value(),
        shared_mode,
        3,
        2,
        0,
        4).value() &&
        gc::framerate::detail::CountGameplayEffectAdvances(
            FramerateProfile::Create(60).value(),
            shared_mode,
            3,
            2).value() == 2,
    "shared target 60 step two preserves intermediate consumers");

for (const std::uint32_t target : {144U, 165U}) {
    const auto profile = FramerateProfile::Create(target).value();
    for (std::uint32_t current = 0; current < target * 2; ++current) {
        for (const std::uint32_t step : {0U, 1U, 2U, 5U}) {
            bool cadence_oracle = false;
            for (std::uint32_t offset = 0; offset < step; ++offset) {
                cadence_oracle =
                    cadence_oracle ||
                    ShouldRunAuthored60Cadence(
                        profile,
                        current + offset,
                        -3,
                        5).value();
            }
            const auto begin =
                profile.MapToAuthored60(current).value();
            const auto end =
                profile.MapToAuthored60(current + step).value();
            failures += Expect(
                gc::framerate::detail::ShouldRunGameplayCadence(
                    profile,
                    shared_mode,
                    current,
                    step,
                    -3,
                    5).value() == cadence_oracle,
                "fractional target shared cadence matches tick oracle");
            failures += Expect(
                gc::framerate::detail::CountGameplayEffectAdvances(
                    profile,
                    shared_mode,
                    current,
                    step).value() == end - begin,
                "fractional target shared effect count matches rational oracle");
        }
    }
}

for (std::uint32_t current = 0; current < 16; ++current) {
    failures += Expect(
        gc::framerate::detail::CountGameplayEffectAdvances(
            profile240,
            shared_mode,
            current,
            1).value() == (current % 4 == 3 ? 1U : 0U),
        "target 240 step one advances effects every fourth tick");
}
failures += Expect(
    gc::framerate::detail::CountGameplayEffectAdvances(
        profile240,
        shared_mode,
        2,
        10).value() == 3,
    "bounded multi-step update counts every authored crossing");
failures += Expect(
    gc::framerate::detail::ShouldRunGameplayCadence(
        profile240,
        original_mode,
        24,
        0,
        0,
        6).value() ==
        ShouldRunAuthored60Cadence(
            profile240, 24, 0, 6).value() &&
        gc::framerate::detail::CountGameplayEffectAdvances(
            profile240,
            original_mode,
            4,
            0).value() ==
        static_cast<std::uint32_t>(
            IsAuthored60FrameBoundary(profile240, 4).value()),
    "non-shared consumers retain point-test semantics");

using CadenceRegister =
    gc::framerate::detail::GameplayCadenceTestRegister;
struct ExpectedCadenceSemantics {
    FramerateHookId id;
    std::uint32_t period;
    CadenceRegister destination;
    bool has_signed_phase;
};
constexpr std::array cadence_semantics{
    ExpectedCadenceSemantics{
        FramerateHookId::EffectCadence6,
        6,
        CadenceRegister::Edx,
        false},
    ExpectedCadenceSemantics{
        FramerateHookId::EffectCadence5,
        5,
        CadenceRegister::Edx,
        false},
    ExpectedCadenceSemantics{
        FramerateHookId::EffectCadence4,
        4,
        CadenceRegister::Edx,
        false},
    ExpectedCadenceSemantics{
        FramerateHookId::EffectCadence16A,
        16,
        CadenceRegister::Edx,
        true},
    ExpectedCadenceSemantics{
        FramerateHookId::EffectCadence16B,
        16,
        CadenceRegister::Ecx,
        true},
    ExpectedCadenceSemantics{
        FramerateHookId::EffectCadence8,
        8,
        CadenceRegister::Eax,
        true},
};
for (const auto& expected : cadence_semantics) {
    const auto actual =
        gc::framerate::detail::GetGameplayCadenceHookSemantics(
            expected.id);
    failures += Expect(
        actual.has_value() &&
            actual->authored_period == expected.period &&
            actual->test_register == expected.destination &&
            actual->has_signed_phase == expected.has_signed_phase,
        "gameplay cadence register and signed phase semantics are stable");
}

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

static_assert(kMaximumFramerateHooks == 53);
failures += Expect(
    FramerateHookHasRuntimeBinding(
        FramerateHookId::GameplaySongClock),
    "shared song-clock root has a runtime binding");
for (const auto& contract : FramerateHookContracts(true)) {
    failures += Expect(
        FramerateHookHasRuntimeBinding(contract.id),
        "every transformed contract has a runtime binding");
}
for (const auto id : {
         FramerateHookId::EffectFlowItemFrame,
         FramerateHookId::EffectTutorialElapsed,
         FramerateHookId::EffectChartPreRollDuration,
         FramerateHookId::EffectPlayerModuloDividend}) {
    failures += Expect(
        FramerateHookHasRuntimeBinding(id),
        "new effect producer has an explicit runtime binding");
}
for (const auto id : {
         FramerateHookId::MovieClipPreprocessVisit,
         FramerateHookId::RankingEntryCounterStore,
         FramerateHookId::HitChartEntryCounterStore,
         FramerateHookId::UnlockRewardCountdownStore,
         FramerateHookId::UnlockRewardPrimaryStateStore,
         FramerateHookId::UnlockRewardSecondaryStateStore}) {
    failures += Expect(
        FramerateHookHasRuntimeBinding(id),
        "menu timing hook has an explicit runtime binding");
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

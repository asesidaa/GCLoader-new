#include "Patches/Framerate/FrameratePatchPlan.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>

namespace {

constexpr std::uintptr_t kFakeBase = 0x00400000;
constexpr std::uint64_t kFakeTargetOperand = 0x12345678;

int Expect(bool condition, const char* name) {
    if (condition) {
        return 0;
    }
    std::cerr << "Expectation failed: " << name << "\n";
    return 1;
}

const gc::framerate::CheckedWrite* FindWrite(
    const gc::framerate::FramerateDirectPatchPlan& plan,
    std::uintptr_t rva) {
    const auto address = kFakeBase + rva;
    const auto found = std::find_if(
        plan.view().begin(), plan.view().end(),
        [address](const auto& write) { return write.address == address; });
    return found == plan.view().end() ? nullptr : &*found;
}

bool PlanContainsRva(
    const gc::framerate::FramerateDirectPatchPlan& plan,
    std::uintptr_t rva) {
    return FindWrite(plan, rva) != nullptr;
}

std::uint32_t ReadInstructionImmediate(
    const gc::framerate::FramerateDirectPatchPlan& plan,
    std::uintptr_t rva,
    std::size_t offset) {
    const auto* write = FindWrite(plan, rva);
    if (write == nullptr || offset + sizeof(std::uint32_t) >
        write->replacement.size) {
        std::abort();
    }
    std::uint32_t value{};
    std::memcpy(
        &value,
        write->replacement.bytes.data() + offset,
        sizeof(value));
    return value;
}

float ReadFloatReplacement(
    const gc::framerate::FramerateDirectPatchPlan& plan,
    std::uintptr_t rva) {
    const auto bits = ReadInstructionImmediate(plan, rva, 0);
    float value{};
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

bool PlanContainsId(
    const gc::framerate::FramerateHookPlan& plan,
    gc::framerate::FramerateHookId id) {
    return std::any_of(
        plan.view().begin(),
        plan.view().end(),
        [id](const auto& contract) { return contract.id == id; });
}

} // namespace

int main() {
using namespace gc::framerate;
int failures = 0;

static_assert(kMaximumFramerateHooks == 53);

const auto native_profile = FramerateProfile::Create(60).value();
const auto native_plan = BuildFramerateDirectPatchPlan(
    kFakeBase, native_profile, kFakeTargetOperand).value();
failures += Expect(native_plan.count == 0, "60 has no timing writes");
failures += Expect(
    native_plan.menu_repeat_initial == 16 &&
        native_plan.menu_repeat_interval == 3,
    "native plan exposes original menu values without writes");

for (const std::uint32_t target : {120U, 144U, 165U, 240U, 360U, 500U}) {
    const auto profile = FramerateProfile::Create(target).value();
    const auto plan = BuildFramerateDirectPatchPlan(
        kFakeBase, profile, kFakeTargetOperand).value();
    failures += Expect(plan.count == 17, "high target has 17 direct writes");
    failures += Expect(
        ReadFloatReplacement(plan, 0x002FC0A0) ==
            profile.frame_milliseconds(),
        "gameplay frame-ms replacement");
    failures += Expect(
        ReadFloatReplacement(plan, 0x002F4604) ==
            profile.frame_milliseconds(),
        "visual frame-ms replacement");
    failures += Expect(
        ReadFloatReplacement(plan, 0x002FC280) ==
            profile.frame_seconds(),
        "gameplay frame-seconds replacement");
    failures += Expect(
        ReadFloatReplacement(plan, 0x002E8F00) ==
            profile.render_smoothing_step(),
        "smoothing replacement");
    failures += Expect(
        ReadFloatReplacement(plan, 0x002E8F04) ==
            profile.render_offset_decay_step(),
        "decay replacement");
    failures += Expect(
        ReadInstructionImmediate(plan, 0x00055CCC, 2) ==
            static_cast<std::uint32_t>(
                profile.ScaleDurationFrames(16).value()),
        "repeat initial duration");
    failures += Expect(
        ReadInstructionImmediate(plan, 0x00055CDD, 2) ==
            static_cast<std::uint32_t>(
                profile.ScaleDurationFrames(8).value()),
        "XIO repeat next duration");
    failures += Expect(
        ReadInstructionImmediate(plan, 0x0005F843, 6) ==
            static_cast<std::uint32_t>(
                profile.ScaleDurationFrames(16).value()),
        "native keyboard repeat initial duration");
    failures += Expect(
        ReadInstructionImmediate(plan, 0x0005F84D, 6) ==
            static_cast<std::uint32_t>(
                profile.ScaleDurationFrames(8).value()),
        "native keyboard repeat next duration");
    failures += Expect(
        ReadInstructionImmediate(plan, 0x00382CE8, 0) ==
            static_cast<std::uint32_t>(
                profile.ScaleDurationFrames(16).value()),
        "non-song initial repeat duration");
    failures += Expect(
        ReadInstructionImmediate(plan, 0x00382CEC, 0) ==
            static_cast<std::uint32_t>(
                profile.ScaleDurationFrames(3).value()),
        "non-song repeat interval");
    failures += Expect(
        plan.menu_repeat_initial ==
                profile.ScaleDurationFrames(16).value() &&
            plan.menu_repeat_interval ==
                profile.ScaleDurationFrames(3).value(),
        "plan exposes menu values for startup diagnostics");
    failures += Expect(
        ReadInstructionImmediate(plan, 0x002645EE, 6) ==
            profile.two_second_frames(),
        "gameplay countdown duration");
    failures += Expect(
        ReadInstructionImmediate(plan, 0x00249A5E, 1) ==
            profile.two_second_frames(),
        "render EAX countdown duration");
    failures += Expect(
        ReadInstructionImmediate(plan, 0x00249A73, 1) ==
            profile.two_second_frames(),
        "render EDX countdown duration");
    for (const auto rva : {0x0022BACFU, 0x0022BAD5U, 0x00262CB6U}) {
        failures += Expect(
            ReadInstructionImmediate(plan, rva, 2) == kFakeTargetOperand,
            "local x87 operand redirects to profile target");
    }
    failures += Expect(
        !PlanContainsRva(plan, 0x0022BA60),
        "palette imm8 compare is never directly patched");
}

const auto plan61 = BuildFramerateDirectPatchPlan(
    kFakeBase,
    FramerateProfile::Create(61).value(),
    kFakeTargetOperand).value();
failures += Expect(
    plan61.count == 17 &&
        ReadInstructionImmediate(plan61, 0x002645EE, 6) == 122,
    "61 boundary uses transformed plan");

const auto native_hooks = FramerateHookContracts(false);
const auto transformed_hooks = FramerateHookContracts(true);
failures += Expect(native_hooks.size() == 1, "60 uses cadence hook only");
failures += Expect(
    native_hooks[0].id == FramerateHookId::OuterFrame,
    "native hook is outer cadence");
failures += Expect(
    transformed_hooks.size() == 53,
    "full transformed view has 53 contracts");
for (const auto diagnostic_rva : {
         0x000E12A0U,
         0x000DC575U,
         0x000D19A6U,
         0x000D19B0U,
         0x000CEC70U,
         0x000CEEB6U,
         0x00058A50U}) {
    failures += Expect(
        std::none_of(
            transformed_hooks.begin(),
            transformed_hooks.end(),
            [diagnostic_rva](const auto& hook) {
                return hook.rva == diagnostic_rva;
            }),
        "exhaustive UnlockReward diagnostic hook is absent");
}
for (const auto removed_rva :
     {0x00218A50U, 0x002544D0U, 0x00230AB6U, 0x0024F0C6U}) {
    const bool present = std::any_of(
        transformed_hooks.begin(), transformed_hooks.end(),
        [removed_rva](const auto& hook) {
            return hook.rva == removed_rva;
        });
    failures += Expect(!present, "invalid timing contract is absent");
}
failures += Expect(
        transformed_hooks[51].id == FramerateHookId::NavigatorAdvance &&
        transformed_hooks[52].id == FramerateHookId::OuterFrame,
    "Navigator and OuterFrame remain final");
for (const auto& hook : transformed_hooks) {
    failures += Expect(
        hook.rva != 0 &&
            hook.expected.size != 0 &&
            hook.name != nullptr &&
            !std::string_view{hook.name}.empty(),
        "hook contracts are installable and named");
}

const auto native_original = BuildFramerateHookPlan(
    false, GameplayAudioClockPlan::OriginalWatchdog);
const auto native_legacy = BuildFramerateHookPlan(
    false, GameplayAudioClockPlan::WasapiLegacyResync);
const auto native_shared = BuildFramerateHookPlan(
    false, GameplayAudioClockPlan::WasapiSharedSongClock);
const auto transformed_original = BuildFramerateHookPlan(
    true, GameplayAudioClockPlan::OriginalWatchdog);
const auto transformed_legacy = BuildFramerateHookPlan(
    true, GameplayAudioClockPlan::WasapiLegacyResync);
const auto transformed_shared = BuildFramerateHookPlan(
    true, GameplayAudioClockPlan::WasapiSharedSongClock);

failures += Expect(
    native_original.count == 1 &&
        native_legacy.count == 2 &&
        native_shared.count == 9,
    "native hook-family selections have exact counts");
failures += Expect(
    transformed_original.count == 51 &&
        transformed_legacy.count == 52 &&
        transformed_shared.count == 50,
    "transformed hook-family selections have exact counts");
failures += Expect(
    PlanContainsId(native_original, FramerateHookId::OuterFrame) &&
        !PlanContainsId(
            native_original, FramerateHookId::GameplaySongClock) &&
        !PlanContainsId(
            native_original, FramerateHookId::AudioResyncPolicy),
    "native original keeps only the outer-frame hook");
failures += Expect(
    PlanContainsId(native_legacy, FramerateHookId::OuterFrame) &&
        PlanContainsId(
            native_legacy, FramerateHookId::AudioResyncPolicy) &&
        !PlanContainsId(
            native_legacy, FramerateHookId::GameplaySongClock),
    "native legacy keeps the old watchdog policy");
for (const auto id : {
         FramerateHookId::GameplaySongClock,
         FramerateHookId::GameplayEffectAdvance,
         FramerateHookId::EffectCadence6,
         FramerateHookId::EffectCadence5,
         FramerateHookId::EffectCadence4,
         FramerateHookId::EffectCadence16A,
         FramerateHookId::EffectCadence16B,
         FramerateHookId::EffectCadence8,
         FramerateHookId::OuterFrame}) {
    failures += Expect(
        PlanContainsId(native_shared, id),
        "native shared selects root and gameplay consumers");
}
for (const auto id : {
         FramerateHookId::AudioSkipMargin,
         FramerateHookId::AudioSkipInterval,
         FramerateHookId::AudioResyncPolicy}) {
    failures += Expect(
        !PlanContainsId(native_shared, id) &&
            !PlanContainsId(transformed_shared, id),
        "shared clock excludes every legacy audio hook");
}
failures += Expect(
    !PlanContainsId(
        transformed_original, FramerateHookId::GameplaySongClock) &&
        !PlanContainsId(
            transformed_legacy, FramerateHookId::GameplaySongClock) &&
        PlanContainsId(
            transformed_shared, FramerateHookId::GameplaySongClock),
    "only shared mode selects the shared song-clock root");

for (std::size_t left = 0; left < transformed_hooks.size(); ++left) {
    for (std::size_t right = left + 1;
         right < transformed_hooks.size();
         ++right) {
        failures += Expect(
            transformed_hooks[left].id != transformed_hooks[right].id,
            "hook IDs are unique");
        failures += Expect(
            transformed_hooks[left].rva != transformed_hooks[right].rva,
            "hook RVAs are unique");
    }
}

for (const auto id : {
         FramerateHookId::MovieClipPreprocessVisit,
         FramerateHookId::RankingEntryCounterStore,
         FramerateHookId::HitChartEntryCounterStore,
         FramerateHookId::UnlockRewardCountdownStore,
         FramerateHookId::UnlockRewardPrimaryStateStore,
         FramerateHookId::UnlockRewardSecondaryStateStore}) {
    failures += Expect(
        !PlanContainsId(native_original, id) &&
            !PlanContainsId(native_legacy, id) &&
            !PlanContainsId(native_shared, id),
        "native plans exclude every menu timing hook");
}

failures += Expect(
    !BuildFramerateDirectPatchPlan(
        kFakeBase,
        FramerateProfile::Create(120).value(),
        static_cast<std::uint64_t>(UINT32_MAX) + 1),
    "x87 operand above 32-bit range is rejected");
failures += Expect(
    !BuildFramerateDirectPatchPlan(
        0x00500000,
        FramerateProfile::Create(120).value(),
        kFakeTargetOperand),
    "unexpected loaded image base is rejected");

for (const std::uint32_t cap : {120U, 144U, 165U, 240U, 360U, 500U}) {
    const auto below = ApplyCmp32Flags(0x202, cap - 1, cap);
    const auto equal = ApplyCmp32Flags(0x202, cap, cap);
    const auto above = ApplyCmp32Flags(0x202, cap + 1, cap);
    failures += Expect((below & 0x40U) == 0, "CMP below clears ZF");
    failures += Expect((equal & 0x40U) != 0, "CMP equal sets ZF");
    failures += Expect((above & 0x40U) == 0, "CMP above clears ZF");
    failures += Expect(
        ((below >> 7U) & 1U) != ((below >> 11U) & 1U),
        "signed JGE classifies below as less");
    failures += Expect(
        ((equal >> 7U) & 1U) == ((equal >> 11U) & 1U) &&
            ((above >> 7U) & 1U) == ((above >> 11U) & 1U),
        "signed JGE classifies equal and above");
}

return failures == 0 ? 0 : 1;
}

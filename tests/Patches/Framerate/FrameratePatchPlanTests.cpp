#include "Patches/Framerate/FrameratePatchPlan.h"
#include "Patches/Framerate/FramerateAuthoredClock.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <iostream>
#include <span>

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

gc::framerate::BytePattern Pattern(
    std::initializer_list<std::uint8_t> values) {
    gc::framerate::BytePattern pattern{};
    pattern.size = static_cast<std::uint8_t>(values.size());
    std::transform(
        values.begin(), values.end(), pattern.bytes.begin(),
        [](std::uint8_t value) { return static_cast<std::byte>(value); });
    return pattern;
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

const gc::framerate::FramerateHookContract& FindHook(
    std::span<const gc::framerate::FramerateHookContract> contracts,
    gc::framerate::FramerateHookId id) {
    const auto found = std::find_if(
        contracts.begin(), contracts.end(),
        [id](const auto& contract) { return contract.id == id; });
    if (found == contracts.end()) {
        std::abort();
    }
    return *found;
}

} // namespace

int main() {
using namespace gc::framerate;
int failures = 0;

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

const auto target120 = FramerateProfile::Create(120).value();
const auto plan120 = BuildFramerateDirectPatchPlan(
    kFakeBase, target120, kFakeTargetOperand).value();
failures += Expect(
    ReadFloatReplacement(plan120, 0x002FC0A0) == 1000.0F / 120.0F &&
        ReadFloatReplacement(plan120, 0x002FC280) == 1.0F / 120.0F &&
        ReadFloatReplacement(plan120, 0x002E8F00) == 2.0F &&
        ReadFloatReplacement(plan120, 0x002E8F04) == 2.5F,
    "120 exact float replacements");
failures += Expect(
    ReadInstructionImmediate(plan120, 0x00055CCC, 2) == 32 &&
        ReadInstructionImmediate(plan120, 0x00055CDD, 2) == 16 &&
        ReadInstructionImmediate(plan120, 0x0005F843, 6) == 32 &&
        ReadInstructionImmediate(plan120, 0x0005F84D, 6) == 16 &&
        ReadInstructionImmediate(plan120, 0x002645EE, 6) == 240 &&
        ReadInstructionImmediate(plan120, 0x00249A5E, 1) == 240 &&
        ReadInstructionImmediate(plan120, 0x00249A73, 1) == 240,
    "120 exact duration replacements");
for (const auto rva : {0x0022BACFU, 0x0022BAD5U, 0x00262CB6U}) {
    failures += Expect(
        ReadInstructionImmediate(plan120, rva, 2) == kFakeTargetOperand,
        "120 exact x87 target operand");
}

const auto plan240 = BuildFramerateDirectPatchPlan(
    kFakeBase,
    FramerateProfile::Create(240).value(),
    kFakeTargetOperand).value();
failures += Expect(
    ReadInstructionImmediate(plan240, 0x00055CCC, 2) == 64 &&
        ReadInstructionImmediate(plan240, 0x00055CDD, 2) == 32 &&
        ReadInstructionImmediate(plan240, 0x0005F843, 6) == 64 &&
        ReadInstructionImmediate(plan240, 0x0005F84D, 6) == 32,
    "240 exact XIO and native keyboard repeat durations");

const auto plan61 = BuildFramerateDirectPatchPlan(
    kFakeBase,
    FramerateProfile::Create(61).value(),
    kFakeTargetOperand).value();
failures += Expect(
    plan61.count == 17 &&
        ReadInstructionImmediate(plan61, 0x002645EE, 6) == 122,
    "61 boundary uses transformed plan");

const std::array<std::pair<std::uintptr_t, BytePattern>, 17>
    expected_writes{{
        {0x002FC0A0, Pattern({0x55, 0x55, 0x85, 0x41})},
        {0x002F4604, Pattern({0x55, 0x55, 0x85, 0x41})},
        {0x002FC280, Pattern({0x89, 0x88, 0x88, 0x3C})},
        {0x002E8F00, Pattern({0x00, 0x00, 0x80, 0x40})},
        {0x002E8F04, Pattern({0x00, 0x00, 0xA0, 0x40})},
        {0x00055CCC, Pattern({0xC7, 0x00, 0x10, 0x00, 0x00, 0x00})},
        {0x00055CDD, Pattern({0xC7, 0x00, 0x08, 0x00, 0x00, 0x00})},
        {0x0005F843, Pattern({0xC7, 0x86, 0xD4, 0x02, 0x00,
            0x00, 0x10, 0x00, 0x00, 0x00})},
        {0x0005F84D, Pattern({0xC7, 0x86, 0xD8, 0x02, 0x00,
            0x00, 0x08, 0x00, 0x00, 0x00})},
        {0x002645EE, Pattern({0xC7, 0x80, 0x14, 0x1D, 0x00, 0x00, 0x78, 0x00, 0x00, 0x00})},
        {0x00249A5E, Pattern({0xB8, 0x78, 0x00, 0x00, 0x00})},
        {0x00249A73, Pattern({0xBA, 0x78, 0x00, 0x00, 0x00})},
        {0x0022BACF, Pattern({0xD8, 0x2D, 0xAC, 0xBB, 0x6F, 0x00})},
        {0x0022BAD5, Pattern({0xD8, 0x35, 0xAC, 0xBB, 0x6F, 0x00})},
        {0x00262CB6, Pattern({0xD8, 0x0D, 0xAC, 0xBB, 0x6F, 0x00})},
        {0x00382CE8, Pattern({0x10, 0x00, 0x00, 0x00})},
        {0x00382CEC, Pattern({0x03, 0x00, 0x00, 0x00})},
    }};
for (const auto& [rva, expected] : expected_writes) {
    const auto* write = FindWrite(plan120, rva);
    failures += Expect(
        write != nullptr && write->expected == expected &&
            write->expected.size == write->replacement.size,
        "exact direct-write source contract");
}

struct MenuCase {
    std::uint32_t target;
    std::uint32_t initial;
    std::uint32_t interval;
};
constexpr std::array menu_cases{
    MenuCase{61, 16, 3},
    MenuCase{120, 32, 6},
    MenuCase{144, 38, 7},
    MenuCase{165, 44, 8},
    MenuCase{240, 64, 12},
    MenuCase{360, 96, 18},
    MenuCase{500, 133, 25},
};
for (const auto& item : menu_cases) {
    const auto plan = BuildFramerateDirectPatchPlan(
        kFakeBase,
        FramerateProfile::Create(item.target).value(),
        kFakeTargetOperand).value();
    failures += Expect(
        ReadInstructionImmediate(plan, 0x00382CE8, 0) == item.initial &&
            ReadInstructionImmediate(plan, 0x00382CEC, 0) == item.interval,
        "exact non-song repeat replacements");
}

const auto native_hooks = FramerateHookContracts(false);
const auto transformed_hooks = FramerateHookContracts(true);
failures += Expect(native_hooks.size() == 1, "60 uses cadence hook only");
failures += Expect(
    native_hooks[0].id == FramerateHookId::OuterFrame,
    "native hook is outer cadence");
failures += Expect(
    transformed_hooks.size() == 21,
    "intermediate transformed mode has 21 retained hooks");
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
    transformed_hooks.back().id == FramerateHookId::OuterFrame,
    "outer frame remains last");
failures += Expect(
    FindHook(transformed_hooks, FramerateHookId::PaletteCompare).expected ==
        Pattern({0x83, 0x78, 0x0C, 0x3C}),
    "palette compare exact bytes");

const std::array<FramerateHookContract, 21> expected_hooks{{
    {FramerateHookId::MovieClipGoto, 0x000DEA30,
        Pattern({0x6A, 0xFF, 0x68, 0xC9, 0x38, 0x67, 0x00}), ""},
    {FramerateHookId::MovieClipAdvance, 0x000DF940,
        Pattern({0x56, 0x8B, 0xF1, 0x8B, 0x06, 0x8B, 0x90, 0x4C, 0x01, 0x00, 0x00}), ""},
    {FramerateHookId::PaletteCompare, 0x0022BA60,
        Pattern({0x83, 0x78, 0x0C, 0x3C}), ""},
    {FramerateHookId::StageClipFrame, 0x00244054,
        Pattern({0x89, 0x4D, 0xF8}), ""},
    {FramerateHookId::IfblWait, 0x002309D4,
        Pattern({0x89, 0x4A, 0x3C}), ""},
    {FramerateHookId::StageBgmPreload, 0x0021001A,
        Pattern({0x83, 0xC0, 0x01}), ""},
    {FramerateHookId::TuneCountdownCompare, 0x002648F7,
        Pattern({0x83, 0xBA, 0x14, 0x1D, 0x00, 0x00, 0x78}), ""},
    {FramerateHookId::AudioSkipMargin, 0x0024018F,
        Pattern({0x8B, 0x45, 0xF4}), ""},
    {FramerateHookId::AudioSkipInterval, 0x002401BD,
        Pattern({0xF7, 0x79, 0x3C}), ""},
    {FramerateHookId::AudioResyncDiagnostic, 0x002401C4,
        Pattern({0x8B, 0x55, 0xF8}), ""},
    {FramerateHookId::GameplayEffectAdvance, 0x00264E2D,
        Pattern({0xE8, 0x6E, 0xBA, 0xF8, 0xFF}), ""},
    {FramerateHookId::EffectCadence6, 0x0024063B,
        Pattern({0x85, 0xD2}), ""},
    {FramerateHookId::EffectCadence5, 0x002408D7,
        Pattern({0x85, 0xD2}), ""},
    {FramerateHookId::EffectCadence4, 0x00240C9C,
        Pattern({0x85, 0xD2}), ""},
    {FramerateHookId::EffectCadence16A, 0x00241213,
        Pattern({0x85, 0xD2}), ""},
    {FramerateHookId::EffectCadence16B, 0x0024122F,
        Pattern({0x81, 0xE1, 0x0F, 0x00, 0x00, 0x80}), ""},
    {FramerateHookId::EffectCadence8, 0x00241268,
        Pattern({0x85, 0xC0}), ""},
    {FramerateHookId::RemoteCadenceA, 0x002632DB,
        Pattern({0x85, 0xD2}), ""},
    {FramerateHookId::RemoteCadenceB, 0x00263646,
        Pattern({0x85, 0xD2}), ""},
    {FramerateHookId::GameplayBlink, 0x0024A1B9,
        Pattern({0xD1, 0xF8}), ""},
    {FramerateHookId::OuterFrame, 0x00058B70,
        Pattern({0x56, 0x8B, 0xF1, 0x8B, 0x06, 0x8B, 0x50, 0x24}), ""},
}};
for (std::size_t index = 0; index < expected_hooks.size(); ++index) {
    failures += Expect(
        transformed_hooks[index].id == expected_hooks[index].id &&
            transformed_hooks[index].rva == expected_hooks[index].rva &&
            transformed_hooks[index].expected == expected_hooks[index].expected,
        "exact hook ID/RVA/byte contract");
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

const auto profile144 = FramerateProfile::Create(144).value();
failures += Expect(
    ScalePositiveDuration(profile144, 25).value() == 60,
    "positive runtime count scales rationally");
failures += Expect(
    ScalePositiveDuration(profile144, 0).value() == 0 &&
        ScalePositiveDuration(profile144, UINT32_MAX).value() == UINT32_MAX,
    "runtime count sentinels remain unchanged");
for (const std::uint32_t target : {120U, 144U, 165U, 240U, 360U}) {
    const auto profile = FramerateProfile::Create(target).value();
    for (std::uint32_t frame = 0; frame < target * 2; ++frame) {
        failures += Expect(
            profile.MapToAuthored60(frame).value() ==
                static_cast<std::uint64_t>(frame) * 60 / target,
            "authored mapping sequence uses rational floor");
    }
}

return failures == 0 ? 0 : 1;
}

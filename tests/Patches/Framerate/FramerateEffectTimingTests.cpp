#include "Patches/Framerate/FramerateEffectTiming.h"

#include <safetyhook.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <set>
#include <string_view>
#include <type_traits>
#include <vector>

namespace {

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

bool NonEmpty(const char* value) {
    return value != nullptr && value[0] != '\0';
}

safetyhook::Context CanaryContext() {
    safetyhook::Context context{};
    context.eax = 0x11111111;
    context.ebx = 0x22222222;
    context.ecx = 0x33333333;
    context.edx = 0x44444444;
    context.esi = 0x55555555;
    context.edi = 0x66666666;
    context.ebp = 0x77777777;
    context.esp = 0x88888888;
    context.eip = 0x99999999;
    context.eflags = 0xA5A5A5A5;
    return context;
}

bool PreservedExceptEax(
    const safetyhook::Context& actual,
    const safetyhook::Context& original) {
    return actual.ebx == original.ebx &&
        actual.ecx == original.ecx &&
        actual.edx == original.edx &&
        actual.esi == original.esi &&
        actual.edi == original.edi &&
        actual.ebp == original.ebp &&
        actual.esp == original.esp &&
        actual.eip == original.eip &&
        actual.eflags == original.eflags;
}

bool PreservedExceptEdx(
    const safetyhook::Context& actual,
    const safetyhook::Context& original) {
    return actual.eax == original.eax &&
        actual.ebx == original.ebx &&
        actual.ecx == original.ecx &&
        actual.esi == original.esi &&
        actual.edi == original.edi &&
        actual.ebp == original.ebp &&
        actual.esp == original.esp &&
        actual.eip == original.eip &&
        actual.eflags == original.eflags;
}

bool IsKnownDisposition(
    gc::framerate::EffectTimingDisposition disposition) {
    using gc::framerate::EffectTimingDisposition;
    switch (disposition) {
    case EffectTimingDisposition::Hook:
    case EffectTimingDisposition::ManagerGated:
    case EffectTimingDisposition::AlreadyAuthoredNormalized:
    case EffectTimingDisposition::ResetOrConstant:
    case EffectTimingDisposition::ChildInherited:
    case EffectTimingDisposition::NonCtuneOutOfScope:
        return true;
    }
    return false;
}

template <typename Range, typename Projection>
bool HasUniqueValues(const Range& values, Projection projection) {
    using Value = std::decay_t<decltype(projection(*values.begin()))>;
    std::set<Value> unique;
    for (const auto& value : values) {
        if (!unique.insert(projection(value)).second) {
            return false;
        }
    }
    return true;
}

} // namespace

int main() {
using namespace gc::framerate;
int failures = 0;

constexpr std::array<std::uintptr_t, 34> kExpectedRegistrationRvas{
    0x001F02F5,
    0x00240674, 0x00240941, 0x00240CDE, 0x002412B5,
    0x00244BC0, 0x00244D30, 0x00244E20, 0x00244F10, 0x00245000,
    0x00246517, 0x00246693,
    0x00248F75, 0x002491C9, 0x002498E8, 0x0024999C,
    0x00249A53, 0x00249BEC,
    0x0024B61C, 0x0024BB11, 0x0024BC19, 0x0024BF72,
    0x0024C56C, 0x0024C5CA, 0x0024C607, 0x0024C8DC,
    0x0024CB4D, 0x0024CBC0, 0x0024CBFD,
    0x0024D710, 0x0024D779, 0x0024D7C4,
    0x0024EF82, 0x00250689,
};
constexpr std::array<std::uintptr_t, 9> kExpectedDurationQueryRvas{
    0x00246463, 0x0024647D,
    0x00248EA7, 0x00248EBF, 0x00249104,
    0x0024962C, 0x00249653, 0x00249790,
    0x0024A92F,
};

const auto registrations = EffectRegistrationSites();
const auto duration_queries = EffectDurationQuerySites();
const auto timing_sites = EffectTimingSites();
const auto effect_hooks = FramerateEffectHookContracts();

std::vector<std::uintptr_t> registration_rvas;
registration_rvas.reserve(registrations.size());
for (const auto& registration : registrations) {
    registration_rvas.push_back(registration.rva);
    failures += Expect(
        NonEmpty(registration.owner),
        "registration owner is present");
    failures += Expect(
        NonEmpty(registration.reaching_frame_path),
        "registration reaching-frame path is present");
}
std::ranges::sort(registration_rvas);
failures += Expect(
    registration_rvas.size() == kExpectedRegistrationRvas.size() &&
        std::ranges::equal(registration_rvas, kExpectedRegistrationRvas),
    "registration census matches exact 34-RVA set");
failures += Expect(
    HasUniqueValues(
        registrations,
        [](const auto& site) { return site.rva; }),
    "registration RVAs are unique");

std::vector<std::uintptr_t> duration_query_rvas;
duration_query_rvas.reserve(duration_queries.size());
for (const auto& query : duration_queries) {
    duration_query_rvas.push_back(query.rva);
    failures += Expect(
        NonEmpty(query.owner),
        "duration-query owner is present");
    failures += Expect(
        NonEmpty(query.consumer_path),
        "duration-query consumer path is present");
}
std::ranges::sort(duration_query_rvas);
failures += Expect(
    duration_query_rvas.size() == kExpectedDurationQueryRvas.size() &&
        std::ranges::equal(duration_query_rvas, kExpectedDurationQueryRvas),
    "duration-query census matches exact 9-RVA set");
failures += Expect(
    HasUniqueValues(
        duration_queries,
        [](const auto& site) { return site.rva; }),
    "duration-query RVAs are unique");

for (const auto& site : timing_sites) {
    failures += Expect(
        NonEmpty(site.stable_id),
        "timing stable ID is present");
    failures += Expect(
        NonEmpty(site.evidence),
        "timing evidence is present");
    failures += Expect(
        IsKnownDisposition(site.disposition),
        "timing disposition is final");
    if (site.hook_id.has_value()) {
        const auto manifest_count = std::ranges::count(
            timing_sites, site.hook_id,
            &EffectTimingSite::hook_id);
        const auto contract_count = std::ranges::count(
            effect_hooks, site.hook_id.value(),
            &FramerateHookContract::id);
        failures += Expect(
            manifest_count == 1,
            "manifest hook ID occurs exactly once");
        failures += Expect(
            contract_count == 1,
            "manifest hook ID has exactly one effect contract");
    }
}
failures += Expect(
    HasUniqueValues(
        timing_sites,
        [](const auto& site) {
            return std::string_view(site.stable_id);
        }),
    "timing stable IDs are unique");
failures += Expect(
    HasUniqueValues(
        timing_sites,
        [](const auto& site) { return site.boundary_rva; }),
    "timing boundary RVAs are unique");

for (const auto& contract : effect_hooks) {
    const auto manifest_count = std::ranges::count(
        timing_sites,
        std::optional<FramerateHookId>{contract.id},
        &EffectTimingSite::hook_id);
    failures += Expect(
        manifest_count == 1,
        "effect contract has exactly one manifest site");
    failures += Expect(
        NonEmpty(contract.name),
        "effect contract name is present");
}
failures += Expect(
    HasUniqueValues(
        effect_hooks,
        [](const auto& contract) { return contract.id; }),
    "effect contract IDs are unique");

struct ExpectedHook {
    FramerateHookId id;
    std::uintptr_t rva;
    BytePattern expected;
};
const std::array expected_new_hooks{
    ExpectedHook{
        FramerateHookId::EffectFlowItemFrame,
        0x001F0310,
        Pattern({0x89, 0x42, 0x08})},
    ExpectedHook{
        FramerateHookId::EffectTutorialElapsed,
        0x00249593,
        Pattern({0x89, 0x95, 0x74, 0xFF, 0xFF, 0xFF})},
    ExpectedHook{
        FramerateHookId::EffectChartPreRollDuration,
        0x0024A934,
        Pattern({0x89, 0x45, 0x9C})},
    ExpectedHook{
        FramerateHookId::EffectPlayerModuloDividend,
        0x0025072E,
        Pattern({0xF7, 0xF9})},
};
for (const auto& expected : expected_new_hooks) {
    const auto found = std::ranges::find(
        effect_hooks, expected.id, &FramerateHookContract::id);
    failures += Expect(
        found != effect_hooks.end() &&
            found->rva == expected.rva &&
            found->expected == expected.expected,
        "new effect contract has exact ID, RVA, and bytes");
}

const auto summary = SummarizeEffectTimingManifest();
const auto disposition_count =
    [timing_sites](EffectTimingDisposition disposition) {
        return static_cast<std::size_t>(
            std::ranges::count(
                timing_sites,
                disposition,
                &EffectTimingSite::disposition));
    };
const auto manifest_hook_count = static_cast<std::size_t>(
    std::ranges::count_if(
        timing_sites,
        [](const auto& site) { return site.hook_id.has_value(); }));
failures += Expect(
    summary.timing_sites == timing_sites.size() &&
        summary.registration_sites == registrations.size() &&
        summary.duration_queries == duration_queries.size() &&
        summary.hook_contracts == effect_hooks.size() &&
        summary.hook_contracts == manifest_hook_count &&
        summary.manager_gated ==
            disposition_count(EffectTimingDisposition::ManagerGated) &&
        summary.already_authored ==
            disposition_count(
                EffectTimingDisposition::AlreadyAuthoredNormalized) &&
        summary.reset_or_constant ==
            disposition_count(EffectTimingDisposition::ResetOrConstant) &&
        summary.child_inherited ==
            disposition_count(EffectTimingDisposition::ChildInherited) &&
        summary.non_ctune_out_of_scope ==
            disposition_count(EffectTimingDisposition::NonCtuneOutOfScope),
    "manifest summary is derived from authoritative views");
failures += Expect(
    summary.timing_sites == 67 &&
        summary.registration_sites == 34 &&
        summary.duration_queries == 9 &&
        summary.hook_contracts == 34 &&
        summary.manager_gated == 1 &&
        summary.already_authored == 12 &&
        summary.reset_or_constant == 9 &&
        summary.child_inherited == 1 &&
        summary.non_ctune_out_of_scope == 12,
    "manifest summary matches exhaustive effect census");

const auto profile60 = FramerateProfile::Create(60).value();
const auto profile144 = FramerateProfile::Create(144).value();
const auto profile240 = FramerateProfile::Create(240).value();

{
    auto context = CanaryContext();
    const auto original = context;
    context.eax = 8;
    failures += Expect(
        MapEffectFrameEaxToAuthored60(context, profile240).has_value() &&
            context.eax == 2 &&
            PreservedExceptEax(context, original),
        "EAX effect frame maps 240 target frames to authored 60");
}
{
    auto context = CanaryContext();
    const auto original = context;
    context.eax = 12;
    failures += Expect(
        MapEffectFrameEaxToAuthored60(context, profile144).has_value() &&
            context.eax == 5 &&
            PreservedExceptEax(context, original),
        "EAX effect frame maps rational 144 target frames");
}
{
    auto context = CanaryContext();
    const auto original = context;
    context.edx = 8;
    failures += Expect(
        MapEffectFrameEdxToAuthored60(context, profile240).has_value() &&
            context.edx == 2 &&
            PreservedExceptEdx(context, original),
        "EDX effect frame maps 240 target frames to authored 60");
}
{
    auto context = CanaryContext();
    const auto original = context;
    context.eax = 25;
    failures += Expect(
        ScaleEffectDurationEaxToTarget(context, profile240).has_value() &&
            context.eax == 100 &&
            PreservedExceptEax(context, original),
        "EAX effect duration scales from authored 60 to target 240");
}
{
    auto context = CanaryContext();
    const auto original = context;
    context.eax = 25;
    failures += Expect(
        ScaleEffectDurationEaxToTarget(context, profile144).has_value() &&
            context.eax == 60 &&
            PreservedExceptEax(context, original),
        "EAX effect duration scales rationally to target 144");
}

constexpr std::array signed_nonpositive{
    0U,
    std::bit_cast<std::uint32_t>(std::int32_t{-1}),
    std::bit_cast<std::uint32_t>(
        std::numeric_limits<std::int32_t>::min()),
};
for (const auto sentinel : signed_nonpositive) {
    auto eax_map = CanaryContext();
    const auto eax_original = eax_map;
    eax_map.eax = sentinel;
    failures += Expect(
        MapEffectFrameEaxToAuthored60(eax_map, profile240).has_value() &&
            eax_map.eax == sentinel &&
            PreservedExceptEax(eax_map, eax_original),
        "EAX mapping preserves signed-nonpositive sentinel and context");

    auto edx_map = CanaryContext();
    const auto edx_original = edx_map;
    edx_map.edx = sentinel;
    failures += Expect(
        MapEffectFrameEdxToAuthored60(edx_map, profile240).has_value() &&
            edx_map.edx == sentinel &&
            PreservedExceptEdx(edx_map, edx_original),
        "EDX mapping preserves signed-nonpositive sentinel and context");

    auto scale = CanaryContext();
    const auto scale_original = scale;
    scale.eax = sentinel;
    failures += Expect(
        ScaleEffectDurationEaxToTarget(scale, profile240).has_value() &&
            scale.eax == sentinel &&
            PreservedExceptEax(scale, scale_original),
        "duration scaling preserves signed-nonpositive sentinel and context");
}

{
    auto eax_map = CanaryContext();
    const auto eax_original = eax_map;
    eax_map.eax = 37;
    failures += Expect(
        MapEffectFrameEaxToAuthored60(eax_map, profile60).has_value() &&
            eax_map.eax == 37 &&
            PreservedExceptEax(eax_map, eax_original),
        "native EAX mapping leaves positive frame unchanged");

    auto edx_map = CanaryContext();
    const auto edx_original = edx_map;
    edx_map.edx = 37;
    failures += Expect(
        MapEffectFrameEdxToAuthored60(edx_map, profile60).has_value() &&
            edx_map.edx == 37 &&
            PreservedExceptEdx(edx_map, edx_original),
        "native EDX mapping leaves positive frame unchanged");

    auto scale = CanaryContext();
    const auto scale_original = scale;
    scale.eax = 37;
    failures += Expect(
        ScaleEffectDurationEaxToTarget(scale, profile60).has_value() &&
            scale.eax == 37 &&
            PreservedExceptEax(scale, scale_original),
        "native duration scaling leaves positive frame unchanged");
}

{
    const auto profile500 = FramerateProfile::Create(500).value();
    auto context = CanaryContext();
    context.eax = static_cast<std::uint32_t>(
        std::numeric_limits<std::int32_t>::max());
    const auto original = context;
    const auto result =
        ScaleEffectDurationEaxToTarget(context, profile500);
    failures += Expect(
        !result &&
            result.error() ==
                EffectTimingTransformError::ProfileConversion &&
            context.eax == original.eax &&
            PreservedExceptEax(context, original),
        "duration overflow reports conversion failure without mutation");
}

return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

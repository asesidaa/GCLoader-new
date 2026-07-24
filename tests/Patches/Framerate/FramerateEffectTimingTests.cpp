#include "Patches/Framerate/FramerateEffectTiming.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <initializer_list>
#include <iostream>
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

return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

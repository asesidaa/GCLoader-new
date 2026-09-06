#include "Patches/GameVersion/VersionedPlan.h"
#include <algorithm>
#include <numeric>
#include <cstring>
#include <type_traits>

namespace gc::game_version {
const SiteContract& ContractOf(const VersionedOperation& operation) noexcept {
    return std::visit([](const auto& value) -> const SiteContract& { return value.contract; }, operation);
}
namespace {
bool PointerMatches(const runtime_image::BytePattern& bytes, const void* pointer) noexcept {
    if (bytes.size != sizeof(void*)) return false;
    return std::memcmp(bytes.bytes.data(), &pointer, sizeof(pointer)) == 0;
}
bool ValidPayload(const VersionedOperation& operation) noexcept {
    return std::visit([](const auto& value) {
        using T = std::decay_t<decltype(value)>;
        const auto& contract = value.contract;
        if constexpr (std::is_same_v<T, BytePatchOperation>) {
            return contract.kind == VersionedOperationKind::byte_patch &&
                value.replacement == contract.installed &&
                (value.memory_kind == runtime_image::MemoryKind::code ||
                 value.memory_kind == runtime_image::MemoryKind::data);
        } else if constexpr (std::is_same_v<T, InlineHookOperation>) {
            return contract.kind == VersionedOperationKind::inline_hook &&
                value.detour && value.original.storage && value.original.publish;
        } else if constexpr (std::is_same_v<T, MidHookOperation>) {
            return contract.kind == VersionedOperationKind::mid_hook && value.callback;
        } else if constexpr (std::is_same_v<T, GlobalVtableSlotOperation>) {
            return contract.kind == VersionedOperationKind::global_vtable_slot &&
                value.expected && value.replacement && value.original.storage && value.original.publish &&
                PointerMatches(contract.original, value.expected) &&
                PointerMatches(contract.installed, value.replacement);
        } else return contract.kind == VersionedOperationKind::read_only_contract;
    }, operation);
}
bool ValidFeature(FeatureId feature) noexcept {
    return feature >= FeatureId::game_compatibility && feature <= FeatureId::nesys_ping;
}
bool ValidPattern(const runtime_image::BytePattern& pattern) noexcept {
    return pattern.size && pattern.size <= runtime_image::kMaximumPatternBytes;
}
bool ReadOnly(const SiteContract& site) noexcept {
    return site.kind == VersionedOperationKind::read_only_contract;
}
PlanError Error(PlanStage stage, PlanContext context, const SiteContract& site) noexcept {
    return {.stage = stage, .context = context, .feature = site.feature, .site = site.site,
        .rva = site.rva, .expected_original = site.original, .expected_installed = site.installed};
}
template <class Detection>
PlanContext Context(const Detection& detection) noexcept {
    return std::visit([](const auto& value) {
        DetectionProof proof = DetectionProof::complete_local_contract;
        if constexpr (requires { value.proof; }) proof = value.proof;
        return PlanContext{value.build, value.variant, proof};
    }, detection);
}
bool ValidContext(const PlanContext& context) noexcept {
    if (const auto* build = std::get_if<GameBuild>(&context.build)) {
        const auto* variant = std::get_if<GameImageVariant>(&context.variant);
        const bool supported = *build == GameBuild::groove_coaster_471 ||
            *build == GameBuild::groove_coaster_206;
        return supported && variant &&
            ((context.proof == DetectionProof::exact_known_hash &&
              (*variant == GameImageVariant::clean ||
               (*build == GameBuild::groove_coaster_471 && *variant == GameImageVariant::legacy_patched))) ||
             (context.proof == DetectionProof::complete_local_contract &&
              *variant == GameImageVariant::locally_verified));
    }
    const auto* variant = std::get_if<nesys_service::NesysImageVariant>(&context.variant);
    const auto build = std::get<nesys_service::NesysBuild>(context.build);
    const bool supported = build == nesys_service::NesysBuild::service_297 ||
        build == nesys_service::NesysBuild::service_2861;
    return supported &&
        variant && ((context.proof == DetectionProof::exact_known_hash &&
                     *variant == nesys_service::NesysImageVariant::original) ||
                    (context.proof == DetectionProof::complete_local_contract &&
                     *variant == nesys_service::NesysImageVariant::locally_verified));
}
}
PlanContext ContextFor(const GameDetection& detection) noexcept { return Context(detection); }
PlanContext ContextFor(const NesysDetection& detection) noexcept { return Context(detection); }

std::expected<void, PlanError> VersionedPlanSet::Require(FeatureRequirement requirement) noexcept {
    if (!ValidFeature(requirement.feature))
        return std::unexpected(PlanError{.stage = PlanStage::invalid_plan, .feature = requirement.feature});
    for (const auto& existing : requirements_)
        if (existing.feature == requirement.feature)
            return std::unexpected(PlanError{.stage = PlanStage::duplicate_feature, .feature = requirement.feature});
    try { requirements_.push_back(requirement); return {}; }
    catch (...) { return std::unexpected(PlanError{.stage = PlanStage::allocation, .feature = requirement.feature}); }
}
std::expected<void, PlanError> VersionedPlanSet::Add(FeaturePlan plan) noexcept {
    if (!ValidFeature(plan.feature) || plan.operations.empty())
        return std::unexpected(PlanError{.stage = PlanStage::invalid_plan, .feature = plan.feature});
    for (const auto& existing : features_)
        if (existing.feature == plan.feature)
            return std::unexpected(PlanError{.stage = PlanStage::duplicate_feature, .feature = plan.feature});
    try {
        features_.push_back({plan.feature, {plan.operations.begin(), plan.operations.end()},
            {plan.install_after.begin(), plan.install_after.end()}});
        return {};
    } catch (...) {
        return std::unexpected(PlanError{.stage = PlanStage::allocation, .feature = plan.feature});
    }
}
std::expected<ApprovedVersionedPlan, PlanError> VersionedPlanSet::Validate(
    const runtime_image::RuntimeImage& image, const GameDetection& detection) const noexcept {
    return ValidateContext(image, ContextFor(detection));
}
std::expected<ApprovedVersionedPlan, PlanError> VersionedPlanSet::Validate(
    const runtime_image::RuntimeImage& image, const NesysDetection& detection) const noexcept {
    return ValidateContext(image, ContextFor(detection));
}

std::expected<ApprovedVersionedPlan, PlanError> VersionedPlanSet::ValidateContext(
    const runtime_image::RuntimeImage& image, PlanContext context) const noexcept {
    try {
        if (!ValidContext(context) || requirements_.empty())
            return std::unexpected(PlanError{.stage = PlanStage::invalid_plan, .context = context});
        const bool game = std::holds_alternative<GameBuild>(context.build);
        for (const auto& requirement : requirements_) {
            const bool present = std::ranges::any_of(features_, [&](const auto& feature) {
                return feature.feature == requirement.feature;
            });
            if ((requirement.mandatory || requirement.enabled) && !present)
                return std::unexpected(PlanError{.stage = PlanStage::unsupported_feature,
                    .context = context, .feature = requirement.feature});
            if (!requirement.mandatory && !requirement.enabled && present)
                return std::unexpected(PlanError{.stage = PlanStage::invalid_plan,
                    .context = context, .feature = requirement.feature});
        }
        // Validate all shapes and dependency references before accessing memory.
        for (const auto& feature : features_) {
            if ((game && feature.feature == FeatureId::nesys_ping) ||
                (!game && feature.feature != FeatureId::nesys_ping) ||
                !std::ranges::any_of(requirements_, [&](const auto& r) { return r.feature == feature.feature; }))
                return std::unexpected(PlanError{.stage = PlanStage::invalid_plan,
                    .context = context, .feature = feature.feature});
            for (std::size_t i = 0; i < feature.operations.size(); ++i) {
                const auto& operation = feature.operations[i];
                const auto& site = ContractOf(operation);
                const bool hook = site.kind == VersionedOperationKind::inline_hook ||
                    site.kind == VersionedOperationKind::mid_hook;
                const bool patch = site.kind == VersionedOperationKind::byte_patch ||
                    site.kind == VersionedOperationKind::global_vtable_slot;
                if (!ValidPayload(operation) || site.feature != feature.feature || site.site.empty() || !site.rva ||
                    !site.protected_span || !ValidPattern(site.original) ||
                    (!hook && site.original.size > site.protected_span) ||
                    (hook && site.protected_span < 5) ||
                    ((hook || ReadOnly(site)) && site.installed.size != 0) ||
                    (!hook && !patch && !ReadOnly(site)) ||
                    (patch && (!ValidPattern(site.installed) ||
                        site.original.size != site.installed.size ||
                        site.protected_span != site.original.size)) ||
                    (site.kind == VersionedOperationKind::global_vtable_slot &&
                        (site.protected_span != sizeof(void*) || site.rva % alignof(void*) != 0)) ||
                    (ReadOnly(site) && site.known_disposition != SiteDisposition::verify_only) ||
                    (!ReadOnly(site) && site.known_disposition != SiteDisposition::install &&
                        site.known_disposition != SiteDisposition::already_installed) ||
                    (hook && site.known_disposition != SiteDisposition::install) ||
                    (context.proof == DetectionProof::complete_local_contract &&
                        site.known_disposition == SiteDisposition::already_installed))
                    return std::unexpected(Error(PlanStage::invalid_plan, context, site));
                for (std::size_t j = 0; j < i; ++j)
                    if (ContractOf(feature.operations[j]).site == site.site)
                        return std::unexpected(Error(PlanStage::duplicate_site, context, site));
            }
            for (std::size_t i = 0; i < feature.install_after.size(); ++i) {
                const auto dependency = feature.install_after[i];
                if (!std::ranges::any_of(features_, [&](const auto& f) { return f.feature == dependency; }) ||
                    std::find(feature.install_after.begin(), feature.install_after.begin() + i, dependency) !=
                        feature.install_after.begin() + i)
                    return std::unexpected(PlanError{.stage = PlanStage::missing_dependency,
                        .context = context, .feature = feature.feature, .peer_feature = dependency});
            }
        }

        std::vector<std::size_t> order;
        std::vector<bool> emitted(features_.size());
        while (order.size() < features_.size()) {
            bool progressed = false;
            for (std::size_t i = 0; i < features_.size(); ++i) {
                if (emitted[i]) continue;
                const bool ready = std::ranges::all_of(features_[i].install_after, [&](FeatureId dependency) {
                    return std::ranges::any_of(order, [&](std::size_t j) { return features_[j].feature == dependency; });
                });
                if (!ready) continue;
                emitted[i] = true;
                order.push_back(i);
                progressed = true;
            }
            if (!progressed)
                return std::unexpected(PlanError{.stage = PlanStage::dependency_cycle, .context = context});
        }
        std::vector<ApprovedSite> sites;
        for (const auto index : order) {
            const auto begin = sites.size();
            for (const auto& operation : features_[index].operations) {
                const auto& contract = ContractOf(operation);
                const runtime_image::SiteIdentity identity{FeatureName(contract.feature), contract.site, contract.rva};
                const auto address = image.Resolve(identity,
                    std::max<std::uint32_t>(contract.protected_span, contract.original.size));
                if (!address) {
                    auto error = Error(PlanStage::address_range, context, contract);
                    error.address = address.error().address;
                    error.memory = address.error();
                    return std::unexpected(error);
                }
                const auto disposition = ReadOnly(contract) ? SiteDisposition::verify_only :
                    context.proof == DetectionProof::exact_known_hash ? contract.known_disposition : SiteDisposition::install;
                sites.push_back({operation, disposition, *address});
            }
            std::stable_sort(sites.begin() + begin, sites.end(), [](const auto& left, const auto& right) {
                return left.contract().install_order < right.contract().install_order;
            });
        }
        std::vector<std::size_t> addresses(sites.size());
        std::iota(addresses.begin(), addresses.end(), 0);
        std::stable_sort(addresses.begin(), addresses.end(), [&](auto l, auto r) {
            return sites[l].address < sites[r].address;
        });
        for (std::size_t i = 0; i < addresses.size(); ++i) {
            const auto& left = sites[addresses[i]];
            if (ReadOnly(left.contract())) continue;
            for (std::size_t j = i + 1; j < addresses.size(); ++j) {
                const auto& right = sites[addresses[j]];
                if (right.address >= left.address + left.contract().protected_span) break;
                if (ReadOnly(right.contract())) continue;
                // Identical vtable registration is one physical mutation.
                if (left.contract().kind == VersionedOperationKind::global_vtable_slot &&
                    right.contract().kind == VersionedOperationKind::global_vtable_slot &&
                    left.address == right.address && left.contract().original == right.contract().original &&
                    left.contract().installed == right.contract().installed && left.disposition == right.disposition)
                    continue;
                auto error = Error(PlanStage::overlap, context, right.contract());
                error.address = right.address;
                error.peer_feature = left.contract().feature;
                error.peer_site = left.contract().site;
                return std::unexpected(error);
            }
        }
        if (context.proof == DetectionProof::complete_local_contract) {
            for (const auto& site : sites) {
                const auto actual = image.Read(
                    {FeatureName(site.contract().feature), site.contract().site, site.contract().rva},
                    site.contract().original.size);
                if (!actual || *actual != site.contract().original) {
                    auto error = Error(PlanStage::contract_mismatch, context, site.contract());
                    error.address = site.address;
                    error.observed = actual ? *actual : actual.error().observed;
                    if (!actual) error.memory = actual.error();
                    return std::unexpected(error);
                }
            }
        }
        // Matching vtable consumers share the first physical exchange. Keep
        // every original contract in preflight, but execute the slot only once.
        for (std::size_t i = 0; i < sites.size(); ++i) {
            if (sites[i].contract().kind != VersionedOperationKind::global_vtable_slot) continue;
            for (std::size_t j = 0; j < i; ++j) {
                if (sites[j].contract().kind == VersionedOperationKind::global_vtable_slot &&
                    sites[j].address == sites[i].address) {
                    sites[i].disposition = SiteDisposition::verify_only;
                    break;
                }
            }
        }
        // Approval owns a copy of every contract and concrete operation. No writes or hooks occur above.
        return ApprovedVersionedPlan{context, image, std::move(sites)};
    } catch (...) {
        return std::unexpected(PlanError{.stage = PlanStage::allocation, .context = context});
    }
}

const char* FeatureName(FeatureId id) noexcept {
    switch (id) {
    case FeatureId::game_compatibility: return "GameCompatibility";
    case FeatureId::auto_play: return "AutoPlay";
    case FeatureId::song_unlock: return "SongUnlock";
    case FeatureId::switch_input: return "SwitchInput";
    case FeatureId::absolute_judgement: return "AbsoluteJudgement";
    case FeatureId::framerate: return "Framerate";
    case FeatureId::countdown: return "Countdown";
    case FeatureId::test_mode_timing: return "TestModeTiming";
    case FeatureId::renderer_device_loss: return "RendererDeviceLoss";
    case FeatureId::windowed_widescreen: return "WindowedWidescreen";
    case FeatureId::asio_close: return "AsioClose";
    case FeatureId::nesys_ping: return "NesysPing";
    }
    return "unknown";
}
} // namespace gc::game_version

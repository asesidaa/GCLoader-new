#include "Patches/GameVersion/VersionedPlanDiagnostics.h"
#include <format>

namespace gc::game_version {
namespace {
std::string Bytes(const runtime_image::BytePattern& pattern) {
    std::string result;
    for (const auto value : pattern.view()) {
        if (!result.empty()) result += ' ';
        result += std::format("{:02X}", std::to_integer<unsigned>(value));
    }
    return result;
}
const char* BuildName(const SelectedBuild& build) noexcept {
    return std::holds_alternative<GameBuild>(build) ? "groove_coaster_471" : "nesys_current_supported";
}
const char* VariantName(const SelectedVariant& variant) noexcept {
    if (const auto* game = std::get_if<GameImageVariant>(&variant)) {
        switch (*game) {
        case GameImageVariant::clean: return "clean";
        case GameImageVariant::legacy_patched: return "legacy_patched";
        case GameImageVariant::locally_verified: return "locally_verified";
        }
    } else {
        switch (std::get<nesys_service::NesysImageVariant>(variant)) {
        case nesys_service::NesysImageVariant::original: return "original";
        case nesys_service::NesysImageVariant::locally_verified: return "locally_verified";
        }
    }
    return "unknown";
}
}
const char* PlanStageName(PlanStage stage) noexcept {
    switch (stage) {
    case PlanStage::invalid_plan: return "invalid_plan";
    case PlanStage::duplicate_feature: return "duplicate_feature";
    case PlanStage::duplicate_site: return "duplicate_site";
    case PlanStage::unsupported_feature: return "unsupported_feature";
    case PlanStage::missing_dependency: return "missing_dependency";
    case PlanStage::dependency_cycle: return "dependency_cycle";
    case PlanStage::address_range: return "address_range";
    case PlanStage::overlap: return "overlap";
    case PlanStage::contract_mismatch: return "contract_mismatch";
    case PlanStage::allocation: return "allocation";
    }
    return "unknown";
}
diagnostics::FatalProcessReport FormatPlanError(const PlanError& error) {
    auto log = std::format(
        "VersionedPlan: rejected stage={} build={} variant={} proof={} feature={} site={} "
        "rva=0x{:08X} address=0x{:08X} expected_original={} expected_installed={} observed={}",
        PlanStageName(error.stage), BuildName(error.context.build), VariantName(error.context.variant),
        error.context.proof == DetectionProof::exact_known_hash ? "exact_known_hash" : "complete_local_contract",
        FeatureName(error.feature), error.site, error.rva, error.address,
        Bytes(error.expected_original), Bytes(error.expected_installed), Bytes(error.observed));
    if (error.peer_feature)
        log += std::format(" peer_feature={} peer_site={}", FeatureName(*error.peer_feature), error.peer_site);
    if (error.memory)
        log += std::format(" memory_stage={} win32_error={} memory_changed={} "
            "restore_attempted={} restore_succeeded={}", runtime_image::MemoryStageName(error.memory->stage),
            error.memory->win32_error, error.memory->memory_changed,
            error.memory->restore_attempted, error.memory->restore_succeeded);
    return {std::move(log),
        L"GCLoader could not validate this executable and its enabled patches. "
        L"Check the process loader log for the exact feature, site, and failure.",
        L"GCLoader executable validation error"};
}
}

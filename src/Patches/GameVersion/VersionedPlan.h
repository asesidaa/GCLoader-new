#pragma once
#include "Patches/GameVersion/BuildDetector.h"
#include "Patches/RuntimeImage/RuntimeImage.h"
#include "Platform/Win32/Hooking/HookPlan.h"
#include <vector>

namespace gc::game_version {
enum class VersionedOperationKind : std::uint8_t {
    byte_patch, inline_hook, mid_hook, global_vtable_slot, read_only_contract,
};
enum class SiteDisposition : std::uint8_t { install, already_installed, verify_only };
struct SiteContract final {
    FeatureId feature;
    // Feature-owned literal with process lifetime, like the profile's ABI.
    std::string_view site;
    VersionedOperationKind kind;
    runtime_image::Rva rva{};
    std::uint32_t protected_span{};
    runtime_image::BytePattern original{};
    runtime_image::BytePattern installed{};
    std::uint32_t install_order{};
    SiteDisposition known_disposition{SiteDisposition::install};
};
struct BytePatchOperation final {
    SiteContract contract;
    runtime_image::BytePattern replacement;
    runtime_image::MemoryKind memory_kind{runtime_image::MemoryKind::code};
};
struct InlineHookOperation final {
    SiteContract contract;
    void* detour{};
    hooking::OriginalPublisher original;
};
struct MidHookOperation final { SiteContract contract; safetyhook::MidHookFn callback{}; };
struct GlobalVtableSlotOperation final {
    SiteContract contract;
    void* expected{};
    void* replacement{};
};
struct ReadOnlyContractOperation final { SiteContract contract; };
using VersionedOperation = std::variant<BytePatchOperation, InlineHookOperation,
    MidHookOperation, GlobalVtableSlotOperation, ReadOnlyContractOperation>;
[[nodiscard]] const SiteContract& ContractOf(const VersionedOperation&) noexcept;
struct FeaturePlan final {
    FeatureId feature;
    std::span<const VersionedOperation> operations;
    std::span<const FeatureId> install_after;
};
using SelectedBuild = std::variant<GameBuild, nesys_service::NesysBuild>;
using SelectedVariant = std::variant<GameImageVariant, nesys_service::NesysImageVariant>;
struct PlanContext final {
    SelectedBuild build{GameBuild::groove_coaster_471};
    SelectedVariant variant{GameImageVariant::clean};
    DetectionProof proof{DetectionProof::exact_known_hash};
};
enum class PlanStage : std::uint8_t {
    invalid_plan, duplicate_feature, duplicate_site, unsupported_feature,
    missing_dependency, dependency_cycle, address_range, overlap, contract_mismatch, allocation,
};
struct PlanError final {
    PlanStage stage{};
    PlanContext context{};
    FeatureId feature{};
    std::string_view site;
    runtime_image::Rva rva{};
    std::uintptr_t address{};
    runtime_image::BytePattern expected_original{};
    runtime_image::BytePattern expected_installed{};
    runtime_image::BytePattern observed{};
    std::optional<FeatureId> peer_feature;
    std::string_view peer_site;
    std::optional<runtime_image::RuntimeImageError> memory;
};
struct ApprovedSite final {
    VersionedOperation operation;
    SiteDisposition disposition;
    std::uintptr_t address{};
    [[nodiscard]] const SiteContract& contract() const noexcept { return ContractOf(operation); }
};
class VersionedPlanSet;
class ApprovedVersionedPlan final {
public:
    [[nodiscard]] std::span<const ApprovedSite> sites() const noexcept { return sites_; }
    [[nodiscard]] const PlanContext& context() const noexcept { return context_; }
    [[nodiscard]] std::uintptr_t image_base() const noexcept { return image_base_; }
    [[nodiscard]] std::uint32_t image_size() const noexcept { return image_size_; }
private:
    friend class VersionedPlanSet;
    ApprovedVersionedPlan(PlanContext context, const runtime_image::RuntimeImage& image,
                          std::vector<ApprovedSite> sites)
        : context_(context), image_base_(image.base()), image_size_(image.size()), sites_(std::move(sites)) {}
    PlanContext context_;
    std::uintptr_t image_base_{};
    std::uint32_t image_size_{};
    std::vector<ApprovedSite> sites_;
};
class VersionedPlanSet final {
public:
    [[nodiscard]] std::expected<void, PlanError> Require(FeatureRequirement) noexcept;
    [[nodiscard]] std::expected<void, PlanError> Add(FeaturePlan) noexcept;
    [[nodiscard]] std::expected<ApprovedVersionedPlan, PlanError>
    Validate(const runtime_image::RuntimeImage&, const GameDetection&) const noexcept;
    [[nodiscard]] std::expected<ApprovedVersionedPlan, PlanError>
    Validate(const runtime_image::RuntimeImage&, const NesysDetection&) const noexcept;
private:
    struct OwnedFeature final {
        FeatureId feature;
        std::vector<VersionedOperation> operations;
        std::vector<FeatureId> install_after;
    };
    [[nodiscard]] std::expected<ApprovedVersionedPlan, PlanError>
    ValidateContext(const runtime_image::RuntimeImage&, PlanContext) const noexcept;
    std::vector<FeatureRequirement> requirements_;
    std::vector<OwnedFeature> features_;
};
[[nodiscard]] PlanContext ContextFor(const GameDetection&) noexcept;
[[nodiscard]] PlanContext ContextFor(const NesysDetection&) noexcept;
} // namespace gc::game_version

#include "Patches/GameVersion/BuildDetector.h"

namespace gc::game_version {
namespace {
template <class Build, class Variant>
auto Detect(HMODULE module, std::span<const KnownImageDescriptor<Build, Variant>> known,
            Variant local_variant) noexcept
    -> std::expected<std::variant<BuildSelection<Build, Variant>,
        StructuralCandidate<Build, Variant>>, DetectionError> {
    using Result = std::variant<BuildSelection<Build, Variant>, StructuralCandidate<Build, Variant>>;
    auto identity = ReadLoadedExecutableIdentity(module);
    if (!identity)
        return std::unexpected(DetectionError{DetectionStage::identity, identity.error()});
    for (const auto& descriptor : known) {
        if (descriptor.sha256 == identity->sha256 && descriptor.file_size == identity->file_size)
            return Result{BuildSelection<Build, Variant>{descriptor.build, descriptor.variant,
                DetectionProof::exact_known_hash, std::move(*identity)}};
    }
    std::optional<Build> candidate;
    for (const auto& descriptor : known) {
        if (descriptor.machine != identity->machine ||
            descriptor.file_size != identity->file_size ||
            descriptor.preferred_image_base != identity->preferred_image_base ||
            descriptor.size_of_image != identity->size_of_image ||
            descriptor.time_date_stamp != identity->time_date_stamp)
            continue;
        // Clean and patched descriptors of the same build are one candidate.
        if (candidate && *candidate != descriptor.build)
            return std::unexpected(DetectionError{DetectionStage::ambiguous_candidate});
        candidate = descriptor.build;
    }
    if (!candidate) return std::unexpected(DetectionError{DetectionStage::no_candidate});
    return Result{StructuralCandidate<Build, Variant>{*candidate, local_variant, std::move(*identity)}};
}
}
std::expected<GameDetection, DetectionError> DetectGameBuild(HMODULE module) noexcept {
    return Detect(module, KnownGameImages(), GameImageVariant::locally_verified);
}
std::expected<NesysDetection, DetectionError> DetectNesysBuild(HMODULE module) noexcept {
    return Detect(module, KnownNesysImages(), nesys_service::NesysImageVariant::locally_verified);
}
} // namespace gc::game_version

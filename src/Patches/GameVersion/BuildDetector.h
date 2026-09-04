#pragma once
#include "Patches/GameVersion/KnownImages.h"
#include "Patches/GameVersion/FeatureCapability.h"
#include <optional>
#include <variant>

namespace gc::game_version {
template <class Build, class Variant>
struct StructuralCandidate final {
    Build build;
    Variant variant;
    LoadedImageIdentity identity;
    // A candidate has no proof. Only complete plan validation can approve it.
};
using GameSelection = BuildSelection<GameBuild, GameImageVariant>;
using NesysSelection = BuildSelection<nesys_service::NesysBuild, nesys_service::NesysImageVariant>;
using GameDetection = std::variant<GameSelection, StructuralCandidate<GameBuild, GameImageVariant>>;
using NesysDetection = std::variant<NesysSelection,
    StructuralCandidate<nesys_service::NesysBuild, nesys_service::NesysImageVariant>>;
enum class DetectionStage : std::uint8_t { identity, no_candidate, ambiguous_candidate, unsupported_feature };
struct DetectionError final {
    DetectionStage stage{};
    std::optional<IdentityError> identity;
    std::optional<FeatureId> feature;
};
[[nodiscard]] std::expected<GameDetection, DetectionError> DetectGameBuild(HMODULE) noexcept;
[[nodiscard]] std::expected<NesysDetection, DetectionError> DetectNesysBuild(HMODULE) noexcept;
} // namespace gc::game_version

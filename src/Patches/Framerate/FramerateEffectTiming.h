#pragma once

#include "Patches/Framerate/FramerateProfile.h"
#include "Patches/Framerate/FrameratePatchPlan.h"

#include <safetyhook.hpp>

// ReSharper disable once CppUnusedIncludeDirective
#include <cstddef>
// ReSharper disable once CppUnusedIncludeDirective
#include <cstdint>
#include <expected>
#include <optional>
#include <span>

namespace gc::framerate {

enum class EffectTimingDisposition {
    Hook,
    ManagerGated,
    AlreadyAuthoredNormalized,
    ResetOrConstant,
    ChildInherited,
    NonCtuneOutOfScope,
};

enum class EffectClockDomain {
    TargetFrame,
    Authored60Frame,
    Milliseconds,
    NormalizedProgress,
    ConstantOrSentinel,
    NonCtuneData,
};

struct EffectRegistrationSite {
    std::uintptr_t rva{};
    const char* owner{};
    const char* reaching_frame_path{};
};

struct EffectDurationQuerySite {
    std::uintptr_t rva{};
    const char* owner{};
    const char* consumer_path{};
};

struct EffectTimingSite {
    const char* stable_id{};
    std::uintptr_t boundary_rva{};
    EffectClockDomain source{};
    EffectClockDomain consumer{};
    EffectTimingDisposition disposition{};
    std::optional<FramerateHookId> hook_id{};
    const char* evidence{};
};

struct EffectTimingManifestSummary {
    std::size_t timing_sites{};
    std::size_t registration_sites{};
    std::size_t duration_queries{};
    std::size_t hook_contracts{};
    std::size_t manager_gated{};
    std::size_t already_authored{};
    std::size_t reset_or_constant{};
    std::size_t child_inherited{};
    std::size_t non_ctune_out_of_scope{};
};

enum class EffectTimingTransformError {
    ProfileConversion,
};

[[nodiscard]] std::span<const EffectRegistrationSite>
EffectRegistrationSites() noexcept;

[[nodiscard]] std::span<const EffectDurationQuerySite>
EffectDurationQuerySites() noexcept;

[[nodiscard]] std::span<const EffectTimingSite>
EffectTimingSites() noexcept;

[[nodiscard]] std::span<const FramerateHookContract>
FramerateEffectHookContracts() noexcept;

[[nodiscard]] EffectTimingManifestSummary
SummarizeEffectTimingManifest() noexcept;

[[nodiscard]] std::expected<void, EffectTimingTransformError>
MapEffectFrameEaxToAuthored60(
    safetyhook::Context& context,
    const FramerateProfile& profile) noexcept;

[[nodiscard]] std::expected<void, EffectTimingTransformError>
MapEffectFrameEdxToAuthored60(
    safetyhook::Context& context,
    const FramerateProfile& profile) noexcept;

[[nodiscard]] std::expected<void, EffectTimingTransformError>
ScaleEffectDurationEaxToTarget(
    safetyhook::Context& context,
    const FramerateProfile& profile) noexcept;

} // namespace gc::framerate

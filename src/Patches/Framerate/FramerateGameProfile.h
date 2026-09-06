#pragma once
#include "Patches/Framerate/FramerateNativeAbi.h"
#include "Patches/Framerate/FramerateEffectTiming.h"
#include "Patches/GameVersion/VersionedPlan.h"
#include <optional>
#include <span>

namespace gc::framerate {
// Operand carriers shared by the verified 4.71 and 2.06 instruction layouts.
struct AuthoredFrameOperand {
    std::array<std::byte, 0x18> padding{};
    float frame_milliseconds{1000.0F / 60.0F};
};

struct PlayerPositionDurationOperand {
    std::array<std::byte, 0xC4> padding{};
    std::int32_t duration_frames{};
};

static_assert(offsetof(AuthoredFrameOperand, frame_milliseconds) == 0x18);
static_assert(offsetof(PlayerPositionDurationOperand, duration_frames) == 0xC4);

enum class FramerateWriteValue {
    frame_ms, frame_seconds, smoothing, decay, repeat_initial,
    repeat_next, two_seconds, target_operand, menu_initial, menu_interval
};
struct FramerateWriteContract final {
    game_version::SiteContract site;
    FramerateWriteValue value;
    runtime_image::MemoryKind memory_kind;
};
struct FramerateHookContract final {
    FramerateHookId id;
    game_version::SiteContract site;
    std::optional<FramerateRegister> authored_operand_register;
};
struct FramerateTargetContract final {
    FramerateNativeTarget id;
    game_version::SiteContract site;
};
struct FramerateGameProfile final {
    game_version::GameBuild build;
    game_version::GameImageVariant variant;
    std::span<const FramerateWriteContract> writes;
    std::span<const FramerateHookContract> hooks;
    std::span<const FramerateTargetContract> targets;
    FramerateNativeLayout layout;
    EffectTimingManifestSummary effect_timing;
};
[[nodiscard]] std::expected<game_version::VersionedOperation, game_version::PlanError>
BindFramerateHook(const FramerateHookContract&) noexcept;
[[nodiscard]] const FramerateGameProfile* ProfileFor(
    game_version::GameBuild, game_version::GameImageVariant) noexcept;
}

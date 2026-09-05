#pragma once
#include "Patches/TestModeTiming/TimingSettingsGameAbi.h"

namespace gc::test_mode_timing {
struct TestModeTimingProfile final {
    game_version::GameBuild build;
    game_version::GameImageVariant variant;
    // Fifteen byte contracts, one write, two hooks, thirteen read-only pointers.
    std::array<game_version::VersionedOperation, 31> operations;
    std::array<runtime_image::Rva, kSoundVtableSlots> sound_vtable_targets;
    runtime_image::Rva judg_time_offset;
    runtime_image::Rva game_time_offset;
    TimingNativeLayout layout;
};
[[nodiscard]] const TestModeTimingProfile* ProfileFor(
    game_version::GameBuild, game_version::GameImageVariant) noexcept;
[[nodiscard]] std::expected<game_version::FeaturePlan, game_version::PlanError>
BuildTestModeTimingPlan(game_version::GameBuild, game_version::GameImageVariant) noexcept;
}

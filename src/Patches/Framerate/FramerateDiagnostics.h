#pragma once

#include "Patches/Framerate/FramerateEffectTiming.h"
#include "Patches/Framerate/FramerateMonitor.h"
#include "Patches/Framerate/FramerateTimingProfile.h"

#include <Windows.h>
// ReSharper disable once CppUnusedIncludeDirective
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace gc::framerate {

struct FramerateStartupPatchSummary {
    std::size_t direct_write_count{};
    std::size_t hook_count{};
    std::int32_t menu_repeat_initial{};
    std::int32_t menu_repeat_interval{};
    float authored_frame_milliseconds{};
    EffectTimingManifestSummary effect_timing{};
};

struct FramerateEffectRuntimeStats {
    std::uint64_t flow_item_mappings{};
    std::uint64_t tutorial_elapsed_mappings{};
    std::uint64_t chart_preroll_scalings{};
    std::uint64_t player_modulo_mappings{};
};

void ReportFramerateStartup(
    const FramerateTimingProfile& profile,
    const FramerateStartupPatchSummary& summary) noexcept;

[[nodiscard]] std::string FormatFramerateEffectRuntimeStats(
    const FramerateEffectRuntimeStats& stats);

[[nodiscard]] bool ShouldSuggestIntervalModeOne(
    std::uint32_t target_fps,
    double measured_fps) noexcept;

[[noreturn]] void ReportFramerateMismatch(
    const FramerateObservation& observation) noexcept;

[[noreturn]] void ReportFramerateClockFailure(
    std::uint32_t target_fps) noexcept;

[[noreturn]] void ReportFramerateRuntimeFailure(
    std::string_view detail) noexcept;

} // namespace gc::framerate

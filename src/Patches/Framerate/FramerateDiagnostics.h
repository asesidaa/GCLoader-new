#pragma once

#include "Patches/Framerate/FramerateEffectTiming.h"
#include "Patches/Framerate/FramerateMonitor.h"
#include "Patches/Framerate/FramerateTimingProfile.h"

#include <Windows.h>
#include <atomic>
// ReSharper disable once CppUnusedIncludeDirective
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace gc::framerate {

struct FrameratePlatformActions {
    void (*log_info)(const char*);
    void (*log_warning)(const char*);
    void (*log_error)(const char*);
    void (*show_error)(const char*);
    void (*terminate_process)(DWORD);
    void (*fail_fast)();
};

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

[[nodiscard]] FrameratePlatformActions ProductionFrameratePlatformActions()
    noexcept;

void ReportFramerateStartup(
    const FramerateTimingProfile& profile,
    const FramerateStartupPatchSummary& summary,
    const FrameratePlatformActions& actions) noexcept;

[[nodiscard]] std::string FormatFramerateEffectRuntimeStats(
    const FramerateEffectRuntimeStats& stats);

[[nodiscard]] bool ShouldSuggestIntervalModeOne(
    std::uint32_t target_fps,
    double measured_fps) noexcept;

void ReportFramerateMismatch(
    const FramerateObservation& observation,
    std::atomic_bool& publication_latch,
    const FrameratePlatformActions& actions) noexcept;

void ReportFramerateClockFailure(
    std::uint32_t target_fps,
    std::atomic_bool& publication_latch,
    const FrameratePlatformActions& actions) noexcept;

void ReportFramerateRuntimeFailure(
    std::string_view detail,
    std::atomic_bool& publication_latch,
    const FrameratePlatformActions& actions) noexcept;

void ReportFramerateInitializationFailure(
    std::string_view detail,
    std::atomic_bool& publication_latch,
    const FrameratePlatformActions& actions) noexcept;

} // namespace gc::framerate

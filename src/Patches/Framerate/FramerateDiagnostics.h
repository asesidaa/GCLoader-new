#pragma once

#include "Patches/Framerate/FramerateMonitor.h"
#include "Patches/Framerate/FramerateProfile.h"

#include <Windows.h>
#include <atomic>
#include <cstddef>
#include <cstdint>
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
};

[[nodiscard]] FrameratePlatformActions ProductionFrameratePlatformActions()
    noexcept;

void ReportFramerateStartup(
    const FramerateProfile& profile,
    const FramerateStartupPatchSummary& summary,
    FrameratePlatformActions actions) noexcept;

[[nodiscard]] bool ShouldSuggestIntervalModeOne(
    std::uint32_t target_fps,
    double measured_fps) noexcept;

void ReportFramerateMismatch(
    const FramerateObservation& observation,
    std::atomic_bool& publication_latch,
    FrameratePlatformActions actions) noexcept;

void ReportFramerateClockFailure(
    std::uint32_t target_fps,
    std::atomic_bool& publication_latch,
    FrameratePlatformActions actions) noexcept;

void ReportFramerateRuntimeFailure(
    std::string_view detail,
    std::atomic_bool& publication_latch,
    FrameratePlatformActions actions) noexcept;

void ReportFramerateInitializationFailure(
    std::string_view detail,
    std::atomic_bool& publication_latch,
    FrameratePlatformActions actions) noexcept;

} // namespace gc::framerate

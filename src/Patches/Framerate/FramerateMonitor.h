#pragma once

#include <array>
// ReSharper disable once CppUnusedIncludeDirective
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>

namespace gc::framerate {

inline constexpr double kFramerateWarmupSeconds = 5.0;
inline constexpr double kFramerateWindowSeconds = 2.0;
inline constexpr double kFramerateTolerance = 0.03;
inline constexpr std::uint32_t kFramerateRequiredStreak = 3;
// ceil(2 seconds * 500 FPS * 1.03) plus two boundary intervals.
inline constexpr std::size_t kMaximumIntervalsPerWindow = 1032;

enum class FramerateMonitorError {
    TargetOutOfRange,
    InvalidQpcFrequency,
    QpcRangeOverflow,
};

enum class FramerateDecision {
    WindowMatch,
    WindowMismatch,
    Validated,
    FatalMismatch,
    FatalClock,
};

struct FramerateObservation {
    FramerateDecision decision{};
    std::uint32_t target_fps{};
    double measured_fps{};
    double relative_error{};
    std::size_t interval_count{};
    std::uint32_t matching_streak{};
    std::uint32_t mismatching_streak{};
    bool storage_overflowed{};
};

class FramerateMonitor {
public:
    [[nodiscard]] static std::expected<
        FramerateMonitor,
        FramerateMonitorError>
    Create(std::uint32_t target_fps, std::int64_t qpc_frequency) noexcept;

    [[nodiscard]] std::optional<FramerateObservation> Observe(
        std::int64_t qpc_timestamp) noexcept;

    [[nodiscard]] bool active() const noexcept { return active_; }

private:
    FramerateMonitor(
        std::uint32_t target_fps,
        std::int64_t qpc_frequency) noexcept;

    [[nodiscard]] FramerateObservation FinishWindow() noexcept;
    [[nodiscard]] FramerateObservation FatalClock() noexcept;

    std::uint32_t target_fps_{};
    std::int64_t qpc_frequency_{};
    std::int64_t warmup_ticks_{};
    std::int64_t window_ticks_{};
    std::int64_t first_timestamp_{};
    std::int64_t previous_timestamp_{};
    std::int64_t window_start_{};
    std::array<std::int64_t, kMaximumIntervalsPerWindow> intervals_{};
    std::size_t interval_count_{};
    std::uint32_t matching_streak_{};
    std::uint32_t mismatching_streak_{};
    bool active_{true};
    bool started_{};
    bool warming_up_{true};
    bool storage_overflowed_{};
};

} // namespace gc::framerate

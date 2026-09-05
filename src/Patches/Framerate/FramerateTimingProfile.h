#pragma once

#include <cstdint>
#include <expected>

namespace gc::framerate {

enum class FramerateTimingProfileError {
    TargetOutOfRange,
    ArithmeticOverflow,
    DestinationOverflow,
    InvalidPeriod,
};

class FramerateTimingProfile {
public:
    [[nodiscard]] static std::expected<
        FramerateTimingProfile,
        FramerateTimingProfileError>
    Create(std::uint32_t target_fps) noexcept;

    [[nodiscard]] std::uint32_t target_fps() const noexcept {
        return target_fps_;
    }
    [[nodiscard]] float target_fps_float() const noexcept {
        return target_fps_float_;
    }
    [[nodiscard]] const float* target_fps_operand() const noexcept {
        return &target_fps_float_;
    }
    [[nodiscard]] bool native_timing() const noexcept {
        return target_fps_ == 60;
    }
    [[nodiscard]] bool gameplay_validated() const noexcept {
        return gameplay_validated_;
    }
    [[nodiscard]] float frame_milliseconds() const noexcept {
        return frame_milliseconds_;
    }
    [[nodiscard]] float frame_seconds() const noexcept {
        return frame_seconds_;
    }
    [[nodiscard]] float render_smoothing_step() const noexcept {
        return render_smoothing_step_;
    }
    [[nodiscard]] float render_offset_decay_step() const noexcept {
        return render_offset_decay_step_;
    }
    [[nodiscard]] std::uint32_t two_second_frames() const noexcept {
        return two_second_frames_;
    }
    [[nodiscard]] std::uint32_t palette_frame_cap() const noexcept {
        return palette_frame_cap_;
    }
    [[nodiscard]] float ScalePerFrameDelta(float value) const noexcept {
        return value * 60.0F / target_fps_float_;
    }

    [[nodiscard]] std::expected<std::int32_t, FramerateTimingProfileError>
    ScaleDurationFrames(std::int32_t value) const noexcept;

    [[nodiscard]] std::expected<std::uint32_t, FramerateTimingProfileError>
    MapToAuthored60(std::uint32_t value) const noexcept;

private:
    explicit FramerateTimingProfile(std::uint32_t target_fps) noexcept;

    std::uint32_t target_fps_{};
    float target_fps_float_{};
    bool gameplay_validated_{};
    float frame_milliseconds_{};
    float frame_seconds_{};
    float render_smoothing_step_{};
    float render_offset_decay_step_{};
    std::uint32_t two_second_frames_{};
    std::uint32_t palette_frame_cap_{};
};

} // namespace gc::framerate

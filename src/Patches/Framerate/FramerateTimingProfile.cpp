#include "Patches/Framerate/FramerateTimingProfile.h"

#include "Patches/Framerate/FrameratePolicy.h"

#include <limits>

namespace gc::framerate
{
    FramerateTimingProfile::FramerateTimingProfile(std::uint32_t target_fps) noexcept
        : target_fps_{target_fps},
          target_fps_float_{static_cast<float>(target_fps)},
          gameplay_validated_{
              IsGameplayValidatedTargetFps(target_fps)
          },
          frame_milliseconds_{1000.0F / target_fps_float_},
          frame_seconds_{1.0F / target_fps_float_},
          render_smoothing_step_{4.0F * 60.0F / target_fps_float_},
          render_offset_decay_step_{5.0F * 60.0F / target_fps_float_},
          two_second_frames_{target_fps * 2},
          palette_frame_cap_{target_fps}
    {
    }

    std::expected<FramerateTimingProfile, FramerateTimingProfileError>
    FramerateTimingProfile::Create(std::uint32_t target_fps) noexcept
    {
        if (!IsTargetFpsInRange(target_fps))
        {
            return std::unexpected(FramerateTimingProfileError::TargetOutOfRange);
        }
        return FramerateTimingProfile{target_fps};
    }

    std::expected<std::int32_t, FramerateTimingProfileError>
    FramerateTimingProfile::ScaleDurationFrames(std::int32_t value) const noexcept
    {
        if (value <= 0)
        {
            return value;
        }

        constexpr auto maximum = std::numeric_limits<std::int64_t>::max();
        if (static_cast<std::int64_t>(value) >
            maximum / static_cast<std::int64_t>(target_fps_))
        {
            return std::unexpected(FramerateTimingProfileError::ArithmeticOverflow);
        }

        const auto product = static_cast<std::int64_t>(value) * target_fps_;
        const auto rounded = (product + 30) / 60;
        if (rounded > std::numeric_limits<std::int32_t>::max())
        {
            return std::unexpected(FramerateTimingProfileError::DestinationOverflow);
        }
        return static_cast<std::int32_t>(rounded);
    }

    std::expected<std::uint32_t, FramerateTimingProfileError>
    FramerateTimingProfile::MapToAuthored60(std::uint32_t value) const noexcept
    {
        constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
        if (static_cast<std::uint64_t>(value) > maximum / 60)
        {
            return std::unexpected(FramerateTimingProfileError::ArithmeticOverflow);
        }
        const auto mapped =
            static_cast<std::uint64_t>(value) * 60 / target_fps_;
        if (mapped > std::numeric_limits<std::uint32_t>::max())
        {
            return std::unexpected(FramerateTimingProfileError::DestinationOverflow);
        }
        return static_cast<std::uint32_t>(mapped);
    }
} // namespace gc::framerate

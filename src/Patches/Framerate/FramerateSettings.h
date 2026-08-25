#pragma once

#include <cstdint>

namespace gc::config
{
    class ConfigCompiler;
}

namespace gc::framerate
{
    class FramerateSettings final
    {
    public:
        [[nodiscard]] std::uint32_t target_fps() const noexcept
        {
            return target_fps_;
        }

        [[nodiscard]] bool timer_freeze_enabled() const noexcept
        {
            return timer_freeze_enabled_;
        }

    private:
        FramerateSettings(
            std::uint32_t target_fps,
            bool timer_freeze_enabled) noexcept
            : target_fps_(target_fps),
              timer_freeze_enabled_(timer_freeze_enabled)
        {
        }

        friend class gc::config::ConfigCompiler;
        std::uint32_t target_fps_{};
        bool timer_freeze_enabled_{};
    };
} // namespace gc::framerate

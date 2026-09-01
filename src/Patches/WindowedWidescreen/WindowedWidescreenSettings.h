#pragma once

#include <cstdint>

namespace gc::config
{
    class ConfigCompiler;
}

namespace gc::windowed_widescreen
{
    class WindowedWidescreenSettings final
    {
    public:
        [[nodiscard]] bool enabled() const noexcept
        {
            return enabled_;
        }

        [[nodiscard]] std::uint32_t output_width() const noexcept
        {
            return output_width_;
        }

        [[nodiscard]] std::uint32_t output_height() const noexcept
        {
            return output_height_;
        }

    private:
        WindowedWidescreenSettings(
            const bool enabled,
            const std::uint32_t output_width,
            const std::uint32_t output_height) noexcept
            : enabled_(enabled),
              output_width_(output_width),
              output_height_(output_height)
        {
        }

        friend class gc::config::ConfigCompiler;
        bool enabled_{};
        std::uint32_t output_width_{};
        std::uint32_t output_height_{};
    };
} // namespace gc::windowed_widescreen

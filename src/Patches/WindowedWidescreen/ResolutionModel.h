#pragma once

#include <cstdint>
#include <expected>
#include <optional>

namespace gc::windowed_widescreen
{
    inline constexpr std::uint32_t kNativeWidth = 720;
    inline constexpr std::uint32_t kNativeHeight = 1280;

    struct OutputSize
    {
        std::uint32_t width{};
        std::uint32_t height{};
    };

    struct NativeRect
    {
        std::int32_t left{};
        std::int32_t top{};
        std::int32_t right{};
        std::int32_t bottom{};
    };

    struct NativePoint
    {
        std::int32_t x{};
        std::int32_t y{};
    };

    enum class ResolutionError
    {
        width_below_native,
        height_below_native,
        signed_range,
        arithmetic_overflow,
    };

    class ResolutionModel
    {
    public:
        [[nodiscard]] static std::expected<ResolutionModel, ResolutionError>
        Create(std::uint32_t width, std::uint32_t height) noexcept;

        [[nodiscard]] OutputSize output_size() const noexcept
        {
            return output_size_;
        }

        [[nodiscard]] NativeRect native_rect() const noexcept
        {
            return native_rect_;
        }

        [[nodiscard]] std::optional<NativePoint> ClientToNative(
            std::int32_t client_x,
            std::int32_t client_y) const noexcept;

    private:
        ResolutionModel(OutputSize output_size, NativeRect native_rect) noexcept;

        OutputSize output_size_{};
        NativeRect native_rect_{};
    };
} // namespace gc::windowed_widescreen

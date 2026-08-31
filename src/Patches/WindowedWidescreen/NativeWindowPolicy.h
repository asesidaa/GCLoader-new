#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>

namespace gc::windowed_widescreen
{
    struct MonitorWorkArea
    {
        std::int32_t left{};
        std::int32_t top{};
        std::int32_t right{};
        std::int32_t bottom{};
        bool primary{};
    };

    struct WindowOuterSize
    {
        std::uint32_t width{};
        std::uint32_t height{};
    };

    struct WindowPlacement
    {
        std::int32_t x{};
        std::int32_t y{};
        WindowOuterSize size{};
        std::size_t monitor_index{};
    };

    enum class NativeWindowPolicyError
    {
        invalid_outer_size,
        no_fitting_work_area,
        arithmetic_overflow,
    };

    [[nodiscard]] std::expected<WindowPlacement, NativeWindowPolicyError>
    SelectWindowPlacement(
        std::span<const MonitorWorkArea> monitors,
        WindowOuterSize outer_size) noexcept;
} // namespace gc::windowed_widescreen

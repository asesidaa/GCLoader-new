#pragma once

#include "Patches/WindowedWidescreen/ResolutionModel.h"
#include "Patches/WindowedWidescreen/WindowedWidescreenAbi.h"

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
        frame_adjustment_failed,
        monitor_enumeration_failed,
        renderer_contract_failed,
        window_move_failed,
        renderer_window_invalid,
        stored_style_mismatch,
        actual_style_mismatch,
        client_size_mismatch,
        moved_client_size_mismatch,
    };

    struct PreparedWindowPlacement
    {
        OutputSize client_size{};
        WindowPlacement outer{};
        std::uint32_t style{};
    };

    [[nodiscard]] std::expected<WindowPlacement, NativeWindowPolicyError>
    SelectWindowPlacement(
        std::span<const MonitorWorkArea> monitors,
        WindowOuterSize outer_size) noexcept;

    [[nodiscard]] std::expected<
        PreparedWindowPlacement,
        NativeWindowPolicyError>
    PrepareFixedWindowPlacement(OutputSize client_size, std::uint32_t style) noexcept;

    [[nodiscard]] std::expected<void, NativeWindowPolicyError>
    ValidateAndPlaceRendererWindow(
        std::uintptr_t renderer_owner,
        const PreparedWindowPlacement& placement,
        const WidescreenNativeLayout& layout) noexcept;
} // namespace gc::windowed_widescreen

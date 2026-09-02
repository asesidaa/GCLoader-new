#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace gc::windowed_widescreen
{
    inline constexpr std::uintptr_t kWidescreenPreferredImageBase =
        0x00400000;
    inline constexpr std::size_t kWidescreenMaximumPatternSize = 24;

    enum class WidescreenContractSite : std::uint8_t
    {
        none,
        config_apply,
        window_device_create,
        frame_begin,
        frame_end,
        task_dispatch,
        test_mode_native_begin,
        test_mode_native_end,
        screen_width_int,
        screen_height_int,
        screen_width_float,
        screen_height_float,
        target_width_int,
        target_height_int,
        target_width_float,
        target_height_float,
        logical_resolution_set,
        logical_target_width_set,
        logical_target_height_set,
        viewport_reset,
        mouse_debug_poll,
        reset_pre,
        reset_post,
        gameplay_stage_background,
        gameplay_track,
        gameplay_effects,
        gameplay_effects_end,
        gameplay_hud_projection,
        combo_begin,
        combo_normal_digits,
        combo_end,
        gameplay_feedback_draw_begin,
        gameplay_feedback_draw_end,
        note_tutorial_group_begin,
        note_tutorial_group_end,
        clip_default,
        clip_gate,
        clip_continuation,
        batch_flush,
        clip_owner,
        live_frustum_helper,
        config_width_setter,
        config_height_setter,
        config_resize_setter,
        config_minmax_setter,
        config_mode_setter,
        common_2d_render,
        common_3d_render,
    };

    enum class WidescreenHookKind : std::uint8_t
    {
        read_only,
        inline_hook,
        mid_hook,
    };

    struct BytePattern
    {
        std::array<std::byte, kWidescreenMaximumPatternSize> bytes{};
        std::uint8_t size{};

        [[nodiscard]] constexpr std::span<const std::byte> view()
        const noexcept
        {
            return valid() ? std::span{bytes}.first(size)
                           : std::span<const std::byte>{};
        }

        [[nodiscard]] constexpr bool valid() const noexcept
        {
            return size > 0 && size <= bytes.size();
        }
    };

    template <std::uint8_t... Values>
    [[nodiscard]] consteval BytePattern BytePatternOf() noexcept
    {
        static_assert(
            sizeof...(Values) > 0 &&
            sizeof...(Values) <= kWidescreenMaximumPatternSize);
        BytePattern pattern{};
        pattern.size = static_cast<std::uint8_t>(sizeof...(Values));
        std::size_t index{};
        ((pattern.bytes[index++] = static_cast<std::byte>(Values)), ...);
        return pattern;
    }

    struct WidescreenByteContract
    {
        WidescreenContractSite site{WidescreenContractSite::none};
        std::uint32_t rva{};
        BytePattern pattern{};
        WidescreenHookKind hook_kind{WidescreenHookKind::read_only};
    };

    struct WidescreenPointerContract
    {
        WidescreenContractSite site{WidescreenContractSite::none};
        std::uint32_t pointer_rva{};
        std::uint32_t target_rva{};
    };

    enum class WidescreenCallingConvention : std::uint8_t
    {
        cdecl_call,
        thiscall_call,
        mid_context,
        read_only,
    };

    struct WidescreenFunctionAbi
    {
        WidescreenContractSite site{WidescreenContractSite::none};
        WidescreenCallingConvention calling_convention{
            WidescreenCallingConvention::read_only};
        std::uint8_t argument_count{};
    };

    inline constexpr std::size_t kMainConfigVtableRva = 0x002AE62C;
    inline constexpr std::size_t kRendererOwnerDeviceOffset = 0x08;
    inline constexpr std::size_t kRendererOwnerWindowOffset = 0x8C;
    inline constexpr std::size_t kRendererOwnerStyleOffset = 0x98;
    inline constexpr std::uint32_t kFixedDecoratedWindowStyle = 0x00CA0000;
    inline constexpr std::size_t kBatchQueuePointerRva = 0x003F24FC;
    inline constexpr std::size_t kBatchQueueCount = 4;
    inline constexpr std::size_t kBatchQueueStride = 24;
    inline constexpr std::size_t kBatchPendingCountOffset = 24;
    inline constexpr std::size_t kMouseXWord = 0;
    inline constexpr std::size_t kMouseYWord = 1;
    inline constexpr std::size_t kMouseValidWord = 6;

    [[nodiscard]] std::span<const WidescreenByteContract>
    WindowedWidescreenByteContracts() noexcept;

    [[nodiscard]] std::span<const WidescreenPointerContract>
    WindowedWidescreenPointerContracts() noexcept;

    [[nodiscard]] std::span<const WidescreenFunctionAbi>
    WindowedWidescreenFunctionAbis() noexcept;

    [[nodiscard]] const char* WidescreenContractSiteName(
        WidescreenContractSite site) noexcept;
} // namespace gc::windowed_widescreen

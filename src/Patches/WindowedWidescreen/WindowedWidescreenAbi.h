#pragma once
#include "Patches/GameVersion/VersionedPlan.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace gc::windowed_widescreen {
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
        chain_label_begin,
        chain_digits_begin,
        gameplay_feedback_draw_begin,
        gameplay_feedback_draw_end,
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
        network_status_movie_clip_accept,
        network_status_shape_draw_visit,
        common_3d_render,
        bar_names_begin,
        chain_glow_begin,
        hundred_digits_begin,
        effect_packet_allocated,
        effect_packet_begin,
        bar_difficulty_a_begin,
        bar_difficulty_a_end,
        bar_difficulty_b_begin,
        bar_difficulty_b_end,
        bar_panel_480_begin,
        bar_panel_480_end,
        bar_panel_524_begin,
        bar_panel_524_end,
        bar_panel_568_begin,
        bar_panel_568_end,
        bar_stage_panel_begin,
        bar_stage_panel_end,
        bar_stage_current_begin,
        bar_stage_current_end,
        bar_stage_total_begin,
        bar_stage_total_end,
        bar_gauge_begin,
        bar_gauge_end,
        bar_panel_216_begin,
        bar_panel_216_end,
        bar_score_panel_begin,
        bar_score_panel_end,
        bar_score_digits_begin,
        bar_score_digits_end,
        bar_extra_panel_a_begin,
        bar_extra_panel_a_end,
        bar_extra_digits_a_begin,
        bar_extra_digits_a_end,
        bar_extra_panel_b_begin,
        bar_extra_panel_b_end,
        bar_extra_digits_b_begin,
        bar_extra_digits_b_end,
        bar_mode_panel_begin,
        bar_mode_panel_end,
        bar_player_panel_begin,
        bar_player_panel_end,
        bar_status_panel_begin,
        bar_status_panel_end,
        bar_names_end,
        chain_label_end,
        chain_digits_end,
        chain_glow_end,
        hundred_digits_end,
        effect_packet_end,
        stage_title_draw_begin,
        stage_title_draw_end,
        stage_players_draw_begin,
        stage_players_draw_end,
        timed_text_draw_begin,
        timed_text_draw_end,
        movie_clip_definition_getter,
        movie_clip_parent_assignment,
        movie_definition_name_getter,
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
            WidescreenCallingConvention::read_only
        };
        std::uint8_t argument_count{};
    };
struct WidescreenNativeLayout final {
    runtime_image::Rva main_config_vtable{};
    runtime_image::Rva batch_queue_pointer{};
    runtime_image::Rva movie_clip_draw_visitor_vtable{};
    runtime_image::Rva common_2d_vtable{};
    runtime_image::Rva common_3d_vtable{};
    std::size_t renderer_owner_device_offset{};
    std::size_t renderer_owner_window_offset{};
    std::size_t renderer_owner_style_offset{};
    std::uint32_t fixed_decorated_window_style{};
    std::size_t batch_queue_stride{};
    std::size_t batch_pending_count_offset{};
    std::size_t mouse_x_word{};
    std::size_t mouse_y_word{};
    std::size_t mouse_valid_word{};
    std::size_t movie_clip_name_offset{};
    std::size_t movie_clip_name_hash_offset{};
    std::uint32_t movie_clip_name_hash_multiplier{};
    std::size_t combo_entry_frame_offset{};
    std::size_t tune_effect_collection_offset{};
    std::size_t effect_root_manager_offset{};
    std::size_t pointer_collection_begin_offset{};
    std::size_t pointer_collection_end_offset{};
    std::size_t network_status_visitor_matrix_stack_offset{};
    // Empty symbol disables separator selection on builds without this contract.
    std::string_view gameplay_header_separator_symbol{};
    std::size_t movie_clip_definition_offset{};
    std::size_t movie_clip_parent_offset{};
    std::size_t movie_definition_name_offset{};
};
struct NativeViewport;
namespace native {
using ConfigApply = int(__cdecl*)(int);
using OwnerCall = int(__thiscall*)(void*);
using IntDimension = int(__cdecl*)();
using FloatDimension = float(__cdecl*)();
using ResolutionSet = int(__cdecl*)(int, int);
using TargetDimensionSet = int(__cdecl*)(int);
using ViewportReset = int(__cdecl*)(const NativeViewport*);
using MousePoll = POINT*(__thiscall*)(void*, std::uint32_t*);
using ConfigDimensionSetter = int(__thiscall*)(void*, int, int);
using ConfigResizeSetter = void(__thiscall*)(void*, int);
using ConfigMinmaxSetter = void(__thiscall*)(void*, int, int);
using ConfigModeSetter = int(__thiscall*)(void*, int, int, int, int);
using BatchFlush = void(__cdecl*)();
using MovieClipAccept = int(__thiscall*)(void*, void*);
using ShapeDrawVisit = void(__thiscall*)(void*, void*);
}
struct WidescreenGameAbi final {
    WidescreenNativeLayout layout;
    std::size_t hook_count{};
    bool selected_hud_draws_only{};
    std::uintptr_t main_config_vtable{};
    std::uintptr_t batch_queue_pointer{};
    std::uintptr_t movie_clip_draw_visitor_vtable{};
    std::uintptr_t common_2d_vtable{};
    std::uintptr_t common_3d_vtable{};
    std::uintptr_t clip_continuation{};
    native::BatchFlush batch_flush{};
    native::ConfigDimensionSetter config_width_setter{};
    native::ConfigDimensionSetter config_height_setter{};
    native::ConfigResizeSetter config_resize_setter{};
    native::ConfigMinmaxSetter config_minmax_setter{};
    native::ConfigModeSetter config_mode_setter{};
};
struct WindowedWidescreenProfile;
[[nodiscard]] std::expected<WidescreenGameAbi, game_version::PlanError>
BuildWidescreenGameAbi(const runtime_image::RuntimeImage&,
    const WindowedWidescreenProfile&, const game_version::ApprovedVersionedPlan&) noexcept;
[[nodiscard]] const char* WidescreenContractSiteName(WidescreenContractSite) noexcept;
} // namespace gc::windowed_widescreen

#include "Patches/WindowedWidescreen/WindowedWidescreenProfile.h"
#include <algorithm>
namespace gc::windowed_widescreen {
namespace {
using namespace game_version;
using namespace runtime_image;
constexpr std::array kBytes{
    WidescreenByteContract{WidescreenContractSite::config_apply, {FeatureId::windowed_widescreen, "config_apply", VersionedOperationKind::inline_hook,
             0x23C360, 6, PatternOf<0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x14, 0xE8, 0xE5, 0xF8, 0xFF, 0xFF>(), {}, 0}},
    WidescreenByteContract{WidescreenContractSite::window_device_create, {FeatureId::windowed_widescreen, "window_device_create", VersionedOperationKind::inline_hook,
             0x5B8A0, 5, PatternOf<0x83, 0xEC, 0x64, 0x53, 0x55, 0x56, 0x57, 0x6A, 0x30, 0x33, 0xED, 0x8D>(), {}, 1}},
    WidescreenByteContract{WidescreenContractSite::frame_begin, {FeatureId::windowed_widescreen, "frame_begin", VersionedOperationKind::inline_hook,
             0x5AC70, 7, PatternOf<0x51, 0x53, 0x56, 0x8D, 0x44, 0x24, 0x8, 0x57, 0x50, 0x8B, 0xF1, 0xE8>(), {}, 5}},
    WidescreenByteContract{WidescreenContractSite::frame_end, {FeatureId::windowed_widescreen, "frame_end", VersionedOperationKind::inline_hook,
             0x5ACE0, 5, PatternOf<0x8B, 0x41, 0x8, 0x8B, 0x8, 0x8B, 0x91, 0xA8, 0x0, 0x0, 0x0, 0x50>(), {}, 6}},
    WidescreenByteContract{WidescreenContractSite::task_dispatch, {FeatureId::windowed_widescreen, "task_dispatch", VersionedOperationKind::inline_hook,
             0x5C1B0, 7, PatternOf<0x8B, 0x9, 0x8B, 0x1, 0x8B, 0x50, 0x10, 0xFF, 0xE2, 0xCC, 0xCC, 0xCC>(), {}, 7}},
    WidescreenByteContract{WidescreenContractSite::screen_width_int, {FeatureId::windowed_widescreen, "screen_width_int", VersionedOperationKind::inline_hook,
             0x52F20, 5, PatternOf<0xA1, 0xE8, 0x6F, 0x78, 0x0, 0xC3>(), {}, 12}},
    WidescreenByteContract{WidescreenContractSite::screen_height_int, {FeatureId::windowed_widescreen, "screen_height_int", VersionedOperationKind::inline_hook,
             0x52F30, 5, PatternOf<0xA1, 0xEC, 0x6F, 0x78, 0x0, 0xC3>(), {}, 13}},
    WidescreenByteContract{WidescreenContractSite::screen_width_float, {FeatureId::windowed_widescreen, "screen_width_float", VersionedOperationKind::inline_hook,
             0x52F40, 6, PatternOf<0xD9, 0x5, 0xF0, 0x6F, 0x78, 0x0, 0xC3>(), {}, 14}},
    WidescreenByteContract{WidescreenContractSite::screen_height_float, {FeatureId::windowed_widescreen, "screen_height_float", VersionedOperationKind::inline_hook,
             0x52F50, 6, PatternOf<0xD9, 0x5, 0xF4, 0x6F, 0x78, 0x0, 0xC3>(), {}, 15}},
    WidescreenByteContract{WidescreenContractSite::target_width_int, {FeatureId::windowed_widescreen, "target_width_int", VersionedOperationKind::inline_hook,
             0x52FA0, 5, PatternOf<0xA1, 0xF8, 0x6F, 0x78, 0x0, 0xC3>(), {}, 16}},
    WidescreenByteContract{WidescreenContractSite::target_height_int, {FeatureId::windowed_widescreen, "target_height_int", VersionedOperationKind::inline_hook,
             0x52FB0, 5, PatternOf<0xA1, 0xFC, 0x6F, 0x78, 0x0, 0xC3>(), {}, 17}},
    WidescreenByteContract{WidescreenContractSite::target_width_float, {FeatureId::windowed_widescreen, "target_width_float", VersionedOperationKind::inline_hook,
             0x52FC0, 6, PatternOf<0xD9, 0x5, 0x0, 0x70, 0x78, 0x0, 0xC3>(), {}, 18}},
    WidescreenByteContract{WidescreenContractSite::target_height_float, {FeatureId::windowed_widescreen, "target_height_float", VersionedOperationKind::inline_hook,
             0x52FD0, 6, PatternOf<0xD9, 0x5, 0x4, 0x70, 0x78, 0x0, 0xC3>(), {}, 19}},
    WidescreenByteContract{WidescreenContractSite::logical_resolution_set, {FeatureId::windowed_widescreen, "logical_resolution_set", VersionedOperationKind::inline_hook,
             0x53660, 7, PatternOf<0x6A, 0xFF, 0x68, 0xEB, 0xDA, 0x66, 0x0, 0x64, 0xA1, 0x0, 0x0, 0x0, 0x0, 0x50>(), {}, 2}},
    WidescreenByteContract{WidescreenContractSite::logical_target_width_set, {FeatureId::windowed_widescreen, "logical_target_width_set", VersionedOperationKind::inline_hook,
             0x52F60, 8, PatternOf<0xDB, 0x44, 0x24, 0x4, 0x8B, 0x44, 0x24, 0x4, 0xA3, 0xF8, 0x6F, 0x78, 0x0>(), {}, 3}},
    WidescreenByteContract{WidescreenContractSite::logical_target_height_set, {FeatureId::windowed_widescreen, "logical_target_height_set", VersionedOperationKind::inline_hook,
             0x52F80, 8, PatternOf<0xDB, 0x44, 0x24, 0x4, 0x8B, 0x44, 0x24, 0x4, 0xA3, 0xFC, 0x6F, 0x78, 0x0>(), {}, 4}},
    WidescreenByteContract{WidescreenContractSite::viewport_reset, {FeatureId::windowed_widescreen, "viewport_reset", VersionedOperationKind::inline_hook,
             0x53140, 6, PatternOf<0x8B, 0x4C, 0x24, 0x4, 0x33, 0xC0, 0x83, 0xEC, 0x20, 0x3B, 0xC8, 0xF>(), {}, 20}},
    WidescreenByteContract{WidescreenContractSite::mouse_debug_poll, {FeatureId::windowed_widescreen, "mouse_debug_poll", VersionedOperationKind::inline_hook,
             0xB06B0, 6, PatternOf<0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x8, 0x89, 0x4D, 0xF8, 0x8B, 0x45, 0xF8>(), {}, 21}},
    WidescreenByteContract{WidescreenContractSite::reset_pre, {FeatureId::windowed_widescreen, "reset_pre", VersionedOperationKind::mid_hook,
             0x5B28B, 7, PatternOf<0x83, 0xBE, 0x94, 0x0, 0x0, 0x0, 0x0>(), {}, 34}},
    WidescreenByteContract{WidescreenContractSite::reset_post, {FeatureId::windowed_widescreen, "reset_post", VersionedOperationKind::mid_hook,
             0x5B474, 8, PatternOf<0x83, 0xC4, 0x4, 0xB8, 0x1, 0x0, 0x0, 0x0>(), {}, 35}},
    WidescreenByteContract{WidescreenContractSite::gameplay_stage_background, {FeatureId::windowed_widescreen, "gameplay_stage_background", VersionedOperationKind::mid_hook,
             0x262FA0, 5, PatternOf<0xE8, 0x4B, 0x1A, 0xFE, 0xFF, 0x8B, 0x4D, 0xC4>(), {}, 22}},
    WidescreenByteContract{WidescreenContractSite::gameplay_track, {FeatureId::windowed_widescreen, "gameplay_track", VersionedOperationKind::mid_hook,
             0x262FA8, 5, PatternOf<0xE8, 0xD3, 0x56, 0xFE, 0xFF, 0x8B, 0x4D, 0xC4>(), {}, 23}},
    WidescreenByteContract{WidescreenContractSite::gameplay_effects, {FeatureId::windowed_widescreen, "gameplay_effects", VersionedOperationKind::mid_hook,
             0x263041, 5, PatternOf<0xE8, 0xFA, 0x5C, 0xFE, 0xFF, 0xE8, 0xD5, 0x0, 0xDF, 0xFF>(), {}, 24}},
    WidescreenByteContract{WidescreenContractSite::gameplay_effects_end, {FeatureId::windowed_widescreen, "gameplay_effects_end", VersionedOperationKind::mid_hook,
             0x263046, 5, PatternOf<0xE8, 0xD5, 0x0, 0xDF, 0xFF>(), {}, 25}},
    WidescreenByteContract{WidescreenContractSite::gameplay_hud_projection, {FeatureId::windowed_widescreen, "gameplay_hud_projection", VersionedOperationKind::mid_hook,
             0x23FDBA, 5, PatternOf<0xE8, 0xB1, 0xF3, 0xF9, 0xFF, 0x8B, 0xB5, 0x24, 0xFF, 0xFF, 0xFF, 0x81, 0xC6, 0xD0, 0x0, 0x0>(), {}, 26}},
    WidescreenByteContract{WidescreenContractSite::combo_begin, {FeatureId::windowed_widescreen, "combo_begin", VersionedOperationKind::mid_hook,
             0x1E4503, 5, PatternOf<0xE8, 0xA8, 0xD0, 0xFF, 0xFF>(), {}, 27}},
    WidescreenByteContract{WidescreenContractSite::combo_normal_digits, {FeatureId::windowed_widescreen, "combo_normal_digits", VersionedOperationKind::read_only_contract,
             0x1E4550, 5, PatternOf<0xE8, 0xB, 0x7B, 0xFE, 0xFF>(), {}, 0, SiteDisposition::verify_only}},
    WidescreenByteContract{WidescreenContractSite::combo_end, {FeatureId::windowed_widescreen, "combo_end", VersionedOperationKind::mid_hook,
             0x1E4B58, 6, PatternOf<0x8B, 0x55, 0xE4, 0x8B, 0x45, 0xE0, 0x89, 0x2, 0xE9, 0xD9, 0xF8, 0xFF, 0xFF>(), {}, 28}},
    WidescreenByteContract{WidescreenContractSite::gameplay_feedback_draw_begin, {FeatureId::windowed_widescreen, "gameplay_feedback_draw_begin", VersionedOperationKind::mid_hook,
             0x1F11E8, 5, PatternOf<0xE8, 0x83, 0xD, 0x0, 0x0>(), {}, 29}},
    WidescreenByteContract{WidescreenContractSite::gameplay_feedback_draw_end, {FeatureId::windowed_widescreen, "gameplay_feedback_draw_end", VersionedOperationKind::mid_hook,
             0x1F11ED, 6, PatternOf<0x8B, 0x4D, 0xF8, 0x8B, 0x51, 0xC, 0x81, 0xE2, 0x0, 0x40>(), {}, 30}},
    WidescreenByteContract{WidescreenContractSite::note_tutorial_group_begin, {FeatureId::windowed_widescreen, "note_tutorial_group_begin", VersionedOperationKind::mid_hook,
             0x24A2D5, 5, PatternOf<0xE8, 0xA6, 0x6E, 0xFA, 0xFF>(), {}, 31}},
    WidescreenByteContract{WidescreenContractSite::note_tutorial_group_end, {FeatureId::windowed_widescreen, "note_tutorial_group_end", VersionedOperationKind::mid_hook,
             0x24A2DA, 6, PatternOf<0xF, 0xB6, 0x55, 0x8, 0x85, 0xD2, 0x74, 0x1B>(), {}, 32}},
    WidescreenByteContract{WidescreenContractSite::test_mode_native_begin, {FeatureId::windowed_widescreen, "test_mode_native_begin", VersionedOperationKind::mid_hook,
             0x23AA89, 5, PatternOf<0xE8, 0xD2, 0xBB, 0xF3, 0xFF, 0xE8, 0x8D, 0x86, 0xE1, 0xFF>(), {}, 10}},
    WidescreenByteContract{WidescreenContractSite::test_mode_native_end, {FeatureId::windowed_widescreen, "test_mode_native_end", VersionedOperationKind::mid_hook,
             0x23AA8E, 5, PatternOf<0xE8, 0x8D, 0x86, 0xE1, 0xFF, 0x89, 0x85, 0x80, 0xFE, 0xFF, 0xFF, 0x8B, 0x8D>(), {}, 11}},
    WidescreenByteContract{WidescreenContractSite::clip_default, {FeatureId::windowed_widescreen, "clip_default", VersionedOperationKind::read_only_contract,
             0x2441C6, 4, PatternOf<0xC6, 0x45, 0xDF, 0x0>(), {}, 0, SiteDisposition::verify_only}},
    WidescreenByteContract{WidescreenContractSite::clip_gate, {FeatureId::windowed_widescreen, "clip_gate", VersionedOperationKind::mid_hook,
             0x2441CA, 6, PatternOf<0x8B, 0x95, 0x80, 0xFE, 0xFF, 0xFF, 0x8B, 0x82, 0x4C, 0x2, 0x0, 0x0, 0xF, 0xB6, 0x88, 0x5C, 0x1, 0x0, 0x0>(), {}, 33}},
    WidescreenByteContract{WidescreenContractSite::clip_continuation, {FeatureId::windowed_widescreen, "clip_continuation", VersionedOperationKind::read_only_contract,
             0x24422F, 10, PatternOf<0x8B, 0x4D, 0xD8, 0xE8, 0xC9, 0x18, 0xDC, 0xFF, 0xF, 0xB6>(), {}, 0, SiteDisposition::verify_only}},
    WidescreenByteContract{WidescreenContractSite::batch_flush, {FeatureId::windowed_widescreen, "batch_flush", VersionedOperationKind::read_only_contract,
             0x1C9B10, 12, PatternOf<0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x8, 0xC7, 0x45, 0xFC, 0x0, 0x0, 0x0>(), {}, 0, SiteDisposition::verify_only}},
    WidescreenByteContract{WidescreenContractSite::clip_owner, {FeatureId::windowed_widescreen, "clip_owner", VersionedOperationKind::read_only_contract,
             0x244000, 12, PatternOf<0x55, 0x8B, 0xEC, 0x81, 0xEC, 0xA0, 0x1, 0x0, 0x0, 0x56, 0x57, 0x89>(), {}, 0, SiteDisposition::verify_only}},
    WidescreenByteContract{WidescreenContractSite::live_frustum_helper, {FeatureId::windowed_widescreen, "live_frustum_helper", VersionedOperationKind::read_only_contract,
             0x243BE0, 12, PatternOf<0x55, 0x8B, 0xEC, 0x81, 0xEC, 0xC0, 0x0, 0x0, 0x0, 0x89, 0x8D, 0x58>(), {}, 0, SiteDisposition::verify_only}}
};
constexpr std::array<WidescreenPointerContract, 9> kPointers{{
    {WidescreenContractSite::config_width_setter, {FeatureId::windowed_widescreen, "config_width_setter", VersionedOperationKind::read_only_contract,
             0x2AE644, 4, PatternOf<0xC0, 0x9C, 0x45, 0x0>(), {}, 0, SiteDisposition::verify_only}, 0x59CC0},
    {WidescreenContractSite::config_height_setter, {FeatureId::windowed_widescreen, "config_height_setter", VersionedOperationKind::read_only_contract,
             0x2AE648, 4, PatternOf<0xE0, 0x9C, 0x45, 0x0>(), {}, 0, SiteDisposition::verify_only}, 0x59CE0},
    {WidescreenContractSite::config_resize_setter, {FeatureId::windowed_widescreen, "config_resize_setter", VersionedOperationKind::read_only_contract,
             0x2AE654, 4, PatternOf<0x20, 0x9D, 0x45, 0x0>(), {}, 0, SiteDisposition::verify_only}, 0x59D20},
    {WidescreenContractSite::config_minmax_setter, {FeatureId::windowed_widescreen, "config_minmax_setter", VersionedOperationKind::read_only_contract,
             0x2AE658, 4, PatternOf<0x40, 0x9D, 0x45, 0x0>(), {}, 0, SiteDisposition::verify_only}, 0x59D40},
    {WidescreenContractSite::config_mode_setter, {FeatureId::windowed_widescreen, "config_mode_setter", VersionedOperationKind::read_only_contract,
             0x2AE65C, 4, PatternOf<0x70, 0x9D, 0x45, 0x0>(), {}, 0, SiteDisposition::verify_only}, 0x59D70},
    {WidescreenContractSite::common_2d_render, {FeatureId::windowed_widescreen, "common_2d_render", VersionedOperationKind::read_only_contract,
             0x2F9B0C, 4, PatternOf<0x70, 0x56, 0x5F, 0x0>(), {}, 0, SiteDisposition::verify_only}, 0x1F5670},
    {WidescreenContractSite::network_status_movie_clip_accept, {FeatureId::windowed_widescreen, "network_status_movie_clip_accept", VersionedOperationKind::global_vtable_slot,
             0x2BE0E0, 4, PatternOf<0xD0, 0xC, 0x4E, 0x0>(), {}, 8}, 0xE0CD0},
    {WidescreenContractSite::network_status_shape_draw_visit, {FeatureId::windowed_widescreen, "network_status_shape_draw_visit", VersionedOperationKind::global_vtable_slot,
             0x2BB798, 4, PatternOf<0x80, 0xC8, 0x4C, 0x0>(), {}, 9}, 0xCC880},
    {WidescreenContractSite::common_3d_render, {FeatureId::windowed_widescreen, "common_3d_render", VersionedOperationKind::read_only_contract,
             0x2FB228, 4, PatternOf<0xB0, 0x84, 0x57, 0x0>(), {}, 0, SiteDisposition::verify_only}, 0x1784B0}
}};
constexpr std::array<WidescreenFunctionAbi, 36> kAbis{{
    {WidescreenContractSite::config_apply, WidescreenCallingConvention::cdecl_call, 1},
    {WidescreenContractSite::window_device_create, WidescreenCallingConvention::thiscall_call, 1},
    {WidescreenContractSite::frame_begin, WidescreenCallingConvention::thiscall_call, 1},
    {WidescreenContractSite::frame_end, WidescreenCallingConvention::thiscall_call, 1},
    {WidescreenContractSite::task_dispatch, WidescreenCallingConvention::thiscall_call, 1},
    {WidescreenContractSite::screen_width_int, WidescreenCallingConvention::cdecl_call, 0},
    {WidescreenContractSite::screen_height_int, WidescreenCallingConvention::cdecl_call, 0},
    {WidescreenContractSite::screen_width_float, WidescreenCallingConvention::cdecl_call, 0},
    {WidescreenContractSite::screen_height_float, WidescreenCallingConvention::cdecl_call, 0},
    {WidescreenContractSite::target_width_int, WidescreenCallingConvention::cdecl_call, 0},
    {WidescreenContractSite::target_height_int, WidescreenCallingConvention::cdecl_call, 0},
    {WidescreenContractSite::target_width_float, WidescreenCallingConvention::cdecl_call, 0},
    {WidescreenContractSite::target_height_float, WidescreenCallingConvention::cdecl_call, 0},
    {WidescreenContractSite::logical_resolution_set, WidescreenCallingConvention::cdecl_call, 2},
    {WidescreenContractSite::logical_target_width_set, WidescreenCallingConvention::cdecl_call, 1},
    {WidescreenContractSite::logical_target_height_set, WidescreenCallingConvention::cdecl_call, 1},
    {WidescreenContractSite::viewport_reset, WidescreenCallingConvention::cdecl_call, 1},
    {WidescreenContractSite::mouse_debug_poll, WidescreenCallingConvention::thiscall_call, 2},
    {WidescreenContractSite::reset_pre, WidescreenCallingConvention::mid_context, 1},
    {WidescreenContractSite::reset_post, WidescreenCallingConvention::mid_context, 1},
    {WidescreenContractSite::gameplay_stage_background, WidescreenCallingConvention::mid_context, 1},
    {WidescreenContractSite::gameplay_track, WidescreenCallingConvention::mid_context, 1},
    {WidescreenContractSite::gameplay_effects, WidescreenCallingConvention::mid_context, 1},
    {WidescreenContractSite::gameplay_effects_end, WidescreenCallingConvention::mid_context, 1},
    {WidescreenContractSite::gameplay_hud_projection, WidescreenCallingConvention::mid_context, 1},
    {WidescreenContractSite::combo_begin, WidescreenCallingConvention::mid_context, 1},
    {WidescreenContractSite::combo_end, WidescreenCallingConvention::mid_context, 1},
    {WidescreenContractSite::gameplay_feedback_draw_begin, WidescreenCallingConvention::mid_context, 1},
    {WidescreenContractSite::gameplay_feedback_draw_end, WidescreenCallingConvention::mid_context, 1},
    {WidescreenContractSite::note_tutorial_group_begin, WidescreenCallingConvention::mid_context, 1},
    {WidescreenContractSite::note_tutorial_group_end, WidescreenCallingConvention::mid_context, 1},
    {WidescreenContractSite::test_mode_native_begin, WidescreenCallingConvention::mid_context, 1},
    {WidescreenContractSite::test_mode_native_end, WidescreenCallingConvention::mid_context, 1},
    {WidescreenContractSite::clip_gate, WidescreenCallingConvention::mid_context, 1},
    {WidescreenContractSite::network_status_movie_clip_accept, WidescreenCallingConvention::thiscall_call, 2},
    {WidescreenContractSite::network_status_shape_draw_visit, WidescreenCallingConvention::thiscall_call, 2}
}};
constexpr std::array kOrder{
    WidescreenContractSite::config_apply,
    WidescreenContractSite::window_device_create,
    WidescreenContractSite::logical_resolution_set,
    WidescreenContractSite::logical_target_width_set,
    WidescreenContractSite::logical_target_height_set,
    WidescreenContractSite::frame_begin,
    WidescreenContractSite::frame_end,
    WidescreenContractSite::task_dispatch,
    WidescreenContractSite::network_status_movie_clip_accept,
    WidescreenContractSite::network_status_shape_draw_visit,
    WidescreenContractSite::test_mode_native_begin,
    WidescreenContractSite::test_mode_native_end,
    WidescreenContractSite::screen_width_int,
    WidescreenContractSite::screen_height_int,
    WidescreenContractSite::screen_width_float,
    WidescreenContractSite::screen_height_float,
    WidescreenContractSite::target_width_int,
    WidescreenContractSite::target_height_int,
    WidescreenContractSite::target_width_float,
    WidescreenContractSite::target_height_float,
    WidescreenContractSite::viewport_reset,
    WidescreenContractSite::mouse_debug_poll,
    WidescreenContractSite::gameplay_stage_background,
    WidescreenContractSite::gameplay_track,
    WidescreenContractSite::gameplay_effects,
    WidescreenContractSite::gameplay_effects_end,
    WidescreenContractSite::gameplay_hud_projection,
    WidescreenContractSite::combo_begin,
    WidescreenContractSite::combo_end,
    WidescreenContractSite::gameplay_feedback_draw_begin,
    WidescreenContractSite::gameplay_feedback_draw_end,
    WidescreenContractSite::note_tutorial_group_begin,
    WidescreenContractSite::note_tutorial_group_end,
    WidescreenContractSite::clip_gate,
    WidescreenContractSite::reset_pre,
    WidescreenContractSite::reset_post
};
constexpr auto CountBytes(VersionedOperationKind kind) {
    return std::ranges::count_if(kBytes, [kind](const auto& row) { return row.contract.kind == kind; });
}
static_assert(kBytes.size() == 40 && kPointers.size() == 9 && kAbis.size() == 36 && kOrder.size() == 36);
static_assert(CountBytes(VersionedOperationKind::inline_hook) == 18);
static_assert(CountBytes(VersionedOperationKind::mid_hook) == 16);
static_assert(CountBytes(VersionedOperationKind::read_only_contract) == 6);
static_assert(std::ranges::count_if(kPointers, [](const auto& row) {
    return row.contract.kind == VersionedOperationKind::global_vtable_slot;
}) == 2);
static_assert(std::ranges::count_if(kPointers, [](const auto& row) {
    return row.contract.kind == VersionedOperationKind::read_only_contract;
}) == 7);
constexpr WindowedWidescreenProfile Make(GameImageVariant variant) {
    return {GameBuild::groove_coaster_471, variant, kBytes, kPointers, kAbis, kOrder, {
        .main_config_vtable = 0x2AE62C,
        .batch_queue_pointer = 0x3F24FC,
        .movie_clip_draw_visitor_vtable = 0x2BB74C,
        .common_2d_vtable = 0x2F9AFC,
        .common_3d_vtable = 0x2FB218,
        .renderer_owner_device_offset = 0x8,
        .renderer_owner_window_offset = 0x8C,
        .renderer_owner_style_offset = 0x98,
        .fixed_decorated_window_style = 0xCA0000,
        .batch_queue_stride = 0x18,
        .batch_pending_count_offset = 0x18,
        .mouse_x_word = 0x0,
        .mouse_y_word = 0x1,
        .mouse_valid_word = 0x6,
        .movie_clip_name_offset = 0x120,
        .movie_clip_name_hash_offset = 0x140,
        .movie_clip_name_hash_multiplier = 0x21,
        .combo_entry_frame_offset = 0x14,
        .tune_effect_collection_offset = 0x1D6C,
        .pointer_collection_begin_offset = 0xC,
        .pointer_collection_end_offset = 0x10,
        .network_status_visitor_matrix_stack_offset = 0x1A0
    }};
}
}
const WindowedWidescreenProfile* ProfileFor(
    game_version::GameBuild build, game_version::GameImageVariant variant) noexcept {
    using namespace game_version;
    if (build != GameBuild::groove_coaster_471) return nullptr;
    static constexpr auto clean = Make(GameImageVariant::clean);
    static constexpr auto patched = Make(GameImageVariant::legacy_patched);
    static constexpr auto verified = Make(GameImageVariant::locally_verified);
    switch (variant) {
    case GameImageVariant::clean: return &clean;
    case GameImageVariant::legacy_patched: return &patched;
    case GameImageVariant::locally_verified: return &verified;
    }
    return nullptr;
}
std::expected<PreparedWidescreenPlan, game_version::PlanError> BuildWidescreenPlan(
    game_version::GameBuild build, game_version::GameImageVariant variant,
    bool enabled, const runtime_image::RuntimeImage& image) noexcept {
    using namespace game_version;
    PreparedWidescreenPlan result;
    if (!enabled) return result;
    const auto* profile = ProfileFor(build, variant);
    if (!profile) return std::unexpected(PlanError{.stage = PlanStage::unsupported_feature,
        .context = {build, variant}, .feature = FeatureId::windowed_widescreen, .site = "profile"});
    for (const auto site : profile->hook_order) {
        const auto byte = std::ranges::find(profile->byte_contracts, site, &WidescreenByteContract::site);
        if (byte != profile->byte_contracts.end()) {
            result.operations[result.count++] = detail::BindWidescreenHook(site, byte->contract);
            continue;
        }
        const auto pointer = std::ranges::find(profile->pointer_contracts, site, &WidescreenPointerContract::site);
        if (pointer == profile->pointer_contracts.end())
            return std::unexpected(PlanError{.stage = PlanStage::invalid_plan, .context = {build, variant},
                .feature = FeatureId::windowed_widescreen, .site = WidescreenContractSiteName(site)});
        const auto target = image.Resolve(
            {"WindowedWidescreen", pointer->contract.site, pointer->target_rva}, 1);
        if (!target) return std::unexpected(PlanError{.stage = PlanStage::address_range,
            .context = {build, variant}, .feature = FeatureId::windowed_widescreen,
            .site = pointer->contract.site, .rva = pointer->target_rva, .memory = target.error()});
        result.operations[result.count++] = detail::BindWidescreenHook(site, pointer->contract,
            reinterpret_cast<void*>(*target));
    }
    for (const auto& row : profile->byte_contracts)
        if (row.contract.kind == VersionedOperationKind::read_only_contract)
            result.operations[result.count++] = ReadOnlyContractOperation{row.contract};
    for (const auto& row : profile->pointer_contracts)
        if (row.contract.kind == VersionedOperationKind::read_only_contract)
            result.operations[result.count++] = ReadOnlyContractOperation{row.contract};
    return result;
}
}

#include "Patches/WindowedWidescreen/WindowedWidescreenAbi.h"

namespace gc::windowed_widescreen
{
    namespace
    {
        constexpr std::array kByteContracts{
            WidescreenByteContract{
                WidescreenContractSite::config_apply, 0x0023C360,
                BytePatternOf<0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x14, 0xE8,
                              0xE5, 0xF8, 0xFF, 0xFF>(),
                WidescreenHookKind::inline_hook
            },
            WidescreenByteContract{
                WidescreenContractSite::window_device_create, 0x0005B8A0,
                BytePatternOf<0x83, 0xEC, 0x64, 0x53, 0x55, 0x56, 0x57,
                              0x6A, 0x30, 0x33, 0xED, 0x8D>(),
                WidescreenHookKind::inline_hook
            },
            WidescreenByteContract{
                WidescreenContractSite::frame_begin, 0x0005AC70,
                BytePatternOf<0x51, 0x53, 0x56, 0x8D, 0x44, 0x24, 0x08,
                              0x57, 0x50, 0x8B, 0xF1, 0xE8>(),
                WidescreenHookKind::inline_hook
            },
            WidescreenByteContract{
                WidescreenContractSite::frame_end, 0x0005ACE0,
                BytePatternOf<0x8B, 0x41, 0x08, 0x8B, 0x08, 0x8B, 0x91,
                              0xA8, 0x00, 0x00, 0x00, 0x50>(),
                WidescreenHookKind::inline_hook
            },
            WidescreenByteContract{
                WidescreenContractSite::task_dispatch, 0x0005C1B0,
                BytePatternOf<0x8B, 0x09, 0x8B, 0x01, 0x8B, 0x50, 0x10,
                              0xFF, 0xE2, 0xCC, 0xCC, 0xCC>(),
                WidescreenHookKind::inline_hook
            },
            WidescreenByteContract{
                WidescreenContractSite::screen_width_int, 0x00052F20,
                BytePatternOf<0xA1, 0xE8, 0x6F, 0x78, 0x00, 0xC3>(),
                WidescreenHookKind::inline_hook
            },
            WidescreenByteContract{
                WidescreenContractSite::screen_height_int, 0x00052F30,
                BytePatternOf<0xA1, 0xEC, 0x6F, 0x78, 0x00, 0xC3>(),
                WidescreenHookKind::inline_hook
            },
            WidescreenByteContract{
                WidescreenContractSite::screen_width_float, 0x00052F40,
                BytePatternOf<0xD9, 0x05, 0xF0, 0x6F, 0x78, 0x00, 0xC3>(),
                WidescreenHookKind::inline_hook
            },
            WidescreenByteContract{
                WidescreenContractSite::screen_height_float, 0x00052F50,
                BytePatternOf<0xD9, 0x05, 0xF4, 0x6F, 0x78, 0x00, 0xC3>(),
                WidescreenHookKind::inline_hook
            },
            WidescreenByteContract{
                WidescreenContractSite::target_width_int, 0x00052FA0,
                BytePatternOf<0xA1, 0xF8, 0x6F, 0x78, 0x00, 0xC3>(),
                WidescreenHookKind::inline_hook
            },
            WidescreenByteContract{
                WidescreenContractSite::target_height_int, 0x00052FB0,
                BytePatternOf<0xA1, 0xFC, 0x6F, 0x78, 0x00, 0xC3>(),
                WidescreenHookKind::inline_hook
            },
            WidescreenByteContract{
                WidescreenContractSite::target_width_float, 0x00052FC0,
                BytePatternOf<0xD9, 0x05, 0x00, 0x70, 0x78, 0x00, 0xC3>(),
                WidescreenHookKind::inline_hook
            },
            WidescreenByteContract{
                WidescreenContractSite::target_height_float, 0x00052FD0,
                BytePatternOf<0xD9, 0x05, 0x04, 0x70, 0x78, 0x00, 0xC3>(),
                WidescreenHookKind::inline_hook
            },
            WidescreenByteContract{
                WidescreenContractSite::logical_resolution_set, 0x00053660,
                BytePatternOf<0x6A, 0xFF, 0x68, 0xEB, 0xDA, 0x66, 0x00,
                              0x64, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x50>(),
                WidescreenHookKind::inline_hook
            },
            WidescreenByteContract{
                WidescreenContractSite::logical_target_width_set, 0x00052F60,
                BytePatternOf<0xDB, 0x44, 0x24, 0x04, 0x8B, 0x44, 0x24,
                              0x04, 0xA3, 0xF8, 0x6F, 0x78, 0x00>(),
                WidescreenHookKind::inline_hook
            },
            WidescreenByteContract{
                WidescreenContractSite::logical_target_height_set, 0x00052F80,
                BytePatternOf<0xDB, 0x44, 0x24, 0x04, 0x8B, 0x44, 0x24,
                              0x04, 0xA3, 0xFC, 0x6F, 0x78, 0x00>(),
                WidescreenHookKind::inline_hook
            },
            WidescreenByteContract{
                WidescreenContractSite::viewport_reset, 0x00053140,
                BytePatternOf<0x8B, 0x4C, 0x24, 0x04, 0x33, 0xC0, 0x83,
                              0xEC, 0x20, 0x3B, 0xC8, 0x0F>(),
                WidescreenHookKind::inline_hook
            },
            WidescreenByteContract{
                WidescreenContractSite::mouse_debug_poll, 0x000B06B0,
                BytePatternOf<0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x08, 0x89,
                              0x4D, 0xF8, 0x8B, 0x45, 0xF8>(),
                WidescreenHookKind::inline_hook
            },
            WidescreenByteContract{
                WidescreenContractSite::reset_pre, 0x0005B28B,
                BytePatternOf<0x83, 0xBE, 0x94, 0x00, 0x00, 0x00, 0x00>(),
                WidescreenHookKind::mid_hook
            },
            WidescreenByteContract{
                WidescreenContractSite::reset_post, 0x0005B474,
                BytePatternOf<0x83, 0xC4, 0x04, 0xB8, 0x01, 0x00, 0x00,
                              0x00>(),
                WidescreenHookKind::mid_hook
            },
            WidescreenByteContract{
                WidescreenContractSite::gameplay_stage_background, 0x00262FA0,
                BytePatternOf<0xE8, 0x4B, 0x1A, 0xFE, 0xFF, 0x8B, 0x4D,
                              0xC4>(),
                WidescreenHookKind::mid_hook
            },
            WidescreenByteContract{
                WidescreenContractSite::gameplay_track, 0x00262FA8,
                BytePatternOf<0xE8, 0xD3, 0x56, 0xFE, 0xFF, 0x8B, 0x4D,
                              0xC4>(),
                WidescreenHookKind::mid_hook
            },
            WidescreenByteContract{
                WidescreenContractSite::gameplay_effects, 0x00263041,
                BytePatternOf<0xE8, 0xFA, 0x5C, 0xFE, 0xFF, 0xE8, 0xD5,
                              0x00, 0xDF, 0xFF>(),
                WidescreenHookKind::mid_hook
            },
            WidescreenByteContract{
                WidescreenContractSite::gameplay_effects_end, 0x00263046,
                BytePatternOf<0xE8, 0xD5, 0x00, 0xDF, 0xFF>(),
                WidescreenHookKind::mid_hook
            },
            WidescreenByteContract{
                WidescreenContractSite::gameplay_hud_projection, 0x0023FDBA,
                BytePatternOf<0xE8, 0xB1, 0xF3, 0xF9, 0xFF, 0x8B, 0xB5,
                              0x24, 0xFF, 0xFF, 0xFF, 0x81, 0xC6, 0xD0,
                              0x00, 0x00>(),
                WidescreenHookKind::mid_hook
            },
            WidescreenByteContract{
                WidescreenContractSite::combo_begin, 0x001E4503,
                BytePatternOf<0xE8, 0xA8, 0xD0, 0xFF, 0xFF>(),
                WidescreenHookKind::mid_hook
            },
            WidescreenByteContract{
                WidescreenContractSite::combo_normal_digits, 0x001E4550,
                BytePatternOf<0xE8, 0x0B, 0x7B, 0xFE, 0xFF>(),
                WidescreenHookKind::read_only
            },
            WidescreenByteContract{
                WidescreenContractSite::combo_end, 0x001E4B58,
                BytePatternOf<0x8B, 0x55, 0xE4, 0x8B, 0x45, 0xE0, 0x89,
                              0x02, 0xE9, 0xD9, 0xF8, 0xFF, 0xFF>(),
                WidescreenHookKind::mid_hook
            },
            WidescreenByteContract{
                WidescreenContractSite::gameplay_feedback_draw_begin,
                0x001F11E8,
                BytePatternOf<0xE8, 0x83, 0x0D, 0x00, 0x00>(),
                WidescreenHookKind::mid_hook
            },
            WidescreenByteContract{
                WidescreenContractSite::gameplay_feedback_draw_end,
                0x001F11ED,
                BytePatternOf<0x8B, 0x4D, 0xF8, 0x8B, 0x51, 0x0C, 0x81,
                              0xE2, 0x00, 0x40>(),
                WidescreenHookKind::mid_hook
            },
            WidescreenByteContract{
                WidescreenContractSite::note_tutorial_group_begin,
                0x0024A2D5,
                BytePatternOf<0xE8, 0xA6, 0x6E, 0xFA, 0xFF>(),
                WidescreenHookKind::mid_hook
            },
            WidescreenByteContract{
                WidescreenContractSite::note_tutorial_group_end,
                0x0024A2DA,
                BytePatternOf<0x0F, 0xB6, 0x55, 0x08, 0x85, 0xD2, 0x74,
                              0x1B>(),
                WidescreenHookKind::mid_hook
            },
            WidescreenByteContract{
                WidescreenContractSite::test_mode_native_begin,
                0x0023AA89,
                BytePatternOf<0xE8, 0xD2, 0xBB, 0xF3, 0xFF, 0xE8, 0x8D,
                              0x86, 0xE1, 0xFF>(),
                WidescreenHookKind::mid_hook
            },
            WidescreenByteContract{
                WidescreenContractSite::test_mode_native_end,
                0x0023AA8E,
                BytePatternOf<0xE8, 0x8D, 0x86, 0xE1, 0xFF, 0x89, 0x85,
                              0x80, 0xFE, 0xFF, 0xFF, 0x8B, 0x8D>(),
                WidescreenHookKind::mid_hook
            },
            WidescreenByteContract{
                WidescreenContractSite::clip_default, 0x002441C6,
                BytePatternOf<0xC6, 0x45, 0xDF, 0x00>(),
                WidescreenHookKind::read_only
            },
            WidescreenByteContract{
                WidescreenContractSite::clip_gate, 0x002441CA,
                BytePatternOf<0x8B, 0x95, 0x80, 0xFE, 0xFF, 0xFF, 0x8B,
                              0x82, 0x4C, 0x02, 0x00, 0x00, 0x0F, 0xB6,
                              0x88, 0x5C, 0x01, 0x00, 0x00>(),
                WidescreenHookKind::mid_hook
            },
            WidescreenByteContract{
                WidescreenContractSite::clip_continuation, 0x0024422F,
                BytePatternOf<0x8B, 0x4D, 0xD8, 0xE8, 0xC9, 0x18, 0xDC,
                              0xFF, 0x0F, 0xB6>(),
                WidescreenHookKind::read_only
            },
            WidescreenByteContract{
                WidescreenContractSite::batch_flush, 0x001C9B10,
                BytePatternOf<0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x08, 0xC7,
                              0x45, 0xFC, 0x00, 0x00, 0x00>(),
                WidescreenHookKind::read_only
            },
            WidescreenByteContract{
                WidescreenContractSite::clip_owner, 0x00244000,
                BytePatternOf<0x55, 0x8B, 0xEC, 0x81, 0xEC, 0xA0, 0x01,
                              0x00, 0x00, 0x56, 0x57, 0x89>(),
                WidescreenHookKind::read_only
            },
            WidescreenByteContract{
                WidescreenContractSite::live_frustum_helper, 0x00243BE0,
                BytePatternOf<0x55, 0x8B, 0xEC, 0x81, 0xEC, 0xC0, 0x00,
                              0x00, 0x00, 0x89, 0x8D, 0x58>(),
                WidescreenHookKind::read_only
            },
            WidescreenByteContract{
                WidescreenContractSite::network_status_movie_clip_accept,
                kMovieClipInstanceVtableRva +
                kMovieClipAcceptVtableOffset,
                BytePatternOf<0xD0, 0x0C, 0x4E, 0x00>(),
                WidescreenHookKind::vtable_hook
            },
            WidescreenByteContract{
                WidescreenContractSite::network_status_shape_draw_visit,
                kMovieClipDrawVisitorVtableRva +
                kMovieClipShapeDrawVtableOffset,
                BytePatternOf<0x80, 0xC8, 0x4C, 0x00>(),
                WidescreenHookKind::vtable_hook
            },
        };

        constexpr std::array kPointerContracts{
            WidescreenPointerContract{
                WidescreenContractSite::config_width_setter,
                0x002AE62C + 0x18, 0x00059CC0
            },
            WidescreenPointerContract{
                WidescreenContractSite::config_height_setter,
                0x002AE62C + 0x1C, 0x00059CE0
            },
            WidescreenPointerContract{
                WidescreenContractSite::config_resize_setter,
                0x002AE62C + 0x28, 0x00059D20
            },
            WidescreenPointerContract{
                WidescreenContractSite::config_minmax_setter,
                0x002AE62C + 0x2C, 0x00059D40
            },
            WidescreenPointerContract{
                WidescreenContractSite::config_mode_setter,
                0x002AE62C + 0x30, 0x00059D70
            },
            WidescreenPointerContract{
                WidescreenContractSite::common_2d_render,
                0x002F9AFC + 0x10, 0x001F5670
            },
            WidescreenPointerContract{
                WidescreenContractSite::network_status_movie_clip_accept,
                kMovieClipInstanceVtableRva +
                kMovieClipAcceptVtableOffset,
                kMovieClipAcceptTargetRva
            },
            WidescreenPointerContract{
                WidescreenContractSite::network_status_shape_draw_visit,
                kMovieClipDrawVisitorVtableRva +
                kMovieClipShapeDrawVtableOffset,
                kMovieClipShapeDrawTargetRva
            },
            WidescreenPointerContract{
                WidescreenContractSite::common_3d_render,
                0x002FB218 + 0x10, 0x001784B0
            },
        };

        constexpr std::array kFunctionAbis{
            WidescreenFunctionAbi{
                WidescreenContractSite::config_apply,
                WidescreenCallingConvention::cdecl_call, 1
            },
            WidescreenFunctionAbi{
                WidescreenContractSite::window_device_create,
                WidescreenCallingConvention::thiscall_call, 1
            },
            WidescreenFunctionAbi{
                WidescreenContractSite::frame_begin,
                WidescreenCallingConvention::thiscall_call, 1
            },
            WidescreenFunctionAbi{
                WidescreenContractSite::frame_end,
                WidescreenCallingConvention::thiscall_call, 1
            },
            WidescreenFunctionAbi{
                WidescreenContractSite::task_dispatch,
                WidescreenCallingConvention::thiscall_call, 1
            },
            WidescreenFunctionAbi{
                WidescreenContractSite::screen_width_int,
                WidescreenCallingConvention::cdecl_call, 0
            },
            WidescreenFunctionAbi{
                WidescreenContractSite::screen_height_int,
                WidescreenCallingConvention::cdecl_call, 0
            },
            WidescreenFunctionAbi{
                WidescreenContractSite::screen_width_float,
                WidescreenCallingConvention::cdecl_call, 0
            },
            WidescreenFunctionAbi{
                WidescreenContractSite::screen_height_float,
                WidescreenCallingConvention::cdecl_call, 0
            },
            WidescreenFunctionAbi{
                WidescreenContractSite::target_width_int,
                WidescreenCallingConvention::cdecl_call, 0
            },
            WidescreenFunctionAbi{
                WidescreenContractSite::target_height_int,
                WidescreenCallingConvention::cdecl_call, 0
            },
            WidescreenFunctionAbi{
                WidescreenContractSite::target_width_float,
                WidescreenCallingConvention::cdecl_call, 0
            },
            WidescreenFunctionAbi{
                WidescreenContractSite::target_height_float,
                WidescreenCallingConvention::cdecl_call, 0
            },
            WidescreenFunctionAbi{
                WidescreenContractSite::logical_resolution_set,
                WidescreenCallingConvention::cdecl_call, 2
            },
            WidescreenFunctionAbi{
                WidescreenContractSite::logical_target_width_set,
                WidescreenCallingConvention::cdecl_call, 1
            },
            WidescreenFunctionAbi{
                WidescreenContractSite::logical_target_height_set,
                WidescreenCallingConvention::cdecl_call, 1
            },
            WidescreenFunctionAbi{
                WidescreenContractSite::viewport_reset,
                WidescreenCallingConvention::cdecl_call, 1
            },
            WidescreenFunctionAbi{
                WidescreenContractSite::mouse_debug_poll,
                WidescreenCallingConvention::thiscall_call, 2
            },
            WidescreenFunctionAbi{
                WidescreenContractSite::reset_pre,
                WidescreenCallingConvention::mid_context, 1
            },
            WidescreenFunctionAbi{
                WidescreenContractSite::reset_post,
                WidescreenCallingConvention::mid_context, 1
            },
            WidescreenFunctionAbi{
                WidescreenContractSite::gameplay_stage_background,
                WidescreenCallingConvention::mid_context, 1
            },
            WidescreenFunctionAbi{
                WidescreenContractSite::gameplay_track,
                WidescreenCallingConvention::mid_context, 1
            },
            WidescreenFunctionAbi{
                WidescreenContractSite::gameplay_effects,
                WidescreenCallingConvention::mid_context, 1
            },
            WidescreenFunctionAbi{
                WidescreenContractSite::gameplay_effects_end,
                WidescreenCallingConvention::mid_context, 1
            },
            WidescreenFunctionAbi{
                WidescreenContractSite::gameplay_hud_projection,
                WidescreenCallingConvention::mid_context, 1
            },
            WidescreenFunctionAbi{
                WidescreenContractSite::combo_begin,
                WidescreenCallingConvention::mid_context, 1
            },
            WidescreenFunctionAbi{
                WidescreenContractSite::combo_end,
                WidescreenCallingConvention::mid_context, 1
            },
            WidescreenFunctionAbi{
                WidescreenContractSite::gameplay_feedback_draw_begin,
                WidescreenCallingConvention::mid_context, 1
            },
            WidescreenFunctionAbi{
                WidescreenContractSite::gameplay_feedback_draw_end,
                WidescreenCallingConvention::mid_context, 1
            },
            WidescreenFunctionAbi{
                WidescreenContractSite::note_tutorial_group_begin,
                WidescreenCallingConvention::mid_context, 1
            },
            WidescreenFunctionAbi{
                WidescreenContractSite::note_tutorial_group_end,
                WidescreenCallingConvention::mid_context, 1
            },
            WidescreenFunctionAbi{
                WidescreenContractSite::test_mode_native_begin,
                WidescreenCallingConvention::mid_context, 1
            },
            WidescreenFunctionAbi{
                WidescreenContractSite::test_mode_native_end,
                WidescreenCallingConvention::mid_context, 1
            },
            WidescreenFunctionAbi{
                WidescreenContractSite::clip_gate,
                WidescreenCallingConvention::mid_context, 1
            },
            WidescreenFunctionAbi{
                WidescreenContractSite::network_status_movie_clip_accept,
                WidescreenCallingConvention::thiscall_call, 2
            },
            WidescreenFunctionAbi{
                WidescreenContractSite::network_status_shape_draw_visit,
                WidescreenCallingConvention::thiscall_call, 2
            },
        };
    } // namespace

    std::span<const WidescreenByteContract>
    WindowedWidescreenByteContracts() noexcept
    {
        return kByteContracts;
    }

    std::span<const WidescreenPointerContract>
    WindowedWidescreenPointerContracts() noexcept
    {
        return kPointerContracts;
    }

    std::span<const WidescreenFunctionAbi>
    WindowedWidescreenFunctionAbis() noexcept
    {
        return kFunctionAbis;
    }

    const char* WidescreenContractSiteName(
        const WidescreenContractSite site) noexcept
    {
        switch (site)
        {
        case WidescreenContractSite::none: return "none";
        case WidescreenContractSite::config_apply: return "config_apply";
        case WidescreenContractSite::window_device_create: return "window_device_create";
        case WidescreenContractSite::frame_begin: return "frame_begin";
        case WidescreenContractSite::frame_end: return "frame_end";
        case WidescreenContractSite::task_dispatch: return "task_dispatch";
        case WidescreenContractSite::test_mode_native_begin: return "test_mode_native_begin";
        case WidescreenContractSite::test_mode_native_end: return "test_mode_native_end";
        case WidescreenContractSite::screen_width_int: return "screen_width_int";
        case WidescreenContractSite::screen_height_int: return "screen_height_int";
        case WidescreenContractSite::screen_width_float: return "screen_width_float";
        case WidescreenContractSite::screen_height_float: return "screen_height_float";
        case WidescreenContractSite::target_width_int: return "target_width_int";
        case WidescreenContractSite::target_height_int: return "target_height_int";
        case WidescreenContractSite::target_width_float: return "target_width_float";
        case WidescreenContractSite::target_height_float: return "target_height_float";
        case WidescreenContractSite::logical_resolution_set: return "logical_resolution_set";
        case WidescreenContractSite::logical_target_width_set: return "logical_target_width_set";
        case WidescreenContractSite::logical_target_height_set: return "logical_target_height_set";
        case WidescreenContractSite::viewport_reset: return "viewport_reset";
        case WidescreenContractSite::mouse_debug_poll: return "mouse_debug_poll";
        case WidescreenContractSite::reset_pre: return "reset_pre";
        case WidescreenContractSite::reset_post: return "reset_post";
        case WidescreenContractSite::gameplay_stage_background: return "gameplay_stage_background";
        case WidescreenContractSite::gameplay_track: return "gameplay_track";
        case WidescreenContractSite::gameplay_effects: return "gameplay_effects";
        case WidescreenContractSite::gameplay_effects_end: return "gameplay_effects_end";
        case WidescreenContractSite::gameplay_hud_projection: return "gameplay_hud_projection";
        case WidescreenContractSite::combo_begin: return "combo_begin";
        case WidescreenContractSite::combo_normal_digits: return "combo_normal_digits";
        case WidescreenContractSite::combo_end: return "combo_end";
        case WidescreenContractSite::gameplay_feedback_draw_begin: return "gameplay_feedback_draw_begin";
        case WidescreenContractSite::gameplay_feedback_draw_end: return "gameplay_feedback_draw_end";
        case WidescreenContractSite::note_tutorial_group_begin: return "note_tutorial_group_begin";
        case WidescreenContractSite::note_tutorial_group_end: return "note_tutorial_group_end";
        case WidescreenContractSite::clip_default: return "clip_default";
        case WidescreenContractSite::clip_gate: return "clip_gate";
        case WidescreenContractSite::clip_continuation: return "clip_continuation";
        case WidescreenContractSite::batch_flush: return "batch_flush";
        case WidescreenContractSite::clip_owner: return "clip_owner";
        case WidescreenContractSite::live_frustum_helper: return "live_frustum_helper";
        case WidescreenContractSite::config_width_setter: return "config_width_setter";
        case WidescreenContractSite::config_height_setter: return "config_height_setter";
        case WidescreenContractSite::config_resize_setter: return "config_resize_setter";
        case WidescreenContractSite::config_minmax_setter: return "config_minmax_setter";
        case WidescreenContractSite::config_mode_setter: return "config_mode_setter";
        case WidescreenContractSite::common_2d_render: return "common_2d_render";
        case WidescreenContractSite::network_status_movie_clip_accept: return "network_status_movie_clip_accept";
        case WidescreenContractSite::network_status_shape_draw_visit: return "network_status_shape_draw_visit";
        case WidescreenContractSite::common_3d_render: return "common_3d_render";
        }
        return "unknown";
    }
} // namespace gc::windowed_widescreen

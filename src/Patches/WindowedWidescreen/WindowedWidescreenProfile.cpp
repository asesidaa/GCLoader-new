#include "Patches/WindowedWidescreen/WindowHooks.h"
#include "Patches/WindowedWidescreen/GameplayHudHooks.h"
#include "Patches/WindowedWidescreen/NetworkStatusHooks.h"
#include "Patches/WindowedWidescreen/RenderHooks.h"
#include "Patches/WindowedWidescreen/WindowedWidescreenProfile.h"
#include <algorithm>
namespace gc::windowed_widescreen {
namespace {
using namespace game_version;
using namespace runtime_image;
constexpr std::array kBytes{
    WidescreenByteContract{WidescreenContractSite::bar_difficulty_a_begin, {FeatureId::windowed_widescreen, "bar_difficulty_a_begin", VersionedOperationKind::mid_hook,
             0x1E3F48, 5, PatternOf<0xE8, 0x33, 0x46, 0xF8, 0xFF>(), {}, 47}},
    WidescreenByteContract{WidescreenContractSite::bar_difficulty_a_end, {FeatureId::windowed_widescreen, "bar_difficulty_a_end", VersionedOperationKind::mid_hook,
             0x1E3F4D, 5, PatternOf<0x83, 0xC4, 0x18, 0xEB, 0x36>(), {}, 48}},
    WidescreenByteContract{WidescreenContractSite::bar_difficulty_b_begin, {FeatureId::windowed_widescreen, "bar_difficulty_b_begin", VersionedOperationKind::mid_hook,
             0x1E3F80, 5, PatternOf<0xE8, 0xFB, 0x45, 0xF8, 0xFF>(), {}, 49}},
    WidescreenByteContract{WidescreenContractSite::bar_difficulty_b_end, {FeatureId::windowed_widescreen, "bar_difficulty_b_end", VersionedOperationKind::mid_hook,
             0x1E3F88, 5, PatternOf<0xE8, 0x93, 0xF1, 0xE6, 0xFF>(), {}, 50}},
    WidescreenByteContract{WidescreenContractSite::bar_panel_480_begin, {FeatureId::windowed_widescreen, "bar_panel_480_begin", VersionedOperationKind::mid_hook,
             0x1E3FB0, 5, PatternOf<0xE8, 0xFB, 0xD5, 0xFF, 0xFF>(), {}, 51}},
    WidescreenByteContract{WidescreenContractSite::bar_panel_480_end, {FeatureId::windowed_widescreen, "bar_panel_480_end", VersionedOperationKind::mid_hook,
             0x1E3FB5, 5, PatternOf<0xE8, 0x16, 0xD2, 0xE1, 0xFF>(), {}, 52}},
    WidescreenByteContract{WidescreenContractSite::bar_panel_524_begin, {FeatureId::windowed_widescreen, "bar_panel_524_begin", VersionedOperationKind::mid_hook,
             0x1E3FD7, 5, PatternOf<0xE8, 0xD4, 0xD5, 0xFF, 0xFF>(), {}, 53}},
    WidescreenByteContract{WidescreenContractSite::bar_panel_524_end, {FeatureId::windowed_widescreen, "bar_panel_524_end", VersionedOperationKind::mid_hook,
             0x1E3FDC, 5, PatternOf<0xE8, 0x3F, 0xF1, 0xE6, 0xFF>(), {}, 54}},
    WidescreenByteContract{WidescreenContractSite::bar_panel_568_begin, {FeatureId::windowed_widescreen, "bar_panel_568_begin", VersionedOperationKind::mid_hook,
             0x1E4026, 5, PatternOf<0xE8, 0x85, 0xD5, 0xFF, 0xFF>(), {}, 55}},
    WidescreenByteContract{WidescreenContractSite::bar_panel_568_end, {FeatureId::windowed_widescreen, "bar_panel_568_end", VersionedOperationKind::mid_hook,
             0x1E402B, 5, PatternOf<0xE8, 0xA0, 0xD1, 0xE1, 0xFF>(), {}, 56}},
    WidescreenByteContract{WidescreenContractSite::bar_stage_panel_begin, {FeatureId::windowed_widescreen, "bar_stage_panel_begin", VersionedOperationKind::mid_hook,
             0x1E4063, 5, PatternOf<0xE8, 0x48, 0xD5, 0xFF, 0xFF>(), {}, 57}},
    WidescreenByteContract{WidescreenContractSite::bar_stage_panel_end, {FeatureId::windowed_widescreen, "bar_stage_panel_end", VersionedOperationKind::mid_hook,
             0x1E4068, 6, PatternOf<0x51, 0xD9, 0xE8, 0xD9, 0x1C, 0x24>(), {}, 58}},
    WidescreenByteContract{WidescreenContractSite::bar_stage_current_begin, {FeatureId::windowed_widescreen, "bar_stage_current_begin", VersionedOperationKind::mid_hook,
             0x1E40D0, 5, PatternOf<0xE8, 0x8B, 0x7F, 0xFE, 0xFF>(), {}, 59}},
    WidescreenByteContract{WidescreenContractSite::bar_stage_current_end, {FeatureId::windowed_widescreen, "bar_stage_current_end", VersionedOperationKind::mid_hook,
             0x1E40D5, 6, PatternOf<0x83, 0xC4, 0x20, 0x51, 0xD9, 0xE8>(), {}, 60}},
    WidescreenByteContract{WidescreenContractSite::bar_stage_total_begin, {FeatureId::windowed_widescreen, "bar_stage_total_begin", VersionedOperationKind::mid_hook,
             0x1E412D, 5, PatternOf<0xE8, 0x2E, 0x7F, 0xFE, 0xFF>(), {}, 61}},
    WidescreenByteContract{WidescreenContractSite::bar_stage_total_end, {FeatureId::windowed_widescreen, "bar_stage_total_end", VersionedOperationKind::mid_hook,
             0x1E4135, 5, PatternOf<0xE8, 0x96, 0xD0, 0xE1, 0xFF>(), {}, 62}},
    WidescreenByteContract{WidescreenContractSite::bar_gauge_begin, {FeatureId::windowed_widescreen, "bar_gauge_begin", VersionedOperationKind::mid_hook,
             0x1E41AB, 5, PatternOf<0xE8, 0x90, 0x8C, 0xFE, 0xFF>(), {}, 63}},
    WidescreenByteContract{WidescreenContractSite::bar_gauge_end, {FeatureId::windowed_widescreen, "bar_gauge_end", VersionedOperationKind::mid_hook,
             0x1E41B3, 5, PatternOf<0xE8, 0x18, 0xD0, 0xE1, 0xFF>(), {}, 64}},
    WidescreenByteContract{WidescreenContractSite::bar_panel_216_begin, {FeatureId::windowed_widescreen, "bar_panel_216_begin", VersionedOperationKind::mid_hook,
             0x1E41E3, 5, PatternOf<0xE8, 0xC8, 0xD3, 0xFF, 0xFF>(), {}, 65}},
    WidescreenByteContract{WidescreenContractSite::bar_panel_216_end, {FeatureId::windowed_widescreen, "bar_panel_216_end", VersionedOperationKind::mid_hook,
             0x1E41E8, 5, PatternOf<0xE8, 0xE3, 0xCF, 0xE1, 0xFF>(), {}, 66}},
    WidescreenByteContract{WidescreenContractSite::bar_score_panel_begin, {FeatureId::windowed_widescreen, "bar_score_panel_begin", VersionedOperationKind::mid_hook,
             0x1E420A, 5, PatternOf<0xE8, 0xA1, 0xD3, 0xFF, 0xFF>(), {}, 67}},
    WidescreenByteContract{WidescreenContractSite::bar_score_panel_end, {FeatureId::windowed_widescreen, "bar_score_panel_end", VersionedOperationKind::mid_hook,
             0x1E420F, 6, PatternOf<0x51, 0xD9, 0xE8, 0xD9, 0x1C, 0x24>(), {}, 68}},
    WidescreenByteContract{WidescreenContractSite::bar_score_digits_begin, {FeatureId::windowed_widescreen, "bar_score_digits_begin", VersionedOperationKind::mid_hook,
             0x1E4261, 5, PatternOf<0xE8, 0xFA, 0x7D, 0xFE, 0xFF>(), {}, 69}},
    WidescreenByteContract{WidescreenContractSite::bar_score_digits_end, {FeatureId::windowed_widescreen, "bar_score_digits_end", VersionedOperationKind::mid_hook,
             0x1E4269, 5, PatternOf<0xE8, 0x62, 0xCF, 0xE1, 0xFF>(), {}, 70}},
    WidescreenByteContract{WidescreenContractSite::bar_extra_panel_a_begin, {FeatureId::windowed_widescreen, "bar_extra_panel_a_begin", VersionedOperationKind::mid_hook,
             0x1E4299, 5, PatternOf<0xE8, 0x12, 0xD3, 0xFF, 0xFF>(), {}, 71}},
    WidescreenByteContract{WidescreenContractSite::bar_extra_panel_a_end, {FeatureId::windowed_widescreen, "bar_extra_panel_a_end", VersionedOperationKind::mid_hook,
             0x1E429E, 6, PatternOf<0x51, 0xD9, 0xE8, 0xD9, 0x1C, 0x24>(), {}, 72}},
    WidescreenByteContract{WidescreenContractSite::bar_extra_digits_a_begin, {FeatureId::windowed_widescreen, "bar_extra_digits_a_begin", VersionedOperationKind::mid_hook,
             0x1E42EA, 5, PatternOf<0xE8, 0x71, 0x7D, 0xFE, 0xFF>(), {}, 73}},
    WidescreenByteContract{WidescreenContractSite::bar_extra_digits_a_end, {FeatureId::windowed_widescreen, "bar_extra_digits_a_end", VersionedOperationKind::mid_hook,
             0x1E42F2, 5, PatternOf<0xE8, 0xD9, 0xCE, 0xE1, 0xFF>(), {}, 74}},
    WidescreenByteContract{WidescreenContractSite::bar_extra_panel_b_begin, {FeatureId::windowed_widescreen, "bar_extra_panel_b_begin", VersionedOperationKind::mid_hook,
             0x1E4314, 5, PatternOf<0xE8, 0x97, 0xD2, 0xFF, 0xFF>(), {}, 75}},
    WidescreenByteContract{WidescreenContractSite::bar_extra_panel_b_end, {FeatureId::windowed_widescreen, "bar_extra_panel_b_end", VersionedOperationKind::mid_hook,
             0x1E4319, 6, PatternOf<0x51, 0xD9, 0xE8, 0xD9, 0x1C, 0x24>(), {}, 76}},
    WidescreenByteContract{WidescreenContractSite::bar_extra_digits_b_begin, {FeatureId::windowed_widescreen, "bar_extra_digits_b_begin", VersionedOperationKind::mid_hook,
             0x1E4365, 5, PatternOf<0xE8, 0xF6, 0x7C, 0xFE, 0xFF>(), {}, 77}},
    WidescreenByteContract{WidescreenContractSite::bar_extra_digits_b_end, {FeatureId::windowed_widescreen, "bar_extra_digits_b_end", VersionedOperationKind::mid_hook,
             0x1E436D, 5, PatternOf<0xE8, 0x5E, 0xCE, 0xE1, 0xFF>(), {}, 78}},
    WidescreenByteContract{WidescreenContractSite::bar_mode_panel_begin, {FeatureId::windowed_widescreen, "bar_mode_panel_begin", VersionedOperationKind::mid_hook,
             0x1E4B99, 5, PatternOf<0xE8, 0x12, 0xCA, 0xFF, 0xFF>(), {}, 79}},
    WidescreenByteContract{WidescreenContractSite::bar_mode_panel_end, {FeatureId::windowed_widescreen, "bar_mode_panel_end", VersionedOperationKind::mid_hook,
             0x1E4B9E, 5, PatternOf<0xE8, 0x2D, 0xC6, 0xE1, 0xFF>(), {}, 80}},
    WidescreenByteContract{WidescreenContractSite::bar_player_panel_begin, {FeatureId::windowed_widescreen, "bar_player_panel_begin", VersionedOperationKind::mid_hook,
             0x1E4BDF, 5, PatternOf<0xE8, 0xCC, 0xC9, 0xFF, 0xFF>(), {}, 81}},
    WidescreenByteContract{WidescreenContractSite::bar_player_panel_end, {FeatureId::windowed_widescreen, "bar_player_panel_end", VersionedOperationKind::mid_hook,
             0x1E4BE4, 5, PatternOf<0xE8, 0xE7, 0xC5, 0xE1, 0xFF>(), {}, 82}},
    WidescreenByteContract{WidescreenContractSite::bar_status_panel_begin, {FeatureId::windowed_widescreen, "bar_status_panel_begin", VersionedOperationKind::mid_hook,
             0x1E4C36, 5, PatternOf<0xE8, 0x75, 0xC9, 0xFF, 0xFF>(), {}, 83}},
    WidescreenByteContract{WidescreenContractSite::bar_status_panel_end, {FeatureId::windowed_widescreen, "bar_status_panel_end", VersionedOperationKind::mid_hook,
             0x1E4C3B, 5, PatternOf<0xE8, 0xE0, 0xE4, 0xE6, 0xFF>(), {}, 84}},
    WidescreenByteContract{WidescreenContractSite::bar_names_end, {FeatureId::windowed_widescreen, "bar_names_end", VersionedOperationKind::mid_hook,
             0x24A284, 6, PatternOf<0x8B, 0x85, 0x54, 0xFE, 0xFF, 0xFF>(), {}, 85}},
    WidescreenByteContract{WidescreenContractSite::chain_label_end, {FeatureId::windowed_widescreen, "chain_label_end", VersionedOperationKind::mid_hook,
             0x1E4508, 7, PatternOf<0x51, 0xD9, 0x45, 0xD8, 0xD9, 0x1C, 0x24>(), {}, 86}},
    WidescreenByteContract{WidescreenContractSite::chain_digits_end, {FeatureId::windowed_widescreen, "chain_digits_end", VersionedOperationKind::mid_hook,
             0x1E4555, 10, PatternOf<0x83, 0xC4, 0x20, 0xC7, 0x45, 0xCC, 0x0, 0x0, 0x0, 0x0>(), {}, 87}},
    WidescreenByteContract{WidescreenContractSite::chain_glow_end, {FeatureId::windowed_widescreen, "chain_glow_end", VersionedOperationKind::mid_hook,
             0x1E4611, 5, PatternOf<0xE9, 0x4B, 0xFF, 0xFF, 0xFF>(), {}, 88}},
    WidescreenByteContract{WidescreenContractSite::hundred_digits_end, {FeatureId::windowed_widescreen, "hundred_digits_end", VersionedOperationKind::mid_hook,
             0x1E4767, 8, PatternOf<0x83, 0xC4, 0x20, 0xE8, 0xB1, 0xE9, 0xE6, 0xFF>(), {}, 89}},
    WidescreenByteContract{WidescreenContractSite::effect_packet_end, {FeatureId::windowed_widescreen, "effect_packet_end", VersionedOperationKind::mid_hook,
             0x1F10C9, 10, PatternOf<0x8B, 0x4D, 0xAC, 0xC7, 0x41, 0x70, 0x0, 0x0, 0x0, 0x0>(), {}, 90}},
    WidescreenByteContract{WidescreenContractSite::bar_names_begin, {FeatureId::windowed_widescreen, "bar_names_begin", VersionedOperationKind::mid_hook,
             0x24A27F, 5, PatternOf<0xE8, 0x1C, 0x7D, 0xF9, 0xFF>(), {}, 38}},
    WidescreenByteContract{WidescreenContractSite::chain_glow_begin, {FeatureId::windowed_widescreen, "chain_glow_begin", VersionedOperationKind::mid_hook,
             0x1E4609, 5, PatternOf<0xE8, 0x52, 0x7A, 0xFE, 0xFF>(), {}, 41}},
    WidescreenByteContract{WidescreenContractSite::hundred_digits_begin, {FeatureId::windowed_widescreen, "hundred_digits_begin", VersionedOperationKind::mid_hook,
             0x1E4762, 5, PatternOf<0xE8, 0xF9, 0x78, 0xFE, 0xFF>(), {}, 42}},
    WidescreenByteContract{WidescreenContractSite::effect_packet_allocated, {FeatureId::windowed_widescreen, "effect_packet_allocated", VersionedOperationKind::mid_hook,
             0x1F0BB0, 7, PatternOf<0x89, 0x45, 0xF4, 0x83, 0x7D, 0xF4, 0x0>(), {}, 44}},
    WidescreenByteContract{WidescreenContractSite::effect_packet_begin, {FeatureId::windowed_widescreen, "effect_packet_begin", VersionedOperationKind::mid_hook,
             0x1F10C4, 5, PatternOf<0xE8, 0x37, 0xF5, 0xFF, 0xFF>(), {}, 45}},
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
    WidescreenByteContract{WidescreenContractSite::chain_label_begin, {FeatureId::windowed_widescreen, "chain_label_begin", VersionedOperationKind::mid_hook,
             0x1E4503, 5, PatternOf<0xE8, 0xA8, 0xD0, 0xFF, 0xFF>(), {}, 27}},
    WidescreenByteContract{WidescreenContractSite::chain_digits_begin, {FeatureId::windowed_widescreen, "chain_digits_begin", VersionedOperationKind::mid_hook,
             0x1E4550, 5, PatternOf<0xE8, 0xB, 0x7B, 0xFE, 0xFF>(), {}, 36}},
    WidescreenByteContract{WidescreenContractSite::gameplay_feedback_draw_begin, {FeatureId::windowed_widescreen, "gameplay_feedback_draw_begin", VersionedOperationKind::mid_hook,
             0x1F11E8, 5, PatternOf<0xE8, 0x83, 0xD, 0x0, 0x0>(), {}, 29}},
    WidescreenByteContract{WidescreenContractSite::gameplay_feedback_draw_end, {FeatureId::windowed_widescreen, "gameplay_feedback_draw_end", VersionedOperationKind::mid_hook,
             0x1F11ED, 6, PatternOf<0x8B, 0x4D, 0xF8, 0x8B, 0x51, 0xC, 0x81, 0xE2, 0x0, 0x40>(), {}, 30}},
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
constexpr std::array<WidescreenFunctionAbi, 83> kAbis{{
    {WidescreenContractSite::bar_difficulty_a_begin, WidescreenCallingConvention::mid_context, 1},
    {WidescreenContractSite::bar_difficulty_a_end, WidescreenCallingConvention::mid_context, 1},
    {WidescreenContractSite::bar_difficulty_b_begin, WidescreenCallingConvention::mid_context, 1},
    {WidescreenContractSite::bar_difficulty_b_end, WidescreenCallingConvention::mid_context, 1},
    {WidescreenContractSite::bar_panel_480_begin, WidescreenCallingConvention::mid_context, 1},
    {WidescreenContractSite::bar_panel_480_end, WidescreenCallingConvention::mid_context, 1},
    {WidescreenContractSite::bar_panel_524_begin, WidescreenCallingConvention::mid_context, 1},
    {WidescreenContractSite::bar_panel_524_end, WidescreenCallingConvention::mid_context, 1},
    {WidescreenContractSite::bar_panel_568_begin, WidescreenCallingConvention::mid_context, 1},
    {WidescreenContractSite::bar_panel_568_end, WidescreenCallingConvention::mid_context, 1},
    {WidescreenContractSite::bar_stage_panel_begin, WidescreenCallingConvention::mid_context, 1},
    {WidescreenContractSite::bar_stage_panel_end, WidescreenCallingConvention::mid_context, 1},
    {WidescreenContractSite::bar_stage_current_begin, WidescreenCallingConvention::mid_context, 1},
    {WidescreenContractSite::bar_stage_current_end, WidescreenCallingConvention::mid_context, 1},
    {WidescreenContractSite::bar_stage_total_begin, WidescreenCallingConvention::mid_context, 1},
    {WidescreenContractSite::bar_stage_total_end, WidescreenCallingConvention::mid_context, 1},
    {WidescreenContractSite::bar_gauge_begin, WidescreenCallingConvention::mid_context, 1},
    {WidescreenContractSite::bar_gauge_end, WidescreenCallingConvention::mid_context, 1},
    {WidescreenContractSite::bar_panel_216_begin, WidescreenCallingConvention::mid_context, 1},
    {WidescreenContractSite::bar_panel_216_end, WidescreenCallingConvention::mid_context, 1},
    {WidescreenContractSite::bar_score_panel_begin, WidescreenCallingConvention::mid_context, 1},
    {WidescreenContractSite::bar_score_panel_end, WidescreenCallingConvention::mid_context, 1},
    {WidescreenContractSite::bar_score_digits_begin, WidescreenCallingConvention::mid_context, 1},
    {WidescreenContractSite::bar_score_digits_end, WidescreenCallingConvention::mid_context, 1},
    {WidescreenContractSite::bar_extra_panel_a_begin, WidescreenCallingConvention::mid_context, 1},
    {WidescreenContractSite::bar_extra_panel_a_end, WidescreenCallingConvention::mid_context, 1},
    {WidescreenContractSite::bar_extra_digits_a_begin, WidescreenCallingConvention::mid_context, 1},
    {WidescreenContractSite::bar_extra_digits_a_end, WidescreenCallingConvention::mid_context, 1},
    {WidescreenContractSite::bar_extra_panel_b_begin, WidescreenCallingConvention::mid_context, 1},
    {WidescreenContractSite::bar_extra_panel_b_end, WidescreenCallingConvention::mid_context, 1},
    {WidescreenContractSite::bar_extra_digits_b_begin, WidescreenCallingConvention::mid_context, 1},
    {WidescreenContractSite::bar_extra_digits_b_end, WidescreenCallingConvention::mid_context, 1},
    {WidescreenContractSite::bar_mode_panel_begin, WidescreenCallingConvention::mid_context, 1},
    {WidescreenContractSite::bar_mode_panel_end, WidescreenCallingConvention::mid_context, 1},
    {WidescreenContractSite::bar_player_panel_begin, WidescreenCallingConvention::mid_context, 1},
    {WidescreenContractSite::bar_player_panel_end, WidescreenCallingConvention::mid_context, 1},
    {WidescreenContractSite::bar_status_panel_begin, WidescreenCallingConvention::mid_context, 1},
    {WidescreenContractSite::bar_status_panel_end, WidescreenCallingConvention::mid_context, 1},
    {WidescreenContractSite::bar_names_end, WidescreenCallingConvention::mid_context, 1},
    {WidescreenContractSite::chain_label_end, WidescreenCallingConvention::mid_context, 1},
    {WidescreenContractSite::chain_digits_end, WidescreenCallingConvention::mid_context, 1},
    {WidescreenContractSite::chain_glow_end, WidescreenCallingConvention::mid_context, 1},
    {WidescreenContractSite::hundred_digits_end, WidescreenCallingConvention::mid_context, 1},
    {WidescreenContractSite::effect_packet_end, WidescreenCallingConvention::mid_context, 1},
    {WidescreenContractSite::chain_digits_begin, WidescreenCallingConvention::mid_context, 1},
    {WidescreenContractSite::bar_names_begin, WidescreenCallingConvention::mid_context, 1},
    {WidescreenContractSite::chain_glow_begin, WidescreenCallingConvention::mid_context, 1},
    {WidescreenContractSite::hundred_digits_begin, WidescreenCallingConvention::mid_context, 1},
    {WidescreenContractSite::effect_packet_allocated, WidescreenCallingConvention::mid_context, 1},
    {WidescreenContractSite::effect_packet_begin, WidescreenCallingConvention::mid_context, 1},
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
    {WidescreenContractSite::chain_label_begin, WidescreenCallingConvention::mid_context, 1},
    {WidescreenContractSite::gameplay_feedback_draw_begin, WidescreenCallingConvention::mid_context, 1},
    {WidescreenContractSite::gameplay_feedback_draw_end, WidescreenCallingConvention::mid_context, 1},
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
    WidescreenContractSite::chain_label_begin,
    WidescreenContractSite::gameplay_feedback_draw_begin,
    WidescreenContractSite::gameplay_feedback_draw_end,
    WidescreenContractSite::clip_gate,
    WidescreenContractSite::reset_pre,
    WidescreenContractSite::reset_post,
    WidescreenContractSite::chain_digits_begin,
    WidescreenContractSite::bar_names_begin,
    WidescreenContractSite::chain_glow_begin,
    WidescreenContractSite::hundred_digits_begin,
    WidescreenContractSite::effect_packet_allocated,
    WidescreenContractSite::effect_packet_begin,
    WidescreenContractSite::bar_difficulty_a_begin,
    WidescreenContractSite::bar_difficulty_a_end,
    WidescreenContractSite::bar_difficulty_b_begin,
    WidescreenContractSite::bar_difficulty_b_end,
    WidescreenContractSite::bar_panel_480_begin,
    WidescreenContractSite::bar_panel_480_end,
    WidescreenContractSite::bar_panel_524_begin,
    WidescreenContractSite::bar_panel_524_end,
    WidescreenContractSite::bar_panel_568_begin,
    WidescreenContractSite::bar_panel_568_end,
    WidescreenContractSite::bar_stage_panel_begin,
    WidescreenContractSite::bar_stage_panel_end,
    WidescreenContractSite::bar_stage_current_begin,
    WidescreenContractSite::bar_stage_current_end,
    WidescreenContractSite::bar_stage_total_begin,
    WidescreenContractSite::bar_stage_total_end,
    WidescreenContractSite::bar_gauge_begin,
    WidescreenContractSite::bar_gauge_end,
    WidescreenContractSite::bar_panel_216_begin,
    WidescreenContractSite::bar_panel_216_end,
    WidescreenContractSite::bar_score_panel_begin,
    WidescreenContractSite::bar_score_panel_end,
    WidescreenContractSite::bar_score_digits_begin,
    WidescreenContractSite::bar_score_digits_end,
    WidescreenContractSite::bar_extra_panel_a_begin,
    WidescreenContractSite::bar_extra_panel_a_end,
    WidescreenContractSite::bar_extra_digits_a_begin,
    WidescreenContractSite::bar_extra_digits_a_end,
    WidescreenContractSite::bar_extra_panel_b_begin,
    WidescreenContractSite::bar_extra_panel_b_end,
    WidescreenContractSite::bar_extra_digits_b_begin,
    WidescreenContractSite::bar_extra_digits_b_end,
    WidescreenContractSite::bar_mode_panel_begin,
    WidescreenContractSite::bar_mode_panel_end,
    WidescreenContractSite::bar_player_panel_begin,
    WidescreenContractSite::bar_player_panel_end,
    WidescreenContractSite::bar_status_panel_begin,
    WidescreenContractSite::bar_status_panel_end,
    WidescreenContractSite::bar_names_end,
    WidescreenContractSite::chain_label_end,
    WidescreenContractSite::chain_digits_end,
    WidescreenContractSite::chain_glow_end,
    WidescreenContractSite::hundred_digits_end,
    WidescreenContractSite::effect_packet_end,
};
constexpr auto CountBytes(VersionedOperationKind kind) {
    return std::ranges::count_if(kBytes, [kind](const auto& row) { return row.contract.kind == kind; });
}
static_assert(kBytes.size() == 86 && kPointers.size() == 9 && kAbis.size() == 83 && kOrder.size() == 83);
static_assert(CountBytes(VersionedOperationKind::inline_hook) == 18);
static_assert(CountBytes(VersionedOperationKind::mid_hook) == 63);
static_assert(CountBytes(VersionedOperationKind::read_only_contract) == 5);
static_assert(std::ranges::count_if(kPointers, [](const auto& row) {
    return row.contract.kind == VersionedOperationKind::global_vtable_slot;
}) == 2);
static_assert(std::ranges::count_if(kPointers, [](const auto& row) {
    return row.contract.kind == VersionedOperationKind::read_only_contract;
}) == 7);
// 2.06: selected draw boundaries and native ownership rechecked in IDA.
constexpr std::array kBytes206{
    WidescreenByteContract{WidescreenContractSite::bar_difficulty_a_begin, {FeatureId::windowed_widescreen, "bar_difficulty_a_begin", VersionedOperationKind::mid_hook, 0x1BD6E8, 5, PatternOf<0xE8, 0xC3, 0x61, 0xF9, 0xFF>(), {}, 47}},
    WidescreenByteContract{WidescreenContractSite::bar_difficulty_a_end, {FeatureId::windowed_widescreen, "bar_difficulty_a_end", VersionedOperationKind::mid_hook, 0x1BD6ED, 5, PatternOf<0x83, 0xC4, 0x18, 0xEB, 0x36>(), {}, 48}},
    WidescreenByteContract{WidescreenContractSite::bar_difficulty_b_begin, {FeatureId::windowed_widescreen, "bar_difficulty_b_begin", VersionedOperationKind::mid_hook, 0x1BD720, 5, PatternOf<0xE8, 0x8B, 0x61, 0xF9, 0xFF>(), {}, 49}},
    WidescreenByteContract{WidescreenContractSite::bar_difficulty_b_end, {FeatureId::windowed_widescreen, "bar_difficulty_b_end", VersionedOperationKind::mid_hook, 0x1BD728, 5, PatternOf<0xE8, 0x53, 0x6C, 0xE8, 0xFF>(), {}, 50}},
    WidescreenByteContract{WidescreenContractSite::bar_panel_480_begin, {FeatureId::windowed_widescreen, "bar_panel_480_begin", VersionedOperationKind::mid_hook, 0x1BD750, 5, PatternOf<0xE8, 0xDB, 0xCA, 0xFF, 0xFF>(), {}, 51}},
    WidescreenByteContract{WidescreenContractSite::bar_panel_480_end, {FeatureId::windowed_widescreen, "bar_panel_480_end", VersionedOperationKind::mid_hook, 0x1BD755, 5, PatternOf<0xE8, 0x56, 0x3A, 0xE4, 0xFF>(), {}, 52}},
    WidescreenByteContract{WidescreenContractSite::bar_panel_524_begin, {FeatureId::windowed_widescreen, "bar_panel_524_begin", VersionedOperationKind::mid_hook, 0x1BD777, 5, PatternOf<0xE8, 0xB4, 0xCA, 0xFF, 0xFF>(), {}, 53}},
    WidescreenByteContract{WidescreenContractSite::bar_panel_524_end, {FeatureId::windowed_widescreen, "bar_panel_524_end", VersionedOperationKind::mid_hook, 0x1BD77C, 5, PatternOf<0xE8, 0xFF, 0x6B, 0xE8, 0xFF>(), {}, 54}},
    WidescreenByteContract{WidescreenContractSite::bar_panel_568_begin, {FeatureId::windowed_widescreen, "bar_panel_568_begin", VersionedOperationKind::mid_hook, 0x1BD7C6, 5, PatternOf<0xE8, 0x65, 0xCA, 0xFF, 0xFF>(), {}, 55}},
    WidescreenByteContract{WidescreenContractSite::bar_panel_568_end, {FeatureId::windowed_widescreen, "bar_panel_568_end", VersionedOperationKind::mid_hook, 0x1BD7CB, 5, PatternOf<0xE8, 0xE0, 0x39, 0xE4, 0xFF>(), {}, 56}},
    WidescreenByteContract{WidescreenContractSite::bar_stage_panel_begin, {FeatureId::windowed_widescreen, "bar_stage_panel_begin", VersionedOperationKind::mid_hook, 0x1BD803, 5, PatternOf<0xE8, 0x28, 0xCA, 0xFF, 0xFF>(), {}, 57}},
    WidescreenByteContract{WidescreenContractSite::bar_stage_panel_end, {FeatureId::windowed_widescreen, "bar_stage_panel_end", VersionedOperationKind::mid_hook, 0x1BD808, 6, PatternOf<0x51, 0xD9, 0xE8, 0xD9, 0x1C, 0x24>(), {}, 58}},
    WidescreenByteContract{WidescreenContractSite::bar_stage_current_begin, {FeatureId::windowed_widescreen, "bar_stage_current_begin", VersionedOperationKind::mid_hook, 0x1BD870, 5, PatternOf<0xE8, 0x2B, 0x79, 0xFE, 0xFF>(), {}, 59}},
    WidescreenByteContract{WidescreenContractSite::bar_stage_current_end, {FeatureId::windowed_widescreen, "bar_stage_current_end", VersionedOperationKind::mid_hook, 0x1BD875, 6, PatternOf<0x83, 0xC4, 0x20, 0x51, 0xD9, 0xE8>(), {}, 60}},
    WidescreenByteContract{WidescreenContractSite::bar_stage_total_begin, {FeatureId::windowed_widescreen, "bar_stage_total_begin", VersionedOperationKind::mid_hook, 0x1BD8CD, 5, PatternOf<0xE8, 0xCE, 0x78, 0xFE, 0xFF>(), {}, 61}},
    WidescreenByteContract{WidescreenContractSite::bar_stage_total_end, {FeatureId::windowed_widescreen, "bar_stage_total_end", VersionedOperationKind::mid_hook, 0x1BD8D5, 5, PatternOf<0xE8, 0xD6, 0x38, 0xE4, 0xFF>(), {}, 62}},
    WidescreenByteContract{WidescreenContractSite::bar_gauge_begin, {FeatureId::windowed_widescreen, "bar_gauge_begin", VersionedOperationKind::mid_hook, 0x1BD94B, 5, PatternOf<0xE8, 0x30, 0x86, 0xFE, 0xFF>(), {}, 63}},
    WidescreenByteContract{WidescreenContractSite::bar_gauge_end, {FeatureId::windowed_widescreen, "bar_gauge_end", VersionedOperationKind::mid_hook, 0x1BD953, 5, PatternOf<0xE8, 0x58, 0x38, 0xE4, 0xFF>(), {}, 64}},
    WidescreenByteContract{WidescreenContractSite::bar_panel_216_begin, {FeatureId::windowed_widescreen, "bar_panel_216_begin", VersionedOperationKind::mid_hook, 0x1BD983, 5, PatternOf<0xE8, 0xA8, 0xC8, 0xFF, 0xFF>(), {}, 65}},
    WidescreenByteContract{WidescreenContractSite::bar_panel_216_end, {FeatureId::windowed_widescreen, "bar_panel_216_end", VersionedOperationKind::mid_hook, 0x1BD988, 5, PatternOf<0xE8, 0x23, 0x38, 0xE4, 0xFF>(), {}, 66}},
    WidescreenByteContract{WidescreenContractSite::bar_score_panel_begin, {FeatureId::windowed_widescreen, "bar_score_panel_begin", VersionedOperationKind::mid_hook, 0x1BD9AA, 5, PatternOf<0xE8, 0x81, 0xC8, 0xFF, 0xFF>(), {}, 67}},
    WidescreenByteContract{WidescreenContractSite::bar_score_panel_end, {FeatureId::windowed_widescreen, "bar_score_panel_end", VersionedOperationKind::mid_hook, 0x1BD9AF, 6, PatternOf<0x51, 0xD9, 0xE8, 0xD9, 0x1C, 0x24>(), {}, 68}},
    WidescreenByteContract{WidescreenContractSite::bar_score_digits_begin, {FeatureId::windowed_widescreen, "bar_score_digits_begin", VersionedOperationKind::mid_hook, 0x1BDA01, 5, PatternOf<0xE8, 0x9A, 0x77, 0xFE, 0xFF>(), {}, 69}},
    WidescreenByteContract{WidescreenContractSite::bar_score_digits_end, {FeatureId::windowed_widescreen, "bar_score_digits_end", VersionedOperationKind::mid_hook, 0x1BDA09, 5, PatternOf<0xE8, 0xA2, 0x37, 0xE4, 0xFF>(), {}, 70}},
    WidescreenByteContract{WidescreenContractSite::bar_extra_panel_a_begin, {FeatureId::windowed_widescreen, "bar_extra_panel_a_begin", VersionedOperationKind::mid_hook, 0x1BDA39, 5, PatternOf<0xE8, 0xF2, 0xC7, 0xFF, 0xFF>(), {}, 71}},
    WidescreenByteContract{WidescreenContractSite::bar_extra_panel_a_end, {FeatureId::windowed_widescreen, "bar_extra_panel_a_end", VersionedOperationKind::mid_hook, 0x1BDA3E, 6, PatternOf<0x51, 0xD9, 0xE8, 0xD9, 0x1C, 0x24>(), {}, 72}},
    WidescreenByteContract{WidescreenContractSite::bar_extra_digits_a_begin, {FeatureId::windowed_widescreen, "bar_extra_digits_a_begin", VersionedOperationKind::mid_hook, 0x1BDA8A, 5, PatternOf<0xE8, 0x11, 0x77, 0xFE, 0xFF>(), {}, 73}},
    WidescreenByteContract{WidescreenContractSite::bar_extra_digits_a_end, {FeatureId::windowed_widescreen, "bar_extra_digits_a_end", VersionedOperationKind::mid_hook, 0x1BDA92, 5, PatternOf<0xE8, 0x19, 0x37, 0xE4, 0xFF>(), {}, 74}},
    WidescreenByteContract{WidescreenContractSite::bar_extra_panel_b_begin, {FeatureId::windowed_widescreen, "bar_extra_panel_b_begin", VersionedOperationKind::mid_hook, 0x1BDAB4, 5, PatternOf<0xE8, 0x77, 0xC7, 0xFF, 0xFF>(), {}, 75}},
    WidescreenByteContract{WidescreenContractSite::bar_extra_panel_b_end, {FeatureId::windowed_widescreen, "bar_extra_panel_b_end", VersionedOperationKind::mid_hook, 0x1BDAB9, 6, PatternOf<0x51, 0xD9, 0xE8, 0xD9, 0x1C, 0x24>(), {}, 76}},
    WidescreenByteContract{WidescreenContractSite::bar_extra_digits_b_begin, {FeatureId::windowed_widescreen, "bar_extra_digits_b_begin", VersionedOperationKind::mid_hook, 0x1BDB05, 5, PatternOf<0xE8, 0x96, 0x76, 0xFE, 0xFF>(), {}, 77}},
    WidescreenByteContract{WidescreenContractSite::bar_extra_digits_b_end, {FeatureId::windowed_widescreen, "bar_extra_digits_b_end", VersionedOperationKind::mid_hook, 0x1BDB0D, 5, PatternOf<0xE8, 0x9E, 0x36, 0xE4, 0xFF>(), {}, 78}},
    WidescreenByteContract{WidescreenContractSite::bar_mode_panel_begin, {FeatureId::windowed_widescreen, "bar_mode_panel_begin", VersionedOperationKind::mid_hook, 0x1BE32B, 5, PatternOf<0xE8, 0x00, 0xBF, 0xFF, 0xFF>(), {}, 79}},
    WidescreenByteContract{WidescreenContractSite::bar_mode_panel_end, {FeatureId::windowed_widescreen, "bar_mode_panel_end", VersionedOperationKind::mid_hook, 0x1BE330, 5, PatternOf<0xE8, 0x7B, 0x2E, 0xE4, 0xFF>(), {}, 80}},
    WidescreenByteContract{WidescreenContractSite::bar_player_panel_begin, {FeatureId::windowed_widescreen, "bar_player_panel_begin", VersionedOperationKind::mid_hook, 0x1BE371, 5, PatternOf<0xE8, 0xBA, 0xBE, 0xFF, 0xFF>(), {}, 81}},
    WidescreenByteContract{WidescreenContractSite::bar_player_panel_end, {FeatureId::windowed_widescreen, "bar_player_panel_end", VersionedOperationKind::mid_hook, 0x1BE376, 5, PatternOf<0xE8, 0x35, 0x2E, 0xE4, 0xFF>(), {}, 82}},
    WidescreenByteContract{WidescreenContractSite::bar_status_panel_begin, {FeatureId::windowed_widescreen, "bar_status_panel_begin", VersionedOperationKind::mid_hook, 0x1BE3C8, 5, PatternOf<0xE8, 0x63, 0xBE, 0xFF, 0xFF>(), {}, 83}},
    WidescreenByteContract{WidescreenContractSite::bar_status_panel_end, {FeatureId::windowed_widescreen, "bar_status_panel_end", VersionedOperationKind::mid_hook, 0x1BE3CD, 5, PatternOf<0xE8, 0xAE, 0x5F, 0xE8, 0xFF>(), {}, 84}},
    WidescreenByteContract{WidescreenContractSite::bar_names_end, {FeatureId::windowed_widescreen, "bar_names_end", VersionedOperationKind::mid_hook, 0x216A69, 6, PatternOf<0x8B, 0x85, 0x4C, 0xFE, 0xFF, 0xFF>(), {}, 85}},
    WidescreenByteContract{WidescreenContractSite::chain_label_end, {FeatureId::windowed_widescreen, "chain_label_end", VersionedOperationKind::mid_hook, 0x1BDCA8, 7, PatternOf<0x51, 0xD9, 0x45, 0xD8, 0xD9, 0x1C, 0x24>(), {}, 86}},
    WidescreenByteContract{WidescreenContractSite::chain_digits_end, {FeatureId::windowed_widescreen, "chain_digits_end", VersionedOperationKind::mid_hook, 0x1BDCF5, 10, PatternOf<0x83, 0xC4, 0x20, 0xC7, 0x45, 0xCC, 0x00, 0x00, 0x00, 0x00>(), {}, 87}},
    WidescreenByteContract{WidescreenContractSite::chain_glow_end, {FeatureId::windowed_widescreen, "chain_glow_end", VersionedOperationKind::mid_hook, 0x1BDDB1, 5, PatternOf<0xE9, 0x4B, 0xFF, 0xFF, 0xFF>(), {}, 88}},
    WidescreenByteContract{WidescreenContractSite::hundred_digits_end, {FeatureId::windowed_widescreen, "hundred_digits_end", VersionedOperationKind::mid_hook, 0x1BDF07, 8, PatternOf<0x83, 0xC4, 0x20, 0xE8, 0x71, 0x64, 0xE8, 0xFF>(), {}, 89}},
    WidescreenByteContract{WidescreenContractSite::effect_packet_end, {FeatureId::windowed_widescreen, "effect_packet_end", VersionedOperationKind::mid_hook, 0x1CDC19, 10, PatternOf<0x8B, 0x4D, 0xAC, 0xC7, 0x41, 0x70, 0x00, 0x00, 0x00, 0x00>(), {}, 90}},
    WidescreenByteContract{WidescreenContractSite::bar_names_begin, {FeatureId::windowed_widescreen, "bar_names_begin", VersionedOperationKind::mid_hook, 0x216A64, 5, PatternOf<0xE8, 0x27, 0x47, 0xFA, 0xFF>(), {}, 38}},
    WidescreenByteContract{WidescreenContractSite::chain_glow_begin, {FeatureId::windowed_widescreen, "chain_glow_begin", VersionedOperationKind::mid_hook, 0x1BDDA9, 5, PatternOf<0xE8, 0xF2, 0x73, 0xFE, 0xFF>(), {}, 41}},
    WidescreenByteContract{WidescreenContractSite::hundred_digits_begin, {FeatureId::windowed_widescreen, "hundred_digits_begin", VersionedOperationKind::mid_hook, 0x1BDF02, 5, PatternOf<0xE8, 0x99, 0x72, 0xFE, 0xFF>(), {}, 42}},
    WidescreenByteContract{WidescreenContractSite::effect_packet_allocated, {FeatureId::windowed_widescreen, "effect_packet_allocated", VersionedOperationKind::mid_hook, 0x1CD700, 7, PatternOf<0x89, 0x45, 0xF4, 0x83, 0x7D, 0xF4, 0x00>(), {}, 44}},
    WidescreenByteContract{WidescreenContractSite::effect_packet_begin, {FeatureId::windowed_widescreen, "effect_packet_begin", VersionedOperationKind::mid_hook, 0x1CDC14, 5, PatternOf<0xE8, 0x37, 0xF5, 0xFF, 0xFF>(), {}, 45}},
    WidescreenByteContract{WidescreenContractSite::config_apply, {FeatureId::windowed_widescreen, "config_apply", VersionedOperationKind::inline_hook, 0x209190, 6, PatternOf<0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x14, 0xE8, 0x05, 0xFF, 0xFF, 0xFF>(), {}, 0}},
    WidescreenByteContract{WidescreenContractSite::window_device_create, {FeatureId::windowed_widescreen, "window_device_create", VersionedOperationKind::inline_hook, 0x4CC60, 5, PatternOf<0x83, 0xEC, 0x64, 0x53, 0x55, 0x56, 0x57, 0x6A, 0x30, 0x33, 0xED, 0x8D>(), {}, 1}},
    WidescreenByteContract{WidescreenContractSite::frame_begin, {FeatureId::windowed_widescreen, "frame_begin", VersionedOperationKind::inline_hook, 0x4C030, 7, PatternOf<0x51, 0x53, 0x56, 0x8D, 0x44, 0x24, 0x08, 0x57, 0x50, 0x8B, 0xF1, 0xE8>(), {}, 5}},
    WidescreenByteContract{WidescreenContractSite::frame_end, {FeatureId::windowed_widescreen, "frame_end", VersionedOperationKind::inline_hook, 0x4C0A0, 5, PatternOf<0x8B, 0x41, 0x08, 0x8B, 0x08, 0x8B, 0x91, 0xA8, 0x00, 0x00, 0x00, 0x50>(), {}, 6}},
    WidescreenByteContract{WidescreenContractSite::task_dispatch, {FeatureId::windowed_widescreen, "task_dispatch", VersionedOperationKind::inline_hook, 0x4D620, 7, PatternOf<0x8B, 0x09, 0x8B, 0x01, 0x8B, 0x50, 0x10, 0xFF, 0xE2, 0xCC, 0xCC, 0xCC>(), {}, 7}},
    WidescreenByteContract{WidescreenContractSite::screen_width_int, {FeatureId::windowed_widescreen, "screen_width_int", VersionedOperationKind::inline_hook, 0x44180, 5, PatternOf<0xA1, 0xA8, 0x0D, 0x74, 0x00, 0xC3>(), {}, 12}},
    WidescreenByteContract{WidescreenContractSite::screen_height_int, {FeatureId::windowed_widescreen, "screen_height_int", VersionedOperationKind::inline_hook, 0x44190, 5, PatternOf<0xA1, 0xAC, 0x0D, 0x74, 0x00, 0xC3>(), {}, 13}},
    WidescreenByteContract{WidescreenContractSite::screen_width_float, {FeatureId::windowed_widescreen, "screen_width_float", VersionedOperationKind::inline_hook, 0x441A0, 6, PatternOf<0xD9, 0x05, 0xB0, 0x0D, 0x74, 0x00, 0xC3>(), {}, 14}},
    WidescreenByteContract{WidescreenContractSite::screen_height_float, {FeatureId::windowed_widescreen, "screen_height_float", VersionedOperationKind::inline_hook, 0x441B0, 6, PatternOf<0xD9, 0x05, 0xB4, 0x0D, 0x74, 0x00, 0xC3>(), {}, 15}},
    WidescreenByteContract{WidescreenContractSite::target_width_int, {FeatureId::windowed_widescreen, "target_width_int", VersionedOperationKind::inline_hook, 0x44200, 5, PatternOf<0xA1, 0xB8, 0x0D, 0x74, 0x00, 0xC3>(), {}, 16}},
    WidescreenByteContract{WidescreenContractSite::target_height_int, {FeatureId::windowed_widescreen, "target_height_int", VersionedOperationKind::inline_hook, 0x44210, 5, PatternOf<0xA1, 0xBC, 0x0D, 0x74, 0x00, 0xC3>(), {}, 17}},
    WidescreenByteContract{WidescreenContractSite::target_width_float, {FeatureId::windowed_widescreen, "target_width_float", VersionedOperationKind::inline_hook, 0x44220, 6, PatternOf<0xD9, 0x05, 0xC0, 0x0D, 0x74, 0x00, 0xC3>(), {}, 18}},
    WidescreenByteContract{WidescreenContractSite::target_height_float, {FeatureId::windowed_widescreen, "target_height_float", VersionedOperationKind::inline_hook, 0x44230, 6, PatternOf<0xD9, 0x05, 0xC4, 0x0D, 0x74, 0x00, 0xC3>(), {}, 19}},
    WidescreenByteContract{WidescreenContractSite::logical_resolution_set, {FeatureId::windowed_widescreen, "logical_resolution_set", VersionedOperationKind::inline_hook, 0x448C0, 7, PatternOf<0x6A, 0xFF, 0x68, 0x3B, 0x49, 0x63, 0x00, 0x64, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x50>(), {}, 2}},
    WidescreenByteContract{WidescreenContractSite::logical_target_width_set, {FeatureId::windowed_widescreen, "logical_target_width_set", VersionedOperationKind::inline_hook, 0x441C0, 8, PatternOf<0xDB, 0x44, 0x24, 0x04, 0x8B, 0x44, 0x24, 0x04, 0xA3, 0xB8, 0x0D, 0x74, 0x00>(), {}, 3}},
    WidescreenByteContract{WidescreenContractSite::logical_target_height_set, {FeatureId::windowed_widescreen, "logical_target_height_set", VersionedOperationKind::inline_hook, 0x441E0, 8, PatternOf<0xDB, 0x44, 0x24, 0x04, 0x8B, 0x44, 0x24, 0x04, 0xA3, 0xBC, 0x0D, 0x74, 0x00>(), {}, 4}},
    WidescreenByteContract{WidescreenContractSite::viewport_reset, {FeatureId::windowed_widescreen, "viewport_reset", VersionedOperationKind::inline_hook, 0x443A0, 6, PatternOf<0x8B, 0x4C, 0x24, 0x04, 0x33, 0xC0, 0x83, 0xEC, 0x20, 0x3B, 0xC8, 0x0F>(), {}, 20}},
    WidescreenByteContract{WidescreenContractSite::mouse_debug_poll, {FeatureId::windowed_widescreen, "mouse_debug_poll", VersionedOperationKind::inline_hook, 0xA3E10, 6, PatternOf<0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x08, 0x89, 0x4D, 0xF8, 0x8B, 0x45, 0xF8>(), {}, 21}},
    WidescreenByteContract{WidescreenContractSite::reset_pre, {FeatureId::windowed_widescreen, "reset_pre", VersionedOperationKind::mid_hook, 0x4C64B, 7, PatternOf<0x83, 0xBE, 0x94, 0x00, 0x00, 0x00, 0x00>(), {}, 34}},
    WidescreenByteContract{WidescreenContractSite::reset_post, {FeatureId::windowed_widescreen, "reset_post", VersionedOperationKind::mid_hook, 0x4C834, 8, PatternOf<0x83, 0xC4, 0x04, 0xB8, 0x01, 0x00, 0x00, 0x00>(), {}, 35}},
    WidescreenByteContract{WidescreenContractSite::gameplay_stage_background, {FeatureId::windowed_widescreen, "gameplay_stage_background", VersionedOperationKind::mid_hook, 0x1C35B0, 5, PatternOf<0xE8, 0x3B, 0xE5, 0x04, 0x00, 0x8B, 0x4D, 0xC8>(), {}, 22}},
    WidescreenByteContract{WidescreenContractSite::gameplay_track, {FeatureId::windowed_widescreen, "gameplay_track", VersionedOperationKind::mid_hook, 0x1C35B8, 5, PatternOf<0xE8, 0x03, 0x38, 0x05, 0x00, 0x8B, 0x4D, 0xC8>(), {}, 23}},
    WidescreenByteContract{WidescreenContractSite::gameplay_effects, {FeatureId::windowed_widescreen, "gameplay_effects", VersionedOperationKind::mid_hook, 0x1C3608, 5, PatternOf<0xE8, 0x53, 0x1F, 0x05, 0x00, 0xE8, 0x6E, 0x0D, 0xE8, 0xFF>(), {}, 24}},
    WidescreenByteContract{WidescreenContractSite::gameplay_effects_end, {FeatureId::windowed_widescreen, "gameplay_effects_end", VersionedOperationKind::mid_hook, 0x1C360D, 5, PatternOf<0xE8, 0x6E, 0x0D, 0xE8, 0xFF>(), {}, 25}},
    WidescreenByteContract{WidescreenContractSite::gameplay_hud_projection, {FeatureId::windowed_widescreen, "gameplay_hud_projection", VersionedOperationKind::mid_hook, 0x20C8EA, 5, PatternOf<0xE8, 0x91, 0xB5, 0xFA, 0xFF, 0x8B, 0xB5, 0x24, 0xFF, 0xFF, 0xFF, 0x81, 0xC6, 0xD0, 0x00, 0x00>(), {}, 26}},
    WidescreenByteContract{WidescreenContractSite::chain_label_begin, {FeatureId::windowed_widescreen, "chain_label_begin", VersionedOperationKind::mid_hook, 0x1BDCA3, 5, PatternOf<0xE8, 0x88, 0xC5, 0xFF, 0xFF>(), {}, 27}},
    WidescreenByteContract{WidescreenContractSite::chain_digits_begin, {FeatureId::windowed_widescreen, "chain_digits_begin", VersionedOperationKind::mid_hook, 0x1BDCF0, 5, PatternOf<0xE8, 0xAB, 0x74, 0xFE, 0xFF>(), {}, 36}},
    WidescreenByteContract{WidescreenContractSite::gameplay_feedback_draw_begin, {FeatureId::windowed_widescreen, "gameplay_feedback_draw_begin", VersionedOperationKind::mid_hook, 0x1CDD38, 5, PatternOf<0xE8, 0x83, 0x0D, 0x00, 0x00>(), {}, 29}},
    WidescreenByteContract{WidescreenContractSite::gameplay_feedback_draw_end, {FeatureId::windowed_widescreen, "gameplay_feedback_draw_end", VersionedOperationKind::mid_hook, 0x1CDD3D, 6, PatternOf<0x8B, 0x4D, 0xF8, 0x8B, 0x51, 0x0C, 0x81, 0xE2, 0x00, 0x40>(), {}, 30}},
    WidescreenByteContract{WidescreenContractSite::test_mode_native_begin, {FeatureId::windowed_widescreen, "test_mode_native_begin", VersionedOperationKind::mid_hook, 0x207F3C, 5, PatternOf<0xE8, 0xCF, 0x91, 0xF5, 0xFF, 0xE8, 0x3A, 0xC4, 0xE3, 0xFF>(), {}, 10}},
    WidescreenByteContract{WidescreenContractSite::test_mode_native_end, {FeatureId::windowed_widescreen, "test_mode_native_end", VersionedOperationKind::mid_hook, 0x207F41, 5, PatternOf<0xE8, 0x3A, 0xC4, 0xE3, 0xFF, 0x89, 0x85, 0x94, 0xFE, 0xFF, 0xFF, 0x8B, 0x95>(), {}, 11}},
    WidescreenByteContract{WidescreenContractSite::clip_default, {FeatureId::windowed_widescreen, "clip_default", VersionedOperationKind::read_only_contract, 0x2112C6, 4, PatternOf<0xC6, 0x45, 0xDF, 0x00>(), {}, 0, SiteDisposition::verify_only}},
    WidescreenByteContract{WidescreenContractSite::clip_gate, {FeatureId::windowed_widescreen, "clip_gate", VersionedOperationKind::mid_hook, 0x2112CA, 6, PatternOf<0x8B, 0x95, 0x80, 0xFE, 0xFF, 0xFF, 0x8B, 0x82, 0x4C, 0x02, 0x00, 0x00, 0x0F, 0xB6, 0x88, 0x5C, 0x01, 0x00, 0x00>(), {}, 33}},
    WidescreenByteContract{WidescreenContractSite::clip_continuation, {FeatureId::windowed_widescreen, "clip_continuation", VersionedOperationKind::read_only_contract, 0x21132F, 11, PatternOf<0x8B, 0x4D, 0xD8, 0xE8, 0x39, 0xA6, 0xE1, 0xFF, 0x0F, 0xB6, 0xC0>(), {}, 0, SiteDisposition::verify_only}},
    WidescreenByteContract{WidescreenContractSite::batch_flush, {FeatureId::windowed_widescreen, "batch_flush", VersionedOperationKind::read_only_contract, 0x1A2C50, 13, PatternOf<0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x08, 0xC7, 0x45, 0xFC, 0x00, 0x00, 0x00, 0x00>(), {}, 0, SiteDisposition::verify_only}},
    WidescreenByteContract{WidescreenContractSite::clip_owner, {FeatureId::windowed_widescreen, "clip_owner", VersionedOperationKind::read_only_contract, 0x211100, 17, PatternOf<0x55, 0x8B, 0xEC, 0x81, 0xEC, 0xA0, 0x01, 0x00, 0x00, 0x56, 0x57, 0x89, 0x8D, 0x80, 0xFE, 0xFF, 0xFF>(), {}, 0, SiteDisposition::verify_only}},
    WidescreenByteContract{WidescreenContractSite::live_frustum_helper, {FeatureId::windowed_widescreen, "live_frustum_helper", VersionedOperationKind::read_only_contract, 0x210CE0, 15, PatternOf<0x55, 0x8B, 0xEC, 0x81, 0xEC, 0xC0, 0x00, 0x00, 0x00, 0x89, 0x8D, 0x58, 0xFF, 0xFF, 0xFF>(), {}, 0, SiteDisposition::verify_only}}
};
constexpr std::array<WidescreenPointerContract, 9> kPointers206{{
    {WidescreenContractSite::common_2d_render, {FeatureId::windowed_widescreen, "common_2d_render",
        VersionedOperationKind::read_only_contract, 0x2B7C98, 4,
        PatternOf<0x50, 0x1C, 0x5D, 0x00>(), {}, 0, SiteDisposition::verify_only}, 0x1D1C50},
    {WidescreenContractSite::common_3d_render, {FeatureId::windowed_widescreen, "common_3d_render",
        VersionedOperationKind::read_only_contract, 0x2B8C70, 4,
        PatternOf<0x60, 0x32, 0x56, 0x00>(), {}, 0, SiteDisposition::verify_only}, 0x163260},
    {WidescreenContractSite::config_height_setter, {FeatureId::windowed_widescreen, "config_height_setter",
        VersionedOperationKind::read_only_contract, 0x26F620, 4,
        PatternOf<0xA0, 0xB0, 0x44, 0x00>(), {}, 0, SiteDisposition::verify_only}, 0x4B0A0},
    {WidescreenContractSite::config_minmax_setter, {FeatureId::windowed_widescreen, "config_minmax_setter",
        VersionedOperationKind::read_only_contract, 0x26F630, 4,
        PatternOf<0x00, 0xB1, 0x44, 0x00>(), {}, 0, SiteDisposition::verify_only}, 0x4B100},
    {WidescreenContractSite::config_mode_setter, {FeatureId::windowed_widescreen, "config_mode_setter",
        VersionedOperationKind::read_only_contract, 0x26F634, 4,
        PatternOf<0x30, 0xB1, 0x44, 0x00>(), {}, 0, SiteDisposition::verify_only}, 0x4B130},
    {WidescreenContractSite::config_resize_setter, {FeatureId::windowed_widescreen, "config_resize_setter",
        VersionedOperationKind::read_only_contract, 0x26F62C, 4,
        PatternOf<0xE0, 0xB0, 0x44, 0x00>(), {}, 0, SiteDisposition::verify_only}, 0x4B0E0},
    {WidescreenContractSite::config_width_setter, {FeatureId::windowed_widescreen, "config_width_setter",
        VersionedOperationKind::read_only_contract, 0x26F61C, 4,
        PatternOf<0x80, 0xB0, 0x44, 0x00>(), {}, 0, SiteDisposition::verify_only}, 0x4B080},
    {WidescreenContractSite::network_status_movie_clip_accept, {FeatureId::windowed_widescreen, "network_status_movie_clip_accept",
        VersionedOperationKind::global_vtable_slot, 0x2806F0, 4,
        PatternOf<0xA0, 0x5F, 0x4D, 0x00>(), {}, 8}, 0xD5FA0},
    {WidescreenContractSite::network_status_shape_draw_visit, {FeatureId::windowed_widescreen, "network_status_shape_draw_visit",
        VersionedOperationKind::global_vtable_slot, 0x27DE18, 4,
        PatternOf<0x70, 0x18, 0x4C, 0x00>(), {}, 9}, 0xC1870}
}};
constexpr WindowedWidescreenProfile Make206(GameImageVariant variant) {
    return {GameBuild::groove_coaster_206, variant, kBytes206, kPointers206, kAbis, kOrder, {
        .main_config_vtable = 0x26F604,
        .batch_queue_pointer = 0x3AA95C,
        .movie_clip_draw_visitor_vtable = 0x27DDCC,
        .common_2d_vtable = 0x2B7C88,
        .common_3d_vtable = 0x2B8C60,
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
        .tune_effect_collection_offset = 0x1D60,
        .effect_root_manager_offset = 0x74,
        .pointer_collection_begin_offset = 0xC,
        .pointer_collection_end_offset = 0x10,
        .network_status_visitor_matrix_stack_offset = 0x1A0,
    }};
}
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
        .effect_root_manager_offset = 0x74,
        .pointer_collection_begin_offset = 0xC,
        .pointer_collection_end_offset = 0x10,
        .network_status_visitor_matrix_stack_offset = 0x1A0,
    }};
}
}
const WindowedWidescreenProfile* ProfileFor(
    game_version::GameBuild build, game_version::GameImageVariant variant) noexcept {
    using namespace game_version;
    if (build == GameBuild::groove_coaster_206) {
        static constexpr auto clean206 = Make206(GameImageVariant::clean);
        static constexpr auto verified206 = Make206(GameImageVariant::locally_verified);
        switch (variant) {
        case GameImageVariant::clean: return &clean206;
        case GameImageVariant::locally_verified: return &verified206;
        case GameImageVariant::legacy_patched: return nullptr;
        }
    }
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

namespace gc::windowed_widescreen {
    game_version::VersionedOperation detail::BindWidescreenHook(
        WidescreenContractSite site, const game_version::SiteContract& contract, void* expected) noexcept {
        using namespace game_version;
        using namespace detail;
        switch (site) {
        case WidescreenContractSite::bar_difficulty_a_begin:
            return MidHookOperation{contract, &BarDrawBeginMid};
        case WidescreenContractSite::bar_difficulty_a_end:
            return MidHookOperation{contract, &BarDrawEndMid};
        case WidescreenContractSite::bar_difficulty_b_begin:
            return MidHookOperation{contract, &BarDrawBeginMid};
        case WidescreenContractSite::bar_difficulty_b_end:
            return MidHookOperation{contract, &BarDrawEndMid};
        case WidescreenContractSite::bar_panel_480_begin:
            return MidHookOperation{contract, &BarDrawBeginMid};
        case WidescreenContractSite::bar_panel_480_end:
            return MidHookOperation{contract, &BarDrawEndMid};
        case WidescreenContractSite::bar_panel_524_begin:
            return MidHookOperation{contract, &BarDrawBeginMid};
        case WidescreenContractSite::bar_panel_524_end:
            return MidHookOperation{contract, &BarDrawEndMid};
        case WidescreenContractSite::bar_panel_568_begin:
            return MidHookOperation{contract, &BarDrawBeginMid};
        case WidescreenContractSite::bar_panel_568_end:
            return MidHookOperation{contract, &BarDrawEndMid};
        case WidescreenContractSite::bar_stage_panel_begin:
            return MidHookOperation{contract, &BarDrawBeginMid};
        case WidescreenContractSite::bar_stage_panel_end:
            return MidHookOperation{contract, &BarDrawEndMid};
        case WidescreenContractSite::bar_stage_current_begin:
            return MidHookOperation{contract, &BarDrawBeginMid};
        case WidescreenContractSite::bar_stage_current_end:
            return MidHookOperation{contract, &BarDrawEndMid};
        case WidescreenContractSite::bar_stage_total_begin:
            return MidHookOperation{contract, &BarDrawBeginMid};
        case WidescreenContractSite::bar_stage_total_end:
            return MidHookOperation{contract, &BarDrawEndMid};
        case WidescreenContractSite::bar_gauge_begin:
            return MidHookOperation{contract, &BarDrawBeginMid};
        case WidescreenContractSite::bar_gauge_end:
            return MidHookOperation{contract, &BarDrawEndMid};
        case WidescreenContractSite::bar_panel_216_begin:
            return MidHookOperation{contract, &BarDrawBeginMid};
        case WidescreenContractSite::bar_panel_216_end:
            return MidHookOperation{contract, &BarDrawEndMid};
        case WidescreenContractSite::bar_score_panel_begin:
            return MidHookOperation{contract, &BarDrawBeginMid};
        case WidescreenContractSite::bar_score_panel_end:
            return MidHookOperation{contract, &BarDrawEndMid};
        case WidescreenContractSite::bar_score_digits_begin:
            return MidHookOperation{contract, &BarDrawBeginMid};
        case WidescreenContractSite::bar_score_digits_end:
            return MidHookOperation{contract, &BarDrawEndMid};
        case WidescreenContractSite::bar_extra_panel_a_begin:
            return MidHookOperation{contract, &BarDrawBeginMid};
        case WidescreenContractSite::bar_extra_panel_a_end:
            return MidHookOperation{contract, &BarDrawEndMid};
        case WidescreenContractSite::bar_extra_digits_a_begin:
            return MidHookOperation{contract, &BarDrawBeginMid};
        case WidescreenContractSite::bar_extra_digits_a_end:
            return MidHookOperation{contract, &BarDrawEndMid};
        case WidescreenContractSite::bar_extra_panel_b_begin:
            return MidHookOperation{contract, &BarDrawBeginMid};
        case WidescreenContractSite::bar_extra_panel_b_end:
            return MidHookOperation{contract, &BarDrawEndMid};
        case WidescreenContractSite::bar_extra_digits_b_begin:
            return MidHookOperation{contract, &BarDrawBeginMid};
        case WidescreenContractSite::bar_extra_digits_b_end:
            return MidHookOperation{contract, &BarDrawEndMid};
        case WidescreenContractSite::bar_mode_panel_begin:
            return MidHookOperation{contract, &BarDrawBeginMid};
        case WidescreenContractSite::bar_mode_panel_end:
            return MidHookOperation{contract, &BarDrawEndMid};
        case WidescreenContractSite::bar_player_panel_begin:
            return MidHookOperation{contract, &BarDrawBeginMid};
        case WidescreenContractSite::bar_player_panel_end:
            return MidHookOperation{contract, &BarDrawEndMid};
        case WidescreenContractSite::bar_status_panel_begin:
            return MidHookOperation{contract, &BarDrawBeginMid};
        case WidescreenContractSite::bar_status_panel_end:
            return MidHookOperation{contract, &BarDrawEndMid};
        case WidescreenContractSite::bar_names_end:
            return MidHookOperation{contract, &BarDrawEndMid};
        case WidescreenContractSite::chain_label_end:
            return MidHookOperation{contract, &CounterDrawEndMid};
        case WidescreenContractSite::chain_digits_end:
            return MidHookOperation{contract, &CounterDrawEndMid};
        case WidescreenContractSite::chain_glow_end:
            return MidHookOperation{contract, &CounterDrawEndMid};
        case WidescreenContractSite::hundred_digits_end:
            return MidHookOperation{contract, &CounterDrawEndMid};
        case WidescreenContractSite::effect_packet_end:
            return MidHookOperation{contract, &EffectPacketEndMid};
        case WidescreenContractSite::chain_digits_begin:
            return MidHookOperation{contract, &ChainDigitsBeginMid};
        case WidescreenContractSite::bar_names_begin:
            return MidHookOperation{contract, &BarDrawBeginMid};
        case WidescreenContractSite::chain_glow_begin:
            return MidHookOperation{contract, &ChainGlowBeginMid};
        case WidescreenContractSite::hundred_digits_begin:
            return MidHookOperation{contract, &HundredDigitsBeginMid};
        case WidescreenContractSite::effect_packet_allocated:
            return MidHookOperation{contract, &EffectPacketAllocatedMid};
        case WidescreenContractSite::effect_packet_begin:
            return MidHookOperation{contract, &EffectPacketSubmitMid};
        case WidescreenContractSite::config_apply:
            return InlineHookOperation{contract, reinterpret_cast<void*>(&ConfigApplyDetour),
                hooking::OriginalPublisher::To(&g_window_originals.config_apply)};
        case WidescreenContractSite::window_device_create:
            return InlineHookOperation{contract, reinterpret_cast<void*>(&WindowDeviceDetour),
                hooking::OriginalPublisher::To(&g_window_originals.window_device_create)};
        case WidescreenContractSite::logical_resolution_set:
            return InlineHookOperation{contract, reinterpret_cast<void*>(&LogicalResolutionSetDetour),
                hooking::OriginalPublisher::To(&g_window_originals.logical_resolution_set)};
        case WidescreenContractSite::logical_target_width_set:
            return InlineHookOperation{contract, reinterpret_cast<void*>(&LogicalTargetDimensionSetDetour< RenderDimensionAxis::width, WidescreenContractSite::logical_target_width_set>),
                hooking::OriginalPublisher::To(&g_window_originals.logical_target_width_set)};
        case WidescreenContractSite::logical_target_height_set:
            return InlineHookOperation{contract, reinterpret_cast<void*>(&LogicalTargetDimensionSetDetour< RenderDimensionAxis::height, WidescreenContractSite::logical_target_height_set>),
                hooking::OriginalPublisher::To(&g_window_originals.logical_target_height_set)};
        case WidescreenContractSite::frame_begin:
            return InlineHookOperation{contract, reinterpret_cast<void*>(&FrameBeginDetour),
                hooking::OriginalPublisher::To(&g_render_originals.frame_begin)};
        case WidescreenContractSite::frame_end:
            return InlineHookOperation{contract, reinterpret_cast<void*>(&FrameEndDetour),
                hooking::OriginalPublisher::To(&g_render_originals.frame_end)};
        case WidescreenContractSite::task_dispatch:
            return InlineHookOperation{contract, reinterpret_cast<void*>(&TaskDispatchDetour),
                hooking::OriginalPublisher::To(&g_gameplay_originals.task_dispatch)};
        case WidescreenContractSite::network_status_movie_clip_accept: {
            auto bound = contract;
            auto* replacement = reinterpret_cast<void*>(&NetworkStatusMovieClipAcceptDetour);
            bound.original.size = sizeof(expected);
            std::memcpy(bound.original.bytes.data(), &expected, sizeof(expected));
            bound.installed.size = sizeof(replacement);
            std::memcpy(bound.installed.bytes.data(), &replacement, sizeof(replacement));
            return GlobalVtableSlotOperation{bound, expected, replacement,
                runtime_image::VtableOriginalPublisher::To(&g_network_originals.network_status_movie_clip_accept)};
        }
        case WidescreenContractSite::network_status_shape_draw_visit: {
            auto bound = contract;
            auto* replacement = reinterpret_cast<void*>(&NetworkStatusShapeDrawVisitDetour);
            bound.original.size = sizeof(expected);
            std::memcpy(bound.original.bytes.data(), &expected, sizeof(expected));
            bound.installed.size = sizeof(replacement);
            std::memcpy(bound.installed.bytes.data(), &replacement, sizeof(replacement));
            return GlobalVtableSlotOperation{bound, expected, replacement,
                runtime_image::VtableOriginalPublisher::To(&g_network_originals.network_status_shape_draw_visit)};
        }
        case WidescreenContractSite::test_mode_native_begin:
            return MidHookOperation{contract, &TestModeNativeBeginMid};
        case WidescreenContractSite::test_mode_native_end:
            return MidHookOperation{contract, &TestModeNativeEndMid};
        case WidescreenContractSite::screen_width_int:
            return InlineHookOperation{contract, reinterpret_cast<void*>(&ScreenWidthIntDetour),
                hooking::OriginalPublisher::To(&g_render_originals.screen_width_int)};
        case WidescreenContractSite::screen_height_int:
            return InlineHookOperation{contract, reinterpret_cast<void*>(&ScreenHeightIntDetour),
                hooking::OriginalPublisher::To(&g_render_originals.screen_height_int)};
        case WidescreenContractSite::screen_width_float:
            return InlineHookOperation{contract, reinterpret_cast<void*>(&ScreenWidthFloatDetour),
                hooking::OriginalPublisher::To(&g_render_originals.screen_width_float)};
        case WidescreenContractSite::screen_height_float:
            return InlineHookOperation{contract, reinterpret_cast<void*>(&ScreenHeightFloatDetour),
                hooking::OriginalPublisher::To(&g_render_originals.screen_height_float)};
        case WidescreenContractSite::target_width_int:
            return InlineHookOperation{contract, reinterpret_cast<void*>(&TargetWidthIntDetour),
                hooking::OriginalPublisher::To(&g_render_originals.target_width_int)};
        case WidescreenContractSite::target_height_int:
            return InlineHookOperation{contract, reinterpret_cast<void*>(&TargetHeightIntDetour),
                hooking::OriginalPublisher::To(&g_render_originals.target_height_int)};
        case WidescreenContractSite::target_width_float:
            return InlineHookOperation{contract, reinterpret_cast<void*>(&TargetWidthFloatDetour),
                hooking::OriginalPublisher::To(&g_render_originals.target_width_float)};
        case WidescreenContractSite::target_height_float:
            return InlineHookOperation{contract, reinterpret_cast<void*>(&TargetHeightFloatDetour),
                hooking::OriginalPublisher::To(&g_render_originals.target_height_float)};
        case WidescreenContractSite::viewport_reset:
            return InlineHookOperation{contract, reinterpret_cast<void*>(&ViewportResetDetour),
                hooking::OriginalPublisher::To(&g_render_originals.viewport_reset)};
        case WidescreenContractSite::mouse_debug_poll:
            return InlineHookOperation{contract, reinterpret_cast<void*>(&MouseDebugPollDetour),
                hooking::OriginalPublisher::To(&g_window_originals.mouse_debug_poll)};
        case WidescreenContractSite::gameplay_stage_background:
            return MidHookOperation{contract, &GameplayStageBackgroundMid};
        case WidescreenContractSite::gameplay_track:
            return MidHookOperation{contract, &GameplayTrackMid};
        case WidescreenContractSite::gameplay_effects:
            return MidHookOperation{contract, &GameplayEffectsMid};
        case WidescreenContractSite::gameplay_effects_end:
            return MidHookOperation{contract, &GameplayEffectsEndMid};
        case WidescreenContractSite::gameplay_hud_projection:
            return MidHookOperation{contract, &GameplayHudProjectionMid};
        case WidescreenContractSite::chain_label_begin:
            return MidHookOperation{contract, &ChainLabelBeginMid};
        case WidescreenContractSite::gameplay_feedback_draw_begin:
            return MidHookOperation{contract, &GameplayFeedbackDrawBeginMid};
        case WidescreenContractSite::gameplay_feedback_draw_end:
            return MidHookOperation{contract, &GameplayFeedbackDrawEndMid};
        case WidescreenContractSite::clip_gate:
            return MidHookOperation{contract, &ClipGateMid};
        case WidescreenContractSite::reset_pre:
            return MidHookOperation{contract, &renderer_device_loss::OnWidescreenBeforeReset};
        case WidescreenContractSite::reset_post:
            return MidHookOperation{contract, &renderer_device_loss::OnWidescreenAfterReset};
        default: return ReadOnlyContractOperation{contract};
        }
    }


}

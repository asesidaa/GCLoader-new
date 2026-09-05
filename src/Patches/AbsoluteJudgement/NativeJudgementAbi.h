#pragma once
#include <cstddef>
#include <cstdint>

namespace gc::absolute_judgement::native_abi {
static_assert(sizeof(void*) == sizeof(std::uint32_t));

using RecognitionFn = void(__thiscall*)(void*, int, int);
using ScoreFn = void(__thiscall*)(void*, int);
using PressedFn = std::uint8_t(__thiscall*)(void*, int, int);
using HeldFn = std::uint8_t(__thiscall*)(void*, int, int);
using ReleasedFn = std::uint8_t(__thiscall*)(void*, int, int);
using DirectionFn = int(__thiscall*)(void*, int, float*, float*, int);
using HeldAgeFn = int(__thiscall*)(void*, unsigned int);
using TimingGradeFn = int(__thiscall*)(void*, const float*, int);

using AccessorFn = void*(__cdecl*)();
using GetGroupCursorFn = int(__thiscall*)(void*, int);

// Value-owned bindings resolved from the approved image contracts before hooks enable.
struct NativeTargets final {
    std::uintptr_t loop_tail{};
    RecognitionFn recognition{};
    ScoreFn score{};
    AccessorFn get_input_manager{};
    AccessorFn get_global{};
    AccessorFn get_config{};
    AccessorFn get_sound_manager{};
    GetGroupCursorFn get_group_cursor{};
};

struct NativeLayout final {
    std::ptrdiff_t tune_stack_offset{};
    std::ptrdiff_t semantic_stage_tune_stack_offset{};
    std::size_t tune_judgement_states_offset{};
    std::size_t tune_score_states_offset{};
    std::size_t pointer_collection_begin_offset{};
    std::size_t pointer_collection_end_offset{};
    std::size_t global_player_index_offset{};
    std::size_t input_manager_booster_offset{};
    std::size_t game_time_offset_offset{};
    std::size_t hold_safe_frame_offset{};
    std::size_t slide_hold_safe_frame_offset{};
    std::size_t score_miss_offset{};
    std::size_t score_good_offset{};
    std::size_t score_cool_offset{};
    std::size_t score_great_offset{};
    std::size_t judgement_arrange_publication_offset{};
    std::size_t judgement_left_free_tap_publication_offset{};
    std::size_t judgement_right_free_tap_publication_offset{};
    std::size_t timing_grade_note_target_float_index{};
    int gameplay_sound_group{};
};
} // namespace gc::absolute_judgement::native_abi

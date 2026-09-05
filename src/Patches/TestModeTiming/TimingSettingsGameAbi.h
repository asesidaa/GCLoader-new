#pragma once
#include "Patches/TestModeTiming/TimingSettingsModel.h"
#include "Patches/GameVersion/VersionedPlan.h"
#include <array>
#include <cstddef>
#include <cstdint>

namespace gc::test_mode_timing {
inline constexpr std::size_t kSoundVtableSlots = 13;
struct TimingNativeLayout final {
    std::size_t sound_form_size{};
    std::size_t form_grid{};
    std::size_t form_children{};
    std::size_t form_row_count{};
    std::size_t form_active_child{};
    std::size_t form_flags{};
    std::size_t grid_row_count{};
    std::size_t grid_column_count{};
    std::size_t grid_selection{};
    std::size_t main_status_window{};
    std::size_t main_help_record{};
    std::size_t main_title_record{};
    std::size_t destructor_slot{};
    std::size_t activate_slot{};
    std::size_t update_slot{};
    std::size_t render_slot{};
    std::size_t confirm_slot{};
    std::size_t back_slot{};
    std::size_t increment_slot{};
    std::size_t decrement_slot{};
};
using GameAllocateFn = void* (__cdecl*)(std::size_t);
using GameDeallocateFn = int (__cdecl*)(void*);
using SoundConstructorFn = void* (__thiscall*)(void*, void*);
using ScalarDeletingDestructorFn =
    void* (__thiscall*)(void*, unsigned char);
using RegisterChildFn = void* (__thiscall*)(void*, int, void*);
using BaseUpdateFn = int (__thiscall*)(void*, int, int);
using SetCellTextFn =
    void* (__thiscall*)(void*, int, int, const unsigned char*);
using SetSelectionFn = int (__thiscall*)(void*, int);
using DrawTitleFn = int (__cdecl*)(
    const unsigned char*,
    const unsigned char*,
    const unsigned char*,
    int);
using SetTitlePositionFn = int (__cdecl*)(int, int);
using DrawHelpFn = int (__cdecl*)(
    const unsigned char*,
    const unsigned char*,
    int,
    int);
using TimingManagerFn = void* (__cdecl*)();
using TimingSetterFn = int (__thiscall*)(void*, int);


using MainConstructorFn = void* (__thiscall*)(void*, void*);
using MainRenderFn = void* (__thiscall*)(void*, int, int);
namespace detail {
struct TimingOriginals final {
    MainConstructorFn main_constructor{};
    MainRenderFn main_render{};
};
extern TimingOriginals g_originals;
}
struct TimingGameAbi {
    TimingNativeLayout layout{};
    GameAllocateFn allocate{};
    GameDeallocateFn deallocate{};
    SoundConstructorFn construct_sound{};
    ScalarDeletingDestructorFn destroy_sound{};
    RegisterChildFn register_child{};
    BaseUpdateFn base_update{};
    SetCellTextFn set_cell_text{};
    SetSelectionFn set_selection{};
    DrawTitleFn draw_title{};
    SetTitlePositionFn set_title_position{};
    DrawHelpFn draw_help{};
    TimingManagerFn get_timing_manager{};
    TimingSetterFn set_judg_time{};
    TimingSetterFn set_game_time{};
    std::array<std::uintptr_t, kSoundVtableSlots> sound_vtable{};
    int* judg_time_offset{};
    int* game_time_offset{};
};


struct TestModeTimingProfile;
[[nodiscard]] std::expected<TimingGameAbi, game_version::PlanError> BuildTimingGameAbi(
    const runtime_image::RuntimeImage&, const TestModeTimingProfile&,
    const game_version::ApprovedVersionedPlan&) noexcept;
[[nodiscard]] bool ApplyLiveTiming(const TimingGameAbi&, TimingOffsets) noexcept;
}

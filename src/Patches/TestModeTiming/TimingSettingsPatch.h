#pragma once

#include "Patches/TestModeTiming/TimingSettingsGameAbi.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace gc::test_mode_timing {

inline constexpr char kTimingTitle[] = "TIMING SETTINGS";
inline constexpr char kTimingMainHelp[] = "EDIT MUSIC/JUDGE OFFSETS";
inline constexpr char kTimingSaveFailureHelp[] =
    "SAVE FAILED - CHECK loader-log.txt";

struct MainRenderRoute {
    int native_selection{};
    bool draw_timing_help{};

    friend bool operator==(
        const MainRenderRoute&,
        const MainRenderRoute&) = default;
};

constexpr MainRenderRoute RouteMainSelection(int selection) noexcept {
    if (selection == 10) {
        return {10, true};
    }
    if (selection == 11) {
        return {10, false};
    }
    return {selection, false};
}

struct CarrierCallbacks {
    std::uintptr_t activate{};
    std::uintptr_t render{};
    std::uintptr_t confirm{};
    std::uintptr_t back{};
    std::uintptr_t increment{};
    std::uintptr_t decrement{};
};

[[nodiscard]] std::array<std::uintptr_t, kSoundVtableSlots>
BuildCarrierVtable(
    std::span<const std::uintptr_t, kSoundVtableSlots> native,
    CarrierCallbacks callbacks,
    std::uintptr_t image_base) noexcept;

struct TimingRenderActions {
    void* context{};
    bool (*draw_title)(void*, const char*) noexcept{};
    bool (*set_title_position)(void*, int, int) noexcept{};
    bool (*set_cell)(
        void*, void*, int, int, const char*) noexcept{};
    bool (*draw_help)(void*, const char*) noexcept{};
};

[[nodiscard]] bool RenderTimingSettings(
    const TimingSettingsModel& model,
    void* grid,
    const TimingRenderActions& actions) noexcept;

[[nodiscard]] bool PrepareCarrierLayout(void* carrier) noexcept;

struct CarrierLifecycleActions {
    void* context{};
    void* (*allocate)(void*, std::size_t) noexcept{};
    void* (*construct)(void*, void*, void*) noexcept{};
    bool (*prepare)(void*, void*) noexcept{};
    bool (*register_child)(void*, void*, int, void*) noexcept{};
    void (*deallocate)(void*, void*) noexcept{};
    void (*destroy)(void*, void*, unsigned char) noexcept{};
};

[[nodiscard]] bool CreateTimingCarrier(
    void* constructor_parent,
    void* owner,
    const CarrierLifecycleActions& actions,
    void** carrier_out) noexcept;

void* __fastcall CarrierActivate(void* self, void*) noexcept;
void* __fastcall CarrierRender(
    void* self,
    void*,
    int frame,
    int input) noexcept;
int __fastcall CarrierConfirm(
    void* self,
    void*,
    int frame,
    int input,
    int selection) noexcept;
int __fastcall CarrierBack(
    void* self,
    void*,
    int frame,
    int input) noexcept;
int __fastcall CarrierIncrement(
    void* self,
    void*,
    int frame,
    int input,
    int selection) noexcept;
int __fastcall CarrierDecrement(
    void* self,
    void*,
    int frame,
    int input,
    int selection) noexcept;

void* __fastcall MainConstructorHook(
    void* self,
    void*,
    void* parent) noexcept;
void* __fastcall MainRenderHook(
    void* self,
    void*,
    int frame,
    int input) noexcept;

[[nodiscard]] bool TimingSettingsPatchInit() noexcept;

} // namespace gc::test_mode_timing

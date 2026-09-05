#pragma once

#include "Patches/TestModeTiming/SystemConfigTimingStore.h"
#include "Patches/TestModeTiming/TimingSettingsGameAbi.h"

#include <array>
// ReSharper disable once CppUnusedIncludeDirective
#include <cstddef>
// ReSharper disable once CppUnusedIncludeDirective
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
    const CarrierCallbacks& callbacks,
    BaseUpdateFn base_update,
    const TimingNativeLayout& layout) noexcept;

[[nodiscard]] bool PrepareCarrierLayout(void* carrier, const TimingNativeLayout& layout) noexcept;

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
    void** carrier_out,
    const TimingNativeLayout& layout) noexcept;

[[nodiscard]] int CancelTimingEdit(TimingSettingsModel& model) noexcept;

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

[[nodiscard]] std::expected<void, game_version::PlanError> PrepareTestModeTimingRuntime(
    const game_version::ApprovedVersionedPlan&, const runtime_image::RuntimeImage&) noexcept;
void CompleteTestModeTimingStartup() noexcept;

} // namespace gc::test_mode_timing

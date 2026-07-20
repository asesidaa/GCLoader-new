#pragma once

#include "Patches/TestModeTiming/TimingSettingsModel.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>

namespace gc::test_mode_timing {

inline constexpr std::uintptr_t kPreferredImageBase = 0x00400000;
inline constexpr std::uint32_t kMainConstructorRva = 0x173EA0;
inline constexpr std::uint32_t kMainRenderRva = 0x173C60;
inline constexpr std::uint32_t kMainRowCountRva = 0x173ED5;
inline constexpr std::uint32_t kSoundConstructorRva = 0x16AE80;
inline constexpr std::uint32_t kGameDeallocatorRva = 0x23BD00;
inline constexpr std::uint32_t kGameAllocatorRva = 0x23BD20;
inline constexpr std::uint32_t kRegisterChildRva = 0x0C2C90;
inline constexpr std::uint32_t kBaseUpdateRva = 0x0C2E40;
inline constexpr std::uint32_t kSetCellTextRva = 0x0C1200;
inline constexpr std::uint32_t kSetSelectionRva = 0x0C1C00;
inline constexpr std::uint32_t kDrawTitleRva = 0x176940;
inline constexpr std::uint32_t kSetTitlePositionRva = 0x176900;
inline constexpr std::uint32_t kDrawHelpRva = 0x176920;
inline constexpr std::uint32_t kTimingManagerRva = 0x001040;
inline constexpr std::uint32_t kJudgTimeSetterRva = 0x259310;
inline constexpr std::uint32_t kGameTimeSetterRva = 0x259350;
inline constexpr std::uint32_t kSoundVtableRva = 0x2FB864;
inline constexpr std::uint32_t kJudgTimeOffsetRva = 0x3D9878;
inline constexpr std::uint32_t kGameTimeOffsetRva = 0x3D987C;

inline constexpr std::size_t kSoundFormSize = 0x1D4;
inline constexpr std::size_t kSoundVtableSlots = 13;
inline constexpr std::size_t kFormGridOffset = 0x28;
inline constexpr std::size_t kFormChildrenOffset = 0x2C;
inline constexpr std::size_t kFormRowCountOffset = 0x30;
inline constexpr std::size_t kFormActiveChildOffset = 0x34;
inline constexpr std::size_t kFormFlagsOffset = 0x38;
inline constexpr std::size_t kGridRowCountOffset = 0x28;
inline constexpr std::size_t kGridColumnCountOffset = 0x2C;
inline constexpr std::size_t kGridSelectionOffset = 0x4C;
inline constexpr std::size_t kMainStatusWindowOffset = 0x3C;
inline constexpr std::size_t kMainHelpRecordOffset = 0x40;
inline constexpr std::size_t kMainTitleRecordOffset = 0x44;

inline constexpr std::size_t kMaximumTimingPatternBytes = 16;
inline constexpr std::size_t kTimingAbiContractCount = 15;
inline constexpr std::size_t kTimingCheckedWriteCount = 1;
inline constexpr std::size_t kTimingHookCount = 2;

inline constexpr std::array<std::uint32_t, kSoundVtableSlots>
kSoundVtableTargetRvas{
    0x06AB20, 0x06AB20, 0x00C9B0, 0x04D070, 0x0C2680,
    0x16B0C0, 0x16B440, 0x16B290, 0x16B230, 0x16AD60,
    0x16AC20, 0x16A9A0, 0x0C2F20,
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

struct TimingBytePattern {
    std::array<std::byte, kMaximumTimingPatternBytes> bytes{};
    std::uint8_t size{};

    [[nodiscard]] std::span<const std::byte> view() const noexcept {
        return {bytes.data(), size};
    }

    friend bool operator==(
        const TimingBytePattern&,
        const TimingBytePattern&) = default;
};

struct TimingByteContract {
    std::uint32_t rva{};
    std::uintptr_t address{};
    TimingBytePattern expected{};
    const char* name{};
};

struct TimingCheckedWrite {
    std::uint32_t rva{};
    std::uintptr_t address{};
    TimingBytePattern expected{};
    TimingBytePattern replacement{};
    const char* name{};
};

struct TimingHookOperation {
    std::uint32_t rva{};
    std::uintptr_t address{};
    const char* name{};
    void* context{};
    bool (*install)(void*) noexcept{};
    void (*reset)(void*) noexcept{};
};

struct TimingMemoryApi {
    bool (*read)(std::uintptr_t, std::span<std::byte>) noexcept{};
    bool (*write)(std::uintptr_t, std::span<const std::byte>) noexcept{};
};

enum class TimingInstallStage {
    None,
    InvalidDescriptor,
    PreflightRead,
    PreflightMismatch,
    DirectWrite,
    HookInstall,
    Rollback,
};

struct TimingInstallError {
    TimingInstallStage stage{TimingInstallStage::None};
    std::size_t operation_index{};
    const char* operation_name{};
    bool rollback_attempted{};
    bool rollback_complete{};
};

class TimingPatchTransaction {
public:
    explicit TimingPatchTransaction(TimingMemoryApi memory) noexcept;

    [[nodiscard]] std::expected<void, TimingInstallError> Install(
        std::span<const TimingByteContract> contracts,
        std::uintptr_t sound_vtable_address,
        std::span<const std::uintptr_t> expected_sound_vtable,
        std::span<const TimingCheckedWrite> writes,
        std::span<const TimingHookOperation> hooks) noexcept;

    [[nodiscard]] std::expected<void, TimingInstallError>
    Rollback() noexcept;

    [[nodiscard]] bool committed() const noexcept { return committed_; }

private:
    [[nodiscard]] bool PatternMatches(
        std::uintptr_t address,
        const TimingBytePattern& pattern) noexcept;
    [[nodiscard]] bool PointerMatches(
        std::uintptr_t address,
        std::uintptr_t expected) noexcept;
    [[nodiscard]] bool VerifyOriginalState() noexcept;
    [[nodiscard]] bool RollbackInternal() noexcept;
    [[nodiscard]] std::expected<void, TimingInstallError> Fail(
        TimingInstallStage stage,
        std::size_t index,
        const char* name) noexcept;

    TimingMemoryApi memory_{};
    std::array<TimingByteContract, kTimingAbiContractCount> contracts_{};
    std::array<std::uintptr_t, kSoundVtableSlots> expected_vtable_{};
    std::array<TimingCheckedWrite, kTimingCheckedWriteCount> writes_{};
    std::array<TimingHookOperation, kTimingHookCount> hooks_{};
    std::uintptr_t vtable_address_{};
    std::size_t contract_count_{};
    std::size_t vtable_count_{};
    std::size_t write_count_{};
    std::size_t hook_count_{};
    std::size_t applied_write_count_{};
    std::size_t installed_hook_count_{};
    bool committed_{};
};

struct TimingGameAbi {
    std::uintptr_t image_base{};
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
    const std::uintptr_t* sound_vtable{};
    int* judg_time_offset{};
    int* game_time_offset{};
};

struct TimingLiveActions {
    void* context{};
    bool (*write_game_time_offset)(void*, int) noexcept{};
    bool (*write_judg_time_offset)(void*, int) noexcept{};
    void* (*get_timing_manager)(void*) noexcept{};
    bool (*set_game_time)(void*, void*, int) noexcept{};
    bool (*set_judg_time)(void*, void*, int) noexcept{};
};

[[nodiscard]] std::array<TimingByteContract, kTimingAbiContractCount>
BuildTimingAbiContracts(std::uintptr_t image_base) noexcept;

[[nodiscard]] std::array<TimingCheckedWrite, kTimingCheckedWriteCount>
BuildTimingCheckedWrites(std::uintptr_t image_base) noexcept;

[[nodiscard]] std::array<std::uintptr_t, kSoundVtableSlots>
ExpectedSoundVtable(std::uintptr_t image_base) noexcept;

[[nodiscard]] TimingGameAbi
BuildTimingGameAbi(std::uintptr_t image_base) noexcept;

[[nodiscard]] TimingMemoryApi ProductionTimingMemoryApi() noexcept;

[[nodiscard]] TimingLiveActions
ProductionTimingLiveActions(std::uintptr_t image_base) noexcept;

[[nodiscard]] bool ApplyLiveTiming(
    TimingOffsets offsets,
    const TimingLiveActions& actions) noexcept;

[[nodiscard]] bool ApplyLiveTiming(
    std::uintptr_t image_base,
    TimingOffsets offsets) noexcept;

} // namespace gc::test_mode_timing

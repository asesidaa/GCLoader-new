#include "Patches/TestModeTiming/TimingSettingsGameAbi.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <initializer_list>

namespace gc::test_mode_timing {

namespace {

TimingBytePattern Pattern(
    std::initializer_list<std::uint8_t> values) noexcept {
    TimingBytePattern pattern{};
    pattern.size = static_cast<std::uint8_t>(values.size());
    std::transform(
        values.begin(), values.end(), pattern.bytes.begin(),
        [](std::uint8_t value) { return static_cast<std::byte>(value); });
    return pattern;
}

TimingByteContract Contract(
    std::uintptr_t base,
    std::uint32_t rva,
    TimingBytePattern expected,
    const char* name) noexcept {
    return {
        .rva = rva,
        .address = base + rva,
        .expected = expected,
        .name = name,
    };
}

bool ProductionRead(
    std::uintptr_t address,
    std::span<std::byte> destination) noexcept {
    __try {
        std::memcpy(
            destination.data(),
            reinterpret_cast<const void*>(address),
            destination.size());
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool CopyExecutableBytes(
    std::uintptr_t address,
    std::span<const std::byte> source) noexcept {
    __try {
        std::memcpy(
            reinterpret_cast<void*>(address),
            source.data(),
            source.size());
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool ProductionWrite(
    std::uintptr_t address,
    std::span<const std::byte> source) noexcept {
    if (source.empty()) {
        return false;
    }

    auto* destination = reinterpret_cast<void*>(address);
    DWORD previous_protection{};
    if (!VirtualProtect(
            destination,
            source.size(),
            PAGE_EXECUTE_READWRITE,
            &previous_protection)) {
        return false;
    }

    const bool copied = CopyExecutableBytes(address, source);
    const bool flushed = copied &&
        FlushInstructionCache(
            GetCurrentProcess(), destination, source.size()) != FALSE;

    DWORD temporary_protection{};
    const bool restored = VirtualProtect(
        destination,
        source.size(),
        previous_protection,
        &temporary_protection) != FALSE;
    return copied && flushed && restored &&
        temporary_protection == PAGE_EXECUTE_READWRITE;
}

std::uintptr_t BaseFromContext(void* context) noexcept {
    return reinterpret_cast<std::uintptr_t>(context);
}

bool ProductionWriteGameOffset(void* context, int value) noexcept {
    __try {
        *reinterpret_cast<int*>(
            BaseFromContext(context) + kGameTimeOffsetRva) = value;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool ProductionWriteJudgOffset(void* context, int value) noexcept {
    __try {
        *reinterpret_cast<int*>(
            BaseFromContext(context) + kJudgTimeOffsetRva) = value;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void* ProductionGetTimingManager(void* context) noexcept {
    __try {
        const auto get_manager = reinterpret_cast<TimingManagerFn>(
            BaseFromContext(context) + kTimingManagerRva);
        return get_manager();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

bool ProductionSetGameTime(
    void* context,
    void* manager,
    int value) noexcept {
    __try {
        const auto setter = reinterpret_cast<TimingSetterFn>(
            BaseFromContext(context) + kGameTimeSetterRva);
        setter(manager, value);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool ProductionSetJudgTime(
    void* context,
    void* manager,
    int value) noexcept {
    __try {
        const auto setter = reinterpret_cast<TimingSetterFn>(
            BaseFromContext(context) + kJudgTimeSetterRva);
        setter(manager, value);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

} // namespace

std::array<TimingByteContract, kTimingAbiContractCount>
BuildTimingAbiContracts(std::uintptr_t image_base) noexcept {
    return {{
        Contract(image_base, kMainConstructorRva, Pattern({
            0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68, 0xA7, 0x9A,
            0x67, 0x00, 0x64, 0xA1, 0x00, 0x00, 0x00, 0x00,
        }), "main constructor"),
        Contract(image_base, kMainRenderRva, Pattern({
            0x55, 0x8B, 0xEC, 0x81, 0xEC, 0x9C, 0x00, 0x00,
            0x00, 0xA1, 0x94, 0x93, 0x77, 0x00, 0x33, 0xC5,
        }), "main render"),
        Contract(image_base, kSoundConstructorRva, Pattern({
            0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68, 0x97, 0x71,
            0x67, 0x00, 0x64, 0xA1, 0x00, 0x00, 0x00, 0x00,
        }), "sound constructor"),
        Contract(image_base, kGameAllocatorRva, Pattern({
            0x55, 0x8B, 0xEC, 0x8B, 0x45, 0x08, 0x50, 0xE8,
            0x94, 0xFE, 0xFF, 0xFF, 0x83, 0xC4, 0x04, 0x5D,
        }), "game allocator"),
        Contract(image_base, kGameDeallocatorRva, Pattern({
            0x55, 0x8B, 0xEC, 0x8B, 0x45, 0x08, 0x50, 0xE8,
            0x44, 0xFE, 0xFF, 0xFF, 0x83, 0xC4, 0x04, 0x5D,
        }), "game deallocator"),
        Contract(image_base, kRegisterChildRva, Pattern({
            0x55, 0x8B, 0xEC, 0x51, 0x89, 0x4D, 0xFC, 0x8B,
            0x45, 0xFC, 0x8B, 0x48, 0x2C, 0x8B, 0x55, 0x08,
        }), "register child"),
        Contract(image_base, kBaseUpdateRva, Pattern({
            0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x0C, 0x89, 0x4D,
            0xF4, 0xC7, 0x45, 0xF8, 0x00, 0x00, 0x00, 0x00,
        }), "base form update"),
        Contract(image_base, kSetCellTextRva, Pattern({
            0x55, 0x8B, 0xEC, 0x51, 0x89, 0x4D, 0xFC, 0x8B,
            0x45, 0xFC, 0x8B, 0x4D, 0x08, 0x3B, 0x48, 0x28,
        }), "set grid cell text"),
        Contract(image_base, kSetSelectionRva, Pattern({
            0x55, 0x8B, 0xEC, 0x51, 0x89, 0x4D, 0xFC, 0x8B,
            0x45, 0xFC, 0x83, 0x78, 0x28, 0x00, 0x75, 0x02,
        }), "set selection"),
        Contract(image_base, kDrawTitleRva, Pattern({
            0x55, 0x8B, 0xEC, 0x83, 0x7D, 0x14, 0x04, 0x75,
            0x07, 0xC7, 0x45, 0x14, 0x00, 0x00, 0x00, 0x00,
        }), "draw title"),
        Contract(image_base, kSetTitlePositionRva, Pattern({
            0x55, 0x8B, 0xEC, 0x8B, 0x45, 0x0C, 0x50, 0x8B,
            0x4D, 0x08, 0x51, 0x8B, 0x0D, 0x64, 0x25, 0x7F,
        }), "set title position"),
        Contract(image_base, kDrawHelpRva, Pattern({
            0x55, 0x8B, 0xEC, 0x8B, 0x45, 0x14, 0x50, 0x8B,
            0x4D, 0x10, 0x51, 0x8B, 0x55, 0x0C, 0x52,
        }), "draw help"),
        Contract(image_base, kTimingManagerRva, Pattern({
            0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68, 0x8E, 0xD6,
            0x67, 0x00, 0x64, 0xA1, 0x00, 0x00, 0x00, 0x00,
        }), "timing manager accessor"),
        Contract(image_base, kJudgTimeSetterRva, Pattern({
            0x55, 0x8B, 0xEC, 0x51, 0x89, 0x4D, 0xFC, 0x8B,
            0x4D, 0xFC, 0xE8, 0xB1, 0x7D, 0xDA, 0xFF, 0x0F,
        }), "judgment timing setter"),
        Contract(image_base, kGameTimeSetterRva, Pattern({
            0x55, 0x8B, 0xEC, 0x51, 0x89, 0x4D, 0xFC, 0x8B,
            0x4D, 0xFC, 0xE8, 0x71, 0x7D, 0xDA, 0xFF, 0x0F,
        }), "game timing setter"),
    }};
}

std::array<TimingCheckedWrite, kTimingCheckedWriteCount>
BuildTimingCheckedWrites(std::uintptr_t image_base) noexcept {
    return {{{
        .rva = kMainRowCountRva,
        .address = image_base + kMainRowCountRva,
        .expected = Pattern({0x6A, 0x0B}),
        .replacement = Pattern({0x6A, 0x0C}),
        .name = "main row count",
    }}};
}

std::array<std::uintptr_t, kSoundVtableSlots>
ExpectedSoundVtable(std::uintptr_t image_base) noexcept {
    std::array<std::uintptr_t, kSoundVtableSlots> result{};
    std::transform(
        kSoundVtableTargetRvas.begin(),
        kSoundVtableTargetRvas.end(),
        result.begin(),
        [image_base](std::uint32_t rva) {
            return image_base + rva;
        });
    return result;
}

TimingGameAbi BuildTimingGameAbi(std::uintptr_t image_base) noexcept {
    return {
        .image_base = image_base,
        .allocate = reinterpret_cast<GameAllocateFn>(
            image_base + kGameAllocatorRva),
        .deallocate = reinterpret_cast<GameDeallocateFn>(
            image_base + kGameDeallocatorRva),
        .construct_sound = reinterpret_cast<SoundConstructorFn>(
            image_base + kSoundConstructorRva),
        .destroy_sound = reinterpret_cast<ScalarDeletingDestructorFn>(
            image_base + kSoundVtableTargetRvas[3]),
        .register_child = reinterpret_cast<RegisterChildFn>(
            image_base + kRegisterChildRva),
        .base_update = reinterpret_cast<BaseUpdateFn>(
            image_base + kBaseUpdateRva),
        .set_cell_text = reinterpret_cast<SetCellTextFn>(
            image_base + kSetCellTextRva),
        .set_selection = reinterpret_cast<SetSelectionFn>(
            image_base + kSetSelectionRva),
        .draw_title = reinterpret_cast<DrawTitleFn>(
            image_base + kDrawTitleRva),
        .set_title_position = reinterpret_cast<SetTitlePositionFn>(
            image_base + kSetTitlePositionRva),
        .draw_help = reinterpret_cast<DrawHelpFn>(
            image_base + kDrawHelpRva),
        .get_timing_manager = reinterpret_cast<TimingManagerFn>(
            image_base + kTimingManagerRva),
        .set_judg_time = reinterpret_cast<TimingSetterFn>(
            image_base + kJudgTimeSetterRva),
        .set_game_time = reinterpret_cast<TimingSetterFn>(
            image_base + kGameTimeSetterRva),
        .sound_vtable = reinterpret_cast<const std::uintptr_t*>(
            image_base + kSoundVtableRva),
        .judg_time_offset = reinterpret_cast<int*>(
            image_base + kJudgTimeOffsetRva),
        .game_time_offset = reinterpret_cast<int*>(
            image_base + kGameTimeOffsetRva),
    };
}

TimingPatchTransaction::TimingPatchTransaction(
    TimingMemoryApi memory) noexcept
    : memory_{memory} {
}

std::expected<void, TimingInstallError> TimingPatchTransaction::Install(
    std::span<const TimingByteContract> contracts,
    std::uintptr_t sound_vtable_address,
    std::span<const std::uintptr_t> expected_sound_vtable,
    std::span<const TimingCheckedWrite> writes,
    std::span<const TimingHookOperation> hooks) noexcept {
    if (committed_ || memory_.read == nullptr || memory_.write == nullptr ||
        contracts.size() != contracts_.size() ||
        expected_sound_vtable.size() != expected_vtable_.size() ||
        writes.size() != writes_.size() || hooks.size() != hooks_.size() ||
        sound_vtable_address == 0) {
        return std::unexpected(TimingInstallError{
            .stage = TimingInstallStage::InvalidDescriptor,
        });
    }

    for (std::size_t index = 0; index < contracts.size(); ++index) {
        const auto& contract = contracts[index];
        if (contract.address == 0 || contract.name == nullptr ||
            contract.expected.size == 0 ||
            contract.expected.size > kMaximumTimingPatternBytes) {
            return std::unexpected(TimingInstallError{
                .stage = TimingInstallStage::InvalidDescriptor,
                .operation_index = index,
                .operation_name = contract.name,
            });
        }
    }
    for (std::size_t index = 0; index < writes.size(); ++index) {
        const auto& write = writes[index];
        if (write.address == 0 || write.name == nullptr ||
            write.expected.size == 0 ||
            write.expected.size != write.replacement.size ||
            write.expected.size > kMaximumTimingPatternBytes) {
            return std::unexpected(TimingInstallError{
                .stage = TimingInstallStage::InvalidDescriptor,
                .operation_index = index,
                .operation_name = write.name,
            });
        }
    }
    for (std::size_t index = 0; index < hooks.size(); ++index) {
        const auto& hook = hooks[index];
        const auto expected_rva =
            index == 0 ? kMainConstructorRva : kMainRenderRva;
        const auto contract = std::ranges::find_if(
            contracts,
            [&hook](const TimingByteContract& candidate) {
                return candidate.rva == hook.rva;
            });
        if (hook.rva != expected_rva || hook.address == 0 ||
            hook.name == nullptr || hook.install == nullptr ||
            hook.reset == nullptr || contract == contracts.end() ||
            contract->address != hook.address) {
            return std::unexpected(TimingInstallError{
                .stage = TimingInstallStage::InvalidDescriptor,
                .operation_index = index,
                .operation_name = hook.name,
            });
        }
    }

    for (std::size_t index = 0; index < contracts.size(); ++index) {
        const auto& contract = contracts[index];
        std::array<std::byte, kMaximumTimingPatternBytes> actual{};
        const auto destination = std::span{
            actual.data(),
            static_cast<std::size_t>(contract.expected.size),
        };
        if (!memory_.read(contract.address, destination)) {
            return std::unexpected(TimingInstallError{
                .stage = TimingInstallStage::PreflightRead,
                .operation_index = index,
                .operation_name = contract.name,
            });
        }
        if (!std::ranges::equal(destination, contract.expected.view())) {
            return std::unexpected(TimingInstallError{
                .stage = TimingInstallStage::PreflightMismatch,
                .operation_index = index,
                .operation_name = contract.name,
            });
        }
    }

    for (std::size_t index = 0; index < expected_sound_vtable.size(); ++index) {
        const auto address =
            sound_vtable_address + index * sizeof(std::uintptr_t);
        std::uintptr_t actual{};
        auto destination = std::as_writable_bytes(
            std::span{&actual, std::size_t{1}});
        if (!memory_.read(address, destination)) {
            return std::unexpected(TimingInstallError{
                .stage = TimingInstallStage::PreflightRead,
                .operation_index = index,
                .operation_name = "sound vtable",
            });
        }
        if (actual != expected_sound_vtable[index]) {
            return std::unexpected(TimingInstallError{
                .stage = TimingInstallStage::PreflightMismatch,
                .operation_index = index,
                .operation_name = "sound vtable",
            });
        }
    }

    for (std::size_t index = 0; index < writes.size(); ++index) {
        const auto& write = writes[index];
        std::array<std::byte, kMaximumTimingPatternBytes> actual{};
        const auto destination = std::span{
            actual.data(),
            static_cast<std::size_t>(write.expected.size),
        };
        if (!memory_.read(write.address, destination)) {
            return std::unexpected(TimingInstallError{
                .stage = TimingInstallStage::PreflightRead,
                .operation_index = index,
                .operation_name = write.name,
            });
        }
        if (!std::ranges::equal(destination, write.expected.view())) {
            return std::unexpected(TimingInstallError{
                .stage = TimingInstallStage::PreflightMismatch,
                .operation_index = index,
                .operation_name = write.name,
            });
        }
    }

    std::copy(contracts.begin(), contracts.end(), contracts_.begin());
    std::copy(
        expected_sound_vtable.begin(),
        expected_sound_vtable.end(),
        expected_vtable_.begin());
    std::copy(writes.begin(), writes.end(), writes_.begin());
    std::copy(hooks.begin(), hooks.end(), hooks_.begin());
    contract_count_ = contracts.size();
    vtable_count_ = expected_sound_vtable.size();
    write_count_ = writes.size();
    hook_count_ = hooks.size();
    vtable_address_ = sound_vtable_address;

    for (std::size_t index = 0; index < hook_count_; ++index) {
        installed_hook_count_ = index + 1;
        if (!hooks_[index].install(hooks_[index].context)) {
            return Fail(
                TimingInstallStage::HookInstall,
                index,
                hooks_[index].name);
        }
    }

    for (std::size_t index = 0; index < write_count_; ++index) {
        applied_write_count_ = index + 1;
        if (!memory_.write(
                writes_[index].address,
                writes_[index].replacement.view())) {
            return Fail(
                TimingInstallStage::DirectWrite,
                index,
                writes_[index].name);
        }
    }

    committed_ = true;
    return {};
}

bool TimingPatchTransaction::PatternMatches(
    std::uintptr_t address,
    const TimingBytePattern& pattern) noexcept {
    if (pattern.size == 0 || pattern.size > kMaximumTimingPatternBytes) {
        return false;
    }
    std::array<std::byte, kMaximumTimingPatternBytes> actual{};
    const auto destination = std::span{
        actual.data(), static_cast<std::size_t>(pattern.size)};
    return memory_.read(address, destination) &&
        std::ranges::equal(destination, pattern.view());
}

bool TimingPatchTransaction::PointerMatches(
    std::uintptr_t address,
    std::uintptr_t expected) noexcept {
    std::uintptr_t actual{};
    auto destination = std::as_writable_bytes(
        std::span{&actual, std::size_t{1}});
    return memory_.read(address, destination) && actual == expected;
}

bool TimingPatchTransaction::VerifyOriginalState() noexcept {
    for (std::size_t index = 0; index < contract_count_; ++index) {
        if (!PatternMatches(
                contracts_[index].address,
                contracts_[index].expected)) {
            return false;
        }
    }
    for (std::size_t index = 0; index < vtable_count_; ++index) {
        if (!PointerMatches(
                vtable_address_ + index * sizeof(std::uintptr_t),
                expected_vtable_[index])) {
            return false;
        }
    }
    for (std::size_t index = 0; index < write_count_; ++index) {
        if (!PatternMatches(
                writes_[index].address,
                writes_[index].expected)) {
            return false;
        }
    }
    return true;
}

bool TimingPatchTransaction::RollbackInternal() noexcept {
    bool writes_restored = true;
    while (applied_write_count_ != 0) {
        const auto index = --applied_write_count_;
        if (!memory_.write(
                writes_[index].address,
                writes_[index].expected.view())) {
            writes_restored = false;
        }
    }

    while (installed_hook_count_ != 0) {
        const auto index = --installed_hook_count_;
        hooks_[index].reset(hooks_[index].context);
    }

    const bool verified = VerifyOriginalState();
    contract_count_ = 0;
    vtable_count_ = 0;
    write_count_ = 0;
    hook_count_ = 0;
    vtable_address_ = 0;
    committed_ = false;
    return writes_restored && verified;
}

std::expected<void, TimingInstallError>
TimingPatchTransaction::Rollback() noexcept {
    const bool complete = RollbackInternal();
    if (!complete) {
        return std::unexpected(TimingInstallError{
            .stage = TimingInstallStage::Rollback,
            .operation_name = "timing patch rollback",
            .rollback_attempted = true,
            .rollback_complete = false,
        });
    }
    return {};
}

std::expected<void, TimingInstallError> TimingPatchTransaction::Fail(
    TimingInstallStage stage,
    std::size_t index,
    const char* name) noexcept {
    const bool complete = RollbackInternal();
    return std::unexpected(TimingInstallError{
        .stage = stage,
        .operation_index = index,
        .operation_name = name,
        .rollback_attempted = true,
        .rollback_complete = complete,
    });
}

TimingMemoryApi ProductionTimingMemoryApi() noexcept {
    return {ProductionRead, ProductionWrite};
}

TimingLiveActions
ProductionTimingLiveActions(std::uintptr_t image_base) noexcept {
    return {
        .context = reinterpret_cast<void*>(image_base),
        .write_game_time_offset = ProductionWriteGameOffset,
        .write_judg_time_offset = ProductionWriteJudgOffset,
        .get_timing_manager = ProductionGetTimingManager,
        .set_game_time = ProductionSetGameTime,
        .set_judg_time = ProductionSetJudgTime,
    };
}

bool ApplyLiveTiming(
    TimingOffsets offsets,
    const TimingLiveActions& actions) noexcept {
    if (actions.write_game_time_offset == nullptr ||
        actions.write_judg_time_offset == nullptr ||
        actions.get_timing_manager == nullptr ||
        actions.set_game_time == nullptr ||
        actions.set_judg_time == nullptr) {
        return false;
    }
    if (!actions.write_game_time_offset(
            actions.context, offsets.game_ms) ||
        !actions.write_judg_time_offset(
            actions.context, offsets.judge_ms)) {
        return false;
    }
    void* manager = actions.get_timing_manager(actions.context);
    return manager != nullptr &&
        actions.set_game_time(
            actions.context, manager, offsets.game_ms) &&
        actions.set_judg_time(
            actions.context, manager, offsets.judge_ms);
}

bool ApplyLiveTiming(
    std::uintptr_t image_base,
    TimingOffsets offsets) noexcept {
    return ApplyLiveTiming(
        offsets, ProductionTimingLiveActions(image_base));
}

} // namespace gc::test_mode_timing

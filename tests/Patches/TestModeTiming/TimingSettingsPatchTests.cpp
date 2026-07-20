#include "Patches/TestModeTiming/TimingSettingsGameAbi.h"
#include "Patches/TestModeTiming/TimingSettingsPatch.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <iostream>
#include <ranges>
#include <span>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

using namespace gc::test_mode_timing;

namespace {

constexpr std::uintptr_t kFakeBase = 0x10000000;

int Expect(bool condition, const char* name) {
    if (condition) {
        return 0;
    }
    std::cerr << "Expectation failed: " << name << '\n';
    return 1;
}

TimingBytePattern Pattern(std::initializer_list<std::uint8_t> values) {
    TimingBytePattern pattern{};
    pattern.size = static_cast<std::uint8_t>(values.size());
    std::transform(
        values.begin(), values.end(), pattern.bytes.begin(),
        [](std::uint8_t value) { return static_cast<std::byte>(value); });
    return pattern;
}

const TimingByteContract& FindContract(
    std::span<const TimingByteContract> contracts,
    std::uint32_t rva) {
    const auto found = std::find_if(
        contracts.begin(), contracts.end(),
        [rva](const TimingByteContract& contract) {
            return contract.rva == rva;
        });
    if (found == contracts.end()) {
        std::abort();
    }
    return *found;
}

struct FakeMemory {
    std::unordered_map<std::uintptr_t, std::byte> bytes;
    int fail_read_call{-1};
    int fail_write_call{-1};
    int read_calls{};
    int write_calls{};
    std::vector<std::string> events;
    std::uintptr_t row_address{};
    TimingBytePattern row_original{};
    TimingBytePattern row_replacement{};
};

FakeMemory* g_memory = nullptr;

void StoreBytes(
    FakeMemory& memory,
    std::uintptr_t address,
    std::span<const std::byte> bytes) {
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        memory.bytes[address + index] = bytes[index];
    }
}

template <typename T>
void StoreValue(FakeMemory& memory, std::uintptr_t address, const T& value) {
    const auto bytes = std::as_bytes(std::span{&value, std::size_t{1}});
    StoreBytes(memory, address, bytes);
}

bool FakeRead(
    std::uintptr_t address,
    std::span<std::byte> destination) noexcept {
    const int call = g_memory->read_calls++;
    if (call == g_memory->fail_read_call) {
        return false;
    }
    for (std::size_t index = 0; index < destination.size(); ++index) {
        const auto found = g_memory->bytes.find(address + index);
        if (found == g_memory->bytes.end()) {
            return false;
        }
        destination[index] = found->second;
    }
    return true;
}

bool FakeWrite(
    std::uintptr_t address,
    std::span<const std::byte> source) noexcept {
    const int call = g_memory->write_calls++;
    const bool restore =
        address == g_memory->row_address &&
        std::ranges::equal(source, g_memory->row_original.view());
    g_memory->events.emplace_back(restore ? "restore row" : "write row");
    if (call == g_memory->fail_write_call) {
        return false;
    }
    StoreBytes(*g_memory, address, source);
    return true;
}

struct FakeHooks {
    int install_call{};
    int fail_on_call{-1};
    std::vector<std::size_t> installed;
    std::vector<std::size_t> reset;
    std::vector<std::string>* events{};
};

struct FakeHookContext {
    FakeHooks* hooks{};
    std::size_t index{};
};

bool InstallFakeHook(void* opaque) noexcept {
    auto& context = *static_cast<FakeHookContext*>(opaque);
    const int call = context.hooks->install_call++;
    context.hooks->events->emplace_back(
        context.index == 0 ? "install constructor" : "install render");
    if (call == context.hooks->fail_on_call) {
        return false;
    }
    context.hooks->installed.push_back(context.index);
    return true;
}

void ResetFakeHook(void* opaque) noexcept {
    auto& context = *static_cast<FakeHookContext*>(opaque);
    context.hooks->reset.push_back(context.index);
    context.hooks->events->emplace_back(
        context.index == 0 ? "reset constructor" : "reset render");
}

struct Fixture {
    std::array<TimingByteContract, kTimingAbiContractCount> contracts{
        BuildTimingAbiContracts(kFakeBase)};
    std::array<std::uintptr_t, kSoundVtableSlots> vtable{
        ExpectedSoundVtable(kFakeBase)};
    std::array<TimingCheckedWrite, kTimingCheckedWriteCount> writes{
        BuildTimingCheckedWrites(kFakeBase)};
    FakeMemory memory{};
    FakeHooks hook_state{};
    std::array<FakeHookContext, kTimingHookCount> hook_contexts{};
    std::array<TimingHookOperation, kTimingHookCount> hooks{};

    Fixture() {
        for (const auto& contract : contracts) {
            StoreBytes(memory, contract.address, contract.expected.view());
        }
        for (std::size_t index = 0; index < vtable.size(); ++index) {
            StoreValue(
                memory,
                kFakeBase + kSoundVtableRva +
                    index * sizeof(std::uintptr_t),
                vtable[index]);
        }
        for (const auto& write : writes) {
            StoreBytes(memory, write.address, write.expected.view());
        }

        memory.row_address = writes[0].address;
        memory.row_original = writes[0].expected;
        memory.row_replacement = writes[0].replacement;
        hook_state.events = &memory.events;
        hook_contexts = {{{&hook_state, 0}, {&hook_state, 1}}};
        hooks = {{
            {
                .rva = kMainConstructorRva,
                .address = kFakeBase + kMainConstructorRva,
                .name = "main constructor",
                .context = &hook_contexts[0],
                .install = InstallFakeHook,
                .reset = ResetFakeHook,
            },
            {
                .rva = kMainRenderRva,
                .address = kFakeBase + kMainRenderRva,
                .name = "main render",
                .context = &hook_contexts[1],
                .install = InstallFakeHook,
                .reset = ResetFakeHook,
            },
        }};
    }

    [[nodiscard]] std::expected<void, TimingInstallError> Install(
        TimingPatchTransaction& transaction) {
        g_memory = &memory;
        return InstallTimingPatch(transaction, kFakeBase, hooks);
    }
};

struct LiveState {
    std::vector<std::string> events;
    int game_offset{};
    int judg_offset{};
    void* manager{reinterpret_cast<void*>(0x12345678)};
};

bool WriteGameOffset(void* opaque, int value) noexcept {
    auto& state = *static_cast<LiveState*>(opaque);
    state.events.emplace_back("write GameTimeOffset");
    state.game_offset = value;
    return true;
}

bool WriteJudgOffset(void* opaque, int value) noexcept {
    auto& state = *static_cast<LiveState*>(opaque);
    state.events.emplace_back("write JudgTimeOffset");
    state.judg_offset = value;
    return true;
}

void* GetManager(void* opaque) noexcept {
    auto& state = *static_cast<LiveState*>(opaque);
    state.events.emplace_back("get timing manager");
    return state.manager;
}

bool SetGameTime(void* opaque, void* manager, int value) noexcept {
    auto& state = *static_cast<LiveState*>(opaque);
    state.events.emplace_back("set GameTime");
    return manager == state.manager && value == state.game_offset;
}

bool SetJudgTime(void* opaque, void* manager, int value) noexcept {
    auto& state = *static_cast<LiveState*>(opaque);
    state.events.emplace_back("set JudgTime");
    return manager == state.manager && value == state.judg_offset;
}

struct CellEvent {
    void* grid{};
    int row{};
    int column{};
    std::string text;

    friend bool operator==(const CellEvent&, const CellEvent&) = default;
};

struct RenderState {
    int title_calls{};
    std::string title;
    int title_x{};
    int title_y{};
    std::vector<CellEvent> cells;
    std::string help;
};

bool RecordTitle(void* opaque, const char* text) noexcept {
    auto& state = *static_cast<RenderState*>(opaque);
    ++state.title_calls;
    state.title = text;
    return true;
}

bool RecordTitlePosition(void* opaque, int x, int y) noexcept {
    auto& state = *static_cast<RenderState*>(opaque);
    state.title_x = x;
    state.title_y = y;
    return true;
}

bool RecordCell(
    void* opaque,
    void* grid,
    int row,
    int column,
    const char* text) noexcept {
    auto& state = *static_cast<RenderState*>(opaque);
    state.cells.push_back({grid, row, column, text});
    return true;
}

bool RecordHelp(void* opaque, const char* text) noexcept {
    auto& state = *static_cast<RenderState*>(opaque);
    state.help = text;
    return true;
}

TimingRenderActions RenderActions(RenderState& state) {
    return {
        .context = &state,
        .draw_title = RecordTitle,
        .set_title_position = RecordTitlePosition,
        .set_cell = RecordCell,
        .draw_help = RecordHelp,
    };
}

enum class CarrierFailPoint {
    None,
    Allocate,
    Construct,
    Prepare,
    Register,
};

struct CarrierLifeState {
    CarrierFailPoint failure{};
    std::array<std::byte, kSoundFormSize> storage{};
    int allocate_calls{};
    int construct_calls{};
    int prepare_calls{};
    int register_calls{};
    int deallocate_calls{};
    int destroy_calls{};
    unsigned char destroy_flag{};
};

void* LifeAllocate(void* opaque, std::size_t size) noexcept {
    auto& state = *static_cast<CarrierLifeState*>(opaque);
    ++state.allocate_calls;
    if (state.failure == CarrierFailPoint::Allocate ||
        size != kSoundFormSize) {
        return nullptr;
    }
    return state.storage.data();
}

void* LifeConstruct(void* opaque, void* raw, void* parent) noexcept {
    auto& state = *static_cast<CarrierLifeState*>(opaque);
    ++state.construct_calls;
    if (state.failure == CarrierFailPoint::Construct) {
        return nullptr;
    }
    return parent == reinterpret_cast<void*>(0xCAFEBABE) ? raw : nullptr;
}

bool LifePrepare(void* opaque, void* carrier) noexcept {
    auto& state = *static_cast<CarrierLifeState*>(opaque);
    ++state.prepare_calls;
    return state.failure != CarrierFailPoint::Prepare &&
        carrier == state.storage.data();
}

bool LifeRegister(
    void* opaque,
    void* owner,
    int index,
    void* carrier) noexcept {
    auto& state = *static_cast<CarrierLifeState*>(opaque);
    ++state.register_calls;
    return state.failure != CarrierFailPoint::Register &&
        owner == reinterpret_cast<void*>(0xDEADC0DE) && index == 10 &&
        carrier == state.storage.data();
}

void LifeDeallocate(void* opaque, void* raw) noexcept {
    auto& state = *static_cast<CarrierLifeState*>(opaque);
    if (raw == state.storage.data()) {
        ++state.deallocate_calls;
    }
}

void LifeDestroy(
    void* opaque,
    void* carrier,
    unsigned char flag) noexcept {
    auto& state = *static_cast<CarrierLifeState*>(opaque);
    if (carrier == state.storage.data()) {
        ++state.destroy_calls;
        state.destroy_flag = flag;
    }
}

CarrierLifecycleActions LifeActions(CarrierLifeState& state) {
    return {
        .context = &state,
        .allocate = LifeAllocate,
        .construct = LifeConstruct,
        .prepare = LifePrepare,
        .register_child = LifeRegister,
        .deallocate = LifeDeallocate,
        .destroy = LifeDestroy,
    };
}

struct CommitState {
    std::vector<std::string> events;
    SaveOutcome save_outcome{SaveOutcome::Changed};
    bool save_succeeds{true};
    bool apply_succeeds{true};
    TimingOffsets saved{};
    TimingOffsets applied{};
};

bool CommitSave(
    void* opaque,
    TimingOffsets offsets,
    SaveOutcome* outcome) noexcept {
    auto& state = *static_cast<CommitState*>(opaque);
    state.events.emplace_back("save");
    state.saved = offsets;
    if (!state.save_succeeds) {
        return false;
    }
    *outcome = state.save_outcome;
    return true;
}

bool CommitApply(void* opaque, TimingOffsets offsets) noexcept {
    auto& state = *static_cast<CommitState*>(opaque);
    state.applied = offsets;
    if (!state.apply_succeeds) {
        state.events.emplace_back("apply failure");
        return false;
    }
    state.events.emplace_back("write GameTimeOffset");
    state.events.emplace_back("write JudgTimeOffset");
    state.events.emplace_back("get timing manager");
    state.events.emplace_back("set GameTime");
    state.events.emplace_back("set JudgTime");
    return true;
}

void CommitStatus(void* opaque, SaveStatus status) noexcept {
    auto& state = *static_cast<CommitState*>(opaque);
    state.events.emplace_back(
        status == SaveStatus::Succeeded
            ? "mark succeeded"
            : "mark failed");
}

void CommitSaveFailure(void* opaque) noexcept {
    auto& state = *static_cast<CommitState*>(opaque);
    state.events.emplace_back("log save failure");
}

void CommitApplyFailure(void* opaque) noexcept {
    auto& state = *static_cast<CommitState*>(opaque);
    state.events.emplace_back("log fatal");
}

void CommitSuccess(
    void*,
    TimingOffsets,
    TimingOffsets,
    SaveOutcome) noexcept {
}

TimingCommitActions CommitActions(CommitState& state) {
    return {
        .context = &state,
        .save = CommitSave,
        .apply_live = CommitApply,
        .status_changed = CommitStatus,
        .save_failed = CommitSaveFailure,
        .apply_failed = CommitApplyFailure,
        .save_succeeded = CommitSuccess,
    };
}

} // namespace

int main() {
    int failures = 0;

    static_assert(sizeof(std::uintptr_t) == 4);
    static_assert(kTimingAbiContractCount == 15);
    static_assert(kTimingCheckedWriteCount == 1);
    static_assert(kTimingHookCount == 2);

    const auto contracts = BuildTimingAbiContracts(kFakeBase);
    failures += Expect(
        contracts.size() == 15,
        "all native ABI entry contracts are present");
    failures += Expect(
        FindContract(contracts, 0x173EA0).expected == Pattern({
            0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68, 0xA7, 0x9A,
            0x67, 0x00, 0x64, 0xA1, 0x00, 0x00, 0x00, 0x00,
        }),
        "main constructor signature is exact");
    failures += Expect(
        FindContract(contracts, 0x173C60).expected == Pattern({
            0x55, 0x8B, 0xEC, 0x81, 0xEC, 0x9C, 0x00, 0x00,
            0x00, 0xA1, 0x94, 0x93, 0x77, 0x00, 0x33, 0xC5,
        }),
        "main render signature is exact");
    failures += Expect(
        FindContract(contracts, kSoundConstructorRva).expected == Pattern({
            0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68, 0x97, 0x71,
            0x67, 0x00, 0x64, 0xA1, 0x00, 0x00, 0x00, 0x00,
        }),
        "sound constructor signature is exact");

    const auto writes = BuildTimingCheckedWrites(kFakeBase);
    failures += Expect(
        writes.size() == 1 &&
            writes[0].rva == 0x173ED5 &&
            writes[0].expected == Pattern({0x6A, 0x0B}) &&
            writes[0].replacement == Pattern({0x6A, 0x0C}),
        "row-count write is exact");

    const auto vtable = ExpectedSoundVtable(kFakeBase);
    failures += Expect(
        vtable.size() == 13 &&
            vtable[3] == kFakeBase + 0x04D070 &&
            vtable[12] == kFakeBase + 0x0C2F20,
        "native destructor and dispatcher targets are exact");

    for (int failed_read = 0;
         failed_read < static_cast<int>(
             kTimingAbiContractCount + kSoundVtableSlots +
             kTimingCheckedWriteCount);
         ++failed_read) {
        Fixture fixture;
        fixture.memory.fail_read_call = failed_read;
        TimingPatchTransaction transaction({FakeRead, FakeWrite});
        const auto result = fixture.Install(transaction);
        failures += Expect(
            !result &&
                result.error().stage == TimingInstallStage::PreflightRead &&
                fixture.memory.write_calls == 0 &&
                fixture.hook_state.installed.empty() &&
                fixture.hook_state.reset.empty(),
            "every preflight read failure mutates nothing");
    }

    for (std::size_t index = 0; index < kTimingAbiContractCount; ++index) {
        Fixture fixture;
        fixture.memory.bytes[fixture.contracts[index].address] ^=
            std::byte{0xFF};
        TimingPatchTransaction transaction({FakeRead, FakeWrite});
        const auto result = fixture.Install(transaction);
        failures += Expect(
            !result &&
                result.error().stage ==
                    TimingInstallStage::PreflightMismatch &&
                fixture.memory.write_calls == 0 &&
                fixture.hook_state.installed.empty(),
            "every ABI mismatch mutates nothing");
    }

    for (std::size_t index = 0; index < kSoundVtableSlots; ++index) {
        Fixture fixture;
        ++fixture.vtable[index];
        StoreValue(
            fixture.memory,
            kFakeBase + kSoundVtableRva +
                index * sizeof(std::uintptr_t),
            fixture.vtable[index]);
        fixture.vtable = ExpectedSoundVtable(kFakeBase);
        TimingPatchTransaction transaction({FakeRead, FakeWrite});
        const auto result = fixture.Install(transaction);
        failures += Expect(
            !result &&
                result.error().stage ==
                    TimingInstallStage::PreflightMismatch &&
                fixture.memory.write_calls == 0 &&
                fixture.hook_state.installed.empty(),
            "every native vtable mismatch mutates nothing");
    }

    {
        Fixture fixture;
        fixture.memory.bytes[fixture.writes[0].address] = std::byte{0xFF};
        TimingPatchTransaction transaction({FakeRead, FakeWrite});
        const auto result = fixture.Install(transaction);
        failures += Expect(
            !result &&
                result.error().stage ==
                    TimingInstallStage::PreflightMismatch &&
                fixture.memory.write_calls == 0 &&
                fixture.hook_state.installed.empty(),
            "row-count mismatch mutates nothing");
    }

    {
        Fixture fixture;
        fixture.hooks[0].address += 4;
        TimingPatchTransaction transaction({FakeRead, FakeWrite});
        const auto result = fixture.Install(transaction);
        failures += Expect(
            !result &&
                result.error().stage ==
                    TimingInstallStage::InvalidDescriptor &&
                fixture.memory.read_calls == 0 &&
                fixture.memory.write_calls == 0 &&
                fixture.hook_state.installed.empty(),
            "hook must target the ABI address that was preflighted");
    }

    for (int failed_hook = 0;
         failed_hook < static_cast<int>(kTimingHookCount);
         ++failed_hook) {
        Fixture fixture;
        fixture.hook_state.fail_on_call = failed_hook;
        TimingPatchTransaction transaction({FakeRead, FakeWrite});
        const auto result = fixture.Install(transaction);
        const std::vector<std::size_t> expected_reset =
            failed_hook == 0
                ? std::vector<std::size_t>{0}
                : std::vector<std::size_t>{1, 0};
        failures += Expect(
            !result &&
                result.error().stage == TimingInstallStage::HookInstall &&
                result.error().operation_index ==
                    static_cast<std::size_t>(failed_hook) &&
                result.error().rollback_complete &&
                fixture.memory.write_calls == 0 &&
                fixture.hook_state.reset == expected_reset,
            "hook failure resets failed and installed hooks in reverse order");
    }

    {
        Fixture fixture;
        fixture.memory.fail_write_call = 0;
        TimingPatchTransaction transaction({FakeRead, FakeWrite});
        const auto result = fixture.Install(transaction);
        failures += Expect(
            !result &&
                result.error().stage == TimingInstallStage::DirectWrite &&
                result.error().rollback_complete &&
                fixture.hook_state.reset ==
                    std::vector<std::size_t>{1, 0} &&
                fixture.memory.events == std::vector<std::string>{
                    "install constructor", "install render", "write row",
                    "restore row", "reset render", "reset constructor",
                },
            "row write failure restores bytes before both hooks");
    }

    {
        Fixture fixture;
        TimingPatchTransaction transaction({FakeRead, FakeWrite});
        const auto installed = fixture.Install(transaction);
        fixture.memory.events.clear();
        const auto rolled_back = transaction.Rollback();
        failures += Expect(
            installed.has_value() && transaction.committed() == false &&
                rolled_back.has_value() &&
                fixture.memory.events == std::vector<std::string>{
                    "restore row", "reset render", "reset constructor",
                },
            "explicit rollback restores row before hooks in reverse order");
    }

    {
        Fixture fixture;
        TimingPatchTransaction transaction({FakeRead, FakeWrite});
        const auto installed = fixture.Install(transaction);
        fixture.memory.fail_write_call = fixture.memory.write_calls;
        const auto rolled_back = transaction.Rollback();
        failures += Expect(
            installed.has_value() && !rolled_back &&
                rolled_back.error().stage == TimingInstallStage::Rollback &&
                rolled_back.error().rollback_attempted &&
                !rolled_back.error().rollback_complete,
            "restore failure reports incomplete rollback");
    }

    {
        LiveState state;
        const TimingLiveActions actions{
            .context = &state,
            .write_game_time_offset = WriteGameOffset,
            .write_judg_time_offset = WriteJudgOffset,
            .get_timing_manager = GetManager,
            .set_game_time = SetGameTime,
            .set_judg_time = SetJudgTime,
        };
        failures += Expect(
            ApplyLiveTiming({.game_ms = 7, .judge_ms = -12}, actions) &&
                state.game_offset == 7 && state.judg_offset == -12 &&
                state.events == std::vector<std::string>{
                    "write GameTimeOffset", "write JudgTimeOffset",
                    "get timing manager", "set GameTime", "set JudgTime",
                },
            "live timing uses startup application order");
    }

    for (int index = 0; index != 10; ++index) {
        const auto route = RouteMainSelection(index);
        failures += Expect(
            route.native_selection == index && !route.draw_timing_help,
            "native main entries remain identity mapped");
    }
    failures += Expect(
        RouteMainSelection(10) == MainRenderRoute{10, true},
        "timing uses native Exit case then custom help");
    failures += Expect(
        RouteMainSelection(11) == MainRenderRoute{10, false},
        "moved Exit uses original Exit help case");

    {
        const auto native = ExpectedSoundVtable(kFakeBase);
        const CarrierCallbacks callbacks{
            .activate = 0x11111111,
            .render = 0x22222222,
            .confirm = 0x33333333,
            .back = 0x44444444,
            .increment = 0x55555555,
            .decrement = 0x66666666,
        };
        const auto carrier =
            BuildCarrierVtable(native, callbacks, kFakeBase);
        failures += Expect(
            carrier[2] == callbacks.activate &&
                carrier[5] == kFakeBase + 0x0C2E40 &&
                carrier[6] == callbacks.render &&
                carrier[7] == callbacks.confirm &&
                carrier[8] == callbacks.back &&
                carrier[9] == callbacks.increment &&
                carrier[10] == callbacks.decrement,
            "only approved behavioral slots change");
        for (const auto slot : {0U, 1U, 3U, 4U, 11U, 12U}) {
            failures += Expect(
                carrier[slot] == native[slot],
                "native lifecycle and unknown slots are preserved");
        }
    }

    {
        TimingSettingsModel model;
        model.Activate({.game_ms = 0, .judge_ms = -16});
        void* const grid = reinterpret_cast<void*>(0x12340000);
        RenderState state;
        auto actions = RenderActions(state);
        failures += Expect(
            RenderTimingSettings(model, grid, actions) &&
                state.title_calls == 1 &&
                state.title == "TIMING SETTINGS" &&
                state.title_x == 4 && state.title_y == 2 &&
                state.cells == std::vector<CellEvent>{
                    {grid, 0, 0, "MUSIC OFFSET"},
                    {grid, 0, 1, "+0 ms"},
                    {grid, 1, 0, "JUDGE OFFSET"},
                    {grid, 1, 1, "-16 ms"},
                    {grid, 2, 0, "SAVE AND BACK"},
                    {grid, 2, 1, ""},
                    {grid, 3, 0, "CANCEL"},
                    {grid, 3, 1, ""},
                } &&
                state.help == "LEFT/RIGHT: MUSIC OFFSET",
            "renderer writes the exact title and four-by-two grid");

        constexpr std::array row_help{
            "LEFT/RIGHT: MUSIC OFFSET",
            "LEFT/RIGHT: JUDGE OFFSET",
            "SAVE VALUES AND RETURN",
            "DISCARD CHANGES AND RETURN",
        };
        for (std::size_t row = 0; row < row_help.size(); ++row) {
            model.SetRow(static_cast<TimingRow>(row));
            state = {};
            failures += Expect(
                RenderTimingSettings(model, grid, actions) &&
                    state.help == row_help[row],
                "renderer selects help for each timing row");
        }

        model.MarkSaveFailed();
        for (std::size_t row = 0; row < row_help.size(); ++row) {
            model.SetRow(static_cast<TimingRow>(row));
            state = {};
            failures += Expect(
                RenderTimingSettings(model, grid, actions) &&
                    state.help == "SAVE FAILED - CHECK loader-log.txt",
                "save failure overrides help on every row");
        }
    }

    {
        std::array<std::byte, kSoundFormSize> carrier{};
        std::array<std::byte, 80> grid{};
        carrier.fill(std::byte{0x5A});
        grid.fill(std::byte{0x6B});
        void* grid_pointer = grid.data();
        std::memcpy(
            carrier.data() + kFormGridOffset,
            &grid_pointer,
            sizeof(grid_pointer));
        auto expected_carrier = carrier;
        auto expected_grid = grid;
        const int rows = 4;
        const int no_child = -1;
        const int selection = 0;
        std::memcpy(
            expected_carrier.data() + kFormRowCountOffset,
            &rows,
            sizeof(rows));
        std::memcpy(
            expected_carrier.data() + kFormActiveChildOffset,
            &no_child,
            sizeof(no_child));
        std::memcpy(
            expected_grid.data() + kGridRowCountOffset,
            &rows,
            sizeof(rows));
        std::memcpy(
            expected_grid.data() + kGridSelectionOffset,
            &selection,
            sizeof(selection));
        failures += Expect(
            PrepareCarrierLayout(carrier.data()) &&
                carrier == expected_carrier && grid == expected_grid,
            "carrier layout changes only four characterized fields");
    }

    for (const auto failure : {
             CarrierFailPoint::Construct,
             CarrierFailPoint::Prepare,
             CarrierFailPoint::Register,
         }) {
        CarrierLifeState state{.failure = failure};
        void* carrier = reinterpret_cast<void*>(0x1);
        const bool created = CreateTimingCarrier(
            reinterpret_cast<void*>(0xCAFEBABE),
            reinterpret_cast<void*>(0xDEADC0DE),
            LifeActions(state),
            &carrier);
        const bool failed_before_construction =
            failure == CarrierFailPoint::Construct;
        failures += Expect(
            !created && carrier == nullptr &&
                state.deallocate_calls ==
                    (failed_before_construction ? 1 : 0) &&
                state.destroy_calls ==
                    (failed_before_construction ? 0 : 1) &&
                (failed_before_construction || state.destroy_flag == 1),
            "every pre-registration failure releases ownership exactly once");
    }

    {
        CarrierLifeState state;
        void* carrier = nullptr;
        failures += Expect(
            CreateTimingCarrier(
                reinterpret_cast<void*>(0xCAFEBABE),
                reinterpret_cast<void*>(0xDEADC0DE),
                LifeActions(state),
                &carrier) &&
                carrier == state.storage.data() &&
                state.register_calls == 1 &&
                state.deallocate_calls == 0 && state.destroy_calls == 0,
            "successful carrier registration transfers ownership to parent");
    }

    {
        TimingSettingsModel model;
        model.Activate({.game_ms = 0, .judge_ms = -16});
        model.SetRow(TimingRow::SaveAndBack);
        CommitState state;
        failures += Expect(
            CommitTimingSelection(model, CommitActions(state)) == 1 &&
                state.events ==
                    std::vector<std::string>{"mark succeeded"} &&
                model.status() == SaveStatus::Succeeded,
            "clean save marks success without store or live apply");
    }

    for (const auto outcome : {
             SaveOutcome::Changed,
             SaveOutcome::Unchanged,
         }) {
        TimingSettingsModel model;
        model.Activate({.game_ms = 0, .judge_ms = -16});
        model.AdjustSelected(1);
        model.SetRow(TimingRow::SaveAndBack);
        CommitState state{.save_outcome = outcome};
        failures += Expect(
            CommitTimingSelection(model, CommitActions(state)) == 1 &&
                state.saved == TimingOffsets{1, -16} &&
                state.applied == TimingOffsets{1, -16} &&
                state.events == std::vector<std::string>{
                    "save", "write GameTimeOffset",
                    "write JudgTimeOffset", "get timing manager",
                    "set GameTime", "set JudgTime", "mark succeeded",
                } &&
                model.status() == SaveStatus::Succeeded,
            "dirty save always applies live timing after persistence");
    }

    {
        TimingSettingsModel model;
        model.Activate({.game_ms = 0, .judge_ms = -16});
        model.AdjustSelected(1);
        model.SetRow(TimingRow::SaveAndBack);
        CommitState state{.save_succeeds = false};
        failures += Expect(
            CommitTimingSelection(model, CommitActions(state)) == 0 &&
                state.events == std::vector<std::string>{
                    "save", "mark failed", "log save failure",
                } &&
                model.status() == SaveStatus::Failed,
            "save failure stays in the form and never applies live timing");
    }

    {
        TimingSettingsModel model;
        model.Activate({.game_ms = 0, .judge_ms = -16});
        model.AdjustSelected(1);
        model.SetRow(TimingRow::SaveAndBack);
        CommitState state{.apply_succeeds = false};
        failures += Expect(
            CommitTimingSelection(model, CommitActions(state)) == 0 &&
                state.events == std::vector<std::string>{
                    "save", "apply failure", "mark failed", "log fatal",
                } &&
                model.status() == SaveStatus::Failed,
            "live apply invariant failure is fatal and remains actionable");
    }

    {
        TimingSettingsModel model;
        model.Activate({.game_ms = 0, .judge_ms = -16});
        model.AdjustSelected(1);
        model.SetRow(TimingRow::Cancel);
        CommitState state;
        failures += Expect(
            CommitTimingSelection(model, CommitActions(state)) == 1 &&
                state.events.empty() &&
                model.staged() == TimingOffsets{0, -16},
            "cancel confirm discards without store or live apply");

        model.AdjustSelected(1);
        failures += Expect(
            CancelTimingEdit(model) == 1 && state.events.empty() &&
                model.staged() == TimingOffsets{0, -16},
            "back discards without store or live apply");
    }

    return failures == 0 ? 0 : 1;
}

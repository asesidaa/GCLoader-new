#include "Patches/TestModeTiming/TimingSettingsGameAbi.h"

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
        return transaction.Install(
            contracts,
            kFakeBase + kSoundVtableRva,
            vtable,
            writes,
            hooks);
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

    return failures == 0 ? 0 : 1;
}

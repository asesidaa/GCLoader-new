#include "Patches/Framerate/FrameratePatchTransaction.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <iostream>
#include <span>
#include <vector>

using namespace gc::framerate;

int Expect(bool condition, const char* name) {
    if (condition) {
        return 0;
    }
    std::cerr << "Expectation failed: " << name << "\n";
    return 1;
}

BytePattern Pattern(std::initializer_list<std::uint8_t> values) {
    BytePattern pattern{};
    pattern.size = static_cast<std::uint8_t>(values.size());
    std::transform(
        values.begin(), values.end(), pattern.bytes.begin(),
        [](std::uint8_t value) { return static_cast<std::byte>(value); });
    return pattern;
}

struct FakeMemory {
    std::array<std::byte, 128> bytes{};
    bool fail_read{};
    int fail_write_call{-1};
    int write_calls{};
};

FakeMemory* g_memory = nullptr;

bool FakeRead(
    std::uintptr_t address,
    std::span<std::byte> destination) noexcept {
    if (g_memory->fail_read ||
        address + destination.size() > g_memory->bytes.size()) {
        return false;
    }
    std::copy_n(
        g_memory->bytes.begin() + address,
        destination.size(),
        destination.begin());
    return true;
}

bool FakeWrite(
    std::uintptr_t address,
    std::span<const std::byte> source) noexcept {
    const int call = g_memory->write_calls++;
    if (call == g_memory->fail_write_call ||
        address + source.size() > g_memory->bytes.size()) {
        return false;
    }
    std::copy(source.begin(), source.end(), g_memory->bytes.begin() + address);
    return true;
}

struct FakeHook {
    int install_call{};
    int fail_on_call{-1};
    std::vector<std::size_t> installed;
    std::vector<std::size_t> reset;
};

struct FakeHookContext {
    FakeHook* state{};
    std::size_t index{};
};

bool InstallFakeHook(void* opaque) noexcept {
    auto& context = *static_cast<FakeHookContext*>(opaque);
    const int call = context.state->install_call++;
    if (call == context.state->fail_on_call) {
        return false;
    }
    context.state->installed.push_back(context.index);
    return true;
}

void ResetFakeHook(void* opaque) noexcept {
    auto& context = *static_cast<FakeHookContext*>(opaque);
    context.state->reset.push_back(context.index);
}

struct Fixture {
    FakeMemory memory{};
    std::array<std::byte, 128> original_bytes{};
    FakeHook hook_state{};
    std::array<FakeHookContext, 4> hook_contexts{};
    std::array<CheckedWrite, 3> writes{};
    std::array<HookOperation, 4> hooks{};

    Fixture() {
        writes = {
            CheckedWrite{8, Pattern({0x01, 0x02}), Pattern({0xA1, 0xA2}), "write0"},
            CheckedWrite{16, Pattern({0x03, 0x04}), Pattern({0xA3, 0xA4}), "write1"},
            CheckedWrite{24, Pattern({0x05, 0x06}), Pattern({0xA5, 0xA6}), "write2"},
        };
        for (const auto& write : writes) {
            std::copy(
                write.expected.view().begin(),
                write.expected.view().end(),
                memory.bytes.begin() + write.address);
        }

        for (std::size_t index = 0; index < hooks.size(); ++index) {
            const auto address = 40 + index * 8;
            BytePattern expected{};
            expected.size = 1;
            expected.bytes[0] = static_cast<std::byte>(0x70 + index);
            memory.bytes[address] = expected.bytes[0];
            hook_contexts[index] = {&hook_state, index};
            hooks[index] = {
                .address = address,
                .expected = expected,
                .name = "fake hook",
                .context = &hook_contexts[index],
                .install = InstallFakeHook,
                .reset = ResetFakeHook,
            };
        }
        original_bytes = memory.bytes;
    }

    Fixture(const Fixture&) = delete;
    Fixture& operator=(const Fixture&) = delete;
};

Fixture MakeFixture() { return Fixture{}; }

std::vector<std::size_t> ReverseIndices(std::size_t count) {
    std::vector<std::size_t> result;
    while (count != 0) {
        result.push_back(--count);
    }
    return result;
}

int main() {
int failures = 0;

for (int failed_write = 0; failed_write < 3; ++failed_write) {
    auto fixture = MakeFixture();
    fixture.memory.fail_write_call = failed_write;
    g_memory = &fixture.memory;
    FrameratePatchTransaction transaction({FakeRead, FakeWrite});
    const auto result = transaction.Install(fixture.writes, fixture.hooks);
    failures += Expect(!result, "write failure rejected");
    failures += Expect(
        fixture.memory.bytes == fixture.original_bytes,
        "write failure restores complete image");
    failures += Expect(
        result.error().rollback_complete,
        "write failure rollback verified");
}

for (int failed_hook = 0; failed_hook < 4; ++failed_hook) {
    auto fixture = MakeFixture();
    fixture.hook_state.fail_on_call = failed_hook;
    g_memory = &fixture.memory;
    FrameratePatchTransaction transaction({FakeRead, FakeWrite});
    const auto result = transaction.Install(fixture.writes, fixture.hooks);
    failures += Expect(!result, "hook failure rejected");
    failures += Expect(
        fixture.memory.bytes == fixture.original_bytes,
        "hook failure restores every direct patch");
    const std::vector<std::size_t> expected_reset =
        ReverseIndices(static_cast<std::size_t>(failed_hook + 1));
    failures += Expect(
        fixture.hook_state.reset == expected_reset,
        "hook rollback is reverse ordered and resets failed hook defensively");
    failures += Expect(
        result.error().rollback_complete,
        "hook failure rollback verified");
}

{
    auto fixture = MakeFixture();
    fixture.memory.bytes[8] = std::byte{0xFF};
    g_memory = &fixture.memory;
    FrameratePatchTransaction transaction({FakeRead, FakeWrite});
    const auto result = transaction.Install(fixture.writes, fixture.hooks);
    failures += Expect(
        !result &&
            result.error().stage ==
                FramerateInstallStage::PreflightMismatch &&
            fixture.memory.write_calls == 0 &&
            fixture.hook_state.installed.empty(),
        "preflight mismatch mutates nothing");
}

{
    auto fixture = MakeFixture();
    fixture.memory.fail_read = true;
    g_memory = &fixture.memory;
    FrameratePatchTransaction transaction({FakeRead, FakeWrite});
    const auto result = transaction.Install(fixture.writes, fixture.hooks);
    failures += Expect(
        !result &&
            result.error().stage == FramerateInstallStage::PreflightRead,
        "preflight read failure is distinct");
}

{
    auto fixture = MakeFixture();
    g_memory = &fixture.memory;
    std::array<CheckedWrite, kMaximumFramerateWrites + 1> too_many{};
    FrameratePatchTransaction transaction({FakeRead, FakeWrite});
    const auto result = transaction.Install(
        too_many, std::span<const HookOperation>{});
    failures += Expect(
        !result && result.error().stage == FramerateInstallStage::Capacity,
        "over-capacity plan rejected before descriptor access");
}

{
    auto fixture = MakeFixture();
    g_memory = &fixture.memory;
    FrameratePatchTransaction transaction({FakeRead, FakeWrite});
    const auto result = transaction.Install(fixture.writes, fixture.hooks);
    failures += Expect(
        result.has_value() && transaction.committed() &&
            fixture.hook_state.installed ==
                std::vector<std::size_t>{0, 1, 2, 3} &&
            fixture.memory.bytes != fixture.original_bytes,
        "successful transaction retains every write and hook");
}

{
    auto fixture = MakeFixture();
    fixture.hook_state.fail_on_call = 0;
    fixture.memory.fail_write_call = 3;
    g_memory = &fixture.memory;
    FrameratePatchTransaction transaction({FakeRead, FakeWrite});
    const auto result = transaction.Install(fixture.writes, fixture.hooks);
    failures += Expect(
        !result && result.error().rollback_attempted &&
            !result.error().rollback_complete,
        "failed restore is reported as incomplete rollback");
}

return failures == 0 ? 0 : 1;
}

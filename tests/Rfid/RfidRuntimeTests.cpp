#include "Rfid/Runtime.h"
#include "Rfid/State.h"

#include <chrono>
#include <expected>
#include <iostream>

namespace {

struct FakeCardWorker {
    int start_calls{};
    bool fail_start{};
    DWORD error{ERROR_NOT_ENOUGH_MEMORY};
    gc::rfid::CardWorkerEntry entry{};
    void* context{};
};

FakeCardWorker* g_fake_worker{};

std::expected<void, DWORD> StartFakeWorker(
    gc::rfid::CardWorkerEntry entry,
    void* context) noexcept
{
    ++g_fake_worker->start_calls;
    g_fake_worker->entry = entry;
    g_fake_worker->context = context;
    if (g_fake_worker->fail_start) {
        return std::unexpected(g_fake_worker->error);
    }
    return {};
}

SHORT FakeGetAsyncKeyState(int) noexcept
{
    return 0;
}

void FakeSleepFor(std::chrono::milliseconds) noexcept
{
}

gc::rfid::CardWorkerApi FakeWorkerApi(FakeCardWorker& fake)
{
    g_fake_worker = &fake;
    return {
        .start_detached = StartFakeWorker,
        .get_async_key_state = FakeGetAsyncKeyState,
        .sleep_for = FakeSleepFor,
    };
}

int expect(bool actual, bool expected, const char* name)
{
    if (actual == expected)
    {
        return 0;
    }

    std::cerr << name << ": expected " << expected
              << ", got " << actual << '\n';
    return 1;
}

}

int main()
{
    int failures = 0;

    FakeCardWorker worker;
    gc::rfid::Runtime runtime{VK_F4, FakeWorkerApi(worker)};
    const auto first = runtime.OpenCom2();
    const auto second = runtime.OpenCom2();
    failures += expect(first.has_value(), true, "first COM2 open");
    failures += expect(second.has_value(), true, "second COM2 open");
    failures += expect(
        first && *first == gc::rfid::EmulatedComHandle(),
        true,
        "stable emulated handle");
    failures += expect(worker.start_calls == 1, true,
                       "worker starts exactly once");
    failures += expect(worker.entry != nullptr && worker.context != nullptr,
                       true, "worker launch receives entry and context");
    failures += expect(runtime.port().IsOpen(), true, "port is open");

    runtime.CloseCom2();
    failures += expect(runtime.port().IsOpen(), false, "port closes");
    const auto reopened = runtime.OpenCom2();
    failures += expect(reopened.has_value(), true, "COM2 reopens");
    failures += expect(worker.start_calls == 1, true,
                       "worker remains process-lifetime");

    FakeCardWorker failed_worker{
        .fail_start = true,
        .error = ERROR_NOT_ENOUGH_MEMORY,
    };
    gc::rfid::Runtime failed_runtime{VK_F4, FakeWorkerApi(failed_worker)};
    const auto failed_first = failed_runtime.OpenCom2();
    const auto failed_second = failed_runtime.OpenCom2();
    failures += expect(
        !failed_first &&
            failed_first.error() == ERROR_NOT_ENOUGH_MEMORY,
        true,
        "first worker failure is returned");
    failures += expect(
        !failed_second &&
            failed_second.error() == ERROR_NOT_ENOUGH_MEMORY,
        true,
        "worker failure is permanent");
    failures += expect(failed_worker.start_calls == 1, true,
                       "failed worker launches only once");
    failures += expect(failed_runtime.port().IsOpen(), false,
                       "port stays closed after worker failure");

    const auto production_api = gc::rfid::ProductionCardWorkerApi();
    failures += expect(
        production_api.start_detached != nullptr &&
            production_api.get_async_key_state != nullptr &&
            production_api.sleep_for != nullptr,
        true,
        "production worker API is complete");

    using gc::rfid::CardScanState;
    CardScanState state;
    failures += expect(state.IsPresent(), false, "initial card state");

    state.Arm();
    failures += expect(state.IsPresent(), true, "armed card state");
    failures += expect(
        state.IsPresent(),
        true,
        "status polling preserves armed card");

    failures += expect(state.Consume(), true, "first payload read");
    failures += expect(
        state.IsPresent(),
        false,
        "payload read clears card state");
    failures += expect(state.Consume(), false, "second payload read");

    state.Arm();
    failures += expect(state.Consume(), true, "later card scan");
    failures += expect(
        state.IsPresent(),
        false,
        "later payload read clears card state");

    gc::rfid::State device_state;
    device_state.assigned_address = gc::rfid::jvs::Address{0x7F};
    device_state.coins = {12, 34};
    device_state.card_scan.Arm();
    device_state.ResetBus();

    failures += expect(
        device_state.assigned_address.has_value(),
        false,
        "bus reset clears assigned address");
    failures += expect(
        device_state.coins[0] == 0,
        true,
        "bus reset clears P1 coins");
    failures += expect(
        device_state.coins[1] == 0,
        true,
        "bus reset clears P2 coins");
    failures += expect(
        device_state.card_scan.IsPresent(),
        true,
        "bus reset preserves physical card presence");

    return failures == 0 ? 0 : 1;
}

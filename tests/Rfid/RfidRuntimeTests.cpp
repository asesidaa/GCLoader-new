#include "Rfid/Runtime.h"
#include "Rfid/State.h"

#include <array>
#include <chrono>
#include <expected>
#include <iostream>

namespace {

struct WorkerLaunch {
    gc::rfid::CardWorkerEntry entry{};
    void* context{};
};

struct FakeCardWorker {
    int start_calls{};
    int fail_on_call{};
    DWORD error{ERROR_NOT_ENOUGH_MEMORY};
    std::array<WorkerLaunch, 4> launches{};
};

FakeCardWorker* g_fake_worker{};

std::expected<void, DWORD> StartFakeWorker(
    gc::rfid::CardWorkerEntry entry,
    void* context) noexcept
{
    ++g_fake_worker->start_calls;
    const auto index = static_cast<std::size_t>(
        g_fake_worker->start_calls - 1);
    if (index < g_fake_worker->launches.size()) {
        g_fake_worker->launches[index] = {
            .entry = entry,
            .context = context,
        };
    }
    if (g_fake_worker->start_calls ==
        g_fake_worker->fail_on_call) {
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
    failures += expect(worker.start_calls == 2, true,
                       "keyboard and listener workers start exactly once");
    failures += expect(
        worker.launches[0].entry != nullptr &&
            worker.launches[1].entry != nullptr &&
            worker.launches[0].entry != worker.launches[1].entry &&
            worker.launches[0].context != nullptr &&
            worker.launches[0].context == worker.launches[1].context,
        true,
        "runtime launches independent workers with shared context");
    failures += expect(runtime.port().IsOpen(), true, "port is open");

    runtime.CloseCom2();
    failures += expect(runtime.port().IsOpen(), false, "port closes");
    const auto reopened = runtime.OpenCom2();
    failures += expect(reopened.has_value(), true, "COM2 reopens");
    failures += expect(worker.start_calls == 2, true,
                       "both workers remain process-lifetime");

    FakeCardWorker failed_worker{
        .fail_on_call = 1,
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

    FakeCardWorker failed_listener{
        .fail_on_call = 2,
        .error = ERROR_NOT_ENOUGH_MEMORY,
    };
    gc::rfid::Runtime listener_failure_runtime{
        VK_F4, FakeWorkerApi(failed_listener)};
    const auto listener_failure_first =
        listener_failure_runtime.OpenCom2();
    listener_failure_runtime.CloseCom2();
    const auto listener_failure_reopen =
        listener_failure_runtime.OpenCom2();
    failures += expect(
        listener_failure_first.has_value() &&
            listener_failure_reopen.has_value() &&
            failed_listener.start_calls == 2 &&
            listener_failure_runtime.port().IsOpen(),
        true,
        "optional listener failure does not fail or retry COM2 open");

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
    const auto first_manual = state.Snapshot();
    failures += expect(
        first_manual.present && !first_manual.card_data,
        true,
        "manual arm publishes no supplied payload");
    failures += expect(
        state.IsPresent(),
        true,
        "status polling preserves armed card");

    failures += expect(
        state.Consume(first_manual.generation),
        true,
        "matching generation consumes payload");
    failures += expect(
        state.IsPresent(),
        false,
        "payload read clears card state");
    failures += expect(
        state.Consume(first_manual.generation),
        false,
        "consumed generation cannot be consumed twice");

    const auto supplied = *gc::rfid::ParseCardNumber(
        "1111222233334444");
    const auto replacement = *gc::rfid::ParseCardNumber(
        "9999888877776666");
    state.Arm(supplied);
    const auto external = state.Snapshot();
    failures += expect(
        external.present && external.card_data == supplied,
        true,
        "external arm publishes supplied payload");

    state.Arm();
    const auto newer_manual = state.Snapshot();
    failures += expect(
        newer_manual.present && !newer_manual.card_data &&
            newer_manual.generation != external.generation,
        true,
        "newer manual trigger replaces supplied payload");

    state.Arm(supplied);
    const auto stale = state.Snapshot();
    state.Arm(replacement);
    const auto newest = state.Snapshot();
    failures += expect(
        !state.Consume(stale.generation),
        true,
        "stale generation cannot consume newer trigger");
    const auto after_stale_consume = state.Snapshot();
    failures += expect(
        after_stale_consume.present &&
            after_stale_consume.generation == newest.generation &&
            after_stale_consume.card_data == replacement,
        true,
        "stale consume preserves newer payload");
    failures += expect(
        state.Consume(newest.generation) && !state.IsPresent(),
        true,
        "newest generation consumes exactly once");

    gc::rfid::State device_state;
    device_state.assigned_address = gc::rfid::jvs::Address{0x7F};
    device_state.coins = {12, 34};
    device_state.card_scan.Arm(supplied);
    const auto pending_before_reset = device_state.card_scan.Snapshot();
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
    const auto pending_after_reset = device_state.card_scan.Snapshot();
    failures += expect(
        pending_after_reset.present &&
            pending_after_reset.generation ==
                pending_before_reset.generation &&
            pending_after_reset.card_data == supplied,
        true,
        "bus reset preserves complete pending card scan");

    return failures == 0 ? 0 : 1;
}

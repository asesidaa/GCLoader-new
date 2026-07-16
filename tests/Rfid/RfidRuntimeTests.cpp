#include "Rfid/State.h"

#include <iostream>

namespace {

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
    using gc::rfid::CardScanState;
    int failures = 0;

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

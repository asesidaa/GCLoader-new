#include "CardScanState.h"

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

    return failures == 0 ? 0 : 1;
}

#include "InputSnapshotState.h"

#include <array>
#include <cstdint>
#include <iostream>
#include <utility>

namespace {

int expect_word(
    std::uint32_t actual,
    std::uint32_t expected,
    const char* name)
{
    if (actual == expected)
    {
        return 0;
    }

    std::cerr << name << ": expected 0x" << std::hex << expected
              << ", got 0x" << actual << std::dec << '\n';
    return 1;
}

}

int main()
{
    using namespace gc::input;
    int failures = 0;

    constexpr std::array input_cases{
        std::pair{LogicalInput::LeftBoosterUp, FastIoBits::P1_UP},
        std::pair{LogicalInput::LeftBoosterDown, FastIoBits::P2_UP},
        std::pair{LogicalInput::LeftBoosterLeft, FastIoBits::P1_DOWN},
        std::pair{LogicalInput::LeftBoosterRight, FastIoBits::P2_DOWN},
        std::pair{LogicalInput::RightBoosterUp, FastIoBits::P1_LEFT},
        std::pair{LogicalInput::RightBoosterDown, FastIoBits::P2_LEFT},
        std::pair{LogicalInput::RightBoosterLeft, FastIoBits::P1_RIGHT},
        std::pair{LogicalInput::RightBoosterRight, FastIoBits::P2_RIGHT},
        std::pair{LogicalInput::LeftBoosterButton, FastIoBits::P1_BUTTON_1},
        std::pair{LogicalInput::RightBoosterButton, FastIoBits::P2_BUTTON_1}};

    for (const auto& [logical, fast_io] : input_cases)
    {
        InputSnapshotState state;
        state.Set(logical, InputSource::Keyboard, true);
        failures += expect_word(
            state.Compose(GameplaySource::Keyboard),
            fast_io,
            "logical gameplay input to FastIO");
    }

    constexpr std::array system_cases{
        std::pair{LogicalInput::Service1, FastIoBits::P1_SERVICE_F1},
        std::pair{LogicalInput::Service2, FastIoBits::P1_SERVICE_I},
        std::pair{LogicalInput::Service3, FastIoBits::P1_SERVICE_P},
        std::pair{LogicalInput::P1Start, FastIoBits::P1_START},
        std::pair{LogicalInput::P2Start, FastIoBits::P2_START},
        std::pair{LogicalInput::P2Service, FastIoBits::P2_SERVICE},
        std::pair{LogicalInput::Test, FastIoBits::TEST_MODE}};

    for (const auto& [logical, fast_io] : system_cases)
    {
        InputSnapshotState state;
        state.Set(logical, InputSource::Keyboard, true);
        failures += expect_word(
            state.Compose(GameplaySource::Gamepad),
            fast_io,
            "system input to FastIO");
    }

    InputSnapshotState held;
    held.Set(LogicalInput::LeftBoosterUp, InputSource::Keyboard, true);
    failures += expect_word(
        held.Compose(GameplaySource::Keyboard),
        FastIoBits::P1_UP,
        "pressed key");
    failures += expect_word(
        held.Compose(GameplaySource::Keyboard),
        FastIoBits::P1_UP,
        "held key remains pressed");
    held.Set(LogicalInput::LeftBoosterUp, InputSource::Keyboard, false);
    failures += expect_word(
        held.Compose(GameplaySource::Keyboard),
        0,
        "released key");

    InputSnapshotState combined;
    combined.Set(
        LogicalInput::LeftBoosterLeft,
        InputSource::GamepadButton,
        true);
    combined.Set(
        LogicalInput::LeftBoosterLeft,
        InputSource::GamepadAxis,
        true);
    combined.Set(
        LogicalInput::LeftBoosterLeft,
        InputSource::GamepadButton,
        false);
    failures += expect_word(
        combined.Compose(GameplaySource::Gamepad),
        FastIoBits::P1_DOWN,
        "axis survives button release");
    combined.Set(
        LogicalInput::LeftBoosterLeft,
        InputSource::GamepadAxis,
        false);
    failures += expect_word(
        combined.Compose(GameplaySource::Gamepad),
        0,
        "direction clears after both gamepad sources release");

    combined.Set(
        LogicalInput::RightBoosterButton,
        InputSource::GamepadButton,
        true);
    combined.ClearGamepad();
    failures += expect_word(
        combined.Compose(GameplaySource::Gamepad),
        0,
        "gamepad disconnect clears gamepad sources");

    InputSnapshotState system_keys;
    system_keys.Set(LogicalInput::Test, InputSource::Keyboard, true);
    system_keys.Set(LogicalInput::P2Start, InputSource::Keyboard, true);
    failures += expect_word(
        system_keys.Compose(GameplaySource::Gamepad),
        FastIoBits::TEST_MODE | FastIoBits::P2_START,
        "system keyboard works in gamepad mode");
    system_keys.ClearKeyboard();
    failures += expect_word(
        system_keys.Compose(GameplaySource::Gamepad),
        0,
        "focus loss clears keyboard sources");

    if (failures != 0)
    {
        return 1;
    }

    std::cout << "InputSnapshotStateTests passed\n";
    return 0;
}

#include "Input/Win32/XInputController.h"

#include <Windows.h>
#include <Xinput.h>

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <optional>
#include <string_view>
#include <utility>

namespace {

struct FakeXInputState {
    DWORD result{ERROR_SUCCESS};
    XINPUT_STATE state{};
    std::array<DWORD, 128> slots{};
    std::size_t call_count{};
};

FakeXInputState* fake{};

DWORD WINAPI fake_get_state(DWORD slot, XINPUT_STATE* state) noexcept
{
    if (fake->call_count < fake->slots.size())
    {
        fake->slots[fake->call_count] = slot;
    }
    ++fake->call_count;
    if (fake->result == ERROR_SUCCESS)
    {
        *state = fake->state;
    }
    return fake->result;
}

gc::input::XInputApi fake_api()
{
    return {
        .get_state = fake_get_state,
        .loaded_name = L"fake-xinput.dll",
    };
}

std::optional<gc::input::DigitalControlBinding> find_binding(
    const gc::input::ControllerStateView& controller,
    gc::input::XInputControl control,
    std::optional<gc::input::ControlDirection> direction = std::nullopt)
{
    for (const auto& descriptor : controller.controls())
    {
        if (descriptor.binding.control == control &&
            (!direction || descriptor.binding.direction == direction))
        {
            return descriptor.binding;
        }
    }
    return std::nullopt;
}

double activation(
    const gc::input::ControllerStateView& controller,
    const gc::input::DigitalControlBinding& binding)
{
    return controller.Activation(binding).value_or(-1.0);
}

int expect_true(bool actual, std::string_view name)
{
    if (actual)
    {
        return 0;
    }
    std::cerr << name << ": expected true\n";
    return 1;
}

int expect_near(double actual, double expected, std::string_view name)
{
    if (std::abs(actual - expected) < 0.00001)
    {
        return 0;
    }
    std::cerr << name << ": expected " << expected
              << ", got " << actual << '\n';
    return 1;
}

void next_packet(FakeXInputState& state)
{
    ++state.state.dwPacketNumber;
    state.state.Gamepad = {};
}

} // namespace

int main()
{
    using namespace gc::input;
    using Clock = XInputController::Clock;

    int failures = 0;
    FakeXInputState fake_state;
    fake = &fake_state;

    failures += expect_true(
        !XInputController::Create(4, fake_api()),
        "slot 4 rejected");
    auto created = XInputController::Create(2, fake_api());
    failures += expect_true(created.has_value(), "slot 2 accepted");
    if (!created)
    {
        return 1;
    }
    auto controller = std::move(*created);
    failures += expect_true(
        controller.identity() == ControllerIdentity{
            ControllerBackend::XInput, "2"},
        "slot identity is exact");

    const auto start = Clock::time_point{};
    fake_state.state.dwPacketNumber = 1;
    auto first_poll = controller.PollAt(start);
    failures += expect_true(
        first_poll && *first_poll && controller.connected(),
        "initial exact-slot connection");

    struct ButtonCase {
        XInputControl control;
        WORD bit;
    };
    constexpr std::array button_cases{
        ButtonCase{XInputControl::A, XINPUT_GAMEPAD_A},
        ButtonCase{XInputControl::B, XINPUT_GAMEPAD_B},
        ButtonCase{XInputControl::X, XINPUT_GAMEPAD_X},
        ButtonCase{XInputControl::Y, XINPUT_GAMEPAD_Y},
        ButtonCase{XInputControl::DPadUp, XINPUT_GAMEPAD_DPAD_UP},
        ButtonCase{XInputControl::DPadDown, XINPUT_GAMEPAD_DPAD_DOWN},
        ButtonCase{XInputControl::DPadLeft, XINPUT_GAMEPAD_DPAD_LEFT},
        ButtonCase{XInputControl::DPadRight, XINPUT_GAMEPAD_DPAD_RIGHT},
        ButtonCase{XInputControl::Start, XINPUT_GAMEPAD_START},
        ButtonCase{XInputControl::Back, XINPUT_GAMEPAD_BACK},
        ButtonCase{XInputControl::LeftShoulder, XINPUT_GAMEPAD_LEFT_SHOULDER},
        ButtonCase{XInputControl::RightShoulder, XINPUT_GAMEPAD_RIGHT_SHOULDER},
        ButtonCase{XInputControl::LeftThumb, XINPUT_GAMEPAD_LEFT_THUMB},
        ButtonCase{XInputControl::RightThumb, XINPUT_GAMEPAD_RIGHT_THUMB},
    };
    for (const auto& test : button_cases)
    {
        next_packet(fake_state);
        fake_state.state.Gamepad.wButtons = test.bit;
        const auto polled = controller.PollAt(start);
        const auto binding = find_binding(controller, test.control);
        failures += expect_true(
            polled.has_value() && binding.has_value(),
            "button descriptor and poll");
        failures += expect_near(
            activation(controller, *binding), 1.0, "XInput button bit");
    }

    struct AxisCase {
        XInputControl control;
        SHORT XINPUT_GAMEPAD::* member;
    };
    constexpr std::array axis_cases{
        AxisCase{XInputControl::LeftX, &XINPUT_GAMEPAD::sThumbLX},
        AxisCase{XInputControl::LeftY, &XINPUT_GAMEPAD::sThumbLY},
        AxisCase{XInputControl::RightX, &XINPUT_GAMEPAD::sThumbRX},
        AxisCase{XInputControl::RightY, &XINPUT_GAMEPAD::sThumbRY},
    };
    for (const auto& test : axis_cases)
    {
        next_packet(fake_state);
        fake_state.state.Gamepad.*(test.member) = 32767;
        (void)controller.PollAt(start);
        const auto positive = find_binding(
            controller, test.control, ControlDirection::Positive);
        const auto negative = find_binding(
            controller, test.control, ControlDirection::Negative);
        failures += expect_true(
            positive && negative,
            "both signed axis directions described");
        failures += expect_near(
            activation(controller, *positive), 1.0, "axis positive maximum");
        failures += expect_near(
            activation(controller, *negative), 0.0, "axis negative inactive");

        next_packet(fake_state);
        fake_state.state.Gamepad.*(test.member) = -32768;
        (void)controller.PollAt(start);
        failures += expect_near(
            activation(controller, *positive), 0.0, "axis positive inactive");
        failures += expect_near(
            activation(controller, *negative), 1.0, "axis negative maximum");
    }

    const auto left_trigger = *find_binding(
        controller, XInputControl::LeftTrigger);
    const auto right_trigger = *find_binding(
        controller, XInputControl::RightTrigger);
    next_packet(fake_state);
    fake_state.state.Gamepad.bLeftTrigger = 255;
    (void)controller.PollAt(start);
    failures += expect_near(
        activation(controller, left_trigger), 1.0, "LT independent");
    failures += expect_near(
        activation(controller, right_trigger), 0.0, "RT remains neutral");

    next_packet(fake_state);
    fake_state.state.Gamepad.bRightTrigger = 255;
    (void)controller.PollAt(start);
    failures += expect_near(
        activation(controller, left_trigger), 0.0, "LT returns neutral");
    failures += expect_near(
        activation(controller, right_trigger), 1.0, "RT independent");

    next_packet(fake_state);
    fake_state.state.Gamepad.bLeftTrigger = 255;
    fake_state.state.Gamepad.bRightTrigger = 255;
    (void)controller.PollAt(start);
    failures += expect_near(
        activation(controller, left_trigger), 1.0, "simultaneous LT");
    failures += expect_near(
        activation(controller, right_trigger), 1.0, "simultaneous RT");

    const DWORD unchanged_packet = fake_state.state.dwPacketNumber;
    fake_state.state.Gamepad.bLeftTrigger = 0;
    fake_state.state.Gamepad.bRightTrigger = 0;
    const auto unchanged = controller.PollAt(start);
    failures += expect_true(
        unchanged && !*unchanged &&
            fake_state.state.dwPacketNumber == unchanged_packet,
        "unchanged packet skips remapping");
    failures += expect_near(
        activation(controller, left_trigger), 1.0, "unchanged packet preserves LT");
    failures += expect_near(
        activation(controller, right_trigger), 1.0, "unchanged packet preserves RT");

    fake_state.result = ERROR_DEVICE_NOT_CONNECTED;
    const auto disconnect_time = start + std::chrono::milliseconds(10);
    const auto disconnected = controller.PollAt(disconnect_time);
    failures += expect_true(
        disconnected && *disconnected && !controller.connected(),
        "disconnect clears connection");
    failures += expect_near(
        activation(controller, left_trigger), 0.0, "disconnect clears LT");
    failures += expect_near(
        activation(controller, right_trigger), 0.0, "disconnect clears RT");

    const std::size_t calls_after_disconnect = fake_state.call_count;
    const auto early_probe = controller.PollAt(
        disconnect_time + std::chrono::milliseconds(999));
    failures += expect_true(
        early_probe && !*early_probe &&
            fake_state.call_count == calls_after_disconnect,
        "disconnected slot probe is rate limited");
    const auto due_probe = controller.PollAt(
        disconnect_time + std::chrono::seconds(1));
    failures += expect_true(
        due_probe && !*due_probe &&
            fake_state.call_count == calls_after_disconnect + 1,
        "disconnected slot probe becomes eligible");

    controller.RequestReconnectProbe();
    const auto forced_probe = controller.PollAt(
        disconnect_time + std::chrono::seconds(1) +
            std::chrono::milliseconds(1));
    failures += expect_true(
        forced_probe && !*forced_probe &&
            fake_state.call_count == calls_after_disconnect + 2,
        "device change forces reconnect probe");

    fake_state.result = ERROR_SUCCESS;
    next_packet(fake_state);
    fake_state.state.Gamepad.wButtons = XINPUT_GAMEPAD_A;
    controller.RequestReconnectProbe();
    const auto reconnected = controller.PollAt(
        disconnect_time + std::chrono::seconds(1) +
            std::chrono::milliseconds(2));
    failures += expect_true(
        reconnected && *reconnected && controller.connected(),
        "selected slot reconnects");

    const auto a_binding = *find_binding(controller, XInputControl::A);
    failures += expect_near(
        activation(controller, a_binding), 1.0, "reconnected state applies");
    failures += expect_true(
        controller.RawValue(a_binding) == std::nullopt,
        "XInput has no Raw HID value");

    for (std::size_t index = 0;
         index < fake_state.call_count && index < fake_state.slots.size();
         ++index)
    {
        failures += expect_true(
            fake_state.slots[index] == 2,
            "configured slot 2 never calls slot 0");
    }

    if (failures != 0)
    {
        return 1;
    }

    std::cout << "XInputControllerTests passed\n";
    return 0;
}

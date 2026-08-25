#include "Input/Win32/XInputController.h"

#include <algorithm>
#include <array>
// ReSharper disable once CppUnusedIncludeDirective
#include <cstring>
#include <string>
#include <utility>

namespace gc::input {
namespace {

constexpr auto kDisconnectedProbeInterval = std::chrono::seconds(1);

struct ButtonDefinition {
    XInputControl control;
    WORD mask;
    const char* label;
};

constexpr std::array kButtons{
    ButtonDefinition{XInputControl::A, XINPUT_GAMEPAD_A, "XInput A"},
    ButtonDefinition{XInputControl::B, XINPUT_GAMEPAD_B, "XInput B"},
    ButtonDefinition{XInputControl::X, XINPUT_GAMEPAD_X, "XInput X"},
    ButtonDefinition{XInputControl::Y, XINPUT_GAMEPAD_Y, "XInput Y"},
    ButtonDefinition{
        XInputControl::DPadUp, XINPUT_GAMEPAD_DPAD_UP, "XInput D-pad Up"},
    ButtonDefinition{
        XInputControl::DPadDown, XINPUT_GAMEPAD_DPAD_DOWN, "XInput D-pad Down"},
    ButtonDefinition{
        XInputControl::DPadLeft, XINPUT_GAMEPAD_DPAD_LEFT, "XInput D-pad Left"},
    ButtonDefinition{
        XInputControl::DPadRight, XINPUT_GAMEPAD_DPAD_RIGHT, "XInput D-pad Right"},
    ButtonDefinition{
        XInputControl::Start, XINPUT_GAMEPAD_START, "XInput Start"},
    ButtonDefinition{
        XInputControl::Back, XINPUT_GAMEPAD_BACK, "XInput Back"},
    ButtonDefinition{
        XInputControl::LeftShoulder,
        XINPUT_GAMEPAD_LEFT_SHOULDER,
        "XInput Left Shoulder"},
    ButtonDefinition{
        XInputControl::RightShoulder,
        XINPUT_GAMEPAD_RIGHT_SHOULDER,
        "XInput Right Shoulder"},
    ButtonDefinition{
        XInputControl::LeftThumb,
        XINPUT_GAMEPAD_LEFT_THUMB,
        "XInput Left Thumb"},
    ButtonDefinition{
        XInputControl::RightThumb,
        XINPUT_GAMEPAD_RIGHT_THUMB,
        "XInput Right Thumb"},
};

struct AxisDefinition {
    XInputControl control;
    SHORT XINPUT_GAMEPAD::* member;
    const char* label;
};

constexpr std::array kAxes{
    AxisDefinition{XInputControl::LeftX, &XINPUT_GAMEPAD::sThumbLX, "Left X"},
    AxisDefinition{XInputControl::LeftY, &XINPUT_GAMEPAD::sThumbLY, "Left Y"},
    AxisDefinition{XInputControl::RightX, &XINPUT_GAMEPAD::sThumbRX, "Right X"},
    AxisDefinition{XInputControl::RightY, &XINPUT_GAMEPAD::sThumbRY, "Right Y"},
};

const ButtonDefinition* FindButton(XInputControl control) noexcept
{
    const auto found = std::ranges::find(kButtons, control, &ButtonDefinition::control);
    return found == kButtons.end() ? nullptr : &*found;
}

const AxisDefinition* FindAxis(XInputControl control) noexcept
{
    const auto found = std::ranges::find(kAxes, control, &AxisDefinition::control);
    return found == kAxes.end() ? nullptr : &*found;
}

DigitalControlBinding XInputBinding(
    DigitalControlType type,
    XInputControl control,
    std::optional<ControlDirection> direction = std::nullopt)
{
    return DigitalControlBinding{
        .type = type,
        .control = control,
        .direction = direction,
    };
}

double AxisActivation(SHORT value, ControlDirection direction) noexcept
{
    if (direction == ControlDirection::Positive)
    {
        return value > 0
            ? static_cast<double>(value) / 32767.0
            : 0.0;
    }
    if (direction == ControlDirection::Negative)
    {
        return value < 0
            ? static_cast<double>(-static_cast<std::int32_t>(value)) / 32768.0
            : 0.0;
    }
    return -1.0;
}

} // namespace

XInputController::~XInputController()
{
    UnloadXInput(api_);
}

XInputController::XInputController(XInputController&& other) noexcept
{
    MoveFrom(std::move(other));
}

XInputController& XInputController::operator=(
    XInputController&& other) noexcept
{
    if (this != &other)
    {
        UnloadXInput(api_);
        MoveFrom(std::move(other));
    }
    return *this;
}

std::expected<XInputController, std::string> XInputController::Create(
    std::uint32_t slot,
    XInputApi api)
{
    if (slot > 3)
    {
        UnloadXInput(api);
        return std::unexpected("XInput slot must be between 0 and 3");
    }
    if (api.get_state == nullptr)
    {
        UnloadXInput(api);
        return std::unexpected("XInputGetState is unavailable");
    }

    XInputController controller;
    controller.identity_ = ControllerIdentity{
        .backend = ControllerBackend::XInput,
        .device_id = std::to_string(slot),
    };
    controller.slot_ = slot;
    controller.api_ = std::move(api);
    controller.descriptors_.reserve(
        kButtons.size() + kAxes.size() * 2 + 2);

    for (const auto& button : kButtons)
    {
        controller.descriptors_.push_back(ControllerControlDescriptor{
            .binding = XInputBinding(
                DigitalControlType::XInputButton, button.control),
            .label = button.label,
        });
    }
    for (const auto& axis : kAxes)
    {
        controller.descriptors_.push_back(ControllerControlDescriptor{
            .binding = XInputBinding(
                DigitalControlType::XInputAxis,
                axis.control,
                ControlDirection::Positive),
            .label = std::string("XInput ") + axis.label + " Positive",
        });
        controller.descriptors_.push_back(ControllerControlDescriptor{
            .binding = XInputBinding(
                DigitalControlType::XInputAxis,
                axis.control,
                ControlDirection::Negative),
            .label = std::string("XInput ") + axis.label + " Negative",
        });
    }
    controller.descriptors_.push_back(ControllerControlDescriptor{
        .binding = XInputBinding(
            DigitalControlType::XInputTrigger,
            XInputControl::LeftTrigger),
        .label = "XInput Left Trigger",
    });
    controller.descriptors_.push_back(ControllerControlDescriptor{
        .binding = XInputBinding(
            DigitalControlType::XInputTrigger,
            XInputControl::RightTrigger),
        .label = "XInput Right Trigger",
    });
    return controller;
}

std::expected<bool, std::string> XInputController::Poll() noexcept
{
    return PollAt(Clock::now());
}

std::expected<bool, std::string> XInputController::PollAt(
    Clock::time_point now) noexcept
{
    if (!connected_ && !reconnect_probe_requested_ &&
        last_disconnected_probe_ &&
        now - *last_disconnected_probe_ < kDisconnectedProbeInterval)
    {
        return false;
    }

    XINPUT_STATE next{};
    const DWORD result = api_.get_state(slot_, &next);
    reconnect_probe_requested_ = false;
    if (result == ERROR_DEVICE_NOT_CONNECTED)
    {
        last_disconnected_probe_ = now;
        const bool changed = connected_;
        state_ = {};
        connected_ = false;
        packet_initialized_ = false;
        return changed;
    }
    if (result != ERROR_SUCCESS)
    {
        last_disconnected_probe_ = now;
        state_ = {};
        connected_ = false;
        packet_initialized_ = false;
        return std::unexpected(
            "XInputGetState failed with error " + std::to_string(result));
    }

    last_disconnected_probe_.reset();
    if (connected_ && packet_initialized_ &&
        next.dwPacketNumber == state_.dwPacketNumber)
    {
        return false;
    }

    const bool changed = !connected_ ||
        std::memcmp(&next.Gamepad, &state_.Gamepad, sizeof(XINPUT_GAMEPAD)) != 0;
    state_ = next;
    connected_ = true;
    packet_initialized_ = true;
    return changed;
}

void XInputController::RequestReconnectProbe() noexcept
{
    reconnect_probe_requested_ = true;
}

void XInputController::Clear() noexcept
{
    state_ = {};
    connected_ = false;
    packet_initialized_ = false;
}

bool XInputController::connected() const noexcept
{
    return connected_;
}

std::uint32_t XInputController::slot() const noexcept
{
    return slot_;
}

const std::wstring& XInputController::loaded_name() const noexcept
{
    return api_.loaded_name;
}

const ControllerIdentity& XInputController::identity() const noexcept
{
    return identity_;
}

std::span<const ControllerControlDescriptor>
XInputController::controls() const noexcept
{
    return descriptors_;
}

std::optional<double> XInputController::Activation(
    const DigitalControlBinding& binding) const noexcept
{
    if (!HasDescriptor(binding))
    {
        return std::nullopt;
    }
    if (!connected_)
    {
        return 0.0;
    }

    if (binding.type == DigitalControlType::XInputButton)
    {
        const auto* definition = FindButton(*binding.control);
        return definition != nullptr &&
            (state_.Gamepad.wButtons & definition->mask) != 0
            ? 1.0
            : 0.0;
    }
    if (binding.type == DigitalControlType::XInputAxis)
    {
        const auto* definition = FindAxis(*binding.control);
        if (definition == nullptr || !binding.direction)
        {
            return std::nullopt;
        }
        const double activation = AxisActivation(
            state_.Gamepad.*(definition->member),
            *binding.direction);
        return activation < 0.0
            ? std::optional<double>{}
            : std::optional<double>{activation};
    }
    if (binding.type == DigitalControlType::XInputTrigger)
    {
        const BYTE value = *binding.control == XInputControl::LeftTrigger
            ? state_.Gamepad.bLeftTrigger
            : state_.Gamepad.bRightTrigger;
        return static_cast<double>(value) / 255.0;
    }
    return std::nullopt;
}

std::optional<std::int32_t> XInputController::RawValue(
    const DigitalControlBinding&) const noexcept
{
    return std::nullopt;
}

void XInputController::MoveFrom(XInputController&& other) noexcept
{
    identity_ = std::move(other.identity_);
    slot_ = other.slot_;
    api_ = std::move(other.api_);
    state_ = other.state_;
    descriptors_ = std::move(other.descriptors_);
    last_disconnected_probe_ = other.last_disconnected_probe_;
    connected_ = other.connected_;
    packet_initialized_ = other.packet_initialized_;
    reconnect_probe_requested_ = other.reconnect_probe_requested_;

    other.api_.module = nullptr;
    other.api_.get_state = nullptr;
    other.api_.loaded_name.clear();
    other.state_ = {};
    other.connected_ = false;
    other.packet_initialized_ = false;
}

bool XInputController::HasDescriptor(
    const DigitalControlBinding& binding) const noexcept
{
    return std::ranges::any_of(
        descriptors_,
        [&](const ControllerControlDescriptor& descriptor) {
            return descriptor.binding.type == binding.type &&
                descriptor.binding.control == binding.control &&
                descriptor.binding.direction == binding.direction;
        });
}

} // namespace gc::input

#pragma once

#include <compare>
#include <cstdint>
#include <optional>
#include <string>

namespace gc::input {

enum class ScanCodePrefix : std::uint8_t {
    None,
    E0,
    E1,
};

struct PhysicalKey {
    std::uint16_t make_code{};
    ScanCodePrefix prefix{ScanCodePrefix::None};

    auto operator<=>(const PhysicalKey&) const = default;
};

enum class LogicalAction : std::uint8_t {
    LeftBoosterUp,
    LeftBoosterDown,
    LeftBoosterLeft,
    LeftBoosterRight,
    LeftBoosterButton,
    RightBoosterUp,
    RightBoosterDown,
    RightBoosterLeft,
    RightBoosterRight,
    RightBoosterButton,
    Service1,
    Service2,
    Service3,
    P1Start,
    P2Start,
    P2Service,
    Test,
    Count,
};

enum class InputMode : std::uint8_t {
    Keyboard,
    Controller,
};

enum class GameplayInputStyle : std::uint8_t {
    Arcade,
    Switch,
};

enum class ControllerBackend : std::uint8_t {
    XInput,
    RawHid,
};

enum class ControlDirection : std::uint8_t {
    Positive,
    Negative,
    Up,
    Down,
    Left,
    Right,
};

enum class DigitalControlType : std::uint8_t {
    XInputButton,
    XInputAxis,
    XInputTrigger,
    RawHidButton,
    RawHidValue,
    RawHidHat,
};

enum class XInputControl : std::uint8_t {
    A,
    B,
    X,
    Y,
    DPadUp,
    DPadDown,
    DPadLeft,
    DPadRight,
    Start,
    Back,
    LeftShoulder,
    RightShoulder,
    LeftThumb,
    RightThumb,
    LeftX,
    LeftY,
    RightX,
    RightY,
    LeftTrigger,
    RightTrigger,
};

struct ControllerIdentity {
    ControllerBackend backend{ControllerBackend::XInput};
    std::string device_id{"0"};

    auto operator<=>(const ControllerIdentity&) const = default;
};

struct DigitalControlBinding {
    LogicalAction action{LogicalAction::LeftBoosterUp};
    DigitalControlType type{DigitalControlType::XInputButton};
    std::optional<XInputControl> control;
    std::optional<ControlDirection> direction;
    std::optional<std::uint32_t> usage_page;
    std::optional<std::uint32_t> usage;
    std::optional<std::uint32_t> link_collection;
    std::optional<std::uint32_t> report_id;
    std::optional<std::int32_t> neutral_value;

    auto operator<=>(const DigitalControlBinding&) const = default;
};

struct KeyboardBinding {
    LogicalAction action{};
    PhysicalKey key{};
};

constexpr bool IsGameplayAction(LogicalAction action) noexcept
{
    return action <= LogicalAction::RightBoosterButton;
}

} // namespace gc::input

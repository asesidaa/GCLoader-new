#include "Config/NativeInputConfig.h"

#include "Input/Types/InputTypes.h"

#include <rfl/toml.hpp>

#include <cstdint>
#include <expected>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>

namespace {

struct NativeInputContract {
    rfl::Rename<"input_schema_version", std::uint32_t>
        input_schema_version{gc::config::kInputSchemaVersion};
    rfl::Rename<"input_poll_hz", std::uint32_t> input_poll_hz{1000};
    rfl::Rename<"input_mode", gc::input::InputMode>
        input_mode{gc::input::InputMode::Keyboard};
    rfl::Rename<"gameplay_input_style", gc::input::GameplayInputStyle>
        gameplay_input_style{gc::input::GameplayInputStyle::Arcade};
    rfl::Rename<"axis_press_threshold_percent", std::uint32_t>
        axis_press_threshold_percent{50};
    rfl::Rename<"axis_release_threshold_percent", std::uint32_t>
        axis_release_threshold_percent{40};
    rfl::Rename<"keyboard", gc::config::NativeKeyboardConfig> keyboard;
    rfl::Rename<"controller", gc::config::ControllerConfig> controller;
};

constexpr std::string_view kValidConfig = R"toml(
input_schema_version = 2
input_poll_hz = 1000
input_mode = 'Keyboard'
gameplay_input_style = 'Arcade'
axis_press_threshold_percent = 50
axis_release_threshold_percent = 40

[keyboard]
left_booster_up = 'sc:0011'
left_booster_down = 'sc:001f'
left_booster_left = 'sc:001e'
left_booster_right = 'sc:0020'
left_booster_button = 'sc:0039'
right_booster_up = 'e0:0048'
right_booster_down = 'e0:0050'
right_booster_left = 'e0:004b'
right_booster_right = 'e0:004d'
right_booster_button = 'sc:0025'
test = 'sc:0014'
service1 = 'sc:003b'
service2 = 'sc:0017'
service3 = 'sc:0019'
p1_start = 'sc:0002'
p2_start = 'sc:0003'
p2_service = 'sc:003c'
card_read = 'sc:003e'

[controller]
backend = 'XInput'
device_id = '0'
bindings = [
  { action = 'LeftBoosterUp', type = 'XInputButton', control = 'DPadUp' },
  { action = 'LeftBoosterUp', type = 'XInputAxis', control = 'LeftY', direction = 'Negative' },
  { action = 'LeftBoosterDown', type = 'XInputButton', control = 'DPadDown' },
  { action = 'LeftBoosterDown', type = 'XInputAxis', control = 'LeftY', direction = 'Positive' },
  { action = 'LeftBoosterLeft', type = 'XInputButton', control = 'DPadLeft' },
  { action = 'LeftBoosterLeft', type = 'XInputAxis', control = 'LeftX', direction = 'Negative' },
  { action = 'LeftBoosterRight', type = 'XInputButton', control = 'DPadRight' },
  { action = 'LeftBoosterRight', type = 'XInputAxis', control = 'LeftX', direction = 'Positive' },
  { action = 'LeftBoosterButton', type = 'XInputButton', control = 'A' },
  { action = 'RightBoosterUp', type = 'XInputAxis', control = 'RightY', direction = 'Negative' },
  { action = 'RightBoosterDown', type = 'XInputAxis', control = 'RightY', direction = 'Positive' },
  { action = 'RightBoosterLeft', type = 'XInputAxis', control = 'RightX', direction = 'Negative' },
  { action = 'RightBoosterRight', type = 'XInputAxis', control = 'RightX', direction = 'Positive' },
  { action = 'RightBoosterButton', type = 'XInputButton', control = 'B' },
]
)toml";

std::expected<NativeInputContract, std::string> parse_and_validate(
    std::string_view text)
{
    auto parsed = rfl::toml::read<NativeInputContract>(std::string{text});
    if (!parsed)
    {
        return std::unexpected(parsed.error().what());
    }

    auto value = std::move(parsed.value());
    const auto validation = gc::config::ValidateNativeInputFields(
        value.input_schema_version(),
        value.input_poll_hz(),
        value.axis_press_threshold_percent(),
        value.axis_release_threshold_percent(),
        value.keyboard(),
        value.controller());
    if (!validation)
    {
        return std::unexpected(validation.error());
    }
    return value;
}

std::string replace_once(
    std::string text,
    std::string_view from,
    std::string_view to)
{
    const auto position = text.find(from);
    if (position == std::string::npos)
    {
        std::cerr << "fixture token not found: " << from << '\n';
        std::exit(2);
    }
    text.replace(position, from.size(), to);
    return text;
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

int expect_false(bool actual, std::string_view name)
{
    if (!actual)
    {
        return 0;
    }
    std::cerr << name << ": expected false\n";
    return 1;
}

int expect_error_contains(
    const std::expected<void, std::string>& result,
    std::string_view expected,
    std::string_view name)
{
    if (!result && result.error().find(expected) != std::string::npos)
    {
        return 0;
    }
    std::cerr << name << ": expected error containing '" << expected
              << "', got '" << (result ? "success" : result.error())
              << "'\n";
    return 1;
}

int expect_parse_failure(
    std::string text,
    std::string_view expected,
    std::string_view name)
{
    const auto result = parse_and_validate(text);
    if (!result && result.error().find(expected) != std::string::npos)
    {
        return 0;
    }
    std::cerr << name << ": expected failure containing '" << expected
              << "', got '" << (result ? "success" : result.error())
              << "'\n";
    return 1;
}

gc::input::DigitalControlBinding xinput_button()
{
    return {
        .action = gc::input::LogicalAction::LeftBoosterButton,
        .type = gc::input::DigitalControlType::XInputButton,
        .control = gc::input::XInputControl::A,
    };
}

gc::input::DigitalControlBinding xinput_axis()
{
    return {
        .action = gc::input::LogicalAction::LeftBoosterUp,
        .type = gc::input::DigitalControlType::XInputAxis,
        .control = gc::input::XInputControl::LeftY,
        .direction = gc::input::ControlDirection::Negative,
    };
}

gc::input::DigitalControlBinding xinput_trigger()
{
    return {
        .action = gc::input::LogicalAction::LeftBoosterButton,
        .type = gc::input::DigitalControlType::XInputTrigger,
        .control = gc::input::XInputControl::LeftTrigger,
    };
}

gc::input::DigitalControlBinding raw_button()
{
    return {
        .action = gc::input::LogicalAction::LeftBoosterButton,
        .type = gc::input::DigitalControlType::RawHidButton,
        .usage_page = 9,
        .usage = 1,
        .link_collection = 0,
        .report_id = 1,
    };
}

gc::input::DigitalControlBinding raw_value()
{
    return {
        .action = gc::input::LogicalAction::LeftBoosterRight,
        .type = gc::input::DigitalControlType::RawHidValue,
        .direction = gc::input::ControlDirection::Positive,
        .usage_page = 1,
        .usage = 0x30,
        .link_collection = 0,
        .report_id = 1,
        .neutral_value = 0,
    };
}

gc::input::DigitalControlBinding raw_hat()
{
    return {
        .action = gc::input::LogicalAction::LeftBoosterUp,
        .type = gc::input::DigitalControlType::RawHidHat,
        .direction = gc::input::ControlDirection::Up,
        .usage_page = 1,
        .usage = 0x39,
        .link_collection = 0,
        .report_id = 1,
    };
}

std::expected<void, std::string> validate_controller(
    gc::input::ControllerBackend backend,
    std::string device_id,
    std::vector<gc::input::DigitalControlBinding> bindings)
{
    auto parsed = parse_and_validate(kValidConfig);
    if (!parsed)
    {
        return std::unexpected(parsed.error());
    }
    auto value = std::move(parsed.value());
    value.controller().backend = backend;
    value.controller().device_id = std::move(device_id);
    value.controller().bindings = std::move(bindings);
    return gc::config::ValidateNativeInputFields(
        value.input_schema_version(),
        value.input_poll_hz(),
        value.axis_press_threshold_percent(),
        value.axis_release_threshold_percent(),
        value.keyboard(),
        value.controller());
}

} // namespace

int main()
{
    using namespace gc::input;

    int failures = 0;

    const auto valid = parse_and_validate(kValidConfig);
    failures += expect_true(valid.has_value(), "valid native input config");
    if (!valid)
    {
        std::cerr << valid.error() << '\n';
        return 1;
    }

    failures += expect_true(
        valid->keyboard().test() ==
            PhysicalKey{0x14, ScanCodePrefix::None},
        "Test physical key");
    failures += expect_true(
        valid->keyboard().right_booster_up() ==
            PhysicalKey{0x48, ScanCodePrefix::E0},
        "E0 physical key");
    failures += expect_true(
        valid->controller().bindings().size() == 14,
        "complete XInput defaults");

    const auto serialized = rfl::toml::write(valid.value());
    failures += expect_true(
        serialized.find("test = 'sc:0014'") != std::string::npos,
        "canonical Test token serialization");
    failures += expect_true(
        serialized.find("right_booster_up = 'e0:0048'") !=
            std::string::npos,
        "canonical E0 serialization");
    const auto reparsed = parse_and_validate(serialized);
    failures += expect_true(reparsed.has_value(), "native config round trip");
    if (reparsed)
    {
        failures += expect_true(
            reparsed->controller().bindings() ==
                valid->controller().bindings(),
            "binding round trip");
    }

    const std::string source{kValidConfig};
    failures += expect_parse_failure(
        replace_once(source, "input_schema_version = 2\n", ""),
        "input_schema_version",
        "missing schema version");
    failures += expect_parse_failure(
        replace_once(source, "input_schema_version = 2", "input_schema_version = 1"),
        "input_schema_version",
        "wrong schema version");
    failures += expect_parse_failure(
        replace_once(source, "input_poll_hz = 1000", "input_poll_hz = 60"),
        "input_poll_hz",
        "unsupported poll rate");
    failures += expect_parse_failure(
        replace_once(source, "axis_press_threshold_percent = 50", "axis_press_threshold_percent = 101"),
        "threshold",
        "press outside range");
    failures += expect_parse_failure(
        replace_once(source, "axis_release_threshold_percent = 40", "axis_release_threshold_percent = 101"),
        "threshold",
        "release outside range");
    failures += expect_parse_failure(
        replace_once(source, "axis_release_threshold_percent = 40", "axis_release_threshold_percent = 50"),
        "lower",
        "equal thresholds");
    failures += expect_parse_failure(
        replace_once(source, "test = 'sc:0014'", "test = 't'"),
        "keyboard.test",
        "malformed physical key");
    failures += expect_parse_failure(
        replace_once(source, "test = 'sc:0014'", "test = 'sc:0000'"),
        "keyboard.test",
        "zero physical key");

    for (const auto rate : {125u, 250u, 500u, 1000u})
    {
        auto value = valid.value();
        value.input_poll_hz = rate;
        failures += expect_true(
            gc::config::ValidateNativeInputFields(
                value.input_schema_version(),
                value.input_poll_hz(),
                value.axis_press_threshold_percent(),
                value.axis_release_threshold_percent(),
                value.keyboard(),
                value.controller()).has_value(),
            "supported poll rate");
    }

    for (const std::string slot : {"0", "1", "2", "3"})
    {
        failures += expect_true(
            validate_controller(
                ControllerBackend::XInput,
                slot,
                {xinput_button(), xinput_axis(), xinput_trigger()})
                .has_value(),
            "valid XInput slot and binding families");
    }
    for (const std::string slot : {"", "-1", "00", "4"})
    {
        failures += expect_error_contains(
            validate_controller(
                ControllerBackend::XInput,
                slot,
                {xinput_button()}),
            "device_id",
            "invalid XInput slot");
    }

    failures += expect_true(
        validate_controller(
            ControllerBackend::RawHid,
            R"(\\?\HID#VID_1234&PID_5678#exact-path)",
            {raw_button(), raw_value(), raw_hat()}).has_value(),
        "valid Raw HID bindings");
    failures += expect_error_contains(
        validate_controller(
            ControllerBackend::RawHid,
            "",
            {raw_button()}),
        "device_id",
        "empty Raw HID path");

    auto invalid = xinput_button();
    invalid.action = LogicalAction::Test;
    failures += expect_error_contains(
        validate_controller(ControllerBackend::XInput, "0", {invalid}),
        "bindings[0]",
        "controller system action");

    failures += expect_error_contains(
        validate_controller(
            ControllerBackend::RawHid,
            "raw-path",
            {xinput_button()}),
        "bindings[0]",
        "Raw HID backend mismatch");
    failures += expect_error_contains(
        validate_controller(
            ControllerBackend::XInput,
            "0",
            {raw_button()}),
        "bindings[0]",
        "XInput backend mismatch");

    invalid = xinput_button();
    invalid.control.reset();
    failures += expect_error_contains(
        validate_controller(ControllerBackend::XInput, "0", {invalid}),
        "bindings[0]",
        "XInput button missing control");
    invalid = xinput_button();
    invalid.direction = ControlDirection::Positive;
    failures += expect_error_contains(
        validate_controller(ControllerBackend::XInput, "0", {invalid}),
        "bindings[0]",
        "XInput button extraneous direction");

    invalid = xinput_axis();
    invalid.direction.reset();
    failures += expect_error_contains(
        validate_controller(ControllerBackend::XInput, "0", {invalid}),
        "bindings[0]",
        "XInput axis missing direction");
    invalid = xinput_axis();
    invalid.control = XInputControl::A;
    failures += expect_error_contains(
        validate_controller(ControllerBackend::XInput, "0", {invalid}),
        "bindings[0]",
        "XInput axis wrong control family");
    invalid = xinput_axis();
    invalid.direction = ControlDirection::Up;
    failures += expect_error_contains(
        validate_controller(ControllerBackend::XInput, "0", {invalid}),
        "bindings[0]",
        "XInput axis wrong direction family");

    invalid = xinput_trigger();
    invalid.control = XInputControl::LeftX;
    failures += expect_error_contains(
        validate_controller(ControllerBackend::XInput, "0", {invalid}),
        "bindings[0]",
        "XInput trigger wrong control family");

    invalid = raw_button();
    invalid.usage.reset();
    failures += expect_error_contains(
        validate_controller(ControllerBackend::RawHid, "raw-path", {invalid}),
        "bindings[0]",
        "Raw HID button missing address");
    invalid = raw_button();
    invalid.report_id = 256;
    failures += expect_error_contains(
        validate_controller(ControllerBackend::RawHid, "raw-path", {invalid}),
        "bindings[0]",
        "Raw HID report ID overflow");

    invalid = raw_value();
    invalid.neutral_value.reset();
    failures += expect_error_contains(
        validate_controller(ControllerBackend::RawHid, "raw-path", {invalid}),
        "bindings[0]",
        "Raw HID value missing neutral");
    invalid = raw_value();
    invalid.direction = ControlDirection::Left;
    failures += expect_error_contains(
        validate_controller(ControllerBackend::RawHid, "raw-path", {invalid}),
        "bindings[0]",
        "Raw HID value wrong direction family");

    invalid = raw_hat();
    invalid.direction = ControlDirection::Positive;
    failures += expect_error_contains(
        validate_controller(ControllerBackend::RawHid, "raw-path", {invalid}),
        "bindings[0]",
        "Raw HID hat non-cardinal direction");
    invalid = raw_hat();
    invalid.neutral_value = 0;
    failures += expect_error_contains(
        validate_controller(ControllerBackend::RawHid, "raw-path", {invalid}),
        "bindings[0]",
        "Raw HID hat extraneous neutral");

    if (failures != 0)
    {
        return 1;
    }

    std::cout << "NativeInputConfigTests passed\n";
    return 0;
}

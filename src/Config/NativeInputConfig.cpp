#include "Config/NativeInputConfig.h"

#include "Input/Types/DigitalLatch.h"

#include <array>
#include <format>
#include <limits>
#include <string_view>

namespace gc::config {
namespace {

using input::ControlDirection;
using input::ControllerBackend;
using input::DigitalControlBinding;
using input::DigitalControlType;
using input::PhysicalKey;
using input::XInputControl;

bool IsSupportedPollRate(std::uint32_t poll_hz) noexcept
{
    return poll_hz == 125 || poll_hz == 250 ||
           poll_hz == 500 || poll_hz == 1000;
}

bool IsXInputButton(XInputControl control) noexcept
{
    return control >= XInputControl::A &&
           control <= XInputControl::RightThumb;
}

bool IsXInputAxis(XInputControl control) noexcept
{
    return control >= XInputControl::LeftX &&
           control <= XInputControl::RightY;
}

bool IsXInputTrigger(XInputControl control) noexcept
{
    return control == XInputControl::LeftTrigger ||
           control == XInputControl::RightTrigger;
}

bool IsAxisDirection(ControlDirection direction) noexcept
{
    return direction == ControlDirection::Positive ||
           direction == ControlDirection::Negative;
}

bool IsCardinalDirection(ControlDirection direction) noexcept
{
    return direction == ControlDirection::Up ||
           direction == ControlDirection::Down ||
           direction == ControlDirection::Left ||
           direction == ControlDirection::Right;
}

std::expected<void, std::string> ValidatePhysicalKey(
    std::string_view field,
    PhysicalKey key)
{
    if (key.make_code == 0)
    {
        return std::unexpected(
            std::format("Invalid keyboard.{} physical scan-code token", field));
    }
    return {};
}

std::expected<void, std::string> ValidateHidAddress(
    const DigitalControlBinding& binding)
{
    if (!binding.usage_page || !binding.usage ||
        !binding.link_collection || !binding.report_id)
    {
        return std::unexpected(
            "requires usage_page, usage, link_collection, and report_id");
    }
    if (*binding.usage_page == 0 || *binding.usage == 0)
    {
        return std::unexpected("usage_page and usage must be nonzero");
    }
    constexpr auto kUshortMax =
        static_cast<std::uint32_t>(std::numeric_limits<std::uint16_t>::max());
    if (*binding.usage_page > kUshortMax ||
        *binding.usage > kUshortMax ||
        *binding.link_collection > kUshortMax)
    {
        return std::unexpected("HID address field exceeds 16-bit range");
    }
    if (*binding.report_id > std::numeric_limits<std::uint8_t>::max())
    {
        return std::unexpected("report_id exceeds 8-bit range");
    }
    return {};
}

bool HasAnyHidAddress(const DigitalControlBinding& binding) noexcept
{
    return binding.usage_page || binding.usage ||
           binding.link_collection || binding.report_id;
}

std::expected<void, std::string> ValidateBindingFields(
    const DigitalControlBinding& binding)
{
    const auto no_hid_fields = [&]() -> std::expected<void, std::string> {
        if (HasAnyHidAddress(binding) || binding.neutral_value)
        {
            return std::unexpected(
                "XInput binding contains Raw HID fields");
        }
        return {};
    };

    switch (binding.type)
    {
    case DigitalControlType::XInputButton:
        if (!binding.control || !IsXInputButton(*binding.control))
        {
            return std::unexpected("requires an XInput button control");
        }
        if (binding.direction)
        {
            return std::unexpected("XInput button must not have direction");
        }
        return no_hid_fields();

    case DigitalControlType::XInputAxis:
        if (!binding.control || !IsXInputAxis(*binding.control))
        {
            return std::unexpected("requires an XInput axis control");
        }
        if (!binding.direction || !IsAxisDirection(*binding.direction))
        {
            return std::unexpected(
                "requires Positive or Negative direction");
        }
        return no_hid_fields();

    case DigitalControlType::XInputTrigger:
        if (!binding.control || !IsXInputTrigger(*binding.control))
        {
            return std::unexpected("requires an XInput trigger control");
        }
        if (binding.direction)
        {
            return std::unexpected("XInput trigger must not have direction");
        }
        return no_hid_fields();

    case DigitalControlType::RawHidButton:
        if (binding.control || binding.direction || binding.neutral_value)
        {
            return std::unexpected(
                "Raw HID button contains unsupported fields");
        }
        return ValidateHidAddress(binding);

    case DigitalControlType::RawHidValue:
        if (binding.control)
        {
            return std::unexpected(
                "Raw HID value must not have XInput control");
        }
        if (!binding.direction || !IsAxisDirection(*binding.direction))
        {
            return std::unexpected(
                "requires Positive or Negative direction");
        }
        if (!binding.neutral_value)
        {
            return std::unexpected("requires neutral_value");
        }
        return ValidateHidAddress(binding);

    case DigitalControlType::RawHidHat:
        if (binding.control || binding.neutral_value)
        {
            return std::unexpected(
                "Raw HID hat contains unsupported fields");
        }
        if (!binding.direction || !IsCardinalDirection(*binding.direction))
        {
            return std::unexpected("requires a cardinal direction");
        }
        return ValidateHidAddress(binding);
    }

    return std::unexpected("unknown binding type");
}

bool IsXInputType(DigitalControlType type) noexcept
{
    return type == DigitalControlType::XInputButton ||
           type == DigitalControlType::XInputAxis ||
           type == DigitalControlType::XInputTrigger;
}

std::expected<void, std::string> ValidateController(
    const ControllerConfig& controller)
{
    if (controller.backend() == ControllerBackend::XInput)
    {
        const auto& slot = controller.device_id();
        if (slot != "0" && slot != "1" && slot != "2" && slot != "3")
        {
            return std::unexpected(
                "Invalid controller.device_id; XInput requires exactly 0, 1, 2, or 3");
        }
    }
    else if (controller.backend() == ControllerBackend::RawHid)
    {
        if (controller.device_id().empty())
        {
            return std::unexpected(
                "Invalid controller.device_id; RawHid path must not be empty");
        }
    }
    else
    {
        return std::unexpected("Invalid controller.backend");
    }

    const auto& bindings = controller.bindings();
    for (std::size_t index = 0; index < bindings.size(); ++index)
    {
        const auto& binding = bindings[index];
        const auto error_prefix =
            std::format("Invalid controller.bindings[{}]: ", index);

        if (!input::IsGameplayAction(binding.action))
        {
            return std::unexpected(
                error_prefix + "controller actions must be gameplay actions");
        }

        const bool xinput_type = IsXInputType(binding.type);
        if ((controller.backend() == ControllerBackend::XInput) != xinput_type)
        {
            return std::unexpected(
                error_prefix + "binding type does not match controller backend");
        }

        const auto fields = ValidateBindingFields(binding);
        if (!fields)
        {
            return std::unexpected(error_prefix + fields.error());
        }
    }
    return {};
}

} // namespace

std::expected<void, std::string> ValidateNativeInputFields(
    std::uint32_t schema_version,
    std::uint32_t poll_hz,
    std::uint32_t press_percent,
    std::uint32_t release_percent,
    const NativeKeyboardConfig& keyboard,
    const ControllerConfig& controller)
{
    if (schema_version != kInputSchemaVersion)
    {
        return std::unexpected(
            "Invalid input_schema_version; expected 2");
    }
    if (!IsSupportedPollRate(poll_hz))
    {
        return std::unexpected(
            "Invalid input_poll_hz; expected one of 125, 250, 500, or 1000");
    }

    const auto latch = input::DigitalLatch::Create(
        press_percent, release_percent);
    if (!latch)
    {
        return std::unexpected(
            "Invalid axis threshold: " + latch.error());
    }

    const std::array keyboard_fields{
        std::pair{"left_booster_up", keyboard.left_booster_up()},
        std::pair{"left_booster_down", keyboard.left_booster_down()},
        std::pair{"left_booster_left", keyboard.left_booster_left()},
        std::pair{"left_booster_right", keyboard.left_booster_right()},
        std::pair{"left_booster_button", keyboard.left_booster_button()},
        std::pair{"right_booster_up", keyboard.right_booster_up()},
        std::pair{"right_booster_down", keyboard.right_booster_down()},
        std::pair{"right_booster_left", keyboard.right_booster_left()},
        std::pair{"right_booster_right", keyboard.right_booster_right()},
        std::pair{"right_booster_button", keyboard.right_booster_button()},
        std::pair{"test", keyboard.test()},
        std::pair{"service1", keyboard.service1()},
        std::pair{"service2", keyboard.service2()},
        std::pair{"service3", keyboard.service3()},
        std::pair{"p1_start", keyboard.p1_start()},
        std::pair{"p2_start", keyboard.p2_start()},
        std::pair{"p2_service", keyboard.p2_service()},
        std::pair{"card_read", keyboard.card_read()},
    };
    for (const auto& [name, key] : keyboard_fields)
    {
        const auto validation = ValidatePhysicalKey(name, key);
        if (!validation)
        {
            return validation;
        }
    }

    return ValidateController(controller);
}

} // namespace gc::config

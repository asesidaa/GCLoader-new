#include "Config/Validation/InputValidation.h"
#include "Config/Validation/CommonValidation.h"
#include "Config/DeclaredEnum.h"
#include <array>
#include <limits>

namespace gc::config::validation {
namespace {
using InputPollValidator = rfl::Validator<
    std::uint32_t,
    rfl::OneOf<
        rfl::EqualTo<125>
        ,
        rfl::EqualTo<250>
        ,
        rfl::EqualTo<500>
        ,
        rfl::EqualTo<1000>
    >
>;
using PercentValidator =
rfl::Validator<std::uint32_t, rfl::Maximum<100>>;
struct ValidPhysicalKeyRule
{
    [[maybe_unused]] static rfl::Result<input::PhysicalKey> validate(
        input::PhysicalKey key) noexcept
    {
        if (key.make_code != 0)
        {
            return key;
        }
        return rfl::error("physical scan-code token must be nonzero");
    }
};

using PhysicalKeyValidator =
rfl::Validator<input::PhysicalKey, ValidPhysicalKeyRule>;
bool IsXInputButton(input::XInputControl control) noexcept
{
    return control >= input::XInputControl::A &&
        control <= input::XInputControl::RightThumb;
}

bool IsXInputAxis(input::XInputControl control) noexcept
{
    return control >= input::XInputControl::LeftX &&
        control <= input::XInputControl::RightY;
}

bool IsXInputTrigger(input::XInputControl control) noexcept
{
    return control == input::XInputControl::LeftTrigger ||
        control == input::XInputControl::RightTrigger;
}

bool IsAxisDirection(input::ControlDirection direction) noexcept
{
    return direction == input::ControlDirection::Positive ||
        direction == input::ControlDirection::Negative;
}

bool IsCardinalDirection(input::ControlDirection direction) noexcept
{
    return direction == input::ControlDirection::Up ||
        direction == input::ControlDirection::Down ||
        direction == input::ControlDirection::Left ||
        direction == input::ControlDirection::Right;
}

bool IsXInputType(input::DigitalControlType type) noexcept
{
    return type == input::DigitalControlType::XInputButton ||
        type == input::DigitalControlType::XInputAxis ||
        type == input::DigitalControlType::XInputTrigger;
}

bool HasCompleteHidAddress(
    const input::DigitalControlBinding& binding) noexcept
{
    if (!binding.usage_page || !binding.usage ||
        !binding.link_collection || !binding.report_id)
    {
        return false;
    }
    return *binding.usage_page != 0 && *binding.usage != 0 &&
        *binding.usage_page <=
        std::numeric_limits<std::uint16_t>::max() &&
        *binding.usage <= std::numeric_limits<std::uint16_t>::max() &&
        *binding.link_collection <=
        std::numeric_limits<std::uint16_t>::max() &&
        *binding.report_id <= std::numeric_limits<std::uint8_t>::max();
}

bool BindingFieldsValid(
    const input::DigitalControlBinding& binding) noexcept
{
    const bool any_hid =
        binding.usage_page || binding.usage ||
        binding.link_collection || binding.report_id;
    switch (binding.type)
    {
    case input::DigitalControlType::XInputButton:
        return binding.control &&
            IsXInputButton(*binding.control) &&
            !binding.direction && !any_hid && !binding.neutral_value;
    case input::DigitalControlType::XInputAxis:
        return binding.control &&
            IsXInputAxis(*binding.control) &&
            binding.direction &&
            IsAxisDirection(*binding.direction) &&
            !any_hid && !binding.neutral_value;
    case input::DigitalControlType::XInputTrigger:
        return binding.control &&
            IsXInputTrigger(*binding.control) &&
            !binding.direction && !any_hid && !binding.neutral_value;
    case input::DigitalControlType::RawHidButton:
        return !binding.control && !binding.direction &&
            !binding.neutral_value && HasCompleteHidAddress(binding);
    case input::DigitalControlType::RawHidValue:
        return !binding.control && binding.direction &&
            IsAxisDirection(*binding.direction) &&
            binding.neutral_value && HasCompleteHidAddress(binding);
    case input::DigitalControlType::RawHidHat:
        return !binding.control && !binding.neutral_value &&
            binding.direction &&
            IsCardinalDirection(*binding.direction) &&
            HasCompleteHidAddress(binding);
    }
    return false;
}

std::array<std::pair<std::string_view, input::PhysicalKey>, 18>
KeyboardFields(const NativeKeyboardConfig& keyboard)
{
    return {
        {
            {"left_booster_up", keyboard.left_booster_up()},
            {"left_booster_down", keyboard.left_booster_down()},
            {"left_booster_left", keyboard.left_booster_left()},
            {"left_booster_right", keyboard.left_booster_right()},
            {"left_booster_button", keyboard.left_booster_button()},
            {"right_booster_up", keyboard.right_booster_up()},
            {"right_booster_down", keyboard.right_booster_down()},
            {"right_booster_left", keyboard.right_booster_left()},
            {"right_booster_right", keyboard.right_booster_right()},
            {"right_booster_button", keyboard.right_booster_button()},
            {"test", keyboard.test()},
            {"service1", keyboard.service1()},
            {"service2", keyboard.service2()},
            {"service3", keyboard.service3()},
            {"p1_start", keyboard.p1_start()},
            {"p2_start", keyboard.p2_start()},
            {"p2_service", keyboard.p2_service()},
            {"card_read", keyboard.card_read()},
        }
    };
}


}
std::vector<input::KeyboardBinding> CompileKeyboard(
    const NativeKeyboardConfig& keyboard)
{
    using enum input::LogicalAction;
    return {
        {LeftBoosterUp, keyboard.left_booster_up()},
        {LeftBoosterDown, keyboard.left_booster_down()},
        {LeftBoosterLeft, keyboard.left_booster_left()},
        {LeftBoosterRight, keyboard.left_booster_right()},
        {LeftBoosterButton, keyboard.left_booster_button()},
        {RightBoosterUp, keyboard.right_booster_up()},
        {RightBoosterDown, keyboard.right_booster_down()},
        {RightBoosterLeft, keyboard.right_booster_left()},
        {RightBoosterRight, keyboard.right_booster_right()},
        {RightBoosterButton, keyboard.right_booster_button()},
        {Test, keyboard.test()},
        {Service1, keyboard.service1()},
        {Service2, keyboard.service2()},
        {Service3, keyboard.service3()},
        {P1Start, keyboard.p1_start()},
        {P2Start, keyboard.p2_start()},
        {P2Service, keyboard.p2_service()},
    };
}

bool ValidateInput(const ConfigDocument& document, ValidationContext& context) {
    auto& errors = context.errors;
    if (document.input_schema_version() != kInputSchemaVersion)
    {
        errors.push_back({
            .path = ConfigPath{"input_schema_version"},
            .code = ConfigErrorCode::unsupported_value,
            .message = "expected current input schema version",
        });
    }
    const bool poll_valid = ValidateLeaf<InputPollValidator>(
        document.input_poll_hz(),
        ConfigPath{"input_poll_hz"},
        ConfigErrorCode::unsupported_value,
        "expected one of 125, 250, 500, or 1000",
        errors);
    const bool mode_valid =
        IsDeclaredEnumValue(document.input_mode());
    if (!mode_valid)
    {
        errors.push_back({
            .path = ConfigPath{"input_mode"},
            .code = ConfigErrorCode::unsupported_value,
            .message = "unsupported input mode",
        });
    }
    if (!IsDeclaredEnumValue(document.gameplay_input_style()))
    {
        errors.push_back({
            .path = ConfigPath{"gameplay_input_style"},
            .code = ConfigErrorCode::unsupported_value,
            .message = "unsupported gameplay input style",
        });
    }
    const bool press_valid = ValidateLeaf<PercentValidator>(
        document.axis_press_threshold_percent(),
        ConfigPath{"axis_press_threshold_percent"},
        ConfigErrorCode::out_of_range,
        "expected a percentage from 0 through 100",
        errors);
    const bool release_valid = ValidateLeaf<PercentValidator>(
        document.axis_release_threshold_percent(),
        ConfigPath{"axis_release_threshold_percent"},
        ConfigErrorCode::out_of_range,
        "expected a percentage from 0 through 100",
        errors);
    if (press_valid && release_valid &&
        document.axis_release_threshold_percent() >=
        document.axis_press_threshold_percent())
    {
        errors.push_back({
            .path = ConfigPath{"axis_release_threshold_percent"},
            .code = ConfigErrorCode::incompatible_fields,
            .message = "release threshold must be below press threshold",
            .related_paths = {
                ConfigPath{"axis_press_threshold_percent"},
            },
        });
    }

    for (const auto& [name, key] :
         KeyboardFields(document.keyboard()))
    {
        ValidateLeaf<PhysicalKeyValidator>(
            key,
            ConfigPath{"keyboard"}.Child(std::string{name}),
            ConfigErrorCode::invalid_value,
            "physical scan-code token must be nonzero",
            errors);
    }

    const auto backend = document.controller().backend();
    const bool backend_valid = IsDeclaredEnumValue(backend);
    if (!backend_valid)
    {
        errors.push_back({
            .path = ConfigPath{"controller", "backend"},
            .code = ConfigErrorCode::unsupported_value,
            .message = "unsupported controller backend",
        });
    }
    bool device_valid = false;
    if (backend == input::ControllerBackend::XInput)
    {
        const auto& id = document.controller().device_id();
        device_valid =
            id == "0" || id == "1" || id == "2" || id == "3";
    }
    else if (backend == input::ControllerBackend::RawHid)
    {
        device_valid = !document.controller().device_id().empty();
    }
    if (backend_valid && !device_valid)
    {
        errors.push_back({
            .path = ConfigPath{"controller", "device_id"},
            .code = ConfigErrorCode::invalid_value,
            .message = "device identity does not match the backend",
            .related_paths = {ConfigPath{"controller", "backend"}},
        });
    }
    for (std::size_t index = 0;
         index < document.controller().bindings().size();
         ++index)
    {
        const auto& binding =
            document.controller().bindings()[index];
        const auto binding_path =
            ConfigPath{"controller", "bindings"}.Index(index);
        if (!input::IsGameplayAction(binding.action))
        {
            errors.push_back({
                .path = binding_path.Child("action"),
                .code = ConfigErrorCode::invalid_value,
                .message =
                "controller actions must be gameplay actions",
            });
            continue;
        }
        const bool xinput_type = IsXInputType(binding.type);
        if (backend_valid &&
            ((backend == input::ControllerBackend::XInput) !=
                xinput_type))
        {
            errors.push_back({
                .path = binding_path.Child("type"),
                .code = ConfigErrorCode::incompatible_fields,
                .message =
                "binding type does not match controller backend",
                .related_paths = {
                    ConfigPath{"controller", "backend"},
                },
            });
            continue;
        }
        if (!BindingFieldsValid(binding))
        {
            errors.push_back({
                .path = binding_path.Child("type"),
                .code = ConfigErrorCode::invalid_value,
                .message =
                "binding fields do not match the selected type",
            });
        }
    }


    return poll_valid;
}
}

#include "Input/Polling/InputMapper.h"
#include "Input/Polling/InputSnapshotState.h"
#include "Input/Win32/ControllerBindingEvaluator.h"
#include "Input/Win32/ControllerStateView.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace {

class FakeControllerView final : public gc::input::ControllerStateView {
public:
    explicit FakeControllerView(
        std::vector<gc::input::DigitalControlBinding> bindings)
        : identity_{gc::input::ControllerBackend::XInput, "0"},
          activations_(bindings.size())
    {
        for (auto& binding : bindings)
        {
            descriptors_.push_back({
                .binding = std::move(binding),
                .label = "Fake control",
            });
        }
    }

    const gc::input::ControllerIdentity& identity() const noexcept override
    {
        return identity_;
    }

    std::span<const gc::input::ControllerControlDescriptor>
    controls() const noexcept override
    {
        return descriptors_;
    }

    std::optional<double> Activation(
        const gc::input::DigitalControlBinding& binding) const noexcept override
    {
        for (std::size_t index = 0; index < descriptors_.size(); ++index)
        {
            if (descriptors_[index].binding == binding)
            {
                return activations_[index];
            }
        }
        return std::nullopt;
    }

    std::optional<std::int32_t> RawValue(
        const gc::input::DigitalControlBinding&) const noexcept override
    {
        return std::nullopt;
    }

    void Set(std::size_t index, double activation)
    {
        activations_[index] = activation;
    }

private:
    gc::input::ControllerIdentity identity_;
    std::vector<gc::input::ControllerControlDescriptor> descriptors_;
    std::vector<double> activations_;
};

int expect_word(
    std::uint32_t actual,
    std::uint32_t expected,
    std::string_view name)
{
    if (actual == expected)
    {
        return 0;
    }
    std::cerr << name << ": expected 0x" << std::hex << expected
              << ", got 0x" << actual << std::dec << '\n';
    return 1;
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

gc::input::DigitalControlBinding xinput_binding(
    gc::input::LogicalAction action,
    gc::input::DigitalControlType type,
    gc::input::XInputControl control,
    std::optional<gc::input::ControlDirection> direction = std::nullopt)
{
    return {
        .action = action,
        .type = type,
        .control = control,
        .direction = direction,
    };
}

gc::input::DigitalControlBinding hat_binding(
    gc::input::LogicalAction action,
    gc::input::ControlDirection direction)
{
    return {
        .action = action,
        .type = gc::input::DigitalControlType::RawHidHat,
        .direction = direction,
        .usage_page = 1,
        .usage = 0x39,
        .link_collection = 1,
        .report_id = 0,
    };
}

} // namespace

int main()
{
    using namespace gc::input;

    int failures = 0;
    constexpr std::array action_bits{
        std::pair{LogicalAction::LeftBoosterUp, FastIoBits::P1_UP},
        std::pair{LogicalAction::LeftBoosterDown, FastIoBits::P2_UP},
        std::pair{LogicalAction::LeftBoosterLeft, FastIoBits::P1_DOWN},
        std::pair{LogicalAction::LeftBoosterRight, FastIoBits::P2_DOWN},
        std::pair{LogicalAction::LeftBoosterButton, FastIoBits::P1_BUTTON_1},
        std::pair{LogicalAction::RightBoosterUp, FastIoBits::P1_LEFT},
        std::pair{LogicalAction::RightBoosterDown, FastIoBits::P2_LEFT},
        std::pair{LogicalAction::RightBoosterLeft, FastIoBits::P1_RIGHT},
        std::pair{LogicalAction::RightBoosterRight, FastIoBits::P2_RIGHT},
        std::pair{LogicalAction::RightBoosterButton, FastIoBits::P2_BUTTON_1},
        std::pair{LogicalAction::Service1, FastIoBits::P1_SERVICE_F1},
        std::pair{LogicalAction::Service2, FastIoBits::P1_SERVICE_I},
        std::pair{LogicalAction::Service3, FastIoBits::P1_SERVICE_P},
        std::pair{LogicalAction::P1Start, FastIoBits::P1_START},
        std::pair{LogicalAction::P2Start, FastIoBits::P2_START},
        std::pair{LogicalAction::P2Service, FastIoBits::P2_SERVICE},
        std::pair{LogicalAction::Test, FastIoBits::TEST_MODE},
    };

    std::vector<KeyboardBinding> all_keys;
    for (std::size_t index = 0; index < action_bits.size(); ++index)
    {
        all_keys.push_back(KeyboardBinding{
            .action = action_bits[index].first,
            .key = PhysicalKey{
                static_cast<std::uint16_t>(index + 1),
                ScanCodePrefix::None},
        });
    }
    InputMapper keyboard_mapper(InputMode::Keyboard, all_keys, {});
    for (std::size_t index = 0; index < action_bits.size(); ++index)
    {
        keyboard_mapper.ClearAll();
        keyboard_mapper.ApplyKeyboardTransition(all_keys[index].key, true);
        failures += expect_word(
            keyboard_mapper.GetInput(),
            action_bits[index].second,
            "all logical actions map to FastIO");
    }
    InputMapper controller_mode_system_mapper(
        InputMode::Controller, all_keys, {});
    for (std::size_t index = 10; index < action_bits.size(); ++index)
    {
        controller_mode_system_mapper.ClearAll();
        controller_mode_system_mapper.ApplyKeyboardTransition(
            all_keys[index].key, true);
        failures += expect_word(
            controller_mode_system_mapper.GetInput(),
            action_bits[index].second,
            "all system keys work in controller mode");
    }

    const PhysicalKey t_key{0x14, ScanCodePrefix::None};
    const std::array t_binding{
        KeyboardBinding{LogicalAction::Test, t_key},
    };
    InputMapper test_mapper(InputMode::Keyboard, t_binding, {});
    test_mapper.ApplyKeyboardTransition(t_key, true);
    failures += expect_word(
        test_mapper.GetInput(), FastIoBits::TEST_MODE, "T enters Test bit");
    test_mapper.ApplyKeyboardTransition(t_key, true);
    failures += expect_word(
        test_mapper.GetInput(),
        FastIoBits::TEST_MODE,
        "repeat make is idempotent");
    test_mapper.ApplyKeyboardTransition(t_key, false);
    failures += expect_word(
        test_mapper.GetInput(), 0, "T break clears Test bit");

    const PhysicalKey shared_key{0x21, ScanCodePrefix::None};
    const std::array shared_bindings{
        KeyboardBinding{LogicalAction::LeftBoosterUp, shared_key},
        KeyboardBinding{LogicalAction::RightBoosterUp, shared_key},
    };
    InputMapper shared_mapper(InputMode::Keyboard, shared_bindings, {});
    shared_mapper.ApplyKeyboardTransition(shared_key, true);
    failures += expect_word(
        shared_mapper.GetInput(),
        FastIoBits::P1_UP | FastIoBits::P1_LEFT,
        "one key drives two logical actions");

    const PhysicalKey gameplay_key{0x11, ScanCodePrefix::None};
    const std::array mode_keys{
        KeyboardBinding{LogicalAction::LeftBoosterUp, gameplay_key},
        KeyboardBinding{LogicalAction::Test, t_key},
    };
    const std::array duplicate_controller{
        xinput_binding(
            LogicalAction::LeftBoosterUp,
            DigitalControlType::XInputButton,
            XInputControl::A),
        xinput_binding(
            LogicalAction::LeftBoosterUp,
            DigitalControlType::XInputButton,
            XInputControl::B),
    };
    InputMapper controller_mapper(
        InputMode::Controller, mode_keys, duplicate_controller);
    controller_mapper.ApplyKeyboardTransition(gameplay_key, true);
    controller_mapper.ApplyKeyboardTransition(t_key, true);
    failures += expect_word(
        controller_mapper.GetInput(),
        FastIoBits::TEST_MODE,
        "controller mode keeps system keyboard only");
    const std::array both_pressed{std::uint8_t{1}, std::uint8_t{1}};
    controller_mapper.ApplyControllerBindingStates(both_pressed);
    failures += expect_word(
        controller_mapper.GetInput(),
        FastIoBits::TEST_MODE | FastIoBits::P1_UP,
        "two bindings OR into one action");
    const std::array second_held{std::uint8_t{0}, std::uint8_t{1}};
    controller_mapper.ApplyControllerBindingStates(second_held);
    failures += expect_word(
        controller_mapper.GetInput(),
        FastIoBits::TEST_MODE | FastIoBits::P1_UP,
        "one binding release preserves the other");
    const std::array both_released{std::uint8_t{0}, std::uint8_t{0}};
    controller_mapper.ApplyControllerBindingStates(both_released);
    failures += expect_word(
        controller_mapper.GetInput(),
        FastIoBits::TEST_MODE,
        "action clears after all bindings release");

    controller_mapper.ClearKeyboard();
    failures += expect_word(
        controller_mapper.GetInput(), 0, "focus clear removes keyboard state");
    controller_mapper.ApplyControllerBindingStates(both_pressed);
    controller_mapper.ClearController();
    failures += expect_word(
        controller_mapper.GetInput(), 0, "disconnect clears controller state");
    controller_mapper.ApplyKeyboardTransition(t_key, true);
    controller_mapper.ApplyControllerBindingStates(both_pressed);
    controller_mapper.ClearAll();
    failures += expect_word(
        controller_mapper.GetInput(), 0, "shutdown clears every source");

    std::vector<DigitalControlBinding> evaluated_bindings{
        xinput_binding(
            LogicalAction::LeftBoosterUp,
            DigitalControlType::XInputAxis,
            XInputControl::LeftX,
            ControlDirection::Positive),
        xinput_binding(
            LogicalAction::LeftBoosterDown,
            DigitalControlType::XInputAxis,
            XInputControl::LeftX,
            ControlDirection::Negative),
        xinput_binding(
            LogicalAction::LeftBoosterButton,
            DigitalControlType::XInputTrigger,
            XInputControl::LeftTrigger),
        hat_binding(LogicalAction::RightBoosterUp, ControlDirection::Up),
        hat_binding(LogicalAction::RightBoosterLeft, ControlDirection::Right),
    };
    FakeControllerView view(evaluated_bindings);
    auto evaluator_result = ControllerBindingEvaluator::Create(
        evaluated_bindings, 50, 40);
    failures += expect_true(evaluator_result.has_value(), "evaluator created");
    failures += expect_true(
        !ControllerBindingEvaluator::Create(evaluated_bindings, 40, 40),
        "invalid hysteresis rejected");
    failures += expect_true(
        !ControllerBindingEvaluator::Create(
            std::span<const DigitalControlBinding>{}, 40, 40),
        "empty evaluator still validates hysteresis");
    if (!evaluator_result)
    {
        return 1;
    }
    auto evaluator = std::move(*evaluator_result);

    view.Set(0, 0.49);
    failures += expect_true(
        evaluator.Update(view)[0] == 0,
        "axis below press remains inactive");
    view.Set(0, 0.50);
    failures += expect_true(
        evaluator.Update(view)[0] == 1,
        "axis at press activates");
    view.Set(0, 0.40);
    failures += expect_true(
        evaluator.Update(view)[0] == 1,
        "axis at release remains active");
    view.Set(0, 0.39);
    failures += expect_true(
        evaluator.Update(view)[0] == 0,
        "axis below release clears");

    view.Set(1, 1.0);
    view.Set(2, 1.0);
    view.Set(3, 1.0);
    view.Set(4, 1.0);
    const auto active_states = evaluator.Update(view);
    failures += expect_true(
        active_states[1] && active_states[2] &&
            active_states[3] && active_states[4],
        "negative axis trigger and diagonal hats evaluate independently");

    evaluator.Clear();
    const auto cleared_states = evaluator.Update(
        FakeControllerView(evaluated_bindings));
    failures += expect_true(
        std::ranges::none_of(cleared_states, [](std::uint8_t state) {
            return state != 0;
        }),
        "evaluator clear resets every latch");

    if (failures != 0)
    {
        return 1;
    }

    std::cout << "InputMapperTests passed\n";
    return 0;
}

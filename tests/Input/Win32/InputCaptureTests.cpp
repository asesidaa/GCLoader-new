#include "Input/Win32/InputCapture.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {

struct FakeControlState {
    gc::input::ControllerControlDescriptor descriptor;
    double activation{};
    std::optional<std::int32_t> raw_value;
    std::int32_t logical_min{};
    std::int32_t logical_max{};
};

bool same_address(
    const gc::input::DigitalControlBinding& left,
    const gc::input::DigitalControlBinding& right)
{
    return left.type == right.type &&
        left.control == right.control &&
        left.usage_page == right.usage_page &&
        left.usage == right.usage &&
        left.link_collection == right.link_collection &&
        left.report_id == right.report_id &&
        (left.type == gc::input::DigitalControlType::RawHidValue ||
         left.direction == right.direction);
}

class FakeControllerView final : public gc::input::ControllerStateView {
public:
    FakeControllerView(
        gc::input::ControllerIdentity identity,
        std::vector<FakeControlState> controls)
        : identity_(std::move(identity)), states_(std::move(controls))
    {
        for (const auto& state : states_)
        {
            descriptors_.push_back(state.descriptor);
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
        for (const auto& state : states_)
        {
            if (!same_address(state.descriptor.binding, binding))
            {
                continue;
            }
            if (binding.type != gc::input::DigitalControlType::RawHidValue)
            {
                return state.activation;
            }
            if (!state.raw_value || !binding.neutral_value || !binding.direction)
            {
                return std::nullopt;
            }
            const auto value = static_cast<std::int64_t>(*state.raw_value);
            const auto neutral = static_cast<std::int64_t>(*binding.neutral_value);
            if (*binding.direction == gc::input::ControlDirection::Positive)
            {
                if (value <= neutral || state.logical_max <= neutral)
                {
                    return 0.0;
                }
                return static_cast<double>(value - neutral) /
                    static_cast<double>(state.logical_max - neutral);
            }
            if (*binding.direction == gc::input::ControlDirection::Negative)
            {
                if (value >= neutral || state.logical_min >= neutral)
                {
                    return 0.0;
                }
                return static_cast<double>(neutral - value) /
                    static_cast<double>(neutral - state.logical_min);
            }
            return std::nullopt;
        }
        return std::nullopt;
    }

    std::optional<std::int32_t> RawValue(
        const gc::input::DigitalControlBinding& binding) const noexcept override
    {
        for (const auto& state : states_)
        {
            if (same_address(state.descriptor.binding, binding))
            {
                return state.raw_value;
            }
        }
        return std::nullopt;
    }

    void SetActivation(std::size_t index, double activation)
    {
        states_[index].activation = activation;
    }

    void SetRawValue(std::size_t index, std::int32_t value)
    {
        states_[index].raw_value = value;
    }

private:
    gc::input::ControllerIdentity identity_;
    std::vector<FakeControlState> states_;
    std::vector<gc::input::ControllerControlDescriptor> descriptors_;
};

gc::input::DigitalControlBinding xinput_button(
    gc::input::XInputControl control)
{
    return {
        .type = gc::input::DigitalControlType::XInputButton,
        .control = control,
    };
}

gc::input::DigitalControlBinding raw_value()
{
    return {
        .type = gc::input::DigitalControlType::RawHidValue,
        .usage_page = 1,
        .usage = 0x30,
        .link_collection = 1,
        .report_id = 0,
    };
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

} // namespace

int main()
{
    using namespace gc::input;

    int failures = 0;
    auto created = InputCapture::Create(50, 40);
    failures += expect_true(created.has_value(), "capture created");
    failures += expect_true(
        !InputCapture::Create(40, 40),
        "capture rejects invalid thresholds");
    if (!created)
    {
        return 1;
    }
    auto capture = std::move(*created);

    const PhysicalKey t_key{0x14, ScanCodePrefix::None};
    capture.OnKeyboardTransition(t_key, true);
    capture.BeginKeyboard();
    capture.OnKeyboardTransition(t_key, true);
    failures += expect_true(
        !capture.TakeResult(),
        "held-key repeat does not complete capture");
    capture.OnKeyboardTransition(t_key, false);
    failures += expect_true(
        !capture.TakeResult(),
        "key break does not complete capture");
    capture.OnKeyboardTransition(t_key, true);
    const auto keyboard_result = capture.TakeResult();
    failures += expect_true(
        keyboard_result && !keyboard_result->controller_identity &&
            std::get_if<PhysicalKey>(&keyboard_result->value) != nullptr &&
            *std::get_if<PhysicalKey>(&keyboard_result->value) == t_key,
        "fresh key make captures physical identity");
    failures += expect_true(
        !capture.TakeResult(),
        "keyboard result is once-only");
    capture.OnKeyboardTransition(t_key, false);

    const ControllerIdentity xinput_identity{ControllerBackend::XInput, "1"};
    FakeControllerView xinput_view(
        xinput_identity,
        {
            {
                .descriptor = {
                    .binding = xinput_button(XInputControl::A),
                    .label = "Shared XInput A Label",
                },
                .activation = 1.0,
            },
            {
                .descriptor = {
                    .binding = xinput_button(XInputControl::B),
                    .label = "Shared XInput B Label",
                },
                .activation = 0.0,
            },
        });
    const auto began = capture.BeginController(
        LogicalAction::LeftBoosterButton,
        xinput_identity,
        xinput_view);
    failures += expect_true(began.has_value(), "controller capture begins");
    failures += expect_true(
        capture.SampleController(xinput_view).has_value() &&
            !capture.TakeResult(),
        "initially active control is ignored");
    xinput_view.SetActivation(0, 0.40);
    (void)capture.SampleController(xinput_view);
    xinput_view.SetActivation(0, 0.39);
    (void)capture.SampleController(xinput_view);
    xinput_view.SetActivation(0, 1.0);
    xinput_view.SetActivation(1, 1.0);
    (void)capture.SampleController(xinput_view);
    failures += expect_true(
        capture.ResultLabel() == "Shared XInput A Label",
        "capture label comes from shared descriptor");
    const auto controller_result = capture.TakeResult();
    const auto* captured_button = controller_result
        ? std::get_if<DigitalControlBinding>(&controller_result->value)
        : nullptr;
    failures += expect_true(
        controller_result &&
            controller_result->controller_identity == xinput_identity &&
            captured_button && captured_button->control == XInputControl::A &&
            captured_button->action == LogicalAction::LeftBoosterButton,
        "first re-armed crossing wins with exact identity");

    FakeControllerView wrong_view(
        ControllerIdentity{ControllerBackend::XInput, "2"},
        {});
    (void)capture.BeginController(
        LogicalAction::LeftBoosterButton,
        xinput_identity,
        xinput_view);
    failures += expect_true(
        !capture.SampleController(wrong_view),
        "capture rejects a different controller identity");
    capture.Cancel();
    failures += expect_true(
        !capture.TakeResult(),
        "cancel produces no binding");

    const ControllerIdentity hid_identity{
        ControllerBackend::RawHid,
        R"(\\?\HID#VID_1234&PID_5678#CAPTURE)"};
    FakeControllerView hid_view(
        hid_identity,
        {
            {
                .descriptor = {
                    .binding = raw_value(),
                    .label = "Shared HID Axis Label",
                },
                .raw_value = 500,
                .logical_min = 100,
                .logical_max = 900,
            },
        });
    failures += expect_true(
        capture.BeginController(
            LogicalAction::RightBoosterRight,
            hid_identity,
            hid_view).has_value(),
        "Raw HID capture begins");
    hid_view.SetRawValue(0, 900);
    (void)capture.SampleController(hid_view);
    failures += expect_true(
        capture.ResultLabel().starts_with("Shared HID Axis Label"),
        "generic HID label comes from shared descriptor");
    const auto hid_result = capture.TakeResult();
    const auto* captured_value = hid_result
        ? std::get_if<DigitalControlBinding>(&hid_result->value)
        : nullptr;
    failures += expect_true(
        hid_result && hid_result->controller_identity == hid_identity &&
            captured_value &&
            captured_value->action == LogicalAction::RightBoosterRight &&
            captured_value->direction == ControlDirection::Positive &&
            captured_value->neutral_value == 500,
        "Raw HID capture persists observed neutral");

    capture.BeginKeyboard();
    capture.Cancel();
    capture.OnKeyboardTransition(
        PhysicalKey{0x22, ScanCodePrefix::None}, true);
    failures += expect_true(
        !capture.TakeResult(),
        "cancelled keyboard capture stays cancelled");

    if (failures != 0)
    {
        return 1;
    }

    std::cout << "InputCaptureTests passed\n";
    return 0;
}

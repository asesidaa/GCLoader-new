#include "InputEditorModel.h"

#include "Config/NativeInputConfig.h"
#include "Input/Types/InputTypes.h"
#include "Input/Win32/InputCapture.h"

#include <rfl.hpp>
#include <rfl/toml.hpp>

#include <cstddef>
#include <iostream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

struct ModelRoundTrip {
    rfl::Rename<"controller", gc::config::ControllerConfig> controller;
};

int expect(bool condition, const char* name)
{
    if (condition)
    {
        return 0;
    }
    std::cerr << name << ": expectation failed\n";
    return 1;
}

gc::input::DigitalControlBinding RawButton(
    std::uint32_t usage,
    gc::input::LogicalAction action =
        gc::input::LogicalAction::LeftBoosterButton)
{
    return {
        .action = action,
        .type = gc::input::DigitalControlType::RawHidButton,
        .usage_page = 9,
        .usage = usage,
        .link_collection = 0,
        .report_id = 1,
    };
}

} // namespace

int main()
{
    using namespace gc::input;

    int failures = 0;
    gc::config::ControllerConfig config;
    InputEditorModel model(std::move(config));

    const ControllerIdentity xinput_three{
        .backend = ControllerBackend::XInput,
        .device_id = "3",
    };
    const std::string raw_path =
        R"(\\?\HID#VID_1234&PID_5678#8&exact-device-path)";
    const ControllerIdentity raw_exact{
        .backend = ControllerBackend::RawHid,
        .device_id = raw_path,
    };

    model.SetAvailableIdentities({xinput_three, raw_exact});
    model.SelectIdentity(xinput_three);
    failures += expect(
        model.SelectedIdentity() == xinput_three,
        "exact XInput identity retained");
    failures += expect(
        model.selected_identity_available(),
        "selected XInput identity available");

    model.SelectIdentity(raw_exact);
    failures += expect(
        model.SelectedIdentity() == raw_exact,
        "exact Raw HID identity retained");
    failures += expect(
        model.config().device_id() == raw_path,
        "Raw HID path retained without normalization");

    model.SetAvailableIdentities({xinput_three});
    failures += expect(
        !model.selected_identity_available(),
        "missing selected identity marked unavailable");
    failures += expect(
        model.SelectedIdentity() == raw_exact,
        "missing selected identity does not fall back");

    failures += expect(
        model.AddBinding(RawButton(1)).has_value(),
        "add first binding");
    failures += expect(
        model.AddBinding(RawButton(2)).has_value(),
        "add second binding for same action");
    failures += expect(
        model.config().bindings().size() == 2 &&
            model.config().bindings()[0].action ==
                LogicalAction::LeftBoosterButton &&
            model.config().bindings()[1].action ==
                LogicalAction::LeftBoosterButton,
        "multiple bindings retained for one action");

    failures += expect(
        model.ReplaceBinding(1, RawButton(3)).has_value(),
        "replace requested binding");
    failures += expect(
        model.config().bindings()[0].usage == 1 &&
            model.config().bindings()[1].usage == 3,
        "replace leaves other binding unchanged");

    failures += expect(
        model.RemoveBinding(0).has_value(),
        "remove requested binding");
    failures += expect(
        model.config().bindings().size() == 1 &&
            model.config().bindings()[0].usage == 3,
        "remove leaves requested survivor");

    const auto before_rejected_capture = model.config().bindings();
    CaptureResult wrong_identity_capture{
        .controller_identity = xinput_three,
        .value = RawButton(4),
    };
    failures += expect(
        !model.AcceptCapture(
            wrong_identity_capture, std::nullopt).has_value(),
        "capture from a different identity rejected");
    failures += expect(
        model.config().bindings() == before_rejected_capture,
        "rejected capture does not mutate bindings");

    CaptureResult exact_identity_capture{
        .controller_identity = raw_exact,
        .value = RawButton(5),
    };
    failures += expect(
        model.AcceptCapture(
            exact_identity_capture, std::size_t{0}).has_value(),
        "capture from selected identity accepted");
    failures += expect(
        model.config().bindings().size() == 1 &&
            model.config().bindings()[0].usage == 5,
        "accepted capture replaces requested index");

    const ModelRoundTrip serialized_source{.controller = model.config()};
    const auto serialized = rfl::toml::write(serialized_source);
    const auto reparsed = rfl::toml::read<ModelRoundTrip>(serialized);
    failures += expect(reparsed.has_value(), "model config reparses");
    if (reparsed)
    {
        failures += expect(
            reparsed->controller().backend() ==
                serialized_source.controller().backend() &&
                reparsed->controller().device_id() ==
                    serialized_source.controller().device_id() &&
                reparsed->controller().bindings() ==
                    serialized_source.controller().bindings(),
            "identity and descriptors serialize without change");
    }

    return failures == 0 ? 0 : 1;
}

#include "InputEditorModel.h"
#include "Win32D3D11Host.h"

#include "Config/RegistryConfig.h"
#include "Config/TargetFps.h"
#include "Config/config.h"
#include "Input/Types/PhysicalKey.h"
#include "Input/Win32/ControllerCatalog.h"
#include "Input/Win32/InputCapture.h"
#include "Input/Win32/PhysicalKeyWin32.h"
#include "Input/Win32/RawHidController.h"
#include "Input/Win32/RawInputPacket.h"
#include "Input/Win32/XInputApi.h"
#include "Input/Win32/XInputController.h"
#include "Nesys/Network/NesysNetworkConfig.h"

#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"
#include <rfl/toml.hpp>

#include <Windows.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {

constexpr std::array<USHORT, 4> kRawInputUsages{0x06, 0x05, 0x04, 0x08};

std::string Win32Failure(const char* operation)
{
    return std::string(operation) + " failed with Win32 error " +
        std::to_string(GetLastError());
}

std::string WideToUtf8(std::wstring_view value)
{
    if (value.empty())
    {
        return {};
    }
    const int count = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        nullptr,
        0,
        nullptr,
        nullptr);
    if (count <= 0)
    {
        return {};
    }
    std::string result(static_cast<std::size_t>(count), '\0');
    if (WideCharToMultiByte(
            CP_UTF8,
            WC_ERR_INVALID_CHARS,
            value.data(),
            static_cast<int>(value.size()),
            result.data(),
            count,
            nullptr,
            nullptr) != count)
    {
        return {};
    }
    return result;
}

std::expected<void, std::string> RegisterGuiRawInput(HWND window)
{
    std::array<RAWINPUTDEVICE, kRawInputUsages.size()> registrations{};
    for (std::size_t index = 0; index < registrations.size(); ++index)
    {
        registrations[index] = RAWINPUTDEVICE{
            .usUsagePage = 0x01,
            .usUsage = kRawInputUsages[index],
            .dwFlags = RIDEV_INPUTSINK | RIDEV_DEVNOTIFY,
            .hwndTarget = window,
        };
    }
    if (!RegisterRawInputDevices(
            registrations.data(),
            static_cast<UINT>(registrations.size()),
            sizeof(RAWINPUTDEVICE)))
    {
        return std::unexpected(Win32Failure("RegisterRawInputDevices"));
    }
    return {};
}

void UnregisterGuiRawInput() noexcept
{
    std::array<RAWINPUTDEVICE, kRawInputUsages.size()> registrations{};
    for (std::size_t index = 0; index < registrations.size(); ++index)
    {
        registrations[index] = RAWINPUTDEVICE{
            .usUsagePage = 0x01,
            .usUsage = kRawInputUsages[index],
            .dwFlags = RIDEV_REMOVE,
            .hwndTarget = nullptr,
        };
    }
    RegisterRawInputDevices(
        registrations.data(),
        static_cast<UINT>(registrations.size()),
        sizeof(RAWINPUTDEVICE));
}

const char* ActionName(gc::input::LogicalAction action) noexcept
{
    using enum gc::input::LogicalAction;
    switch (action)
    {
    case LeftBoosterUp: return "Left Booster Up";
    case LeftBoosterDown: return "Left Booster Down";
    case LeftBoosterLeft: return "Left Booster Left";
    case LeftBoosterRight: return "Left Booster Right";
    case LeftBoosterButton: return "Left Booster Button";
    case RightBoosterUp: return "Right Booster Up";
    case RightBoosterDown: return "Right Booster Down";
    case RightBoosterLeft: return "Right Booster Left";
    case RightBoosterRight: return "Right Booster Right";
    case RightBoosterButton: return "Right Booster Button";
    case Service1: return "Service 1";
    case Service2: return "Service 2";
    case Service3: return "Service 3";
    case P1Start: return "P1 Start";
    case P2Start: return "P2 Start";
    case P2Service: return "P2 Service";
    case Test: return "Test";
    case Count: break;
    }
    return "Unknown";
}

const char* XInputControlName(gc::input::XInputControl control) noexcept
{
    using enum gc::input::XInputControl;
    switch (control)
    {
    case A: return "A";
    case B: return "B";
    case X: return "X";
    case Y: return "Y";
    case DPadUp: return "D-pad Up";
    case DPadDown: return "D-pad Down";
    case DPadLeft: return "D-pad Left";
    case DPadRight: return "D-pad Right";
    case Start: return "Start";
    case Back: return "Back";
    case LeftShoulder: return "Left Shoulder";
    case RightShoulder: return "Right Shoulder";
    case LeftThumb: return "Left Thumb";
    case RightThumb: return "Right Thumb";
    case LeftX: return "Left X";
    case LeftY: return "Left Y";
    case RightX: return "Right X";
    case RightY: return "Right Y";
    case LeftTrigger: return "Left Trigger";
    case RightTrigger: return "Right Trigger";
    }
    return "Unknown";
}

const char* DirectionName(gc::input::ControlDirection direction) noexcept
{
    using enum gc::input::ControlDirection;
    switch (direction)
    {
    case Positive: return "+";
    case Negative: return "-";
    case Up: return "Up";
    case Down: return "Down";
    case Left: return "Left";
    case Right: return "Right";
    }
    return "?";
}

std::string BindingLabel(const gc::input::DigitalControlBinding& binding)
{
    using enum gc::input::DigitalControlType;
    if (binding.type == XInputButton || binding.type == XInputAxis ||
        binding.type == XInputTrigger)
    {
        std::string label = "XInput ";
        label += binding.control
            ? XInputControlName(*binding.control)
            : "Unknown";
        if (binding.direction)
        {
            label += " ";
            label += DirectionName(*binding.direction);
        }
        return label;
    }

    std::ostringstream label;
    if (binding.type == RawHidButton)
    {
        label << "Button ";
    }
    else if (binding.type == RawHidHat)
    {
        label << "Hat ";
    }
    else
    {
        label << "Value ";
    }
    label << "page 0x" << std::hex << binding.usage_page.value_or(0)
          << " usage 0x" << binding.usage.value_or(0) << std::dec;
    if (binding.direction)
    {
        label << " " << DirectionName(*binding.direction);
    }
    label << " (report " << binding.report_id.value_or(0)
          << ", link " << binding.link_collection.value_or(0) << ")";
    return label.str();
}

void DrawInlineValidationError(bool valid, const char* message)
{
    if (!valid)
    {
        ImGui::TextColored(
            ImVec4(1.0F, 0.35F, 0.35F, 1.0F), "%s", message);
    }
}

class GuiInputContext {
public:
    GuiInputContext(InputConfig& config, InputEditorModel& editor)
        : config_(config), editor_(editor)
    {
    }

    static LRESULT MessageHandler(
        void* context,
        HWND window,
        UINT message,
        WPARAM wparam,
        LPARAM lparam) noexcept
    {
        auto* self = static_cast<GuiInputContext*>(context);
        if (self == nullptr)
        {
            return 0;
        }
        try
        {
            if (message == WM_INPUT)
            {
                self->OnRawInput(
                    window, reinterpret_cast<HRAWINPUT>(lparam));
            }
            else if (message == WM_INPUT_DEVICE_CHANGE)
            {
                self->device_refresh_requested_ = true;
            }
        }
        catch (...)
        {
            self->CancelCapture();
            OutputDebugStringA("ConfigGUI native input handler failed\n");
        }
        return 0;
    }

    void SetWindow(HWND window) noexcept
    {
        window_ = window;
    }

    void InitializeControllers()
    {
        for (std::uint32_t slot = 0; slot < xinput_.size(); ++slot)
        {
            auto api = gc::input::LoadSystemXInput();
            if (!api)
            {
                controller_error_ = api.error();
                break;
            }
            auto controller = gc::input::XInputController::Create(
                slot, std::move(*api));
            if (!controller)
            {
                controller_error_ = controller.error();
                continue;
            }
            xinput_[slot].emplace(std::move(*controller));
        }
        RefreshDevices();
    }

    void RefreshDevices()
    {
        device_refresh_requested_ = false;
        for (auto& controller : xinput_)
        {
            if (!controller)
            {
                continue;
            }
            controller->RequestReconnectProbe();
            if (const auto poll = controller->Poll(); !poll)
            {
                controller_error_ = poll.error();
            }
        }

        if (auto devices = gc::input::EnumerateRawHidDevices(); devices)
        {
            raw_devices_ = std::move(*devices);
        }
        else
        {
            raw_devices_.clear();
            controller_error_ = devices.error();
        }
        ReopenSelectedRawController();
        UpdateAvailableIdentities();
    }

    void PollSelectedController()
    {
        if (device_refresh_requested_)
        {
            RefreshDevices();
        }

        const auto selected = editor_.SelectedIdentity();
        if (selected.backend != gc::input::ControllerBackend::XInput)
        {
            return;
        }
        const auto slot = SelectedXInputSlot();
        if (!slot || !xinput_[*slot])
        {
            return;
        }

        const bool was_connected = xinput_[*slot]->connected();
        const auto poll = xinput_[*slot]->Poll();
        if (!poll)
        {
            controller_error_ = poll.error();
            return;
        }
        if (was_connected != xinput_[*slot]->connected())
        {
            UpdateAvailableIdentities();
        }
        if (capture_mode_ == CaptureMode::Controller && capture_ &&
            xinput_[*slot]->connected())
        {
            if (const auto sample =
                    capture_->SampleController(*xinput_[*slot]);
                !sample)
            {
                capture_error_ = sample.error();
                CancelCapture();
                return;
            }
            HarvestCaptureResult();
        }
    }

    void SelectIdentity(gc::input::ControllerIdentity identity)
    {
        editor_.SelectIdentity(std::move(identity));
        config_.controller = editor_.config();
        ReopenSelectedRawController();
        UpdateAvailableIdentities();
    }

    [[nodiscard]] bool BeginKeyboardCapture(
        gc::input::PhysicalKey& target,
        std::string label)
    {
        auto capture = gc::input::InputCapture::Create(
            config_.axis_press_threshold_percent(),
            config_.axis_release_threshold_percent());
        if (!capture)
        {
            capture_error_ = capture.error();
            return false;
        }
        capture_.emplace(std::move(*capture));
        capture_->BeginKeyboard();
        capture_mode_ = CaptureMode::Keyboard;
        keyboard_target_ = &target;
        replacement_index_.reset();
        capture_label_ = std::move(label);
        capture_error_.clear();
        popup_requested_ = true;
        return true;
    }

    [[nodiscard]] bool BeginControllerCapture(
        gc::input::LogicalAction action,
        std::optional<std::size_t> replacement_index)
    {
        auto* view = SelectedControllerView();
        if (view == nullptr || !editor_.selected_identity_available())
        {
            capture_error_ = "The selected controller is unavailable";
            return false;
        }

        auto capture = gc::input::InputCapture::Create(
            config_.axis_press_threshold_percent(),
            config_.axis_release_threshold_percent());
        if (!capture)
        {
            capture_error_ = capture.error();
            return false;
        }
        if (const auto begin = capture->BeginController(
                action, editor_.SelectedIdentity(), *view);
            !begin)
        {
            capture_error_ = begin.error();
            return false;
        }

        capture_.emplace(std::move(*capture));
        capture_mode_ = CaptureMode::Controller;
        keyboard_target_ = nullptr;
        replacement_index_ = replacement_index;
        capture_label_ = ActionName(action);
        capture_error_.clear();
        popup_requested_ = true;
        return true;
    }

    void CancelCapture() noexcept
    {
        if (capture_)
        {
            capture_->Cancel();
        }
        capture_.reset();
        completed_capture_.reset();
        capture_mode_ = CaptureMode::None;
        keyboard_target_ = nullptr;
        replacement_index_.reset();
        close_popup_requested_ = true;
    }

    [[nodiscard]] bool ApplyCompletedCapture()
    {
        if (!completed_capture_)
        {
            return false;
        }

        bool changed = false;
        if (capture_mode_ == CaptureMode::Keyboard)
        {
            const auto* key = std::get_if<gc::input::PhysicalKey>(
                &completed_capture_->value);
            if (key != nullptr && keyboard_target_ != nullptr)
            {
                *keyboard_target_ = *key;
                changed = true;
            }
            else
            {
                capture_error_ = "Keyboard capture returned an invalid value";
            }
        }
        else if (capture_mode_ == CaptureMode::Controller)
        {
            const auto accepted = editor_.AcceptCapture(
                *completed_capture_, replacement_index_);
            if (accepted)
            {
                config_.controller = editor_.config();
                changed = true;
            }
            else
            {
                capture_error_ = accepted.error();
            }
        }

        completed_capture_.reset();
        capture_.reset();
        capture_mode_ = CaptureMode::None;
        keyboard_target_ = nullptr;
        replacement_index_.reset();
        close_popup_requested_ = true;
        return changed;
    }

    [[nodiscard]] bool TakePopupRequested() noexcept
    {
        return std::exchange(popup_requested_, false);
    }

    [[nodiscard]] bool TakeClosePopupRequested() noexcept
    {
        return std::exchange(close_popup_requested_, false);
    }

    [[nodiscard]] bool capture_active() const noexcept
    {
        return capture_mode_ != CaptureMode::None;
    }

    [[nodiscard]] const std::string& capture_label() const noexcept
    {
        return capture_label_;
    }

    [[nodiscard]] const std::string& capture_error() const noexcept
    {
        return capture_error_;
    }

    void ClearCaptureError()
    {
        capture_error_.clear();
    }

    [[nodiscard]] bool xinput_connected(std::size_t slot) const noexcept
    {
        return slot < xinput_.size() && xinput_[slot] &&
            xinput_[slot]->connected();
    }

    [[nodiscard]] const std::vector<gc::input::RawHidDeviceInfo>&
    raw_devices() const noexcept
    {
        return raw_devices_;
    }

    [[nodiscard]] const std::string& controller_error() const noexcept
    {
        return controller_error_;
    }

private:
    enum class CaptureMode : std::uint8_t {
        None,
        Keyboard,
        Controller,
    };

    void OnRawInput(HWND window, HRAWINPUT handle)
    {
        if (window_ == nullptr || window != window_ ||
            GetForegroundWindow() != window_)
        {
            return;
        }
        const auto packet = packets_.Read(handle);
        if (!packet)
        {
            capture_error_ = packet.error();
            return;
        }

        const RAWINPUT& input = **packet;
        if (input.header.dwType == RIM_TYPEKEYBOARD && capture_ &&
            capture_mode_ == CaptureMode::Keyboard)
        {
            const auto transition =
                gc::input::DecodeRawKeyboard(input.data.keyboard);
            if (!transition)
            {
                return;
            }
            if (transition->pressed &&
                transition->key == gc::input::PhysicalKey{
                    0x01, gc::input::ScanCodePrefix::None})
            {
                CancelCapture();
                return;
            }
            capture_->OnKeyboardTransition(
                transition->key, transition->pressed);
            HarvestCaptureResult();
            return;
        }

        if (input.header.dwType == RIM_TYPEHID && raw_controller_)
        {
            const auto applied = raw_controller_->Apply(
                input.header.hDevice, input.data.hid);
            if (!applied)
            {
                capture_error_ = applied.error();
                return;
            }
            if (*applied && capture_ &&
                capture_mode_ == CaptureMode::Controller)
            {
                if (const auto sample =
                        capture_->SampleController(*raw_controller_);
                    !sample)
                {
                    capture_error_ = sample.error();
                    CancelCapture();
                    return;
                }
                HarvestCaptureResult();
            }
        }
    }

    void HarvestCaptureResult()
    {
        if (!capture_)
        {
            return;
        }
        if (auto result = capture_->TakeResult(); result)
        {
            completed_capture_ = std::move(*result);
        }
    }

    std::optional<std::size_t> SelectedXInputSlot() const noexcept
    {
        const auto identity = editor_.SelectedIdentity();
        if (identity.backend != gc::input::ControllerBackend::XInput ||
            identity.device_id.size() != 1 ||
            identity.device_id[0] < '0' || identity.device_id[0] > '3')
        {
            return std::nullopt;
        }
        return static_cast<std::size_t>(identity.device_id[0] - '0');
    }

    gc::input::ControllerStateView* SelectedControllerView() noexcept
    {
        const auto identity = editor_.SelectedIdentity();
        if (identity.backend == gc::input::ControllerBackend::XInput)
        {
            const auto slot = SelectedXInputSlot();
            if (!slot || !xinput_[*slot] || !xinput_[*slot]->connected())
            {
                return nullptr;
            }
            return &*xinput_[*slot];
        }
        return raw_controller_ ? &*raw_controller_ : nullptr;
    }

    void ReopenSelectedRawController()
    {
        raw_controller_.reset();
        const auto selected = editor_.SelectedIdentity();
        if (selected.backend != gc::input::ControllerBackend::RawHid)
        {
            return;
        }
        const auto* device = gc::input::FindExactRawHidDevice(
            raw_devices_, selected.device_id);
        if (device == nullptr)
        {
            return;
        }
        auto opened = gc::input::RawHidController::Open(*device);
        if (!opened)
        {
            controller_error_ = opened.error();
            return;
        }
        raw_controller_.emplace(std::move(*opened));
    }

    void UpdateAvailableIdentities()
    {
        std::vector<gc::input::ControllerIdentity> available;
        for (std::size_t slot = 0; slot < xinput_.size(); ++slot)
        {
            if (xinput_[slot] && xinput_[slot]->connected())
            {
                available.push_back(xinput_[slot]->identity());
            }
        }
        for (const auto& device : raw_devices_)
        {
            available.push_back(gc::input::ControllerIdentity{
                .backend = gc::input::ControllerBackend::RawHid,
                .device_id = device.device_path,
            });
        }

        const auto selected = editor_.SelectedIdentity();
        if (selected.backend == gc::input::ControllerBackend::RawHid &&
            gc::input::FindExactRawHidDevice(
                raw_devices_, selected.device_id) != nullptr)
        {
            available.push_back(selected);
        }
        editor_.SetAvailableIdentities(std::move(available));
    }

    InputConfig& config_;
    InputEditorModel& editor_;
    HWND window_{};
    gc::input::RawInputPacketBuffer packets_;
    std::array<std::optional<gc::input::XInputController>, 4> xinput_;
    std::vector<gc::input::RawHidDeviceInfo> raw_devices_;
    std::optional<gc::input::RawHidController> raw_controller_;
    std::optional<gc::input::InputCapture> capture_;
    std::optional<gc::input::CaptureResult> completed_capture_;
    CaptureMode capture_mode_{CaptureMode::None};
    gc::input::PhysicalKey* keyboard_target_{};
    std::optional<std::size_t> replacement_index_;
    std::string capture_label_;
    std::string capture_error_;
    std::string controller_error_;
    bool device_refresh_requested_{};
    bool popup_requested_{};
    bool close_popup_requested_{};
};

void DrawKeyboardBinding(
    const char* label,
    gc::input::PhysicalKey& key,
    GuiInputContext& input)
{
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextUnformatted(label);
    ImGui::TableSetColumnIndex(1);
    const auto logical_label = WideToUtf8(gc::input::PhysicalKeyLabel(key));
    const auto token = gc::input::FormatPhysicalKey(key);
    ImGui::Text("%s [%s]", logical_label.c_str(), token.c_str());
    ImGui::TableSetColumnIndex(2);
    ImGui::PushID(&key);
    if (ImGui::Button("Bind"))
    {
        (void) input.BeginKeyboardCapture(key, label);
    }
    ImGui::PopID();
}

void DrawKeyboardBindings(
    InputConfig& config,
    GuiInputContext& input,
    bool gameplay)
{
    if (!ImGui::BeginTable(
            gameplay ? "GameplayKeyboardBindings" : "SystemKeyboardBindings",
            3,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                ImGuiTableFlags_Resizable))
    {
        return;
    }
    ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, 180.0F);
    ImGui::TableSetupColumn("Physical key", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Rebind", ImGuiTableColumnFlags_WidthFixed, 75.0F);
    ImGui::TableHeadersRow();

    auto& keyboard = config.keyboard();
    if (gameplay)
    {
        DrawKeyboardBinding("Left Booster Up", keyboard.left_booster_up(), input);
        DrawKeyboardBinding("Left Booster Down", keyboard.left_booster_down(), input);
        DrawKeyboardBinding("Left Booster Left", keyboard.left_booster_left(), input);
        DrawKeyboardBinding("Left Booster Right", keyboard.left_booster_right(), input);
        DrawKeyboardBinding("Left Booster Button", keyboard.left_booster_button(), input);
        DrawKeyboardBinding("Right Booster Up", keyboard.right_booster_up(), input);
        DrawKeyboardBinding("Right Booster Down", keyboard.right_booster_down(), input);
        DrawKeyboardBinding("Right Booster Left", keyboard.right_booster_left(), input);
        DrawKeyboardBinding("Right Booster Right", keyboard.right_booster_right(), input);
        DrawKeyboardBinding("Right Booster Button", keyboard.right_booster_button(), input);
    }
    else
    {
        DrawKeyboardBinding("Test", keyboard.test(), input);
        DrawKeyboardBinding("Service 1", keyboard.service1(), input);
        DrawKeyboardBinding("Service 2", keyboard.service2(), input);
        DrawKeyboardBinding("Service 3", keyboard.service3(), input);
        DrawKeyboardBinding("P1 Start", keyboard.p1_start(), input);
        DrawKeyboardBinding("P2 Start", keyboard.p2_start(), input);
        DrawKeyboardBinding("P2 Service", keyboard.p2_service(), input);
        DrawKeyboardBinding("Card Read", keyboard.card_read(), input);
    }
    ImGui::EndTable();
}

void DrawControllerDevices(
    InputConfig& config,
    InputEditorModel& editor,
    GuiInputContext& input,
    bool& dirty)
{
    ImGui::SeparatorText("Controller device");
    if (ImGui::Button("Refresh devices"))
    {
        input.RefreshDevices();
    }

    const auto selected = editor.SelectedIdentity();
    ImGui::Text("Configured identity: %s / %s",
        selected.backend == gc::input::ControllerBackend::XInput
            ? "XInput"
            : "Raw HID",
        selected.device_id.c_str());
    if (!editor.selected_identity_available())
    {
        ImGui::TextColored(
            ImVec4(1.0F, 0.35F, 0.35F, 1.0F),
            "Configured device is unavailable; no fallback will be selected.");
    }

    if (ImGui::TreeNodeEx("XInput", ImGuiTreeNodeFlags_DefaultOpen))
    {
        for (std::size_t slot = 0; slot < 4; ++slot)
        {
            ImGui::PushID(static_cast<int>(slot));
            const bool connected = input.xinput_connected(slot);
            ImGui::Text("Slot %zu: %s", slot, connected ? "connected" : "not connected");
            ImGui::SameLine();
            if (ImGui::Button("Select"))
            {
                input.SelectIdentity(gc::input::ControllerIdentity{
                    .backend = gc::input::ControllerBackend::XInput,
                    .device_id = std::to_string(slot),
                });
                dirty = true;
            }
            ImGui::PopID();
        }
        ImGui::TreePop();
    }

    if (ImGui::TreeNodeEx("Generic Raw HID", ImGuiTreeNodeFlags_DefaultOpen))
    {
        if (input.raw_devices().empty())
        {
            ImGui::TextDisabled("No generic Raw HID controllers found.");
        }
        for (const auto& device : input.raw_devices())
        {
            ImGui::PushID(device.device_path.c_str());
            const auto name = WideToUtf8(device.product_name);
            ImGui::Text("%s", name.empty() ? "Generic HID controller" : name.c_str());
            ImGui::Text(
                "VID %04X PID %04X, usage %04X:%04X",
                device.vendor_id,
                device.product_id,
                device.usage_page,
                device.usage);
            ImGui::TextWrapped("%s", device.device_path.c_str());
            if (ImGui::Button("Select this Raw HID device"))
            {
                input.SelectIdentity(gc::input::ControllerIdentity{
                    .backend = gc::input::ControllerBackend::RawHid,
                    .device_id = device.device_path,
                });
                dirty = true;
            }
            ImGui::Separator();
            ImGui::PopID();
        }
        ImGui::TreePop();
    }

    if (!input.controller_error().empty())
    {
        ImGui::TextColored(
            ImVec4(1.0F, 0.55F, 0.2F, 1.0F),
            "%s",
            input.controller_error().c_str());
    }
    config.controller = editor.config();
}

void DrawControllerBindings(
    InputConfig& config,
    InputEditorModel& editor,
    GuiInputContext& input,
    bool& dirty)
{
    constexpr std::array actions{
        gc::input::LogicalAction::LeftBoosterUp,
        gc::input::LogicalAction::LeftBoosterDown,
        gc::input::LogicalAction::LeftBoosterLeft,
        gc::input::LogicalAction::LeftBoosterRight,
        gc::input::LogicalAction::LeftBoosterButton,
        gc::input::LogicalAction::RightBoosterUp,
        gc::input::LogicalAction::RightBoosterDown,
        gc::input::LogicalAction::RightBoosterLeft,
        gc::input::LogicalAction::RightBoosterRight,
        gc::input::LogicalAction::RightBoosterButton,
    };

    std::optional<std::size_t> remove_index;
    if (ImGui::BeginTable(
            "ControllerBindings",
            3,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                ImGuiTableFlags_Resizable))
    {
        ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, 180.0F);
        ImGui::TableSetupColumn("Bindings", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Edit", ImGuiTableColumnFlags_WidthFixed, 150.0F);
        ImGui::TableHeadersRow();

        for (const auto action : actions)
        {
            bool found = false;
            const auto& bindings = editor.config().bindings();
            for (std::size_t index = 0; index < bindings.size(); ++index)
            {
                if (bindings[index].action != action)
                {
                    continue;
                }
                found = true;
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(ActionName(action));
                ImGui::TableSetColumnIndex(1);
                const auto label = BindingLabel(bindings[index]);
                ImGui::TextWrapped("%s", label.c_str());
                ImGui::TableSetColumnIndex(2);
                ImGui::PushID(static_cast<int>(index));
                if (ImGui::Button("Replace"))
                {
                    (void) input.BeginControllerCapture(action, index);
                }
                ImGui::SameLine();
                if (ImGui::Button("Remove"))
                {
                    remove_index = index;
                }
                ImGui::PopID();
            }

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            if (!found)
            {
                ImGui::TextUnformatted(ActionName(action));
            }
            ImGui::TableSetColumnIndex(1);
            if (!found)
            {
                ImGui::TextDisabled("No binding");
            }
            ImGui::TableSetColumnIndex(2);
            ImGui::PushID(static_cast<int>(action));
            if (ImGui::Button("Add"))
            {
                (void) input.BeginControllerCapture(action, std::nullopt);
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    if (remove_index)
    {
        if (const auto removed = editor.RemoveBinding(*remove_index); removed)
        {
            config.controller = editor.config();
            dirty = true;
        }
    }
}

void DrawRegistry(InputConfig& config, bool& dirty)
{
    ImGui::SeparatorText("Registry");
    auto& registry = config.registry();
    bool enabled = registry.enabled();
    if (ImGui::Checkbox("Registry configuration overrides", &enabled))
    {
        registry.enabled = enabled;
        dirty = true;
    }

    constexpr const char* countries[]{
        "GrooveCoasterJpn - GROOVE COASTER, Japanese branding",
        "Rhythmvaders - RHYTHMVADERS, English branding",
        "GrooveCoasterEng - GROOVE COASTER, English branding",
    };
    int country = static_cast<int>(registry.game().country());
    if (ImGui::Combo("Game country", &country, countries, IM_ARRAYSIZE(countries)))
    {
        registry.game().country = static_cast<GameCountry>(country);
        dirty = true;
    }

    auto& nesys = registry.nesys();
    auto& game_kind = nesys.game_kind();
    if (ImGui::InputScalar("Registry GameKind", ImGuiDataType_S64, &game_kind))
    {
        dirty = true;
    }
    DrawInlineValidationError(
        gc::registry_config::IsRegistryDword(game_kind),
        "Enter an integer from 0 through 4294967295.");

    auto& event_next_time = nesys.event_next_time();
    if (ImGui::InputScalar("Registry EventNextTime", ImGuiDataType_S64, &event_next_time))
    {
        dirty = true;
    }
    DrawInlineValidationError(
        gc::registry_config::IsRegistryDword(event_next_time),
        "Enter an integer from 0 through 4294967295.");

    auto& condition_time = nesys.condition_time();
    if (ImGui::InputScalar("Registry ConditionTime", ImGuiDataType_S64, &condition_time))
    {
        dirty = true;
    }
    DrawInlineValidationError(
        gc::registry_config::IsRegistryDword(condition_time),
        "Enter an integer from 0 through 4294967295.");

    auto& log_level = nesys.log_level();
    if (ImGui::InputScalar("Registry LogLevel", ImGuiDataType_S64, &log_level))
    {
        dirty = true;
    }
    DrawInlineValidationError(
        gc::registry_config::IsRegistryLogLevel(log_level),
        "Enter an integer from 0 through 3.");

    auto& news_path = nesys.news_path();
    if (ImGui::InputText("Registry NewsPath", &news_path))
    {
        dirty = true;
    }
    DrawInlineValidationError(
        gc::registry_config::IsRegistryPath(news_path),
        "Path must contain 1-259 encoded bytes before the terminating NUL.");

    auto& event_path = nesys.event_path();
    if (ImGui::InputText("Registry EventPath", &event_path))
    {
        dirty = true;
    }
    DrawInlineValidationError(
        gc::registry_config::IsRegistryPath(event_path),
        "Path must contain 1-259 encoded bytes before the terminating NUL.");

    auto& log_path = nesys.log_path();
    if (ImGui::InputText("Registry LogPath", &log_path))
    {
        dirty = true;
    }
    DrawInlineValidationError(
        gc::registry_config::IsRegistryPath(log_path),
        "Path must contain 1-259 encoded bytes before the terminating NUL.");
}

void DrawExperimental(InputConfig& config, bool& dirty)
{
    ImGui::SeparatorText("Experimental");
    auto& experimental = config.experimental();
    auto& target_fps = experimental.target_fps();
    if (ImGui::InputScalar(
            "Target FPS",
            ImGuiDataType_U32,
            &target_fps,
            nullptr,
            nullptr,
            "%u",
            ImGuiInputTextFlags_CharsDecimal))
    {
        dirty = true;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip(
            "Fixed framerate expected for this launch (60-500).\n"
            "Configure the same cap in the driver or RTSS.\n"
            "Restart the game after changing it.");
    }
    const bool target_valid = gc::config::IsTargetFpsInRange(
        static_cast<std::uint32_t>(target_fps));
    DrawInlineValidationError(
        target_valid, "Enter an integer from 60 through 500.");
    if (target_valid && !gc::config::IsGameplayValidatedTargetFps(
            static_cast<std::uint32_t>(target_fps)))
    {
        ImGui::TextColored(
            ImVec4(1.0F, 0.75F, 0.2F, 1.0F),
            "This value is formula-driven but not individually gameplay-validated.");
    }

    bool timer_freeze = experimental.enable_timer_freeze_patches();
    if (ImGui::Checkbox("Timer freeze patches", &timer_freeze))
    {
        experimental.enable_timer_freeze_patches = timer_freeze;
        dirty = true;
    }
    bool storage_redirect = experimental.enable_testmode_storage_redirect();
    if (ImGui::Checkbox("Test-mode storage redirect", &storage_redirect))
    {
        experimental.enable_testmode_storage_redirect = storage_redirect;
        dirty = true;
    }
    bool nesys_adapter = experimental.enable_nesys_service_adapter_patch();
    if (ImGui::Checkbox("NESYS service adapter patch", &nesys_adapter))
    {
        experimental.enable_nesys_service_adapter_patch = nesys_adapter;
        dirty = true;
    }
    bool exclusive_audio = experimental.enable_wasapi_exclusive_audio();
    if (ImGui::Checkbox("WASAPI exclusive low-latency audio", &exclusive_audio))
    {
        experimental.enable_wasapi_exclusive_audio = exclusive_audio;
        dirty = true;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip(
            "Uses the default console endpoint in exclusive stereo PCM16 mode.\n"
            "Prefers exact 44.1 kHz and falls back to exact 48 kHz.");
    }
    auto& buffer_ms = experimental.wasapi_exclusive_buffer_ms();
    if (ImGui::InputScalar(
            "WASAPI exclusive buffer (ms)", ImGuiDataType_U32, &buffer_ms))
    {
        dirty = true;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("%s", kWasapiExclusiveBufferTooltip);
    }
}

std::expected<InputConfig, std::string> LoadConfig(
    const std::string& path)
{
    std::ifstream input(path);
    if (!input.is_open())
    {
        return std::unexpected("Could not open " + path + " for reading");
    }
    const std::string text{
        std::istreambuf_iterator<char>{input},
        std::istreambuf_iterator<char>{}};
    return gc::config::ParseAndValidateInputConfig(text);
}

std::expected<void, std::string> SaveConfig(
    const std::string& path,
    const InputConfig& config)
{
    const auto validation = gc::config::ValidateInputConfig(config);
    if (!validation)
    {
        return std::unexpected(validation.error());
    }
    try
    {
        std::ofstream output(path, std::ios::trunc);
        if (!output.is_open())
        {
            return std::unexpected("Could not open " + path + " for writing");
        }
        output << rfl::toml::write(config);
        if (!output)
        {
            return std::unexpected("Failed while writing " + path);
        }
        return {};
    }
    catch (const std::exception& error)
    {
        return std::unexpected(
            "Could not serialize configuration: " +
            std::string(error.what()));
    }
}

} // namespace

int main(int argc, char** argv)
{
    const std::string config_path = argc > 1 ? argv[1] : "config.toml";
    auto loaded = LoadConfig(config_path);
    if (!loaded)
    {
        std::cerr << loaded.error() << '\n';
        MessageBoxA(
            nullptr, loaded.error().c_str(), "ConfigGUI", MB_OK | MB_ICONERROR);
        return 1;
    }

    InputConfig config = std::move(*loaded);
    InputEditorModel editor(config.controller());
    GuiInputContext input(config, editor);
    Win32D3D11Host host;
    const auto opened = host.Open(
        GetModuleHandleW(nullptr), &GuiInputContext::MessageHandler, &input);
    if (!opened)
    {
        std::cerr << opened.error() << '\n';
        MessageBoxA(
            nullptr, opened.error().c_str(), "ConfigGUI", MB_OK | MB_ICONERROR);
        return 1;
    }
    input.SetWindow(host.window());

    const auto raw_input = RegisterGuiRawInput(host.window());
    if (!raw_input)
    {
        std::cerr << raw_input.error() << '\n';
        MessageBoxA(
            host.window(),
            raw_input.error().c_str(),
            "ConfigGUI",
            MB_OK | MB_ICONERROR);
        host.Close();
        return 1;
    }
    input.InitializeControllers();

    bool dirty = false;
    std::string save_status;
    constexpr ImVec4 clear_color(0.45F, 0.56F, 0.60F, 1.0F);

    while (host.PumpMessages())
    {
        input.PollSelectedController();
        if (input.ApplyCompletedCapture())
        {
            dirty = true;
        }

        host.BeginFrame();
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        constexpr ImGuiWindowFlags window_flags =
            ImGuiWindowFlags_NoDecoration |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoBringToFrontOnFocus |
            ImGuiWindowFlags_NoNavFocus;
        ImGui::Begin("GCLoader Configuration", nullptr, window_flags);

        ImGui::SeparatorText("Input");
        int input_mode = config.input_mode() == gc::input::InputMode::Keyboard
            ? 0
            : 1;
        constexpr const char* input_modes[]{"Keyboard", "Controller"};
        if (ImGui::Combo(
                "Input Mode", &input_mode, input_modes, IM_ARRAYSIZE(input_modes)))
        {
            config.input_mode = input_mode == 0
                ? gc::input::InputMode::Keyboard
                : gc::input::InputMode::Controller;
            dirty = true;
        }

        int gameplay_style =
            config.gameplay_input_style() ==
                    gc::input::GameplayInputStyle::Arcade
                ? 0
                : 1;
        constexpr const char* gameplay_styles[]{"Arcade", "Switch"};
        if (ImGui::Combo(
                "Gameplay Input Style",
                &gameplay_style,
                gameplay_styles,
                IM_ARRAYSIZE(gameplay_styles)))
        {
            config.gameplay_input_style = gameplay_style == 0
                ? gc::input::GameplayInputStyle::Arcade
                : gc::input::GameplayInputStyle::Switch;
            dirty = true;
        }

        constexpr std::array<std::uint32_t, 4> poll_rates{125, 250, 500, 1000};
        int poll_index = 0;
        for (std::size_t index = 0; index < poll_rates.size(); ++index)
        {
            if (poll_rates[index] == config.input_poll_hz())
            {
                poll_index = static_cast<int>(index);
                break;
            }
        }
        constexpr const char* poll_labels[]{"125 Hz", "250 Hz", "500 Hz", "1000 Hz"};
        if (ImGui::Combo(
                "Input polling rate",
                &poll_index,
                poll_labels,
                IM_ARRAYSIZE(poll_labels)))
        {
            config.input_poll_hz = poll_rates[static_cast<std::size_t>(poll_index)];
            dirty = true;
        }

        auto& press_threshold = config.axis_press_threshold_percent();
        if (ImGui::InputScalar(
                "Axis press threshold (%)",
                ImGuiDataType_U32,
                &press_threshold,
                nullptr,
                nullptr,
                "%u",
                ImGuiInputTextFlags_CharsDecimal))
        {
            dirty = true;
        }
        auto& release_threshold = config.axis_release_threshold_percent();
        if (ImGui::InputScalar(
                "Axis release threshold (%)",
                ImGuiDataType_U32,
                &release_threshold,
                nullptr,
                nullptr,
                "%u",
                ImGuiInputTextFlags_CharsDecimal))
        {
            dirty = true;
        }
        DrawInlineValidationError(
            press_threshold <= 100 && release_threshold <= 100 &&
                release_threshold < press_threshold,
            "Thresholds must be 0-100 and release must be lower than press.");

        if (config.input_mode() == gc::input::InputMode::Controller)
        {
            DrawControllerDevices(config, editor, input, dirty);
            ImGui::SeparatorText("Controller gameplay bindings");
            DrawControllerBindings(config, editor, input, dirty);
        }
        else
        {
            ImGui::SeparatorText("Keyboard gameplay bindings");
            DrawKeyboardBindings(config, input, true);
        }
        ImGui::SeparatorText("Keyboard system bindings");
        DrawKeyboardBindings(config, input, false);

        if (input.TakePopupRequested())
        {
            ImGui::OpenPopup("Bind Input");
        }
        bool popup_open = true;
        if (ImGui::BeginPopupModal(
                "Bind Input",
                &popup_open,
                ImGuiWindowFlags_AlwaysAutoResize |
                    ImGuiWindowFlags_NoMove))
        {
            ImGui::Text("Waiting for input for:");
            ImGui::TextColored(
                ImVec4(1.0F, 1.0F, 0.0F, 1.0F),
                "%s",
                input.capture_label().c_str());
            ImGui::Text("Release active controls, then press the desired input.");
            if (ImGui::Button("Cancel") ||
                ImGui::IsKeyPressed(ImGuiKey_Escape))
            {
                input.CancelCapture();
            }
            if (!popup_open)
            {
                input.CancelCapture();
            }
            if (input.TakeClosePopupRequested())
            {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        if (!input.capture_error().empty())
        {
            ImGui::TextColored(
                ImVec4(1.0F, 0.35F, 0.35F, 1.0F),
                "%s",
                input.capture_error().c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("Dismiss capture error"))
            {
                input.ClearCaptureError();
            }
        }

        ImGui::SeparatorText("NESYS");
        auto& server_ip = config.nesys().server_ip();
        if (ImGui::InputText("NESYS Server IPv4", &server_ip))
        {
            dirty = true;
        }
        DrawInlineValidationError(
            gc::nesys_service::IsDottedDecimalIpv4(server_ip),
            "Enter a dotted-decimal IPv4 address without a port.");

        DrawRegistry(config, dirty);
        DrawExperimental(config, dirty);

        config.controller = editor.config();
        const auto validation = gc::config::ValidateInputConfig(config);
        if (!validation)
        {
            ImGui::TextColored(
                ImVec4(1.0F, 0.35F, 0.35F, 1.0F),
                "%s",
                validation.error().c_str());
        }

        ImGui::Separator();
        ImGui::BeginDisabled(!validation.has_value() || !dirty);
        if (ImGui::Button("Save Configuration"))
        {
            const auto saved = SaveConfig(config_path, config);
            if (saved)
            {
                dirty = false;
                save_status = "Configuration saved.";
            }
            else
            {
                save_status = saved.error();
            }
        }
        ImGui::EndDisabled();
        if (dirty)
        {
            ImGui::SameLine();
            ImGui::TextColored(
                ImVec4(1.0F, 1.0F, 0.0F, 1.0F), "Unsaved changes");
        }
        if (!save_status.empty())
        {
            ImGui::TextWrapped("%s", save_status.c_str());
        }

        ImGui::End();
        host.Render(clear_color);
    }

    UnregisterGuiRawInput();
    host.Close();
    return 0;
}

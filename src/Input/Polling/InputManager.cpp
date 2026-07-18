#include "Input/Polling/InputManager.h"

#include "Platform/Win32/KeyMapping.h"
#include "plog/Log.h"

#include <iomanip>

namespace {

const char* input_mode_name(InputMode mode)
{
    switch (mode)
    {
    case InputMode::Keyboard:
        return "Keyboard";
    case InputMode::Gamepad:
        return "Gamepad";
    default:
        return "Unknown";
    }
}

}

InputManager::InputManager()
{
    LoadConfig();
    ReinitializeGamepad();
}

InputManager::~InputManager()
{
    CloseGamepad();
}

void InputManager::LoadConfig()
{
    const auto& config = ConfigManager::instance();

    keyP1Up = config.GetP1UpKey();
    keyP1Down = config.GetP1DownKey();
    keyP1Left = config.GetP1LeftKey();
    keyP1Right = config.GetP1RightKey();
    keyP1Button1 = config.GetP1Button1Key();
    keyP2Up = config.GetP2UpKey();
    keyP2Down = config.GetP2DownKey();
    keyP2Left = config.GetP2LeftKey();
    keyP2Right = config.GetP2RightKey();
    keyP2Button1 = config.GetP2Button1Key();
    keyTest = config.GetTestKey();
    keyService1 = config.GetService1Key();
    keyService2 = config.GetService2Key();
    keyService3 = config.GetService3Key();
    keyP1Start = config.GetP1StartKey();
    keyP2Start = config.GetP2StartKey();
    keyP2Service = config.GetP2ServiceKey();

    gpButtonP1Up = config.GetP1UpButton();
    gpButtonP1Down = config.GetP1DownButton();
    gpButtonP1Left = config.GetP1LeftButton();
    gpButtonP1Right = config.GetP1RightButton();
    gpButtonP1Button1 = config.GetP1Button1Button();
    gpButtonP2Up = config.GetP2UpButton();
    gpButtonP2Down = config.GetP2DownButton();
    gpButtonP2Left = config.GetP2LeftButton();
    gpButtonP2Right = config.GetP2RightButton();
    gpButtonP2Button1 = config.GetP2Button1Button();

    gpAxisP1Horizontal = config.GetP1HorizontalAxis();
    gpAxisP1Vertical = config.GetP1VerticalAxis();
    gpAxisP2Horizontal = config.GetP2HorizontalAxis();
    gpAxisP2Vertical = config.GetP2VerticalAxis();

    m_axisThreshold = config.GetGamepadAxisThreshold();
    m_targetGamepadIndex = config.GetGamepadIndex();
    m_inputMode = config.GetInputMode();

    PLOG_INFO << "Input configuration loaded: mode="
              << input_mode_name(m_inputMode)
              << ", gamepad_index=" << m_targetGamepadIndex
              << ", axis_threshold=" << m_axisThreshold
              << ", test_key=" << SDL_GetKeyName(keyTest)
              << ", test_vk=0x" << std::hex
              << SdlKeycodeToVirtualKey(keyTest) << std::dec;
}

void InputManager::OpenGamepad(SDL_JoystickID instance_id)
{
    if (m_gamepad != nullptr)
    {
        CloseGamepad();
    }

    m_gamepad = SDL_OpenGamepad(instance_id);
    if (m_gamepad == nullptr)
    {
        PLOG_ERROR << "Could not open gamepad with instance ID "
                   << instance_id << ": " << SDL_GetError();
        return;
    }

    m_gamepadInstanceId = instance_id;
    const char* name = SDL_GetGamepadName(m_gamepad);
    PLOG_INFO << "Opened gamepad: " << (name != nullptr ? name : "Unknown")
              << " (instance ID " << instance_id << ")";
    m_snapshotState.ClearGamepad();
}

void InputManager::CloseGamepad()
{
    if (m_gamepad != nullptr)
    {
        const char* name = SDL_GetGamepadName(m_gamepad);
        PLOG_INFO << "Closing gamepad: "
                  << (name != nullptr ? name : "Unknown")
                  << " (instance ID " << m_gamepadInstanceId << ")";
        SDL_CloseGamepad(m_gamepad);
        m_gamepad = nullptr;
        m_gamepadInstanceId = 0;
    }

    m_snapshotState.ClearGamepad();
}

void InputManager::ReinitializeGamepad()
{
    CloseGamepad();

    if (m_targetGamepadIndex < 0)
    {
        PLOG_INFO << "Gamepad input is disabled by configuration.";
        return;
    }

    int count = 0;
    SDL_JoystickID* gamepads = SDL_GetGamepads(&count);
    if (gamepads == nullptr || count == 0)
    {
        PLOG_INFO << "No gamepads detected.";
        SDL_free(gamepads);
        return;
    }

    int target_index = m_targetGamepadIndex;
    if (target_index >= count)
    {
        PLOG_WARNING << "Target gamepad index " << target_index
                     << " is out of range for " << count
                     << " devices; trying index 0.";
        target_index = 0;
    }

    const SDL_JoystickID target_id = gamepads[target_index];
    if (SDL_IsGamepad(target_id))
    {
        OpenGamepad(target_id);
    }
    else
    {
        PLOG_WARNING << "Device at index " << target_index
                     << " is not a recognized gamepad.";
    }

    SDL_free(gamepads);
}

void InputManager::HandleEvent(const SDL_Event& event)
{
    switch (event.type)
    {
    case SDL_EVENT_GAMEPAD_ADDED:
        PLOG_INFO << "Gamepad added: instance ID " << event.gdevice.which;
        if (m_gamepad == nullptr)
        {
            ReinitializeGamepad();
        }
        break;

    case SDL_EVENT_GAMEPAD_REMOVED:
        PLOG_INFO << "Gamepad removed: instance ID " << event.gdevice.which;
        if (m_gamepad != nullptr &&
            event.gdevice.which == m_gamepadInstanceId)
        {
            CloseGamepad();
        }
        break;

    case SDL_EVENT_KEY_DOWN:
        if (!event.key.repeat)
        {
            UpdateKeyState(event.key.key, true);
        }
        break;

    case SDL_EVENT_KEY_UP:
        UpdateKeyState(event.key.key, false);
        break;

    case SDL_EVENT_WINDOW_FOCUS_LOST:
        m_snapshotState.ClearKeyboard();
        break;

    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        if (m_gamepad != nullptr &&
            event.gbutton.which == m_gamepadInstanceId)
        {
            UpdateButtonState(
                static_cast<SDL_GamepadButton>(event.gbutton.button),
                true);
        }
        break;

    case SDL_EVENT_GAMEPAD_BUTTON_UP:
        if (m_gamepad != nullptr &&
            event.gbutton.which == m_gamepadInstanceId)
        {
            UpdateButtonState(
                static_cast<SDL_GamepadButton>(event.gbutton.button),
                false);
        }
        break;

    case SDL_EVENT_GAMEPAD_AXIS_MOTION:
        if (m_gamepad != nullptr &&
            event.gaxis.which == m_gamepadInstanceId)
        {
            UpdateAxisState(
                static_cast<SDL_GamepadAxis>(event.gaxis.axis),
                event.gaxis.value);
        }
        break;

    default:
        break;
    }
}

void InputManager::UpdateKeyState(SDL_Keycode key, bool pressed)
{
    using enum gc::input::LogicalInput;
    constexpr auto source = gc::input::InputSource::Keyboard;

    if (key == keyService1)
    {
        m_snapshotState.Set(Service1, source, pressed);
    }
    else if (key == keyService2)
    {
        m_snapshotState.Set(Service2, source, pressed);
    }
    else if (key == keyService3)
    {
        m_snapshotState.Set(Service3, source, pressed);
    }
    else if (key == keyP1Start)
    {
        m_snapshotState.Set(P1Start, source, pressed);
    }
    else if (key == keyP2Start)
    {
        m_snapshotState.Set(P2Start, source, pressed);
    }
    else if (key == keyTest)
    {
        m_snapshotState.Set(Test, source, pressed);
    }
    else if (key == keyP2Service)
    {
        m_snapshotState.Set(P2Service, source, pressed);
    }

    if (m_inputMode != InputMode::Keyboard)
    {
        return;
    }

    if (key == keyP1Up)
    {
        m_snapshotState.Set(LeftBoosterUp, source, pressed);
    }
    else if (key == keyP2Up)
    {
        m_snapshotState.Set(LeftBoosterDown, source, pressed);
    }
    else if (key == keyP1Down)
    {
        m_snapshotState.Set(LeftBoosterLeft, source, pressed);
    }
    else if (key == keyP2Down)
    {
        m_snapshotState.Set(LeftBoosterRight, source, pressed);
    }
    else if (key == keyP1Button1)
    {
        m_snapshotState.Set(LeftBoosterButton, source, pressed);
    }
    else if (key == keyP1Left)
    {
        m_snapshotState.Set(RightBoosterUp, source, pressed);
    }
    else if (key == keyP2Left)
    {
        m_snapshotState.Set(RightBoosterDown, source, pressed);
    }
    else if (key == keyP1Right)
    {
        m_snapshotState.Set(RightBoosterLeft, source, pressed);
    }
    else if (key == keyP2Right)
    {
        m_snapshotState.Set(RightBoosterRight, source, pressed);
    }
    else if (key == keyP2Button1)
    {
        m_snapshotState.Set(RightBoosterButton, source, pressed);
    }
}

void InputManager::HandleKeyboardVirtualKey(
    int virtual_key,
    bool pressed)
{
    if (virtual_key == 0)
    {
        return;
    }

    using enum gc::input::LogicalInput;
    constexpr auto source = gc::input::InputSource::Keyboard;
    const auto matches = [virtual_key](SDL_Keycode configured_key) {
        return SdlKeycodeToVirtualKey(configured_key) == virtual_key;
    };

    if (matches(keyService1))
    {
        m_snapshotState.Set(Service1, source, pressed);
    }
    if (matches(keyService2))
    {
        m_snapshotState.Set(Service2, source, pressed);
    }
    if (matches(keyService3))
    {
        m_snapshotState.Set(Service3, source, pressed);
    }
    if (matches(keyP1Start))
    {
        m_snapshotState.Set(P1Start, source, pressed);
    }
    if (matches(keyP2Start))
    {
        m_snapshotState.Set(P2Start, source, pressed);
    }
    const bool is_test_key = matches(keyTest);
    if (is_test_key)
    {
        m_snapshotState.Set(Test, source, pressed);
        PLOG_INFO << "Input test key transition: raw_vk=0x"
                  << std::hex << virtual_key
                  << " fastio=0x" << GetInput()
                  << std::dec
                  << " pressed=" << pressed;
    }
    if (matches(keyP2Service))
    {
        m_snapshotState.Set(P2Service, source, pressed);
    }

    if (m_inputMode != InputMode::Keyboard)
    {
        return;
    }

    if (matches(keyP1Up))
    {
        m_snapshotState.Set(LeftBoosterUp, source, pressed);
    }
    if (matches(keyP2Up))
    {
        m_snapshotState.Set(LeftBoosterDown, source, pressed);
    }
    if (matches(keyP1Down))
    {
        m_snapshotState.Set(LeftBoosterLeft, source, pressed);
    }
    if (matches(keyP2Down))
    {
        m_snapshotState.Set(LeftBoosterRight, source, pressed);
    }
    if (matches(keyP1Button1))
    {
        m_snapshotState.Set(LeftBoosterButton, source, pressed);
    }
    if (matches(keyP1Left))
    {
        m_snapshotState.Set(RightBoosterUp, source, pressed);
    }
    if (matches(keyP2Left))
    {
        m_snapshotState.Set(RightBoosterDown, source, pressed);
    }
    if (matches(keyP1Right))
    {
        m_snapshotState.Set(RightBoosterLeft, source, pressed);
    }
    if (matches(keyP2Right))
    {
        m_snapshotState.Set(RightBoosterRight, source, pressed);
    }
    if (matches(keyP2Button1))
    {
        m_snapshotState.Set(RightBoosterButton, source, pressed);
    }
}

void InputManager::ClearKeyboardInput() noexcept
{
    m_snapshotState.ClearKeyboard();
}

void InputManager::UpdateButtonState(
    SDL_GamepadButton button,
    bool pressed)
{
    if (m_inputMode != InputMode::Gamepad)
    {
        return;
    }

    using enum gc::input::LogicalInput;
    constexpr auto source = gc::input::InputSource::GamepadButton;

    if (button == gpButtonP1Up)
    {
        m_snapshotState.Set(LeftBoosterUp, source, pressed);
    }
    else if (button == gpButtonP2Up)
    {
        m_snapshotState.Set(LeftBoosterDown, source, pressed);
    }
    else if (button == gpButtonP1Down)
    {
        m_snapshotState.Set(LeftBoosterLeft, source, pressed);
    }
    else if (button == gpButtonP2Down)
    {
        m_snapshotState.Set(LeftBoosterRight, source, pressed);
    }
    else if (button == gpButtonP1Button1)
    {
        m_snapshotState.Set(LeftBoosterButton, source, pressed);
    }
    else if (button == gpButtonP1Left)
    {
        m_snapshotState.Set(RightBoosterUp, source, pressed);
    }
    else if (button == gpButtonP2Left)
    {
        m_snapshotState.Set(RightBoosterDown, source, pressed);
    }
    else if (button == gpButtonP1Right)
    {
        m_snapshotState.Set(RightBoosterLeft, source, pressed);
    }
    else if (button == gpButtonP2Right)
    {
        m_snapshotState.Set(RightBoosterRight, source, pressed);
    }
    else if (button == gpButtonP2Button1)
    {
        m_snapshotState.Set(RightBoosterButton, source, pressed);
    }
}

void InputManager::UpdateAxisState(SDL_GamepadAxis axis, Sint16 value)
{
    if (m_inputMode != InputMode::Gamepad)
    {
        return;
    }

    using enum gc::input::LogicalInput;
    constexpr auto source = gc::input::InputSource::GamepadAxis;
    const bool negative = value < -m_axisThreshold;
    const bool positive = value > m_axisThreshold;

    if (axis == gpAxisP1Vertical)
    {
        m_snapshotState.Set(LeftBoosterUp, source, negative);
        m_snapshotState.Set(LeftBoosterDown, source, positive);
    }
    else if (axis == gpAxisP1Horizontal)
    {
        m_snapshotState.Set(LeftBoosterLeft, source, negative);
        m_snapshotState.Set(LeftBoosterRight, source, positive);
    }
    else if (axis == gpAxisP2Vertical)
    {
        m_snapshotState.Set(RightBoosterUp, source, negative);
        m_snapshotState.Set(RightBoosterDown, source, positive);
    }
    else if (axis == gpAxisP2Horizontal)
    {
        m_snapshotState.Set(RightBoosterLeft, source, negative);
        m_snapshotState.Set(RightBoosterRight, source, positive);
    }
}

std::uint32_t InputManager::GetInput() const noexcept
{
    const auto gameplay_source = m_inputMode == InputMode::Keyboard
        ? gc::input::GameplaySource::Keyboard
        : gc::input::GameplaySource::Gamepad;
    return m_snapshotState.Compose(gameplay_source);
}

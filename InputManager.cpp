#include "InputManager.h"
#include "config.h" // Assuming this exists and is set up
#include "plog/Log.h"
#include "plog/Initializers/RollingFileInitializer.h"
#include <vector>

InputManager::InputManager()
{
    plog::init(plog::info, "loader-log.txt");
    LoadConfig(); // Load mappings from ConfigManager
    ReinitializeGamepad(); // Attempt to open the configured gamepad
}

InputManager::~InputManager()
{
    CloseGamepad();
    PLOG_INFO << "Shutting down SDL.";
    SDL_Quit();
}

void InputManager::LoadConfig()
{
    PLOG_DEBUG << "Loading input configuration...";
    auto& config = ConfigManager::instance(); // Get singleton instance

    // --- Load Keyboard Config ---
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
    keyService1 = config.GetService1Key(); // F1
    keyService2 = config.GetService2Key(); // I
    keyService3 = config.GetService3Key(); // P
    keyP1Start = config.GetP1StartKey(); // 1
    keyP2Start = config.GetP2StartKey(); // 2
    keyP2Service = config.GetP2ServiceKey(); // F2

    // --- Load Gamepad Config ---
    gpButtonP1Up = config.GetP1UpButton(); // Default: DPAD_UP
    gpButtonP1Down = config.GetP1DownButton(); // Default: DPAD_DOWN
    gpButtonP1Left = config.GetP1LeftButton(); // Default: DPAD_LEFT
    gpButtonP1Right = config.GetP1RightButton(); // Default: DPAD_RIGHT
    gpButtonP1Button1 = config.GetP1Button1Button(); // Default: SOUTH (A)

    gpButtonP2Up = config.GetP2UpButton(); // Default: INVALID (use axis primarily)
    gpButtonP2Down = config.GetP2DownButton(); // Default: INVALID
    gpButtonP2Left = config.GetP2LeftButton(); // Default: INVALID
    gpButtonP2Right = config.GetP2RightButton(); // Default: INVALID
    gpButtonP2Button1 = config.GetP2Button1Button(); // Default: EAST (B)

    gpAxisP1Horizontal = config.GetP1HorizontalAxis(); // Default: LEFTX
    gpAxisP1Vertical = config.GetP1VerticalAxis(); // Default: LEFTY

    gpAxisP2Horizontal = config.GetP2HorizontalAxis(); // Default: RIGHTX
    gpAxisP2Vertical = config.GetP2VerticalAxis(); // Default: RIGHTY

    // --- Load Other Config ---
    m_axisThreshold = config.GetGamepadAxisThreshold(); // Default: 16384
    m_targetGamepadIndex = config.GetGamepadIndex(); // Default: 0
    m_inputMode = config.GetInputMode();
    
    PLOG_INFO << "Input configuration loaded.\n" ;
    // Log the loaded key codes and button mappings if needed for debugging
}


void InputManager::OpenGamepad(SDL_JoystickID instance_id)
{
    if (m_gamepad)
    {
        PLOG_WARNING << "Gamepad already open. Closing first.";
        CloseGamepad();
    }

    m_gamepad = SDL_OpenGamepad(instance_id);
    if (!m_gamepad)
    {
        PLOG_ERROR << "Could not open gamepad with instance ID " << instance_id << ": " << SDL_GetError();
        return;
    }

    m_gamepadInstanceId = instance_id; // Store the ID of the opened gamepad
    const char* name = SDL_GetGamepadName(m_gamepad);
    PLOG_INFO << "Opened Gamepad: " << (name ? name : "Unknown") << " (Instance ID: " << instance_id << ")";

    // Reset gamepad specific states
    stateP1AxisUp = stateP1AxisDown = stateP1AxisLeft = stateP1AxisRight = false;
    stateP2AxisUp = stateP2AxisDown = stateP2AxisLeft = stateP2AxisRight = false;
    // Reset button states tied to gamepad buttons
    if (gpButtonP1Up != SDL_GAMEPAD_BUTTON_INVALID) stateP1Up = false;
    if (gpButtonP1Down != SDL_GAMEPAD_BUTTON_INVALID) stateP1Down = false;
    // ... reset all other gamepad-linked states
}

void InputManager::CloseGamepad()
{
    if (m_gamepad)
    {
        const char* name = SDL_GetGamepadName(m_gamepad);
        PLOG_INFO << "Closing Gamepad: " << (name ? name : "Unknown") << " (Instance ID: " << m_gamepadInstanceId <<
 ")";
        SDL_CloseGamepad(m_gamepad);
        m_gamepad = nullptr;
        m_gamepadInstanceId = 0;

        // Reset states that depend on the gamepad
        stateP1AxisUp = stateP1AxisDown = stateP1AxisLeft = stateP1AxisRight = false;
        stateP2AxisUp = stateP2AxisDown = stateP2AxisLeft = stateP2AxisRight = false;
        // Determine combined state again after resetting axis parts
        stateP1Up = (stateP1Up && keyP1Up != SDLK_UNKNOWN); // Keep keyboard state if mapped
        stateP1Down = (stateP1Down && keyP1Down != SDLK_UNKNOWN);
        stateP1Left = (stateP1Left && keyP1Left != SDLK_UNKNOWN);
        stateP1Right = (stateP1Right && keyP1Right != SDLK_UNKNOWN);
        stateP1Button1 = (stateP1Button1 && keyP1Button1 != SDLK_UNKNOWN);
        stateP2Up = (stateP2Up && keyP2Up != SDLK_UNKNOWN);
        stateP2Down = (stateP2Down && keyP2Down != SDLK_UNKNOWN);
        stateP2Left = (stateP2Left && keyP2Left != SDLK_UNKNOWN);
        stateP2Right = (stateP2Right && keyP2Right != SDLK_UNKNOWN);
        stateP2Button1 = (stateP2Button1 && keyP2Button1 != SDLK_UNKNOWN);
    }
}

void InputManager::ReinitializeGamepad()
{
    CloseGamepad(); // Ensure any existing gamepad is closed

    SDL_JoystickID* gamepads = SDL_GetGamepads(nullptr);
    if (!gamepads)
    {
        PLOG_INFO << "No gamepads detected.";
        return;
    }

    int count = 0;
    for (SDL_JoystickID* it = gamepads; *it != 0; ++it)
    {
        count++;
    }


    if (m_targetGamepadIndex < count)
    {
        SDL_JoystickID target_id = gamepads[m_targetGamepadIndex];
        if (SDL_IsGamepad(target_id))
        {
            OpenGamepad(target_id);
        }
        else
        {
            PLOG_WARNING << "Device at index " << m_targetGamepadIndex << " is not a recognized gamepad.";
        }
    }
    else if (count > 0)
    {
        PLOG_WARNING << "Target gamepad index " << m_targetGamepadIndex << " out of range. Found " << count <<
 " devices. Trying index 0.";
        if (SDL_IsGamepad(gamepads[0]))
        {
            OpenGamepad(gamepads[0]);
        }
        else
        {
            PLOG_WARNING << "Device at index 0 is not a recognized gamepad.";
        }
    }
    else
    {
        PLOG_INFO << "No suitable gamepads found.";
    }

    SDL_free(gamepads);
}


void InputManager::HandleEvent(const SDL_Event& event)
{
    switch (event.type)
    {
    case SDL_EVENT_GAMEPAD_ADDED:
        PLOG_INFO << "Gamepad Added: Instance ID " << event.gdevice.which;
        if (!m_gamepad)
        {
            // If we don't have one open already
            // Check if this new gamepad matches our target index (tricky without re-scanning)
            // Simplest: just try to reinitialize based on index
            ReinitializeGamepad();
        }
        break;

    case SDL_EVENT_GAMEPAD_REMOVED:
        PLOG_INFO << "Gamepad Removed: Instance ID " << event.gdevice.which;
        if (m_gamepad && event.gdevice.which == m_gamepadInstanceId)
        {
            CloseGamepad();
            // Optionally, try to open another gamepad immediately
            // ReinitializeGamepad();
        }
        break;

    // --- Keyboard Events ---
    case SDL_EVENT_KEY_DOWN:
        if (!event.key.repeat)
        {
            // Ignore key repeats for state changes
            UpdateKeyState(event.key.key, true);
            PLOG_DEBUG << "Key Down: " << SDL_GetKeyName(event.key.key);
        }
        break;
    case SDL_EVENT_KEY_UP:
        UpdateKeyState(event.key.key, false);
        PLOG_DEBUG << "Key Up: " << SDL_GetKeyName(event.key.key);
        break;

    // --- Gamepad Events ---
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        if (m_gamepad && event.gbutton.which == m_gamepadInstanceId)
        {
            UpdateButtonState(static_cast<SDL_GamepadButton>(event.gbutton.button), true);
            PLOG_DEBUG << "Gamepad Button Down: " << event.gbutton.button;
        }
        break;
    case SDL_EVENT_GAMEPAD_BUTTON_UP:
        if (m_gamepad && event.gbutton.which == m_gamepadInstanceId)
        {
            UpdateButtonState(static_cast<SDL_GamepadButton>(event.gbutton.button), false);
            PLOG_DEBUG << "Gamepad Button Up: " << event.gbutton.button;
        }
        break;
    case SDL_EVENT_GAMEPAD_AXIS_MOTION:
        if (m_gamepad && event.gaxis.which == m_gamepadInstanceId)
        {
            UpdateAxisState(static_cast<SDL_GamepadAxis>(event.gaxis.axis), event.gaxis.value);
            // Optional: Log axis value only if it changes significantly
        }
        break;

    default:
        break; // Ignore other event types
    }
}

void InputManager::UpdateKeyState(SDL_Keycode key, bool pressed)
{
    if (m_inputMode == InputMode::Keyboard)
    {
        // Only update the main control when in kb mode
        if (key == keyP1Up) stateP1Up = pressed;
        else if (key == keyP1Down) stateP1Down = pressed;
        else if (key == keyP1Left) stateP1Left = pressed;
        else if (key == keyP1Right) stateP1Right = pressed;
        else if (key == keyP1Button1) stateP1Button1 = pressed;

        else if (key == keyP2Up) stateP2Up = pressed;
        else if (key == keyP2Down) stateP2Down = pressed;
        else if (key == keyP2Left) stateP2Left = pressed;
        else if (key == keyP2Right) stateP2Right = pressed;
        else if (key == keyP2Button1) stateP2Button1 = pressed;
        return;
    }
    // Update service/test regardless of modes
    if (key == keyService1) stateService1 = pressed;
    else if (key == keyService2) stateService2 = pressed;
    else if (key == keyService3) stateService3 = pressed;
    else if (key == keyP1Start) stateP1Start = pressed;
    else if (key == keyP2Start) stateP2Start = pressed;
    else if (key == keyTest) stateTest = pressed;
    else if (key == keyP2Service) stateP2Service = pressed;

}

void InputManager::UpdateButtonState(SDL_GamepadButton button, bool pressed)
{
    if (m_inputMode == InputMode::Keyboard)
    {
        return;
    }
    // P1 Directions (D-Pad)
    if (button == gpButtonP1Up) stateP1Up = pressed;
    else if (button == gpButtonP1Down) stateP1Down = pressed;
    else if (button == gpButtonP1Left) stateP1Left = pressed;
    else if (button == gpButtonP1Right) stateP1Right = pressed;
    else if (button == gpButtonP2Up) stateP2Up = pressed;
    else if (button == gpButtonP2Down) stateP2Down = pressed;
    else if (button == gpButtonP2Left) stateP2Left = pressed;
    else if (button == gpButtonP2Right) stateP2Right = pressed;
    // P1/P2 Buttons
    else if (button == gpButtonP1Button1) stateP1Button1 = pressed;
    else if (button == gpButtonP2Button1) stateP2Button1 = pressed;

}


void InputManager::UpdateAxisState(SDL_GamepadAxis axis, Sint16 value)
{
    if (m_inputMode == InputMode::Keyboard)
    {
        return;
    }
    bool changed = false;

    // --- P1 Axes (Left Stick) ---
    if (axis == SDL_GAMEPAD_AXIS_LEFTY)
    {
        bool newUp = value < -m_axisThreshold;
        bool newDown = value > m_axisThreshold;
        if (newUp != stateP1AxisUp)
        {
            stateP1AxisUp = newUp;
            changed = true;
        }
        if (newDown != stateP2AxisUp)
        {
            stateP2AxisUp = newDown;
            changed = true;
        }
    }
    else if (axis == SDL_GAMEPAD_AXIS_LEFTX)
    {
        bool newLeft = value < -m_axisThreshold;
        bool newRight = value > m_axisThreshold;
        if (newLeft != stateP1AxisDown)
        {
            stateP1AxisDown = newLeft;
            changed = true;
        }
        if (newRight != stateP2AxisDown)
        {
            stateP2AxisDown = newRight;
            changed = true;
        }
    }
    // --- P2 Axes (Right Stick) ---
    else if (axis == SDL_GAMEPAD_AXIS_RIGHTY)
    {
        bool newUp = value < -m_axisThreshold;
        bool newDown = value > m_axisThreshold;
        if (newUp != stateP1AxisLeft)
        {
            stateP1AxisLeft = newUp;
            changed = true;
        }
        if (newDown != stateP2AxisLeft)
        {
            stateP2AxisLeft = newDown;
            changed = true;
        }
    }
    else if (axis == SDL_GAMEPAD_AXIS_RIGHTX)
    {
        bool newLeft = value < -m_axisThreshold;
        bool newRight = value > m_axisThreshold;
        if (newLeft != stateP1AxisRight)
        {
            stateP1AxisRight = newLeft;
            changed = true;
        }
        if (newRight != stateP2AxisRight)
        {
            stateP2AxisRight = newRight;
            changed = true;
        }
    }

    // Update combined state if an axis changed state
    if (changed)
    {
        stateP1Up = stateP1AxisUp;
        stateP1Down = stateP1AxisDown;
        stateP1Left = stateP1AxisLeft;
        stateP1Right = stateP1AxisRight;
        
        // *** Update P2 state logic to include buttons ***
        stateP2Up = stateP2AxisUp; // Added p2UpButton
        stateP2Down = stateP2AxisDown; // Added p2DownButton
        stateP2Left = stateP2AxisLeft; // Added p2LeftButton
        stateP2Right = stateP2AxisRight; // Added p2RightButton

        // Optional: Log axis state change
        // PLOG_DEBUG << "Axis state changed: P1(" << stateP1Up << stateP1Down << stateP1Left << stateP1Right
        //            << ") P2(" << stateP2Up << stateP2Down << stateP2Left << stateP2Right << ")";
    }
}


DWORD InputManager::GetInput() const
{
    DWORD inputState = 0;

    // P1 Inputs
    if (stateService1) inputState |= InputBits::P1_SERVICE_F1;
    if (stateService2) inputState |= InputBits::P1_SERVICE_I;
    if (stateService3) inputState |= InputBits::P1_SERVICE_P;
    if (stateP1Start) inputState |= InputBits::P1_START;
    if (stateTest) inputState |= InputBits::TEST_MODE;
    if (stateP1Up) inputState |= InputBits::P1_UP;
    if (stateP1Down) inputState |= InputBits::P1_DOWN;
    if (stateP1Left) inputState |= InputBits::P1_LEFT;
    if (stateP1Right) inputState |= InputBits::P1_RIGHT;
    if (stateP1Button1)inputState |= InputBits::P1_BUTTON_1;

    if (stateP2Service) inputState |= InputBits::P2_SERVICE;
    if (stateP2Start) inputState |= InputBits::P2_START;
    if (stateP2Up) inputState |= InputBits::P2_UP;
    if (stateP2Down) inputState |= InputBits::P2_DOWN;
    if (stateP2Left) inputState |= InputBits::P2_LEFT;
    if (stateP2Right) inputState |= InputBits::P2_RIGHT;
    if (stateP2Button1) inputState |= InputBits::P2_BUTTON_1;

    return inputState;
}

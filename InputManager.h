#pragma once

#include <SDL3/SDL.h>
#include <string>
#include <cstdint> // For uint32_t

#include "config.h"

namespace InputBits {
    constexpr DWORD P1_SERVICE_I   = (1 << 1);
    constexpr DWORD P1_SERVICE_F1  = (1 << 2);
    constexpr DWORD P1_SERVICE_P   = (1 << 3);
    constexpr DWORD P2_SERVICE_F2  = (1 << 2);
    constexpr DWORD P1_START      = (1 << 4);
    constexpr DWORD TEST_MODE     = (1 << 6);
    constexpr DWORD P1_UP         = (1 << 8);
    constexpr DWORD P2_UP         = (1 << 9);
    constexpr DWORD P1_DOWN       = (1 << 10);
    constexpr DWORD P2_DOWN       = (1 << 11);
    constexpr DWORD P1_LEFT       = (1 << 12);
    constexpr DWORD P2_LEFT       = (1 << 13);
    constexpr DWORD P1_RIGHT      = (1 << 14);
    constexpr DWORD P2_RIGHT      = (1 << 15);
    constexpr DWORD P1_BUTTON_1   = (1 << 16);
    constexpr DWORD P2_BUTTON_1   = (1 << 17);
    constexpr DWORD P2_SERVICE    = (1 << 2);
    constexpr DWORD P2_START      = (1 << 5);
}


class InputManager {
public:
    InputManager();
    ~InputManager();

    InputManager(const InputManager&) = delete;
    InputManager& operator=(const InputManager&) = delete;

    // Call this frequently from your main loop AFTER SDL_PollEvent
    void HandleEvent(const SDL_Event& event);

    // Call this when your game logic needs the current input state
    DWORD GetInput() const;

    // Force closing and reopening the gamepad (e.g., if config changes)
    void ReinitializeGamepad();

private:
    void LoadConfig();
    void OpenGamepad(SDL_JoystickID instance_id);
    void CloseGamepad();
    void UpdateAxisState(SDL_GamepadAxis axis, Sint16 value);
    void UpdateButtonState(SDL_GamepadButton button, bool pressed);
    void UpdateKeyState(SDL_Keycode key, bool pressed);

    // --- Configuration Storage ---
    // Keyboard Mappings
    SDL_Keycode keyP1Up = SDLK_UNKNOWN;
    SDL_Keycode keyP1Down = SDLK_UNKNOWN;
    SDL_Keycode keyP1Left = SDLK_UNKNOWN;
    SDL_Keycode keyP1Right = SDLK_UNKNOWN;
    SDL_Keycode keyP1Button1 = SDLK_UNKNOWN;
    SDL_Keycode keyP2Up = SDLK_UNKNOWN;
    SDL_Keycode keyP2Down = SDLK_UNKNOWN;
    SDL_Keycode keyP2Left = SDLK_UNKNOWN;
    SDL_Keycode keyP2Right = SDLK_UNKNOWN;
    SDL_Keycode keyP2Button1 = SDLK_UNKNOWN;
    SDL_Keycode keyTest = SDLK_UNKNOWN;
    SDL_Keycode keyService1 = SDLK_UNKNOWN; // e.g., F1
    SDL_Keycode keyService2 = SDLK_UNKNOWN; // e.g., I
    SDL_Keycode keyService3 = SDLK_UNKNOWN; // e.g., P
    SDL_Keycode keyP1Start = SDLK_UNKNOWN;  // e.g., 1
    SDL_Keycode keyP2Start = SDLK_UNKNOWN;  // e.g., 2
    SDL_Keycode keyP2Service = SDLK_UNKNOWN; // e.g., F2

    // Gamepad Mappings (for ONE controller)
    SDL_GamepadButton gpButtonP1Up = SDL_GAMEPAD_BUTTON_INVALID;       // Usually DPAD_UP
    SDL_GamepadButton gpButtonP1Down = SDL_GAMEPAD_BUTTON_INVALID;     // Usually DPAD_DOWN
    SDL_GamepadButton gpButtonP1Left = SDL_GAMEPAD_BUTTON_INVALID;     // Usually DPAD_LEFT
    SDL_GamepadButton gpButtonP1Right = SDL_GAMEPAD_BUTTON_INVALID;    // Usually DPAD_RIGHT
    SDL_GamepadButton gpButtonP1Button1 = SDL_GAMEPAD_BUTTON_INVALID;  // Usually SOUTH (A/X)

    SDL_GamepadButton gpButtonP2Up = SDL_GAMEPAD_BUTTON_INVALID;
    SDL_GamepadButton gpButtonP2Down = SDL_GAMEPAD_BUTTON_INVALID;
    SDL_GamepadButton gpButtonP2Left = SDL_GAMEPAD_BUTTON_INVALID;
    SDL_GamepadButton gpButtonP2Right = SDL_GAMEPAD_BUTTON_INVALID;
    SDL_GamepadButton gpButtonP2Button1 = SDL_GAMEPAD_BUTTON_INVALID;  // Usually EAST (B/O)


    // P1 Axes (Left Stick default)
    SDL_GamepadAxis gpAxisP1Horizontal = SDL_GAMEPAD_AXIS_INVALID; // Usually LEFTX
    SDL_GamepadAxis gpAxisP1Vertical = SDL_GAMEPAD_AXIS_INVALID;   // Usually LEFTY

    // P2 Axes (Right Stick default)
    SDL_GamepadAxis gpAxisP2Horizontal = SDL_GAMEPAD_AXIS_INVALID; // Usually RIGHTX
    SDL_GamepadAxis gpAxisP2Vertical = SDL_GAMEPAD_AXIS_INVALID;   // Usually RIGHTY


    Sint16 m_axisThreshold = 16384; // Default threshold
    int m_targetGamepadIndex = 0;   // Default to first gamepad
    InputMode m_inputMode;

    // --- Runtime State ---
    SDL_Gamepad* m_gamepad = nullptr;
    SDL_JoystickID m_gamepadInstanceId = 0;

    // Input States (updated by HandleEvent)
    bool stateP1Up = false;
    bool stateP1Down = false;
    bool stateP1Left = false;
    bool stateP1Right = false;
    bool stateP1Button1 = false;

    bool stateP2Up = false;
    bool stateP2Down = false;
    bool stateP2Left = false;
    bool stateP2Right = false;
    bool stateP2Button1 = false;

    bool stateTest = false;
    bool stateService1 = false; // F1 state
    bool stateService2 = false; // I state
    bool stateService3 = false; // P state
    bool stateP1Start = false;
    bool stateP2Start = false;
    bool stateP2Service = false; // F2 state


    // Internal tracking for axes to handle returning to neutral
    bool stateP1AxisUp = false;
    bool stateP1AxisDown = false;
    bool stateP1AxisLeft = false;
    bool stateP1AxisRight = false;
    bool stateP2AxisUp = false;
    bool stateP2AxisDown = false;
    bool stateP2AxisLeft = false;
    bool stateP2AxisRight = false;
};
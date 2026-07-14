#pragma once

#include <SDL3/SDL.h>

#include <cstdint>

#include "InputSnapshotState.h"
#include "config.h"

class InputManager {
public:
    InputManager();
    ~InputManager();

    InputManager(const InputManager&) = delete;
    InputManager& operator=(const InputManager&) = delete;

    void HandleEvent(const SDL_Event& event);
    std::uint32_t GetInput() const noexcept;
    void ReinitializeGamepad();

private:
    void LoadConfig();
    void OpenGamepad(SDL_JoystickID instance_id);
    void CloseGamepad();
    void UpdateAxisState(SDL_GamepadAxis axis, Sint16 value);
    void UpdateButtonState(SDL_GamepadButton button, bool pressed);
    void UpdateKeyState(SDL_Keycode key, bool pressed);

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
    SDL_Keycode keyService1 = SDLK_UNKNOWN;
    SDL_Keycode keyService2 = SDLK_UNKNOWN;
    SDL_Keycode keyService3 = SDLK_UNKNOWN;
    SDL_Keycode keyP1Start = SDLK_UNKNOWN;
    SDL_Keycode keyP2Start = SDLK_UNKNOWN;
    SDL_Keycode keyP2Service = SDLK_UNKNOWN;

    SDL_GamepadButton gpButtonP1Up = SDL_GAMEPAD_BUTTON_INVALID;
    SDL_GamepadButton gpButtonP1Down = SDL_GAMEPAD_BUTTON_INVALID;
    SDL_GamepadButton gpButtonP1Left = SDL_GAMEPAD_BUTTON_INVALID;
    SDL_GamepadButton gpButtonP1Right = SDL_GAMEPAD_BUTTON_INVALID;
    SDL_GamepadButton gpButtonP1Button1 = SDL_GAMEPAD_BUTTON_INVALID;
    SDL_GamepadButton gpButtonP2Up = SDL_GAMEPAD_BUTTON_INVALID;
    SDL_GamepadButton gpButtonP2Down = SDL_GAMEPAD_BUTTON_INVALID;
    SDL_GamepadButton gpButtonP2Left = SDL_GAMEPAD_BUTTON_INVALID;
    SDL_GamepadButton gpButtonP2Right = SDL_GAMEPAD_BUTTON_INVALID;
    SDL_GamepadButton gpButtonP2Button1 = SDL_GAMEPAD_BUTTON_INVALID;

    SDL_GamepadAxis gpAxisP1Horizontal = SDL_GAMEPAD_AXIS_INVALID;
    SDL_GamepadAxis gpAxisP1Vertical = SDL_GAMEPAD_AXIS_INVALID;
    SDL_GamepadAxis gpAxisP2Horizontal = SDL_GAMEPAD_AXIS_INVALID;
    SDL_GamepadAxis gpAxisP2Vertical = SDL_GAMEPAD_AXIS_INVALID;

    Sint16 m_axisThreshold = 16384;
    int m_targetGamepadIndex = 0;
    InputMode m_inputMode = InputMode::Keyboard;
    SDL_Gamepad* m_gamepad = nullptr;
    SDL_JoystickID m_gamepadInstanceId = 0;
    gc::input::InputSnapshotState m_snapshotState;
};

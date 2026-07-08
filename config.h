#pragma once
#include <map>
#include <stdexcept>
#include <string>
#include <Windows.h>
#include "plog/Log.h"
#include "SDL3/SDL.h"

#include <vector> // If you had lists, not needed here

// Make sure parsers are included *before* defining structs that use them
#include "SdlRflParsers.h"

enum class InputMode {
    Keyboard,
    Gamepad
};

struct KeyboardConfig
{
    // Use rfl::Rename to match TOML keys with C++ variable names
    rfl::Rename<"p1_up", SDL_Keycode> p1_up = SDLK_W; // Default values
    rfl::Rename<"p1_down", SDL_Keycode> p1_down = SDLK_S;
    rfl::Rename<"p1_left", SDL_Keycode> p1_left = SDLK_A;
    rfl::Rename<"p1_right", SDL_Keycode> p1_right = SDLK_D;
    rfl::Rename<"p1_button1", SDL_Keycode> p1_button1 = SDLK_SPACE;

    rfl::Rename<"p2_up", SDL_Keycode> p2_up = SDLK_UP;
    rfl::Rename<"p2_down", SDL_Keycode> p2_down = SDLK_DOWN;
    rfl::Rename<"p2_left", SDL_Keycode> p2_left = SDLK_LEFT;
    rfl::Rename<"p2_right", SDL_Keycode> p2_right = SDLK_RIGHT;
    rfl::Rename<"p2_button1", SDL_Keycode> p2_button1 = SDLK_K;

    rfl::Rename<"test", SDL_Keycode> test = SDLK_T;
    rfl::Rename<"service1", SDL_Keycode> service1 = SDLK_F1; // F1
    rfl::Rename<"service2", SDL_Keycode> service2 = SDLK_I; // I
    rfl::Rename<"service3", SDL_Keycode> service3 = SDLK_P; // P
    rfl::Rename<"p1_start", SDL_Keycode> p1_start = SDLK_1;
    rfl::Rename<"p2_start", SDL_Keycode> p2_start = SDLK_2;
    rfl::Rename<"p2_service", SDL_Keycode> p2_service = SDLK_F2; // F2
    rfl::Rename<"card_read", SDL_Keycode> card_read = SDLK_F4;
};

struct ExperimentalConfig
{
    rfl::Rename<"enable_120fps_timer_patches", bool> enable_120fps_timer_patches = false;
    rfl::Rename<"enable_testmode_storage_redirect", bool> enable_testmode_storage_redirect = false;
    rfl::Rename<"enable_timer_freeze_patches", bool> enable_timer_freeze_patches = false;
};

struct GamepadConfig
{
    rfl::Rename<"p1_dpad_up", SDL_GamepadButton> p1_dpad_up = SDL_GAMEPAD_BUTTON_DPAD_UP;
    rfl::Rename<"p1_dpad_down", SDL_GamepadButton> p1_dpad_down = SDL_GAMEPAD_BUTTON_DPAD_DOWN;
    rfl::Rename<"p1_dpad_left", SDL_GamepadButton> p1_dpad_left = SDL_GAMEPAD_BUTTON_DPAD_LEFT;
    rfl::Rename<"p1_dpad_right", SDL_GamepadButton> p1_dpad_right = SDL_GAMEPAD_BUTTON_DPAD_RIGHT;
    rfl::Rename<"p1_button1", SDL_GamepadButton> p1_button1 = SDL_GAMEPAD_BUTTON_SOUTH; // A/X

    rfl::Rename<"p1_axis_horizontal", SDL_GamepadAxis> p1_axis_horizontal = SDL_GAMEPAD_AXIS_LEFTX;
    rfl::Rename<"p1_axis_vertical", SDL_GamepadAxis> p1_axis_vertical = SDL_GAMEPAD_AXIS_LEFTY;

    rfl::Rename<"p2_axis_horizontal", SDL_GamepadAxis> p2_axis_horizontal = SDL_GAMEPAD_AXIS_RIGHTX;
    rfl::Rename<"p2_axis_vertical", SDL_GamepadAxis> p2_axis_vertical = SDL_GAMEPAD_AXIS_RIGHTY;
    rfl::Rename<"p2_button1", SDL_GamepadButton> p2_button1 = SDL_GAMEPAD_BUTTON_EAST; // B/O

    // Optional P2 Direction Buttons
    rfl::Rename<"p2_button_up", SDL_GamepadButton> p2_button_up = SDL_GAMEPAD_BUTTON_INVALID; // Default to invalid
    rfl::Rename<"p2_button_down", SDL_GamepadButton> p2_button_down = SDL_GAMEPAD_BUTTON_INVALID;
    rfl::Rename<"p2_button_left", SDL_GamepadButton> p2_button_left = SDL_GAMEPAD_BUTTON_INVALID;
    rfl::Rename<"p2_button_right", SDL_GamepadButton> p2_button_right = SDL_GAMEPAD_BUTTON_INVALID;
};


struct InputConfig
{
    // Top-level settings
    rfl::Rename<"gamepad_index", int> gamepad_index = 0;
    rfl::Rename<"axis_threshold", Sint16> axis_threshold = 16384;
    rfl::Rename<"input_mode", InputMode> input_mode = InputMode::Keyboard; // Default to keyboard

    // Nested tables require nested structs
    rfl::Rename<"keyboard", KeyboardConfig> keyboard;
    rfl::Rename<"gamepad", GamepadConfig> gamepad;
    rfl::Rename<"experimental", ExperimentalConfig> experimental;
};


class ConfigManager
{
public:
    static ConfigManager& instance()
    {
        try
        {
            static ConfigManager instance; // Guaranteed to be destroyed, instantiated on first use
            return instance;
        }
        catch (std::runtime_error& e)
        {
            PLOG_ERROR << "Failed to parse Default Config: " << e.what() << '\n';
            MessageBoxA(nullptr, e.what(), "Error", MB_OK | MB_ICONERROR);
            ExitProcess(1);
        }
    }

    // Keyboard Scancodes
    SDL_Keycode GetP1UpKey() const { return config.keyboard.value().p1_up.value(); }
    SDL_Keycode GetP1DownKey() const { return config.keyboard.value().p1_down.value(); }
    SDL_Keycode GetP1LeftKey() const { return config.keyboard.value().p1_left.value(); }
    SDL_Keycode GetP1RightKey() const { return config.keyboard.value().p1_right.value(); }
    SDL_Keycode GetP1Button1Key() const { return config.keyboard.value().p1_button1.value(); }

    SDL_Keycode GetP2UpKey() const { return config.keyboard.value().p2_up.value(); }
    SDL_Keycode GetP2DownKey() const { return config.keyboard.value().p2_down.value(); }
    SDL_Keycode GetP2LeftKey() const { return config.keyboard.value().p2_left.value(); }
    SDL_Keycode GetP2RightKey() const { return config.keyboard.value().p2_right.value(); }
    SDL_Keycode GetP2Button1Key() const { return config.keyboard.value().p2_button1.value(); }

    SDL_Keycode GetService1Key() const { return config.keyboard.value().service1.value(); } // e.g., F1
    SDL_Keycode GetService2Key() const { return config.keyboard.value().service2.value(); }
    SDL_Keycode GetService3Key() const { return config.keyboard.value().service3.value(); }
    SDL_Keycode GetP2ServiceKey() const { return config.keyboard.value().p2_service.value(); } // e.g., F2
    SDL_Keycode GetTestKey() const { return config.keyboard.value().test.value(); } // e.g., F3 or from settings
    SDL_Keycode GetP1StartKey() const { return config.keyboard.value().p1_start.value(); } // e.g., 1
    SDL_Keycode GetP2StartKey() const { return config.keyboard.value().p2_start.value(); } // e.g., 2
    SDL_Keycode GetCardReadKey() const { return config.keyboard.value().card_read.value(); }

    SDL_GamepadButton GetP1UpButton() const { return config.gamepad.value().p1_dpad_up.value(); }
    // e.g., SDL_GAMEPAD_BUTTON_DPAD_UP
    SDL_GamepadButton GetP1DownButton() const { return config.gamepad.value().p1_dpad_down.value(); }
    // e.g., SDL_GAMEPAD_BUTTON_DPAD_DOWN
    SDL_GamepadButton GetP1LeftButton() const { return config.gamepad.value().p1_dpad_left.value(); }
    // e.g., SDL_GAMEPAD_BUTTON_DPAD_LEFT
    SDL_GamepadButton GetP1RightButton() const { return config.gamepad.value().p1_dpad_right.value(); }
    // e.g., SDL_GAMEPAD_BUTTON_DPAD_RIGHT
    SDL_GamepadButton GetP1Button1Button() const { return config.gamepad.value().p1_button1.value(); }
    // e.g., SDL_GAMEPAD_BUTTON_A

    SDL_GamepadButton GetP2UpButton() const { return config.gamepad.value().p2_button_up.value(); }
    // e.g., SDL_GAMEPAD_BUTTON_INVALID
    SDL_GamepadButton GetP2DownButton() const { return config.gamepad.value().p2_button_down.value(); }
    // e.g., SDL_GAMEPAD_BUTTON_INVALID
    SDL_GamepadButton GetP2LeftButton() const { return config.gamepad.value().p2_button_left.value(); }
    // e.g., SDL_GAMEPAD_BUTTON_INVALID
    SDL_GamepadButton GetP2RightButton() const { return config.gamepad.value().p2_button_right.value(); }
    // e.g., SDL_GAMEPAD_BUTTON_INVALID
    SDL_GamepadButton GetP2Button1Button() const { return config.gamepad.value().p2_button1.value(); }
    // e.g., SDL_GAMEPAD_BUTTON_B

    // Could add Start/Service buttons for controller too if desired

    SDL_GamepadAxis GetP1HorizontalAxis() const { return config.gamepad.value().p1_axis_horizontal.value(); }
    // e.g., SDL_GAMEPAD_AXIS_LEFTX
    SDL_GamepadAxis GetP1VerticalAxis() const { return config.gamepad.value().p1_axis_vertical.value(); }
    // e.g., SDL_GAMEPAD_AXIS_LEFTY
    SDL_GamepadAxis GetP2HorizontalAxis() const { return config.gamepad.value().p2_axis_horizontal.value(); }
    // e.g., SDL_GAMEPAD_AXIS_RIGHTX or LEFTX if P2 uses same stick
    SDL_GamepadAxis GetP2VerticalAxis() const { return config.gamepad.value().p2_axis_vertical.value(); }
    // e.g., SDL_GAMEPAD_AXIS_RIGHTY or LEFTY

    Sint16 GetGamepadAxisThreshold() const { return config.axis_threshold.value(); } // e.g., 16384 or 24000
    int GetGamepadIndex() const { return config.gamepad_index.value(); } // e.g., 0 or 1
    InputMode GetInputMode() const { return config.input_mode.value(); }
    bool GetEnable120FpsTimerPatches() const { return config.experimental.value().enable_120fps_timer_patches.value(); }
    bool GetEnableTestModeStorageRedirect() const { return config.experimental.value().enable_testmode_storage_redirect.value(); }
    bool GetEnableTimerFreezePatches() const { return config.experimental.value().enable_timer_freeze_patches.value(); }

    ConfigManager(const ConfigManager&) = delete;
    ConfigManager& operator=(const ConfigManager&) = delete;

private:
    ConfigManager();
    ~ConfigManager() = default;

    InputConfig config;
};

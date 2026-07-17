#pragma once

#include <SDL3/SDL.h>
#include <rfl.hpp>
#include <rfl/parsing/Parser.hpp> // Or specific parser headers if needed
#include <algorithm>
#include <cctype>
#include <string>
#include <unordered_map>

inline bool IsPrintableAsciiKey(SDL_Keycode keycode) {
    return keycode >= 0x21 && keycode <= 0x7e;
}

inline std::string KeycodeToString(SDL_Keycode keycode) {
    static const std::unordered_map<SDL_Keycode, std::string> keycode_map = {
        {SDLK_UNKNOWN, "unknown"},
        {SDLK_A, "a"}, {SDLK_B, "b"}, {SDLK_C, "c"}, {SDLK_D, "d"}, {SDLK_E, "e"}, {SDLK_F, "f"}, {SDLK_G, "g"},
        {SDLK_H, "h"}, {SDLK_I, "i"}, {SDLK_J, "j"}, {SDLK_K, "k"}, {SDLK_L, "l"}, {SDLK_M, "m"}, {SDLK_N, "n"},
        {SDLK_O, "o"}, {SDLK_P, "p"}, {SDLK_Q, "q"}, {SDLK_R, "r"}, {SDLK_S, "s"}, {SDLK_T, "t"}, {SDLK_U, "u"},
        {SDLK_V, "v"}, {SDLK_W, "w"}, {SDLK_X, "x"}, {SDLK_Y, "y"}, {SDLK_Z, "z"},
        {SDLK_0, "0"}, {SDLK_1, "1"}, {SDLK_2, "2"}, {SDLK_3, "3"}, {SDLK_4, "4"},
        {SDLK_5, "5"}, {SDLK_6, "6"}, {SDLK_7, "7"}, {SDLK_8, "8"}, {SDLK_9, "9"},
        {SDLK_F1, "f1"}, {SDLK_F2, "f2"}, {SDLK_F3, "f3"}, {SDLK_F4, "f4"}, {SDLK_F5, "f5"},
        {SDLK_F6, "f6"}, {SDLK_F7, "f7"}, {SDLK_F8, "f8"}, {SDLK_F9, "f9"}, {SDLK_F10, "f10"},
        {SDLK_F11, "f11"}, {SDLK_F12, "f12"},
        {SDLK_UP, "up"}, {SDLK_DOWN, "down"}, {SDLK_LEFT, "left"}, {SDLK_RIGHT, "right"},
        {SDLK_SPACE, "space"}, {SDLK_RETURN, "return"}, {SDLK_ESCAPE, "escape"},
        {SDLK_LCTRL, "lctrl"}, {SDLK_LSHIFT, "lshift"}, {SDLK_LALT, "lalt"},
        {SDLK_RCTRL, "rctrl"}, {SDLK_RSHIFT, "rshift"}, {SDLK_RALT, "ralt"},
        {SDLK_TAB, "tab"}, {SDLK_BACKSPACE, "backspace"}, {SDLK_DELETE, "delete"},
        {SDLK_HOME, "home"}, {SDLK_END, "end"}, {SDLK_PAGEUP, "pageup"}, {SDLK_PAGEDOWN, "pagedown"},
        {SDLK_INSERT, "insert"}, {SDLK_PRINTSCREEN, "printscreen"}, {SDLK_PAUSE, "pause"}
    };

    auto it = keycode_map.find(keycode);
    if (it != keycode_map.end()) {
        return it->second;
    }
    if (IsPrintableAsciiKey(keycode)) {
        return std::string(1, static_cast<char>(keycode));
    }
    return "unknown";
}

inline std::string GamepadButtonToString(SDL_GamepadButton button) {
    static const std::unordered_map<SDL_GamepadButton, std::string> button_map = {
        {SDL_GAMEPAD_BUTTON_INVALID, "invalid"},
        {SDL_GAMEPAD_BUTTON_SOUTH, "south"},
        {SDL_GAMEPAD_BUTTON_EAST, "east"},
        {SDL_GAMEPAD_BUTTON_WEST, "west"},
        {SDL_GAMEPAD_BUTTON_NORTH, "north"},
        {SDL_GAMEPAD_BUTTON_BACK, "back"},
        {SDL_GAMEPAD_BUTTON_GUIDE, "guide"},
        {SDL_GAMEPAD_BUTTON_START, "start"},
        {SDL_GAMEPAD_BUTTON_LEFT_STICK, "left_stick"},
        {SDL_GAMEPAD_BUTTON_RIGHT_STICK, "right_stick"},
        {SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, "left_shoulder"},
        {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, "right_shoulder"},
        {SDL_GAMEPAD_BUTTON_DPAD_UP, "dpad_up"},
        {SDL_GAMEPAD_BUTTON_DPAD_DOWN, "dpad_down"},
        {SDL_GAMEPAD_BUTTON_DPAD_LEFT, "dpad_left"},
        {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, "dpad_right"},
        {SDL_GAMEPAD_BUTTON_MISC1, "misc1"},
        {SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1, "paddle1"},
        {SDL_GAMEPAD_BUTTON_LEFT_PADDLE1, "paddle2"},
        {SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2, "paddle3"},
        {SDL_GAMEPAD_BUTTON_LEFT_PADDLE2, "paddle4"},
        {SDL_GAMEPAD_BUTTON_TOUCHPAD, "touchpad"}
    };

    auto it = button_map.find(button);
    return it != button_map.end() ? it->second : "invalid";
}

inline std::string GamepadAxisToString(SDL_GamepadAxis axis) {
    static const std::unordered_map<SDL_GamepadAxis, std::string> axis_map = {
        {SDL_GAMEPAD_AXIS_INVALID, "invalid"},
        {SDL_GAMEPAD_AXIS_LEFTX, "leftx"},
        {SDL_GAMEPAD_AXIS_LEFTY, "lefty"},
        {SDL_GAMEPAD_AXIS_RIGHTX, "rightx"},
        {SDL_GAMEPAD_AXIS_RIGHTY, "righty"},
        {SDL_GAMEPAD_AXIS_LEFT_TRIGGER, "left_trigger"},
        {SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, "right_trigger"}
    };

    auto it = axis_map.find(axis);
    return it != axis_map.end() ? it->second : "invalid";
}


inline SDL_Keycode StringToKeycode(const std::string& str) {
    std::string lower_str = str;
    std::ranges::transform(lower_str, lower_str.begin(), ::tolower);

    if (lower_str == "unknown") return SDLK_UNKNOWN;
    if (lower_str == "a") return SDLK_A;
    if (lower_str == "b") return SDLK_B;
    if (lower_str == "c") return SDLK_C;
    if (lower_str == "d") return SDLK_D;
    if (lower_str == "e") return SDLK_E;
    if (lower_str == "f") return SDLK_F;
    if (lower_str == "g") return SDLK_G;
    if (lower_str == "h") return SDLK_H;
    if (lower_str == "i") return SDLK_I;
    if (lower_str == "j") return SDLK_J;
    if (lower_str == "k") return SDLK_K;
    if (lower_str == "l") return SDLK_L;
    if (lower_str == "m") return SDLK_M;
    if (lower_str == "n") return SDLK_N;
    if (lower_str == "o") return SDLK_O;
    if (lower_str == "p") return SDLK_P;
    if (lower_str == "q") return SDLK_Q;
    if (lower_str == "r") return SDLK_R;
    if (lower_str == "s") return SDLK_S;
    if (lower_str == "t") return SDLK_T;
    if (lower_str == "u") return SDLK_U;
    if (lower_str == "v") return SDLK_V;
    if (lower_str == "w") return SDLK_W;
    if (lower_str == "x") return SDLK_X;
    if (lower_str == "y") return SDLK_Y;
    if (lower_str == "z") return SDLK_Z;
    if (lower_str == "1") return SDLK_1;
    if (lower_str == "2") return SDLK_2;
    if (lower_str == "3") return SDLK_3;
    if (lower_str == "4") return SDLK_4;
    if (lower_str == "5") return SDLK_5;
    if (lower_str == "6") return SDLK_6;
    if (lower_str == "7") return SDLK_7;
    if (lower_str == "8") return SDLK_8;
    if (lower_str == "9") return SDLK_9;
    if (lower_str == "0") return SDLK_0;
    if (lower_str == "f1") return SDLK_F1;
    if (lower_str == "f2") return SDLK_F2;
    if (lower_str == "f3") return SDLK_F3;
    if (lower_str == "f4") return SDLK_F4;
    if (lower_str == "f5") return SDLK_F5;
    if (lower_str == "f6") return SDLK_F6;
    if (lower_str == "f7") return SDLK_F7;
    if (lower_str == "f8") return SDLK_F8;
    if (lower_str == "f9") return SDLK_F9;
    if (lower_str == "f10") return SDLK_F10;
    if (lower_str == "f11") return SDLK_F11;
    if (lower_str == "f12") return SDLK_F12;
    if (lower_str == "up") return SDLK_UP;
    if (lower_str == "down") return SDLK_DOWN;
    if (lower_str == "left") return SDLK_LEFT;
    if (lower_str == "right") return SDLK_RIGHT;
    if (lower_str == "space") return SDLK_SPACE;
    if (lower_str == "return") return SDLK_RETURN;
    if (lower_str == "escape") return SDLK_ESCAPE;
    if (lower_str == "lctrl") return SDLK_LCTRL;
    if (lower_str == "lshift") return SDLK_LSHIFT;
    if (lower_str == "lalt") return SDLK_LALT;
    if (lower_str == "rctrl") return SDLK_RCTRL;
    if (lower_str == "rshift") return SDLK_RSHIFT;
    if (lower_str == "ralt") return SDLK_RALT;
    if (lower_str == "tab") return SDLK_TAB;
    if (lower_str == "backspace") return SDLK_BACKSPACE;
    if (lower_str == "delete") return SDLK_DELETE;
    if (lower_str == "home") return SDLK_HOME;
    if (lower_str == "end") return SDLK_END;
    if (lower_str == "pageup") return SDLK_PAGEUP;
    if (lower_str == "pagedown") return SDLK_PAGEDOWN;
    if (lower_str == "insert") return SDLK_INSERT;
    if (lower_str == "printscreen") return SDLK_PRINTSCREEN;
    if (lower_str == "pause") return SDLK_PAUSE;
    if (lower_str.size() == 1) {
        const auto character = static_cast<unsigned char>(lower_str.front());
        if (character >= 0x21 && character <= 0x7e) {
            return static_cast<SDL_Keycode>(character);
        }
    }

    return SDLK_UNKNOWN; // Default fallback
}

// Helper function to convert string to SDL_GamepadButton
inline SDL_GamepadButton StringToGamepadButton(const std::string& str) {
    std::string lower_str = str;
    std::ranges::transform(lower_str, lower_str.begin(), ::tolower);

    if (lower_str == "invalid") return SDL_GAMEPAD_BUTTON_INVALID;
    if (lower_str == "south" || lower_str == "a") return SDL_GAMEPAD_BUTTON_SOUTH; // Cross-platform A/X
    if (lower_str == "east" || lower_str == "b") return SDL_GAMEPAD_BUTTON_EAST;   // Cross-platform B/O
    if (lower_str == "west" || lower_str == "x") return SDL_GAMEPAD_BUTTON_WEST;   // Cross-platform X/Square
    if (lower_str == "north" || lower_str == "y") return SDL_GAMEPAD_BUTTON_NORTH;  // Cross-platform Y/Triangle
    if (lower_str == "back") return SDL_GAMEPAD_BUTTON_BACK;
    if (lower_str == "guide") return SDL_GAMEPAD_BUTTON_GUIDE;
    if (lower_str == "start") return SDL_GAMEPAD_BUTTON_START;
    if (lower_str == "left_stick") return SDL_GAMEPAD_BUTTON_LEFT_STICK;
    if (lower_str == "right_stick") return SDL_GAMEPAD_BUTTON_RIGHT_STICK;
    if (lower_str == "left_shoulder") return SDL_GAMEPAD_BUTTON_LEFT_SHOULDER;
    if (lower_str == "right_shoulder") return SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER;
    if (lower_str == "dpad_up") return SDL_GAMEPAD_BUTTON_DPAD_UP;
    if (lower_str == "dpad_down") return SDL_GAMEPAD_BUTTON_DPAD_DOWN;
    if (lower_str == "dpad_left") return SDL_GAMEPAD_BUTTON_DPAD_LEFT;
    if (lower_str == "dpad_right") return SDL_GAMEPAD_BUTTON_DPAD_RIGHT;
    if (lower_str == "misc1") return SDL_GAMEPAD_BUTTON_MISC1; // Usually Xbox Series X share button, PS5 mic button, Nintendo Switch Pro capture button
    if (lower_str == "paddle1") return SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1; // Xbox Elite paddle P1/P3
    if (lower_str == "paddle2") return SDL_GAMEPAD_BUTTON_LEFT_PADDLE1;  // Xbox Elite paddle P2/P4
    if (lower_str == "paddle3") return SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2; // Xbox Elite paddle P3/P1
    if (lower_str == "paddle4") return SDL_GAMEPAD_BUTTON_LEFT_PADDLE2;  // Xbox Elite paddle P4/P2
    if (lower_str == "touchpad") return SDL_GAMEPAD_BUTTON_TOUCHPAD; // PS4/PS5 touchpad button
    // Consider SDL_GetGamepadButtonFromString if SDL is initialized

    return SDL_GAMEPAD_BUTTON_INVALID; // Default fallback
}

// Helper function to convert string to SDL_GamepadAxis
inline SDL_GamepadAxis StringToGamepadAxis(const std::string& str) {
    std::string lower_str = str;
    std::ranges::transform(lower_str, lower_str.begin(), ::tolower);

    if (lower_str == "invalid") return SDL_GAMEPAD_AXIS_INVALID;
    if (lower_str == "leftx") return SDL_GAMEPAD_AXIS_LEFTX;
    if (lower_str == "lefty") return SDL_GAMEPAD_AXIS_LEFTY;
    if (lower_str == "rightx") return SDL_GAMEPAD_AXIS_RIGHTX;
    if (lower_str == "righty") return SDL_GAMEPAD_AXIS_RIGHTY;
    if (lower_str == "left_trigger") return SDL_GAMEPAD_AXIS_LEFT_TRIGGER;
    if (lower_str == "right_trigger") return SDL_GAMEPAD_AXIS_RIGHT_TRIGGER;
    // Consider SDL_GetGamepadAxisFromString if SDL is initialized

    return SDL_GAMEPAD_AXIS_INVALID; // Default fallback
}

// reflect-cpp 0.24+ handles primitive types before consulting custom
// reflectors. SDL_Keycode is a Uint32 alias, so give persisted key bindings a
// distinct type while preserving transparent SDL_Keycode access at runtime.
struct SdlKeycodeConfigValue {
    SDL_Keycode value = SDLK_UNKNOWN;

    constexpr SdlKeycodeConfigValue() noexcept = default;
    constexpr SdlKeycodeConfigValue(SDL_Keycode keycode) noexcept
        : value(keycode) {}

    constexpr SdlKeycodeConfigValue& operator=(SDL_Keycode keycode) noexcept {
        value = keycode;
        return *this;
    }

    constexpr operator SDL_Keycode&() noexcept {
        return value;
    }

    constexpr operator const SDL_Keycode&() const noexcept {
        return value;
    }
};


namespace rfl {

    template <>
    struct Reflector<SdlKeycodeConfigValue> {
        // Tell rfl to represent persisted keycodes as strings.
        using ReflType = std::string;

        static SdlKeycodeConfigValue to(const ReflType& str) noexcept {
            return {StringToKeycode(str)};
        }
        
        static ReflType from(const SdlKeycodeConfigValue& key) {
            return KeycodeToString(key.value);
        }
        
    };

    template <>
    struct Reflector<SDL_GamepadButton> {
        using ReflType = std::string;

        static SDL_GamepadButton to(const ReflType& str) noexcept {
            return StringToGamepadButton(str);
        }


        static ReflType from(const SDL_GamepadButton& button) {
            return GamepadButtonToString(button);
        }
        
    };

    template <>
    struct Reflector<SDL_GamepadAxis> {
        using ReflType = std::string;

        static SDL_GamepadAxis to(const ReflType& str) noexcept {
            return StringToGamepadAxis(str);
        }
        
        static ReflType from(const SDL_GamepadAxis& axis) {
            return GamepadAxisToString(axis);
        }
        
    };

} // namespace rfl

#pragma once

#include <Windows.h>
#include <SDL3/SDL.h>

inline int SdlKeycodeToVirtualKey(SDL_Keycode key) noexcept {
    switch (key) {
    case SDLK_UNKNOWN:
        return 0;
    case SDLK_A: return 'A';
    case SDLK_B: return 'B';
    case SDLK_C: return 'C';
    case SDLK_D: return 'D';
    case SDLK_E: return 'E';
    case SDLK_F: return 'F';
    case SDLK_G: return 'G';
    case SDLK_H: return 'H';
    case SDLK_I: return 'I';
    case SDLK_J: return 'J';
    case SDLK_K: return 'K';
    case SDLK_L: return 'L';
    case SDLK_M: return 'M';
    case SDLK_N: return 'N';
    case SDLK_O: return 'O';
    case SDLK_P: return 'P';
    case SDLK_Q: return 'Q';
    case SDLK_R: return 'R';
    case SDLK_S: return 'S';
    case SDLK_T: return 'T';
    case SDLK_U: return 'U';
    case SDLK_V: return 'V';
    case SDLK_W: return 'W';
    case SDLK_X: return 'X';
    case SDLK_Y: return 'Y';
    case SDLK_Z: return 'Z';
    case SDLK_0: return '0';
    case SDLK_1: return '1';
    case SDLK_2: return '2';
    case SDLK_3: return '3';
    case SDLK_4: return '4';
    case SDLK_5: return '5';
    case SDLK_6: return '6';
    case SDLK_7: return '7';
    case SDLK_8: return '8';
    case SDLK_9: return '9';
    case SDLK_F1: return VK_F1;
    case SDLK_F2: return VK_F2;
    case SDLK_F3: return VK_F3;
    case SDLK_F4: return VK_F4;
    case SDLK_F5: return VK_F5;
    case SDLK_F6: return VK_F6;
    case SDLK_F7: return VK_F7;
    case SDLK_F8: return VK_F8;
    case SDLK_F9: return VK_F9;
    case SDLK_F10: return VK_F10;
    case SDLK_F11: return VK_F11;
    case SDLK_F12: return VK_F12;
    case SDLK_UP: return VK_UP;
    case SDLK_DOWN: return VK_DOWN;
    case SDLK_LEFT: return VK_LEFT;
    case SDLK_RIGHT: return VK_RIGHT;
    case SDLK_SPACE: return VK_SPACE;
    case SDLK_RETURN: return VK_RETURN;
    case SDLK_ESCAPE: return VK_ESCAPE;
    case SDLK_LCTRL: return VK_LCONTROL;
    case SDLK_LSHIFT: return VK_LSHIFT;
    case SDLK_LALT: return VK_LMENU;
    case SDLK_RCTRL: return VK_RCONTROL;
    case SDLK_RSHIFT: return VK_RSHIFT;
    case SDLK_RALT: return VK_RMENU;
    case SDLK_TAB: return VK_TAB;
    case SDLK_BACKSPACE: return VK_BACK;
    case SDLK_DELETE: return VK_DELETE;
    case SDLK_HOME: return VK_HOME;
    case SDLK_END: return VK_END;
    case SDLK_PAGEUP: return VK_PRIOR;
    case SDLK_PAGEDOWN: return VK_NEXT;
    case SDLK_INSERT: return VK_INSERT;
    case SDLK_PRINTSCREEN: return VK_SNAPSHOT;
    case SDLK_PAUSE: return VK_PAUSE;
    default:
        return 0;
    }
}

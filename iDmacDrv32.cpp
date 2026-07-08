#include <windows.h>
#include "RegisterOpTypes.h"
#include "plog/Log.h"
#include "plog/Initializers/RollingFileInitializer.h"
#include <format>
#include <cstdint>

#include "SDL3/SDL.h"
#define SDL_MAIN_NOIMPL
#define SDL_MAIN_HANDLED
#include "SDL3/SDL_main.h"
#include "InputManager.h"

inline bool inited = false;
inline SDL_Window *window;

namespace {

InputManager& GetInputManager()
{
    static InputManager manager;
    return manager;
}

const char* register_read_name(DWORD command)
{
    switch (static_cast<RegisterReadType>(command))
    {
    case RegisterReadType::DMAC_ID:
        return "DMAC_ID";
    case RegisterReadType::FIO_NODE_0_STATUS:
        return "FIO_NODE_0_STATUS";
    case RegisterReadType::FIO_NODE_1_STATUS:
        return "FIO_NODE_1_STATUS";
    case RegisterReadType::FIO_NODE_0_INPUT:
        return "FIO_NODE_0_INPUT";
    case RegisterReadType::FIO_NODE0_ANALOG1:
        return "FIO_NODE0_ANALOG1";
    case RegisterReadType::FIO_NODE0_ANALOG2:
        return "FIO_NODE0_ANALOG2";
    case RegisterReadType::FIO_NODE0_ANALOG3:
        return "FIO_NODE0_ANALOG3";
    case RegisterReadType::FIO_NODE_1_INPUT:
        return "FIO_NODE_1_INPUT";
    case RegisterReadType::FIO_NODE1_ANALOG1:
        return "FIO_NODE1_ANALOG1";
    case RegisterReadType::FIO_NODE1_ANALOG2:
        return "FIO_NODE1_ANALOG2";
    case RegisterReadType::FIO_NODE1_ANALOG3:
        return "FIO_NODE1_ANALOG3";
    case RegisterReadType::FIO_NODE_0_COINSLOT_1:
        return "FIO_NODE_0_COINSLOT_1";
    case RegisterReadType::FIO_NODE_0_COINSLOT_2:
        return "FIO_NODE_0_COINSLOT_2";
    case RegisterReadType::FIO_NODE_1_COINSLOT_1:
        return "FIO_NODE_1_COINSLOT_1";
    case RegisterReadType::FIO_NODE_1_COINSLOT_2:
        return "FIO_NODE_1_COINSLOT_2";
    case RegisterReadType::FIO_HUB_PORT_1:
        return "FIO_HUB_PORT_1";
    default:
        return "UNKNOWN";
    }
}

}

extern "C" __declspec(dllexport) DWORD __cdecl iDmacDrvOpen(int deviceId, LPVOID outBuffer, LPVOID lpSomeFlag) {
    PLOG_VERBOSE << "iDmacDrvOpen" << std::endl;
    *static_cast<DWORD*>(outBuffer) = 284;
    *static_cast<DWORD*>(lpSomeFlag) = 0;

    if (!inited)
    {
        auto hwnd = FindWindowA("GameWare", "GameWare");
        if (hwnd == nullptr)
        {
            PLOG_ERROR << "FindWindowA failed" << std::endl;
            MessageBoxA(nullptr, "FindWindowA failed!", "Error", MB_OK | MB_ICONERROR);
            ExitProcess(1);
        }
        SDL_SetMainReady ();
        SDL_SetHint (SDL_HINT_JOYSTICK_HIDAPI_PS4, "1");
        SDL_SetHint (SDL_HINT_JOYSTICK_ENHANCED_REPORTS, "1");
        SDL_SetHint (SDL_HINT_JOYSTICK_HIDAPI_PS5, "1");
        auto success = SDL_Init (SDL_INIT_JOYSTICK  | SDL_INIT_GAMEPAD | SDL_INIT_EVENTS | SDL_INIT_VIDEO );
        if (!success)
        {
            MessageBoxA(nullptr, ("SDL init failed!" + std::string(SDL_GetError())).c_str(), "Error", MB_OK | MB_ICONERROR);
        }
        PLOG_DEBUG << "SDL init complete!" << std::endl;
        auto configPath = std::filesystem::current_path () / "gamecontrollerdb.txt";
        if (exists(configPath)) {
            SDL_AddGamepadMappingsFromFile (configPath.string ().c_str ());
        }

        SDL_SetGamepadEventsEnabled (true);
        SDL_SetJoystickEventsEnabled (true);
        PLOG_DEBUG << "SDL events set up!" << std::endl;
        SDL_PropertiesID props = SDL_CreateProperties();
        if(props == 0) {
            PLOG_ERROR << "SDL_CreateProperties failed" << std::endl;
            MessageBoxA(nullptr, "SDL_CreateProperties failed!", "Error", MB_OK | MB_ICONERROR);
            ExitProcess(1);
        }
        SDL_SetPointerProperty (props, SDL_PROP_WINDOW_CREATE_WIN32_HWND_POINTER, hwnd);
        window = SDL_CreateWindowWithProperties (props);
        inited = true;
    }
    return 0;
}

extern "C" __declspec(dllexport) DWORD __cdecl iDmacDrvClose(int deviceId, LPVOID lpWriteAccess) {
    PLOG_VERBOSE << "iDmacDrvClose" << std::endl;
    return 0;
}

extern "C" __declspec(dllexport) int __cdecl iDmacDrvDmaRead(int a1, LPVOID lp, UINT_PTR ucb, LPVOID a4) {
    PLOG_VERBOSE << "iDmacDrvDmaRead" << std::endl;
    return 0;
}

extern "C" __declspec(dllexport) int __cdecl iDmacDrvDmaWrite(int a1, void* lp, UINT_PTR ucb, LPVOID a4) {
    PLOG_VERBOSE << "iDmacDrvDmaWrite" << std::endl;
    return 0;
}

extern "C" __declspec(dllexport) int __cdecl iDmacDrvRegisterRead(int DeviceId, DWORD CommandCode, LPVOID OutBuffer, LPVOID DeviceResult) {
    PLOG_VERBOSE << std::format("iDmacDrvRegisterRead, command {:#x}", CommandCode) << std::endl;
    static uint64_t total_reads = 0;
    static uint64_t node0_input_reads = 0;
    static uint64_t node0_nonzero_reads = 0;
    static uint64_t sdl_events = 0;
    static uint64_t unknown_reads = 0;
    static DWORD last_node0_input = 0;

    ++total_reads;
    auto readType = static_cast<RegisterReadType>(CommandCode);
    DWORD result;
    SDL_Event event;
    InputManager& inputManager = GetInputManager();
    if (SDL_PollEvent (&event) != false)
    {
        ++sdl_events;
        PLOG_INFO << "GC120FPS_INPUT: idmac SDL_PollEvent hit before command="
                  << register_read_name(CommandCode)
                  << " total_events=" << sdl_events;
        inputManager.HandleEvent(event);
    }
    switch (readType)
    {
    case RegisterReadType::DMAC_ID:
        result = 0x00010201;
        break;
    case RegisterReadType::FIO_NODE_0_STATUS:
        result = 0x00FF00FF;
        break;
    case RegisterReadType::FIO_NODE_1_STATUS:
        result = 0x00FF0000;
        break;
    case RegisterReadType::FIO_NODE_0_INPUT:
        ++node0_input_reads;
        result = inputManager.GetInput();
        if (result != 0)
        {
            ++node0_nonzero_reads;
        }
        break;
    case RegisterReadType::FIO_NODE0_ANALOG1:
        result = 0;
        break;
    // Seems to be volume for GC
    case RegisterReadType::FIO_NODE0_ANALOG2:
        result = 0xFF;
        break;
    case RegisterReadType::FIO_NODE0_ANALOG3:
        result = 0;
        break;
    case RegisterReadType::FIO_NODE_1_INPUT:
        result = 0;
        break;
    case RegisterReadType::FIO_NODE1_ANALOG1:
        result = 0;
        break;
    case RegisterReadType::FIO_NODE1_ANALOG2:
        result = 0;
        break;
    case RegisterReadType::FIO_NODE1_ANALOG3:
        result = 0;
        break;
    case RegisterReadType::FIO_NODE_0_COINSLOT_1:
        result = 0;
        break;
    case RegisterReadType::FIO_NODE_0_COINSLOT_2:
        result = 0;
        break;
    case RegisterReadType::FIO_NODE_1_COINSLOT_1:
        result = 0;
        break;
    case RegisterReadType::FIO_NODE_1_COINSLOT_2:
        result = 0;
        break;
    case RegisterReadType::FIO_HUB_PORT_1:
        result = 0x0001823C;
        break;
    case RegisterReadType::FIO_HUB_PORT_2:
    case RegisterReadType::FIO_HUB_PORT_3:
    case RegisterReadType::FIO_HUB_PORT_4:
    case RegisterReadType::FIO_GC_UNKNOWN_1:
    case RegisterReadType::FIO_GC_UNKNOWN_2:
    case RegisterReadType::FIO_GC_UNKNOWN_3:
    case RegisterReadType::FIO_GC_UNKNOWN_4:
    case RegisterReadType::FIO_GC_UNKNOWN_5:
    case RegisterReadType::FIO_GC_UNKNOWN_6:
    case RegisterReadType::FIO_GC_UNKNOWN_7:
    case RegisterReadType::FIO_GC_UNKNOWN_8:
        result = 0;
        break;
    default:
        ++unknown_reads;
        PLOG_DEBUG << std::format("Unknown command for iDmacDrvRegisterRead: {:#x}", CommandCode) << std::endl;
        result = 0;
    }
    if (readType == RegisterReadType::FIO_NODE_0_INPUT
        && (result != last_node0_input || result != 0 || (node0_input_reads % 300) == 0))
    {
        PLOG_INFO << std::format(
            "GC120FPS_INPUT: FIO_NODE_0_INPUT read#{} result={:#010x} changed={} nonzero_reads={} total_reads={} sdl_events={}",
            node0_input_reads,
            result,
            result != last_node0_input,
            node0_nonzero_reads,
            total_reads,
            sdl_events);
        last_node0_input = result;
    }
    if ((total_reads % 2000) == 0)
    {
        PLOG_INFO << std::format(
            "GC120FPS_INPUT: idmac summary total_reads={} node0_input_reads={} node0_nonzero_reads={} sdl_events={} unknown_reads={}",
            total_reads,
            node0_input_reads,
            node0_nonzero_reads,
            sdl_events,
            unknown_reads);
    }
    *static_cast<DWORD*>(OutBuffer) = result;
    *static_cast<DWORD*>(DeviceResult) = 0;
    return 0;
}

extern "C" __declspec(dllexport) int __cdecl iDmacDrvRegisterWrite(int deviceId, DWORD CommandCode, int unused, LPVOID DeviceResult) {
    PLOG_VERBOSE << std::format("iDmacDrvRegisterWrite, command {:#x}", CommandCode) << std::endl;
    return 0;
}

extern "C" __declspec(dllexport) int __cdecl iDmacDrvRegisterBufferRead(int a1, DWORD BytesReturned, LPVOID lp, UINT_PTR ucb, LPVOID a5) {
    PLOG_VERBOSE << "iDmacDrvRegisterBufferRead" << std::endl;
    return 0;
}

extern "C" __declspec(dllexport) int __cdecl iDmacDrvRegisterBufferWrite(int a1, DWORD BytesReturned, void* lp, UINT_PTR ucb, LPVOID a5) {
    PLOG_VERBOSE << "iDmacDrvRegisterBufferWrite" << std::endl;
    return 0;
}

extern "C" __declspec(dllexport) int __cdecl iDmacDrvMemoryRead(int a1, DWORD BytesReturned, LPVOID lp, LPVOID a4) {
    PLOG_VERBOSE << "iDmacDrvMemoryRead" << std::endl;
    return 0;
}

extern "C" __declspec(dllexport) int __cdecl iDmacDrvMemoryWrite(int a1, DWORD BytesReturned, int a3, LPVOID lp) {
    PLOG_VERBOSE << "iDmacDrvMemoryWrite" << std::endl;
    return 0;
}

extern "C" __declspec(dllexport) int __cdecl iDmacDrvMemoryBufferRead(int a1, DWORD BytesReturned, LPVOID lp, UINT_PTR ucb, LPVOID a5) {
    PLOG_VERBOSE << std::format("iDmacDrvMemoryBufferRead, a1: {:#x},a2: {:#x},a3: {:#x},a4: {:#x},a5: {:#x}",
                                a1, BytesReturned, reinterpret_cast<DWORD>(lp), ucb, reinterpret_cast<DWORD>(a5)) << std::endl;
    return 0;
}

extern "C" __declspec(dllexport) int __cdecl iDmacDrvMemoryBufferWrite(int a1, int a2, void* lp, UINT_PTR ucb, LPVOID a5) {
    PLOG_VERBOSE << std::format("iDmacDrvMemoryBufferWrite, a1: {:#x},a2: {:#x},a3: {:#x},a4: {:#x},a5: {:#x}",
                                a1, a2, reinterpret_cast<DWORD>(lp), ucb, reinterpret_cast<DWORD>(a5)) << std::endl;
    return 0;
}

extern "C" __declspec(dllexport) int __cdecl iDmacDrvMemoryReadExt(int a1, DWORD BytesReturned, int a3, LPVOID lp, DWORD nOutBufferSize, LPVOID a6) {
    PLOG_VERBOSE << "iDmacDrvMemoryReadExt" << std::endl;
    return 0;
}

extern "C" __declspec(dllexport) int __cdecl iDmacDrvMemoryWriteExt(int a1, int a2, int a3, void* Src, rsize_t DstSize, LPVOID lp) {
    PLOG_VERBOSE << "iDmacDrvMemoryWriteExt" << std::endl;
    return 0;
}

extern "C" __declspec(dllexport) int __cdecl iDmacDrvProgramDownload() {
    return 0;
}

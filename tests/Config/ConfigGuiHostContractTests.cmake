if(NOT DEFINED CONFIG_GUI_HOST_SOURCE)
    message(FATAL_ERROR "CONFIG_GUI_HOST_SOURCE is required")
endif()

if(NOT EXISTS "${CONFIG_GUI_HOST_SOURCE}")
    message(FATAL_ERROR "ConfigGUI host source does not exist: ${CONFIG_GUI_HOST_SOURCE}")
endif()

file(READ "${CONFIG_GUI_HOST_SOURCE}" host_source)

set(required_tokens
        "D3D11CreateDeviceAndSwapChain"
        "ImGui_ImplWin32_Init"
        "ImGui_ImplDX11_Init"
        "ImGui_ImplWin32_NewFrame"
        "ImGui_ImplDX11_NewFrame"
        "ImGui_ImplDX11_RenderDrawData"
        "case WM_SIZE"
        "CreateRenderTarget"
        "CleanupRenderTarget"
        "ImGui_ImplDX11_Shutdown"
        "ImGui_ImplWin32_Shutdown")

foreach(required_token IN LISTS required_tokens)
    string(FIND "${host_source}" "${required_token}" token_position)
    if(token_position EQUAL -1)
        message(FATAL_ERROR
                "ConfigGUI host source is missing required token: ${required_token}")
    endif()
endforeach()

string(REGEX MATCH "SDL" forbidden_sdl_token "${host_source}")
if(forbidden_sdl_token)
    message(FATAL_ERROR "ConfigGUI native host source must not contain SDL")
endif()

add_library(gc_nesys_process STATIC NesysServiceProcess.cpp)
target_include_directories(gc_nesys_process PUBLIC
        ${PROJECT_SOURCE_DIR}/src)

add_library(gc_nesys_diagnostics STATIC
        Diagnostics/RequestPipelineDiagnostics.cpp
        Diagnostics/GamePipeWin32Observers.cpp)
target_include_directories(gc_nesys_diagnostics PUBLIC
        ${PROJECT_SOURCE_DIR}/src
        ${plog_SOURCE_DIR}/include
)

target_link_libraries(gc_nesys_diagnostics PUBLIC gc_hooking gc_win32_hooks)

add_library(gc_nesys STATIC
        NesysServicePatch.cpp
        ThreadPriorityOverride.cpp
        Launcher/NesysServiceLauncher.cpp
        Network/ServerAddressOverride.cpp
        Network/SyntheticNetworkAdapter.cpp
        Network/NesysPingProfile.cpp
        Registry/RegistryConfigOverride.cpp
)
target_include_directories(gc_nesys PUBLIC
        ${PROJECT_SOURCE_DIR}/src
        ${plog_SOURCE_DIR}/include
)
target_link_libraries(gc_nesys PUBLIC
        gc_game_version
        gc_hooking
        gc_nesys_process
        gc_nesys_diagnostics
        gc_nesys_network_config
        safetyhook::safetyhook
)
target_link_libraries(gc_nesys PRIVATE gc_platform_win32)

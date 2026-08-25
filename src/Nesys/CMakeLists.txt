add_library(gc_nesys_process STATIC NesysServiceProcess.cpp)
target_include_directories(gc_nesys_process PUBLIC
        ${PROJECT_SOURCE_DIR}/src)

add_library(gc_nesys_diagnostics STATIC
        Diagnostics/RequestPipelineDiagnostics.cpp)
target_include_directories(gc_nesys_diagnostics PUBLIC
        ${PROJECT_SOURCE_DIR}/src
        ${minhook_SOURCE_DIR}/include
        ${plog_SOURCE_DIR}/include
)

add_library(gc_nesys STATIC
        NesysHookTransaction.cpp
        NesysServicePatch.cpp
        ThreadPriorityOverride.cpp
        Launcher/NesysServiceLauncher.cpp
        Network/ServerAddressOverride.cpp
        Network/SyntheticNetworkAdapter.cpp
        Registry/RegistryConfigOverride.cpp
)
target_include_directories(gc_nesys PUBLIC
        ${PROJECT_SOURCE_DIR}/src
        ${minhook_SOURCE_DIR}/include
        ${plog_SOURCE_DIR}/include
)
target_link_libraries(gc_nesys PUBLIC
        gc_nesys_process
        gc_nesys_diagnostics
        gc_nesys_network_config
        minhook
        safetyhook::safetyhook
)

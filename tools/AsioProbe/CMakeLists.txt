add_executable(AsioProbe Main.cpp)
set_target_properties(AsioProbe PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${GC_DIST_DIR}"
        PDB_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
if(MSVC)
    target_link_options(AsioProbe PRIVATE
            "/ILK:${CMAKE_CURRENT_BINARY_DIR}/AsioProbe.ilk")
endif()
target_include_directories(AsioProbe PRIVATE
        ${PROJECT_SOURCE_DIR}/src
)
target_link_libraries(AsioProbe PRIVATE
        gc_asio
        ole32
        user32
)

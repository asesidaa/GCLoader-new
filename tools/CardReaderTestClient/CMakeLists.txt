add_library(gc_card_reader_test_client_transport STATIC
        CardReaderClient.cpp
)
target_include_directories(gc_card_reader_test_client_transport PUBLIC
        ${PROJECT_SOURCE_DIR}/src
        ${PROJECT_SOURCE_DIR}/tools
)

add_executable(CardReaderTestClient WIN32 Main.cpp)
target_include_directories(CardReaderTestClient PRIVATE
        ${PROJECT_SOURCE_DIR}/src
        ${PROJECT_SOURCE_DIR}/tools
)
target_link_libraries(CardReaderTestClient PRIVATE
        gc_card_reader_test_client_transport
        gdi32
        user32
)
set_target_properties(CardReaderTestClient PROPERTIES
        PDB_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
if(MSVC)
    target_link_options(CardReaderTestClient PRIVATE
            "/ILK:${CMAKE_CURRENT_BINARY_DIR}/CardReaderTestClient.ilk")
endif()

add_library(gc_card_reader_test_client_transport STATIC
        CardReaderClient.cpp
)
target_include_directories(gc_card_reader_test_client_transport PUBLIC
        ${PROJECT_SOURCE_DIR}/src
        ${PROJECT_SOURCE_DIR}/tools
)

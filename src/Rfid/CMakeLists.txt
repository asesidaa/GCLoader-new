add_library(gc_rfid_core STATIC
        CardData.cpp
        CardReaderInterface.cpp
        ComPortState.cpp
        Runtime.cpp
        State.cpp
        TaitoCommands.cpp
        Jvs/Decoder.cpp
        Jvs/Device.cpp
        Jvs/Encoder.cpp
)
target_include_directories(gc_rfid_core PUBLIC
        ${PROJECT_SOURCE_DIR}/src
        ${plog_SOURCE_DIR}/include
)
target_link_libraries(gc_rfid_core PUBLIC advapi32)

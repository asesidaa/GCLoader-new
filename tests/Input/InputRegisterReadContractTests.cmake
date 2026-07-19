if(NOT DEFINED IDMAC_DRIVER_SOURCE)
    message(FATAL_ERROR "IDMAC_DRIVER_SOURCE is required")
endif()

if(NOT EXISTS "${IDMAC_DRIVER_SOURCE}")
    message(FATAL_ERROR
            "iDmac driver source does not exist: ${IDMAC_DRIVER_SOURCE}")
endif()

file(READ "${IDMAC_DRIVER_SOURCE}" driver_source)

string(REGEX MATCH
        "case[ \t]+RegisterReadType::FIO_NODE_0_INPUT:[ \t\r\n]+result[ \t]*=[ \t]*gc::input::ReadPublishedInput\\(\\);[ \t\r\n]+break;"
        direct_snapshot_read
        "${driver_source}")

if(NOT direct_snapshot_read)
    message(FATAL_ERROR
            "FIO_NODE_0_INPUT must contain only one published snapshot load")
endif()

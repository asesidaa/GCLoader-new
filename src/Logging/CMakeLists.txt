add_library(gc_logging STATIC SessionLog.cpp)
target_include_directories(gc_logging PUBLIC
        ${PROJECT_SOURCE_DIR}/src
        ${plog_SOURCE_DIR}/include
)
target_link_libraries(gc_logging PUBLIC gc_nesys_process)

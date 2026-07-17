add_library(gc_hooking STATIC MinHookTransaction.cpp)
target_include_directories(gc_hooking PUBLIC
        ${PROJECT_SOURCE_DIR}/src
        ${minhook_SOURCE_DIR}/include
        ${plog_SOURCE_DIR}/include
)
target_link_libraries(gc_hooking PUBLIC minhook)

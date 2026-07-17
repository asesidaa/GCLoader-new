# CMake 4.2 can decode localized MSVC /showIncludes output twice on a
# Simplified Chinese host. Ninja then records zero header dependencies because
# its generated prefix is mojibake. Repair only that detected value; leave all
# other locales and generators untouched.
if(MSVC AND CMAKE_GENERATOR MATCHES "^Ninja" AND
        CMAKE_CL_SHOWINCLUDES_PREFIX MATCHES "^娉")
    set(CMAKE_C_CL_SHOWINCLUDES_PREFIX "注意: 包含文件:  ")
    set(CMAKE_CXX_CL_SHOWINCLUDES_PREFIX "注意: 包含文件:  ")
    set(CMAKE_CL_SHOWINCLUDES_PREFIX "注意: 包含文件:  ")
endif()

set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY
            "MultiThreaded$<$<CONFIG:Debug>:Debug>"
            CACHE INTERNAL "")
    add_compile_options(/W3 /utf-8)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

add_definitions(-DNOMINMAX)

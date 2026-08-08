cmake_minimum_required(VERSION 3.31)
project(AsioSdkFixture LANGUAGES CXX)

if(NOT DEFINED GC_PROJECT_SOURCE_DIR OR GC_PROJECT_SOURCE_DIR STREQUAL "")
    message(FATAL_ERROR "GC_PROJECT_SOURCE_DIR is required")
endif()

list(APPEND CMAKE_MODULE_PATH "${GC_PROJECT_SOURCE_DIR}/cmake")
include(AsioSdk)
gc_require_asio_sdk()

file(WRITE "${CMAKE_BINARY_DIR}/resolved-sdk.txt" "${GC_ASIO_SDK_DIR}")

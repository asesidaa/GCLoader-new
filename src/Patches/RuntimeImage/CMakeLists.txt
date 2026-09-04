add_library(gc_runtime_image STATIC RuntimeImage.cpp)
target_include_directories(gc_runtime_image PUBLIC ${PROJECT_SOURCE_DIR}/src)

add_library(gc_game_version STATIC
    ImageIdentity.cpp
    KnownImages.cpp
    BuildDetector.cpp
    VersionedPlan.cpp
    VersionedPlanDiagnostics.cpp
)
target_include_directories(gc_game_version PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(gc_game_version PUBLIC gc_runtime_image PRIVATE bcrypt)

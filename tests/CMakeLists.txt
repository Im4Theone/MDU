add_executable(mdu_tests
    test_main.cpp
    base64_tests.cpp
    bridge_tests.cpp
    config_tests.cpp
    crash_guard_tests.cpp
    dock_layout_tests.cpp
    theme_tests.cpp
    wallpaper_tests.cpp
)
target_link_libraries(mdu_tests PRIVATE mdu_core)
target_compile_definitions(mdu_tests PRIVATE MDU_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
mdu_set_warnings(mdu_tests)

add_test(NAME mdu_tests COMMAND mdu_tests)

cmake_minimum_required(VERSION 3.26)

project(MDU
    VERSION 0.1.0
    DESCRIPTION "Miku Desktop Unit - a desktop dock and theming layer for Windows 11"
    HOMEPAGE_URL "https://github.com/im4theone/mdu"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Static MSVC runtime for every target, so mdu.exe needs no Visual C++
# redistributable. It must be project-wide: mixing runtimes fails to link.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# The application itself only builds on Windows. The platform-independent core
# (configuration, layout, theme parsing, message validation, wallpaper
# adaptation) also builds elsewhere so its tests can run anywhere.
if(WIN32)
    set(MDU_DEFAULT_BUILD_APP ON)
else()
    set(MDU_DEFAULT_BUILD_APP OFF)
endif()
option(MDU_BUILD_APP "Build the Windows application" ${MDU_DEFAULT_BUILD_APP})
option(MDU_BUILD_TESTS "Build unit tests" ON)
option(MDU_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

include(cmake/Dependencies.cmake)
include(cmake/CompilerWarnings.cmake)

# --- Portable core -----------------------------------------------------------

add_library(mdu_core STATIC
    src/app/crash_guard.cpp
    src/dock/dock_layout.cpp
    src/settings/config.cpp
    src/ui/bridge.cpp
    src/ui/settings_protocol.cpp
    src/ui/theme.cpp
    src/utilities/base64.cpp
    src/utilities/file_io.cpp
    src/utilities/log.cpp
    src/wallpaper/adapt.cpp
)
target_include_directories(mdu_core PUBLIC src)
target_link_libraries(mdu_core PUBLIC nlohmann_json::nlohmann_json)
mdu_set_warnings(mdu_core)
if(WIN32)
    target_compile_definitions(mdu_core PUBLIC UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

# --- Windows application -----------------------------------------------------

if(MDU_BUILD_APP)
    if(NOT WIN32)
        message(FATAL_ERROR "MDU_BUILD_APP requires Windows")
    endif()
    include(cmake/App.cmake)
endif()

# --- Tests -------------------------------------------------------------------

if(MDU_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

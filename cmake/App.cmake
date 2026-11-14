# The Windows application: native host, frontend and bundled themes.

add_executable(mdu WIN32
    src/main.cpp
    src/app/app.cpp
    src/app/app_self_test.cpp
    src/app/commands.cpp
    src/app/single_instance.cpp
    src/dock/app_bar.cpp
    src/dock/dock_window.cpp
    src/settings/paths.cpp
    src/ui/webview_environment.cpp
    src/ui/webview_host.cpp
    src/utilities/strings.cpp
    src/wallpaper/wallpaper.cpp
    src/windows/app_launcher.cpp
    src/windows/icon_extractor.cpp
    src/windows/image_io.cpp
    src/windows/monitors.cpp
    src/windows/screen_capture.cpp
    src/windows/startup.cpp
    src/windows/tray_icon.cpp
    resources/mdu.rc
)
target_link_libraries(mdu PRIVATE
    mdu_core webview2
    comctl32 dwmapi gdi32 shcore shlwapi shell32 ole32 windowscodecs advapi32 user32 version)
target_compile_definitions(mdu PRIVATE
    MDU_VERSION="${PROJECT_VERSION}"
    _WIN32_WINNT=0x0A00 WINVER=0x0A00)
# The resource script reads the version from a generated header.
configure_file(resources/version.h.in ${CMAKE_CURRENT_BINARY_DIR}/generated/version.h @ONLY)
target_include_directories(mdu PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)
mdu_set_warnings(mdu)

if(MSVC)
    # The manifest is embedded through the resource script.
    target_link_options(mdu PRIVATE /MANIFEST:NO)
endif()

# Compile the TypeScript frontend and lay out web/ and themes/ next to mdu.exe,
# matching the installed layout.
find_program(NPM_EXECUTABLE NAMES npm.cmd npm REQUIRED)
set(MDU_WEB_SOURCES
    ${PROJECT_SOURCE_DIR}/web/scripts/bridge.ts
    ${PROJECT_SOURCE_DIR}/web/scripts/dock.ts
    ${PROJECT_SOURCE_DIR}/web/tsconfig.json
    ${PROJECT_SOURCE_DIR}/web/package.json)
set(MDU_WEB_STAMP ${CMAKE_CURRENT_BINARY_DIR}/web.stamp)
add_custom_command(
    OUTPUT ${MDU_WEB_STAMP}
    COMMAND ${NPM_EXECUTABLE} ci --no-audit --no-fund
    COMMAND ${NPM_EXECUTABLE} run build
    COMMAND ${CMAKE_COMMAND} -E touch ${MDU_WEB_STAMP}
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}/web
    DEPENDS ${MDU_WEB_SOURCES} ${PROJECT_SOURCE_DIR}/web/package-lock.json
    COMMENT "Compiling the TypeScript frontend"
    VERBATIM)
add_custom_target(mdu_web DEPENDS ${MDU_WEB_STAMP})
add_dependencies(mdu mdu_web)

add_custom_command(TARGET mdu POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
        ${PROJECT_SOURCE_DIR}/web/styles $<TARGET_FILE_DIR:mdu>/web/styles
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
        ${PROJECT_SOURCE_DIR}/web/dist/scripts $<TARGET_FILE_DIR:mdu>/web/scripts
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${PROJECT_SOURCE_DIR}/web/index.html $<TARGET_FILE_DIR:mdu>/web/index.html
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
        ${PROJECT_SOURCE_DIR}/themes $<TARGET_FILE_DIR:mdu>/themes
    VERBATIM)

install(TARGETS mdu RUNTIME DESTINATION .)
install(DIRECTORY ${PROJECT_SOURCE_DIR}/web/styles DESTINATION web)
install(DIRECTORY ${PROJECT_SOURCE_DIR}/web/dist/scripts DESTINATION web)
install(FILES ${PROJECT_SOURCE_DIR}/web/index.html DESTINATION web)
# User-supplied wallpaper images are never packaged.
install(DIRECTORY ${PROJECT_SOURCE_DIR}/themes DESTINATION . REGEX "/wallpaper/.*\\.(jpe?g|png)$" EXCLUDE)
install(FILES ${PROJECT_SOURCE_DIR}/LICENSE DESTINATION .)

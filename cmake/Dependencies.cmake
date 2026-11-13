# Third-party dependencies. Every version is pinned and every download is
# verified by hash. Upgrade deliberately, never implicitly.

include(FetchContent)

# nlohmann/json - JSON parsing for configuration, themes and frontend messages.
# Header-only, widely used, and far safer than a hand-written parser.
FetchContent_Declare(nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
    URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
set(JSON_Install OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(nlohmann_json)

# Microsoft WebView2 SDK - headers and loader for the WebView2 runtime that
# ships with Windows 11. Distributed as a NuGet package (a zip archive).
if(MDU_BUILD_APP)
    set(MDU_WEBVIEW2_VERSION 1.0.4314.35)
    FetchContent_Declare(webview2
        URL https://api.nuget.org/v3-flatcontainer/microsoft.web.webview2/${MDU_WEBVIEW2_VERSION}/microsoft.web.webview2.${MDU_WEBVIEW2_VERSION}.nupkg
        URL_HASH SHA256=90778deb1b77e36292828b4b5833c9255c27533f40e0ddd55891ae718944edc4
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        DOWNLOAD_NAME webview2.zip)
    FetchContent_MakeAvailable(webview2)

    if(CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64|arm64|aarch64" OR CMAKE_GENERATOR_PLATFORM STREQUAL "ARM64")
        set(_webview2_arch arm64)
    else()
        set(_webview2_arch x64)
    endif()

    # The static loader avoids shipping WebView2Loader.dll next to the executable.
    add_library(webview2 INTERFACE)
    target_include_directories(webview2 SYSTEM INTERFACE ${webview2_SOURCE_DIR}/build/native/include)
    target_link_libraries(webview2 INTERFACE
        ${webview2_SOURCE_DIR}/build/native/${_webview2_arch}/WebView2LoaderStatic.lib)
endif()

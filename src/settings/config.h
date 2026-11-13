#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace mdu {

enum class DockEdge { Bottom, Left, Right };
enum class DockMonitors { All, Primary };
enum class DockPlacement {
    Reserve,  // registers an app bar so maximized windows stop above the dock
    Overlay,  // floats over windows without changing the work area
};
enum class MotionPreference { System, Reduced, Full };

struct PinnedItem {
    std::string id;         // stable identifier used by the frontend, [a-z0-9_-]
    std::string name;       // display name
    std::string target;     // file, folder, shortcut or URI; environment variables are expanded
    std::string arguments;  // optional command-line arguments
};

struct DockSettings {
    bool enabled = true;
    DockEdge edge = DockEdge::Bottom;
    DockMonitors monitors = DockMonitors::All;
    DockPlacement placement = DockPlacement::Reserve;
    int iconSize = 48;  // DIPs
    int margin = 4;     // DIPs between the dock and the screen edge
};

struct WallpaperSettings {
    bool enabled = false;
    // Absolute path, or a path relative to the active theme directory.
    // Empty means "use the theme's wallpaper".
    std::string image;
};

struct Config {
    bool startWithWindows = true;
    std::string theme = "miku";
    MotionPreference motion = MotionPreference::System;
    DockSettings dock;
    WallpaperSettings wallpaper;
    std::vector<PinnedItem> pinned;
};

struct ConfigLoadResult {
    Config config;
    std::vector<std::string> warnings;
    bool fileWasInvalid = false;  // the file existed but was not valid JSON
};

Config defaultConfig();

// Parses a configuration document. Never throws: unknown or invalid values fall
// back to their defaults and produce a warning.
ConfigLoadResult parseConfig(std::string_view json);
std::string serializeConfig(const Config& config);

// A missing file yields the default configuration.
ConfigLoadResult loadConfigFile(const std::filesystem::path& path);

// Writes atomically (temporary file + rename) so a crash mid-write cannot
// corrupt the existing configuration.
bool saveConfigFile(const std::filesystem::path& path, const Config& config, std::string& error);

bool isValidItemId(std::string_view id);

}  // namespace mdu
